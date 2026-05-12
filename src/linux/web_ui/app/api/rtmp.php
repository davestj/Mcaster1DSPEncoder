<?php
/**
 * rtmp.php — RTMP Multi-Platform Streaming Relay API
 *
 * File:    src/linux/web_ui/app/api/rtmp.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Purpose: We manage RTMP relay targets (Twitch, YouTube Live, Facebook Live,
 *          custom RTMP) in the rtmp_targets table. We start/stop ffmpeg relay
 *          processes that read from the encoder's Icecast/DNAS stream and
 *          re-mux to RTMP/FLV for each platform.
 *
 * Actions (all POST JSON, all require auth):
 *  list       — return all RTMP targets (optionally filtered by slot_id)
 *  create     — add a new RTMP target
 *  update     — update an existing target
 *  delete     — remove a target
 *  start      — start ffmpeg relay for a target
 *  stop       — stop ffmpeg relay (kill ffmpeg process)
 *  status     — check relay status for a target or all targets
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We use first-person plural throughout all comments
 *  - We use mc1_api_respond() for all JSON responses
 *  - We use escapeshellarg() on all user-provided values in shell commands
 *  - Stream keys are sensitive — we mask them in list responses
 */

define('MC1_BOOT', true);
$API_VERSION = '2.0.1';
require_once __DIR__ . '/../inc/mc1_config.php';
require_once __DIR__ . '/../inc/db.php';
require_once __DIR__ . '/../inc/traits.db.class.php';
require_once __DIR__ . '/../inc/logger.php';
require_once __DIR__ . '/../inc/auth.php';
require_once __DIR__ . '/../inc/user_auth.php';

header('Content-Type: application/json');

/* -- Auth gate ------------------------------------------------------------ */
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

$valid_platforms  = ['twitch', 'youtube', 'facebook', 'custom_rtmp'];
$valid_resolutions = ['640x360', '854x480', '1280x720', '1920x1080'];

/* We define the PID file path pattern for ffmpeg relay processes */
function mc1_rtmp_pid_file(int $target_id): string {
    return '/tmp/mc1_rtmp_' . $target_id . '.pid';
}

/* We define the log file path for ffmpeg relay output */
function mc1_rtmp_log_file(int $target_id): string {
    return '/tmp/mc1_rtmp_' . $target_id . '.log';
}

/* We check if an ffmpeg relay process is running for a given target */
function mc1_rtmp_is_running(int $target_id): array {
    $pid_file = mc1_rtmp_pid_file($target_id);
    if (!file_exists($pid_file)) {
        return ['running' => false, 'pid' => 0];
    }
    $pid = (int)trim((string)file_get_contents($pid_file));
    if ($pid <= 0) {
        return ['running' => false, 'pid' => 0];
    }
    /* We check if the process is actually alive */
    $alive = file_exists('/proc/' . $pid);
    if (!$alive) {
        /* We clean up the stale PID file */
        @unlink($pid_file);
        return ['running' => false, 'pid' => 0];
    }
    return ['running' => true, 'pid' => $pid];
}

/* We mask a stream key for display — show first 4 and last 4 chars */
function mc1_mask_key(string $key): string {
    $len = strlen($key);
    if ($len <= 8) return str_repeat('*', $len);
    return substr($key, 0, 4) . str_repeat('*', $len - 8) . substr($key, -4);
}

/* We build the source stream URL from the encoder_configs table for a slot */
function mc1_get_slot_stream_url(int $slot_id): ?string {
    try {
        $pdo  = mc1_db('mcaster1_encoder');
        $stmt = $pdo->prepare(
            'SELECT server_host, server_port, server_mount, server_protocol
             FROM encoder_configs
             WHERE slot_id = ? LIMIT 1'
        );
        $stmt->execute([$slot_id]);
        $cfg = $stmt->fetch(\PDO::FETCH_ASSOC);
        if (!$cfg || empty($cfg['server_host'])) return null;

        $host  = $cfg['server_host'];
        $port  = (int)$cfg['server_port'];
        $mount = $cfg['server_mount'] ?: '/live';

        /* We construct the stream URL — Icecast/DNAS streams are plain HTTP */
        $scheme = 'http';
        /* We use HTTPS for port 443 or typical SSL ports */
        if ($port === 443 || $port === 9443) {
            $scheme = 'https';
        }
        return $scheme . '://' . $host . ':' . $port . $mount;
    } catch (Exception $e) {
        mc1_log(2, 'rtmp get_slot_stream_url failed', json_encode([
            'slot_id' => $slot_id, 'err' => $e->getMessage()
        ]));
        return null;
    }
}

