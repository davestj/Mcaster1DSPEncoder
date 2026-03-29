<?php
/**
 * remote.php -- Remote Podcast Recording Session API
 *
 * File:    src/linux/web_ui/app/api/remote.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Phase:   PC-7
 * Purpose: We provide session management for remote podcast recording —
 *          create/join sessions, chat, hand raise, track upload, and
 *          participant management. Guests join via invite code without auth.
 *
 * Actions (all POST JSON):
 *  create_session    — Host creates a new recording session, returns invite code
 *  join_session      — Guest joins with code + name (NO auth required for guests)
 *  get_session       — Get session details + participant list
 *  start_recording   — Host starts recording all participants
 *  stop_recording    — Host stops, finalizes track files
 *  end_session       — Host ends session, disconnects all
 *  send_chat         — Send chat message (host or guest)
 *  get_chat          — Get recent chat messages (since timestamp)
 *  hand_raise        — Guest signals hand raise
 *  list_sessions     — Host lists their sessions
 *  upload_track      — Participant uploads recorded audio blob
 *  update_level      — Participant sends current audio level (RMS)
 *  get_levels        — Host polls all participant levels
 *  heartbeat         — Participant pings to stay connected
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We use Mc1Db trait for all database access
 *  - We use first-person plural throughout all comments
 *  - We use raw SQL only, no ORMs
 *  - Guest actions (join_session, send_chat, get_chat, hand_raise, upload_track,
 *    update_level, heartbeat) require participant_id + session_code but NOT mc1session auth
 *  - Host actions require mc1_is_authed()
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/../inc/mc1_config.php';
require_once __DIR__ . '/../inc/db.php';
require_once __DIR__ . '/../inc/traits.db.class.php';
require_once __DIR__ . '/../inc/logger.php';
require_once __DIR__ . '/../inc/auth.php';
require_once __DIR__ . '/../inc/user_auth.php';

header('Content-Type: application/json; charset=utf-8');

$raw    = file_get_contents('php://input');
$req    = ($raw !== '') ? json_decode($raw, true) : [];
if (!is_array($req)) $req = [];
$action = (string)($req['action'] ?? '');

/* ── Helper: generate random alphanumeric code ──────────────────────────── */

function rc_gen_code(int $len = 8): string
{
    $chars = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789'; // no 0/O/1/I ambiguity
    $code  = '';
    for ($i = 0; $i < $len; $i++) {
        $code .= $chars[random_int(0, strlen($chars) - 1)];
    }
    return $code;
}

/* ── Helper: validate guest identity (session_code + participant_id) ───── */

