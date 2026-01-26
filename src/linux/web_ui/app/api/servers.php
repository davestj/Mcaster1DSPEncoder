<?php
/**
 * servers.php — Streaming Servers CRUD + Live Stats Proxy API
 *
 * File:    src/linux/web_ui/app/api/servers.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-02-23
 * Purpose: We manage the streaming_servers table (add/update/delete/list) and
 *          proxy live listener stats from Icecast2, Shoutcast v1/v2, Steamcast,
 *          and Mcaster1DNAS endpoints. We update last_checked/last_status/
 *          last_listeners in DB on every stats fetch.
 *
 * Actions (all POST JSON, all require auth):
 *  list       — return all servers with last known status
 *  add        — create a new server entry (admin only)
 *  update     — update an existing server (admin only)
 *  delete     — remove a server entry (admin only)
 *  get_stats  — fetch live listener count for one (id=N) or all active servers
 *  test       — test a server connection without saving (admin only)
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We use first-person plural throughout all comments
 *  - We use mc1_api_respond() for all JSON responses
 *  - We use raw SQL only (no ORMs)
 *
 * CHANGELOG:
 *  2026-02-23  v1.0.0  Initial implementation — full CRUD + cURL stats proxy
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/../inc/mc1_config.php';
require_once __DIR__ . '/../inc/db.php';
require_once __DIR__ . '/../inc/traits.db.class.php';
require_once __DIR__ . '/../inc/logger.php';
require_once __DIR__ . '/../inc/auth.php';
require_once __DIR__ . '/../inc/user_auth.php';

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

$user     = mc1_current_user();
$is_admin = $user && !empty($user['can_admin']);

/* We define valid server types matching the streaming_servers enum */
$valid_types = ['icecast2', 'shoutcast1', 'shoutcast2', 'steamcast', 'mcaster1dnas'];

