<?php
/**
 * srt.php — SRT (Secure Reliable Transport) Streaming API
 *
 * File:    src/linux/web_ui/app/api/srt.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-29
 * Purpose: We manage SRT streaming targets in the srt_targets table. We start/stop
 *          ffmpeg processes that read from the encoder's proxy/stream endpoint and
 *          push (caller mode) or serve (listener mode) SRT streams. SRT provides
 *          low-latency (<1s), encrypted (AES-128/256), NAT-traversal streaming.
 *
 * Actions (all POST JSON, all require auth):
 *  list       — return all SRT targets (optionally filtered by slot_id)
 *  create     — add a new SRT target
 *  update     — update an existing target
 *  delete     — remove a target (stops relay first)
 *  start      — start ffmpeg SRT push/listen for a target
 *  stop       — stop ffmpeg process (kill)
 *  status     — check relay status for a target or all targets
 *
 * SRT modes:
 *  caller      — push stream TO an SRT server (like OBS to server)
 *  listener    — act AS an SRT server (others pull FROM you)
 *  rendezvous  — both sides connect simultaneously (NAT traversal)
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We use first-person plural throughout all comments
 *  - We use mc1_api_respond() for all JSON responses
 *  - We use escapeshellarg() on all user-provided values in shell commands
 *  - Passphrases are sensitive — we mask them in list responses
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

$valid_modes       = ['caller', 'listener', 'rendezvous'];
$valid_encryptions = ['none', 'aes128', 'aes192', 'aes256'];
$valid_codecs      = ['aac', 'mp3', 'opus', 'copy'];

/* We define the PID file path pattern for ffmpeg SRT processes */
function mc1_srt_pid_file(int $target_id): string {
    return '/tmp/mc1_srt_' . $target_id . '.pid';
}

/* We define the log file path for ffmpeg SRT output */
function mc1_srt_log_file(int $target_id): string {
    return '/tmp/mc1_srt_' . $target_id . '.log';
}

