<?php
/**
 * requests.php — Song Request & Dedication API
 *
 * File:    src/linux/web_ui/app/api/requests.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Purpose: We handle song request submissions (public), request management (admin),
 *          and dedication submissions/management for Phase L11.
 *
 * Actions:
 *  submit             — PUBLIC (no auth), create a song request with rate limiting
 *  list               — admin, list pending/all requests with pagination
 *  approve            — admin, set status=approved, optionally match to track_id
 *  reject             — admin, set status=rejected
 *  mark_played        — admin, set status=played
 *  submit_dedication  — PUBLIC (no auth), create a dedication with rate limiting
 *  list_dedications   — admin, list dedications with optional status filter
 *  approve_dedication — admin, approve a dedication
 *  reject_dedication  — admin, reject a dedication
 *  stats              — admin, get request statistics for today
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We use Mc1Db trait for all database access
 *  - We use first-person plural in all comments
 *  - We use mc1_api_respond() for all JSON responses
 *  - Rate limiting: max 3 requests per IP per hour (checked in DB)
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/../inc/mc1_config.php';
require_once __DIR__ . '/../inc/db.php';
require_once __DIR__ . '/../inc/traits.db.class.php';
require_once __DIR__ . '/../inc/logger.php';
require_once __DIR__ . '/../inc/auth.php';
require_once __DIR__ . '/../inc/user_auth.php';

header('Content-Type: application/json');

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    mc1_api_respond(['error' => 'POST required'], 405);
    return;
}

$input  = json_decode(file_get_contents('php://input'), true) ?: [];
$action = $input['action'] ?? '';

/* ── We define a helper class using the Mc1Db trait ── */
class SongRequests {
    use Mc1Db;

    /**
     * We check rate limiting: max 3 requests per IP in the last hour.
     */
    public static function isRateLimited(string $ip, string $table = 'song_requests'): bool
    {
        $count = (int) self::scalar(
            'mcaster1_media',
            "SELECT COUNT(*) FROM $table WHERE ip_address = ? AND created_at > DATE_SUB(NOW(), INTERVAL 1 HOUR)",
            [$ip]
        );
        return $count >= 3;
    }

    /**
     * We submit a new song request (public, no auth).
     */
    public static function submit(array $input, string $ip): array
    {
        if (self::isRateLimited($ip, 'song_requests')) {
            return ['ok' => false, 'error' => 'Rate limit exceeded. Please wait before submitting another request.'];
        }

        $name    = trim($input['listener_name'] ?? '') ?: 'Anonymous';
        $email   = trim($input['listener_email'] ?? '') ?: null;
        $title   = trim($input['track_title'] ?? '');
        $artist  = trim($input['track_artist'] ?? '') ?: null;
        $message = trim($input['message'] ?? '') ?: null;
        $slot_id = isset($input['slot_id']) ? (int) $input['slot_id'] : null;

        /* We validate the CAPTCHA answer */
        $captcha_a = (int) ($input['captcha_answer'] ?? 0);
        $captcha_e = (int) ($input['captcha_expected'] ?? -1);
        if ($captcha_a !== $captcha_e || $captcha_e < 0) {
            return ['ok' => false, 'error' => 'Incorrect answer to the math question. Please try again.'];
        }

        if ($title === '') {
            return ['ok' => false, 'error' => 'Song title is required.'];
        }
        if (mb_strlen($name) > 128) {
            return ['ok' => false, 'error' => 'Name is too long (max 128 characters).'];
        }
        if (mb_strlen($title) > 255) {
            return ['ok' => false, 'error' => 'Title is too long (max 255 characters).'];
        }

        /* We try to match the request to an existing track in the library */
        $track_id = null;
        try {
            $match = self::row(
                'mcaster1_media',
                'SELECT id FROM tracks WHERE title LIKE ? AND (artist LIKE ? OR ? IS NULL) LIMIT 1',
                ['%' . $title . '%', '%' . ($artist ?? '') . '%', $artist]
            );
            if ($match) {
                $track_id = (int) $match['id'];
            }
        } catch (\Exception $e) {
            /* We silently continue — matching is best-effort */
        }

        self::run(
            'mcaster1_media',
            'INSERT INTO song_requests (listener_name, listener_email, track_title, track_artist, message, slot_id, track_id, ip_address)
             VALUES (?, ?, ?, ?, ?, ?, ?, ?)',
            [$name, $email, $title, $artist, $message, $slot_id, $track_id, $ip]
        );

        return ['ok' => true, 'message' => 'Your song request has been submitted!', 'matched' => $track_id !== null];
    }

