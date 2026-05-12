<?php
/**
 * app/api/daw.php — DAW project CRUD + export mixdown.
 *
 * We handle all DAW project operations: list, get, save, delete, and server-side
 * ffmpeg mixdown export. Project state is stored as JSON in the daw_projects table.
 *
 * No exit()/die() — uopz active. JSON responses only.
 * Auth gate on every action via mc1_is_authed().
 *
 * @author  Dave St. John <davestj@gmail.com>
 * @version 2.0.1
 * @since   2026-03-27
 */

define('MC1_BOOT', true);
$API_VERSION = '2.0.1';
require_once __DIR__ . '/../inc/auth.php';
require_once __DIR__ . '/../inc/db.php';
require_once __DIR__ . '/../inc/mc1_config.php';
require_once __DIR__ . '/../inc/logger.php';
require_once __DIR__ . '/../inc/user_auth.php';

header('Content-Type: application/json; charset=utf-8');

if (!mc1_is_authed()) {
    mc1_api_respond(['ok' => false, 'error' => 'Forbidden'], 403);
    return;
}

$user = mc1_current_user();
$userId = $user ? (int)($user['id'] ?? 0) : 0;
if ($userId <= 0) {
    mc1_api_respond(['ok' => false, 'error' => 'User not found'], 401);
    return;
}

$raw    = file_get_contents('php://input');
$req    = ($raw !== '') ? json_decode($raw, true) : [];
if (!is_array($req)) $req = [];
$action = (string)($req['action'] ?? '');

