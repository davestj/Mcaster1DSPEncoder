<?php
/**
 * schedule.php — Clockwheel Schedule CRUD API
 *
 * Actions: get_schedule, save_assignment, delete_assignment, get_playlists
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/../inc/mc1_config.php';
require_once __DIR__ . '/../inc/db.php';
require_once __DIR__ . '/../inc/traits.db.class.php';
require_once __DIR__ . '/../inc/logger.php';
require_once __DIR__ . '/../inc/auth.php';
require_once __DIR__ . '/../inc/user_auth.php';

header('Content-Type: application/json');

if (!mc1_is_authed()) { mc1_api_respond(['error' => 'Unauthorized'], 403); return; }
if ($_SERVER['REQUEST_METHOD'] !== 'POST') { mc1_api_respond(['error' => 'POST required'], 405); return; }

$raw    = (string)file_get_contents('php://input');
$body   = json_decode($raw, true) ?: [];
$action = (string)($body['action'] ?? '');

/* ── get_schedule — all clock hour assignments ────────────────────────────── */
if ($action === 'get_schedule') {
    $slot_id = (int)($body['slot_id'] ?? 0);
    try {
        $db = mc1_db('mcaster1_media');
        $where = '';
        $params = [];
        if ($slot_id > 0) { $where = 'WHERE slot_id = ?'; $params = [$slot_id]; }
        $rows = $db->prepare("SELECT id, slot_id, hour, day_of_week, name, playlist_id, playlist_path, is_active FROM clock_hours $where ORDER BY slot_id, hour");
        $rows->execute($params);
        mc1_api_respond(['ok' => true, 'schedule' => $rows->fetchAll(PDO::FETCH_ASSOC)]);
    } catch (Exception $e) {
        mc1_api_respond(['ok' => false, 'error' => $e->getMessage()], 500);
    }
    return;
}

/* ── save_assignment — upsert a clock hour entry ──────────────────────────── */
if ($action === 'save_assignment') {
    $slot_id       = (int)($body['slot_id'] ?? 0);
    $hour          = (int)($body['hour'] ?? 0);
    $day_of_week   = isset($body['day_of_week']) ? (int)$body['day_of_week'] : null;
    $name          = trim($body['name'] ?? '');
    $playlist_id   = (int)($body['playlist_id'] ?? 0);
    $playlist_path = trim($body['playlist_path'] ?? '');

    if ($slot_id < 1 || $hour < 0 || $hour > 23) {
        mc1_api_respond(['error' => 'slot_id (1+) and hour (0-23) required'], 400);
        return;
    }

    try {
        $db = mc1_db('mcaster1_media');
        $stmt = $db->prepare(
            "INSERT INTO clock_hours (slot_id, hour, day_of_week, name, playlist_id, playlist_path, is_active)
             VALUES (?, ?, ?, ?, ?, ?, 1)
             ON DUPLICATE KEY UPDATE name=VALUES(name), playlist_id=VALUES(playlist_id),
             playlist_path=VALUES(playlist_path), is_active=1"
        );
        $stmt->execute([$slot_id, $hour, $day_of_week, $name, $playlist_id ?: null, $playlist_path]);
        mc1_api_respond(['ok' => true]);
    } catch (Exception $e) {
        mc1_api_respond(['ok' => false, 'error' => $e->getMessage()], 500);
    }
    return;
}

/* ── delete_assignment ────────────────────────────────────────────────────── */
if ($action === 'delete_assignment') {
    $id = (int)($body['id'] ?? 0);
    if ($id < 1) { mc1_api_respond(['error' => 'id required'], 400); return; }
    try {
        $db = mc1_db('mcaster1_media');
        $db->prepare("DELETE FROM clock_hours WHERE id = ?")->execute([$id]);
        mc1_api_respond(['ok' => true]);
    } catch (Exception $e) {
        mc1_api_respond(['ok' => false, 'error' => $e->getMessage()], 500);
    }
    return;
}

/* ── get_playlists — list available playlists for dropdown ────────────────── */
if ($action === 'get_playlists') {
    try {
        $db = mc1_db('mcaster1_media');
        $rows = $db->query("SELECT id, name, description FROM playlists ORDER BY name")->fetchAll(PDO::FETCH_ASSOC);

        /* We also list .m3u files from the playlist directory */
        $files = [];
        $dir = defined('MC1_PLAYLIST_DIR') ? MC1_PLAYLIST_DIR : '';
        if ($dir && is_dir($dir)) {
            foreach (glob($dir . '/*.m3u') as $f) {
                $files[] = ['path' => $f, 'name' => basename($f)];
            }
        }

        mc1_api_respond(['ok' => true, 'playlists' => $rows, 'files' => $files]);
    } catch (Exception $e) {
        mc1_api_respond(['ok' => false, 'error' => $e->getMessage()], 500);
    }
    return;
}

mc1_api_respond(['error' => 'Unknown action: ' . $action], 400);
