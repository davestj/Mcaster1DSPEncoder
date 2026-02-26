<?php
// upload.php — Batch file upload API for Mcaster1 Media Library
//
// File:    src/linux/web_ui/app/api/upload.php
// Author:  Dave St. John <davestj@gmail.com>
// Date:    2026-02-24
// Purpose: Handles batch audio file uploads from browser to server.
//
// Actions (POST):
//   list_dirs   — JSON body — enumerate uploadable directories within allowed roots
//   create_dir  — JSON body {parent, name} — create new subdirectory
//   upload      — multipart/form-data {action, target_dir, file} — save one file
//
// Security:
//   - Requires mc1_is_authed() (C++ session)
//   - target_dir must be realpath() within MC1_AUDIO_ROOT or extra library paths
//   - Only audio file extensions accepted
//   - Filenames sanitised before writing
//
// Note: No exit()/die() — uopz extension active.

define('MC1_BOOT', true);
require_once __DIR__ . '/../inc/auth.php';
require_once __DIR__ . '/../inc/mc1_config.php';
require_once __DIR__ . '/../inc/logger.php';
require_once __DIR__ . '/../inc/db.php';

header('Content-Type: application/json; charset=utf-8');

if (!mc1_is_authed()) {
    mc1_api_respond(['ok' => false, 'error' => 'Forbidden'], 403);
    return;
}

// ── Determine action source (multipart vs JSON) ───────────────────────────

$is_multipart = !empty($_FILES);

if ($is_multipart) {
    // upload action: action + target_dir come from $_POST
    $action = (string)($_POST['action'] ?? '');
    $req    = [];
} else {
    $raw    = file_get_contents('php://input');
    $req    = ($raw !== '') ? json_decode($raw, true) : [];
    $action = is_array($req) ? (string)($req['action'] ?? '') : '';
}

// ── Helpers ───────────────────────────────────────────────────────────────

// We return all allowed upload roots (MC1_AUDIO_ROOT + active extra paths).
function upload_allowed_roots(): array
{
    $roots = [rtrim(MC1_AUDIO_ROOT, '/')];
    try {
        $rows = mc1_db('mcaster1_media')
            ->query("SELECT path FROM media_library_paths WHERE is_active=1")
            ->fetchAll(PDO::FETCH_COLUMN);
        foreach ($rows as $p) {
            $p = rtrim($p, '/');
            if ($p && is_dir($p)) $roots[] = $p;
        }
    } catch (Exception $e) {}
    return array_unique($roots);
}

// We check that $path is within one of the allowed roots.
function upload_is_safe(string $path, array $roots): bool
{
    $real = realpath($path);
    if ($real === false) return false;
    foreach ($roots as $root) {
        $root_real = realpath($root);
        if ($root_real && (
            $real === $root_real ||
            strpos($real, $root_real . DIRECTORY_SEPARATOR) === 0
        )) {
            return true;
        }
    }
    return false;
}

// We recursively collect subdirectories up to $max_depth levels.
function upload_list_subdirs(string $base, int $depth = 0, int $max_depth = 4): array
{
    if ($depth > $max_depth) return [];
    $result  = [];
    $entries = @scandir($base);
    if (!$entries) return [];
    sort($entries);
    foreach ($entries as $entry) {
        if ($entry === '.' || $entry === '..') continue;
        if ($entry[0] === '.') continue;      // skip hidden
        $full = $base . '/' . $entry;
        if (!is_dir($full)) continue;
        $result[] = ['path' => $full, 'depth' => $depth, 'name' => $entry];
        $children = upload_list_subdirs($full, $depth + 1, $max_depth);
        foreach ($children as $c) $result[] = $c;
    }
    return $result;
}

// ── Action: list_dirs ─────────────────────────────────────────────────────

