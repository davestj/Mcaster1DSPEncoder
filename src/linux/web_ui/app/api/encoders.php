<?php
// api/encoders.php — Encoder config CRUD (MySQL) + admin user/DB management.
//
// IMPORTANT: start/stop/restart are NOT proxied here — browser JS must call
// /api/v1/encoders/{slot}/{action} DIRECTLY to avoid C++ thread pool deadlock.
// (PHP→C++ loopback via curl on same port exhausts the thread pool.)
//
// Actions: list_configs, get_config, save_config, delete_config, sync_yaml
//          load_playlist (MySQL update only — JS does actual C++ call directly)
//          add_user, delete_user, toggle_user, reset_password, db_stats
//
// No exit()/die() — uopz active. JSON responses only.

define('MC1_BOOT', true);
require_once __DIR__ . '/../inc/auth.php';
require_once __DIR__ . '/../inc/db.php';
require_once __DIR__ . '/../inc/user_auth.php';

header('Content-Type: application/json; charset=utf-8');

if (!mc1_is_authed()) {
    http_response_code(403);
    echo json_encode(['ok' => false, 'error' => 'Forbidden']);
    return;
}

$raw    = file_get_contents('php://input');
$req    = ($raw !== '') ? json_decode($raw, true) : [];
if (!is_array($req)) $req = [];
$action = (string)($req['action'] ?? '');

// ── Encoder config CRUD (MySQL — no C++ loopback) ────────────────────────
// NOTE: start/stop/restart/playlist/dsp calls go browser→C++ directly.
// Never proxy PHP→C++ on loopback: causes thread pool deadlock.

if ($action === 'list_configs') {
    try {
        $rows = mc1_db('mcaster1_encoder')->query(
            'SELECT id, slot_id, name, input_type, playlist_path, codec, bitrate_kbps,
                    sample_rate, channels, server_host, server_port, server_mount,
                    server_user, server_protocol, station_name, genre, website_url,
                    volume, shuffle, repeat_all,
                    eq_enabled, eq_preset, agc_enabled, crossfade_enabled, crossfade_duration,
                    is_active, created_at, modified_at
             FROM encoder_configs
             ORDER BY slot_id'
        )->fetchAll();
        echo json_encode(['ok' => true, 'configs' => $rows]);
    } catch (Exception $e) {
        http_response_code(500);
        echo json_encode(['ok' => false, 'error' => $e->getMessage()]);
    }
    return;
}

if ($action === 'get_config') {
    $id = (int)($req['id'] ?? 0);
    if ($id <= 0) { http_response_code(400); echo json_encode(['ok'=>false,'error'=>'id required']); return; }
    try {
        $st = mc1_db('mcaster1_encoder')->prepare('SELECT * FROM encoder_configs WHERE id=?');
        $st->execute([$id]);
        $row = $st->fetch();
        if (!$row) { http_response_code(404); echo json_encode(['ok'=>false,'error'=>'Not found']); return; }
        echo json_encode(['ok' => true, 'config' => $row]);
    } catch (Exception $e) {
        http_response_code(500);
        echo json_encode(['ok' => false, 'error' => $e->getMessage()]);
    }
    return;
}