/* =========================================================================
 * action: list — return all RTMP targets with masked stream keys
 * ========================================================================= */
if ($action === 'list') {
    try {
        $pdo = mc1_db('mcaster1_encoder');
        $slot_filter = isset($body['slot_id']) ? (int)$body['slot_id'] : 0;
        if ($slot_filter > 0) {
            $stmt = $pdo->prepare(
                'SELECT * FROM rtmp_targets WHERE slot_id = ? ORDER BY id'
            );
            $stmt->execute([$slot_filter]);
        } else {
            $stmt = $pdo->query('SELECT * FROM rtmp_targets ORDER BY slot_id, id');
        }
        $rows = $stmt->fetchAll(\PDO::FETCH_ASSOC);
        foreach ($rows as &$r) {
            $r['id']             = (int)$r['id'];
            $r['slot_id']        = (int)$r['slot_id'];
            $r['is_active']      = (bool)$r['is_active'];
            $r['video_enabled']  = (bool)$r['video_enabled'];
            $r['stream_key_masked'] = mc1_mask_key($r['stream_key']);
            unset($r['stream_key']); /* We never send raw stream key in list */
            /* We check if the ffmpeg relay is currently running */
            $status = mc1_rtmp_is_running((int)$r['id']);
            $r['relay_running'] = $status['running'];
            $r['relay_pid']     = $status['pid'];
        }
        unset($r);
        mc1_api_respond(['ok' => true, 'targets' => $rows]);
    } catch (Exception $e) {
        mc1_log(2, 'rtmp list failed', json_encode(['err' => $e->getMessage()]));
        mc1_api_respond(['error' => 'Query failed: ' . $e->getMessage()], 500);
    }
    return;
}

/* =========================================================================
 * action: create — add a new RTMP target (admin only)
 * ========================================================================= */
if ($action === 'create') {
    if (!$is_admin) {
        mc1_api_respond(['error' => 'Admin permission required'], 403);
        return;
    }
    $slot_id    = (int)($body['slot_id']    ?? 0);
    $platform   = (string)($body['platform'] ?? '');
    $name       = trim($body['name']        ?? '');
    $rtmp_url   = trim($body['rtmp_url']    ?? '');
    $stream_key = trim($body['stream_key']  ?? '');

    if ($slot_id < 1 || !in_array($platform, $valid_platforms, true) || $name === '' || $stream_key === '') {
        mc1_api_respond(['error' => 'slot_id, platform, name, and stream_key are required'], 400);
        return;
    }
    /* We allow rtmp_url to be empty for known platforms — we will auto-fill */
    if ($rtmp_url === '') {
        $templates = [
            'twitch'   => 'rtmp://live.twitch.tv/app/',
            'youtube'  => 'rtmp://a.rtmp.youtube.com/live2/',
            'facebook' => 'rtmps://live-api-s.facebook.com:443/rtmp/',
        ];
        $rtmp_url = $templates[$platform] ?? '';
    }
    if ($rtmp_url === '') {
        mc1_api_respond(['error' => 'rtmp_url is required for custom RTMP targets'], 400);
        return;
    }

    $video_enabled  = (int)(bool)($body['video_enabled']  ?? false);
    $video_res      = in_array($body['video_resolution'] ?? '', $valid_resolutions, true)
                        ? $body['video_resolution'] : '1280x720';

    try {
        $pdo = mc1_db('mcaster1_encoder');
        $pdo->prepare(
            'INSERT INTO rtmp_targets
                (slot_id, platform, name, rtmp_url, stream_key, is_active,
                 video_enabled, video_resolution)
             VALUES (?, ?, ?, ?, ?, 0, ?, ?)'
        )->execute([
            $slot_id, $platform, $name, $rtmp_url, $stream_key,
            $video_enabled, $video_res,
        ]);
        $id = (int)$pdo->lastInsertId();
        mc1_api_respond(['ok' => true, 'id' => $id]);
    } catch (Exception $e) {
        mc1_log(2, 'rtmp create failed', json_encode(['err' => $e->getMessage()]));
        mc1_api_respond(['error' => 'Insert failed: ' . $e->getMessage()], 500);
    }
    return;
}