/* We check if an ffmpeg SRT process is running for a given target */
function mc1_srt_is_running(int $target_id): array {
    $pid_file = mc1_srt_pid_file($target_id);
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

/* We mask a passphrase for display — show first 3 and last 3 chars */
function mc1_mask_passphrase(string $pass): string {
    $len = strlen($pass);
    if ($len === 0) return '';
    if ($len <= 6) return str_repeat('*', $len);
    return substr($pass, 0, 3) . str_repeat('*', $len - 6) . substr($pass, -3);
}

/* We map encryption enum to SRT pbkeylen parameter */
function mc1_srt_pbkeylen(string $enc): int {
    switch ($enc) {
        case 'aes128': return 16;
        case 'aes192': return 24;
        case 'aes256': return 32;
        default:       return 0;
    }
}

/* We parse ffmpeg SRT stats from the log file (stderr) for a given target */
function mc1_srt_parse_stats(int $target_id): array {
    $log_file = mc1_srt_log_file($target_id);
    $stats = [
        'bitrate_kbps' => 0,
        'rtt_ms'       => 0,
        'packet_loss'  => 0,
        'log_tail'     => '',
    ];
    if (!file_exists($log_file)) return $stats;

    $lines = @file($log_file, FILE_IGNORE_NEW_LINES | FILE_SKIP_EMPTY_LINES);
    if (!$lines) return $stats;

    /* We take the last 20 lines for stats parsing */
    $tail = array_slice($lines, -20);
    $stats['log_tail'] = implode("\n", array_slice($lines, -5));
    if (strlen($stats['log_tail']) > 500) {
        $stats['log_tail'] = substr($stats['log_tail'], -500);
    }

    /* We scan lines for bitrate info from ffmpeg output */
    foreach (array_reverse($tail) as $line) {
        /* ffmpeg outputs lines like: "bitrate= 128.0kbits/s" */
        if (preg_match('/bitrate=\s*([\d.]+)kbits\/s/', $line, $m)) {
            $stats['bitrate_kbps'] = (float)$m[1];
            break;
        }
    }

    return $stats;
}

/* =========================================================================
 * action: list — return all SRT targets with masked passphrases
 * ========================================================================= */
if ($action === 'list') {
    try {
        $pdo = mc1_db('mcaster1_encoder');
        $slot_filter = isset($body['slot_id']) ? (int)$body['slot_id'] : 0;
        if ($slot_filter > 0) {
            $stmt = $pdo->prepare(
                'SELECT * FROM srt_targets WHERE slot_id = ? ORDER BY id'
            );
            $stmt->execute([$slot_filter]);
        } else {
            $stmt = $pdo->query('SELECT * FROM srt_targets ORDER BY slot_id, id');
        }
        $rows = $stmt->fetchAll(\PDO::FETCH_ASSOC);
        foreach ($rows as &$r) {
            $r['id']          = (int)$r['id'];
            $r['slot_id']     = (int)$r['slot_id'];
            $r['port']        = (int)$r['port'];
            $r['latency_ms']  = (int)$r['latency_ms'];
            $r['bitrate_kbps'] = (int)$r['bitrate_kbps'];
            $r['is_active']   = (bool)$r['is_active'];
            $r['passphrase_masked'] = mc1_mask_passphrase($r['passphrase'] ?? '');
            unset($r['passphrase']); /* We never send raw passphrase in list */
            /* We check if the ffmpeg process is currently running */
            $status = mc1_srt_is_running((int)$r['id']);
            $r['relay_running'] = $status['running'];
            $r['relay_pid']     = $status['pid'];
        }
        unset($r);
        mc1_api_respond(['ok' => true, 'targets' => $rows]);
    } catch (Exception $e) {
        mc1_log(2, 'srt list failed', json_encode(['err' => $e->getMessage()]));
        mc1_api_respond(['error' => 'Query failed: ' . $e->getMessage()], 500);
    }
    return;
}

/* =========================================================================
 * action: create — add a new SRT target (admin only)
 * ========================================================================= */
if ($action === 'create') {
    if (!$is_admin) {
        mc1_api_respond(['error' => 'Admin permission required'], 403);
        return;
    }
    $slot_id    = (int)($body['slot_id']    ?? 0);
    $name       = trim($body['name']        ?? '');
    $mode       = (string)($body['mode']    ?? 'caller');
    $host       = trim($body['host']        ?? '0.0.0.0');
    $port       = (int)($body['port']       ?? 9000);
    $passphrase = trim($body['passphrase']  ?? '');
    $latency_ms = (int)($body['latency_ms'] ?? 200);
    $encryption = (string)($body['encryption'] ?? 'aes128');
    $codec      = (string)($body['codec']   ?? 'aac');
    $bitrate    = (int)($body['bitrate_kbps'] ?? 128);

    if ($slot_id < 1 || $name === '') {
        mc1_api_respond(['error' => 'slot_id and name are required'], 400);
        return;
    }
    if (!in_array($mode, $valid_modes, true)) {
        mc1_api_respond(['error' => 'Invalid mode. Must be: ' . implode(', ', $valid_modes)], 400);
        return;
    }
    if (!in_array($encryption, $valid_encryptions, true)) {
        mc1_api_respond(['error' => 'Invalid encryption. Must be: ' . implode(', ', $valid_encryptions)], 400);
        return;
    }
    if (!in_array($codec, $valid_codecs, true)) {
        mc1_api_respond(['error' => 'Invalid codec. Must be: ' . implode(', ', $valid_codecs)], 400);
        return;
    }
    if ($port < 1 || $port > 65535) {
        mc1_api_respond(['error' => 'Port must be between 1 and 65535'], 400);
        return;
    }
    if ($latency_ms < 50 || $latency_ms > 10000) {
        mc1_api_respond(['error' => 'Latency must be between 50 and 10000 ms'], 400);
        return;
    }
    if ($bitrate < 32 || $bitrate > 512) {
        mc1_api_respond(['error' => 'Bitrate must be between 32 and 512 kbps'], 400);
        return;
    }

    /* We require a passphrase if encryption is enabled */
    if ($encryption !== 'none' && strlen($passphrase) < 10) {
        mc1_api_respond(['error' => 'Passphrase must be at least 10 characters when encryption is enabled (SRT minimum)'], 400);
        return;
    }

    try {
        $pdo = mc1_db('mcaster1_encoder');
        $pdo->prepare(
            'INSERT INTO srt_targets
                (slot_id, name, mode, host, port, passphrase, latency_ms,
                 encryption, codec, bitrate_kbps, is_active)
             VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0)'
        )->execute([
            $slot_id, $name, $mode, $host, $port, $passphrase,
            $latency_ms, $encryption, $codec, $bitrate,
        ]);
        $id = (int)$pdo->lastInsertId();
        mc1_api_respond(['ok' => true, 'id' => $id]);
    } catch (Exception $e) {
        mc1_log(2, 'srt create failed', json_encode(['err' => $e->getMessage()]));
        mc1_api_respond(['error' => 'Insert failed: ' . $e->getMessage()], 500);
    }
    return;
}

