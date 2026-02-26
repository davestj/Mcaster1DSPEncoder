<?php
/**
 * app/api/playlists.php — DB-backed playlist CRUD + generation + load-to-encoder-slot.
 *
 * We handle all playlist operations: list, get_tracks, create, delete, add_track,
 * remove_track, load, generate, preview, get_categories, get_algorithms.
 *
 * No exit()/die() — uopz active. JSON responses only.
 * Auth gate on every action via mc1_is_authed().
 *
 * @author  Dave St. John <davestj@gmail.com>
 * @version 1.4.0
 * @since   2026-02-23
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/../inc/auth.php';
require_once __DIR__ . '/../inc/db.php';
require_once __DIR__ . '/../inc/mc1_config.php';
require_once __DIR__ . '/../inc/logger.php';
require_once __DIR__ . '/../inc/user_auth.php';
require_once __DIR__ . '/../inc/playlist.manager.pro.class.php';

header('Content-Type: application/json; charset=utf-8');

if (!mc1_is_authed()) {
    mc1_api_respond(['ok' => false, 'error' => 'Forbidden'], 403);
    return;
}

$raw    = file_get_contents('php://input');
$req    = ($raw !== '') ? json_decode($raw, true) : [];
if (!is_array($req)) $req = [];
$action = (string)($req['action'] ?? '');

// ── list ──────────────────────────────────────────────────────────────────

if ($action === 'list') {
    try {
        $rows = mc1_db('mcaster1_media')->query(
            'SELECT id, name, type, description, track_count, created_at, modified_at
             FROM playlists ORDER BY name'
        )->fetchAll();
        mc1_api_respond(['ok' => true, 'playlists' => $rows]);
    } catch (Exception $e) {
        mc1_api_respond(['ok' => false, 'error' => $e->getMessage()], 500);
    }
    return;
}

// ── get_tracks ────────────────────────────────────────────────────────────

if ($action === 'get_tracks') {
    $id = (int)($req['id'] ?? 0);
    if ($id <= 0) { mc1_api_respond(['ok' => false, 'error' => 'id required'], 400); return; }
    try {
        $st = mc1_db('mcaster1_media')->prepare(
            'SELECT pt.id AS playlist_track_id, pt.position, pt.weight,
                    t.id, t.title, t.artist, t.album, t.genre,
                    t.duration_ms, t.bpm, t.rating, t.is_jingle, t.is_sweeper
             FROM playlist_tracks pt
             JOIN tracks t ON t.id = pt.track_id
             WHERE pt.playlist_id = ?
             ORDER BY pt.position, pt.id'
        );
        $st->execute([$id]);
        mc1_api_respond(['ok' => true, 'tracks' => $st->fetchAll()]);
    } catch (Exception $e) {
        mc1_api_respond(['ok' => false, 'error' => $e->getMessage()], 500);
    }
    return;
}

// ── create ────────────────────────────────────────────────────────────────

if ($action === 'create') {
    $name = trim($req['name'] ?? '');
    if ($name === '') { mc1_api_respond(['ok' => false, 'error' => 'name required'], 400); return; }
    $type = in_array($req['type'] ?? '', ['static', 'smart', 'ai', 'clock']) ? $req['type'] : 'static';
    $desc = trim($req['description'] ?? '');
    try {
        $db = mc1_db('mcaster1_media');
        $db->prepare('INSERT INTO playlists (name, type, description) VALUES (?, ?, ?)')->execute([$name, $type, $desc]);
        mc1_api_respond(['ok' => true, 'id' => (int)$db->lastInsertId()]);
    } catch (Exception $e) {
        mc1_api_respond(['ok' => false, 'error' => $e->getMessage()], 500);
    }
    return;
}

// ── delete ────────────────────────────────────────────────────────────────

if ($action === 'delete') {
    $id = (int)($req['id'] ?? 0);
    if ($id <= 0) { mc1_api_respond(['ok' => false, 'error' => 'id required'], 400); return; }
    try {
        $db = mc1_db('mcaster1_media');
        $db->prepare('DELETE FROM playlist_tracks WHERE playlist_id=?')->execute([$id]);
        $db->prepare('DELETE FROM playlists WHERE id=?')->execute([$id]);
        mc1_api_respond(['ok' => true]);
    } catch (Exception $e) {
        mc1_api_respond(['ok' => false, 'error' => $e->getMessage()], 500);
    }
    return;
}

// ── add_track ─────────────────────────────────────────────────────────────

if ($action === 'add_track') {
    $pl_id    = (int)($req['playlist_id'] ?? 0);
    $track_id = (int)($req['track_id']    ?? 0);
    $weight   = min(10.0, max(0.1, (float)($req['weight'] ?? 1.0)));
    if ($pl_id <= 0 || $track_id <= 0) {
        mc1_api_respond(['ok' => false, 'error' => 'playlist_id and track_id required'], 400);
        return;
    }
    try {
        $db  = mc1_db('mcaster1_media');
        $stp = $db->prepare('SELECT COALESCE(MAX(position),0)+1 FROM playlist_tracks WHERE playlist_id=?');
        $stp->execute([$pl_id]);
        $pos = (int)$stp->fetchColumn();
        $db->prepare('INSERT INTO playlist_tracks (playlist_id, track_id, position, weight) VALUES (?,?,?,?)')->execute([$pl_id, $track_id, $pos, $weight]);
        $db->prepare('UPDATE playlists SET track_count=(SELECT COUNT(*) FROM playlist_tracks WHERE playlist_id=?), modified_at=NOW() WHERE id=?')->execute([$pl_id, $pl_id]);
        mc1_api_respond(['ok' => true]);
    } catch (Exception $e) {
        mc1_api_respond(['ok' => false, 'error' => $e->getMessage()], 500);
    }
    return;
}

// ── remove_track ──────────────────────────────────────────────────────────

if ($action === 'remove_track') {
    $pt_id = (int)($req['playlist_track_id'] ?? 0);
    if ($pt_id <= 0) { mc1_api_respond(['ok' => false, 'error' => 'playlist_track_id required'], 400); return; }
    try {
        $db    = mc1_db('mcaster1_media');
        $stf   = $db->prepare('SELECT playlist_id FROM playlist_tracks WHERE id=?');
        $stf->execute([$pt_id]);
        $pl_id = (int)($stf->fetchColumn() ?: 0);
        $db->prepare('DELETE FROM playlist_tracks WHERE id=?')->execute([$pt_id]);
        if ($pl_id > 0) {
            $db->prepare('UPDATE playlists SET track_count=(SELECT COUNT(*) FROM playlist_tracks WHERE playlist_id=?), modified_at=NOW() WHERE id=?')->execute([$pl_id, $pl_id]);
        }
        mc1_api_respond(['ok' => true]);
    } catch (Exception $e) {
        mc1_api_respond(['ok' => false, 'error' => $e->getMessage()], 500);
    }
    return;
}

// ── load ──────────────────────────────────────────────────────────────────
// We return track file paths to the browser — JS then calls /api/v1/playlist/load directly.
// We NEVER proxy PHP→C++ on loopback: C++ thread blocks in FastCGI, PHP curl blocks
// waiting for C++ → deadlock. Browser has mc1session cookie already.

if ($action === 'load') {
    $pl_id = (int)($req['id']   ?? 0);
    $slot  = (int)($req['slot'] ?? 1);
    if ($pl_id <= 0) { mc1_api_respond(['ok' => false, 'error' => 'id required'], 400); return; }
    try {
        $st = mc1_db('mcaster1_media')->prepare(
            'SELECT t.file_path FROM playlist_tracks pt
             JOIN tracks t ON t.id = pt.track_id
             WHERE pt.playlist_id = ? ORDER BY pt.position, pt.id'
        );
        $st->execute([$pl_id]);
        $paths = array_column($st->fetchAll(), 'file_path');
        if (empty($paths)) { mc1_api_respond(['ok' => false, 'error' => 'Playlist is empty']); return; }
        mc1_api_respond(['ok' => true, 'tracks' => $paths, 'slot' => $slot]);
    } catch (Exception $e) {
        mc1_api_respond(['ok' => false, 'error' => $e->getMessage()], 500);
    }
    return;
}

// ── get_categories ────────────────────────────────────────────────────────

if ($action === 'get_categories') {
    try {
        $cats = mc1_db('mcaster1_media')->query(
            'SELECT id, name, color_hex, icon, type FROM categories ORDER BY sort_order, name'
        )->fetchAll();
        mc1_api_respond(['ok' => true, 'categories' => $cats]);
    } catch (Exception $e) {
        mc1_api_respond(['ok' => false, 'error' => $e->getMessage()], 500);
    }
    return;
}

// ── get_algorithms ────────────────────────────────────────────────────────

if ($action === 'get_algorithms') {
    mc1_api_respond(['ok' => true, 'algorithms' => PlaylistBuilderAlgorithm::algo_catalog()]);
    return;
}

// ── get_rotation_rules ────────────────────────────────────────────────────

if ($action === 'get_rotation_rules') {
    try {
        $rules = mc1_db('mcaster1_media')->query(
            'SELECT id, name, artist_separation, song_separation_hrs, genre_ratios,
                    jingle_every_n, spot_on_hour, spot_on_half, is_active
             FROM rotation_rules WHERE is_active=1 ORDER BY name'
        )->fetchAll();
        mc1_api_respond(['ok' => true, 'rules' => $rules]);
    } catch (Exception $e) {
        mc1_api_respond(['ok' => false, 'error' => $e->getMessage()], 500);
    }
    return;
}

// ── preview ───────────────────────────────────────────────────────────────
// We generate a 20-track preview without writing to the database.

if ($action === 'preview') {
    try {
        $result = PlaylistManagerPro::preview($req, 20);
        mc1_api_respond($result);
    } catch (\Throwable $e) {
        mc1_log(2, 'Playlist preview error: ' . $e->getMessage());
        mc1_api_respond(['ok' => false, 'error' => $e->getMessage()], 500);
    }
    return;
}

// ── generate ──────────────────────────────────────────────────────────────
// We run the full generation: DB write + M3U file.

if ($action === 'generate') {
    try {
        $result = PlaylistManagerPro::generate($req);
        mc1_log(4, 'Playlist generated: ' . ($req['name'] ?? '?') . ' → ' . ($result['track_count'] ?? 0) . ' tracks');
        mc1_api_respond($result, $result['ok'] ? 200 : 400);
    } catch (\Throwable $e) {
        mc1_log(2, 'Playlist generate error: ' . $e->getMessage());
        mc1_api_respond(['ok' => false, 'error' => $e->getMessage()], 500);
    }
    return;
}

// ── get_clock_templates ───────────────────────────────────────────────────

if ($action === 'get_clock_templates') {
    try {
        $rows = mc1_db('mcaster1_media')->query(
            'SELECT id, hour, day_of_week, name, segment_json, is_active
             FROM clock_hours ORDER BY hour, day_of_week'
        )->fetchAll();
        mc1_api_respond(['ok' => true, 'templates' => $rows]);
    } catch (Exception $e) {
        mc1_api_respond(['ok' => false, 'error' => $e->getMessage()], 500);
    }
    return;
}

// ── save_clock_template ───────────────────────────────────────────────────

if ($action === 'save_clock_template') {
    $hour         = (int)($req['hour']         ?? -1);
    $name         = trim($req['name']          ?? '');
    $segment_json = json_encode($req['segments'] ?? []);
    $day_of_week  = isset($req['day_of_week']) && is_numeric($req['day_of_week'])
                    ? (int)$req['day_of_week'] : null;

    if ($hour < 0 || $hour > 23) {
        mc1_api_respond(['ok' => false, 'error' => 'hour must be 0-23'], 400);
        return;
    }
    try {
        $db = mc1_db('mcaster1_media');
        $db->prepare(
            "INSERT INTO clock_hours (hour, day_of_week, name, segment_json, is_active)
             VALUES (?, ?, ?, ?, 1)
             ON DUPLICATE KEY UPDATE name=VALUES(name), segment_json=VALUES(segment_json), is_active=1"
        )->execute([$hour, $day_of_week, $name ?: "Hour $hour", $segment_json]);
        mc1_api_respond(['ok' => true]);
    } catch (Exception $e) {
        mc1_api_respond(['ok' => false, 'error' => $e->getMessage()], 500);
    }
    return;
}

// ── unknown action ────────────────────────────────────────────────────────

mc1_api_respond(['ok' => false, 'error' => 'Unknown action: ' . htmlspecialchars($action, ENT_QUOTES, 'UTF-8')], 400);
