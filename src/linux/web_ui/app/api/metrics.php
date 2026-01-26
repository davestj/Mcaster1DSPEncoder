<?php
// api/metrics.php — Listener metrics query API (POST-only, JSON responses)
// Note: uopz extension active — no exit() calls used.
//
// Actions: summary, daily_stats, sessions, top_tracks,
//          server_list, server_history, stream_events, track_plays

define('MC1_BOOT', true);
require_once __DIR__ . '/../inc/auth.php';
require_once __DIR__ . '/../inc/db.php';
require_once __DIR__ . '/../inc/metrics.datacalculate.class.php';

header('Content-Type: application/json; charset=utf-8');

$authed  = mc1_is_authed();
$is_post = ($_SERVER['REQUEST_METHOD'] === 'POST');
$raw     = $is_post ? file_get_contents('php://input') : '';
$req     = is_string($raw) && $raw !== '' ? json_decode($raw, true) : null;
$action  = is_array($req) ? (string)($req['action'] ?? '') : '';

// ── Dispatch ───────────────────────────────────────────────────────────────

if (!$authed) {

    http_response_code(403);
    echo json_encode(['ok' => false, 'error' => 'Forbidden']);

} elseif (!$is_post) {

    http_response_code(405);
    echo json_encode(['ok' => false, 'error' => 'POST required']);

} elseif (!is_array($req)) {

    http_response_code(400);
    echo json_encode(['ok' => false, 'error' => 'Invalid JSON body']);

} elseif ($action === 'summary') {

    try {
        $db  = mc1_db('mcaster1_metrics');
        $row = $db->query(
            "SELECT COUNT(*)                              AS total_sessions,
                    COUNT(DISTINCT client_ip)             AS unique_ips,
                    COALESCE(SUM(duration_sec)/3600, 0)  AS total_hours,
                    COALESCE(AVG(duration_sec)/60,   0)  AS avg_listen_min,
                    COALESCE(SUM(bytes_sent),        0)  AS total_bytes
             FROM listener_sessions
             WHERE disconnected_at IS NOT NULL"
        )->fetch();
        echo json_encode(['ok' => true, 'data' => $row]);
    } catch (Exception $e) {
        http_response_code(500);
        echo json_encode(['ok' => false, 'error' => $e->getMessage()]);
    }

} elseif ($action === 'daily_stats') {

    $days  = min(365, max(1, (int)($req['days']  ?? 30)));
    $mount = trim($req['mount'] ?? '');

    try {
        $db     = mc1_db('mcaster1_metrics');
        $where  = 'WHERE stat_date >= CURDATE() - INTERVAL ? DAY';
        $params = [$days];
        if ($mount !== '') {
            $where  .= ' AND mount = ?';
            $params[] = $mount;
        }
        $st = $db->prepare(
            "SELECT stat_date, mount, peak_listeners, total_sessions, total_hours
             FROM daily_stats
             $where
             ORDER BY stat_date DESC"
        );
        $st->execute($params);
        echo json_encode(['ok' => true, 'data' => $st->fetchAll()]);
    } catch (Exception $e) {
        http_response_code(500);
        echo json_encode(['ok' => false, 'error' => $e->getMessage()]);
    }

} elseif ($action === 'sessions') {

    $page   = max(1, (int)($req['page']  ?? 1));
    $limit  = min(200, max(1, (int)($req['limit'] ?? 50)));
    $offset = ($page - 1) * $limit;
    $mount  = trim($req['mount'] ?? '');

    try {
        $db     = mc1_db('mcaster1_metrics');
        $where  = '';
        $params = [];
        if ($mount !== '') {
            $where  = 'WHERE stream_mount = ?';
            $params = [$mount];
        }
        $cs = $db->prepare("SELECT COUNT(*) FROM listener_sessions $where");
        $cs->execute($params);
        $total = (int)$cs->fetchColumn();
        $pages = max(1, (int)ceil($total / $limit));

        $st = $db->prepare(
            "SELECT client_ip, user_agent, stream_mount,
                    connected_at, disconnected_at, duration_sec, bytes_sent
             FROM listener_sessions $where
             ORDER BY connected_at DESC
             LIMIT ? OFFSET ?"
        );
        $st->execute(array_merge($params, [$limit, $offset]));

        echo json_encode([
            'ok'    => true,
            'total' => $total,
            'pages' => $pages,
            'page'  => $page,
            'data'  => $st->fetchAll(),
        ]);
    } catch (Exception $e) {
        http_response_code(500);
        echo json_encode(['ok' => false, 'error' => $e->getMessage()]);
    }

} elseif ($action === 'top_tracks') {

    $limit = min(100, max(1, (int)($req['limit'] ?? 20)));

    try {
        $db = mc1_db('mcaster1_media');
        $st = $db->prepare(
            "SELECT id, title, artist, album, play_count, last_played_at
             FROM tracks
             WHERE play_count > 0
             ORDER BY play_count DESC
             LIMIT ?"
        );
        $st->execute([$limit]);
        echo json_encode(['ok' => true, 'data' => $st->fetchAll()]);
    } catch (Exception $e) {
        http_response_code(500);
        echo json_encode(['ok' => false, 'error' => $e->getMessage()]);
    }

} elseif ($action === 'server_list') {

    $data = Mc1MetricsCalculator::getServerList();
    echo json_encode(['ok' => true, 'data' => $data]);

} elseif ($action === 'server_history') {

    $server_id = (int)($req['server_id'] ?? 0);
    $hours     = min(24, max(1, (int)($req['hours'] ?? 6)));
    if ($server_id <= 0) {
        http_response_code(400);
        echo json_encode(['ok' => false, 'error' => 'server_id required']);
    } else {
        $data = Mc1MetricsCalculator::getServerStatHistory($server_id, $hours);
        echo json_encode(['ok' => true, 'data' => $data, 'server_id' => $server_id]);
    }

} elseif ($action === 'stream_events') {

    $page       = max(1, (int)($req['page']       ?? 1));
    $limit      = min(200, max(1, (int)($req['limit'] ?? 50)));
    $slot_id    = isset($req['slot_id'])    ? (int)$req['slot_id']      : null;
    $event_type = isset($req['event_type']) ? (string)$req['event_type'] : null;
    $result     = Mc1MetricsCalculator::getStreamEvents($page, $limit, $slot_id, $event_type);
    echo json_encode(array_merge(['ok' => true], $result));

} elseif ($action === 'track_plays') {

    $limit = min(100, max(1, (int)($req['limit'] ?? 20)));
    $data  = Mc1MetricsCalculator::getTopTracks($limit);
    echo json_encode(['ok' => true, 'data' => $data]);

} else {

    http_response_code(400);
    echo json_encode(['ok' => false, 'error' => 'Unknown action: ' . $action]);

}
