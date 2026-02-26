<?php
/**
 * serverstats.php — Server Stats API (per-mount + global DNAS / Icecast2 health)
 *
 * File:    src/linux/web_ui/app/api/serverstats.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-02-23
 * Purpose: We fetch deep server stats for a streaming_servers entry and return
 *          structured JSON with global health, per-mount metrics, and a flag
 *          indicating which mounts belong to our configured encoder slots.
 *
 * Actions (all POST JSON, all require auth):
 *  get_stats   — fetch full XML stats for a server by id, cross-reference
 *                our encoder_configs.server_mount values, return rich data
 *  get_summary — lightweight summary (total listeners, bandwidth, source count)
 *                for the server table refresh without the full per-mount payload
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We use first-person plural throughout all comments
 *  - We use mc1_api_respond() for all JSON responses
 *  - We use raw SQL only (no ORMs)
 *
 * CHANGELOG:
 *  2026-02-23  v1.0.0  Initial implementation — full stats + summary actions
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/../inc/mc1_config.php';
require_once __DIR__ . '/../inc/db.php';
require_once __DIR__ . '/../inc/traits.db.class.php';
require_once __DIR__ . '/../inc/logger.php';
require_once __DIR__ . '/../inc/auth.php';
require_once __DIR__ . '/../inc/user_auth.php';
require_once __DIR__ . '/../inc/metrics.serverstats.class.php';

header('Content-Type: application/json');

/* ── Auth gate ───────────────────────────────────────────────────────────── */
if (!mc1_is_authed()) {
    mc1_api_respond(['error' => 'Unauthorized'], 403);
    return;
}

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    mc1_api_respond(['error' => 'POST required'], 405);
    return;
}

$raw    = (string)file_get_contents('php://input');
$body   = json_decode($raw, true) ?: [];
$action = (string)($body['action'] ?? '');

/* ── Shared: look up server record + our mount paths ─────────────────────── */
function mc1_serverstats_load_server(int $id): ?array {
    if ($id < 1) return null;
    try {
        $pdo  = mc1_db('mcaster1_encoder');
        $stmt = $pdo->prepare(
            'SELECT id, name, server_type, host, port, mount_point,
                    stat_username, stat_password, ssl_enabled, is_active
             FROM streaming_servers WHERE id=? LIMIT 1'
        );
        $stmt->execute([$id]);
        return $stmt->fetch(\PDO::FETCH_ASSOC) ?: null;
    } catch (Exception $e) {
        return null;
    }
}

/* We collect the server_mount values from our encoder_configs for "our mounts" flagging */
function mc1_serverstats_our_mounts(string $host, int $port): array {
    try {
        $pdo  = mc1_db('mcaster1_encoder');
        /* We match mounts from any config targeting this host:port */
        $stmt = $pdo->prepare(
            'SELECT server_mount FROM encoder_configs
             WHERE server_host = ? AND server_port = ?'
        );
        $stmt->execute([$host, $port]);
        $rows = $stmt->fetchAll(\PDO::FETCH_COLUMN);
        return array_values(array_filter($rows));
    } catch (Exception $e) {
        return [];
    }
}

/* We choose the stats URL path by server_type (same logic as servers.php) */
function mc1_serverstats_path(string $server_type): string {
    if ($server_type === 'mcaster1dnas') return '/admin/mcaster1stats';
    if ($server_type === 'shoutcast2')   return '/statistics?sid=1';
    if ($server_type === 'shoutcast1')   return '/7.html';
    /* Icecast2 and Steamcast both use the JSON stats endpoint */
    return '/status-json.xsl';
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: get_stats — fetch full per-mount + global stats for one server.
 * We return the entire parsed data structure for the stats modal.
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'get_stats') {
    $id  = (int)($body['id'] ?? 0);
    $srv = mc1_serverstats_load_server($id);
    if (!$srv) {
        mc1_api_respond(['error' => 'Server not found or id required'], 404);
        return;
    }

    $stats_path = mc1_serverstats_path($srv['server_type']);
    $ss = new Mc1ServerStats([
        'host'        => $srv['host'],
        'port'        => (int)$srv['port'],
        'ssl_enabled' => (bool)$srv['ssl_enabled'],
        'username'    => $srv['stat_username'],
        'password'    => $srv['stat_password'],
        'stats_path'  => $stats_path,
        'timeout'     => 10,
    ]);

    if (!$ss->fetch()) {
        mc1_api_respond([
            'ok'    => false,
            'error' => $ss->get_error() ?? 'Fetch failed',
            'server_name' => $srv['name'],
        ], 200); /* We return 200 so the modal can display the error gracefully */
        return;
    }

    /* We flag which mounts belong to our configured encoder slots */
    $our_mounts = mc1_serverstats_our_mounts($srv['host'], (int)$srv['port']);
    $ss->mark_our_mounts($our_mounts);

    $data = $ss->get_data();

    /* We also update last_checked/last_status/last_listeners in streaming_servers */
    $g = $data['global'];
    try {
        mc1_db('mcaster1_encoder')->prepare(
            'UPDATE streaming_servers
             SET last_checked=NOW(), last_status=\'online\', last_listeners=?
             WHERE id=?'
        )->execute([(int)($g['_listeners'] ?? 0), $id]);
    } catch (Exception $e) {
        mc1_log(2, 'serverstats DB update failed', json_encode(['id' => $id, 'err' => $e->getMessage()]));
    }

    mc1_api_respond([
        'ok'          => true,
        'server_name' => $srv['name'],
        'server_type' => $srv['server_type'],
        'fetch_ms'    => $ss->get_fetch_ms(),
        'our_mounts'  => $our_mounts,
        'global'      => $data['global'],
        'sources'     => $data['sources'],
    ]);
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: get_summary — lightweight summary for table row status refresh.
 * We return only global totals, not the full per-mount source list.
 * We update last_checked/last_status/last_listeners in DB.
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'get_summary') {
    $id  = (int)($body['id'] ?? 0);
    $srv = mc1_serverstats_load_server($id);
    if (!$srv) {
        mc1_api_respond(['error' => 'Server not found or id required'], 404);
        return;
    }

    $stats_path = mc1_serverstats_path($srv['server_type']);
    $ss = new Mc1ServerStats([
        'host'        => $srv['host'],
        'port'        => (int)$srv['port'],
        'ssl_enabled' => (bool)$srv['ssl_enabled'],
        'username'    => $srv['stat_username'],
        'password'    => $srv['stat_password'],
        'stats_path'  => $stats_path,
        'timeout'     => 6,
    ]);

    $ok = $ss->fetch();
    if ($ok) {
        $summary = $ss->get_summary();
        try {
            mc1_db('mcaster1_encoder')->prepare(
                'UPDATE streaming_servers
                 SET last_checked=NOW(), last_status=\'online\', last_listeners=?
                 WHERE id=?'
            )->execute([$summary['listeners'], $id]);
        } catch (Exception $e) {}
        mc1_api_respond(array_merge(['ok' => true, 'id' => $id], $summary));
    } else {
        try {
            mc1_db('mcaster1_encoder')->prepare(
                'UPDATE streaming_servers SET last_checked=NOW(), last_status=\'offline\' WHERE id=?'
            )->execute([$id]);
        } catch (Exception $e) {}
        mc1_api_respond(['ok' => false, 'id' => $id, 'error' => $ss->get_error()]);
    }
    return;
}

/* We return 400 for unknown actions */
mc1_api_respond(['error' => 'Unknown action: ' . $action], 400);
