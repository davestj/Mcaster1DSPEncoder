<?php
/**
 * ndi.php -- NDI / Network Device Interface Management API
 *
 * File:    src/linux/web_ui/app/api/ndi.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-29
 * Purpose: We manage NDI source discovery, HLS preview via ffmpeg,
 *          capture sessions, and network source CRUD for the
 *          video producer and settings pages.
 *
 * Actions (JSON POST, all require auth):
 *  discover       — scan LAN for NDI sources (ffmpeg or avahi-browse fallback)
 *  preview        — start HLS preview of an NDI/network source via ffmpeg
 *  stop_preview   — kill the ffmpeg preview process
 *  start_capture  — begin capturing an NDI source to file or producer pipe
 *  stop           — kill the ffmpeg NDI capture process
 *  list_active    — list active NDI/network captures
 *  list_sources   — list saved network_sources from DB
 *  save_source    — upsert a network source into DB
 *  delete_source  — delete a saved network source from DB
 *  check_support  — check NDI SDK + ffmpeg NDI availability
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We use first-person plural throughout all comments
 *  - We use mc1_api_respond() for all JSON responses
 *  - We use escapeshellarg() on ALL shell arguments
 *  - PID files in /tmp for process management
 */

define('MC1_BOOT', true);
$API_VERSION = '2.0.1';
require_once __DIR__ . '/../inc/mc1_config.php';
require_once __DIR__ . '/../inc/db.php';
require_once __DIR__ . '/../inc/traits.db.class.php';
require_once __DIR__ . '/../inc/logger.php';
require_once __DIR__ . '/../inc/auth.php';
require_once __DIR__ . '/../inc/user_auth.php';

/* -- Auth gate ------------------------------------------------------------ */
if (!mc1_is_authed()) {
    header('Content-Type: application/json');
    mc1_api_respond(['error' => 'Unauthorized'], 403);
    return;
}

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    header('Content-Type: application/json');
    mc1_api_respond(['error' => 'POST required'], 405);
    return;
}

$user     = mc1_current_user();
$is_admin = $user && !empty($user['can_admin']);

/* -- We read the JSON body ------------------------------------------------ */
$raw    = file_get_contents('php://input');
$input  = json_decode($raw, true);
$action = $input['action'] ?? '';

/* -- HLS output directory ------------------------------------------------- */
$hls_dir = '/var/www/mcaster1.com/Mcaster1DSPEncoder/src/linux/web_ui/hls';
if (!is_dir($hls_dir)) {
    @mkdir($hls_dir, 0755, true);
}

/* -- PID file helpers ----------------------------------------------------- */

/**
 * We write the ffmpeg PID to a known location for later cleanup.
 */
function ndi_pid_file(string $key): string {
    return '/tmp/mc1_ndi_' . preg_replace('/[^a-zA-Z0-9_-]/', '_', $key) . '.pid';
}

/**
 * We check if a process is still alive.
 */
function ndi_is_running(string $pid_file): bool {
    if (!file_exists($pid_file)) return false;
    $pid = (int)trim(file_get_contents($pid_file));
    if ($pid <= 0) return false;
    /* We use kill(0) to test if the process exists */
    return posix_kill($pid, 0);
}

/**
 * We kill the process and clean up the PID file.
 */
function ndi_kill(string $pid_file): bool {
    if (!file_exists($pid_file)) return false;
    $pid = (int)trim(file_get_contents($pid_file));
    if ($pid > 0) {
        posix_kill($pid, 15); // SIGTERM
        usleep(500000);
        if (posix_kill($pid, 0)) {
            posix_kill($pid, 9); // SIGKILL fallback
        }
    }
    @unlink($pid_file);
    return true;
}

/**
 * We detect whether ffmpeg has NDI support compiled in.
 */
