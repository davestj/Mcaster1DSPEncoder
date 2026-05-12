<?php
/**
 * ads.php -- Podcast Monetization API (Dynamic Ad Insertion + Sponsor Management)
 *
 * File:    src/linux/web_ui/app/api/ads.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Purpose: We provide CRUD for ad campaigns, placements, sponsors, impression tracking,
 *          dynamic ad-inserted episode serving, and revenue analytics.
 *
 * Actions (POST JSON, auth required unless noted):
 *  -- Campaigns --
 *  list_campaigns      List all ad campaigns with stats
 *  create_campaign     Create a new ad campaign
 *  update_campaign     Update campaign settings
 *  delete_campaign     Delete a campaign and its placements
 *  upload_ad_audio     Upload an ad audio file to archives/ads/
 *
 *  -- Placements --
 *  list_placements     List ad placements for an episode
 *  add_placement       Assign a campaign to an episode position
 *  remove_placement    Remove a placement
 *  auto_place          Auto-assign ads based on targeting rules
 *
 *  -- Sponsors --
 *  list_sponsors       List all sponsors
 *  create_sponsor      Create a sponsor
 *  update_sponsor      Update sponsor info
 *  delete_sponsor      Delete a sponsor
 *
 *  -- Analytics --
 *  ad_stats            Per-campaign impressions, clicks, revenue
 *  revenue_summary     Total revenue by period
 *
 *  -- Dynamic Serving (GET, no auth) --
 *  serve_episode       Concatenate pre-roll + episode + mid-rolls + post-roll via ffmpeg
 *  track_impression    Record an impression/click/complete event (no auth)
 *  track_click         Redirect through click tracking URL
 *
 * Standards:
 *  - We never call exit() or die() -- uopz extension is active
 *  - We use Mc1Db trait for all database access
 *  - We use mc1_api_respond() for all JSON responses
 *  - We use mc1_is_authed() for auth gate (except public endpoints)
 */

define('MC1_BOOT', true);
$API_VERSION = '2.0.1';
require_once __DIR__ . '/../inc/mc1_config.php';
require_once __DIR__ . '/../inc/db.php';
require_once __DIR__ . '/../inc/traits.db.class.php';
require_once __DIR__ . '/../inc/logger.php';
require_once __DIR__ . '/../inc/auth.php';
require_once __DIR__ . '/../inc/user_auth.php';

/* ── Public endpoints (GET, no auth) ── */
if ($_SERVER['REQUEST_METHOD'] === 'GET') {
    $action = $_GET['action'] ?? '';

    if ($action === 'serve_episode') {
        AdsApi::serveEpisode((int)($_GET['episode_id'] ?? 0));
        return;
    }

    if ($action === 'track_impression') {
        AdsApi::trackImpression($_GET);
        return;
    }

    if ($action === 'track_click') {
        AdsApi::trackClick($_GET);
        return;
    }

    http_response_code(400);
    header('Content-Type: application/json');
    echo json_encode(['error' => 'Unknown GET action']);
    return;
}

/* ── Auth-required POST endpoints ── */
header('Content-Type: application/json');

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    mc1_api_respond(['error' => 'POST required'], 405);
    return;
}

$raw  = file_get_contents('php://input');
$data = json_decode($raw, true);
if (!is_array($data) || empty($data['action'])) {
    mc1_api_respond(['error' => 'Missing action'], 400);
    return;
}

$action = $data['action'];

if (!mc1_is_authed()) {
    mc1_api_respond(['error' => 'Unauthorized'], 403);
    return;
}

/* ── Ads API class ── */
class AdsApi {
    use Mc1Db;

    const DB = 'mcaster1_media';

    /* We store ad audio files in archives/ads/ */
    public static function adsDir(): string
    {
        $base = defined('MC1_ARCHIVE_DIR')
            ? MC1_ARCHIVE_DIR
            : '/var/www/mcaster1.com/Mcaster1DSPEncoder/archives';
        $dir = $base . '/ads';
        if (!is_dir($dir)) {
            @mkdir($dir, 0775, true);
        }
        return $dir;
    }

    /* We cache dynamically served episodes here */
    public static function cacheDir(): string
    {
        $dir = self::adsDir() . '/cache';
        if (!is_dir($dir)) {
            @mkdir($dir, 0775, true);
        }
        return $dir;
    }

    /* ────────────────────────────────────────────────────
       CAMPAIGNS
       ──────────────────────────────────────────────────── */

