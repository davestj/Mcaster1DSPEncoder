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
 * @version 1.8.1
 * @since   2026-03-27
 */

define('MC1_BOOT', true);
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

// ── Unknown action ────────────────────────────────────────────────────────

mc1_api_respond(['ok' => false, 'error' => 'Unknown action: ' . $action], 400);