    /**
     * We list requests with optional status filter and pagination (admin).
     */
    public static function listRequests(array $input): array
    {
        $status = $input['status'] ?? null;
        $page   = max(1, (int) ($input['page'] ?? 1));
        $limit  = min(100, max(10, (int) ($input['limit'] ?? 25)));
        $offset = ($page - 1) * $limit;

        $where  = [];
        $params = [];
        if ($status && in_array($status, ['pending', 'approved', 'played', 'rejected'])) {
            $where[]  = 'status = ?';
            $params[] = $status;
        }

        $wc = !empty($where) ? 'WHERE ' . implode(' AND ', $where) : '';

        $total = (int) self::scalar('mcaster1_media', "SELECT COUNT(*) FROM song_requests $wc", $params);

        $pq = $params;
        $pq[] = $limit;
        $pq[] = $offset;
        $rows = self::rows(
            'mcaster1_media',
            "SELECT sr.*, t.file_path as matched_file
             FROM song_requests sr
             LEFT JOIN tracks t ON t.id = sr.track_id
             $wc
             ORDER BY sr.created_at DESC LIMIT ? OFFSET ?",
            $pq
        );

        return [
            'ok'    => true,
            'rows'  => $rows,
            'total' => $total,
            'page'  => $page,
            'pages' => max(1, (int) ceil($total / $limit)),
        ];
    }

    /**
     * We approve a request, optionally setting a track_id match (admin).
     */
    public static function approve(array $input): array
    {
        $id       = (int) ($input['id'] ?? 0);
        $track_id = isset($input['track_id']) ? (int) $input['track_id'] : null;

        if ($id < 1) return ['ok' => false, 'error' => 'Invalid request ID.'];

        $sql = 'UPDATE song_requests SET status = ?, processed_at = NOW()';
        $params = ['approved'];
        if ($track_id !== null) {
            $sql .= ', track_id = ?';
            $params[] = $track_id;
        }
        $sql .= ' WHERE id = ?';
        $params[] = $id;

        self::run('mcaster1_media', $sql, $params);
        return ['ok' => true];
    }

    /**
     * We reject a request (admin).
     */
    public static function reject(array $input): array
    {
        $id = (int) ($input['id'] ?? 0);
        if ($id < 1) return ['ok' => false, 'error' => 'Invalid request ID.'];
        self::run('mcaster1_media', 'UPDATE song_requests SET status = ?, processed_at = NOW() WHERE id = ?', ['rejected', $id]);
        return ['ok' => true];
    }

    /**
     * We mark a request as played (admin).
     */
    public static function markPlayed(array $input): array
    {
        $id = (int) ($input['id'] ?? 0);
        if ($id < 1) return ['ok' => false, 'error' => 'Invalid request ID.'];
        self::run('mcaster1_media', 'UPDATE song_requests SET status = ?, processed_at = NOW() WHERE id = ?', ['played', $id]);
        return ['ok' => true];
    }

    /**
     * We submit a dedication (public, no auth).
     */
    public static function submitDedication(array $input, string $ip): array
    {
        if (self::isRateLimited($ip, 'dedications')) {
            return ['ok' => false, 'error' => 'Rate limit exceeded. Please wait before submitting another dedication.'];
        }

        $name    = trim($input['listener_name'] ?? '');
        $to      = trim($input['dedication_to'] ?? '');
        $message = trim($input['message'] ?? '');
        $title   = trim($input['track_title'] ?? '') ?: null;
        $slot_id = isset($input['slot_id']) ? (int) $input['slot_id'] : null;

        /* We validate the CAPTCHA answer */
        $captcha_a = (int) ($input['captcha_answer'] ?? 0);
        $captcha_e = (int) ($input['captcha_expected'] ?? -1);
        if ($captcha_a !== $captcha_e || $captcha_e < 0) {
            return ['ok' => false, 'error' => 'Incorrect answer to the math question. Please try again.'];
        }

        if ($name === '' || $to === '' || $message === '') {
            return ['ok' => false, 'error' => 'Name, dedication recipient, and message are all required.'];
        }

        self::run(
            'mcaster1_media',
            'INSERT INTO dedications (listener_name, dedication_to, message, track_title, slot_id, ip_address)
             VALUES (?, ?, ?, ?, ?, ?)',
            [$name, $to, $message, $title, $slot_id, $ip]
        );

        return ['ok' => true, 'message' => 'Your dedication has been submitted!'];
    }

    /**
     * We list dedications with optional status filter (admin).
     */
    public static function listDedications(array $input): array
    {
        $status = $input['status'] ?? null;
        $page   = max(1, (int) ($input['page'] ?? 1));
        $limit  = min(100, max(10, (int) ($input['limit'] ?? 25)));
        $offset = ($page - 1) * $limit;

        $where  = [];
        $params = [];
        if ($status && in_array($status, ['pending', 'approved', 'read', 'rejected'])) {
            $where[]  = 'status = ?';
            $params[] = $status;
        }

        $wc    = !empty($where) ? 'WHERE ' . implode(' AND ', $where) : '';
        $total = (int) self::scalar('mcaster1_media', "SELECT COUNT(*) FROM dedications $wc", $params);

        $pq = $params;
        $pq[] = $limit;
        $pq[] = $offset;
        $rows = self::rows(
            'mcaster1_media',
            "SELECT * FROM dedications $wc ORDER BY created_at DESC LIMIT ? OFFSET ?",
            $pq
        );

        return ['ok' => true, 'rows' => $rows, 'total' => $total, 'page' => $page, 'pages' => max(1, (int) ceil($total / $limit))];
    }