/* =========================================================================
 * action: update — update an existing RTMP target (admin only)
 * ========================================================================= */
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
    $fields = [];
    $values = [];

    if (isset($body['name'])) {
        $fields[] = 'name=?';
        $values[] = trim($body['name']);
    }
    if (isset($body['platform']) && in_array($body['platform'], $valid_platforms, true)) {
        $fields[] = 'platform=?';
        $values[] = $body['platform'];
    }
    if (isset($body['slot_id'])) {
        $fields[] = 'slot_id=?';
        $values[] = (int)$body['slot_id'];
    }
    if (isset($body['rtmp_url'])) {
        $fields[] = 'rtmp_url=?';
        $values[] = trim($body['rtmp_url']);
    }
    /* We only update stream_key when a non-empty value is provided */
    if (!empty($body['stream_key'])) {
        $fields[] = 'stream_key=?';
        $values[] = trim($body['stream_key']);
    }
    if (isset($body['video_enabled'])) {
        $fields[] = 'video_enabled=?';
        $values[] = (int)(bool)$body['video_enabled'];
    }
    if (isset($body['video_resolution']) && in_array($body['video_resolution'], $valid_resolutions, true)) {
        $fields[] = 'video_resolution=?';
        $values[] = $body['video_resolution'];
    }
    if (empty($fields)) {
        mc1_api_respond(['error' => 'No fields to update'], 400);
        return;
    }
    $values[] = $id;
    try {
        mc1_db('mcaster1_encoder')
            ->prepare('UPDATE rtmp_targets SET ' . implode(', ', $fields) . ' WHERE id=?')
            ->execute($values);
        mc1_api_respond(['ok' => true]);
    } catch (Exception $e) {
        mc1_log(2, 'rtmp update failed', json_encode(['id' => $id, 'err' => $e->getMessage()]));
        mc1_api_respond(['error' => 'Update failed: ' . $e->getMessage()], 500);
    }
    return;
}

/* =========================================================================
 * action: delete — remove an RTMP target (admin only)
 * We also stop any running relay for this target first.
 * ========================================================================= */
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
    /* We stop the relay if it is running before deleting the DB row */
    $status = mc1_rtmp_is_running($id);
    if ($status['running'] && $status['pid'] > 0) {
        @posix_kill($status['pid'], 15); /* SIGTERM */
        @unlink(mc1_rtmp_pid_file($id));
    }
    try {
        mc1_db('mcaster1_encoder')
            ->prepare('DELETE FROM rtmp_targets WHERE id=?')
            ->execute([$id]);
        mc1_api_respond(['ok' => true]);
    } catch (Exception $e) {
        mc1_log(2, 'rtmp delete failed', json_encode(['id' => $id, 'err' => $e->getMessage()]));
        mc1_api_respond(['error' => 'Delete failed: ' . $e->getMessage()], 500);
    }
    return;
}

/* =========================================================================
 * action: start — start ffmpeg relay for a target (admin only)
 * We launch ffmpeg in the background reading from the slot's Icecast/DNAS
 * stream and re-muxing to RTMP/FLV for the configured platform.
 * ========================================================================= */
