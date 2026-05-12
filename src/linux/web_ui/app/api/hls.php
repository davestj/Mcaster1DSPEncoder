<?php
/**
 * hls.php — HLS/DASH Adaptive Bitrate Streaming API
 *
 * File:    src/linux/web_ui/app/api/hls.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-29
 * Purpose: We manage HLS and DASH adaptive bitrate streaming outputs from encoder
 *          slots. We launch ffmpeg processes that read from the encoder's proxy
 *          stream and generate segmented output (HLS .m3u8/.ts or DASH .mpd/.m4s)
 *          for browser-native playback with automatic quality switching.
 *
 * Actions (all POST JSON, all require auth):
 *  start         — start HLS output for a slot at one or more bitrates
 *  start_dash    — start DASH output for a slot
 *  stop          — stop HLS/DASH ffmpeg process(es) for a slot
 *  status        — check HLS/DASH status for a slot or all slots
 *  list_streams  — list all active HLS/DASH streams with URLs
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We use first-person plural throughout all comments
 *  - We use mc1_api_respond() for all JSON responses
 *  - We use escapeshellarg() on all user-provided values in shell commands
 *  - PID files stored in /tmp for ffmpeg process management
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

/* We define the base output directories for HLS and DASH segments */
define('MC1_HLS_DIR',  '/var/www/mcaster1.com/Mcaster1DSPEncoder/hls');
define('MC1_DASH_DIR', '/var/www/mcaster1.com/Mcaster1DSPEncoder/dash');

/* We define the valid bitrate tiers for adaptive bitrate */
$valid_bitrates = [64, 96, 128, 192, 256, 320];

/* We define the PID file path pattern for HLS/DASH ffmpeg processes */
function mc1_hls_pid_file(int $slot_id, string $format = 'hls', int $bitrate = 0): string {
    if ($bitrate > 0) {
        return '/tmp/mc1_' . $format . '_slot' . $slot_id . '_' . $bitrate . 'k.pid';
    }
    return '/tmp/mc1_' . $format . '_slot' . $slot_id . '.pid';
}

/* We define the log file path for ffmpeg output */
function mc1_hls_log_file(int $slot_id, string $format = 'hls', int $bitrate = 0): string {
    if ($bitrate > 0) {
        return '/tmp/mc1_' . $format . '_slot' . $slot_id . '_' . $bitrate . 'k.log';
    }
    return '/tmp/mc1_' . $format . '_slot' . $slot_id . '.log';
}

/* We check if an ffmpeg process is running for a given PID file */
function mc1_hls_is_running(string $pid_file): array {
    if (!file_exists($pid_file)) {
        return ['running' => false, 'pid' => 0];
    }
    $pid = (int)trim((string)file_get_contents($pid_file));
    if ($pid <= 0) {
        return ['running' => false, 'pid' => 0];
    }
    $alive = file_exists('/proc/' . $pid);
    if (!$alive) {
        @unlink($pid_file);
        return ['running' => false, 'pid' => 0];
    }
    return ['running' => true, 'pid' => $pid];
}

/* We build the local proxy stream URL for a given slot */
function mc1_hls_source_url(int $slot_id): string {
    return 'http://127.0.0.1:' . MC1_API_PORT . '/proxy/stream?slot=' . $slot_id;
}

/* We count segments in a directory matching a pattern */
function mc1_count_segments(string $dir, string $pattern): int {
    $files = glob($dir . '/' . $pattern);
    return $files ? count($files) : 0;
}

/* We get total size of segments for a slot */
function mc1_segments_size(string $dir, string $pattern): int {
    $files = glob($dir . '/' . $pattern);
    $total = 0;
    if ($files) {
        foreach ($files as $f) {
            $total += (int)filesize($f);
        }
    }
    return $total;
}