    public static function listCampaigns(): array
    {
        $rows = self::rows(self::DB,
            "SELECT c.*,
                    (SELECT COUNT(*) FROM ad_impressions i WHERE i.campaign_id = c.id AND i.event = 'impression') AS total_impressions,
                    (SELECT COUNT(*) FROM ad_impressions i WHERE i.campaign_id = c.id AND i.event = 'click') AS total_clicks,
                    (SELECT COUNT(*) FROM ad_placements p WHERE p.campaign_id = c.id) AS placement_count
             FROM ad_campaigns c ORDER BY c.created_at DESC"
        );
        return ['ok' => true, 'campaigns' => $rows];
    }

    public static function createCampaign(array $data): array
    {
        $name = trim($data['name'] ?? '');
        if ($name === '') return ['error' => 'Campaign name is required'];

        $type = $data['type'] ?? 'pre_roll';
        if (!in_array($type, ['pre_roll', 'mid_roll', 'post_roll'])) {
            return ['error' => 'Invalid type. Must be pre_roll, mid_roll, or post_roll'];
        }

        $audio_path = trim($data['audio_file_path'] ?? '');
        if ($audio_path === '') return ['error' => 'Audio file path is required'];

        $duration = (int)($data['duration_sec'] ?? 0);
        if ($duration < 1) return ['error' => 'Duration must be at least 1 second'];

        $targeting = null;
        if (!empty($data['targeting_json'])) {
            $targeting = is_string($data['targeting_json'])
                ? $data['targeting_json']
                : json_encode($data['targeting_json']);
        }

        self::run(self::DB,
            "INSERT INTO ad_campaigns (name, advertiser, type, audio_file_path, duration_sec,
             click_url, max_impressions, cpm_rate, is_active, start_date, end_date, targeting_json)
             VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            [
                $name,
                $data['advertiser'] ?? '',
                $type,
                $audio_path,
                $duration,
                $data['click_url'] ?? '',
                !empty($data['max_impressions']) ? (int)$data['max_impressions'] : null,
                (float)($data['cpm_rate'] ?? 0),
                (int)($data['is_active'] ?? 1),
                $data['start_date'] ?? null,
                $data['end_date'] ?? null,
                $targeting,
            ]
        );
        return ['ok' => true, 'id' => (int)self::lastId(self::DB), 'message' => 'Campaign created'];
    }

    public static function updateCampaign(array $data): array
    {
        $id = (int)($data['id'] ?? 0);
        if ($id < 1) return ['error' => 'Missing campaign id'];

        $name = trim($data['name'] ?? '');
        if ($name === '') return ['error' => 'Campaign name is required'];

        $type = $data['type'] ?? 'pre_roll';
        if (!in_array($type, ['pre_roll', 'mid_roll', 'post_roll'])) {
            return ['error' => 'Invalid type'];
        }

        $targeting = null;
        if (!empty($data['targeting_json'])) {
            $targeting = is_string($data['targeting_json'])
                ? $data['targeting_json']
                : json_encode($data['targeting_json']);
        }

        self::run(self::DB,
            "UPDATE ad_campaigns SET name=?, advertiser=?, type=?, audio_file_path=?,
             duration_sec=?, click_url=?, max_impressions=?, cpm_rate=?, is_active=?,
             start_date=?, end_date=?, targeting_json=?
             WHERE id=?",
            [
                $name,
                $data['advertiser'] ?? '',
                $type,
                $data['audio_file_path'] ?? '',
                (int)($data['duration_sec'] ?? 0),
                $data['click_url'] ?? '',
                !empty($data['max_impressions']) ? (int)$data['max_impressions'] : null,
                (float)($data['cpm_rate'] ?? 0),
                (int)($data['is_active'] ?? 1),
                $data['start_date'] ?? null,
                $data['end_date'] ?? null,
                $targeting,
                $id,
            ]
        );
        return ['ok' => true, 'message' => 'Campaign updated'];
    }

    public static function deleteCampaign(array $data): array
    {
        $id = (int)($data['id'] ?? 0);
        if ($id < 1) return ['error' => 'Missing campaign id'];

        self::run(self::DB, "DELETE FROM ad_placements WHERE campaign_id = ?", [$id]);
        self::run(self::DB, "DELETE FROM ad_campaigns WHERE id = ?", [$id]);
        return ['ok' => true, 'message' => 'Campaign deleted'];
    }

    public static function uploadAdAudio(): array
    {
        if (empty($_FILES['audio_file'])) {
            return ['error' => 'No audio file uploaded'];
        }

        $file = $_FILES['audio_file'];
        if ($file['error'] !== UPLOAD_ERR_OK) {
            return ['error' => 'Upload error code: ' . $file['error']];
        }

        $ext = strtolower(pathinfo($file['name'], PATHINFO_EXTENSION));
        $allowed = ['mp3', 'wav', 'ogg', 'opus', 'flac', 'aac', 'm4a'];
        if (!in_array($ext, $allowed)) {
            return ['error' => 'Invalid audio format. Allowed: ' . implode(', ', $allowed)];
        }

        $safe_name = preg_replace('/[^a-zA-Z0-9_\-.]/', '_', pathinfo($file['name'], PATHINFO_FILENAME));
        $dest_name = $safe_name . '_' . time() . '.' . $ext;
        $dest_path = self::adsDir() . '/' . $dest_name;

        if (!move_uploaded_file($file['tmp_name'], $dest_path)) {
            return ['error' => 'Failed to move uploaded file'];
        }

        /* We probe duration with ffprobe if available */
        $duration = 0;
        $probe_cmd = 'ffprobe -v quiet -show_entries format=duration -of csv=p=0 '
                   . escapeshellarg($dest_path) . ' 2>/dev/null';
        $probe_out = trim(shell_exec($probe_cmd) ?? '');
        if (is_numeric($probe_out)) {
            $duration = (int)round((float)$probe_out);
        }

        return [
            'ok'         => true,
            'file_path'  => $dest_path,
            'file_name'  => $dest_name,
            'duration_sec' => $duration,
            'message'    => 'Ad audio uploaded',
        ];
    }

    /* ────────────────────────────────────────────────────
       PLACEMENTS
       ──────────────────────────────────────────────────── */

    public static function listPlacements(array $data): array
    {
        $episode_id = (int)($data['episode_id'] ?? 0);
        if ($episode_id < 1) return ['error' => 'Missing episode_id'];

        $rows = self::rows(self::DB,
            "SELECT p.*, c.name AS campaign_name, c.type AS campaign_type,
                    c.advertiser, c.duration_sec, c.audio_file_path, c.click_url,
                    c.is_active AS campaign_active
             FROM ad_placements p
             JOIN ad_campaigns c ON c.id = p.campaign_id
             WHERE p.episode_id = ?
             ORDER BY p.position, p.timestamp_ms",
            [$episode_id]
        );
        return ['ok' => true, 'placements' => $rows];
    }

    public static function addPlacement(array $data): array
    {
        $episode_id  = (int)($data['episode_id'] ?? 0);
        $campaign_id = (int)($data['campaign_id'] ?? 0);
        if ($episode_id < 1 || $campaign_id < 1) {
            return ['error' => 'Missing episode_id or campaign_id'];
        }

        $position = $data['position'] ?? 'pre_roll';
        if (!in_array($position, ['pre_roll', 'mid_roll', 'post_roll'])) {
            return ['error' => 'Invalid position'];
        }

        $timestamp_ms = (int)($data['timestamp_ms'] ?? 0);
        $is_dynamic   = (int)($data['is_dynamic'] ?? 1);

        self::run(self::DB,
            "INSERT INTO ad_placements (episode_id, campaign_id, position, timestamp_ms, is_dynamic)
             VALUES (?, ?, ?, ?, ?)",
            [$episode_id, $campaign_id, $position, $timestamp_ms, $is_dynamic]
        );
        return ['ok' => true, 'id' => (int)self::lastId(self::DB), 'message' => 'Placement added'];
    }

    public static function removePlacement(array $data): array
    {
        $id = (int)($data['id'] ?? 0);
        if ($id < 1) return ['error' => 'Missing placement id'];

        self::run(self::DB, "DELETE FROM ad_placements WHERE id = ?", [$id]);
        return ['ok' => true, 'message' => 'Placement removed'];
    }

    public static function autoPlace(array $data): array
    {
        $episode_id = (int)($data['episode_id'] ?? 0);
        if ($episode_id < 1) return ['error' => 'Missing episode_id'];

        /* We get the episode to find its show_id */
        $ep = self::row(self::DB,
            "SELECT id, show_id, duration_sec FROM podcast_episodes WHERE id = ?", [$episode_id]);
        if (!$ep) return ['error' => 'Episode not found'];

        $show_id = (int)$ep['show_id'];
        $now = date('Y-m-d');

        /* We find active campaigns that target this show or have no targeting */
        $campaigns = self::rows(self::DB,
            "SELECT * FROM ad_campaigns
             WHERE is_active = 1
               AND (start_date IS NULL OR start_date <= ?)
               AND (end_date IS NULL OR end_date >= ?)
               AND (max_impressions IS NULL OR impressions < max_impressions)
             ORDER BY cpm_rate DESC",
            [$now, $now]
        );

        /* We remove any existing dynamic placements for this episode */
        self::run(self::DB,
            "DELETE FROM ad_placements WHERE episode_id = ? AND is_dynamic = 1", [$episode_id]);

        $placed = 0;
        $pre_placed = false;
        $post_placed = false;

        foreach ($campaigns as $c) {
            /* We check targeting_json for show match */
            if (!empty($c['targeting_json'])) {
                $targeting = json_decode($c['targeting_json'], true);
                if (!empty($targeting['shows']) && !in_array($show_id, $targeting['shows'])) {
                    continue;
                }
            }

            $type = $c['type'];

            if ($type === 'pre_roll' && !$pre_placed) {
                self::run(self::DB,
                    "INSERT INTO ad_placements (episode_id, campaign_id, position, timestamp_ms, is_dynamic)
                     VALUES (?, ?, 'pre_roll', 0, 1)",
                    [$episode_id, (int)$c['id']]
                );
                $pre_placed = true;
                $placed++;
            } elseif ($type === 'post_roll' && !$post_placed) {
                self::run(self::DB,
                    "INSERT INTO ad_placements (episode_id, campaign_id, position, timestamp_ms, is_dynamic)
                     VALUES (?, ?, 'post_roll', 0, 1)",
                    [$episode_id, (int)$c['id']]
                );
                $post_placed = true;
                $placed++;
            } elseif ($type === 'mid_roll') {
                /* We look for ad_break markers in episode_markers for mid-roll insertion points */
                $markers = self::rows(self::DB,
                    "SELECT * FROM episode_markers
                     WHERE episode_id = ? AND marker_type = 'ad_break'
                     ORDER BY timestamp_ms",
                    [$episode_id]
                );
                foreach ($markers as $mk) {
                    /* We only place one mid-roll per ad break marker */
                    $existing = self::scalar(self::DB,
                        "SELECT COUNT(*) FROM ad_placements
                         WHERE episode_id = ? AND position = 'mid_roll' AND timestamp_ms = ?",
                        [$episode_id, (int)$mk['timestamp_ms']]
                    );
                    if ((int)$existing === 0) {
                        self::run(self::DB,
                            "INSERT INTO ad_placements (episode_id, campaign_id, position, timestamp_ms, is_dynamic)
                             VALUES (?, ?, 'mid_roll', ?, 1)",
                            [$episode_id, (int)$c['id'], (int)$mk['timestamp_ms']]
                        );
                        $placed++;
                        break; // one mid-roll campaign per marker
                    }
                }
            }
        }

        return ['ok' => true, 'placed' => $placed, 'message' => "Auto-placed $placed ad(s)"];
    }

    /* ────────────────────────────────────────────────────
       SPONSORS
       ──────────────────────────────────────────────────── */

    public static function listSponsors(): array
    {
        $rows = self::rows(self::DB,
            "SELECT * FROM sponsors ORDER BY created_at DESC"
        );
        return ['ok' => true, 'sponsors' => $rows];
    }

    public static function createSponsor(array $data): array
    {
        $name = trim($data['name'] ?? '');
        if ($name === '') return ['error' => 'Sponsor name is required'];

        self::run(self::DB,
            "INSERT INTO sponsors (name, contact_email, website_url, logo_path, notes, is_active)
             VALUES (?, ?, ?, ?, ?, ?)",
            [
                $name,
                $data['contact_email'] ?? '',
                $data['website_url'] ?? '',
                $data['logo_path'] ?? '',
                $data['notes'] ?? '',
                (int)($data['is_active'] ?? 1),
            ]
        );
        return ['ok' => true, 'id' => (int)self::lastId(self::DB), 'message' => 'Sponsor created'];
    }

    public static function updateSponsor(array $data): array
    {
        $id = (int)($data['id'] ?? 0);
        if ($id < 1) return ['error' => 'Missing sponsor id'];

        $name = trim($data['name'] ?? '');
        if ($name === '') return ['error' => 'Sponsor name is required'];

        self::run(self::DB,
            "UPDATE sponsors SET name=?, contact_email=?, website_url=?, logo_path=?,
             notes=?, is_active=?, total_spent=?
             WHERE id=?",
            [
                $name,
                $data['contact_email'] ?? '',
                $data['website_url'] ?? '',
                $data['logo_path'] ?? '',
                $data['notes'] ?? '',
                (int)($data['is_active'] ?? 1),
                (float)($data['total_spent'] ?? 0),
                $id,
            ]
        );
        return ['ok' => true, 'message' => 'Sponsor updated'];
    }

    public static function deleteSponsor(array $data): array
    {
        $id = (int)($data['id'] ?? 0);
        if ($id < 1) return ['error' => 'Missing sponsor id'];

        self::run(self::DB, "DELETE FROM sponsors WHERE id = ?", [$id]);
        return ['ok' => true, 'message' => 'Sponsor deleted'];
    }

    /* ────────────────────────────────────────────────────
       ANALYTICS
       ──────────────────────────────────────────────────── */

    public static function adStats(array $data): array
    {
        $campaign_id = (int)($data['campaign_id'] ?? 0);
        $days = (int)($data['days'] ?? 30);

        $where = '';
        $params = [];

        if ($campaign_id > 0) {
            $where = 'WHERE c.id = ?';
            $params[] = $campaign_id;
        }

        $rows = self::rows(self::DB,
            "SELECT c.id, c.name, c.advertiser, c.type, c.cpm_rate,
                    c.impressions AS total_impressions,
                    c.clicks AS total_clicks,
                    ROUND(c.impressions / 1000.0 * c.cpm_rate, 2) AS revenue,
                    CASE WHEN c.impressions > 0 THEN ROUND(c.clicks / c.impressions * 100, 2) ELSE 0 END AS ctr,
                    (SELECT COUNT(*) FROM ad_impressions i
                     WHERE i.campaign_id = c.id AND i.event = 'impression'
                       AND i.recorded_at >= DATE_SUB(NOW(), INTERVAL $days DAY)) AS recent_impressions,
                    (SELECT COUNT(*) FROM ad_impressions i
                     WHERE i.campaign_id = c.id AND i.event = 'click'
                       AND i.recorded_at >= DATE_SUB(NOW(), INTERVAL $days DAY)) AS recent_clicks
             FROM ad_campaigns c $where
             ORDER BY revenue DESC"
            , $params
        );
        return ['ok' => true, 'stats' => $rows];
    }

    public static function revenueSummary(array $data): array
    {
        $period = $data['period'] ?? 'monthly';
        $days = (int)($data['days'] ?? 365);

        $groupBy = match ($period) {
            'daily'   => "DATE(i.recorded_at)",
            'weekly'  => "YEARWEEK(i.recorded_at, 1)",
            default   => "DATE_FORMAT(i.recorded_at, '%Y-%m')",
        };

        $labelExpr = match ($period) {
            'daily'   => "DATE(i.recorded_at)",
            'weekly'  => "DATE(MIN(i.recorded_at))",
            default   => "DATE_FORMAT(i.recorded_at, '%Y-%m')",
        };

        $rows = self::rows(self::DB,
            "SELECT $labelExpr AS period_label,
                    COUNT(CASE WHEN i.event = 'impression' THEN 1 END) AS impressions,
                    COUNT(CASE WHEN i.event = 'click' THEN 1 END) AS clicks,
                    ROUND(SUM(CASE WHEN i.event = 'impression' THEN c.cpm_rate / 1000.0 ELSE 0 END), 2) AS revenue
             FROM ad_impressions i
             JOIN ad_campaigns c ON c.id = i.campaign_id
             WHERE i.recorded_at >= DATE_SUB(NOW(), INTERVAL $days DAY)
             GROUP BY $groupBy
             ORDER BY $groupBy"
        );

        /* We also compute totals */
        $totals = self::row(self::DB,
            "SELECT COUNT(CASE WHEN i.event = 'impression' THEN 1 END) AS total_impressions,
                    COUNT(CASE WHEN i.event = 'click' THEN 1 END) AS total_clicks,
                    ROUND(SUM(CASE WHEN i.event = 'impression' THEN c.cpm_rate / 1000.0 ELSE 0 END), 2) AS total_revenue,
                    COUNT(DISTINCT i.campaign_id) AS active_campaigns
             FROM ad_impressions i
             JOIN ad_campaigns c ON c.id = i.campaign_id
             WHERE i.recorded_at >= DATE_SUB(NOW(), INTERVAL $days DAY)"
        );

        return ['ok' => true, 'periods' => $rows, 'totals' => $totals ?: []];
    }

    /* ────────────────────────────────────────────────────
       DYNAMIC AD SERVING (public, no auth)
       ──────────────────────────────────────────────────── */

    public static function serveEpisode(int $episode_id): void
    {
        if ($episode_id < 1) {
            http_response_code(400);
            echo 'Missing episode_id';
            return;
        }

        $ep = self::row(self::DB,
            "SELECT * FROM podcast_episodes WHERE id = ?", [$episode_id]);
        if (!$ep || empty($ep['file_path']) || !file_exists($ep['file_path'])) {
            http_response_code(404);
            echo 'Episode not found or file missing';
            return;
        }

        /* We find all dynamic placements for this episode */
        $placements = self::rows(self::DB,
            "SELECT p.*, c.audio_file_path, c.duration_sec, c.is_active, c.id AS cid
             FROM ad_placements p
             JOIN ad_campaigns c ON c.id = p.campaign_id
             WHERE p.episode_id = ? AND p.is_dynamic = 1 AND c.is_active = 1
             ORDER BY p.position, p.timestamp_ms",
            [$episode_id]
        );

        /* If no placements, serve original file directly */
        if (empty($placements)) {
            self::streamFile($ep['file_path']);
            return;
        }

        /* We check for a cached version */
        $cache_key = $episode_id . '_' . md5(json_encode(array_column($placements, 'id')));
        $cache_file = self::cacheDir() . '/ep_' . $cache_key . '.mp3';

        if (file_exists($cache_file) && filemtime($cache_file) > filemtime($ep['file_path'])) {
            /* We record impressions for all placements */
            self::recordImpressions($placements, $episode_id);
            self::streamFile($cache_file);
            return;
        }

        /* We build the ffmpeg concat command */
        $pre_rolls  = [];
        $mid_rolls  = [];
        $post_rolls = [];

        foreach ($placements as $p) {
            if (empty($p['audio_file_path']) || !file_exists($p['audio_file_path'])) continue;

            if ($p['position'] === 'pre_roll') {
                $pre_rolls[] = $p;
            } elseif ($p['position'] === 'mid_roll') {
                $mid_rolls[] = $p;
            } elseif ($p['position'] === 'post_roll') {
                $post_rolls[] = $p;
            }
        }

        /* We build the input file list for ffmpeg */
        $inputs = [];
        $episode_path = $ep['file_path'];

        /* Pre-rolls first */
        foreach ($pre_rolls as $pr) {
            $inputs[] = $pr['audio_file_path'];
        }

        if (empty($mid_rolls)) {
            /* No mid-rolls: just add the episode */
            $inputs[] = $episode_path;
        } else {
            /* We need to split the episode at mid-roll timestamps.
             * For simplicity, we use a single ffmpeg command with filter_complex.
             * We sort mid-rolls by timestamp. */
            usort($mid_rolls, function($a, $b) {
                return (int)$a['timestamp_ms'] - (int)$b['timestamp_ms'];
            });

            /* We build a segment list: episode segments interleaved with mid-roll ads */
            $segments = [];
            $last_ts_sec = 0;

            foreach ($mid_rolls as $mr) {
                $ts_sec = (int)$mr['timestamp_ms'] / 1000.0;
                if ($ts_sec > $last_ts_sec) {
                    $segments[] = ['type' => 'episode_segment', 'start' => $last_ts_sec, 'end' => $ts_sec];
                }
                $segments[] = ['type' => 'ad', 'path' => $mr['audio_file_path']];
                $last_ts_sec = $ts_sec;
            }
            /* We add the remaining episode after last mid-roll */
            $segments[] = ['type' => 'episode_segment', 'start' => $last_ts_sec, 'end' => -1];

            /* For mid-roll support, we use a more complex ffmpeg approach.
             * We extract episode segments and concat them with ads. */
            $temp_parts = [];
            $part_idx = 0;
            $temp_dir = sys_get_temp_dir();

            foreach ($segments as $seg) {
                $part_file = $temp_dir . '/mc1_ad_part_' . $episode_id . '_' . $part_idx . '.mp3';

                if ($seg['type'] === 'episode_segment') {
                    $start = (float)$seg['start'];
                    $end   = (float)$seg['end'];
                    $ss_arg = '-ss ' . escapeshellarg(sprintf('%.3f', $start));
                    $to_arg = ($end > 0) ? ('-to ' . escapeshellarg(sprintf('%.3f', $end))) : '';
                    $cmd = "ffmpeg -y $ss_arg -i " . escapeshellarg($episode_path)
                         . " $to_arg -c:a libmp3lame -b:a 128k "
                         . escapeshellarg($part_file) . ' 2>/dev/null';
                    shell_exec($cmd);
                } else {
                    $cmd = "ffmpeg -y -i " . escapeshellarg($seg['path'])
                         . " -c:a libmp3lame -b:a 128k "
                         . escapeshellarg($part_file) . ' 2>/dev/null';
                    shell_exec($cmd);
                }

                if (file_exists($part_file) && filesize($part_file) > 0) {
                    $temp_parts[] = $part_file;
                }
                $part_idx++;
            }

            /* We write a concat list file */
            $list_file = $temp_dir . '/mc1_ad_concat_' . $episode_id . '.txt';
            /* Add pre-rolls to the beginning */
            $concat_list = '';
            foreach ($pre_rolls as $pr) {
                $concat_list .= "file " . escapeshellarg($pr['audio_file_path']) . "\n";
            }
            foreach ($temp_parts as $tp) {
                $concat_list .= "file " . escapeshellarg($tp) . "\n";
            }
            foreach ($post_rolls as $pr) {
                $concat_list .= "file " . escapeshellarg($pr['audio_file_path']) . "\n";
            }

            file_put_contents($list_file, $concat_list);

            $cmd = "ffmpeg -y -f concat -safe 0 -i " . escapeshellarg($list_file)
                 . " -c:a libmp3lame -b:a 128k " . escapeshellarg($cache_file) . ' 2>/dev/null';
            shell_exec($cmd);

            /* Clean up temp files */
            foreach ($temp_parts as $tp) {
                @unlink($tp);
            }
            @unlink($list_file);

            if (file_exists($cache_file)) {
                self::recordImpressions($placements, $episode_id);
                self::streamFile($cache_file);
                return;
            }

            /* Fallback: serve original */
            self::streamFile($episode_path);
            return;
        }

        /* Post-rolls */
        foreach ($post_rolls as $pr) {
            $inputs[] = $pr['audio_file_path'];
        }

        /* Simple concat (no mid-rolls) */
        $n = count($inputs);
        if ($n === 1) {
            /* Just the episode, no ads actually had valid files */
            self::streamFile($episode_path);
            return;
        }

        $input_args = '';
        $filter_parts = '';
        for ($i = 0; $i < $n; $i++) {
            $input_args .= ' -i ' . escapeshellarg($inputs[$i]);
            $filter_parts .= "[$i:a]";
        }
        $filter = "$filter_parts concat=n=$n:v=0:a=1[out]";

        $cmd = "ffmpeg -y $input_args -filter_complex " . escapeshellarg($filter)
             . " -map '[out]' -c:a libmp3lame -b:a 128k "
             . escapeshellarg($cache_file) . ' 2>/dev/null';
        shell_exec($cmd);

        if (file_exists($cache_file)) {
            self::recordImpressions($placements, $episode_id);
            self::streamFile($cache_file);
            return;
        }

        /* Fallback to original */
        self::streamFile($episode_path);
    }

    /* We record impressions, deduplicating by IP per episode per 24h */
    private static function recordImpressions(array $placements, int $episode_id): void
    {
        $client_ip = $_SERVER['REMOTE_ADDR'] ?? '';
        $user_agent = $_SERVER['HTTP_USER_AGENT'] ?? '';

        foreach ($placements as $p) {
            /* We check for duplicate impression within 24h */
            $exists = self::scalar(self::DB,
                "SELECT COUNT(*) FROM ad_impressions
                 WHERE placement_id = ? AND client_ip = ? AND event = 'impression'
                   AND recorded_at >= DATE_SUB(NOW(), INTERVAL 24 HOUR)",
                [(int)$p['id'], $client_ip]
            );

            if ((int)$exists === 0) {
                self::run(self::DB,
                    "INSERT INTO ad_impressions (placement_id, campaign_id, episode_id, client_ip, user_agent, event)
                     VALUES (?, ?, ?, ?, ?, 'impression')",
                    [(int)$p['id'], (int)$p['campaign_id'], $episode_id, $client_ip, $user_agent]
                );
                /* We also update the campaign counter */
                self::run(self::DB,
                    "UPDATE ad_campaigns SET impressions = impressions + 1 WHERE id = ?",
                    [(int)$p['campaign_id']]
                );
            }
        }
    }

    private static function streamFile(string $path): void
    {
        $ext = strtolower(pathinfo($path, PATHINFO_EXTENSION));
        $mime = match ($ext) {
            'mp3'         => 'audio/mpeg',
            'ogg', 'opus' => 'audio/ogg',
            'flac'        => 'audio/flac',
            'aac', 'm4a'  => 'audio/mp4',
            'wav'         => 'audio/wav',
            default       => 'audio/mpeg',
        };

        header('Content-Type: ' . $mime);
        header('Content-Length: ' . filesize($path));
        header('Accept-Ranges: bytes');
        header('Cache-Control: public, max-age=3600');
        readfile($path);
    }

    /* ── Impression tracking (public, no auth) ── */

    public static function trackImpression(array $params): void
    {
        header('Content-Type: image/gif');
        /* We serve a 1x1 transparent GIF (tracking pixel) */
        echo base64_decode('R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7');

        $placement_id = (int)($params['placement_id'] ?? 0);
        $campaign_id  = (int)($params['campaign_id'] ?? 0);
        $episode_id   = (int)($params['episode_id'] ?? 0);
        $event        = $params['event'] ?? 'impression';

        if (!in_array($event, ['impression', 'click', 'complete'])) $event = 'impression';
        if ($placement_id < 1 || $campaign_id < 1) return;

        $client_ip  = $_SERVER['REMOTE_ADDR'] ?? '';
        $user_agent = $_SERVER['HTTP_USER_AGENT'] ?? '';

        /* We deduplicate impressions: 1 per unique IP per episode per 24h */
        if ($event === 'impression') {
            $exists = self::scalar(self::DB,
                "SELECT COUNT(*) FROM ad_impressions
                 WHERE placement_id = ? AND client_ip = ? AND event = 'impression'
                   AND recorded_at >= DATE_SUB(NOW(), INTERVAL 24 HOUR)",
                [$placement_id, $client_ip]
            );
            if ((int)$exists > 0) return;
        }

        self::run(self::DB,
            "INSERT INTO ad_impressions (placement_id, campaign_id, episode_id, client_ip, user_agent, event)
             VALUES (?, ?, ?, ?, ?, ?)",
            [$placement_id, $campaign_id, $episode_id, $client_ip, $user_agent, $event]
        );

        /* We update campaign counters */
        if ($event === 'impression') {
            self::run(self::DB,
                "UPDATE ad_campaigns SET impressions = impressions + 1 WHERE id = ?", [$campaign_id]);
        } elseif ($event === 'click') {
            self::run(self::DB,
                "UPDATE ad_campaigns SET clicks = clicks + 1 WHERE id = ?", [$campaign_id]);
        }
    }

    public static function trackClick(array $params): void
    {
        $campaign_id  = (int)($params['campaign_id'] ?? 0);
        $placement_id = (int)($params['placement_id'] ?? 0);
        $episode_id   = (int)($params['episode_id'] ?? 0);

        /* We record the click */
        if ($campaign_id > 0) {
            $client_ip  = $_SERVER['REMOTE_ADDR'] ?? '';
            $user_agent = $_SERVER['HTTP_USER_AGENT'] ?? '';

            self::run(self::DB,
                "INSERT INTO ad_impressions (placement_id, campaign_id, episode_id, client_ip, user_agent, event)
                 VALUES (?, ?, ?, ?, ?, 'click')",
                [$placement_id, $campaign_id, $episode_id, $client_ip, $user_agent]
            );
            self::run(self::DB,
                "UPDATE ad_campaigns SET clicks = clicks + 1 WHERE id = ?", [$campaign_id]);

            /* We redirect to the campaign's click URL */
            $campaign = self::row(self::DB,
                "SELECT click_url FROM ad_campaigns WHERE id = ?", [$campaign_id]);
            if ($campaign && !empty($campaign['click_url'])) {
                header('Location: ' . $campaign['click_url'], true, 302);
                return;
            }
        }

        /* Fallback: redirect to homepage */
        header('Location: /', true, 302);
    }
}