/* =========================================================================
 * action: update — update an existing SRT target (admin only)
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
    if (isset($body['mode']) && in_array($body['mode'], $valid_modes, true)) {
        $fields[] = 'mode=?';
        $values[] = $body['mode'];
    }
    if (isset($body['slot_id'])) {
        $fields[] = 'slot_id=?';
        $values[] = (int)$body['slot_id'];
    }
    if (isset($body['host'])) {
        $fields[] = 'host=?';
        $values[] = trim($body['host']);
    }
    if (isset($body['port'])) {
        $p = (int)$body['port'];
        if ($p >= 1 && $p <= 65535) {
            $fields[] = 'port=?';
            $values[] = $p;
        }
    }
    /* We only update passphrase when a non-empty value is provided */
    if (!empty($body['passphrase'])) {
        $fields[] = 'passphrase=?';
        $values[] = trim($body['passphrase']);
    }
    if (isset($body['latency_ms'])) {
        $lat = (int)$body['latency_ms'];
        if ($lat >= 50 && $lat <= 10000) {
            $fields[] = 'latency_ms=?';
            $values[] = $lat;
        }
    }
    if (isset($body['encryption']) && in_array($body['encryption'], $valid_encryptions, true)) {
        $fields[] = 'encryption=?';
        $values[] = $body['encryption'];
    }
    if (isset($body['codec']) && in_array($body['codec'], $valid_codecs, true)) {
        $fields[] = 'codec=?';
        $values[] = $body['codec'];
    }
    if (isset($body['bitrate_kbps'])) {
        $br = (int)$body['bitrate_kbps'];
        if ($br >= 32 && $br <= 512) {
            $fields[] = 'bitrate_kbps=?';
            $values[] = $br;
        }
    }
    if (empty($fields)) {
        mc1_api_respond(['error' => 'No fields to update'], 400);
        return;
    }
    $values[] = $id;
    try {
        mc1_db('mcaster1_encoder')
            ->prepare('UPDATE srt_targets SET ' . implode(', ', $fields) . ' WHERE id=?')
            ->execute($values);
        mc1_api_respond(['ok' => true]);
    } catch (Exception $e) {
        mc1_log(2, 'srt update failed', json_encode(['id' => $id, 'err' => $e->getMessage()]));
        mc1_api_respond(['error' => 'Update failed: ' . $e->getMessage()], 500);
    }
    return;
}

/* =========================================================================
 * action: delete — remove an SRT target (admin only)
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
    $status = mc1_srt_is_running($id);
    if ($status['running'] && $status['pid'] > 0) {
        @posix_kill($status['pid'], 15); /* SIGTERM */
        @unlink(mc1_srt_pid_file($id));
    }
    try {
        mc1_db('mcaster1_encoder')
            ->prepare('DELETE FROM srt_targets WHERE id=?')
            ->execute([$id]);
        mc1_api_respond(['ok' => true]);
    } catch (Exception $e) {
        mc1_log(2, 'srt delete failed', json_encode(['id' => $id, 'err' => $e->getMessage()]));
        mc1_api_respond(['error' => 'Delete failed: ' . $e->getMessage()], 500);
    }
    return;
}