/* We generate a master HLS playlist for multi-bitrate ABR */
function mc1_write_master_playlist(int $slot_id, array $bitrates): void {
    $master = "#EXTM3U\n";
    foreach ($bitrates as $br) {
        $bw = $br * 1000;
        $master .= "#EXT-X-STREAM-INF:BANDWIDTH={$bw},CODECS=\"mp4a.40.2\"\n";
        $master .= "slot{$slot_id}_{$br}k.m3u8\n";
    }
    file_put_contents(MC1_HLS_DIR . '/slot' . $slot_id . '.m3u8', $master);
}

/* We clean up segment files for a slot */
function mc1_cleanup_segments(int $slot_id, string $format): void {
    if ($format === 'hls' || $format === 'both') {
        $files = glob(MC1_HLS_DIR . '/slot' . $slot_id . '_*');
        if ($files) {
            foreach ($files as $f) { @unlink($f); }
        }
        /* We also remove the master playlist */
        @unlink(MC1_HLS_DIR . '/slot' . $slot_id . '.m3u8');
    }
    if ($format === 'dash' || $format === 'both') {
        $files = glob(MC1_DASH_DIR . '/slot' . $slot_id . '*');
        if ($files) {
            foreach ($files as $f) { @unlink($f); }
        }
    }
}


/* =========================================================================
 * action: start — start HLS output for a slot with optional multi-bitrate ABR
 * ========================================================================= */
if ($action === 'start') {
    if (!$is_admin) {
        mc1_api_respond(['error' => 'Admin permission required'], 403);
        return;
    }

    $slot_id      = (int)($body['slot_id'] ?? 0);
    $bitrates     = $body['bitrates'] ?? [128];
    $seg_duration = (int)($body['segment_duration'] ?? 6);

    if ($slot_id < 1) {
        mc1_api_respond(['error' => 'slot_id is required'], 400);
        return;
    }

    /* We validate and sanitize bitrates */
    if (!is_array($bitrates) || empty($bitrates)) {
        $bitrates = [128];
    }
    $bitrates = array_values(array_unique(array_filter(array_map('intval', $bitrates), function($b) use ($valid_bitrates) {
        return in_array($b, $valid_bitrates, true);
    })));
    if (empty($bitrates)) {
        $bitrates = [128];
    }

    /* We clamp segment duration between 2 and 30 seconds */
    $seg_duration = max(2, min(30, $seg_duration));

    $source_url = mc1_hls_source_url($slot_id);
    $ffmpeg_bin = '/usr/bin/ffmpeg';
    $started    = [];
    $errors     = [];

    foreach ($bitrates as $br) {
        $pid_file = mc1_hls_pid_file($slot_id, 'hls', $br);
        $log_file = mc1_hls_log_file($slot_id, 'hls', $br);

        /* We check if already running at this bitrate */
        $status = mc1_hls_is_running($pid_file);
        if ($status['running']) {
            $started[] = ['bitrate' => $br, 'pid' => $status['pid'], 'already_running' => true];
            continue;
        }

        /* We build the ffmpeg HLS command */
        $seg_pattern = MC1_HLS_DIR . '/slot' . $slot_id . '_' . $br . 'k_%03d.ts';
        $playlist    = MC1_HLS_DIR . '/slot' . $slot_id . '_' . $br . 'k.m3u8';

        $cmd = sprintf(
            'nohup %s -re -i %s '
            . '-c:a aac -b:a %s -ar 44100 -ac 2 '
            . '-f hls '
            . '-hls_time %s -hls_list_size 10 -hls_flags delete_segments '
            . '-hls_segment_filename %s '
            . '%s '
            . '> %s 2>&1 & echo $!',
            escapeshellarg($ffmpeg_bin),
            escapeshellarg($source_url),
            escapeshellarg($br . 'k'),
            escapeshellarg((string)$seg_duration),
            escapeshellarg($seg_pattern),
            escapeshellarg($playlist),
            escapeshellarg($log_file)
        );

        $output = [];
        $retval = 0;
        exec($cmd, $output, $retval);

        $pid = isset($output[0]) ? (int)trim($output[0]) : 0;
        if ($pid > 0) {
            file_put_contents($pid_file, (string)$pid);
            $started[] = ['bitrate' => $br, 'pid' => $pid, 'already_running' => false];
        } else {
            $err_detail = '';
            if (file_exists($log_file)) {
                $err_detail = trim((string)file_get_contents($log_file));
                if (strlen($err_detail) > 500) $err_detail = substr($err_detail, -500);
            }
            $errors[] = ['bitrate' => $br, 'error' => $err_detail ?: 'Failed to start ffmpeg'];
        }
    }

    /* We generate the master playlist if we have multiple bitrates */
    if (count($bitrates) > 1) {
        mc1_write_master_playlist($slot_id, $bitrates);
    } else if (count($bitrates) === 1) {
        /* For single bitrate we still write a master playlist pointing to the single variant */
        mc1_write_master_playlist($slot_id, $bitrates);
    }

    mc1_log(4, 'hls start', json_encode([
        'slot_id' => $slot_id, 'bitrates' => $bitrates,
        'started' => count($started), 'errors' => count($errors),
    ]));

    mc1_api_respond([
        'ok'      => empty($errors),
        'started' => $started,
        'errors'  => $errors,
        'master_playlist' => '/hls/slot' . $slot_id . '.m3u8',
    ]);
    return;
}