if ($action === 'start') {
    if (!$is_admin) {
        mc1_api_respond(['error' => 'Admin permission required'], 403);
        return;
    }
    $id = (int)($body['id'] ?? 0);
    if ($id < 1) {
        mc1_api_respond(['error' => 'id required'], 400);
        return;
    }

    /* We check if already running */
    $status = mc1_rtmp_is_running($id);
    if ($status['running']) {
        mc1_api_respond(['error' => 'Relay is already running (PID ' . $status['pid'] . ')'], 409);
        return;
    }

    /* We load the target from DB */
    try {
        $pdo  = mc1_db('mcaster1_encoder');
        $stmt = $pdo->prepare('SELECT * FROM rtmp_targets WHERE id=?');
        $stmt->execute([$id]);
        $target = $stmt->fetch(\PDO::FETCH_ASSOC);
    } catch (Exception $e) {
        mc1_api_respond(['error' => 'DB error: ' . $e->getMessage()], 500);
        return;
    }
    if (!$target) {
        mc1_api_respond(['error' => 'Target not found'], 404);
        return;
    }

    /* We get the stream source URL from the encoder config */
    $source_url = mc1_get_slot_stream_url((int)$target['slot_id']);
    if (!$source_url) {
        mc1_api_respond(['error' => 'Could not determine stream URL for slot ' . $target['slot_id'] . '. Check encoder config.'], 400);
        return;
    }

    /* We build the full RTMP destination URL */
    $rtmp_dest = rtrim($target['rtmp_url'], '/') . '/' . $target['stream_key'];

    $pid_file = mc1_rtmp_pid_file($id);
    $log_file = mc1_rtmp_log_file($id);

    /* We build the ffmpeg command */
    $ffmpeg_bin = '/usr/bin/ffmpeg';

    if ((bool)$target['video_enabled']) {
        /* Audio + video (static image + waveform) — for platforms that require video */
        $res = in_array($target['video_resolution'], $valid_resolutions, true)
             ? $target['video_resolution'] : '1280x720';

        /* We generate a simple slate image via ffmpeg lavfi if no cover art exists */
        $cmd = sprintf(
            'nohup %s -re -f lavfi -i color=c=0x0a0f1e:s=%s:r=1 '
            . '-i %s '
            . '-c:v libx264 -preset ultrafast -tune stillimage -r 1 -pix_fmt yuv420p '
            . '-c:a aac -b:a 128k -ar 44100 -ac 2 '
            . '-shortest -f flv %s '
            . '> %s 2>&1 & echo $!',
            escapeshellarg($ffmpeg_bin),
            escapeshellarg($res),
            escapeshellarg($source_url),
            escapeshellarg($rtmp_dest),
            escapeshellarg($log_file)
        );
    } else {
        /* Audio-only RTMP relay — re-encode audio to AAC for RTMP compatibility */
        $cmd = sprintf(
            'nohup %s -re -i %s '
            . '-c:a aac -b:a 128k -ar 44100 -ac 2 '
            . '-f flv %s '
            . '> %s 2>&1 & echo $!',
            escapeshellarg($ffmpeg_bin),
            escapeshellarg($source_url),
            escapeshellarg($rtmp_dest),
            escapeshellarg($log_file)
        );
    }

    /* We launch the ffmpeg process and capture its PID */
    $output = [];
    $retval = 0;
    exec($cmd, $output, $retval);

    $pid = isset($output[0]) ? (int)trim($output[0]) : 0;
    if ($pid <= 0) {
        /* We try to read the log for error details */
        $err_detail = '';
        if (file_exists($log_file)) {
            $err_detail = trim((string)file_get_contents($log_file));
            if (strlen($err_detail) > 500) $err_detail = substr($err_detail, -500);
        }
        /* We update the DB with the error */
        try {
            $pdo->prepare('UPDATE rtmp_targets SET is_active=0, error_message=? WHERE id=?')
                ->execute(['Failed to start ffmpeg: ' . $err_detail, $id]);
        } catch (Exception $e) {}

        mc1_api_respond(['error' => 'Failed to start ffmpeg relay', 'detail' => $err_detail], 500);
        return;
    }

    /* We write the PID file */
    file_put_contents($pid_file, (string)$pid);

    /* We update the DB to mark this target as active */
    try {
        $pdo->prepare(
            'UPDATE rtmp_targets SET is_active=1, last_connected_at=NOW(), error_message=NULL WHERE id=?'
        )->execute([$id]);
    } catch (Exception $e) {
        mc1_log(2, 'rtmp start DB update failed', json_encode(['id' => $id, 'err' => $e->getMessage()]));
    }

    mc1_log(4, 'rtmp relay started', json_encode([
        'target_id' => $id, 'pid' => $pid, 'slot_id' => (int)$target['slot_id'],
        'platform' => $target['platform'], 'dest' => rtrim($target['rtmp_url'], '/') . '/****',
    ]));

    mc1_api_respond(['ok' => true, 'pid' => $pid, 'source_url' => $source_url]);
    return;
}

/* =========================================================================
 * action: stop — stop ffmpeg relay for a target (admin only)
 * ========================================================================= */