function rc_validate_guest(array $req): ?array
{
    $code = trim($req['session_code'] ?? '');
    $pid  = (int)($req['participant_id'] ?? 0);
    if ($code === '' || $pid < 1) return null;

    $db = mc1_db('mcaster1_media');
    $st = $db->prepare('SELECT s.id AS session_id, s.status, p.id AS pid, p.name, p.role
                         FROM remote_sessions s
                         JOIN remote_participants p ON p.session_id = s.id
                         WHERE s.session_code = ? AND p.id = ? AND s.status != "ended"');
    $st->execute([$code, $pid]);
    $row = $st->fetch(\PDO::FETCH_ASSOC);
    return $row ?: null;
}

/* ── Helper: base invite URL ─────────────────────────────────────────── */

function rc_invite_url(string $code): string
{
    $proto = (!empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off') ? 'https' : 'http';
    $host  = $_SERVER['HTTP_HOST'] ?? 'encoder.mcaster1.com:8344';
    return $proto . '://' . $host . '/join/' . $code;
}

/* ── Helper: recording upload directory ─────────────────────────────── */

function rc_upload_dir(int $session_id): string
{
    $dir = MC1_AUDIO_ROOT . '/remote_recordings/' . $session_id;
    if (!is_dir($dir)) mkdir($dir, 0775, true);
    return $dir;
}

/* ── Guest actions (no mc1session auth required) ────────────────────────── */

$guest_actions = ['join_session', 'send_chat', 'get_chat', 'hand_raise',
                  'upload_track', 'update_level', 'heartbeat', 'get_session_public'];

if (in_array($action, $guest_actions, true)) {
    // We handle guest actions separately — no auth gate
    $db = mc1_db('mcaster1_media');

    // ── join_session ──────────────────────────────────────────────────────
    if ($action === 'join_session') {
        $code = strtoupper(trim($req['session_code'] ?? ''));
        $name = trim($req['name'] ?? '');

        if ($code === '' || $name === '') {
            http_response_code(400);
            echo json_encode(['ok' => false, 'error' => 'Session code and name are required']);
            return;
        }

        // We find the session
        $st = $db->prepare('SELECT id, status, max_participants, title FROM remote_sessions WHERE session_code = ?');
        $st->execute([$code]);
        $sess = $st->fetch(\PDO::FETCH_ASSOC);

        if (!$sess) {
            http_response_code(404);
            echo json_encode(['ok' => false, 'error' => 'Session not found. Check your invite code.']);
            return;
        }
        if ($sess['status'] === 'ended') {
            http_response_code(410);
            echo json_encode(['ok' => false, 'error' => 'This session has ended']);
            return;
        }

        // We check participant count
        $st2 = $db->prepare('SELECT COUNT(*) FROM remote_participants WHERE session_id = ?');
        $st2->execute([$sess['id']]);
        $count = (int)$st2->fetchColumn();

        if ($count >= (int)$sess['max_participants']) {
            http_response_code(409);
            echo json_encode(['ok' => false, 'error' => 'Session is full (max ' . $sess['max_participants'] . ' participants)']);
            return;
        }

        // We insert the participant
        $st3 = $db->prepare('INSERT INTO remote_participants (session_id, name, role, is_connected, joined_at)
                              VALUES (?, ?, "guest", TRUE, NOW())');
        $st3->execute([$sess['id'], $name]);
        $pid = (int)$db->lastInsertId();

        echo json_encode([
            'ok'             => true,
            'participant_id' => $pid,
            'session_id'     => (int)$sess['id'],
            'session_title'  => $sess['title'],
            'session_status' => $sess['status'],
        ]);
        return;
    }

    // ── get_session_public (guest view — limited info) ────────────────────
    if ($action === 'get_session_public') {
        $code = strtoupper(trim($req['session_code'] ?? ''));
        if ($code === '') {
            http_response_code(400);
            echo json_encode(['ok' => false, 'error' => 'session_code required']);
            return;
        }

        $st = $db->prepare('SELECT id, session_code, title, status, max_participants, started_at, created_at
                             FROM remote_sessions WHERE session_code = ?');
        $st->execute([$code]);
        $sess = $st->fetch(\PDO::FETCH_ASSOC);
        if (!$sess) {
            http_response_code(404);
            echo json_encode(['ok' => false, 'error' => 'Session not found']);
            return;
        }

        // We get participant list
        $st2 = $db->prepare('SELECT id, name, role, is_connected, joined_at FROM remote_participants
                              WHERE session_id = ? ORDER BY role ASC, joined_at ASC');
        $st2->execute([$sess['id']]);
        $parts = $st2->fetchAll(\PDO::FETCH_ASSOC);

        // We find host name
        $host_name = 'Host';
        foreach ($parts as $p) {
            if ($p['role'] === 'host') { $host_name = $p['name']; break; }
        }

        echo json_encode([
            'ok'           => true,
            'session'      => $sess,
            'participants' => $parts,
            'host_name'    => $host_name,
        ]);
        return;
    }

    // ── send_chat ─────────────────────────────────────────────────────────
    if ($action === 'send_chat') {
        $guest = rc_validate_guest($req);
        if (!$guest) {
            http_response_code(403);
            echo json_encode(['ok' => false, 'error' => 'Invalid session or participant']);
            return;
        }
        $message = trim($req['message'] ?? '');
        if ($message === '') {
            http_response_code(400);
            echo json_encode(['ok' => false, 'error' => 'Message cannot be empty']);
            return;
        }
        if (strlen($message) > 2000) {
            $message = substr($message, 0, 2000);
        }

        $st = $db->prepare('INSERT INTO remote_chat (session_id, participant_id, message, sent_at) VALUES (?, ?, ?, NOW())');
        $st->execute([$guest['session_id'], $guest['pid'], $message]);

        echo json_encode(['ok' => true, 'chat_id' => (int)$db->lastInsertId()]);
        return;
    }

    // ── get_chat ──────────────────────────────────────────────────────────
    if ($action === 'get_chat') {
        $guest = rc_validate_guest($req);
        if (!$guest) {
            http_response_code(403);
            echo json_encode(['ok' => false, 'error' => 'Invalid session or participant']);
            return;
        }
        $since_id = (int)($req['since_id'] ?? 0);
        $limit    = min((int)($req['limit'] ?? 50), 200);

        $st = $db->prepare('SELECT c.id, c.participant_id, c.message, c.sent_at, p.name AS sender_name, p.role AS sender_role
                             FROM remote_chat c
                             JOIN remote_participants p ON p.id = c.participant_id
                             WHERE c.session_id = ? AND c.id > ?
                             ORDER BY c.id ASC LIMIT ?');
        $st->execute([$guest['session_id'], $since_id, $limit]);
        $msgs = $st->fetchAll(\PDO::FETCH_ASSOC);

        echo json_encode(['ok' => true, 'messages' => $msgs]);
        return;
    }

    // ── hand_raise ────────────────────────────────────────────────────────
    if ($action === 'hand_raise') {
        $guest = rc_validate_guest($req);
        if (!$guest) {
            http_response_code(403);
            echo json_encode(['ok' => false, 'error' => 'Invalid session or participant']);
            return;
        }

        // We insert a system chat message indicating hand raise
        $msg = '** ' . $guest['name'] . ' raised their hand **';
        $st  = $db->prepare('INSERT INTO remote_chat (session_id, participant_id, message, sent_at) VALUES (?, ?, ?, NOW())');
        $st->execute([$guest['session_id'], $guest['pid'], $msg]);

        echo json_encode(['ok' => true]);
        return;
    }

    // ── upload_track ──────────────────────────────────────────────────────
    if ($action === 'upload_track') {
        $guest = rc_validate_guest($req);
        if (!$guest) {
            http_response_code(403);
            echo json_encode(['ok' => false, 'error' => 'Invalid session or participant']);
            return;
        }

        // We expect a file upload via multipart form, or a base64 blob in JSON
        if (isset($_FILES['audio_track'])) {
            $file = $_FILES['audio_track'];
            if ($file['error'] !== UPLOAD_ERR_OK) {
                http_response_code(400);
                echo json_encode(['ok' => false, 'error' => 'Upload error: ' . $file['error']]);
                return;
            }

            $dir  = rc_upload_dir($guest['session_id']);
            $ext  = pathinfo($file['name'], PATHINFO_EXTENSION) ?: 'webm';
            $safe = preg_replace('/[^a-zA-Z0-9_-]/', '_', $guest['name']);
            $fname = 'track_' . $guest['pid'] . '_' . $safe . '_' . date('Ymd_His') . '.' . $ext;
            $dest = $dir . '/' . $fname;

            move_uploaded_file($file['tmp_name'], $dest);

            $st = $db->prepare('UPDATE remote_participants SET track_file_path = ?, track_size_bytes = ? WHERE id = ?');
            $st->execute([$dest, filesize($dest), $guest['pid']]);

            echo json_encode(['ok' => true, 'file_path' => $dest, 'file_size' => filesize($dest)]);
            return;
        }

        // We also accept base64-encoded audio in JSON body
        $b64 = $req['audio_base64'] ?? '';
        if ($b64 !== '') {
            $data = base64_decode($b64, true);
            if ($data === false) {
                http_response_code(400);
                echo json_encode(['ok' => false, 'error' => 'Invalid base64 data']);
                return;
            }

            $dir  = rc_upload_dir($guest['session_id']);
            $ext  = $req['format'] ?? 'webm';
            $safe = preg_replace('/[^a-zA-Z0-9_-]/', '_', $guest['name']);
            $fname = 'track_' . $guest['pid'] . '_' . $safe . '_' . date('Ymd_His') . '.' . $ext;
            $dest = $dir . '/' . $fname;

            file_put_contents($dest, $data);

            $duration = (int)($req['duration_sec'] ?? 0);
            $st = $db->prepare('UPDATE remote_participants SET track_file_path = ?, track_size_bytes = ?, track_duration_sec = ? WHERE id = ?');
            $st->execute([$dest, strlen($data), $duration, $guest['pid']]);

            echo json_encode(['ok' => true, 'file_path' => $dest, 'file_size' => strlen($data)]);
            return;
        }

        http_response_code(400);
        echo json_encode(['ok' => false, 'error' => 'No audio data provided (use audio_track file or audio_base64)']);
        return;
    }

    // ── update_level ──────────────────────────────────────────────────────
    if ($action === 'update_level') {
        // We store level data transiently in a shared file (no DB for per-500ms updates)
        $guest = rc_validate_guest($req);
        if (!$guest) {
            http_response_code(403);
            echo json_encode(['ok' => false, 'error' => 'Invalid session or participant']);
            return;
        }

        $level = max(0.0, min(1.0, (float)($req['level'] ?? 0)));
        $hand  = !empty($req['hand_raised']);

        // We write to a temp file per session for level aggregation
        $lfile = sys_get_temp_dir() . '/mc1_remote_levels_' . $guest['session_id'] . '.json';
        $levels = [];
        if (file_exists($lfile)) {
            $raw = file_get_contents($lfile);
            $levels = json_decode($raw, true) ?: [];
        }
        $levels[$guest['pid']] = [
            'level'        => $level,
            'name'         => $guest['name'],
            'role'         => $guest['role'],
            'hand_raised'  => $hand,
            'updated_at'   => time(),
        ];
        file_put_contents($lfile, json_encode($levels));

        echo json_encode(['ok' => true]);
        return;
    }

    // ── heartbeat ─────────────────────────────────────────────────────────
    if ($action === 'heartbeat') {
        $guest = rc_validate_guest($req);
        if (!$guest) {
            http_response_code(403);
            echo json_encode(['ok' => false, 'error' => 'Invalid session or participant']);
            return;
        }

        // We update connected status
        $st = $db->prepare('UPDATE remote_participants SET is_connected = TRUE WHERE id = ?');
        $st->execute([$guest['pid']]);

        // We return current session status so guest knows if recording started/ended
        $st2 = $db->prepare('SELECT status FROM remote_sessions WHERE id = ?');
        $st2->execute([$guest['session_id']]);
        $status = $st2->fetchColumn();

        echo json_encode(['ok' => true, 'session_status' => $status]);
        return;
    }

    // We should not reach here
    http_response_code(400);
    echo json_encode(['ok' => false, 'error' => 'Unknown guest action: ' . $action]);
    return;
}

/* ── Host actions (require mc1session auth) ─────────────────────────────── */

if (!mc1_is_authed()) {
    http_response_code(403);
    echo json_encode(['ok' => false, 'error' => 'Forbidden']);
    return;
}

$db   = mc1_db('mcaster1_media');
$user = function_exists('mc1_current_user') ? mc1_current_user() : null;
$uid  = $user ? (int)($user['id'] ?? 0) : 0;

// ── create_session ────────────────────────────────────────────────────────

if ($action === 'create_session') {
    $title = trim($req['title'] ?? 'Recording Session');
    $show_id = !empty($req['show_id']) ? (int)$req['show_id'] : null;
    $max    = max(2, min(10, (int)($req['max_participants'] ?? 4)));
    $settings = $req['settings'] ?? ['sample_rate' => 48000, 'channels' => 1, 'format' => 'wav'];

    // We generate a unique invite code
    $code = rc_gen_code(8);
    $tries = 0;
    while ($tries < 10) {
        $check = $db->prepare('SELECT COUNT(*) FROM remote_sessions WHERE session_code = ?');
        $check->execute([$code]);
        if ((int)$check->fetchColumn() === 0) break;
        $code = rc_gen_code(8);
        $tries++;
    }

    $st = $db->prepare('INSERT INTO remote_sessions (session_code, host_user_id, show_id, title, status, max_participants, settings_json, created_at)
                         VALUES (?, ?, ?, ?, "waiting", ?, ?, NOW())');
    $st->execute([$code, $uid, $show_id, $title, $max, json_encode($settings)]);
    $sid = (int)$db->lastInsertId();

    // We auto-join the host as first participant
    $host_name = $user ? ($user['display_name'] ?: $user['username']) : 'Host';
    $st2 = $db->prepare('INSERT INTO remote_participants (session_id, name, role, is_connected, joined_at)
                          VALUES (?, ?, "host", TRUE, NOW())');
    $st2->execute([$sid, $host_name]);
    $host_pid = (int)$db->lastInsertId();

    echo json_encode([
        'ok'              => true,
        'session_id'      => $sid,
        'session_code'    => $code,
        'invite_url'      => rc_invite_url($code),
        'host_participant_id' => $host_pid,
    ]);
    return;
}

// ── get_session (host — full details) ─────────────────────────────────────

if ($action === 'get_session') {
    $sid = (int)($req['session_id'] ?? 0);
    if ($sid < 1) {
        http_response_code(400);
        echo json_encode(['ok' => false, 'error' => 'session_id required']);
        return;
    }

    $st = $db->prepare('SELECT * FROM remote_sessions WHERE id = ? AND host_user_id = ?');
    $st->execute([$sid, $uid]);
    $sess = $st->fetch(\PDO::FETCH_ASSOC);
    if (!$sess) {
        http_response_code(404);
        echo json_encode(['ok' => false, 'error' => 'Session not found or not your session']);
        return;
    }

    // We get participants
    $st2 = $db->prepare('SELECT * FROM remote_participants WHERE session_id = ? ORDER BY role ASC, joined_at ASC');
    $st2->execute([$sid]);
    $parts = $st2->fetchAll(\PDO::FETCH_ASSOC);

    // We get levels from temp file
    $lfile = sys_get_temp_dir() . '/mc1_remote_levels_' . $sid . '.json';
    $levels = [];
    if (file_exists($lfile)) {
        $levels = json_decode(file_get_contents($lfile), true) ?: [];
        // We prune stale entries (older than 5 seconds)
        $now = time();
        foreach ($levels as $pid => $lv) {
            if (($now - ($lv['updated_at'] ?? 0)) > 5) {
                unset($levels[$pid]);
            }
        }
    }

    echo json_encode([
        'ok'           => true,
        'session'      => $sess,
        'participants' => $parts,
        'levels'       => $levels,
        'invite_url'   => rc_invite_url($sess['session_code']),
    ]);
    return;
}

// ── start_recording ───────────────────────────────────────────────────────

if ($action === 'start_recording') {
    $sid = (int)($req['session_id'] ?? 0);
    if ($sid < 1) {
        http_response_code(400);
        echo json_encode(['ok' => false, 'error' => 'session_id required']);
        return;
    }

    $st = $db->prepare('SELECT * FROM remote_sessions WHERE id = ? AND host_user_id = ?');
    $st->execute([$sid, $uid]);
    $sess = $st->fetch(\PDO::FETCH_ASSOC);
    if (!$sess) {
        http_response_code(404);
        echo json_encode(['ok' => false, 'error' => 'Session not found']);
        return;
    }
    if ($sess['status'] === 'ended') {
        http_response_code(409);
        echo json_encode(['ok' => false, 'error' => 'Session has ended']);
        return;
    }

    $st2 = $db->prepare('UPDATE remote_sessions SET status = "recording", started_at = NOW() WHERE id = ?');
    $st2->execute([$sid]);

    echo json_encode(['ok' => true, 'status' => 'recording']);
    return;
}

// ── stop_recording ────────────────────────────────────────────────────────

if ($action === 'stop_recording') {
    $sid = (int)($req['session_id'] ?? 0);
    if ($sid < 1) {
        http_response_code(400);
        echo json_encode(['ok' => false, 'error' => 'session_id required']);
        return;
    }

    $st = $db->prepare('SELECT * FROM remote_sessions WHERE id = ? AND host_user_id = ?');
    $st->execute([$sid, $uid]);
    $sess = $st->fetch(\PDO::FETCH_ASSOC);
    if (!$sess) {
        http_response_code(404);
        echo json_encode(['ok' => false, 'error' => 'Session not found']);
        return;
    }

    $st2 = $db->prepare('UPDATE remote_sessions SET status = "waiting" WHERE id = ?');
    $st2->execute([$sid]);

    // We get the track summary
    $st3 = $db->prepare('SELECT id, name, track_file_path, track_size_bytes, track_duration_sec
                          FROM remote_participants WHERE session_id = ? AND track_file_path IS NOT NULL');
    $st3->execute([$sid]);
    $tracks = $st3->fetchAll(\PDO::FETCH_ASSOC);

    echo json_encode([
        'ok'     => true,
        'status' => 'waiting',
        'tracks' => $tracks,
    ]);
    return;
}

// ── end_session ───────────────────────────────────────────────────────────

if ($action === 'end_session') {
    $sid = (int)($req['session_id'] ?? 0);
    if ($sid < 1) {
        http_response_code(400);
        echo json_encode(['ok' => false, 'error' => 'session_id required']);
        return;
    }

    $st = $db->prepare('UPDATE remote_sessions SET status = "ended", ended_at = NOW()
                         WHERE id = ? AND host_user_id = ?');
    $st->execute([$sid, $uid]);

    // We mark all participants as disconnected
    $st2 = $db->prepare('UPDATE remote_participants SET is_connected = FALSE, left_at = NOW() WHERE session_id = ?');
    $st2->execute([$sid]);

    // We clean up temp level file
    $lfile = sys_get_temp_dir() . '/mc1_remote_levels_' . $sid . '.json';
    if (file_exists($lfile)) unlink($lfile);

    echo json_encode(['ok' => true]);
    return;
}

// ── send_chat (host — uses auth user info) ────────────────────────────────

if ($action === 'send_chat') {
    $sid     = (int)($req['session_id'] ?? 0);
    $pid     = (int)($req['participant_id'] ?? 0);
    $message = trim($req['message'] ?? '');

    if ($sid < 1 || $pid < 1 || $message === '') {
        http_response_code(400);
        echo json_encode(['ok' => false, 'error' => 'session_id, participant_id, and message required']);
        return;
    }
    if (strlen($message) > 2000) $message = substr($message, 0, 2000);

    $st = $db->prepare('INSERT INTO remote_chat (session_id, participant_id, message, sent_at) VALUES (?, ?, ?, NOW())');
    $st->execute([$sid, $pid, $message]);

    echo json_encode(['ok' => true, 'chat_id' => (int)$db->lastInsertId()]);
    return;
}

// ── get_chat (host — uses session_id) ─────────────────────────────────────

if ($action === 'get_chat') {
    $sid      = (int)($req['session_id'] ?? 0);
    $since_id = (int)($req['since_id'] ?? 0);
    $limit    = min((int)($req['limit'] ?? 50), 200);

    if ($sid < 1) {
        http_response_code(400);
        echo json_encode(['ok' => false, 'error' => 'session_id required']);
        return;
    }

    $st = $db->prepare('SELECT c.id, c.participant_id, c.message, c.sent_at, p.name AS sender_name, p.role AS sender_role
                         FROM remote_chat c
                         JOIN remote_participants p ON p.id = c.participant_id
                         WHERE c.session_id = ? AND c.id > ?
                         ORDER BY c.id ASC LIMIT ?');
    $st->execute([$sid, $since_id, $limit]);
    $msgs = $st->fetchAll(\PDO::FETCH_ASSOC);

    echo json_encode(['ok' => true, 'messages' => $msgs]);
    return;
}

// ── get_levels (host polls all participant levels) ────────────────────────

if ($action === 'get_levels') {
    $sid = (int)($req['session_id'] ?? 0);
    if ($sid < 1) {
        http_response_code(400);
        echo json_encode(['ok' => false, 'error' => 'session_id required']);
        return;
    }

    $lfile = sys_get_temp_dir() . '/mc1_remote_levels_' . $sid . '.json';
    $levels = [];
    if (file_exists($lfile)) {
        $levels = json_decode(file_get_contents($lfile), true) ?: [];
        // We prune stale entries
        $now = time();
        foreach ($levels as $pid => $lv) {
            if (($now - ($lv['updated_at'] ?? 0)) > 5) {
                unset($levels[$pid]);
            }
        }
    }

    echo json_encode(['ok' => true, 'levels' => $levels]);
    return;
}

// ── list_sessions ─────────────────────────────────────────────────────────

if ($action === 'list_sessions') {
    $status = $req['status'] ?? null;

    $sql = 'SELECT s.*, COUNT(p.id) AS participant_count
            FROM remote_sessions s
            LEFT JOIN remote_participants p ON p.session_id = s.id
            WHERE s.host_user_id = ?';
    $params = [$uid];

    if ($status && in_array($status, ['waiting', 'recording', 'ended'], true)) {
        $sql .= ' AND s.status = ?';
        $params[] = $status;
    }
    $sql .= ' GROUP BY s.id ORDER BY s.created_at DESC LIMIT 50';

    $st = $db->prepare($sql);
    $st->execute($params);
    $sessions = $st->fetchAll(\PDO::FETCH_ASSOC);

    echo json_encode(['ok' => true, 'sessions' => $sessions]);
    return;
}

// ── Unknown action ────────────────────────────────────────────────────────

http_response_code(400);
echo json_encode(['ok' => false, 'error' => 'Unknown action: ' . $action]);