/* =========================================================================
 * action: start_dash — start DASH output for a slot
 * ========================================================================= */
if ($action === 'start_dash') {
    if (!$is_admin) {
        mc1_api_respond(['error' => 'Admin permission required'], 403);
        return;
    }

    $slot_id      = (int)($body['slot_id'] ?? 0);
    $bitrate      = (int)($body['bitrate'] ?? 128);
    $seg_duration = (int)($body['segment_duration'] ?? 6);

    if ($slot_id < 1) {
        mc1_api_respond(['error' => 'slot_id is required'], 400);
        return;
    }

    /* We validate bitrate */
    if (!in_array($bitrate, $valid_bitrates, true)) {
        $bitrate = 128;
    }

    $seg_duration = max(2, min(30, $seg_duration));

    $pid_file = mc1_hls_pid_file($slot_id, 'dash');
    $log_file = mc1_hls_log_file($slot_id, 'dash');

    /* We check if already running */
    $status = mc1_hls_is_running($pid_file);
    if ($status['running']) {
        mc1_api_respond(['error' => 'DASH is already running for slot ' . $slot_id . ' (PID ' . $status['pid'] . ')'], 409);
        return;
    }

    $source_url = mc1_hls_source_url($slot_id);
    $ffmpeg_bin = '/usr/bin/ffmpeg';
    $mpd_file   = MC1_DASH_DIR . '/slot' . $slot_id . '.mpd';

    $cmd = sprintf(
        'nohup %s -re -i %s '
        . '-c:a aac -b:a %s -ar 44100 -ac 2 '
        . '-f dash '
        . '-seg_duration %s -window_size 10 '
        . '-init_seg_name %s -media_seg_name %s '
        . '%s '
        . '> %s 2>&1 & echo $!',
        escapeshellarg($ffmpeg_bin),
        escapeshellarg($source_url),
        escapeshellarg($bitrate . 'k'),
        escapeshellarg((string)$seg_duration),
        escapeshellarg('slot' . $slot_id . '_init.m4s'),
        escapeshellarg('slot' . $slot_id . '_$Number$.m4s'),
        escapeshellarg($mpd_file),
        escapeshellarg($log_file)
    );

    $output = [];
    $retval = 0;
    exec($cmd, $output, $retval);

    $pid = isset($output[0]) ? (int)trim($output[0]) : 0;
    if ($pid <= 0) {
        $err_detail = '';
        if (file_exists($log_file)) {
            $err_detail = trim((string)file_get_contents($log_file));
            if (strlen($err_detail) > 500) $err_detail = substr($err_detail, -500);
        }
        mc1_api_respond(['error' => 'Failed to start DASH ffmpeg', 'detail' => $err_detail], 500);
        return;
    }

    file_put_contents($pid_file, (string)$pid);

    mc1_log(4, 'dash start', json_encode([
        'slot_id' => $slot_id, 'pid' => $pid, 'bitrate' => $bitrate,
    ]));

    mc1_api_respond([
        'ok'       => true,
        'pid'      => $pid,
        'bitrate'  => $bitrate,
        'manifest' => '/dash/slot' . $slot_id . '.mpd',
    ]);
    return;
}


