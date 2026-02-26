<?php
// api/health.php — System & encoder health history API (POST-only, JSON)
//
// File:    src/linux/web_ui/app/api/health.php
// Author:  Dave St. John <davestj@gmail.com>
// Date:    2026-02-24
// Purpose: PHP-side API for historical health snapshots stored in DB.
//          Live data (current CPU, mem, slot state) is served directly
//          by C++ at GET /api/v1/system/health — browser JS calls that
//          endpoint directly and does NOT go through PHP.
//
// Actions: get_history, get_encoder, prune, aggregate
// Note: No exit()/die() — uopz extension active.

define('MC1_BOOT', true);
require_once __DIR__ . '/../inc/auth.php';
require_once __DIR__ . '/../inc/logger.php';
require_once __DIR__ . '/../inc/metrics.datacollector.class.php';
require_once __DIR__ . '/../inc/metrics.datacalculate.class.php';

header('Content-Type: application/json; charset=utf-8');

$authed  = mc1_is_authed();
$is_post = ($_SERVER['REQUEST_METHOD'] === 'POST');
$raw     = $is_post ? file_get_contents('php://input') : '';
$req     = is_string($raw) && $raw !== '' ? json_decode($raw, true) : null;
$action  = is_array($req) ? (string)($req['action'] ?? '') : '';

if (!$authed) {
    mc1_api_respond(['ok' => false, 'error' => 'Forbidden'], 403);
} elseif (!$is_post) {
    mc1_api_respond(['ok' => false, 'error' => 'POST required'], 405);
} elseif (!is_array($req)) {
    mc1_api_respond(['ok' => false, 'error' => 'Invalid JSON body'], 400);
} elseif ($action === 'get_history') {

    $minutes = min(1440, max(5, (int)($req['minutes'] ?? 60)));
    $data    = Mc1MetricsCalculator::getSystemHealthHistory($minutes);
    mc1_api_respond(['ok' => true, 'data' => $data, 'minutes' => $minutes]);

} elseif ($action === 'get_encoder') {

    $slot_id = (int)($req['slot_id'] ?? 0);
    $hours   = min(24, max(1, (int)($req['hours'] ?? 1)));
    if ($slot_id <= 0) {
        mc1_api_respond(['ok' => false, 'error' => 'slot_id required'], 400);
    } else {
        $data = Mc1MetricsCalculator::getEncoderHealthHistory($slot_id, $hours);
        mc1_api_respond(['ok' => true, 'data' => $data, 'slot_id' => $slot_id]);
    }

} elseif ($action === 'prune') {

    $days   = min(365, max(1, (int)($req['days'] ?? 7)));
    $result = Mc1MetricsCollector::pruneOldData($days);
    mc1_api_respond(['ok' => true, 'pruned' => $result, 'days' => $days]);

} elseif ($action === 'aggregate') {

    $date   = isset($req['date']) && preg_match('/^\d{4}-\d{2}-\d{2}$/', $req['date'])
              ? $req['date'] : null;
    $ok     = Mc1MetricsCollector::aggregateDailyStats($date);
    mc1_api_respond(['ok' => $ok, 'date' => $date ?? date('Y-m-d', strtotime('-1 day'))]);

} else {
    mc1_api_respond(['ok' => false, 'error' => 'Unknown action: ' . $action], 400);
}
