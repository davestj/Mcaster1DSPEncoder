<?php
/**
 * producer.php -- Video Producer Streaming & Recording API
 *
 * File:    src/linux/web_ui/app/api/producer.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Purpose: We manage video streaming relay (browser → ffmpeg → RTMP/Icecast2)
 *          and server-side recording for the video producer page.
 *
 * Actions (JSON POST, all require auth):
 *  start_stream    — create FIFO + start ffmpeg relay for an RTMP target
 *  stop_stream     — close FIFO, kill ffmpeg relay
 *  stream_status   — check ffmpeg PID, bytes streamed, uptime
 *  start_recording — start ffmpeg writing to archive file from FIFO
 *  stop_recording  — finalize recording file
 *  list_recordings — list video recordings in archive directory
 *
 * Chunk upload (binary POST with headers, requires auth):
 *  Content-Type: application/octet-stream
 *  X-Stream-Target: {target_id}
 *  X-Chunk-Index: {n}
 *  Body: raw WebM chunk data
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We use first-person plural throughout all comments
 *  - We use mc1_api_respond() for all JSON responses
 *  - We use escapeshellarg() on ALL shell arguments
 *  - PID files in /tmp for process management
 *  - Named pipes (mkfifo) for ffmpeg input
 *  - Clean up pipes + PIDs on stop
 */

define('MC1_BOOT', true);
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

/* =========================================================================
 * We detect whether this is a binary chunk upload or a JSON action request
 * by checking the Content-Type header.
 * ========================================================================= */

$content_type = $_SERVER['CONTENT_TYPE'] ?? $_SERVER['HTTP_CONTENT_TYPE'] ?? '';

if (strpos($content_type, 'application/octet-stream') !== false) {
    /* =====================================================================
     * Binary chunk upload — stream relay FIFO or vodcast file assembly
     * We detect which type by checking for X-Vodcast-Id vs X-Stream-Target.
     * ===================================================================== */

    $vodcast_id = (int)($_SERVER['HTTP_X_VODCAST_ID'] ?? 0);
    $target_id  = (int)($_SERVER['HTTP_X_STREAM_TARGET'] ?? 0);
    $chunk_idx  = (int)($_SERVER['HTTP_X_CHUNK_INDEX'] ?? 0);

    /* We read the raw POST body */
    $raw = file_get_contents('php://input');
    $bytes = strlen($raw);

    if ($bytes === 0) {
        header('Content-Type: application/json');
        mc1_api_respond(['error' => 'Empty chunk'], 400);
        return;
    }

    /* ── Vodcast chunk upload — append to recording file ── */
    if ($vodcast_id > 0) {
        $vodcast_meta = mc1_vodcast_meta_path($vodcast_id);
        if (!file_exists($vodcast_meta)) {
            header('Content-Type: application/json');
            mc1_api_respond(['error' => 'No active vodcast recording for id ' . $vodcast_id], 404);
            return;
        }

        $meta = json_decode((string)file_get_contents($vodcast_meta), true);
        $filepath = $meta['filepath'] ?? '';

        if ($filepath === '' || !is_dir(dirname($filepath))) {
            header('Content-Type: application/json');
            mc1_api_respond(['error' => 'Vodcast recording path invalid'], 500);
            return;
        }

        /* We append the raw data to the recording file */
        $written = @file_put_contents($filepath, $raw, FILE_APPEND | LOCK_EX);
        if ($written === false) {
            header('Content-Type: application/json');
            mc1_api_respond(['error' => 'Write to vodcast file failed'], 500);
            return;
        }

        header('Content-Type: application/json');
        mc1_api_respond(['ok' => true, 'chunk' => $chunk_idx, 'bytes' => $written]);
        return;
    }

    /* ── Stream relay chunk upload — write to FIFO pipe ── */
    if ($target_id < 1) {
        header('Content-Type: application/json');
        mc1_api_respond(['error' => 'X-Stream-Target or X-Vodcast-Id header required'], 400);
        return;
    }

    $pipe_path = mc1_producer_pipe_path($target_id);

    if (!file_exists($pipe_path)) {
        header('Content-Type: application/json');
        mc1_api_respond(['error' => 'No active stream for target ' . $target_id . '. Start stream first.'], 404);
        return;
    }

    /* We open the pipe in non-blocking write mode to prevent PHP from hanging
     * if ffmpeg is not reading fast enough. We retry with a short timeout. */
    $fp = @fopen($pipe_path, 'c');
    if (!$fp) {
        header('Content-Type: application/json');
        mc1_api_respond(['error' => 'Cannot open pipe for writing'], 500);
        return;
    }

    $written = @fwrite($fp, $raw);
    @fclose($fp);

    if ($written === false) {
        header('Content-Type: application/json');
        mc1_api_respond(['error' => 'Write to pipe failed'], 500);
        return;
    }

    /* We update the bytes counter file */
    $counter_file = mc1_producer_counter_path($target_id);
    $prev = file_exists($counter_file) ? (int)file_get_contents($counter_file) : 0;
    file_put_contents($counter_file, (string)($prev + $written));

    header('Content-Type: application/json');
    mc1_api_respond(['ok' => true, 'chunk' => $chunk_idx, 'bytes' => $written]);
    return;
}