if ($action === 'save_config') {
    $user = mc1_current_user();
    if (!$user || empty($user['can_admin'])) {
        http_response_code(403); echo json_encode(['ok'=>false,'error'=>'Admin permission required']); return;
    }
    $id       = (int)($req['id'] ?? 0);
    $slot_id  = (int)($req['slot_id'] ?? 0);
    $name     = trim($req['name'] ?? '');
    if ($slot_id <= 0 || $name === '') {
        http_response_code(400); echo json_encode(['ok'=>false,'error'=>'slot_id and name required']); return;
    }
    $fields = [
        'slot_id'       => $slot_id,
        'name'          => $name,
        'input_type'    => in_array($req['input_type'] ?? '', ['device','playlist','url']) ? $req['input_type'] : 'playlist',
        'playlist_path' => trim($req['playlist_path'] ?? ''),
        'codec'         => in_array($req['codec'] ?? '', ['mp3','vorbis','opus','flac','aac_lc','aac_he','aac_hev2','aac_eld']) ? $req['codec'] : 'mp3',
        'bitrate_kbps'  => min(320, max(32, (int)($req['bitrate_kbps'] ?? 128))),
        'sample_rate'   => in_array((int)($req['sample_rate'] ?? 44100), [22050,44100,48000]) ? (int)$req['sample_rate'] : 44100,
        'channels'      => in_array((int)($req['channels'] ?? 2), [1,2]) ? (int)$req['channels'] : 2,
        'server_host'   => trim($req['server_host'] ?? ''),
        'server_port'   => min(65535, max(1, (int)($req['server_port'] ?? 8000))),
        'server_mount'  => trim($req['server_mount'] ?? '/live'),
        'server_user'   => trim($req['server_user'] ?? 'source'),
        'server_pass'   => (function() use ($req, $id) {
            $submitted = trim($req['server_pass'] ?? '');
            if ($submitted !== '') return $submitted;
            if ($id > 0) {
                try {
                    $st = mc1_db('mcaster1_encoder')->prepare('SELECT server_pass FROM encoder_configs WHERE id=? LIMIT 1');
                    $st->execute([$id]);
                    return (string)($st->fetchColumn() ?: '');
                } catch (\Throwable $e) {}
            }
            return '';
        })(),
        'station_name'  => trim($req['station_name'] ?? ''),
        'description'   => trim($req['description'] ?? ''),
        'genre'         => trim($req['genre'] ?? ''),
        'website_url'   => trim($req['website_url'] ?? ''),
        'volume'        => min(2.0, max(0.0, (float)($req['volume'] ?? 1.0))),
        'shuffle'       => (int)(bool)($req['shuffle'] ?? false),
        'repeat_all'    => (int)(bool)($req['repeat_all'] ?? true),
        'archive_enabled'    => (int)(bool)($req['archive_enabled'] ?? false),
        'archive_dir'        => trim($req['archive_dir'] ?? ''),
        'server_protocol'    => in_array($req['server_protocol'] ?? '', ['icecast2','shoutcast1','shoutcast2','mcaster1']) ? $req['server_protocol'] : 'icecast2',
        'eq_enabled'         => (int)(bool)($req['eq_enabled'] ?? false),
        'eq_preset'          => in_array($req['eq_preset'] ?? '', ['flat','classic_rock','country','modern_rock','broadcast','spoken_word']) ? $req['eq_preset'] : 'flat',
        'agc_enabled'        => (int)(bool)($req['agc_enabled'] ?? false),
        'crossfade_enabled'  => (int)(bool)($req['crossfade_enabled'] ?? true),
        'crossfade_duration' => min(10.0, max(1.0, (float)($req['crossfade_duration'] ?? 3.0))),
        'is_active'          => (int)(bool)($req['is_active'] ?? true),
    ];
    try {
        $db = mc1_db('mcaster1_encoder');
        if ($id > 0) {
            $sets = implode(', ', array_map(fn($k) => "$k=?", array_keys($fields)));
            $vals = array_values($fields);
            $vals[] = $id;
            $db->prepare("UPDATE encoder_configs SET $sets WHERE id=?")->execute($vals);
            echo json_encode(['ok' => true, 'id' => $id]);
        } else {
            $cols = implode(', ', array_keys($fields));
            $phs  = implode(', ', array_fill(0, count($fields), '?'));
            $db->prepare("INSERT INTO encoder_configs ($cols) VALUES ($phs)")->execute(array_values($fields));
            echo json_encode(['ok' => true, 'id' => (int)$db->lastInsertId()]);
        }
    } catch (Exception $e) {
        http_response_code(500);
        echo json_encode(['ok' => false, 'error' => $e->getMessage()]);
    }
    return;
}