if ($action === 'stop') {
    if (!$is_admin) {
        mc1_api_respond(['error' => 'Admin permission required'], 403);
        return;
    }
    $id = (int)($body['id'] ?? 0);
    if ($id < 1) {
        mc1_api_respond(['error' => 'id required'], 400);
        return;
    }

    $status = mc1_rtmp_is_running($id);
    if (!$status['running']) {
        /* We still mark as inactive in DB in case state is stale */
        try {
            mc1_db('mcaster1_encoder')
                ->prepare('UPDATE rtmp_targets SET is_active=0 WHERE id=?')
                ->execute([$id]);
        } catch (Exception $e) {}
        mc1_api_respond(['ok' => true, 'message' => 'Relay was not running']);
        return;
    }

    /* We send SIGTERM first, then SIGKILL after a brief wait if needed */
    $pid = $status['pid'];
    @posix_kill($pid, 15); /* SIGTERM */

    /* We give ffmpeg up to 2 seconds to exit cleanly */
    $waited = 0;
    while ($waited < 2000 && file_exists('/proc/' . $pid)) {
        usleep(100000); /* 100ms */
        $waited += 100;
    }
    /* If still alive, we force kill */
    if (file_exists('/proc/' . $pid)) {
        @posix_kill($pid, 9); /* SIGKILL */
    }

    @unlink(mc1_rtmp_pid_file($id));

    /* We update DB */
    try {
        mc1_db('mcaster1_encoder')
            ->prepare('UPDATE rtmp_targets SET is_active=0 WHERE id=?')
            ->execute([$id]);
    } catch (Exception $e) {}

    mc1_log(4, 'rtmp relay stopped', json_encode(['target_id' => $id, 'pid' => $pid]));
    mc1_api_respond(['ok' => true, 'killed_pid' => $pid]);
    return;
}

/* =========================================================================
 * action: status — check relay status for one or all targets
 * ========================================================================= */
if ($action === 'status') {
    $id = isset($body['id']) ? (int)$body['id'] : 0;
    try {
        $pdo = mc1_db('mcaster1_encoder');
        if ($id > 0) {
            $stmt = $pdo->prepare('SELECT id, slot_id, platform, name, is_active, last_connected_at, error_message FROM rtmp_targets WHERE id=?');
            $stmt->execute([$id]);
        } else {
            $stmt = $pdo->query('SELECT id, slot_id, platform, name, is_active, last_connected_at, error_message FROM rtmp_targets ORDER BY slot_id, id');
        }
        $rows = $stmt->fetchAll(\PDO::FETCH_ASSOC);
    } catch (Exception $e) {
        mc1_api_respond(['error' => 'Query failed: ' . $e->getMessage()], 500);
        return;
    }

    $results = [];
    foreach ($rows as $r) {
        $tid    = (int)$r['id'];
        $status = mc1_rtmp_is_running($tid);

        /* We sync DB is_active with actual process state */
        if ((bool)$r['is_active'] && !$status['running']) {
            /* We read the last few lines of the log for error context */
            $log_file = mc1_rtmp_log_file($tid);
            $err_msg  = '';
            if (file_exists($log_file)) {
                $lines = file($log_file, FILE_IGNORE_NEW_LINES | FILE_SKIP_EMPTY_LINES);
                if ($lines) {
                    $err_msg = implode(' | ', array_slice($lines, -3));
                    if (strlen($err_msg) > 500) $err_msg = substr($err_msg, -500);
                }
            }
            try {
                $pdo->prepare('UPDATE rtmp_targets SET is_active=0, error_message=? WHERE id=?')
                    ->execute([$err_msg ?: 'Process exited unexpectedly', $tid]);
            } catch (Exception $e) {}
            $r['is_active']     = false;
            $r['error_message'] = $err_msg ?: 'Process exited unexpectedly';
        }

        $results[] = [
            'id'                => $tid,
            'slot_id'           => (int)$r['slot_id'],
            'platform'          => $r['platform'],
            'name'              => $r['name'],
            'relay_running'     => $status['running'],
            'relay_pid'         => $status['pid'],
            'is_active'         => (bool)$r['is_active'],
            'last_connected_at' => $r['last_connected_at'],
            'error_message'     => $r['error_message'],
        ];
    }
    mc1_api_respond(['ok' => true, 'targets' => $results]);
    return;
}

/* We return 400 for any unknown action */
mc1_api_respond(['error' => 'Unknown action: ' . $action], 400);