/* ── Action dispatch ── */

try {
    /* We handle file upload separately since it comes as multipart form data */
    if ($action === 'upload_ad_audio') {
        $result = AdsApi::uploadAdAudio();
    } else {
        $result = match ($action) {
            /* Campaigns */
            'list_campaigns'    => AdsApi::listCampaigns(),
            'create_campaign'   => AdsApi::createCampaign($data),
            'update_campaign'   => AdsApi::updateCampaign($data),
            'delete_campaign'   => AdsApi::deleteCampaign($data),
            /* Placements */
            'list_placements'   => AdsApi::listPlacements($data),
            'add_placement'     => AdsApi::addPlacement($data),
            'remove_placement'  => AdsApi::removePlacement($data),
            'auto_place'        => AdsApi::autoPlace($data),
            /* Sponsors */
            'list_sponsors'     => AdsApi::listSponsors(),
            'create_sponsor'    => AdsApi::createSponsor($data),
            'update_sponsor'    => AdsApi::updateSponsor($data),
            'delete_sponsor'    => AdsApi::deleteSponsor($data),
            /* Analytics */
            'ad_stats'          => AdsApi::adStats($data),
            'revenue_summary'   => AdsApi::revenueSummary($data),
            default             => ['error' => 'Unknown action: ' . $action],
        };
    }

    $status = isset($result['error']) ? 400 : 200;
    mc1_api_respond($result, $status);
} catch (Throwable $e) {
    mc1_log_exception($e, 'ads');
    mc1_api_respond(['error' => mc1_safe_error($e, 'Ads API error')], 500);
}