if ($action === 'copy_config') {
    $user = mc1_current_user();
    if (!$user || empty($user['can_admin'])) {
        http_response_code(403);
        echo json_encode(['ok'=>false,'error'=>'Admin permission required']);
        return;
    }
    $source_id = (int)($req['source_id'] ?? 0);
    if ($source_id <= 0) {
        http_response_code(400);
        echo json_encode(['ok'=>false,'error'=>'source_id required']);
        return;
    }
    try {
        $db = mc1_db('mcaster1_encoder');

        // Fetch the full source row
        $st = $db->prepare('SELECT * FROM encoder_configs WHERE id=? LIMIT 1');
        $st->execute([$source_id]);
        $src = $st->fetch(PDO::FETCH_ASSOC);
        if (!$src) {
            http_response_code(404);
            echo json_encode(['ok'=>false,'error'=>'Source encoder not found']);
            return;
        }

        // Next available slot_id = current MAX + 1
        $next_slot = (int)$db->query('SELECT COALESCE(MAX(slot_id),0)+1 FROM encoder_configs')->fetchColumn();

        // Strip auto-generated columns, override key fields for the copy
        unset($src['id'], $src['created_at'], $src['modified_at']);
        $src['slot_id']    = $next_slot;
        $src['name']       = $src['name'] . ' (Copy)';
        $src['auto_start'] = 0;
        $src['is_active']  = 1;

        $cols = implode(', ', array_keys($src));
        $phs  = implode(', ', array_fill(0, count($src), '?'));
        $db->prepare("INSERT INTO encoder_configs ($cols) VALUES ($phs)")->execute(array_values($src));
        $new_id = (int)$db->lastInsertId();

        echo json_encode(['ok' => true, 'id' => $new_id, 'slot_id' => $next_slot]);
    } catch (Exception $e) {
        http_response_code(500);
        echo json_encode(['ok'=>false,'error'=>$e->getMessage()]);
    }
    return;
}

if ($action === 'delete_config') {
    $user = mc1_current_user();
    if (!$user || empty($user['can_admin'])) {
        http_response_code(403); echo json_encode(['ok'=>false,'error'=>'Admin permission required']); return;
    }
    $id = (int)($req['id'] ?? 0);
    if ($id <= 0) { http_response_code(400); echo json_encode(['ok'=>false,'error'=>'id required']); return; }
    try {
        mc1_db('mcaster1_encoder')->prepare('DELETE FROM encoder_configs WHERE id=?')->execute([$id]);
        echo json_encode(['ok' => true]);
    } catch (Exception $e) {
        http_response_code(500);
        echo json_encode(['ok' => false, 'error' => $e->getMessage()]);
    }
    return;
}

if ($action === 'update_playlist') {
    // Update just the playlist_path for a config slot (no C++ call — JS does that directly)
    // Accepts either config_id (MySQL PK) or slot_id (1-based slot number)
    $config_id = (int)($req['config_id'] ?? 0);
    $slot_id   = (int)($req['slot_id']   ?? 0);
    $path      = trim($req['path'] ?? '');
    if ($path === '' || ($config_id <= 0 && $slot_id <= 0)) {
        http_response_code(400);
        echo json_encode(['ok'=>false,'error'=>'path and config_id or slot_id required']);
        return;
    }
    // Resolve config_id from slot_id if needed
    if ($config_id <= 0 && $slot_id > 0) {
        try {
            $st = mc1_db('mcaster1_encoder')->prepare('SELECT id FROM encoder_configs WHERE slot_id=? LIMIT 1');
            $st->execute([$slot_id]);
            $config_id = (int)($st->fetchColumn() ?: 0);
        } catch(Exception $e) {}
        if ($config_id <= 0) {
            http_response_code(404);
            echo json_encode(['ok'=>false,'error'=>'No encoder config for slot '.$slot_id]);
            return;
        }
    }
    try {
        mc1_db('mcaster1_encoder')->prepare(
            'UPDATE encoder_configs SET playlist_path=?, modified_at=NOW() WHERE id=?'
        )->execute([$path, $config_id]);
        echo json_encode(['ok' => true]);
    } catch (Exception $e) {
        http_response_code(500);
        echo json_encode(['ok' => false, 'error' => $e->getMessage()]);
    }
    return;
}

// NOTE: start/stop/restart/load_playlist/set_dsp/set_metadata are NOT here.
// Browser JS must call /api/v1/... directly to avoid C++ thread pool deadlock.
// If needed for legacy compat, a non-blocking fire-and-forget approach could work.

// ── Admin-only actions ────────────────────────────────────────────────────

$user     = mc1_current_user();
$is_admin = $user && !empty($user['can_admin']);
$admin_actions = ['add_user','update_user','delete_user','toggle_user','reset_password','db_stats','get_token','generate_token'];

if (!$is_admin && in_array($action, $admin_actions, true)) {
    http_response_code(403);
    echo json_encode(['ok' => false, 'error' => 'Admin permission required']);
    return;
}