if ($action === 'list_dirs') {

    $roots = upload_allowed_roots();
    $dirs  = [];

    foreach ($roots as $root) {
        // The root itself is always a valid upload target
        $dirs[] = [
            'path'  => $root,
            'label' => basename($root) . ' [audio root]',
            'depth' => 0,
        ];
        foreach (upload_list_subdirs($root) as $d) {
            $dirs[] = [
                'path'  => $d['path'],
                'label' => str_repeat('  ', $d['depth'] + 1) . $d['name'],
                'depth' => $d['depth'] + 1,
            ];
        }
    }

    mc1_api_respond(['ok' => true, 'dirs' => $dirs]);

// ── Action: create_dir ────────────────────────────────────────────────────

} elseif ($action === 'create_dir') {

    $parent = trim($req['parent'] ?? '');
    $name   = trim($req['name']   ?? '');

    // Sanitise directory name: allow letters, digits, space, dash, underscore, dot
    $name = preg_replace('/[^a-zA-Z0-9 _\-.]/', '_', $name);
    $name = trim($name, '. ');

    if ($parent === '' || $name === '') {
        mc1_api_respond(['ok' => false, 'error' => 'parent and name are required'], 400);
        return;
    }

    $roots = upload_allowed_roots();
    if (!upload_is_safe($parent, $roots)) {
        mc1_api_respond(['ok' => false, 'error' => 'Parent directory not allowed'], 403);
        return;
    }

    $new_dir = rtrim(realpath($parent), '/') . '/' . $name;

    if (is_dir($new_dir)) {
        mc1_api_respond(['ok' => true, 'path' => $new_dir, 'created' => false,
                         'msg' => 'Directory already exists']);
        return;
    }

    if (@mkdir($new_dir, 0755, true)) {
        mc1_log(4, "Created upload directory: $new_dir", 'upload');
        mc1_api_respond(['ok' => true, 'path' => $new_dir, 'created' => true]);
    } else {
        mc1_api_respond(['ok' => false, 'error' => 'Could not create directory (check permissions)'], 500);
    }

// ── Action: upload ────────────────────────────────────────────────────────

} elseif ($action === 'upload') {

    if (!$is_multipart || !isset($_FILES['file'])) {
        mc1_api_respond(['ok' => false, 'error' => 'No file in request'], 400);
        return;
    }

    $target_dir = trim($_POST['target_dir'] ?? '');
    if ($target_dir === '') {
        // Default to audio root when no destination supplied
        $target_dir = rtrim(MC1_AUDIO_ROOT, '/');
    }

    $roots = upload_allowed_roots();

    // If the directory doesn't exist yet, try to auto-create it as long as
    // a real ancestor is within an allowed root (handles folder uploads with
    // subdirectory structure, and avoids requiring a pre-created dest dir).
    if (!is_dir($target_dir)) {
        $ancestor = rtrim($target_dir, '/');
        while ($ancestor !== '' && $ancestor !== '/' && !is_dir($ancestor)) {
            $ancestor = dirname($ancestor);
        }
        $real_ancestor = realpath($ancestor);
        if ($real_ancestor !== false && upload_is_safe($real_ancestor, $roots)) {
            @mkdir($target_dir, 0755, true);
            if (is_dir($target_dir)) {
                mc1_log(4, 'Auto-created upload subdir: ' . $target_dir, 'upload');
            }
        }
    }

    $real_target = realpath($target_dir);
    if ($real_target === false || !is_dir($real_target) || !upload_is_safe($real_target, $roots)) {
        mc1_api_respond(['ok' => false, 'error' => 'Target directory not allowed or does not exist'], 403);
        return;
    }

    $file     = $_FILES['file'];
    $err_code = (int)$file['error'];

    if ($err_code !== UPLOAD_ERR_OK) {
        $err_msgs = [
            UPLOAD_ERR_INI_SIZE   => 'File exceeds server upload_max_filesize (256 MB)',
            UPLOAD_ERR_FORM_SIZE  => 'File exceeds form MAX_FILE_SIZE',
            UPLOAD_ERR_PARTIAL    => 'File was only partially uploaded',
            UPLOAD_ERR_NO_FILE    => 'No file was uploaded',
            UPLOAD_ERR_NO_TMP_DIR => 'Missing temporary folder',
            UPLOAD_ERR_CANT_WRITE => 'Failed to write to disk',
            UPLOAD_ERR_EXTENSION  => 'Upload blocked by PHP extension',
        ];
        $msg = $err_msgs[$err_code] ?? "Upload error code $err_code";
        mc1_api_respond(['ok' => false, 'error' => $msg], 400);
        return;
    }

    // Validate extension (audio files only)
    $allowed_exts = ['mp3','ogg','flac','opus','aac','m4a','mp4','wav','wma','aiff','aif','ape','alac'];
    $orig_name = basename($file['name']);
    $ext       = strtolower(pathinfo($orig_name, PATHINFO_EXTENSION));

    if (!in_array($ext, $allowed_exts, true)) {
        mc1_api_respond(['ok' => false, 'error' => ".$ext files are not allowed (audio only)"], 400);
        return;
    }

    // Sanitise filename: keep letters, digits, spaces, dash, underscore, dot, parentheses
    $safe_name = preg_replace('/[^a-zA-Z0-9 _\-.()\[\]]/', '_', $orig_name);
    $safe_name = preg_replace('/_+/', '_', $safe_name);
    $safe_name = trim($safe_name, '_. ');
    if ($safe_name === '') $safe_name = 'upload_' . time() . '.' . $ext;

    // If destination already exists, append a counter before the extension
    $dest = $real_target . '/' . $safe_name;
    if (file_exists($dest)) {
        $base_name = pathinfo($safe_name, PATHINFO_FILENAME);
        $file_ext  = pathinfo($safe_name, PATHINFO_EXTENSION);
        $counter   = 1;
        do {
            $dest = $real_target . '/' . $base_name . '_' . $counter . '.' . $file_ext;
            $counter++;
        } while (file_exists($dest) && $counter < 9999);
    }

    if (!move_uploaded_file($file['tmp_name'], $dest)) {
        mc1_api_respond(['ok' => false, 'error' => 'Failed to move uploaded file to destination'], 500);
        return;
    }

    mc1_log(4, 'Uploaded: ' . $dest . ' (' . $file['size'] . ' bytes)', 'upload');
    mc1_api_respond([
        'ok'       => true,
        'saved_as' => basename($dest),
        'path'     => $dest,
        'size'     => (int)$file['size'],
    ]);

} else {

    mc1_api_respond(['ok' => false, 'error' => 'Unknown action. Valid: list_dirs, create_dir, upload'], 400);

}