/* =========================================================================
 * action: stop — stop HLS/DASH ffmpeg process(es) for a slot
 * ========================================================================= */
if ($action === 'stop') {
    if (!$is_admin) {
        mc1_api_respond(['error' => 'Admin permission required'], 403);
        return;
    }

    $slot_id = (int)($body['slot_id'] ?? 0);
    $format  = (string)($body['format'] ?? 'both'); /* hls, dash, or both */

    if ($slot_id < 1) {
        mc1_api_respond(['error' => 'slot_id is required'], 400);
        return;
    }

    if (!in_array($format, ['hls', 'dash', 'both'], true)) {
        $format = 'both';
    }

    $killed = [];

    /* We stop HLS processes */
    if ($format === 'hls' || $format === 'both') {
        foreach ($valid_bitrates as $br) {
            $pid_file = mc1_hls_pid_file($slot_id, 'hls', $br);
            $status = mc1_hls_is_running($pid_file);
            if ($status['running'] && $status['pid'] > 0) {
                @posix_kill($status['pid'], 15);
                /* We give ffmpeg up to 2 seconds to exit cleanly */
                $waited = 0;
                while ($waited < 2000 && file_exists('/proc/' . $status['pid'])) {
                    usleep(100000);
                    $waited += 100;
                }
                if (file_exists('/proc/' . $status['pid'])) {
                    @posix_kill($status['pid'], 9);
                }
                @unlink($pid_file);
                $killed[] = ['format' => 'hls', 'bitrate' => $br, 'pid' => $status['pid']];
            }
        }
    }

    /* We stop DASH process */
    if ($format === 'dash' || $format === 'both') {
        $pid_file = mc1_hls_pid_file($slot_id, 'dash');
        $status = mc1_hls_is_running($pid_file);
        if ($status['running'] && $status['pid'] > 0) {
            @posix_kill($status['pid'], 15);
            $waited = 0;
            while ($waited < 2000 && file_exists('/proc/' . $status['pid'])) {
                usleep(100000);
                $waited += 100;
            }
            if (file_exists('/proc/' . $status['pid'])) {
                @posix_kill($status['pid'], 9);
            }
            @unlink($pid_file);
            $killed[] = ['format' => 'dash', 'pid' => $status['pid']];
        }
    }

    /* We optionally clean up segment files */
    $cleanup = (bool)($body['cleanup'] ?? false);
    if ($cleanup) {
        mc1_cleanup_segments($slot_id, $format);
    }

    mc1_log(4, 'hls/dash stop', json_encode([
        'slot_id' => $slot_id, 'format' => $format, 'killed' => count($killed),
    ]));

    mc1_api_respond(['ok' => true, 'killed' => $killed, 'cleanup' => $cleanup]);
    return;
}


/* =========================================================================
 * action: status — check HLS/DASH status for one or all slots
 * ========================================================================= */