if ($action === 'add_user') {
    $username = trim($req['username'] ?? '');
    $pass     = $req['password'] ?? '';
    $role_id  = (int)($req['role_id'] ?? 2);
    $email    = trim($req['email'] ?? '');
    $dname    = trim($req['display_name'] ?? '');
    if ($username === '' || strlen($pass) < 6) {
        http_response_code(400);
        echo json_encode(['ok'=>false,'error'=>'Username and password (min 6) required']);
        return;
    }
    try {
        $db = mc1_db('mcaster1_encoder');
        $hash = password_hash($pass, PASSWORD_BCRYPT);
        $st = $db->prepare('INSERT INTO users (username, email, display_name, password_hash, role_id) VALUES (?,?,?,?,?)');
        $st->execute([$username, $email, $dname, $hash, $role_id]);
        echo json_encode(['ok'=>true, 'id'=>(int)$db->lastInsertId()]);
    } catch (Exception $e) {
        http_response_code(500);
        echo json_encode(['ok'=>false,'error'=>$e->getMessage()]);
    }
    return;
}

if ($action === 'update_user') {
    $id = (int)($req['id'] ?? 0);
    if ($id <= 0) {
        http_response_code(400);
        echo json_encode(['ok' => false, 'error' => 'id required']);
        return;
    }
    /* We build the SET clause only from fields that were actually supplied */
    $updates = [];
    $params  = [];
    if (isset($req['username']) && trim($req['username']) !== '') {
        $updates[] = 'username=?';
        $params[]  = trim($req['username']);
    }
    if (array_key_exists('display_name', $req)) {
        $updates[] = 'display_name=?';
        $params[]  = trim($req['display_name']);
    }
    if (array_key_exists('email', $req)) {
        $updates[] = 'email=?';
        $params[]  = trim($req['email']);
    }
    if (isset($req['role_id']) && (int)$req['role_id'] > 0) {
        $updates[] = 'role_id=?';
        $params[]  = (int)$req['role_id'];
    }
    if (isset($req['password']) && strlen($req['password']) >= 6) {
        $updates[] = 'password_hash=?';
        $params[]  = password_hash($req['password'], PASSWORD_BCRYPT);
    }
    if (empty($updates)) {
        echo json_encode(['ok' => true]); /* We treat nothing-to-update as success */
        return;
    }
    $params[] = $id;
    try {
        mc1_db('mcaster1_encoder')->prepare(
            'UPDATE users SET ' . implode(', ', $updates) . ' WHERE id=?'
        )->execute($params);
        echo json_encode(['ok' => true]);
    } catch (Exception $e) {
        http_response_code(500);
        echo json_encode(['ok' => false, 'error' => $e->getMessage()]);
    }
    return;
}

if ($action === 'delete_user') {
    $id = (int)($req['id'] ?? 0);
    if ($id <= 0) { http_response_code(400); echo json_encode(['ok'=>false,'error'=>'id required']); return; }
    try {
        $db = mc1_db('mcaster1_encoder');
        $db->prepare('DELETE FROM user_sessions WHERE user_id=?')->execute([$id]);
        $db->prepare('DELETE FROM users WHERE id=?')->execute([$id]);
        echo json_encode(['ok'=>true]);
    } catch (Exception $e) {
        http_response_code(500);
        echo json_encode(['ok'=>false,'error'=>$e->getMessage()]);
    }
    return;
}

if ($action === 'toggle_user') {
    $id     = (int)($req['id'] ?? 0);
    $active = (int)($req['is_active'] ?? 0) ? 1 : 0;
    if ($id <= 0) { http_response_code(400); echo json_encode(['ok'=>false,'error'=>'id required']); return; }
    try {
        mc1_db('mcaster1_encoder')->prepare('UPDATE users SET is_active=? WHERE id=?')->execute([$active, $id]);
        echo json_encode(['ok'=>true]);
    } catch (Exception $e) {
        http_response_code(500);
        echo json_encode(['ok'=>false,'error'=>$e->getMessage()]);
    }
    return;
}

if ($action === 'reset_password') {
    $id   = (int)($req['id'] ?? 0);
    $pass = $req['password'] ?? '';
    if ($id <= 0 || strlen($pass) < 6) {
        http_response_code(400);
        echo json_encode(['ok'=>false,'error'=>'id and password (min 6) required']);
        return;
    }
    try {
        $hash = password_hash($pass, PASSWORD_BCRYPT);
        mc1_db('mcaster1_encoder')->prepare('UPDATE users SET password_hash=? WHERE id=?')->execute([$hash, $id]);
        echo json_encode(['ok'=>true]);
    } catch (Exception $e) {
        http_response_code(500);
        echo json_encode(['ok'=>false,'error'=>$e->getMessage()]);
    }
    return;
}