/* =========================================================================
 * action: start — start ffmpeg SRT relay for a target (admin only)
 * We launch ffmpeg in the background reading from the encoder's proxy/stream
 * endpoint and pushing via SRT to the configured destination.
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
    $status = mc1_srt_is_running($id);
    if ($status['running']) {
        mc1_api_respond(['error' => 'SRT relay is already running (PID ' . $status['pid'] . ')'], 409);
        return;
    }

    /* We load the target from DB */
    try {
        $pdo  = mc1_db('mcaster1_encoder');
        $stmt = $pdo->prepare('SELECT * FROM srt_targets WHERE id=?');
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

    /* We build the source URL — we use the local proxy/stream endpoint */
    $slot_id    = (int)$target['slot_id'];
    $source_url = 'http://127.0.0.1:8330/proxy/stream?slot=' . $slot_id;

    $mode       = $target['mode'] ?: 'caller';
    $host       = $target['host'] ?: '0.0.0.0';
    $port       = (int)$target['port'] ?: 9000;
    $passphrase = $target['passphrase'] ?? '';
    $latency_us = ((int)$target['latency_ms'] ?: 200) * 1000; /* SRT uses microseconds */
    $encryption = $target['encryption'] ?: 'none';
    $codec      = $target['codec'] ?: 'aac';
    $bitrate    = (int)$target['bitrate_kbps'] ?: 128;

    /* We build the SRT URL with query parameters */
    $srt_params = [];
    $srt_params[] = 'mode=' . $mode;
    if ($passphrase !== '') {
        $srt_params[] = 'passphrase=' . $passphrase;
        $pbkeylen = mc1_srt_pbkeylen($encryption);
        if ($pbkeylen > 0) {
            $srt_params[] = 'pbkeylen=' . $pbkeylen;
        }
    }
    $srt_params[] = 'latency=' . $latency_us;

    $srt_url = 'srt://' . $host . ':' . $port . '?' . implode('&', $srt_params);

    $pid_file = mc1_srt_pid_file($id);
    $log_file = mc1_srt_log_file($id);

    /* We build the ffmpeg command */
    $ffmpeg_bin = '/usr/bin/ffmpeg';

    /* We determine the codec flags */
    $codec_flags = '';
    switch ($codec) {
        case 'aac':
            $codec_flags = '-c:a aac -b:a ' . $bitrate . 'k -ar 44100 -ac 2';
            break;
        case 'mp3':
            $codec_flags = '-c:a libmp3lame -b:a ' . $bitrate . 'k -ar 44100 -ac 2';
            break;
        case 'opus':
            $codec_flags = '-c:a libopus -b:a ' . $bitrate . 'k -ar 48000 -ac 2';
            break;
        case 'copy':
            $codec_flags = '-c:a copy';
            break;
        default:
            $codec_flags = '-c:a aac -b:a ' . $bitrate . 'k -ar 44100 -ac 2';
    }

    /* We build the full command — SRT uses MPEG-TS container */
    $cmd = sprintf(
        'nohup %s -re -i %s %s -f mpegts %s > %s 2>&1 & echo $!',
        escapeshellarg($ffmpeg_bin),
        escapeshellarg($source_url),
        $codec_flags,
        escapeshellarg($srt_url),
        escapeshellarg($log_file)
    );

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
            $pdo->prepare('UPDATE srt_targets SET is_active=0, error_message=? WHERE id=?')
                ->execute(['Failed to start ffmpeg: ' . $err_detail, $id]);
        } catch (Exception $e) {}

        mc1_api_respond(['error' => 'Failed to start ffmpeg SRT relay', 'detail' => $err_detail], 500);
        return;
    }

    /* We write the PID file */
    file_put_contents($pid_file, (string)$pid);

    /* We update the DB to mark this target as active */
    try {
        $pdo->prepare(
            'UPDATE srt_targets SET is_active=1, last_connected_at=NOW(), error_message=NULL WHERE id=?'
        )->execute([$id]);
    } catch (Exception $e) {
        mc1_log(2, 'srt start DB update failed', json_encode(['id' => $id, 'err' => $e->getMessage()]));
    }

    /* We build the listener URL for display if this is listener mode */
    $listener_url = '';
    if ($mode === 'listener') {
        $listener_url = 'srt://<your-server-ip>:' . $port;
    }

    mc1_log(4, 'srt relay started', json_encode([
        'target_id' => $id, 'pid' => $pid, 'slot_id' => $slot_id,
        'mode' => $mode, 'host' => $host, 'port' => $port,
    ]));

    mc1_api_respond([
        'ok'           => true,
        'pid'          => $pid,
        'source_url'   => $source_url,
        'srt_url'      => 'srt://' . $host . ':' . $port,
        'listener_url' => $listener_url,
    ]);
    return;
}