/* ══════════════════════════════════════════════════════════════════════════
 * action: list — return all streaming servers with current status fields
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'list') {
    try {
        $pdo  = mc1_db('mcaster1_encoder');
        $stmt = $pdo->query(
            'SELECT id, name, server_type, host, port, mount_point, stat_username,
                    ssl_enabled, is_active, display_order,
                    last_checked, last_status, last_listeners, created_at
             FROM streaming_servers
             ORDER BY display_order ASC, name ASC'
        );
        $rows = $stmt->fetchAll(\PDO::FETCH_ASSOC);
        foreach ($rows as &$r) {
            $r['id']             = (int)$r['id'];
            $r['port']           = (int)$r['port'];
            $r['ssl_enabled']    = (bool)$r['ssl_enabled'];
            $r['is_active']      = (bool)$r['is_active'];
            $r['display_order']  = (int)$r['display_order'];
            $r['last_listeners'] = (int)$r['last_listeners'];
        }
        unset($r);
        mc1_api_respond(['ok' => true, 'servers' => $rows]);
    } catch (Exception $e) {
        mc1_log(2, 'servers list failed', json_encode(['err' => $e->getMessage()]));
        mc1_api_respond(['error' => 'Query failed: ' . $e->getMessage()], 500);
    }
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: add — insert a new streaming server record (admin only)
 * We require name, server_type, and host. All other fields have defaults.
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'add') {
    if (!$is_admin) {
        mc1_api_respond(['error' => 'Admin permission required'], 403);
        return;
    }
    $name = trim($body['name'] ?? '');
    $type = in_array($body['server_type'] ?? '', $valid_types, true) ? $body['server_type'] : '';
    $host = trim($body['host'] ?? '');
    if ($name === '' || $type === '' || $host === '') {
        mc1_api_respond(['error' => 'name, server_type, and host are required'], 400);
        return;
    }
    try {
        $pdo = mc1_db('mcaster1_encoder');
        $pdo->prepare(
            'INSERT INTO streaming_servers
                (name, server_type, host, port, mount_point, stat_username, stat_password,
                 ssl_enabled, is_active, display_order)
             VALUES (?,?,?,?,?,?,?,?,?,?)'
        )->execute([
            $name,
            $type,
            $host,
            min(65535, max(1, (int)($body['port'] ?? 8000))),
            trim($body['mount_point']   ?? ''),
            trim($body['stat_username'] ?? ''),
            trim($body['stat_password'] ?? ''),
            (int)(bool)($body['ssl_enabled']   ?? false),
            (int)(bool)($body['is_active']     ?? true),
            min(255, max(0, (int)($body['display_order'] ?? 0))),
        ]);
        $id = (int)$pdo->lastInsertId();
        mc1_api_respond(['ok' => true, 'id' => $id]);
    } catch (Exception $e) {
        mc1_log(2, 'servers add failed', json_encode(['err' => $e->getMessage()]));
        mc1_api_respond(['error' => 'Insert failed: ' . $e->getMessage()], 500);
    }
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: update — update an existing streaming server (admin only)
 * We only update stat_password if a new value is provided.
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'update') {
    if (!$is_admin) {
        mc1_api_respond(['error' => 'Admin permission required'], 403);
        return;
    }
    $id = (int)($body['id'] ?? 0);
    if ($id < 1) {
        mc1_api_respond(['error' => 'id required'], 400);
        return;
    }
    $name = trim($body['name'] ?? '');
    $type = in_array($body['server_type'] ?? '', $valid_types, true) ? $body['server_type'] : '';
    $host = trim($body['host'] ?? '');
    if ($name === '' || $type === '' || $host === '') {
        mc1_api_respond(['error' => 'name, server_type, and host are required'], 400);
        return;
    }
    try {
        $pdo    = mc1_db('mcaster1_encoder');
        $fields = [
            'name'          => $name,
            'server_type'   => $type,
            'host'          => $host,
            'port'          => min(65535, max(1, (int)($body['port'] ?? 8000))),
            'mount_point'   => trim($body['mount_point'] ?? ''),
            'stat_username' => trim($body['stat_username'] ?? ''),
            'ssl_enabled'   => (int)(bool)($body['ssl_enabled']   ?? false),
            'is_active'     => (int)(bool)($body['is_active']     ?? true),
            'display_order' => min(255, max(0, (int)($body['display_order'] ?? 0))),
        ];
        /* We only update the password when the caller provides a new non-empty one */
        if (!empty($body['stat_password'])) {
            $fields['stat_password'] = trim($body['stat_password']);
        }
        $sets = implode(', ', array_map(fn($k) => "$k=?", array_keys($fields)));
        $vals = array_values($fields);
        $vals[] = $id;
        $pdo->prepare("UPDATE streaming_servers SET $sets WHERE id=?")->execute($vals);
        mc1_api_respond(['ok' => true]);
    } catch (Exception $e) {
        mc1_log(2, 'servers update failed', json_encode(['id' => $id, 'err' => $e->getMessage()]));
        mc1_api_respond(['error' => 'Update failed: ' . $e->getMessage()], 500);
    }
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: delete — remove a streaming server entry (admin only)
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'delete') {
    if (!$is_admin) {
        mc1_api_respond(['error' => 'Admin permission required'], 403);
        return;
    }
    $id = (int)($body['id'] ?? 0);
    if ($id < 1) {
        mc1_api_respond(['error' => 'id required'], 400);
        return;
    }
    try {
        mc1_db('mcaster1_encoder')
            ->prepare('DELETE FROM streaming_servers WHERE id=?')
            ->execute([$id]);
        mc1_api_respond(['ok' => true]);
    } catch (Exception $e) {
        mc1_log(2, 'servers delete failed', json_encode(['id' => $id, 'err' => $e->getMessage()]));
        mc1_api_respond(['error' => 'Delete failed: ' . $e->getMessage()], 500);
    }
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: get_stats — fetch live listener counts from server endpoints.
 * We accept an optional id=N to fetch a single server; otherwise we fetch
 * all active servers. We update last_checked/last_status/last_listeners.
 * Returns { ok:true, stats:[{id, status, listeners, response_ms, error}] }
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'get_stats') {
    $id = isset($body['id']) ? (int)$body['id'] : 0;
    try {
        $pdo   = mc1_db('mcaster1_encoder');
        $where = $id > 0 ? 'WHERE id=?' : 'WHERE is_active=1';
        $par   = $id > 0 ? [$id] : [];
        $stmt  = $pdo->prepare(
            'SELECT id, server_type, host, port, mount_point,
                    stat_username, stat_password, ssl_enabled
             FROM streaming_servers ' . $where . '
             ORDER BY display_order ASC'
        );
        $stmt->execute($par);
        $servers = $stmt->fetchAll(\PDO::FETCH_ASSOC);
    } catch (Exception $e) {
        mc1_api_respond(['error' => 'Query failed: ' . $e->getMessage()], 500);
        return;
    }

    $results = [];
    foreach ($servers as $srv) {
        $t0     = microtime(true);
        $result = mc1_servers_fetch_stats($srv);
        $ms     = (int)round((microtime(true) - $t0) * 1000);
        $status    = $result['ok'] ? 'online' : 'offline';
        $listeners = (int)($result['listeners'] ?? 0);

        /* We update the DB row with fresh status, listener count, and check time */
        try {
            $pdo->prepare(
                'UPDATE streaming_servers
                 SET last_checked=NOW(), last_status=?, last_listeners=?
                 WHERE id=?'
            )->execute([$status, $listeners, (int)$srv['id']]);
        } catch (Exception $e) {
            mc1_log(2, 'servers stats DB update failed', json_encode(['id' => $srv['id'], 'err' => $e->getMessage()]));
        }

        $results[] = [
            'id'          => (int)$srv['id'],
            'status'      => $status,
            'listeners'   => $listeners,
            'response_ms' => $ms,
            'error'       => $result['error'] ?? null,
        ];
    }
    mc1_api_respond(['ok' => true, 'stats' => $results]);
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: test — test an ad-hoc server connection without saving (admin only).
 * We accept all server fields inline and return connection result immediately.
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'test') {
    if (!$is_admin) {
        mc1_api_respond(['error' => 'Admin permission required'], 403);
        return;
    }
    $host = trim($body['host'] ?? '');
    if ($host === '') {
        mc1_api_respond(['error' => 'host required'], 400);
        return;
    }
    $srv = [
        'server_type'   => $body['server_type']   ?? 'icecast2',
        'host'          => $host,
        'port'          => (int)($body['port']     ?? 8000),
        'mount_point'   => trim($body['mount_point']   ?? ''),
        'stat_username' => trim($body['stat_username'] ?? ''),
        'stat_password' => trim($body['stat_password'] ?? ''),
        'ssl_enabled'   => (int)(bool)($body['ssl_enabled'] ?? false),
    ];
    $t0     = microtime(true);
    $result = mc1_servers_fetch_stats($srv);
    $ms     = (int)round((microtime(true) - $t0) * 1000);
    mc1_api_respond([
        'ok'          => $result['ok'],
        'listeners'   => (int)($result['listeners'] ?? 0),
        'response_ms' => $ms,
        'error'       => $result['error'] ?? null,
    ]);
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * mc1_servers_fetch_stats() — We make a cURL request to the server's stats
 * endpoint and parse listener counts by server_type.
 *
 * @param array $srv  Row from streaming_servers (or ad-hoc test fields)
 * @return array { ok:bool, listeners:int, error:string|null }
 * ══════════════════════════════════════════════════════════════════════════ */
function mc1_servers_fetch_stats(array $srv): array {
    $type   = (string)($srv['server_type'] ?? 'icecast2');
    $host   = (string)($srv['host'] ?? '');
    $port   = (int)($srv['port'] ?? 8000);
    $uname  = (string)($srv['stat_username'] ?? '');
    $pass   = (string)($srv['stat_password'] ?? '');
    $ssl    = (bool)($srv['ssl_enabled'] ?? false);
    $scheme = $ssl ? 'https' : 'http';

    /* We choose the stats endpoint URL by server type */
    if ($type === 'icecast2' || $type === 'steamcast') {
        /* Icecast2/Steamcast: /status-json.xsl returns JSON without auth */
        $url = "{$scheme}://{$host}:{$port}/status-json.xsl";
    } elseif ($type === 'shoutcast1') {
        /* Shoutcast v1: /7.html returns CSV listener data — always HTTP */
        $url = "http://{$host}:{$port}/7.html";
    } elseif ($type === 'shoutcast2') {
        /* Shoutcast v2: /statistics?sid=1 returns XML stats */
        $url = "http://{$host}:{$port}/statistics?sid=1";
    } elseif ($type === 'mcaster1dnas') {
        /* Mcaster1DNAS: proprietary stats endpoint (same path used by C++ /api/v1/dnas/stats proxy) */
        $url = "{$scheme}://{$host}:{$port}/admin/mcaster1stats";
    } else {
        return ['ok' => false, 'listeners' => 0, 'error' => 'Unknown server type: ' . $type];
    }

    if (!function_exists('curl_init')) {
        return ['ok' => false, 'listeners' => 0, 'error' => 'PHP cURL extension not available'];
    }

    $ch = curl_init();
    curl_setopt_array($ch, [
        CURLOPT_URL            => $url,
        CURLOPT_RETURNTRANSFER => true,
        CURLOPT_TIMEOUT        => 5,
        CURLOPT_CONNECTTIMEOUT => 3,
        CURLOPT_FOLLOWLOCATION => true,
        CURLOPT_MAXREDIRS      => 2,
        CURLOPT_SSL_VERIFYPEER => false,
        CURLOPT_SSL_VERIFYHOST => 0,
        CURLOPT_USERAGENT      => 'Mcaster1Encoder/1.4.0 (stats-proxy; +https://mcaster1.com)',
    ]);
    if ($uname !== '' && $pass !== '') {
        curl_setopt($ch, CURLOPT_USERPWD, $uname . ':' . $pass);
        curl_setopt($ch, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    }

    $body_raw  = curl_exec($ch);
    $http_code = (int)curl_getinfo($ch, CURLINFO_HTTP_CODE);
    $curl_err  = curl_error($ch);
    curl_close($ch);

    if ($curl_err !== '' || $body_raw === false) {
        return ['ok' => false, 'listeners' => 0, 'error' => $curl_err ?: 'Connection failed'];
    }
    if ($http_code === 401 || $http_code === 403) {
        /* We treat auth-required responses as online (server is reachable) but listener count unknown */
        return ['ok' => true, 'listeners' => 0, 'error' => "Auth required (HTTP {$http_code}) — add stats credentials"];
    }
    if ($http_code < 200 || $http_code >= 300) {
        return ['ok' => false, 'listeners' => 0, 'error' => "HTTP {$http_code}"];
    }

    /* We parse listener count based on server type */
    $listeners = 0;

    if ($type === 'icecast2' || $type === 'steamcast') {
        /* We try JSON first (modern Icecast2/Steamcast) */
        $json = json_decode($body_raw, true);
        if (is_array($json) && isset($json['icestats'])) {
            $ice = $json['icestats'];
            if (isset($ice['listeners'])) {
                $listeners = (int)$ice['listeners'];
            } elseif (isset($ice['source'])) {
                $sources = $ice['source'];
                /* We handle both single-source (object) and multi-source (array) responses */
                if (isset($sources['listeners'])) {
                    $listeners = (int)$sources['listeners'];
                } elseif (is_array($sources)) {
                    foreach ($sources as $src) {
                        $listeners += (int)($src['listeners'] ?? 0);
                    }
                }
            }
        } else {
            /* We fall back to XML parsing for older Icecast versions */
            if (preg_match('/<listeners>(\d+)<\/listeners>/i', $body_raw, $m)) {
                $listeners = (int)$m[1];
            } elseif (preg_match('/<total_listeners>(\d+)<\/total_listeners>/i', $body_raw, $m)) {
                $listeners = (int)$m[1];
            }
        }
    } elseif ($type === 'mcaster1dnas') {
        /* We parse Mcaster1DNAS XML <listeners> or <total_listeners> */
        if (preg_match('/<listeners>(\d+)<\/listeners>/i', $body_raw, $m)) {
            $listeners = (int)$m[1];
        } elseif (preg_match('/<total_listeners>(\d+)<\/total_listeners>/i', $body_raw, $m)) {
            $listeners = (int)$m[1];
        }
    } elseif ($type === 'shoutcast1') {
        /* Shoutcast v1 /7.html CSV format: CurrentListeners,Status,PeakListeners,...  */
        if (preg_match('/^(\d+),/', trim(strip_tags($body_raw)), $m)) {
            $listeners = (int)$m[1];
        }
    } elseif ($type === 'shoutcast2') {
        /* Shoutcast v2 /statistics XML: <CURRENTLISTENERS>N</CURRENTLISTENERS> */
        if (preg_match('/<CURRENTLISTENERS>(\d+)<\/CURRENTLISTENERS>/i', $body_raw, $m)) {
            $listeners = (int)$m[1];
        }
    }

    return ['ok' => true, 'listeners' => $listeners, 'error' => null];
}

/* We return 400 for any unknown action */
mc1_api_respond(['error' => 'Unknown action: ' . $action], 400);
