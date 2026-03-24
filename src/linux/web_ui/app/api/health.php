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

} elseif ($action === 'disk_usage') {

    $disks = [];
    $out = shell_exec('df -BM --output=source,fstype,size,used,avail,pcent,target 2>/dev/null');
    if ($out) {
        $lines = explode("\n", trim($out));
        array_shift($lines);
        foreach ($lines as $line) {
            $parts = preg_split('/\s+/', trim($line), 7);
            if (count($parts) < 7) continue;
            if (strpos($parts[0], '/dev/') !== 0 && $parts[0] !== 'tmpfs') continue;
            if (in_array($parts[6], ['/boot/efi', '/snap'])) continue;
            $disks[] = [
                'source' => $parts[0], 'fstype' => $parts[1],
                'size_mb' => (int)$parts[2], 'used_mb' => (int)$parts[3],
                'avail_mb' => (int)$parts[4], 'use_pct' => (int)$parts[5], 'mount' => $parts[6],
            ];
        }
    }
    mc1_api_respond(['ok' => true, 'disks' => $disks]);

} elseif ($action === 'fpm_status') {

    $fpm = ['pool' => 'mcaster1', 'active' => false, 'processes' => 0];
    $out = shell_exec('pgrep -c php-fpm 2>/dev/null');
    $fpm['processes'] = (int)trim($out ?: '0');
    $fpm['active'] = $fpm['processes'] > 0;
    $fpm['socket_exists'] = file_exists('/run/php/php8.2-fpm-mc1.sock');
    $fpm['php_version'] = phpversion();
    $fpm['memory_limit'] = ini_get('memory_limit');
    mc1_api_respond(['ok' => true, 'fpm' => $fpm]);

} elseif ($action === 'ssl_info') {

    $cert_path = '/etc/ssl/certs/encoder.mcaster1.com.pem';
    $ssl = ['has_cert' => false];
    if (file_exists($cert_path)) {
        $cert_data = openssl_x509_parse(file_get_contents($cert_path));
        if ($cert_data) {
            $ssl['has_cert'] = true;
            $ssl['subject'] = $cert_data['subject']['CN'] ?? '';
            $ssl['issuer'] = $cert_data['issuer']['O'] ?? $cert_data['issuer']['CN'] ?? '';
            $ssl['valid_from'] = date('Y-m-d', $cert_data['validFrom_time_t'] ?? 0);
            $ssl['valid_to'] = date('Y-m-d', $cert_data['validTo_time_t'] ?? 0);
            $ssl['days_left'] = max(0, (int)(($cert_data['validTo_time_t'] - time()) / 86400));
        }
    }
    mc1_api_respond(['ok' => true, 'ssl' => $ssl]);

} elseif ($action === 'codec_check') {

    $codecs = [];
    $checks = [
        'lame' => ['cmd' => 'lame --version 2>&1 | head -1', 'name' => 'MP3 (LAME)'],
        'vorbis' => ['cmd' => 'oggenc --version 2>&1 | head -1', 'name' => 'Ogg Vorbis'],
        'opus' => ['cmd' => 'opusenc --version 2>&1 | head -1', 'name' => 'Opus'],
        'flac' => ['cmd' => 'flac --version 2>&1 | head -1', 'name' => 'FLAC'],
        'fdkaac' => ['cmd' => 'ldconfig -p 2>/dev/null | grep libfdk-aac', 'name' => 'AAC (fdk-aac)'],
        'mpg123' => ['cmd' => 'mpg123 --version 2>&1 | head -1', 'name' => 'mpg123'],
        'ffmpeg' => ['cmd' => 'ffmpeg -version 2>&1 | head -1', 'name' => 'FFmpeg'],
        'taglib' => ['cmd' => 'ldconfig -p 2>/dev/null | grep libtag', 'name' => 'TagLib'],
        'portaudio' => ['cmd' => 'ldconfig -p 2>/dev/null | grep libportaudio', 'name' => 'PortAudio'],
        'jack' => ['cmd' => 'jackd --version 2>&1 | head -1', 'name' => 'JACK'],
    ];
    foreach ($checks as $key => $info) {
        $out = trim(shell_exec($info['cmd']) ?: '');
        $codecs[] = ['key' => $key, 'name' => $info['name'], 'installed' => !empty($out), 'version' => substr($out, 0, 80)];
    }
    mc1_api_respond(['ok' => true, 'codecs' => $codecs]);

} elseif ($action === 'swap_info') {

    $swap = ['total_mb' => 0, 'used_mb' => 0, 'free_mb' => 0, 'pct' => 0];
    $out = shell_exec('free -m 2>/dev/null | grep Swap');
    if ($out && preg_match('/Swap:\s+(\d+)\s+(\d+)\s+(\d+)/', $out, $m)) {
        $swap['total_mb'] = (int)$m[1]; $swap['used_mb'] = (int)$m[2]; $swap['free_mb'] = (int)$m[3];
        $swap['pct'] = $swap['total_mb'] > 0 ? round($swap['used_mb'] / $swap['total_mb'] * 100, 1) : 0;
    }
    mc1_api_respond(['ok' => true, 'swap' => $swap]);

} else {
    mc1_api_respond(['ok' => false, 'error' => 'Unknown action'], 400);
}