// ── Ensure daw_projects table exists ──────────────────────────────────────
try {
    mc1_db('mcaster1_media')->exec("
        CREATE TABLE IF NOT EXISTS daw_projects (
            id INT AUTO_INCREMENT PRIMARY KEY,
            user_id INT NOT NULL,
            project_name VARCHAR(255) NOT NULL,
            bpm FLOAT DEFAULT 120.0,
            time_signature VARCHAR(8) DEFAULT '4/4',
            project_json JSON NOT NULL COMMENT 'Full project state: tracks, clips, settings',
            duration_sec FLOAT DEFAULT 0,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
            INDEX idx_user (user_id)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
    ");
} catch (Exception $e) {
    // Table likely already exists, ignore
}

// ── list_projects ─────────────────────────────────────────────────────────

if ($action === 'list_projects') {
    try {
        $st = mc1_db('mcaster1_media')->prepare(
            'SELECT id, project_name, bpm, time_signature, duration_sec, created_at, updated_at
             FROM daw_projects WHERE user_id = ? ORDER BY updated_at DESC'
        );
        $st->execute([$userId]);
        $rows = $st->fetchAll();
        mc1_api_respond(['ok' => true, 'projects' => $rows]);
    } catch (Exception $e) {
        mc1_api_respond(['ok' => false, 'error' => $e->getMessage()], 500);
    }
    return;
}

// ── get_project ───────────────────────────────────────────────────────────

if ($action === 'get_project') {
    $id = (int)($req['id'] ?? 0);
    if ($id <= 0) { mc1_api_respond(['ok' => false, 'error' => 'id required'], 400); return; }
    try {
        $st = mc1_db('mcaster1_media')->prepare(
            'SELECT * FROM daw_projects WHERE id = ? AND user_id = ?'
        );
        $st->execute([$id, $userId]);
        $row = $st->fetch();
        if (!$row) { mc1_api_respond(['ok' => false, 'error' => 'Project not found'], 404); return; }
        // Decode JSON for the client
        if (is_string($row['project_json'])) {
            $row['project_json'] = json_decode($row['project_json'], true);
        }
        mc1_api_respond(['ok' => true, 'project' => $row]);
    } catch (Exception $e) {
        mc1_api_respond(['ok' => false, 'error' => $e->getMessage()], 500);
    }
    return;
}

// ── save_project ──────────────────────────────────────────────────────────

if ($action === 'save_project') {
    $id          = !empty($req['id']) ? (int)$req['id'] : null;
    $name        = trim((string)($req['project_name'] ?? 'Untitled'));
    $bpm         = (float)($req['bpm'] ?? 120.0);
    $timeSig     = (string)($req['time_signature'] ?? '4/4');
    $projectJson = $req['project_json'] ?? [];
    $durationSec = (float)($req['duration_sec'] ?? 0);

    if (empty($name)) { mc1_api_respond(['ok' => false, 'error' => 'project_name required'], 400); return; }

    $jsonStr = is_string($projectJson) ? $projectJson : json_encode($projectJson);

    try {
        $db = mc1_db('mcaster1_media');
        if ($id) {
            // Update existing — verify ownership
            $st = $db->prepare('SELECT id FROM daw_projects WHERE id = ? AND user_id = ?');
            $st->execute([$id, $userId]);
            if (!$st->fetch()) {
                mc1_api_respond(['ok' => false, 'error' => 'Project not found or not yours'], 404);
                return;
            }
            $st = $db->prepare(
                'UPDATE daw_projects SET project_name=?, bpm=?, time_signature=?, project_json=?, duration_sec=?
                 WHERE id=? AND user_id=?'
            );
            $st->execute([$name, $bpm, $timeSig, $jsonStr, $durationSec, $id, $userId]);
            mc1_api_respond(['ok' => true, 'id' => $id, 'updated' => true]);
        } else {
            // Insert new
            $st = $db->prepare(
                'INSERT INTO daw_projects (user_id, project_name, bpm, time_signature, project_json, duration_sec)
                 VALUES (?, ?, ?, ?, ?, ?)'
            );
            $st->execute([$userId, $name, $bpm, $timeSig, $jsonStr, $durationSec]);
            $newId = (int)$db->lastInsertId();
            mc1_api_respond(['ok' => true, 'id' => $newId, 'created' => true]);
        }
    } catch (Exception $e) {
        mc1_api_respond(['ok' => false, 'error' => $e->getMessage()], 500);
    }
    return;
}

// ── delete_project ────────────────────────────────────────────────────────

if ($action === 'delete_project') {
    $id = (int)($req['id'] ?? 0);
    if ($id <= 0) { mc1_api_respond(['ok' => false, 'error' => 'id required'], 400); return; }
    try {
        $st = mc1_db('mcaster1_media')->prepare(
            'DELETE FROM daw_projects WHERE id = ? AND user_id = ?'
        );
        $st->execute([$id, $userId]);
        mc1_api_respond(['ok' => true, 'deleted' => $st->rowCount() > 0]);
    } catch (Exception $e) {
        mc1_api_respond(['ok' => false, 'error' => $e->getMessage()], 500);
    }
    return;
}

// ── export_mixdown ────────────────────────────────────────────────────────

if ($action === 'export_mixdown') {
    $projectId  = (int)($req['project_id'] ?? 0);
    $format     = (string)($req['format'] ?? 'mp3');
    $bitrate    = (string)($req['bitrate'] ?? '192k');
    $quality    = (string)($req['quality'] ?? '5');
    $bitDepth   = (string)($req['bit_depth'] ?? '16');
    $stemExport = !empty($req['stem_export']);
    $outputName = preg_replace('/[^a-zA-Z0-9_\-]/', '_', trim((string)($req['output_name'] ?? 'mixdown')));

    if ($projectId <= 0) { mc1_api_respond(['ok' => false, 'error' => 'project_id required'], 400); return; }

    // Validate format
    $validFormats = ['mp3', 'wav', 'flac', 'ogg', 'aac', 'opus'];
    if (!in_array($format, $validFormats)) {
        mc1_api_respond(['ok' => false, 'error' => 'Invalid format. Use: ' . implode(', ', $validFormats)], 400);
        return;
    }

    // Load project
    try {
        $st = mc1_db('mcaster1_media')->prepare('SELECT * FROM daw_projects WHERE id = ? AND user_id = ?');
        $st->execute([$projectId, $userId]);
        $project = $st->fetch();
        if (!$project) { mc1_api_respond(['ok' => false, 'error' => 'Project not found'], 404); return; }
    } catch (Exception $e) {
        mc1_api_respond(['ok' => false, 'error' => $e->getMessage()], 500);
        return;
    }

    $json = is_string($project['project_json']) ? json_decode($project['project_json'], true) : $project['project_json'];
    if (!$json || !isset($json['tracks'])) {
        mc1_api_respond(['ok' => false, 'error' => 'Project has no tracks'], 400);
        return;
    }

    // Collect audio files per track (for both mixdown and stem export)
    $trackFiles = []; // [ ['name'=>..., 'volume'=>..., 'files'=>[...]] ]
    foreach ($json['tracks'] as $track) {
        $vol = (float)($track['volume'] ?? 1.0);
        $trackName = preg_replace('/[^a-zA-Z0-9_\-]/', '_', trim((string)($track['name'] ?? 'Track')));
        $files = [];
        foreach ($track['clips'] ?? [] as $clip) {
            $clipName = $clip['name'] ?? '';
            $st2 = mc1_db('mcaster1_media')->prepare(
                'SELECT file_path FROM tracks WHERE title LIKE ? OR file_path LIKE ? LIMIT 1'
            );
            $search = '%' . $clipName . '%';
            $st2->execute([$search, $search]);
            $trackRow = $st2->fetch();
            if ($trackRow && file_exists($trackRow['file_path'])) {
                $files[] = $trackRow['file_path'];
            }
        }
        if (count($files) > 0) {
            $trackFiles[] = ['name' => $trackName, 'volume' => $vol, 'files' => $files];
        }
    }

    if (count($trackFiles) === 0) {
        mc1_api_respond(['ok' => false, 'error' => 'No audio files found in project clips. Export requires clips that match tracks in the media library.'], 400);
        return;
    }

    // Build codec flags based on format
    $codecFlags = _dawCodecFlags($format, $bitrate, $quality, $bitDepth);
    $ext = _dawExtension($format);

    $exportDir = '/tmp/mc1_daw_exports';
    if (!is_dir($exportDir)) mkdir($exportDir, 0755, true);

    // ── Stem export: one file per track ──
    if ($stemExport) {
        $stemDir = $exportDir . '/' . $outputName . '_stems_' . date('Ymd_His');
        if (!is_dir($stemDir)) mkdir($stemDir, 0755, true);
        $stemCount = 0;

        foreach ($trackFiles as $ti => $tf) {
            $stemFile = $stemDir . '/' . $tf['name'] . '_' . ($ti + 1) . '.' . $ext;
            $inputs = '';
            $filters = '';
            $labels = [];
            for ($fi = 0; $fi < count($tf['files']); $fi++) {
                $inputs .= ' -i ' . escapeshellarg($tf['files'][$fi]);
                $vol = round($tf['volume'], 2);
                $label = 'a' . $fi;
                $filters .= '[' . $fi . ':a]volume=' . $vol . '[' . $label . '];';
                $labels[] = '[' . $label . ']';
            }
            if (count($tf['files']) > 1) {
                $filters .= implode('', $labels) . 'amix=inputs=' . count($tf['files']);
            } else {
                // Single file: just apply volume filter
                $filters = rtrim($filters, ';');
                $filters .= '';
            }

            if (count($tf['files']) === 1) {
                $cmd = 'ffmpeg -y' . $inputs . ' -filter_complex ' . escapeshellarg('[0:a]volume=' . round($tf['volume'], 2))
                     . ' ' . $codecFlags . ' ' . escapeshellarg($stemFile) . ' 2>&1';
            } else {
                $cmd = 'ffmpeg -y' . $inputs . ' -filter_complex ' . escapeshellarg($filters)
                     . ' ' . $codecFlags . ' ' . escapeshellarg($stemFile) . ' 2>&1';
            }

            mc1_log(4, 'DAW stem export: ' . $cmd);
            $output = [];
            $ret = 0;
            exec($cmd, $output, $ret);
            if ($ret === 0 && file_exists($stemFile)) $stemCount++;
        }

        // Create zip of stems
        $zipFile = $exportDir . '/' . $outputName . '_stems_' . date('Ymd_His') . '.zip';
        $zipCmd = 'cd ' . escapeshellarg($stemDir) . ' && zip -j ' . escapeshellarg($zipFile) . ' *.' . $ext . ' 2>&1';
        exec($zipCmd, $zipOutput, $zipRet);

        if ($zipRet !== 0 || !file_exists($zipFile)) {
            mc1_api_respond(['ok' => false, 'error' => 'Failed to create stem zip'], 500);
            return;
        }

        $downloadUrl = '/app/api/audio.php?path=' . urlencode($zipFile);
        mc1_api_respond(['ok' => true, 'download_url' => $downloadUrl, 'file' => basename($zipFile), 'stem_count' => $stemCount]);
        return;
    }

    // ── Standard mixdown: all tracks into one file ──
    $outFile = $exportDir . '/' . $outputName . '_' . date('Ymd_His') . '.' . $ext;
    $allFiles = [];
    $allVolumes = [];
    foreach ($trackFiles as $tf) {
        foreach ($tf['files'] as $f) {
            $allFiles[] = $f;
            $allVolumes[] = $tf['volume'];
        }
    }

    $inputs = '';
    $filters = '';
    $labels = [];
    for ($i = 0; $i < count($allFiles); $i++) {
        $inputs .= ' -i ' . escapeshellarg($allFiles[$i]);
        $vol = round($allVolumes[$i], 2);
        $label = 'a' . $i;
        $filters .= '[' . $i . ':a]volume=' . $vol . '[' . $label . '];';
        $labels[] = '[' . $label . ']';
    }
    $filters .= implode('', $labels) . 'amix=inputs=' . count($allFiles);

    $cmd = 'ffmpeg -y' . $inputs . ' -filter_complex ' . escapeshellarg($filters)
         . ' ' . $codecFlags . ' ' . escapeshellarg($outFile) . ' 2>&1';

    mc1_log(4, 'DAW export command: ' . $cmd);

    $output = [];
    $ret = 0;
    exec($cmd, $output, $ret);

    if ($ret !== 0 || !file_exists($outFile)) {
        mc1_log(2, 'DAW export failed: ret=' . $ret . ' output=' . implode("\n", $output));
        mc1_api_respond(['ok' => false, 'error' => 'ffmpeg export failed', 'detail' => implode("\n", array_slice($output, -5))], 500);
        return;
    }

    $downloadUrl = '/app/api/audio.php?path=' . urlencode($outFile);
    mc1_api_respond(['ok' => true, 'download_url' => $downloadUrl, 'file' => basename($outFile)]);
    return;
}

// ── Helper: build ffmpeg codec flags per format ──────────────────────────

function _dawCodecFlags(string $format, string $bitrate, string $quality, string $bitDepth): string {
    switch ($format) {
        case 'mp3':
            return '-c:a libmp3lame -b:a ' . escapeshellarg($bitrate);
        case 'wav':
            $codec = ($bitDepth === '24') ? 'pcm_s24le' : 'pcm_s16le';
            return '-c:a ' . $codec;
        case 'flac':
            $level = max(0, min(8, (int)$quality));
            return '-c:a flac -compression_level ' . $level;
        case 'ogg':
            // libvorbis: quality 0-10 maps to -q:a
            $q = max(0, min(10, (int)$quality));
            return '-c:a libvorbis -q:a ' . $q;
        case 'aac':
            return '-c:a aac -b:a ' . escapeshellarg($bitrate);
        case 'opus':
            return '-c:a libopus -b:a ' . escapeshellarg($bitrate);
        default:
            return '-c:a libmp3lame -b:a 192k';
    }
}

function _dawExtension(string $format): string {
    $map = [
        'mp3'  => 'mp3',
        'wav'  => 'wav',
        'flac' => 'flac',
        'ogg'  => 'ogg',
        'aac'  => 'm4a',
        'opus' => 'opus',
    ];
    return $map[$format] ?? $format;
}

// ── denoise_track (server-side via ffmpeg afftdn) ────────────────────────

if ($action === 'denoise_track') {
    $inputPath  = (string)($req['input_path'] ?? '');
    $noiseLevel = (string)($req['noise_level'] ?? '-25');

    if (empty($inputPath) || !file_exists($inputPath)) {
        mc1_api_respond(['ok' => false, 'error' => 'Input file not found'], 400);
        return;
    }

    $exportDir = '/tmp/mc1_daw_exports';
    if (!is_dir($exportDir)) mkdir($exportDir, 0755, true);
    $outFile = $exportDir . '/denoised_' . date('Ymd_His') . '_' . basename($inputPath);

    // Use ffmpeg afftdn (adaptive FFT denoising)
    $nf = max(-80, min(0, (int)$noiseLevel));
    $cmd = 'ffmpeg -y -i ' . escapeshellarg($inputPath)
         . ' -af ' . escapeshellarg('afftdn=nf=' . $nf)
         . ' ' . escapeshellarg($outFile) . ' 2>&1';

    mc1_log(4, 'DAW denoise: ' . $cmd);
    $output = [];
    $ret = 0;
    exec($cmd, $output, $ret);

    if ($ret !== 0 || !file_exists($outFile)) {
        mc1_log(2, 'DAW denoise failed: ret=' . $ret . ' output=' . implode("\n", $output));
        mc1_api_respond(['ok' => false, 'error' => 'ffmpeg denoise failed', 'detail' => implode("\n", array_slice($output, -5))], 500);
        return;
    }

    $downloadUrl = '/app/api/audio.php?path=' . urlencode($outFile);
    mc1_api_respond(['ok' => true, 'download_url' => $downloadUrl, 'file' => basename($outFile)]);
    return;
}

// ── time_stretch (server-side via ffmpeg atempo) ─────────────────────────

if ($action === 'time_stretch') {
    $inputPath = (string)($req['input_path'] ?? '');
    $factor    = (float)($req['factor'] ?? 1.0);

    if (empty($inputPath) || !file_exists($inputPath)) {
        mc1_api_respond(['ok' => false, 'error' => 'Input file not found'], 400);
        return;
    }
    if ($factor <= 0 || $factor > 4.0) {
        mc1_api_respond(['ok' => false, 'error' => 'Factor must be between 0.25 and 4.0'], 400);
        return;
    }

    $exportDir = '/tmp/mc1_daw_exports';
    if (!is_dir($exportDir)) mkdir($exportDir, 0755, true);
    $outFile = $exportDir . '/stretched_' . date('Ymd_His') . '_' . basename($inputPath);

    // ffmpeg atempo is limited to 0.5-2.0; chain multiple for wider range
    // Speed = 1/factor (stretch factor 2.0 = half speed = atempo 0.5)
    $speed = 1.0 / $factor;
    $atempoFilters = [];
    $remaining = $speed;
    while ($remaining < 0.5 || $remaining > 2.0) {
        if ($remaining < 0.5) {
            $atempoFilters[] = 'atempo=0.5';
            $remaining /= 0.5;
        } elseif ($remaining > 2.0) {
            $atempoFilters[] = 'atempo=2.0';
            $remaining /= 2.0;
        }
    }
    $atempoFilters[] = 'atempo=' . round($remaining, 6);
    $filterStr = implode(',', $atempoFilters);

    $cmd = 'ffmpeg -y -i ' . escapeshellarg($inputPath)
         . ' -af ' . escapeshellarg($filterStr)
         . ' ' . escapeshellarg($outFile) . ' 2>&1';

    mc1_log(4, 'DAW time stretch: ' . $cmd);
    $output = [];
    $ret = 0;
    exec($cmd, $output, $ret);

    if ($ret !== 0 || !file_exists($outFile)) {
        mc1_log(2, 'DAW time stretch failed: ret=' . $ret . ' output=' . implode("\n", $output));
        mc1_api_respond(['ok' => false, 'error' => 'ffmpeg time stretch failed', 'detail' => implode("\n", array_slice($output, -5))], 500);
        return;
    }

    $downloadUrl = '/app/api/audio.php?path=' . urlencode($outFile);
    mc1_api_respond(['ok' => true, 'download_url' => $downloadUrl, 'file' => basename($outFile)]);
    return;
}

// ── pitch_shift (server-side via ffmpeg asetrate + atempo or rubberband) ─

if ($action === 'pitch_shift') {
    $inputPath  = (string)($req['input_path'] ?? '');
    $semitones  = (int)($req['semitones'] ?? 0);

    if (empty($inputPath) || !file_exists($inputPath)) {
        mc1_api_respond(['ok' => false, 'error' => 'Input file not found'], 400);
        return;
    }
    if ($semitones < -24 || $semitones > 24) {
        mc1_api_respond(['ok' => false, 'error' => 'Semitones must be between -24 and +24'], 400);
        return;
    }

    $exportDir = '/tmp/mc1_daw_exports';
    if (!is_dir($exportDir)) mkdir($exportDir, 0755, true);
    $outFile = $exportDir . '/pitched_' . date('Ymd_His') . '_' . basename($inputPath);

    // Check if rubberband is available
    $hasRubberband = false;
    exec('which rubberband 2>/dev/null', $rbOut, $rbRet);
    if ($rbRet === 0) $hasRubberband = true;

    if ($hasRubberband) {
        // Use rubberband for high-quality pitch shift
        $cmd = 'rubberband -p ' . $semitones . ' ' . escapeshellarg($inputPath) . ' ' . escapeshellarg($outFile) . ' 2>&1';
    } else {
        // Fallback: ffmpeg asetrate + atempo combo
        // asetrate changes sample rate (changes pitch + speed)
        // Then atempo corrects the speed back to original
        $pitchRatio = pow(2, $semitones / 12);
        // Get original sample rate
        $probeCmd = 'ffprobe -v quiet -print_format json -show_streams ' . escapeshellarg($inputPath);
        $probeOut = [];
        exec($probeCmd, $probeOut);
        $probeJson = json_decode(implode('', $probeOut), true);
        $origSr = 44100;
        if (isset($probeJson['streams'][0]['sample_rate'])) {
            $origSr = (int)$probeJson['streams'][0]['sample_rate'];
        }
        $newSr = round($origSr * $pitchRatio);
        $atempoFactor = $pitchRatio; // atempo to restore original duration

        // Build atempo chain for values outside 0.5-2.0
        $atempoFilters = [];
        $remaining = $atempoFactor;
        while ($remaining < 0.5 || $remaining > 2.0) {
            if ($remaining < 0.5) {
                $atempoFilters[] = 'atempo=0.5';
                $remaining /= 0.5;
            } elseif ($remaining > 2.0) {
                $atempoFilters[] = 'atempo=2.0';
                $remaining /= 2.0;
            }
        }
        $atempoFilters[] = 'atempo=' . round($remaining, 6);

        $filterStr = 'asetrate=' . $newSr . ',' . implode(',', $atempoFilters) . ',aresample=' . $origSr;
        $cmd = 'ffmpeg -y -i ' . escapeshellarg($inputPath)
             . ' -af ' . escapeshellarg($filterStr)
             . ' ' . escapeshellarg($outFile) . ' 2>&1';
    }

    mc1_log(4, 'DAW pitch shift: ' . $cmd);
    $output = [];
    $ret = 0;
    exec($cmd, $output, $ret);

    if ($ret !== 0 || !file_exists($outFile)) {
        mc1_log(2, 'DAW pitch shift failed: ret=' . $ret . ' output=' . implode("\n", $output));
        mc1_api_respond(['ok' => false, 'error' => 'Pitch shift failed', 'detail' => implode("\n", array_slice($output, -5))], 500);
        return;
    }

    $downloadUrl = '/app/api/audio.php?path=' . urlencode($outFile);
    mc1_api_respond(['ok' => true, 'download_url' => $downloadUrl, 'file' => basename($outFile)]);
    return;
}

// ── freeze_track (server-side: render track via ffmpeg) ──────────────────

if ($action === 'freeze_track') {
    $projectId = (int)($req['project_id'] ?? 0);
    $trackIdx  = (int)($req['track_index'] ?? -1);

    if ($projectId <= 0) { mc1_api_respond(['ok' => false, 'error' => 'project_id required'], 400); return; }
    if ($trackIdx < 0)   { mc1_api_respond(['ok' => false, 'error' => 'track_index required'], 400); return; }

    // Load project
    try {
        $st = mc1_db('mcaster1_media')->prepare('SELECT * FROM daw_projects WHERE id = ? AND user_id = ?');
        $st->execute([$projectId, $userId]);
        $project = $st->fetch();
        if (!$project) { mc1_api_respond(['ok' => false, 'error' => 'Project not found'], 404); return; }
    } catch (Exception $e) {
        mc1_api_respond(['ok' => false, 'error' => $e->getMessage()], 500);
        return;
    }

    $json = is_string($project['project_json']) ? json_decode($project['project_json'], true) : $project['project_json'];
    if (!$json || !isset($json['tracks'][$trackIdx])) {
        mc1_api_respond(['ok' => false, 'error' => 'Track not found in project'], 400);
        return;
    }

    $track = $json['tracks'][$trackIdx];
    $vol = (float)($track['volume'] ?? 1.0);
    $trackName = preg_replace('/[^a-zA-Z0-9_\-]/', '_', trim((string)($track['name'] ?? 'Track')));

    // Collect audio files from clips
    $files = [];
    foreach ($track['clips'] ?? [] as $clip) {
        $clipName = $clip['name'] ?? '';
        $st2 = mc1_db('mcaster1_media')->prepare(
            'SELECT file_path FROM tracks WHERE title LIKE ? OR file_path LIKE ? LIMIT 1'
        );
        $search = '%' . $clipName . '%';
        $st2->execute([$search, $search]);
        $trackRow = $st2->fetch();
        if ($trackRow && file_exists($trackRow['file_path'])) {
            $files[] = $trackRow['file_path'];
        }
    }

    if (count($files) === 0) {
        mc1_api_respond(['ok' => false, 'error' => 'No audio files found for track clips'], 400);
        return;
    }

    $exportDir = '/tmp/mc1_daw_exports';
    if (!is_dir($exportDir)) mkdir($exportDir, 0755, true);
    $outFile = $exportDir . '/frozen_' . $trackName . '_' . date('Ymd_His') . '.wav';

    // Build ffmpeg command to mix all clips with volume
    $inputs = '';
    $filters = '';
    $labels = [];
    for ($fi = 0; $fi < count($files); $fi++) {
        $inputs .= ' -i ' . escapeshellarg($files[$fi]);
        $v = round($vol, 2);
        $label = 'a' . $fi;
        $filters .= '[' . $fi . ':a]volume=' . $v . '[' . $label . '];';
        $labels[] = '[' . $label . ']';
    }
    if (count($files) > 1) {
        $filters .= implode('', $labels) . 'amix=inputs=' . count($files);
    } else {
        $filters = '[0:a]volume=' . round($vol, 2);
    }

    $cmd = 'ffmpeg -y' . $inputs . ' -filter_complex ' . escapeshellarg($filters)
         . ' -c:a pcm_s16le ' . escapeshellarg($outFile) . ' 2>&1';

    mc1_log(4, 'DAW freeze track: ' . $cmd);
    $output = [];
    $ret = 0;
    exec($cmd, $output, $ret);

    if ($ret !== 0 || !file_exists($outFile)) {
        mc1_log(2, 'DAW freeze failed: ret=' . $ret . ' output=' . implode("\n", $output));
        mc1_api_respond(['ok' => false, 'error' => 'ffmpeg freeze failed', 'detail' => implode("\n", array_slice($output, -5))], 500);
        return;
    }

    $downloadUrl = '/app/api/audio.php?path=' . urlencode($outFile);
    mc1_api_respond(['ok' => true, 'download_url' => $downloadUrl, 'file' => basename($outFile)]);
    return;
}

// ── Unknown action ────────────────────────────────────────────────────────

mc1_api_respond(['ok' => false, 'error' => 'Unknown action: ' . $action], 400);