/* =========================================================================
 * JSON action request
 * ========================================================================= */

header('Content-Type: application/json');

$raw    = (string)file_get_contents('php://input');
$body   = json_decode($raw, true) ?: [];
$action = (string)($body['action'] ?? '');

/* -- Helper functions ----------------------------------------------------- */

function mc1_producer_pipe_path(int $target_id): string {
    return '/tmp/mc1_video_stream_' . $target_id . '.pipe';
}

function mc1_producer_pid_path(int $target_id): string {
    return '/tmp/mc1_video_stream_' . $target_id . '.pid';
}

function mc1_producer_log_path(int $target_id): string {
    return '/tmp/mc1_video_stream_' . $target_id . '.log';
}

function mc1_producer_counter_path(int $target_id): string {
    return '/tmp/mc1_video_stream_' . $target_id . '.bytes';
}

function mc1_producer_rec_pid_path(int $rec_id): string {
    return '/tmp/mc1_video_rec_' . $rec_id . '.pid';
}

function mc1_producer_rec_pipe_path(int $rec_id): string {
    return '/tmp/mc1_video_rec_' . $rec_id . '.pipe';
}

function mc1_producer_is_running(int $target_id): array {
    $pid_file = mc1_producer_pid_path($target_id);
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

function mc1_producer_cleanup(int $target_id): void {
    $pipe = mc1_producer_pipe_path($target_id);
    $pid_file = mc1_producer_pid_path($target_id);
    $counter = mc1_producer_counter_path($target_id);
    @unlink($pipe);
    @unlink($pid_file);
    @unlink($counter);
}

/* -- Vodcast recording helpers ------------------------------------------- */

function mc1_vodcast_meta_path(int $vodcast_id): string {
    return '/tmp/mc1_vodcast_' . $vodcast_id . '.meta';
}

function mc1_vodcast_archive_dir(): string {
    $dir = '/var/www/mcaster1.com/audio/video_recordings';
    if (!is_dir($dir)) {
        @mkdir($dir, 0775, true);
    }
    return $dir;
}

/* We load an RTMP target row from the DB */
function mc1_producer_load_target(int $target_id): ?array {
    try {
        $pdo  = mc1_db('mcaster1_encoder');
        $stmt = $pdo->prepare('SELECT * FROM rtmp_targets WHERE id = ?');
        $stmt->execute([$target_id]);
        $row = $stmt->fetch(\PDO::FETCH_ASSOC);
        return $row ?: null;
    } catch (Exception $e) {
        mc1_log(2, 'producer load_target failed', json_encode([
            'target_id' => $target_id, 'err' => $e->getMessage()
        ]));
        return null;
    }
}

/* =========================================================================
 * action: start_stream — create FIFO pipe, launch ffmpeg relay to RTMP
 * ========================================================================= */
if ($action === 'start_stream') {
    if (!$is_admin) {
        mc1_api_respond(['error' => 'Admin permission required'], 403);
        return;
    }

    $target_id = (int)($body['target_id'] ?? 0);
    if ($target_id < 1) {
        mc1_api_respond(['error' => 'target_id required'], 400);
        return;
    }

    /* We check if already running */
    $status = mc1_producer_is_running($target_id);
    if ($status['running']) {
        mc1_api_respond(['error' => 'Stream relay already running (PID ' . $status['pid'] . ')'], 409);
        return;
    }

    /* We load the RTMP target from the DB */
    $target = mc1_producer_load_target($target_id);
    if (!$target) {
        mc1_api_respond(['error' => 'RTMP target not found'], 404);
        return;
    }

    $pipe_path = mc1_producer_pipe_path($target_id);
    $pid_file  = mc1_producer_pid_path($target_id);
    $log_file  = mc1_producer_log_path($target_id);

    /* We clean up any stale pipe from a previous run */
    if (file_exists($pipe_path)) {
        @unlink($pipe_path);
    }

    /* We create the named pipe (FIFO) */
    $mkfifo_cmd = 'mkfifo ' . escapeshellarg($pipe_path);
    $mkfifo_out = [];
    $mkfifo_ret = 0;
    exec($mkfifo_cmd, $mkfifo_out, $mkfifo_ret);
    if ($mkfifo_ret !== 0) {
        mc1_api_respond(['error' => 'Failed to create named pipe'], 500);
        return;
    }

    /* We reset the byte counter */
    file_put_contents(mc1_producer_counter_path($target_id), '0');

    /* We build the ffmpeg command based on the target type */
    $ffmpeg_bin = '/usr/bin/ffmpeg';
    $rtmp_dest  = rtrim($target['rtmp_url'], '/') . '/' . $target['stream_key'];

    /* We determine whether this is an Icecast WebM target or an RTMP target */
    $is_icecast = (stripos($target['rtmp_url'], 'icecast://') === 0);

    if ($is_icecast) {
        /* Icecast2 with WebM: VP9 + Vorbis */
        $res = (string)($target['video_resolution'] ?: '1280x720');
        $cmd = sprintf(
            'nohup %s -y -re -i %s '
            . '-c:v libvpx-vp9 -b:v 1500k -deadline realtime -cpu-used 8 '
            . '-s %s -r 30 '
            . '-c:a libvorbis -b:a 128k '
            . '-f webm -content_type video/webm '
            . '%s '
            . '> %s 2>&1 & echo $!',
            escapeshellarg($ffmpeg_bin),
            escapeshellarg($pipe_path),
            escapeshellarg($res),
            escapeshellarg($rtmp_dest),
            escapeshellarg($log_file)
        );
    } else {
        /* RTMP target (Twitch, YouTube, Facebook, custom): H264 + AAC */
        $res = (string)($target['video_resolution'] ?: '1280x720');
        $cmd = sprintf(
            'nohup %s -y -re -i %s '
            . '-c:v libx264 -preset ultrafast -tune zerolatency -b:v 2500k '
            . '-s %s -r 30 -pix_fmt yuv420p '
            . '-c:a aac -b:a 128k -ar 44100 -ac 2 '
            . '-f flv %s '
            . '> %s 2>&1 & echo $!',
            escapeshellarg($ffmpeg_bin),
            escapeshellarg($pipe_path),
            escapeshellarg($res),
            escapeshellarg($rtmp_dest),
            escapeshellarg($log_file)
        );
    }

    /* We launch ffmpeg in the background */
    $output = [];
    $retval = 0;
    exec($cmd, $output, $retval);

    $pid = isset($output[0]) ? (int)trim($output[0]) : 0;
    if ($pid <= 0) {
        mc1_producer_cleanup($target_id);
        $err_detail = '';
        if (file_exists($log_file)) {
            $err_detail = trim((string)file_get_contents($log_file));
            if (strlen($err_detail) > 500) $err_detail = substr($err_detail, -500);
        }
        mc1_api_respond(['error' => 'Failed to start ffmpeg relay', 'detail' => $err_detail], 500);
        return;
    }

    /* We write the PID file */
    file_put_contents($pid_file, (string)$pid);

    /* We update the DB to mark this target as active */
    try {
        mc1_db('mcaster1_encoder')
            ->prepare('UPDATE rtmp_targets SET is_active=1, last_connected_at=NOW(), error_message=NULL WHERE id=?')
            ->execute([$target_id]);
    } catch (Exception $e) {
        mc1_log(2, 'producer start_stream DB update failed', json_encode([
            'target_id' => $target_id, 'err' => $e->getMessage()
        ]));
    }

    mc1_log(4, 'producer video stream started', json_encode([
        'target_id' => $target_id, 'pid' => $pid, 'platform' => $target['platform'],
    ]));

    mc1_api_respond(['ok' => true, 'pid' => $pid, 'pipe' => $pipe_path]);
    return;
}

/* =========================================================================
 * action: stop_stream — close FIFO, kill ffmpeg relay
 * ========================================================================= */
if ($action === 'stop_stream') {
    if (!$is_admin) {
        mc1_api_respond(['error' => 'Admin permission required'], 403);
        return;
    }

    $target_id = (int)($body['target_id'] ?? 0);
    if ($target_id < 1) {
        mc1_api_respond(['error' => 'target_id required'], 400);
        return;
    }

    $status = mc1_producer_is_running($target_id);
    if ($status['running'] && $status['pid'] > 0) {
        /* We send SIGTERM first, then SIGKILL after a brief wait */
        $pid = $status['pid'];
        @posix_kill($pid, 15);
        $waited = 0;
        while ($waited < 2000 && file_exists('/proc/' . $pid)) {
            usleep(100000);
            $waited += 100;
        }
        if (file_exists('/proc/' . $pid)) {
            @posix_kill($pid, 9);
        }
    }

    /* We clean up all temp files */
    mc1_producer_cleanup($target_id);
    @unlink(mc1_producer_log_path($target_id));

    /* We update the DB */
    try {
        mc1_db('mcaster1_encoder')
            ->prepare('UPDATE rtmp_targets SET is_active=0 WHERE id=?')
            ->execute([$target_id]);
    } catch (Exception $e) {}

    mc1_log(4, 'producer video stream stopped', json_encode([
        'target_id' => $target_id
    ]));

    mc1_api_respond(['ok' => true, 'killed_pid' => $status['pid'] ?? 0]);
    return;
}

/* =========================================================================
 * action: stream_status — check relay status for one or all targets
 * ========================================================================= */
if ($action === 'stream_status') {
    $target_id = isset($body['target_id']) ? (int)$body['target_id'] : 0;

    try {
        $pdo = mc1_db('mcaster1_encoder');
        if ($target_id > 0) {
            $stmt = $pdo->prepare(
                'SELECT id, slot_id, platform, name, is_active, video_enabled, video_resolution,
                        last_connected_at, error_message
                 FROM rtmp_targets WHERE id = ?'
            );
            $stmt->execute([$target_id]);
        } else {
            $stmt = $pdo->query(
                'SELECT id, slot_id, platform, name, is_active, video_enabled, video_resolution,
                        last_connected_at, error_message
                 FROM rtmp_targets WHERE video_enabled = 1
                 ORDER BY slot_id, id'
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
        $status = mc1_producer_is_running($tid);

        /* We read the byte counter */
        $counter_file = mc1_producer_counter_path($tid);
        $bytes_streamed = file_exists($counter_file) ? (int)file_get_contents($counter_file) : 0;

        /* We sync DB if process died */
        if ((bool)$r['is_active'] && !$status['running']) {
            $log_file = mc1_producer_log_path($tid);
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
            'id'               => $tid,
            'slot_id'          => (int)$r['slot_id'],
            'platform'         => $r['platform'],
            'name'             => $r['name'],
            'video_enabled'    => (bool)$r['video_enabled'],
            'video_resolution' => $r['video_resolution'],
            'relay_running'    => $status['running'],
            'relay_pid'        => $status['pid'],
            'is_active'        => (bool)$r['is_active'],
            'bytes_streamed'   => $bytes_streamed,
            'last_connected_at'=> $r['last_connected_at'],
            'error_message'    => $r['error_message'],
        ];
    }
    mc1_api_respond(['ok' => true, 'targets' => $results]);
    return;
}

/* =========================================================================
 * action: start_recording — start ffmpeg writing to archive file from FIFO
 * ========================================================================= */
if ($action === 'start_recording') {
    if (!$is_admin) {
        mc1_api_respond(['error' => 'Admin permission required'], 403);
        return;
    }

    $format = (string)($body['format'] ?? 'webm');
    if (!in_array($format, ['webm', 'mp4', 'mkv'], true)) {
        $format = 'webm';
    }

    /* We create the archive directory if it does not exist */
    $archive_dir = '/var/www/mcaster1.com/audio/video_recordings';
    if (!is_dir($archive_dir)) {
        @mkdir($archive_dir, 0775, true);
    }

    /* We generate a unique filename */
    $filename = 'producer_' . date('Ymd_His') . '.' . $format;
    $filepath = $archive_dir . '/' . $filename;

    /* We use a unique rec ID based on timestamp */
    $rec_id   = (int)date('YmdHis');
    $pipe_path = mc1_producer_rec_pipe_path($rec_id);
    $pid_file  = mc1_producer_rec_pid_path($rec_id);
    $log_file  = '/tmp/mc1_video_rec_' . $rec_id . '.log';

    /* We clean up stale pipe */
    if (file_exists($pipe_path)) {
        @unlink($pipe_path);
    }

    /* We create the FIFO */
    exec('mkfifo ' . escapeshellarg($pipe_path), $_, $mkret);
    if ($mkret !== 0) {
        mc1_api_respond(['error' => 'Failed to create recording pipe'], 500);
        return;
    }

    /* We build the ffmpeg recording command */
    $ffmpeg_bin = '/usr/bin/ffmpeg';

    if ($format === 'mp4') {
        $cmd = sprintf(
            'nohup %s -y -i %s -c:v libx264 -preset ultrafast -c:a aac -b:a 128k %s > %s 2>&1 & echo $!',
            escapeshellarg($ffmpeg_bin),
            escapeshellarg($pipe_path),
            escapeshellarg($filepath),
            escapeshellarg($log_file)
        );
    } else {
        /* WebM or MKV — copy codec (already WebM from browser) */
        $cmd = sprintf(
            'nohup %s -y -i %s -c copy %s > %s 2>&1 & echo $!',
            escapeshellarg($ffmpeg_bin),
            escapeshellarg($pipe_path),
            escapeshellarg($filepath),
            escapeshellarg($log_file)
        );
    }

    $output = [];
    exec($cmd, $output, $retval);
    $pid = isset($output[0]) ? (int)trim($output[0]) : 0;

    if ($pid <= 0) {
        @unlink($pipe_path);
        mc1_api_respond(['error' => 'Failed to start recording'], 500);
        return;
    }

    file_put_contents($pid_file, (string)$pid);

    mc1_log(4, 'producer recording started', json_encode([
        'rec_id' => $rec_id, 'pid' => $pid, 'file' => $filepath
    ]));

    mc1_api_respond([
        'ok'       => true,
        'rec_id'   => $rec_id,
        'pid'      => $pid,
        'filename' => $filename,
        'pipe'     => $pipe_path
    ]);
    return;
}

/* =========================================================================
 * action: stop_recording — finalize the recording file
 * ========================================================================= */
if ($action === 'stop_recording') {
    if (!$is_admin) {
        mc1_api_respond(['error' => 'Admin permission required'], 403);
        return;
    }

    $rec_id = (int)($body['rec_id'] ?? 0);
    if ($rec_id < 1) {
        mc1_api_respond(['error' => 'rec_id required'], 400);
        return;
    }

    $pid_file  = mc1_producer_rec_pid_path($rec_id);
    $pipe_path = mc1_producer_rec_pipe_path($rec_id);

    if (file_exists($pid_file)) {
        $pid = (int)trim((string)file_get_contents($pid_file));
        if ($pid > 0 && file_exists('/proc/' . $pid)) {
            @posix_kill($pid, 15);
            $waited = 0;
            while ($waited < 2000 && file_exists('/proc/' . $pid)) {
                usleep(100000);
                $waited += 100;
            }
            if (file_exists('/proc/' . $pid)) {
                @posix_kill($pid, 9);
            }
        }
    }

    @unlink($pid_file);
    @unlink($pipe_path);

    mc1_log(4, 'producer recording stopped', json_encode(['rec_id' => $rec_id]));

    mc1_api_respond(['ok' => true]);
    return;
}

/* =========================================================================
 * action: list_recordings — list video recordings in archive directory
 * ========================================================================= */
if ($action === 'list_recordings') {
    $archive_dir = '/var/www/mcaster1.com/audio/video_recordings';
    $recordings = [];

    if (is_dir($archive_dir)) {
        $files = @scandir($archive_dir, SCANDIR_SORT_DESCENDING);
        if ($files) {
            foreach ($files as $f) {
                if ($f === '.' || $f === '..') continue;
                if (!preg_match('/\.(webm|mp4|mkv)$/i', $f)) continue;
                $full = $archive_dir . '/' . $f;
                $recordings[] = [
                    'filename'  => $f,
                    'size'      => filesize($full),
                    'modified'  => date('Y-m-d H:i:s', filemtime($full)),
                ];
            }
        }
    }

    mc1_api_respond(['ok' => true, 'recordings' => $recordings]);
    return;
}

/* =========================================================================
 * action: start_vodcast — create temp recording file, podcast_episode row
 * ========================================================================= */
if ($action === 'start_vodcast') {
    if (!$is_admin) {
        mc1_api_respond(['error' => 'Admin permission required'], 403);
        return;
    }

    $show_id = (int)($body['show_id'] ?? 0);
    $title   = trim((string)($body['title'] ?? ''));
    $format  = (string)($body['format'] ?? 'webm');

    if ($show_id < 1) {
        mc1_api_respond(['error' => 'show_id required'], 400);
        return;
    }
    if ($title === '') {
        mc1_api_respond(['error' => 'title required'], 400);
        return;
    }
    if (!in_array($format, ['webm', 'mp4'], true)) {
        $format = 'webm';
    }

    /* We generate a unique vodcast ID and filename */
    $vodcast_id = (int)date('YmdHis');
    $archive_dir = mc1_vodcast_archive_dir();

    /* We always record as WebM natively; MP4 is transcoded at stop time */
    $rec_ext   = 'webm';
    $filename  = 'vodcast_' . date('Ymd_His') . '.' . $rec_ext;
    $filepath  = $archive_dir . '/' . $filename;

    /* We write metadata file so chunk uploads can find the path */
    $meta = [
        'vodcast_id' => $vodcast_id,
        'show_id'    => $show_id,
        'title'      => $title,
        'format'     => $format,
        'filepath'   => $filepath,
        'filename'   => $filename,
        'started_at' => date('Y-m-d H:i:s'),
    ];
    file_put_contents(mc1_vodcast_meta_path($vodcast_id), json_encode($meta));

    /* We create an empty file to start appending chunks to */
    file_put_contents($filepath, '');

    /* We create the podcast_episode record in draft state */
    try {
        $pdo = mc1_db('mcaster1_media');

        /* We auto-compute episode number */
        $stmt = $pdo->prepare(
            'SELECT COALESCE(MAX(episode_number), 0) FROM podcast_episodes WHERE show_id = ?'
        );
        $stmt->execute([$show_id]);
        $ep_num = (int)$stmt->fetchColumn() + 1;

        $ins = $pdo->prepare(
            'INSERT INTO podcast_episodes
             (show_id, title, file_path, format, episode_number, is_published)
             VALUES (?, ?, ?, ?, ?, 0)'
        );
        $ins->execute([
            $show_id,
            $title,
            $filepath,
            $format,      /* We store the desired final format (mp4 or webm) */
            $ep_num,
        ]);
        $episode_id = (int)$pdo->lastInsertId();

        /* We store the episode_id in the meta file */
        $meta['episode_id'] = $episode_id;
        file_put_contents(mc1_vodcast_meta_path($vodcast_id), json_encode($meta));
    } catch (Exception $e) {
        mc1_log(2, 'vodcast start_vodcast DB error', json_encode([
            'err' => $e->getMessage(), 'show_id' => $show_id,
        ]));
        mc1_api_respond(['error' => 'Database error creating episode'], 500);
        return;
    }

    mc1_log(4, 'vodcast recording started', json_encode([
        'vodcast_id' => $vodcast_id, 'episode_id' => $episode_id,
        'show_id' => $show_id, 'format' => $format,
    ]));

    mc1_api_respond([
        'ok'         => true,
        'vodcast_id' => $vodcast_id,
        'episode_id' => $episode_id,
        'filename'   => $filename,
        'filepath'   => $filepath,
    ]);
    return;
}

/* =========================================================================
 * action: stop_vodcast — finalize recording file, get duration, optionally
 * transcode WebM to MP4, update episode record
 * ========================================================================= */
if ($action === 'stop_vodcast') {
    if (!$is_admin) {
        mc1_api_respond(['error' => 'Admin permission required'], 403);
        return;
    }

    $vodcast_id = (int)($body['vodcast_id'] ?? 0);
    if ($vodcast_id < 1) {
        mc1_api_respond(['error' => 'vodcast_id required'], 400);
        return;
    }

    $meta_path = mc1_vodcast_meta_path($vodcast_id);
    if (!file_exists($meta_path)) {
        mc1_api_respond(['error' => 'No active vodcast recording for id ' . $vodcast_id], 404);
        return;
    }

    $meta = json_decode((string)file_get_contents($meta_path), true);
    $filepath   = $meta['filepath'] ?? '';
    $format     = $meta['format'] ?? 'webm';
    $episode_id = (int)($meta['episode_id'] ?? 0);

    if (!file_exists($filepath)) {
        @unlink($meta_path);
        mc1_api_respond(['error' => 'Recording file not found'], 500);
        return;
    }

    $file_size = filesize($filepath);
    $final_path = $filepath;

    /* We get duration from the WebM file via ffprobe */
    $duration_sec = 0;
    if (function_exists('exec')) {
        $cmd = 'ffprobe -v quiet -show_entries format=duration -of csv=p=0 '
             . escapeshellarg($filepath) . ' 2>/dev/null';
        $lines = [];
        @exec($cmd, $lines);
        if (!empty($lines[0])) {
            $duration_sec = (int)round((float)$lines[0]);
        }
    }

    /* We transcode WebM to MP4 if requested */
    if ($format === 'mp4') {
        $mp4_path = preg_replace('/\.webm$/', '.mp4', $filepath);
        $transcode_cmd = sprintf(
            '/usr/bin/ffmpeg -y -i %s -c:v libx264 -preset fast -crf 23 '
            . '-c:a aac -b:a 128k -movflags +faststart %s 2>/dev/null',
            escapeshellarg($filepath),
            escapeshellarg($mp4_path)
        );
        $retval = 0;
        @exec($transcode_cmd, $_, $retval);

        if ($retval === 0 && file_exists($mp4_path) && filesize($mp4_path) > 0) {
            /* We remove the WebM source and use the MP4 */
            @unlink($filepath);
            $final_path = $mp4_path;
            $file_size = filesize($mp4_path);

            /* We re-probe MP4 duration */
            $lines = [];
            $cmd = 'ffprobe -v quiet -show_entries format=duration -of csv=p=0 '
                 . escapeshellarg($mp4_path) . ' 2>/dev/null';
            @exec($cmd, $lines);
            if (!empty($lines[0])) {
                $duration_sec = (int)round((float)$lines[0]);
            }
        } else {
            /* We fall back to WebM if transcode failed */
            $format = 'webm';
            mc1_log(2, 'vodcast MP4 transcode failed, keeping WebM', json_encode([
                'vodcast_id' => $vodcast_id, 'retval' => $retval,
            ]));
        }
    }

    /* We update the podcast_episode record with file size, duration, format, path */
    if ($episode_id > 0) {
        try {
            mc1_db('mcaster1_media')->prepare(
                'UPDATE podcast_episodes
                 SET file_path = ?, file_size_bytes = ?, duration_sec = ?, format = ?
                 WHERE id = ?'
            )->execute([$final_path, $file_size, $duration_sec, $format, $episode_id]);
        } catch (Exception $e) {
            mc1_log(2, 'vodcast stop_vodcast DB update error', json_encode([
                'err' => $e->getMessage(), 'episode_id' => $episode_id,
            ]));
        }
    }

    /* We clean up the meta file */
    @unlink($meta_path);

    mc1_log(4, 'vodcast recording stopped', json_encode([
        'vodcast_id' => $vodcast_id, 'episode_id' => $episode_id,
        'format' => $format, 'duration' => $duration_sec, 'size' => $file_size,
    ]));

    mc1_api_respond([
        'ok'           => true,
        'episode_id'   => $episode_id,
        'filename'     => basename($final_path),
        'file_path'    => $final_path,
        'file_size'    => $file_size,
        'duration_sec' => $duration_sec,
        'format'       => $format,
    ]);
    return;
}

/* =========================================================================
 * action: get_vodcast_thumbnail — extract a frame from recording via ffmpeg
 * ========================================================================= */
if ($action === 'get_vodcast_thumbnail') {
    if (!$is_admin) {
        mc1_api_respond(['error' => 'Admin permission required'], 403);
        return;
    }

    $episode_id = (int)($body['episode_id'] ?? 0);
    $timestamp  = trim((string)($body['timestamp'] ?? '00:00:30'));

    if ($episode_id < 1) {
        mc1_api_respond(['error' => 'episode_id required'], 400);
        return;
    }

    /* We validate timestamp format (HH:MM:SS or seconds) */
    if (!preg_match('/^(\d{1,2}:)?\d{1,2}:\d{2}$/', $timestamp) && !preg_match('/^\d+(\.\d+)?$/', $timestamp)) {
        $timestamp = '00:00:30';
    }

    /* We load the episode to get the file path */
    try {
        $ep = mc1_db('mcaster1_media')->prepare(
            'SELECT file_path, format FROM podcast_episodes WHERE id = ?'
        );
        $ep->execute([$episode_id]);
        $ep = $ep->fetch(\PDO::FETCH_ASSOC);
    } catch (Exception $e) {
        mc1_api_respond(['error' => 'Database error'], 500);
        return;
    }

    if (!$ep || empty($ep['file_path']) || !file_exists($ep['file_path'])) {
        mc1_api_respond(['error' => 'Episode file not found'], 404);
        return;
    }

    /* We only support video formats for thumbnail extraction */
    $video_formats = ['mp4', 'webm', 'mkv', 'avi', 'mov'];
    if (!in_array($ep['format'] ?? '', $video_formats, true)) {
        mc1_api_respond(['error' => 'Episode is not a video format'], 400);
        return;
    }

    /* We generate the thumbnail path */
    $archive_dir = mc1_vodcast_archive_dir();
    $thumb_filename = 'thumb_ep' . $episode_id . '_' . date('YmdHis') . '.jpg';
    $thumb_path = $archive_dir . '/' . $thumb_filename;

    /* We extract a frame via ffmpeg */
    $cmd = sprintf(
        '/usr/bin/ffmpeg -y -i %s -ss %s -vframes 1 -q:v 2 %s 2>/dev/null',
        escapeshellarg($ep['file_path']),
        escapeshellarg($timestamp),
        escapeshellarg($thumb_path)
    );
    $retval = 0;
    @exec($cmd, $_, $retval);

    if ($retval !== 0 || !file_exists($thumb_path) || filesize($thumb_path) === 0) {
        @unlink($thumb_path);
        mc1_api_respond(['error' => 'Thumbnail extraction failed. Try a different timestamp.'], 500);
        return;
    }

    /* We update the episode cover_art_path if they have that column,
     * or we just return the path for the caller to use */
    try {
        mc1_db('mcaster1_media')->prepare(
            'UPDATE podcast_episodes SET cover_art_path = ? WHERE id = ?'
        )->execute([$thumb_path, $episode_id]);
    } catch (Exception $e) {
        /* We do not fail if the column does not exist yet */
        mc1_log(5, 'vodcast thumbnail DB update skipped', json_encode([
            'err' => $e->getMessage(), 'episode_id' => $episode_id,
        ]));
    }

    mc1_log(4, 'vodcast thumbnail extracted', json_encode([
        'episode_id' => $episode_id, 'thumb' => $thumb_path,
    ]));

    mc1_api_respond([
        'ok'       => true,
        'path'     => $thumb_path,
        'filename' => $thumb_filename,
        'size'     => filesize($thumb_path),
    ]);
    return;
}

/* We return 400 for any unknown action */
mc1_api_respond(['error' => 'Unknown action: ' . $action], 400);