    /**
     * We approve a dedication (admin).
     */
    public static function approveDedication(array $input): array
    {
        $id = (int) ($input['id'] ?? 0);
        if ($id < 1) return ['ok' => false, 'error' => 'Invalid dedication ID.'];
        self::run('mcaster1_media', "UPDATE dedications SET status = 'approved' WHERE id = ?", [$id]);
        return ['ok' => true];
    }

    /**
     * We reject a dedication (admin).
     */
    public static function rejectDedication(array $input): array
    {
        $id = (int) ($input['id'] ?? 0);
        if ($id < 1) return ['ok' => false, 'error' => 'Invalid dedication ID.'];
        self::run('mcaster1_media', "UPDATE dedications SET status = 'rejected' WHERE id = ?", [$id]);
        return ['ok' => true];
    }

    /**
     * We get request statistics for the dashboard (admin).
     */
    public static function stats(): array
    {
        $today_total = (int) self::scalar('mcaster1_media', "SELECT COUNT(*) FROM song_requests WHERE DATE(created_at) = CURDATE()");
        $pending     = (int) self::scalar('mcaster1_media', "SELECT COUNT(*) FROM song_requests WHERE status = 'pending'");
        $approved    = (int) self::scalar('mcaster1_media', "SELECT COUNT(*) FROM song_requests WHERE status = 'approved' AND DATE(created_at) = CURDATE()");
        $played      = (int) self::scalar('mcaster1_media', "SELECT COUNT(*) FROM song_requests WHERE status = 'played' AND DATE(created_at) = CURDATE()");
        $rejected    = (int) self::scalar('mcaster1_media', "SELECT COUNT(*) FROM song_requests WHERE status = 'rejected' AND DATE(created_at) = CURDATE()");

        $ded_pending = (int) self::scalar('mcaster1_media', "SELECT COUNT(*) FROM dedications WHERE status = 'pending'");
        $ded_total   = (int) self::scalar('mcaster1_media', "SELECT COUNT(*) FROM dedications WHERE DATE(created_at) = CURDATE()");

        return [
            'ok'           => true,
            'today_total'  => $today_total,
            'pending'      => $pending,
            'approved'     => $approved,
            'played'       => $played,
            'rejected'     => $rejected,
            'ded_pending'  => $ded_pending,
            'ded_total'    => $ded_total,
        ];
    }
}

/* ── We route actions — public actions skip auth, admin actions require it ── */
$public_actions = ['submit', 'submit_dedication'];
$admin_actions  = ['list', 'approve', 'reject', 'mark_played', 'list_dedications', 'approve_dedication', 'reject_dedication', 'stats'];

if (in_array($action, $admin_actions)) {
    if (!mc1_is_authed()) {
        mc1_api_respond(['error' => 'Unauthorized'], 403);
        return;
    }
}

$ip = $_SERVER['REMOTE_ADDR'] ?? '0.0.0.0';

try {
    switch ($action) {
        /* ── Public actions ── */
        case 'submit':
            mc1_api_respond(SongRequests::submit($input, $ip));
            return;

        case 'submit_dedication':
            mc1_api_respond(SongRequests::submitDedication($input, $ip));
            return;

        /* ── Admin actions ── */
        case 'list':
            mc1_api_respond(SongRequests::listRequests($input));
            return;

        case 'approve':
            mc1_api_respond(SongRequests::approve($input));
            return;

        case 'reject':
            mc1_api_respond(SongRequests::reject($input));
            return;

        case 'mark_played':
            mc1_api_respond(SongRequests::markPlayed($input));
            return;

        case 'list_dedications':
            mc1_api_respond(SongRequests::listDedications($input));
            return;

        case 'approve_dedication':
            mc1_api_respond(SongRequests::approveDedication($input));
            return;

        case 'reject_dedication':
            mc1_api_respond(SongRequests::rejectDedication($input));
            return;

        case 'stats':
            mc1_api_respond(SongRequests::stats());
            return;

        default:
            mc1_api_respond(['error' => 'Unknown action: ' . $action], 400);
            return;
    }
} catch (\Exception $e) {
    mc1_log(MC1_LOG_ERROR, 'requests.php error: ' . $e->getMessage(), 'requests');
    mc1_api_respond(['error' => mc1_safe_error($e, 'Request processing failed')], 500);
    return;
}
