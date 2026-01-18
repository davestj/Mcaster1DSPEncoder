<?php
// api/tracks.php — Media library tracks API (POST-only, JSON responses)
// Note: uopz extension is active on this server — no exit() calls used.
//
// Actions: list, search, add, delete, scan

define('MC1_BOOT', true);
require_once __DIR__ . '/../inc/auth.php';
require_once __DIR__ . '/../inc/db.php';

header('Content-Type: application/json; charset=utf-8');

$authed  = mc1_is_authed();
$is_post = ($_SERVER['REQUEST_METHOD'] === 'POST');
$raw     = $is_post ? file_get_contents('php://input') : '';
$req     = is_string($raw) && $raw !== '' ? json_decode($raw, true) : null;
$action  = is_array($req) ? (string)($req['action'] ?? '') : '';

// ── Helpers ────────────────────────────────────────────────────────────────

function mc1_parse_filename(string $path): array
{
    $base = pathinfo($path, PATHINFO_FILENAME);
    if (strpos($base, ' - ') !== false) {
        [$artist, $title] = explode(' - ', $base, 2);
        return ['title' => trim($title), 'artist' => trim($artist)];
    }
    return ['title' => trim($base), 'artist' => ''];
}

function mc1_is_audio(string $path): bool
{
    $ext = strtolower(pathinfo($path, PATHINFO_EXTENSION));
    return in_array($ext, ['mp3', 'ogg', 'flac', 'opus', 'aac', 'm4a', 'wav'], true);
}

// ── Dispatch ───────────────────────────────────────────────────────────────

if (!$authed) {

    http_response_code(403);
    echo json_encode(['ok' => false, 'error' => 'Forbidden']);

} elseif (!$is_post) {

    http_response_code(405);
    echo json_encode(['ok' => false, 'error' => 'POST required']);

} elseif (!is_array($req)) {

    http_response_code(400);
    echo json_encode(['ok' => false, 'error' => 'Invalid JSON body']);

} elseif ($action === 'list' || $action === 'search') {

    $page   = max(1, (int)($req['page']  ?? 1));
    $limit  = min(200, max(1, (int)($req['limit'] ?? 50)));
    $search = trim($req['q'] ?? '');
    $offset = ($page - 1) * $limit;

    try {
        $db     = mc1_db('mcaster1_media');
        $where  = '';
        $params = [];
        if ($search !== '') {
            $like   = '%' . $search . '%';
            $where  = 'WHERE title LIKE ? OR artist LIKE ? OR album LIKE ?';
            $params = [$like, $like, $like];
        }

        $cs = $db->prepare("SELECT COUNT(*) FROM tracks $where");
        $cs->execute($params);
        $total = (int)$cs->fetchColumn();
        $pages = max(1, (int)ceil($total / $limit));

        $ts = $db->prepare(
            "SELECT id, file_path, title, artist, album, genre, year,
                    duration_ms, bitrate_kbps, play_count, is_missing
             FROM tracks $where
             ORDER BY artist, title
             LIMIT ? OFFSET ?"
        );
        $ts->execute(array_merge($params, [$limit, $offset]));

        echo json_encode([
            'ok'    => true,
            'total' => $total,
            'pages' => $pages,
            'page'  => $page,
            'data'  => $ts->fetchAll(),
        ]);
    } catch (Exception $e) {
        http_response_code(500);
        echo json_encode(['ok' => false, 'error' => $e->getMessage()]);
    }

} elseif ($action === 'add') {

    $file_path = trim($req['file_path'] ?? '');

    if ($file_path === '' || !file_exists($file_path)) {
        http_response_code(400);
        echo json_encode(['ok' => false, 'error' => 'file_path required and must exist on disk']);
    } elseif (!mc1_is_audio($file_path)) {
        http_response_code(400);
        echo json_encode(['ok' => false, 'error' => 'Not a supported audio file']);
    } else {
        $meta   = mc1_parse_filename($file_path);
        $title  = $req['title']  ?? $meta['title'];
        $artist = $req['artist'] ?? $meta['artist'];
        $album  = $req['album']  ?? '';
        $genre  = $req['genre']  ?? '';
        $year   = isset($req['year']) ? (int)$req['year'] : null;
        $size   = @filesize($file_path) ?: null;

        try {
            $db = mc1_db('mcaster1_media');
            $st = $db->prepare(
                "INSERT INTO tracks
                   (file_path, title, artist, album, genre, year,
                    file_size_bytes, date_added)
                 VALUES (?,?,?,?,?,?,?,NOW())
                 ON DUPLICATE KEY UPDATE
                   title=VALUES(title), artist=VALUES(artist),
                   album=VALUES(album),  genre=VALUES(genre)"
            );
            $st->execute([$file_path, $title, $artist, $album, $genre, $year, $size]);
            echo json_encode(['ok' => true, 'id' => (int)$db->lastInsertId()]);
        } catch (Exception $e) {
            http_response_code(500);
            echo json_encode(['ok' => false, 'error' => $e->getMessage()]);
        }
    }

} elseif ($action === 'delete') {

    $id = (int)($req['id'] ?? 0);

    if ($id <= 0) {
        http_response_code(400);
        echo json_encode(['ok' => false, 'error' => 'id required']);
    } else {
        try {
            $db = mc1_db('mcaster1_media');
            $st = $db->prepare('DELETE FROM tracks WHERE id = ?');
            $st->execute([$id]);
            echo json_encode(['ok' => true, 'deleted' => $st->rowCount()]);
        } catch (Exception $e) {
            http_response_code(500);
            echo json_encode(['ok' => false, 'error' => $e->getMessage()]);
        }
    }

} elseif ($action === 'scan') {

    $dir = realpath(trim($req['directory'] ?? ''));

    if ($dir === false || !is_dir($dir)) {
        http_response_code(400);
        echo json_encode(['ok' => false, 'error' => 'directory not found or not accessible']);
    } else {
        $added = 0; $skipped = 0; $errors = [];

        try {
            $db = mc1_db('mcaster1_media');
            $st = $db->prepare(
                "INSERT IGNORE INTO tracks
                   (file_path, title, artist, album, genre, file_size_bytes, date_added)
                 VALUES (?,?,?,?,?,?,NOW())"
            );

            $it = new RecursiveIteratorIterator(
                new RecursiveDirectoryIterator($dir,
                    FilesystemIterator::SKIP_DOTS | FilesystemIterator::FOLLOW_SYMLINKS)
            );

            foreach ($it as $file) {
                if ($added + $skipped >= 5000) break;
                if (!$file->isFile() || !mc1_is_audio($file->getRealPath())) {
                    $skipped++;
                    continue;
                }
                $path = $file->getRealPath();
                $meta = mc1_parse_filename($path);
                try {
                    $st->execute([$path, $meta['title'], $meta['artist'], '', '',
                                  $file->getSize() ?: null]);
                    $added++;
                } catch (Exception $e) {
                    $skipped++;
                    if (count($errors) < 5) $errors[] = $e->getMessage();
                }
            }

            echo json_encode([
                'ok'      => true,
                'added'   => $added,
                'skipped' => $skipped,
                'errors'  => $errors,
            ]);
        } catch (Exception $e) {
            http_response_code(500);
            echo json_encode(['ok' => false, 'error' => $e->getMessage()]);
        }
    }

} else {

    http_response_code(400);
    echo json_encode(['ok' => false, 'error' => 'Unknown action: ' . $action]);

}