function ndi_check_ffmpeg_ndi(): array {
    $ffmpeg = trim(shell_exec('which ffmpeg 2>/dev/null') ?? '');
    if (!$ffmpeg) {
        return ['ffmpeg_found' => false, 'ndi_input' => false, 'ndi_output' => false];
    }

    /* We check for libndi_newtek in devices and protocols */
    $devices = shell_exec(escapeshellarg($ffmpeg) . ' -devices 2>&1') ?? '';
    $protocols = shell_exec(escapeshellarg($ffmpeg) . ' -protocols 2>&1') ?? '';

    $has_ndi_input  = (stripos($devices, 'ndi') !== false) || (stripos($protocols, 'ndi') !== false);
    $has_ndi_output = (stripos($devices, 'ndi') !== false);

    return [
        'ffmpeg_found' => true,
        'ndi_input'    => $has_ndi_input,
        'ndi_output'   => $has_ndi_output,
    ];
}

/**
 * We detect whether avahi-browse is available for mDNS discovery.
 */
function ndi_check_avahi(): bool {
    $path = trim(shell_exec('which avahi-browse 2>/dev/null') ?? '');
    return !empty($path);
}

/* =========================================================================
 * Action dispatcher
 * ========================================================================= */

header('Content-Type: application/json');

if ($action === 'check_support') {
    /* ── Check NDI SDK + ffmpeg support ────────────────────────────────── */
    $ffmpeg_ndi = ndi_check_ffmpeg_ndi();
    $avahi      = ndi_check_avahi();

    mc1_api_respond([
        'ok'            => true,
        'ffmpeg_found'  => $ffmpeg_ndi['ffmpeg_found'],
        'ndi_supported' => $ffmpeg_ndi['ndi_input'],
        'ndi_output'    => $ffmpeg_ndi['ndi_output'],
        'avahi_found'   => $avahi,
        'fallback_mode' => !$ffmpeg_ndi['ndi_input'],
        'message'       => $ffmpeg_ndi['ndi_input']
            ? 'NDI via ffmpeg is available'
            : ($avahi ? 'NDI SDK not installed — using mDNS discovery + network stream fallback' : 'NDI SDK not installed — manual network source input only'),
    ]);

} elseif ($action === 'discover') {
    /* ── Discover NDI sources on the LAN ──────────────────────────────── */
    $sources = [];
    $ffmpeg_ndi = ndi_check_ffmpeg_ndi();
    $method = 'none';

    if ($ffmpeg_ndi['ndi_input']) {
        /* We try ffmpeg NDI device listing first */
        $method = 'ffmpeg_ndi';
        $cmd = 'timeout 5 ffmpeg -f libndi_newtek -find_sources 1 -i dummy 2>&1';
        $output = shell_exec($cmd) ?? '';

        /* We parse the ffmpeg NDI source listing output.
         * Example lines: "    'OBS (Studio PC)'     192.168.1.50"
         */
        if (preg_match_all("/'([^']+)'/", $output, $matches)) {
            foreach ($matches[1] as $name) {
                $sources[] = [
                    'name' => $name,
                    'type' => 'ndi',
                    'url'  => $name, /* ffmpeg NDI uses the name directly as -i */
                ];
            }
        }
    }

    if (empty($sources) && ndi_check_avahi()) {
        /* We fall back to avahi-browse for mDNS NDI discovery */
        $method = 'avahi_mdns';
        $cmd = 'timeout 5 avahi-browse -rpt _ndi._tcp 2>/dev/null';
        $output = shell_exec($cmd) ?? '';

        /* avahi-browse -rpt output format:
         * =;eth0;IPv4;OBS (Studio PC);_ndi._tcp;local;studio-pc.local;192.168.1.50;5961;"..."
         */
        $lines = explode("\n", trim($output));
        foreach ($lines as $line) {
            if (strpos($line, '=;') !== 0) continue;
            $parts = explode(';', $line);
            if (count($parts) >= 8) {
                $name = $parts[3] ?? 'Unknown';
                $ip   = $parts[7] ?? '';
                $port = $parts[8] ?? '5961';
                $sources[] = [
                    'name' => $name,
                    'type' => 'ndi',
                    'ip'   => $ip,
                    'port' => (int)$port,
                    'url'  => $name,
                ];
            }
        }
    }

    if (empty($sources)) {
        $method = $method ?: 'none';
    }

    mc1_api_respond([
        'ok'      => true,
        'method'  => $method,
        'sources' => $sources,
        'count'   => count($sources),
        'message' => empty($sources) ? 'No NDI sources found on the network' : count($sources) . ' source(s) discovered',
    ]);

} elseif ($action === 'preview') {
    /* ── Start HLS preview of an NDI or network source ─────────────────── */
    $source_url  = trim($input['url']     ?? '');
    $source_type = trim($input['type']    ?? 'ndi');
    $source_name = trim($input['name']    ?? 'preview');
    $preview_id  = preg_replace('/[^a-zA-Z0-9_-]/', '_', $source_name ?: 'ndi_preview');

    if (empty($source_url)) {
        mc1_api_respond(['error' => 'Source URL is required'], 400);
        return;
    }

    /* We kill any existing preview for this source */
    $pid_file = ndi_pid_file('preview_' . $preview_id);
    ndi_kill($pid_file);

    /* We clean up old HLS segments */
    $hls_playlist = $hls_dir . '/' . $preview_id . '.m3u8';
    foreach (glob($hls_dir . '/' . $preview_id . '*.ts') as $f) {
        @unlink($f);
    }
    @unlink($hls_playlist);

    /* We build the ffmpeg command based on source type */
    $ffmpeg_ndi = ndi_check_ffmpeg_ndi();
    $log_file = '/tmp/mc1_ndi_preview_' . $preview_id . '.log';

    if ($source_type === 'ndi' && $ffmpeg_ndi['ndi_input']) {
        /* Native NDI input via ffmpeg */
        $cmd = 'ffmpeg -f libndi_newtek -i ' . escapeshellarg($source_url)
             . ' -c:v libx264 -preset ultrafast -tune zerolatency -b:v 1000k'
             . ' -c:a aac -b:a 128k'
             . ' -f hls -hls_time 2 -hls_list_size 5 -hls_flags delete_segments'
             . ' ' . escapeshellarg($hls_playlist)
             . ' > ' . escapeshellarg($log_file) . ' 2>&1 & echo $!';
    } else {
        /* Network stream fallback: MPEG-TS, RTSP, SRT, HTTP, etc. */
        $input_opts = '';
        if ($source_type === 'rtsp') {
            $input_opts = '-rtsp_transport tcp ';
        } elseif ($source_type === 'srt') {
            $input_opts = '-re ';
        }

        $cmd = 'ffmpeg ' . $input_opts . '-i ' . escapeshellarg($source_url)
             . ' -c:v libx264 -preset ultrafast -tune zerolatency -b:v 1000k'
             . ' -c:a aac -b:a 128k'
             . ' -f hls -hls_time 2 -hls_list_size 5 -hls_flags delete_segments'
             . ' ' . escapeshellarg($hls_playlist)
             . ' > ' . escapeshellarg($log_file) . ' 2>&1 & echo $!';
    }

    $pid = (int)trim(shell_exec($cmd) ?? '');
    if ($pid > 0) {
        file_put_contents($pid_file, (string)$pid);
    }

    /* We wait briefly for the first HLS segment to appear */
    usleep(1500000);

    mc1_api_respond([
        'ok'       => true,
        'pid'      => $pid,
        'hls_url'  => '/hls/' . $preview_id . '.m3u8',
        'pid_file' => basename($pid_file),
    ]);

} elseif ($action === 'stop_preview') {
    /* ── Stop an HLS preview ──────────────────────────────────────────── */
    $preview_id = preg_replace('/[^a-zA-Z0-9_-]/', '_', trim($input['name'] ?? 'ndi_preview'));
    $pid_file   = ndi_pid_file('preview_' . $preview_id);

    $killed = ndi_kill($pid_file);

    /* We clean up HLS segments */
    foreach (glob($hls_dir . '/' . $preview_id . '*.ts') as $f) {
        @unlink($f);
    }
    @unlink($hls_dir . '/' . $preview_id . '.m3u8');

    mc1_api_respond(['ok' => true, 'killed' => $killed]);

} elseif ($action === 'start_capture') {
    /* ── Start capturing an NDI/network source ────────────────────────── */
    $source_url  = trim($input['url']     ?? '');
    $source_type = trim($input['type']    ?? 'ndi');
    $source_name = trim($input['name']    ?? 'capture');
    $output_file = trim($input['output']  ?? '');
    $capture_id  = preg_replace('/[^a-zA-Z0-9_-]/', '_', $source_name ?: 'ndi_capture');

    if (empty($source_url)) {
        mc1_api_respond(['error' => 'Source URL is required'], 400);
        return;
    }

    /* We default output to a timestamped file in the audio root */
    if (empty($output_file)) {
        $ts = date('Ymd_His');
        $output_file = MC1_AUDIO_ROOT . '/ndi_capture_' . $capture_id . '_' . $ts . '.mp4';
    }

    /* We kill any existing capture with the same ID */
    $pid_file = ndi_pid_file('capture_' . $capture_id);
    ndi_kill($pid_file);

    $ffmpeg_ndi = ndi_check_ffmpeg_ndi();
    $log_file   = '/tmp/mc1_ndi_capture_' . $capture_id . '.log';

    if ($source_type === 'ndi' && $ffmpeg_ndi['ndi_input']) {
        $cmd = 'ffmpeg -f libndi_newtek -i ' . escapeshellarg($source_url)
             . ' -c:v libx264 -preset fast -crf 18'
             . ' -c:a aac -b:a 192k'
             . ' ' . escapeshellarg($output_file)
             . ' > ' . escapeshellarg($log_file) . ' 2>&1 & echo $!';
    } else {
        $input_opts = '';
        if ($source_type === 'rtsp') {
            $input_opts = '-rtsp_transport tcp ';
        }

        $cmd = 'ffmpeg ' . $input_opts . '-i ' . escapeshellarg($source_url)
             . ' -c:v libx264 -preset fast -crf 18'
             . ' -c:a aac -b:a 192k'
             . ' ' . escapeshellarg($output_file)
             . ' > ' . escapeshellarg($log_file) . ' 2>&1 & echo $!';
    }

    $pid = (int)trim(shell_exec($cmd) ?? '');
    if ($pid > 0) {
        file_put_contents($pid_file, (string)$pid);
    }

    /* We update the network_sources table to mark this source active */
    try {
        $db = mc1_db('mcaster1_encoder');
        $db->prepare('UPDATE network_sources SET is_active = 1 WHERE name = ? LIMIT 1')
           ->execute([$source_name]);
    } catch (\Exception $e) {
        /* Non-fatal — DB row may not exist yet */
    }

    mc1_api_respond([
        'ok'     => true,
        'pid'    => $pid,
        'output' => $output_file,
    ]);

} elseif ($action === 'stop') {
    /* ── Stop an NDI capture ──────────────────────────────────────────── */
    $capture_id = preg_replace('/[^a-zA-Z0-9_-]/', '_', trim($input['name'] ?? 'ndi_capture'));
    $pid_file   = ndi_pid_file('capture_' . $capture_id);
    $killed     = ndi_kill($pid_file);

    /* We mark the source inactive in DB */
    $source_name = trim($input['name'] ?? '');
    if ($source_name) {
        try {
            $db = mc1_db('mcaster1_encoder');
            $db->prepare('UPDATE network_sources SET is_active = 0 WHERE name = ? LIMIT 1')
               ->execute([$source_name]);
        } catch (\Exception $e) {}
    }

    mc1_api_respond(['ok' => true, 'killed' => $killed]);

} elseif ($action === 'list_active') {
    /* ── List active NDI/network captures ─────────────────────────────── */
    $active = [];

    /* We scan /tmp for our PID files */
    foreach (glob('/tmp/mc1_ndi_*.pid') as $pf) {
        $key = str_replace(['/tmp/mc1_ndi_', '.pid'], '', $pf);
        $pid = (int)trim(file_get_contents($pf));
        $is_preview = (strpos($key, 'preview_') === 0);
        $is_capture = (strpos($key, 'capture_') === 0);
        $running = ($pid > 0) && posix_kill($pid, 0);

        if (!$running) {
            @unlink($pf);
            continue;
        }

        $entry = [
            'key'     => $key,
            'pid'     => $pid,
            'type'    => $is_preview ? 'preview' : ($is_capture ? 'capture' : 'unknown'),
            'name'    => preg_replace('/^(preview|capture)_/', '', $key),
            'running' => true,
        ];

        if ($is_preview) {
            $preview_id  = preg_replace('/^preview_/', '', $key);
            $entry['hls_url'] = '/hls/' . $preview_id . '.m3u8';
        }

        $active[] = $entry;
    }

    mc1_api_respond(['ok' => true, 'active' => $active, 'count' => count($active)]);

} elseif ($action === 'list_sources') {
    /* ── List saved network sources from DB ───────────────────────────── */
    $sources = [];
    try {
        $db = mc1_db('mcaster1_encoder');
        $stmt = $db->query(
            'SELECT id, name, type, url, is_active, last_seen_at, config_json, created_at
             FROM network_sources ORDER BY name'
        );
        $sources = $stmt->fetchAll();
    } catch (\Exception $e) {
        mc1_log(2, 'ndi.php DB error', json_encode(['err' => $e->getMessage()]));
        mc1_api_respond(['error' => 'Database operation failed. Check server logs.'], 500);
        return;
    }

    mc1_api_respond(['ok' => true, 'sources' => $sources]);

} elseif ($action === 'save_source') {
    /* ── Save or update a network source in DB ────────────────────────── */
    $id   = (int)($input['id'] ?? 0);
    $name = trim($input['name'] ?? '');
    $type = trim($input['type'] ?? 'ndi');
    $url  = trim($input['url']  ?? '');

    if (empty($name) || empty($url)) {
        mc1_api_respond(['error' => 'Name and URL are required'], 400);
        return;
    }

    $valid_types = ['ndi', 'mpeg_ts', 'rtsp', 'srt', 'http'];
    if (!in_array($type, $valid_types)) {
        mc1_api_respond(['error' => 'Invalid source type. Must be one of: ' . implode(', ', $valid_types)], 400);
        return;
    }

    $config_json = null;
    if (!empty($input['config'])) {
        $config_json = json_encode($input['config']);
    }

    try {
        $db = mc1_db('mcaster1_encoder');

        if ($id > 0) {
            /* We update an existing source */
            $stmt = $db->prepare(
                'UPDATE network_sources SET name = ?, type = ?, url = ?, config_json = ?, last_seen_at = NOW()
                 WHERE id = ?'
            );
            $stmt->execute([$name, $type, $url, $config_json, $id]);
        } else {
            /* We insert a new source */
            $stmt = $db->prepare(
                'INSERT INTO network_sources (name, type, url, config_json, last_seen_at)
                 VALUES (?, ?, ?, ?, NOW())'
            );
            $stmt->execute([$name, $type, $url, $config_json]);
            $id = (int)$db->lastInsertId();
        }

        mc1_api_respond(['ok' => true, 'id' => $id]);
    } catch (\Exception $e) {
        mc1_log(2, 'ndi.php DB error', json_encode(['err' => $e->getMessage()]));
        mc1_api_respond(['error' => 'Database operation failed. Check server logs.'], 500);
        return;
    }

} elseif ($action === 'delete_source') {
    /* ── Delete a saved network source from DB ────────────────────────── */
    $id = (int)($input['id'] ?? 0);
    if ($id <= 0) {
        mc1_api_respond(['error' => 'Source ID is required'], 400);
        return;
    }

    try {
        $db = mc1_db('mcaster1_encoder');
        $db->prepare('DELETE FROM network_sources WHERE id = ?')->execute([$id]);
        mc1_api_respond(['ok' => true]);
    } catch (\Exception $e) {
        mc1_log(2, 'ndi.php DB error', json_encode(['err' => $e->getMessage()]));
        mc1_api_respond(['error' => 'Database operation failed. Check server logs.'], 500);
        return;
    }

} else {
    mc1_api_respond(['error' => 'Unknown action: ' . ($action ?: '(empty)')], 400);
}
