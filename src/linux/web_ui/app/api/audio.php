<?php
/**
 * audio.php — Audio File Streaming Endpoint
 *
 * File:    src/linux/web_ui/app/api/audio.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-02-24
 * Purpose: We serve audio files to the browser's <audio> element with full
 *          HTTP Range request support (206 Partial Content) so seeking works.
 *          We look up the file_path via the tracks table — no raw paths accepted.
 *
 *          GET  ?id={track_id}  — stream audio file bytes
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We require auth for all requests (audio files are encoder content)
 *  - We use first-person plural in all comments
 *  - We do NOT use mc1_api_respond() here — we send binary audio data
 *
 * CHANGELOG:
 *  2026-02-24  v1.0.0  Initial implementation — range-aware audio streaming
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/../inc/mc1_config.php';
require_once __DIR__ . '/../inc/db.php';
require_once __DIR__ . '/../inc/logger.php';
require_once __DIR__ . '/../inc/auth.php';

/* ── We disable output compression so Content-Length stays accurate ──────── */
if (function_exists('apache_setenv')) {
    apache_setenv('no-gzip', '1');
}
@ini_set('zlib.output_compression', 'Off');

/* We flush any pending output buffer before streaming audio */
while (ob_get_level() > 0) {
    ob_end_clean();
}

/* ── Method gate — we only serve GET requests ────────────────────────────── */
if ($_SERVER['REQUEST_METHOD'] !== 'GET') {
    http_response_code(405);
    header('Content-Type: application/json');
    echo json_encode(['error' => 'Method not allowed']);
    return;
}

/* ── Auth gate — we require a valid C++ session ──────────────────────────── */
if (!mc1_is_authed()) {
    http_response_code(403);
    header('Content-Type: application/json');
    echo json_encode(['error' => 'Unauthorized']);
    return;
}

/* ── Validate track_id parameter ─────────────────────────────────────────── */
$track_id = (int)($_GET['id'] ?? 0);
if ($track_id < 1) {
    http_response_code(400);
    header('Content-Type: application/json');
    echo json_encode(['error' => 'id parameter required']);
    return;
}

/* ── Fetch file_path from DB — we never accept raw paths from the URL ─────── */
$file_path = '';
try {
    $pdo  = mc1_db('mcaster1_media');
    $stmt = $pdo->prepare(
        'SELECT file_path FROM tracks WHERE id = ? AND is_missing = 0 LIMIT 1'
    );
    $stmt->execute([$track_id]);
    $row = $stmt->fetch(\PDO::FETCH_ASSOC);
    if ($row) {
        $file_path = (string)$row['file_path'];
    }
} catch (Exception $e) {
    http_response_code(500);
    header('Content-Type: application/json');
    echo json_encode(['error' => 'Database error']);
    mc1_log(2, 'audio.php DB lookup failed', json_encode(['id' => $track_id, 'err' => $e->getMessage()]));
    return;
}

if (!$file_path || !is_readable($file_path)) {
    http_response_code(404);
    header('Content-Type: application/json');
    echo json_encode(['error' => 'Track not found or not readable', 'id' => $track_id]);
    return;
}

/* ── Determine MIME type from file extension ─────────────────────────────── */
$mime_map = [
    'mp3'  => 'audio/mpeg',
    'ogg'  => 'audio/ogg',
    'flac' => 'audio/flac',
    'opus' => 'audio/ogg; codecs=opus',
    'aac'  => 'audio/mp4',
    'm4a'  => 'audio/mp4',
    'wav'  => 'audio/wav',
    'wma'  => 'audio/x-ms-wma',
];
$ext       = strtolower(pathinfo($file_path, PATHINFO_EXTENSION));
$mime_type = $mime_map[$ext] ?? 'application/octet-stream';

/* ── Parse HTTP Range header for seek support ────────────────────────────── */
$file_size = filesize($file_path);
if ($file_size === false || $file_size <= 0) {
    http_response_code(500);
    header('Content-Type: application/json');
    echo json_encode(['error' => 'Cannot stat file']);
    return;
}

$start  = 0;
$end    = $file_size - 1;
$length = $file_size;

if (isset($_SERVER['HTTP_RANGE'])) {
    /* We parse: "bytes=start-end" or "bytes=start-" or "bytes=-suffix" */
    if (preg_match('/bytes=(\d*)-(\d*)/i', $_SERVER['HTTP_RANGE'], $m)) {
        $range_start = ($m[1] !== '') ? (int)$m[1] : 0;
        $range_end   = ($m[2] !== '') ? (int)$m[2] : ($file_size - 1);
        $range_end   = min($range_end, $file_size - 1);

        if ($range_start > $range_end || $range_start >= $file_size) {
            /* We return 416 Range Not Satisfiable for invalid ranges */
            http_response_code(416);
            header('Content-Range: bytes */' . $file_size);
            return;
        }

        $start  = $range_start;
        $end    = $range_end;
        $length = $end - $start + 1;
        http_response_code(206);
        header('Content-Range: bytes ' . $start . '-' . $end . '/' . $file_size);
    }
} else {
    http_response_code(200);
}

/* ── Send audio headers ──────────────────────────────────────────────────── */
header('Content-Type: ' . $mime_type);
header('Content-Length: ' . $length);
header('Accept-Ranges: bytes');
header('Cache-Control: no-cache, no-store');
header('X-Track-Id: ' . $track_id);

/* ── Stream the audio data in 64 KB chunks ───────────────────────────────── */
$fh = fopen($file_path, 'rb');
if ($fh === false) {
    http_response_code(500);
    header('Content-Type: application/json');
    echo json_encode(['error' => 'Cannot open file']);
    return;
}

fseek($fh, $start);
$remaining = $length;
while ($remaining > 0 && !feof($fh)) {
    $chunk = min(65536, $remaining);   /* We stream in 64 KB chunks */
    $data  = fread($fh, $chunk);
    if ($data === false) { break; }
    echo $data;
    $remaining -= strlen($data);
    if (connection_aborted()) { break; }
}
fclose($fh);

mc1_log(5, 'audio.php stream', json_encode(['id' => $track_id, 'start' => $start, 'length' => $length]));