/* ── We ensure the site_settings table exists before any token action ── */
function mc1_ensure_site_settings(): void {
    mc1_db('mcaster1_encoder')->exec(
        'CREATE TABLE IF NOT EXISTS site_settings (
            `key`      VARCHAR(100) NOT NULL PRIMARY KEY,
            `value`    TEXT,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
         ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4'
    );
}

if ($action === 'get_token') {
    try {
        mc1_ensure_site_settings();
        $row = mc1_db('mcaster1_encoder')
            ->query("SELECT `value`, updated_at FROM site_settings WHERE `key`='api_token' LIMIT 1")
            ->fetch();
        if ($row && $row['value'] !== '') {
            echo json_encode(['ok' => true, 'token' => $row['value'], 'updated_at' => $row['updated_at']]);
        } else {
            echo json_encode(['ok' => true, 'token' => '']);
        }
    } catch (Exception $e) {
        http_response_code(500);
        echo json_encode(['ok' => false, 'error' => $e->getMessage()]);
    }
    return;
}

if ($action === 'generate_token') {
    try {
        mc1_ensure_site_settings();
        /* We generate a 64-character hex token (32 cryptographically secure bytes) */
        $token = bin2hex(random_bytes(32));
        mc1_db('mcaster1_encoder')->prepare(
            "INSERT INTO site_settings (`key`, `value`) VALUES ('api_token', ?)
             ON DUPLICATE KEY UPDATE `value`=VALUES(`value`), updated_at=NOW()"
        )->execute([$token]);
    } catch (Exception $e) {
        http_response_code(500);
        echo json_encode(['ok' => false, 'error' => $e->getMessage()]);
        return;
    }

    /* We attempt to write the new token into the YAML config file (best-effort).
     * We replace the api-token: line in-place using a regex so we don't need to
     * parse the full YAML structure. */
    $wrote_yaml = false;
    $yaml_note  = '';
    $cfg = defined('MC1_CONFIG_FILE') ? MC1_CONFIG_FILE : '';
    if ($cfg !== '' && is_readable($cfg)) {
        if (!is_writable($cfg)) {
            $yaml_note = 'Config file is not writable by the web server user';
        } else {
            $yaml = file_get_contents($cfg);
            if ($yaml !== false) {
                $count   = 0;
                $updated = preg_replace(
                    '/^(\s+api-token:\s+).*$/m',
                    '${1}"' . $token . '"',
                    $yaml, -1, $count
                );
                if ($count > 0 && $updated !== null) {
                    $wrote_yaml = (file_put_contents($cfg, $updated) !== false);
                    if (!$wrote_yaml) $yaml_note = 'file_put_contents failed';
                } else {
                    $yaml_note = 'api-token key not found in config file';
                }
            } else {
                $yaml_note = 'Could not read config file';
            }
        }
    } else {
        $yaml_note = $cfg === '' ? 'MC1_CONFIG_FILE not defined' : 'Config file not found';
    }

    echo json_encode([
        'ok'         => true,
        'token'      => $token,
        'wrote_yaml' => $wrote_yaml,
        'yaml_note'  => $yaml_note,
        'yaml_path'  => $cfg,
    ]);
    return;
}

if ($action === 'db_stats') {
    try {
        $rows = mc1_db('information_schema')->query(
            "SELECT table_schema, table_name,
             COALESCE(table_rows,0) as row_count,
             ROUND((data_length+index_length)/1024,1) as size_kb
             FROM tables
             WHERE table_schema IN ('mcaster1_media','mcaster1_metrics','mcaster1_encoder')
             ORDER BY table_schema, table_name"
        )->fetchAll();
        echo json_encode(['ok'=>true, 'tables'=>$rows]);
    } catch (Exception $e) {
        http_response_code(500);
        echo json_encode(['ok'=>false,'error'=>$e->getMessage()]);
    }
    return;
}

http_response_code(400);
echo json_encode(['ok'=>false,'error'=>'Unknown action: '.h($action)]);