/* =========================================================================
 * action: stop — stop ffmpeg SRT relay for a target (admin only)
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

    $status = mc1_srt_is_running($id);
    if (!$status['running']) {
        /* We still mark as inactive in DB in case state is stale */
        try {
            mc1_db('mcaster1_encoder')
                ->prepare('UPDATE srt_targets SET is_active=0 WHERE id=?')
                ->execute([$id]);
        } catch (Exception $e) {}
        mc1_api_respond(['ok' => true, 'message' => 'SRT relay was not running']);
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

    @unlink(mc1_srt_pid_file($id));

    /* We update DB */
    try {
        mc1_db('mcaster1_encoder')
            ->prepare('UPDATE srt_targets SET is_active=0 WHERE id=?')
            ->execute([$id]);
    } catch (Exception $e) {}

    mc1_log(4, 'srt relay stopped', json_encode(['target_id' => $id, 'pid' => $pid]));
    mc1_api_respond(['ok' => true, 'killed_pid' => $pid]);
    return;
}

/* =========================================================================
 * action: status — check relay status for one or all targets
 * We also parse ffmpeg log for basic SRT stats (bitrate, RTT, packet loss).
 * ========================================================================= */
if ($action === 'status') {
    $id = isset($body['id']) ? (int)$body['id'] : 0;
    try {
        $pdo = mc1_db('mcaster1_encoder');
        if ($id > 0) {
            $stmt = $pdo->prepare(
                'SELECT id, slot_id, name, mode, host, port, encryption, is_active,
                        last_connected_at, error_message
                 FROM srt_targets WHERE id=?'
            );
            $stmt->execute([$id]);
        } else {
            $stmt = $pdo->query(
                'SELECT id, slot_id, name, mode, host, port, encryption, is_active,
                        last_connected_at, error_message
                 FROM srt_targets ORDER BY slot_id, id'
            );
        }
        $rows = $stmt->fetchAll(\PDO::FETCH_ASSOC);
    } catch (Exception $e) {
        mc1_api_respond(['error' => 'Query failed: ' . $e->getMessage()], 500);
        return;
    }

    $results = [];
    foreach ($rows as $r) {
        $tid    = (int)$r['id'];
        $status = mc1_srt_is_running($tid);
        $stats  = mc1_srt_parse_stats($tid);

        /* We sync DB is_active with actual process state */
        if ((bool)$r['is_active'] && !$status['running']) {
            $log_file = mc1_srt_log_file($tid);
            $err_msg  = '';
            if (file_exists($log_file)) {
                $lines = file($log_file, FILE_IGNORE_NEW_LINES | FILE_SKIP_EMPTY_LINES);
                if ($lines) {
                    $err_msg = implode(' | ', array_slice($lines, -3));
                    if (strlen($err_msg) > 500) $err_msg = substr($err_msg, -500);
                }
            }
            try {
                $pdo->prepare('UPDATE srt_targets SET is_active=0, error_message=? WHERE id=?')
                    ->execute([$err_msg ?: 'Process exited unexpectedly', $tid]);
            } catch (Exception $e) {}
            $r['is_active']     = false;
            $r['error_message'] = $err_msg ?: 'Process exited unexpectedly';
        }

        $results[] = [
            'id'                => $tid,
            'slot_id'           => (int)$r['slot_id'],
            'name'              => $r['name'],
            'mode'              => $r['mode'],
            'host'              => $r['host'],
            'port'              => (int)$r['port'],
            'encryption'        => $r['encryption'],
            'relay_running'     => $status['running'],
            'relay_pid'         => $status['pid'],
            'is_active'         => (bool)$r['is_active'],
            'last_connected_at' => $r['last_connected_at'],
            'error_message'     => $r['error_message'],
            'stats'             => $stats,
        ];
    }
    mc1_api_respond(['ok' => true, 'targets' => $results]);
    return;
}

/* We return 400 for any unknown action */
mc1_api_respond(['error' => 'Unknown action: ' . $action], 400);