if ($action === 'status') {
    $slot_id = isset($body['slot_id']) ? (int)$body['slot_id'] : 0;
    $slots_to_check = [];

    if ($slot_id > 0) {
        $slots_to_check = [$slot_id];
    } else {
        /* We scan for any PID files to discover active slots */
        $pid_files = glob('/tmp/mc1_hls_slot*_*.pid');
        $dash_pids = glob('/tmp/mc1_dash_slot*.pid');
        $all_pids  = array_merge($pid_files ?: [], $dash_pids ?: []);
        $found_slots = [];
        foreach ($all_pids as $pf) {
            if (preg_match('/slot(\d+)/', $pf, $m)) {
                $found_slots[(int)$m[1]] = true;
            }
        }
        $slots_to_check = array_keys($found_slots);
        sort($slots_to_check);
    }

    $results = [];
    foreach ($slots_to_check as $sid) {
        $slot_status = [
            'slot_id' => $sid,
            'hls'     => ['active' => false, 'variants' => []],
            'dash'    => ['active' => false],
        ];

        /* We check each HLS bitrate variant */
        foreach ($valid_bitrates as $br) {
            $pid_file = mc1_hls_pid_file($sid, 'hls', $br);
            $run = mc1_hls_is_running($pid_file);
            if ($run['running']) {
                $slot_status['hls']['active'] = true;
                $seg_count = mc1_count_segments(MC1_HLS_DIR, 'slot' . $sid . '_' . $br . 'k_*.ts');
                $seg_bytes = mc1_segments_size(MC1_HLS_DIR, 'slot' . $sid . '_' . $br . 'k_*.ts');
                $slot_status['hls']['variants'][] = [
                    'bitrate'    => $br,
                    'pid'        => $run['pid'],
                    'segments'   => $seg_count,
                    'size_bytes' => $seg_bytes,
                    'playlist'   => '/hls/slot' . $sid . '_' . $br . 'k.m3u8',
                ];
            }
        }

        if ($slot_status['hls']['active']) {
            $slot_status['hls']['master_playlist'] = '/hls/slot' . $sid . '.m3u8';
        }

        /* We check DASH */
        $dash_pid_file = mc1_hls_pid_file($sid, 'dash');
        $dash_run = mc1_hls_is_running($dash_pid_file);
        if ($dash_run['running']) {
            $slot_status['dash']['active']    = true;
            $slot_status['dash']['pid']       = $dash_run['pid'];
            $seg_count = mc1_count_segments(MC1_DASH_DIR, 'slot' . $sid . '*.m4s');
            $seg_bytes = mc1_segments_size(MC1_DASH_DIR, 'slot' . $sid . '*.m4s');
            $slot_status['dash']['segments']   = $seg_count;
            $slot_status['dash']['size_bytes'] = $seg_bytes;
            $slot_status['dash']['manifest']   = '/dash/slot' . $sid . '.mpd';
        }

        $results[] = $slot_status;
    }

    mc1_api_respond(['ok' => true, 'streams' => $results]);
    return;
}


/* =========================================================================
 * action: list_streams — list all active HLS/DASH streams with embed URLs
 * ========================================================================= */
if ($action === 'list_streams') {
    $streams = [];

    /* We scan for HLS PID files */
    $hls_pids = glob('/tmp/mc1_hls_slot*_*.pid') ?: [];
    $hls_slots = [];
    foreach ($hls_pids as $pf) {
        if (preg_match('/slot(\d+)_(\d+)k\.pid/', $pf, $m)) {
            $sid = (int)$m[1];
            $br  = (int)$m[2];
            $run = mc1_hls_is_running($pf);
            if ($run['running']) {
                if (!isset($hls_slots[$sid])) {
                    $hls_slots[$sid] = [];
                }
                $hls_slots[$sid][] = $br;
            }
        }
    }

    foreach ($hls_slots as $sid => $bitrates) {
        sort($bitrates);
        $streams[] = [
            'slot_id'         => $sid,
            'format'          => 'hls',
            'bitrates'        => $bitrates,
            'master_playlist' => '/hls/slot' . $sid . '.m3u8',
            'player_url'      => '/widget-hls.php?stream=slot' . $sid . '&format=hls',
        ];
    }

    /* We scan for DASH PID files */
    $dash_pids = glob('/tmp/mc1_dash_slot*.pid') ?: [];
    foreach ($dash_pids as $pf) {
        if (preg_match('/slot(\d+)\.pid/', $pf, $m)) {
            $sid = (int)$m[1];
            $run = mc1_hls_is_running($pf);
            if ($run['running']) {
                $streams[] = [
                    'slot_id'    => $sid,
                    'format'     => 'dash',
                    'manifest'   => '/dash/slot' . $sid . '.mpd',
                    'player_url' => '/widget-hls.php?stream=slot' . $sid . '&format=dash',
                ];
            }
        }
    }

    mc1_api_respond(['ok' => true, 'streams' => $streams]);
    return;
}


/* We return 400 for any unknown action */
mc1_api_respond(['error' => 'Unknown action: ' . $action], 400);
