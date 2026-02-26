<?php
/**
 * tracks.php — Media Library Track API
 *
 * File:    src/linux/web_ui/app/api/tracks.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-02-23
 * Purpose: We provide all track CRUD, folder browsing, tag editing, album art fetching,
 *          and playlist management endpoints for the media library.
 *          We also manage the media_library_paths table for DB-stored scan roots.
 *
 * Actions (all POST JSON):
 *  list                — Paginated track list with optional search query and folder filter
 *  folders             — List immediate subdirectories for the folder tree browser
 *  get                 — Fetch all fields for a single track (edit modal population)
 *  update              — Save edited metadata to DB + best-effort ffmpeg tag write
 *  delete              — Remove track from DB; optionally unlink physical file
 *  fetch_art           — Fetch album art from MusicBrainz / Cover Art Archive
 *  scan                — Recursive directory scan, add audio files to library
 *  playlist_files      — Discover .m3u/.pls/.xspf/.txt playlist files on disk
 *  add_to_playlist     — Add a track to an existing playlist with clockwheel weight
 *  create_playlist_add — Create a new playlist and immediately add a track to it
 *  add_library_path    — Add an additional server folder path to the media library DB
 *  list_library_paths  — Return all DB-stored media library scan paths
 *  remove_library_path — Remove a stored scan path by id
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active on this server
 *  - We use Mc1Db trait for all database access
 *  - We use first-person plural throughout all comments
 *  - We use raw SQL only, no ORMs or 3rd-party query builders
 *  - We escape all shell arguments with escapeshellarg() before any exec
 *  - We sanitize all user input before using in filesystem or DB operations
 *
 * CHANGELOG:
 *  2026-02-23  v2.0.0  Full rewrite — added folders, get, update (ffmpeg), fetch_art
 *                       (MusicBrainz/CAA), add/list/remove_library_path (DB-stored paths),
 *                       add_to_playlist, create_playlist_add, enhanced list/delete.
 *  2026-02-21  v1.0.0  Initial scaffold — list, add, delete, scan, playlist_files
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/../inc/mc1_config.php';
require_once __DIR__ . '/../inc/db.php';
require_once __DIR__ . '/../inc/traits.db.class.php';
require_once __DIR__ . '/../inc/logger.php';
require_once __DIR__ . '/../inc/auth.php';
require_once __DIR__ . '/../inc/user_auth.php';

header('Content-Type: application/json');

/* ── Auth gate — we require a valid C++ session for all track API actions ── */
if (!mc1_is_authed()) {
    mc1_api_respond(['error' => 'Unauthorized'], 403);
    return;
}

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    mc1_api_respond(['error' => 'POST required'], 405);
    return;
}

/*
 * We bootstrap the media_library_paths table here so any deployment
 * automatically gets this feature without a separate migration step.
 * We also bootstrap the cover_art table for the same reason.
 */
(function() {
    try {
        $pdo = mc1_db('mcaster1_media');
        $pdo->exec("
            CREATE TABLE IF NOT EXISTS media_library_paths (
                id         INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
                path       VARCHAR(1024) NOT NULL                        COMMENT 'Absolute filesystem path to scan root',
                label      VARCHAR(255)  DEFAULT NULL                    COMMENT 'Human-readable display label',
                is_active  TINYINT(1)    NOT NULL DEFAULT 1              COMMENT 'We set 0 to disable without deleting',
                date_added DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP,
                UNIQUE KEY uq_path (path(512))
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
            COMMENT='We store additional media library scan paths here — no YAML edit required'
        ");
        $pdo->exec("
            CREATE TABLE IF NOT EXISTS cover_art (
                hash      CHAR(40)          NOT NULL PRIMARY KEY   COMMENT 'SHA1 hex of raw image bytes',
                data      MEDIUMBLOB        NOT NULL               COMMENT 'Raw JPEG/PNG — up to 16 MB',
                mime_type VARCHAR(50)       NOT NULL DEFAULT 'image/jpeg',
                width     SMALLINT UNSIGNED          DEFAULT NULL,
                height    SMALLINT UNSIGNED          DEFAULT NULL,
                source    VARCHAR(512)               DEFAULT NULL  COMMENT 'MusicBrainz CAA source URL',
                created_at DATETIME         NOT NULL DEFAULT CURRENT_TIMESTAMP
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
            COMMENT='We store fetched album cover art here, referenced by tracks.cover_art_hash'
        ");
        /*
         * We add categories and track_categories tables so operators can create
         * custom groupings beyond filesystem folders and genres.
         * Examples: Drive Time, Morning Show, Sweepers, Holiday rotation, etc.
         */
        $pdo->exec("
            CREATE TABLE IF NOT EXISTS categories (
                id         INT UNSIGNED     NOT NULL AUTO_INCREMENT PRIMARY KEY,
                name       VARCHAR(100)     NOT NULL                              COMMENT 'Category display name',
                color_hex  VARCHAR(7)       NOT NULL DEFAULT '#14b8a6'            COMMENT 'UI badge color',
                icon       VARCHAR(50)               DEFAULT 'fa-tag'            COMMENT 'FontAwesome icon class',
                type       ENUM('genre','custom') NOT NULL DEFAULT 'custom'      COMMENT 'genre = from track tags, custom = user-created',
                sort_order SMALLINT UNSIGNED NOT NULL DEFAULT 0,
                created_at DATETIME         NOT NULL DEFAULT CURRENT_TIMESTAMP,
                UNIQUE KEY uq_name (name)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
            COMMENT='We store user-created media library categories here'
        ");
        $pdo->exec("
            CREATE TABLE IF NOT EXISTS track_categories (
                track_id    INT UNSIGNED NOT NULL,
                category_id INT UNSIGNED NOT NULL,
                added_at    DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
                PRIMARY KEY (track_id, category_id),
                INDEX idx_category_id (category_id)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
            COMMENT='We link tracks to categories (many-to-many)'
        ");
    } catch (Exception $e) {
        /* We log but do not abort — tables may already exist or error is transient */
        mc1_log(2, 'tracks.php table bootstrap failed', json_encode(['err' => $e->getMessage()]));
    }
})();

$raw    = (string)file_get_contents('php://input');
$body   = json_decode($raw, true) ?: [];
$action = (string)($body['action'] ?? '');

mc1_log_request();

/* ══════════════════════════════════════════════════════════════════════════
 * action: list_library_paths — Return all DB-stored media library paths
 * We include the built-in MC1_AUDIO_ROOT and MC1_AUDIO_GENRE_DIRS as
 * immutable entries so the UI can show the full picture.
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'list_library_paths') {
    try {
        $pdo  = mc1_db('mcaster1_media');
        $stmt = $pdo->query(
            'SELECT id, path, label, is_active, date_added FROM media_library_paths ORDER BY date_added ASC'
        );
        $rows = $stmt->fetchAll(\PDO::FETCH_ASSOC);

        foreach ($rows as &$r) {
            $r['id']        = (int)$r['id'];
            $r['is_active'] = (bool)$r['is_active'];
            $r['builtin']   = false;
        }
        unset($r);

        /* We prepend the built-in paths from config so they always appear at the top */
        $builtins = [];
        $builtins[] = ['id' => null, 'path' => MC1_AUDIO_ROOT, 'label' => 'Audio Root (built-in)', 'is_active' => true, 'builtin' => true, 'date_added' => null];
        foreach (MC1_AUDIO_GENRE_DIRS as $gd) {
            $builtins[] = ['id' => null, 'path' => $gd, 'label' => basename($gd) . ' (built-in genre)', 'is_active' => true, 'builtin' => true, 'date_added' => null];
        }

        mc1_api_respond(['ok' => true, 'paths' => array_merge($builtins, $rows)]);
        return;
    } catch (Exception $e) {
        mc1_api_respond(['error' => 'Database error: ' . $e->getMessage()], 500);
        return;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: add_library_path — Persist a new server folder path to the DB
 * We validate the path exists on disk before storing it.
 * The user can label the path with a friendly name for the UI.
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'add_library_path') {
    $path  = trim((string)($body['path']  ?? ''));
    $label = trim((string)($body['label'] ?? ''));

    if ($path === '') {
        mc1_api_respond(['error' => 'path is required'], 400);
        return;
    }

    $real = realpath($path);
    if ($real === false || !is_dir($real)) {
        mc1_api_respond(['error' => 'Path does not exist or is not a directory: ' . $path], 400);
        return;
    }

    /* We do not store paths that point inside sensitive system directories */
    $blocked_prefixes = ['/proc', '/sys', '/dev', '/boot', '/etc/shadow', '/root'];
    foreach ($blocked_prefixes as $bp) {
        if (strncmp($real, $bp, strlen($bp)) === 0) {
            mc1_api_respond(['error' => 'Path is in a restricted system area'], 403);
            return;
        }
    }

    if ($label === '') {
        $label = basename($real);
    }

    try {
        $pdo  = mc1_db('mcaster1_media');
        $stmt = $pdo->prepare(
            'INSERT INTO media_library_paths (path, label, is_active, date_added)
             VALUES (?, ?, 1, NOW())
             ON DUPLICATE KEY UPDATE label = VALUES(label), is_active = 1'
        );
        $stmt->execute([$real, $label]);
        $new_id = $stmt->rowCount() > 0 ? (int)$pdo->lastInsertId() : null;

        mc1_log(4, 'library path added', json_encode(['path' => $real, 'label' => $label]));
        mc1_api_respond(['ok' => true, 'id' => $new_id, 'path' => $real, 'label' => $label]);
        return;
    } catch (Exception $e) {
        mc1_api_respond(['error' => 'Failed to store path: ' . $e->getMessage()], 500);
        return;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: remove_library_path — Remove a stored scan path by id
 * We only remove DB-stored entries; built-in paths cannot be removed here.
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'remove_library_path') {
    $id = (int)($body['id'] ?? 0);
    if ($id < 1) {
        mc1_api_respond(['error' => 'id required'], 400);
        return;
    }
    try {
        $pdo = mc1_db('mcaster1_media');
        $pdo->prepare('DELETE FROM media_library_paths WHERE id = ?')->execute([$id]);
        mc1_api_respond(['ok' => true, 'id' => $id]);
        return;
    } catch (Exception $e) {
        mc1_api_respond(['error' => 'Delete failed: ' . $e->getMessage()], 500);
        return;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: list — Paginated track list with optional search and folder filter
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'list') {
    $page   = max(1, (int)($body['page']   ?? 1));
    $limit  = min(200, max(1, (int)($body['limit'] ?? 50)));
    $q      = trim((string)($body['q']      ?? ''));
    $folder = trim((string)($body['folder'] ?? ''));

    $offset  = ($page - 1) * $limit;
    $params  = [];
    $where   = [];

    if ($q !== '') {
        $like    = '%' . $q . '%';
        $where[] = '(title LIKE ? OR artist LIKE ? OR album LIKE ? OR genre LIKE ?)';
        $params  = array_merge($params, [$like, $like, $like, $like]);
    }

    if ($folder !== '') {
        $real = realpath($folder);
        if ($real !== false && is_dir($real)) {
            $where[]  = 'file_path LIKE ?';
            $params[] = $real . '/%';
        }
    }

    $where_sql = $where ? 'WHERE ' . implode(' AND ', $where) : '';

    try {
        $pdo      = mc1_db('mcaster1_media');
        $cnt_stmt = $pdo->prepare("SELECT COUNT(*) FROM tracks $where_sql");
        $cnt_stmt->execute($params);
        $total = (int)$cnt_stmt->fetchColumn();
        $pages = $total > 0 ? (int)ceil($total / $limit) : 1;

        $sel_stmt = $pdo->prepare(
            "SELECT id, file_path, title, artist, album, genre, year,
                    duration_ms, bitrate_kbps, bpm, energy_level, mood_tag,
                    rating, play_count, is_missing, is_jingle, is_sweeper, is_spot,
                    cover_art_hash, date_added, last_played_at
             FROM tracks
             $where_sql
             ORDER BY artist ASC, title ASC
             LIMIT $limit OFFSET $offset"
        );
        $sel_stmt->execute($params);
        $rows = $sel_stmt->fetchAll(\PDO::FETCH_ASSOC);

        foreach ($rows as &$r) {
            $r['id']           = (int)$r['id'];
            $r['duration_ms']  = (int)$r['duration_ms'];
            $r['bitrate_kbps'] = (int)$r['bitrate_kbps'];
            $r['bpm']          = $r['bpm']         !== null ? (float)$r['bpm']         : null;
            $r['energy_level'] = $r['energy_level'] !== null ? (float)$r['energy_level'] : null;
            $r['rating']       = (int)$r['rating'];
            $r['play_count']   = (int)$r['play_count'];
            $r['is_missing']   = (bool)$r['is_missing'];
            $r['is_jingle']    = (bool)$r['is_jingle'];
            $r['is_sweeper']   = (bool)$r['is_sweeper'];
            $r['is_spot']      = (bool)$r['is_spot'];
            $r['art_url']      = $r['cover_art_hash'] ? '/app/api/artwork.php?h=' . $r['cover_art_hash'] : null;
        }
        unset($r);

        mc1_api_respond(['ok' => true, 'total' => $total, 'pages' => $pages, 'page' => $page, 'data' => $rows]);
        return;
    } catch (Exception $e) {
        mc1_log(2, 'tracks list failed', json_encode(['err' => $e->getMessage()]));
        mc1_api_respond(['error' => 'Database error'], 500);
        return;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: folders — List immediate subdirectories for the folder tree browser
 * We validate the requested path is under an allowed root before listing.
 * We also return track counts per subfolder from the DB.
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'folders') {
    $requested = trim((string)($body['path'] ?? MC1_AUDIO_ROOT));
    $real      = realpath($requested);

    if ($real === false || !is_dir($real)) {
        mc1_api_respond(['error' => 'Path not found or not a directory'], 400);
        return;
    }

    /* We build the full set of allowed root paths: built-in + DB-stored */
    $allowed_roots = [MC1_AUDIO_ROOT];
    try {
        $pdo = mc1_db('mcaster1_media');
        $db_paths = $pdo->query(
            'SELECT path FROM media_library_paths WHERE is_active = 1'
        )->fetchAll(\PDO::FETCH_COLUMN);
        $allowed_roots = array_merge($allowed_roots, $db_paths);
    } catch (Exception $ignored) {}

    $is_allowed = false;
    foreach ($allowed_roots as $root) {
        $real_root = realpath($root) ?: $root;
        if ($real === $real_root || strncmp($real, $real_root . '/', strlen($real_root) + 1) === 0) {
            $is_allowed = true;
            break;
        }
    }
    if (!$is_allowed) {
        mc1_api_respond(['error' => 'Path outside allowed media library roots'], 403);
        return;
    }

    $entries = [];
    try {
        $pdo  = mc1_db('mcaster1_media');
        $iter = new \DirectoryIterator($real);
        foreach ($iter as $item) {
            if ($item->isDot() || !$item->isDir() || $item->getFilename()[0] === '.') {
                continue;
            }
            $sub_path = $item->getRealPath();
            $name     = $item->getFilename();

            $has_children = false;
            try {
                foreach (new \DirectoryIterator($sub_path) as $si) {
                    if (!$si->isDot() && $si->isDir() && $si->getFilename()[0] !== '.') {
                        $has_children = true;
                        break;
                    }
                }
            } catch (\Exception $ignored) {}

            $track_count = 0;
            try {
                $c = $pdo->prepare('SELECT COUNT(*) FROM tracks WHERE file_path LIKE ?');
                $c->execute([$sub_path . '/%']);
                $track_count = (int)$c->fetchColumn();
            } catch (\Exception $ignored) {}

            $entries[] = [
                'name'         => $name,
                'path'         => $sub_path,
                'has_children' => $has_children,
                'track_count'  => $track_count,
            ];
        }
    } catch (\Exception $e) {
        mc1_api_respond(['error' => 'Failed to read directory: ' . $e->getMessage()], 500);
        return;
    }

    usort($entries, fn($a, $b) => strcasecmp($a['name'], $b['name']));
    mc1_api_respond(['ok' => true, 'path' => $real, 'folders' => $entries]);
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: get — Fetch all fields for a single track (populates the edit modal)
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'get') {
    $id = (int)($body['id'] ?? 0);
    if ($id < 1) {
        mc1_api_respond(['error' => 'id required'], 400);
        return;
    }

    try {
        $pdo  = mc1_db('mcaster1_media');
        $stmt = $pdo->prepare(
            'SELECT id, file_path, title, artist, album, genre, year, track_number,
                    duration_ms, bitrate_kbps, sample_rate, channels, file_size_bytes,
                    bpm, musical_key, energy_level, mood_tag,
                    intro_ms, outro_ms, cue_in_ms, cue_out_ms, isrc,
                    is_jingle, is_sweeper, is_spot, is_missing,
                    rating, play_count, last_played_at,
                    cover_art_hash, date_added, date_modified
             FROM tracks WHERE id = ? LIMIT 1'
        );
        $stmt->execute([$id]);
        $row = $stmt->fetch(\PDO::FETCH_ASSOC);

        if (!$row) {
            mc1_api_respond(['error' => 'Track not found'], 404);
            return;
        }

        $row['id']             = (int)$row['id'];
        $row['duration_ms']    = (int)$row['duration_ms'];
        $row['bitrate_kbps']   = (int)$row['bitrate_kbps'];
        $row['sample_rate']    = (int)$row['sample_rate'];
        $row['channels']       = (int)$row['channels'];
        $row['file_size_bytes']= (int)$row['file_size_bytes'];
        $row['year']           = $row['year']        ? (int)$row['year']        : null;
        $row['track_number']   = $row['track_number']? (int)$row['track_number']: null;
        $row['bpm']            = $row['bpm']         !== null ? (float)$row['bpm']         : null;
        $row['energy_level']   = $row['energy_level']!== null ? (float)$row['energy_level']: null;
        $row['intro_ms']       = (int)$row['intro_ms'];
        $row['outro_ms']       = (int)$row['outro_ms'];
        $row['cue_in_ms']      = (int)$row['cue_in_ms'];
        $row['cue_out_ms']     = (int)$row['cue_out_ms'];
        $row['rating']         = (int)$row['rating'];
        $row['play_count']     = (int)$row['play_count'];
        $row['is_missing']     = (bool)$row['is_missing'];
        $row['is_jingle']      = (bool)$row['is_jingle'];
        $row['is_sweeper']     = (bool)$row['is_sweeper'];
        $row['is_spot']        = (bool)$row['is_spot'];
        $row['art_url']        = $row['cover_art_hash'] ? '/app/api/artwork.php?h=' . $row['cover_art_hash'] : null;

        mc1_api_respond(['ok' => true, 'track' => $row]);
        return;
    } catch (Exception $e) {
        mc1_log(2, 'tracks get failed', json_encode(['id' => $id, 'err' => $e->getMessage()]));
        mc1_api_respond(['error' => 'Database error'], 500);
        return;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: update — Save edited metadata to DB and optionally write file tags
 * We use ffmpeg -c copy so the audio data is NEVER re-encoded.
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'update') {
    $id = (int)($body['id'] ?? 0);
    if ($id < 1) {
        mc1_api_respond(['error' => 'id required'], 400);
        return;
    }

    try {
        $pdo  = mc1_db('mcaster1_media');
        $t    = $pdo->prepare('SELECT file_path FROM tracks WHERE id = ? LIMIT 1');
        $t->execute([$id]);
        $track = $t->fetch(\PDO::FETCH_ASSOC);
        if (!$track) {
            mc1_api_respond(['error' => 'Track not found'], 404);
            return;
        }
    } catch (Exception $e) {
        mc1_api_respond(['error' => 'Database error'], 500);
        return;
    }

    $file_path = $track['file_path'];
    $fields = [
        'title'        => (string)($body['title']        ?? ''),
        'artist'       => (string)($body['artist']       ?? ''),
        'album'        => (string)($body['album']        ?? ''),
        'genre'        => (string)($body['genre']        ?? ''),
        'year'         => isset($body['year'])         && $body['year'] !== '' ? (int)$body['year'] : null,
        'track_number' => isset($body['track_number']) && $body['track_number'] !== '' ? (int)$body['track_number'] : null,
        'bpm'          => isset($body['bpm'])          && $body['bpm'] !== '' ? (float)$body['bpm'] : null,
        'energy_level' => isset($body['energy_level']) && $body['energy_level'] !== '' ? max(0.0, min(1.0, (float)$body['energy_level'])) : null,
        'mood_tag'     => isset($body['mood_tag'])    ? trim((string)$body['mood_tag'])    : null,
        'musical_key'  => isset($body['musical_key']) ? trim((string)$body['musical_key']) : null,
        'rating'       => isset($body['rating'])  ? max(1, min(5, (int)$body['rating']))   : 3,
        'intro_ms'     => isset($body['intro_ms'])   && $body['intro_ms']  !== '' ? max(0, (int)$body['intro_ms'])  : 0,
        'outro_ms'     => isset($body['outro_ms'])   && $body['outro_ms']  !== '' ? max(0, (int)$body['outro_ms'])  : 0,
        'cue_in_ms'    => isset($body['cue_in_ms'])  && $body['cue_in_ms'] !== '' ? max(0, (int)$body['cue_in_ms']) : 0,
        'cue_out_ms'   => isset($body['cue_out_ms']) && $body['cue_out_ms']!== '' ? max(0, (int)$body['cue_out_ms']): 0,
        'is_jingle'    => !empty($body['is_jingle'])  ? 1 : 0,
        'is_sweeper'   => !empty($body['is_sweeper']) ? 1 : 0,
        'is_spot'      => !empty($body['is_spot'])    ? 1 : 0,
    ];

    $set_parts  = [];
    $set_params = [];
    foreach ($fields as $col => $val) {
        $set_parts[]  = "$col = ?";
        $set_params[] = $val;
    }
    $set_parts[]  = 'date_modified = NOW()';
    $set_params[] = $id;

    try {
        $pdo->prepare('UPDATE tracks SET ' . implode(', ', $set_parts) . ' WHERE id = ?')
            ->execute($set_params);
    } catch (Exception $e) {
        mc1_api_respond(['error' => 'DB update failed: ' . $e->getMessage()], 500);
        return;
    }

    /* We write tags to the file via ffmpeg if the caller requests it */
    $wrote_tags = false;
    $tag_error  = '';
    if (!empty($body['write_tags']) && is_writable($file_path)) {
        $ext = strtolower(pathinfo($file_path, PATHINFO_EXTENSION));
        $tmp = tempnam(sys_get_temp_dir(), 'mc1retag_') . '.' . $ext;

        $tag_map = [
            'title'  => $fields['title'],
            'artist' => $fields['artist'],
            'album'  => $fields['album'],
            'genre'  => $fields['genre'],
            'track'  => $fields['track_number'] !== null ? (string)$fields['track_number'] : '',
            'date'   => $fields['year']         !== null ? (string)$fields['year']         : '',
            'bpm'    => $fields['bpm']          !== null ? (string)((int)$fields['bpm'])   : '',
            'mood'   => $fields['mood_tag']     ?? '',
            'key'    => $fields['musical_key']  ?? '',
        ];
        $meta_args = '';
        foreach ($tag_map as $k => $v) {
            if ($v !== '' && $v !== null) {
                $meta_args .= ' -metadata ' . escapeshellarg($k . '=' . $v);
            }
        }

        /* We clear old tags first with -map_metadata -1, then set our clean values */
        $cmd = 'ffmpeg -y -i ' . escapeshellarg($file_path)
             . ' -map_metadata -1'
             . $meta_args
             . ' -c copy ' . escapeshellarg($tmp)
             . ' > /dev/null 2>&1';
        @shell_exec($cmd);

        if (is_file($tmp) && filesize($tmp) > 4096) {
            if (@rename($tmp, $file_path)) {
                $wrote_tags = true;
            } else {
                $tag_error = 'ffmpeg succeeded but rename failed (permissions?)';
                @unlink($tmp);
            }
        } else {
            $tag_error = 'ffmpeg produced no output (unsupported format or file locked)';
            @unlink($tmp);
        }
    }

    mc1_log(4, 'tracks update ok', json_encode(['id' => $id, 'wrote_tags' => $wrote_tags]));
    mc1_api_respond([
        'ok'         => true,
        'id'         => $id,
        'wrote_tags' => $wrote_tags,
        'tag_error'  => $tag_error ?: null,
    ]);
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: delete — Remove track from DB; optionally delete the physical file
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'delete') {
    $id          = (int)($body['id']          ?? 0);
    $delete_file = !empty($body['delete_file']);

    if ($id < 1) {
        mc1_api_respond(['error' => 'id required'], 400);
        return;
    }

    try {
        $pdo  = mc1_db('mcaster1_media');
        $stmt = $pdo->prepare('SELECT file_path FROM tracks WHERE id = ? LIMIT 1');
        $stmt->execute([$id]);
        $track = $stmt->fetch(\PDO::FETCH_ASSOC);

        if (!$track) {
            mc1_api_respond(['error' => 'Track not found'], 404);
            return;
        }

        /* We remove playlist references first */
        $pdo->prepare('DELETE FROM playlist_tracks WHERE track_id = ?')->execute([$id]);
        $pdo->prepare('DELETE FROM tracks WHERE id = ?')->execute([$id]);

        /* We update all playlist track counts in one query */
        $pdo->exec(
            'UPDATE playlists p
             LEFT JOIN (SELECT playlist_id, COUNT(*) cnt FROM playlist_tracks GROUP BY playlist_id) pc
             ON pc.playlist_id = p.id
             SET p.track_count = COALESCE(pc.cnt, 0), p.modified_at = NOW()'
        );

        $file_deleted = false;
        if ($delete_file && !empty($track['file_path'])) {
            /* We build the full list of allowed roots for the path safety check */
            $allowed_roots = [MC1_AUDIO_ROOT];
            try {
                $db_paths = $pdo->query(
                    'SELECT path FROM media_library_paths WHERE is_active = 1'
                )->fetchAll(\PDO::FETCH_COLUMN);
                $allowed_roots = array_merge($allowed_roots, $db_paths);
            } catch (\Exception $ignored) {}

            $fp       = realpath($track['file_path']);
            $safe_del = false;
            foreach ($allowed_roots as $root) {
                $rr = realpath($root) ?: $root;
                if ($fp !== false && strncmp($fp, $rr, strlen($rr)) === 0) {
                    $safe_del = true;
                    break;
                }
            }
            if ($safe_del && is_file($fp)) {
                $file_deleted = @unlink($fp);
            }
        }

        mc1_log(4, 'tracks delete ok', json_encode(['id' => $id, 'file_deleted' => $file_deleted]));
        mc1_api_respond(['ok' => true, 'id' => $id, 'file_deleted' => $file_deleted]);
        return;
    } catch (Exception $e) {
        mc1_log(2, 'tracks delete failed', json_encode(['id' => $id, 'err' => $e->getMessage()]));
        mc1_api_respond(['error' => 'Delete failed: ' . $e->getMessage()], 500);
        return;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: fetch_art — Fetch album art from MusicBrainz / Cover Art Archive
 *
 * MusicBrainz API docs: https://musicbrainz.org/doc/MusicBrainz_API
 * Rate limit: 1 request/second — we do at most 2 requests per call (search + art).
 * Required User-Agent format: "AppName/Version (contact-url)"
 * No API key required for public read queries.
 *
 * Search endpoint: /ws/2/recording?query=artist:"X"+recording:"Y"&fmt=json&limit=5
 * Response contains recordings[].releases[].id (release MBID)
 *              and recordings[].releases[].release-group.id (release-group MBID)
 *
 * Cover Art Archive: https://coverartarchive.org/release-group/{rg-mbid}/front-500
 *                 or https://coverartarchive.org/release/{release-mbid}/front-500
 * Both endpoints redirect (307) to the actual JPEG — we follow via CURLOPT_FOLLOWLOCATION.
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'fetch_art') {
    $track_id = (int)($body['track_id'] ?? 0);
    if ($track_id < 1) {
        mc1_api_respond(['error' => 'track_id required'], 400);
        return;
    }

    try {
        $pdo  = mc1_db('mcaster1_media');
        $stmt = $pdo->prepare(
            'SELECT id, file_path, title, artist, album, cover_art_hash FROM tracks WHERE id = ? LIMIT 1'
        );
        $stmt->execute([$track_id]);
        $track = $stmt->fetch(\PDO::FETCH_ASSOC);
    } catch (Exception $e) {
        mc1_api_respond(['error' => 'Database error'], 500);
        return;
    }

    if (!$track) {
        mc1_api_respond(['error' => 'Track not found'], 404);
        return;
    }

    $artist    = trim((string)($body['artist'] ?? $track['artist'] ?? ''));
    $title     = trim((string)($body['title']  ?? $track['title']  ?? ''));
    $album     = trim((string)($body['album']  ?? $track['album']  ?? ''));
    $file_path = $track['file_path'];

    if ($artist === '' && $title === '') {
        mc1_api_respond(['error' => 'artist or title required for search'], 400);
        return;
    }

    /*
     * We send a compliant User-Agent as required by MusicBrainz policy.
     * Missing or generic agents (curl, python-requests) trigger 403 throttling.
     * Format: "AppName/Version (url-or-email)"
     */
    $mb_ua = 'Mcaster1Encoder/1.4.0 (https://mcaster1.com; studio@mcaster1.com)';

    /*
     * We use Lucene query field syntax for precision.
     * json_encode on the string gives us quoted output: "Queen" — we strip the outer PHP quotes.
     * The API supports: artist, recording, release, date, country, label, etc.
     */
    $q_parts = [];
    if ($artist !== '') $q_parts[] = 'artist:'  . json_encode($artist);
    if ($title  !== '') $q_parts[] = 'recording:' . json_encode($title);
    if ($album  !== '') $q_parts[] = 'release:'  . json_encode($album);
    $mb_query = implode(' AND ', $q_parts);

    $mb_url = 'https://musicbrainz.org/ws/2/recording?' . http_build_query([
        'query' => $mb_query,
        'fmt'   => 'json',
        'limit' => 5,
    ]);

    $ch = curl_init();
    curl_setopt_array($ch, [
        CURLOPT_URL            => $mb_url,
        CURLOPT_RETURNTRANSFER => true,
        CURLOPT_USERAGENT      => $mb_ua,
        CURLOPT_TIMEOUT        => 12,
        CURLOPT_FOLLOWLOCATION => true,
        CURLOPT_HTTPHEADER     => ['Accept: application/json'],
    ]);
    $mb_raw  = curl_exec($ch);
    $mb_code = curl_getinfo($ch, CURLINFO_HTTP_CODE);
    curl_close($ch);

    if ($mb_code !== 200 || !$mb_raw) {
        mc1_api_respond(['ok' => false, 'error' => 'MusicBrainz returned HTTP ' . $mb_code], 502);
        return;
    }

    $mb_data = json_decode($mb_raw, true);
    if (!$mb_data || empty($mb_data['recordings'])) {
        mc1_api_respond(['ok' => false, 'error' => 'No MusicBrainz recording found for: ' . $mb_query], 404);
        return;
    }

    /*
     * We iterate recordings and pick the first high-confidence result with release data.
     * We prefer release-group MBID for CAA queries — it returns the canonical album cover
     * across all pressings/editions, not just one specific release.
     */
    $release_mbid = null;
    $rg_mbid      = null;
    foreach ($mb_data['recordings'] as $rec) {
        if ((int)($rec['score'] ?? 0) < 50) {
            continue;
        }
        foreach (($rec['releases'] ?? []) as $rel) {
            if (!empty($rel['id'])) {
                $release_mbid = $rel['id'];
                if (!empty($rel['release-group']['id'])) {
                    $rg_mbid = $rel['release-group']['id'];
                }
                break 2;
            }
        }
    }

    if (!$release_mbid) {
        mc1_api_respond(['ok' => false, 'error' => 'No release MBID in MusicBrainz results'], 404);
        return;
    }

    /*
     * We try CAA endpoints in order of preference.
     * front-500 gives a 500px thumbnail (~50-100KB) which is sufficient for the UI.
     * We fall back to the non-sized front which may be a large original scan.
     */
    $caa_urls = [];
    if ($rg_mbid) {
        $caa_urls[] = 'https://coverartarchive.org/release-group/' . $rg_mbid . '/front-500';
        $caa_urls[] = 'https://coverartarchive.org/release-group/' . $rg_mbid . '/front';
    }
    $caa_urls[] = 'https://coverartarchive.org/release/' . $release_mbid . '/front-500';
    $caa_urls[] = 'https://coverartarchive.org/release/' . $release_mbid . '/front';

    $img_bytes  = null;
    $img_mime   = 'image/jpeg';
    $source_url = '';

    foreach ($caa_urls as $caa_url) {
        $ch = curl_init();
        curl_setopt_array($ch, [
            CURLOPT_URL            => $caa_url,
            CURLOPT_RETURNTRANSFER => true,
            CURLOPT_USERAGENT      => $mb_ua,
            CURLOPT_TIMEOUT        => 15,
            CURLOPT_FOLLOWLOCATION => true,
            CURLOPT_MAXREDIRS      => 5,
            CURLOPT_HEADER         => true,
        ]);
        $resp      = curl_exec($ch);
        $http_code = curl_getinfo($ch, CURLINFO_HTTP_CODE);
        $hdr_size  = curl_getinfo($ch, CURLINFO_HEADER_SIZE);
        curl_close($ch);

        if ($http_code !== 200 || !$resp) {
            continue;
        }
        $body_bytes = substr($resp, $hdr_size);
        $hdrs       = substr($resp, 0, $hdr_size);

        if (preg_match('/Content-Type:\s*(image\/[a-z+]+)/i', $hdrs, $m)) {
            $img_mime = $m[1];
        }

        /* We validate magic bytes — JPEG starts with 0xFF 0xD8, PNG with 0x89 PNG */
        $magic   = substr($body_bytes, 0, 4);
        $is_jpeg = (substr($magic, 0, 2) === "\xFF\xD8");
        $is_png  = ($magic === "\x89PNG");
        if (!$is_jpeg && !$is_png) {
            continue;
        }

        $img_bytes  = $body_bytes;
        $source_url = $caa_url;
        break;
    }

    if (!$img_bytes) {
        mc1_api_respond([
            'ok'           => false,
            'error'        => 'Cover Art Archive returned no usable image for any tried URL',
            'release_mbid' => $release_mbid,
            'rg_mbid'      => $rg_mbid,
        ], 404);
        return;
    }

    $hash = sha1($img_bytes);

    if (!is_dir(MC1_ARTWORK_DIR)) {
        @mkdir(MC1_ARTWORK_DIR, 0755, true);
    }
    $art_file   = MC1_ARTWORK_DIR . '/' . $hash . '.jpg';
    $wrote_file = (bool)@file_put_contents($art_file, $img_bytes);

    try {
        $pdo = mc1_db('mcaster1_media');
        $pdo->prepare(
            'INSERT INTO cover_art (hash, data, mime_type, source)
             VALUES (?, ?, ?, ?)
             ON DUPLICATE KEY UPDATE source = VALUES(source)'
        )->execute([$hash, $img_bytes, $img_mime, $source_url]);

        $pdo->prepare(
            'UPDATE tracks SET cover_art_hash = ?, date_modified = NOW() WHERE id = ?'
        )->execute([$hash, $track_id]);
    } catch (Exception $e) {
        mc1_api_respond(['error' => 'Art downloaded but DB store failed: ' . $e->getMessage()], 500);
        return;
    }

    /* We optionally embed the cover into the file's tag stream via ffmpeg */
    $wrote_tag = false;
    $tag_error = '';
    if (!empty($body['embed_in_file']) && $wrote_file && is_writable($file_path)) {
        $ext = strtolower(pathinfo($file_path, PATHINFO_EXTENSION));
        $tmp = tempnam(sys_get_temp_dir(), 'mc1art_') . '.' . $ext;
        $cmd = 'ffmpeg -y'
             . ' -i ' . escapeshellarg($file_path)
             . ' -i ' . escapeshellarg($art_file)
             . ' -map 0:a -map 1:v'
             . ' -c:a copy -c:v mjpeg'
             . ' -metadata:s:v title=' . escapeshellarg('Album cover')
             . ' -metadata:s:v comment=' . escapeshellarg('Cover (front)')
             . ' -id3v2_version 3'
             . ' ' . escapeshellarg($tmp)
             . ' > /dev/null 2>&1';
        @shell_exec($cmd);

        if (is_file($tmp) && filesize($tmp) > 4096) {
            if (@rename($tmp, $file_path)) {
                $wrote_tag = true;
            } else {
                $tag_error = 'rename to original failed';
                @unlink($tmp);
            }
        } else {
            $tag_error = 'ffmpeg art-embed produced empty output';
            @unlink($tmp);
        }
    }

    mc1_log(4, 'tracks fetch_art ok', json_encode(['id' => $track_id, 'hash' => $hash, 'source' => $source_url]));
    mc1_api_respond([
        'ok'           => true,
        'hash'         => $hash,
        'art_url'      => '/app/api/artwork.php?h=' . $hash,
        'source_url'   => $source_url,
        'release_mbid' => $release_mbid,
        'rg_mbid'      => $rg_mbid,
        'wrote_file'   => $wrote_file,
        'wrote_tag'    => $wrote_tag,
        'tag_error'    => $tag_error ?: null,
    ]);
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: add_to_playlist — Add a track to an existing playlist with weight
 * Weight (0.1-10.0) is the clockwheel rotation factor for this track.
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'add_to_playlist') {
    $track_id    = (int)($body['track_id']    ?? 0);
    $playlist_id = (int)($body['playlist_id'] ?? 0);
    $weight      = isset($body['weight']) ? max(0.1, min(10.0, (float)$body['weight'])) : 1.0;

    if ($track_id < 1 || $playlist_id < 1) {
        mc1_api_respond(['error' => 'track_id and playlist_id required'], 400);
        return;
    }

    try {
        $pdo = mc1_db('mcaster1_media');

        if (!(int)$pdo->prepare('SELECT COUNT(*) FROM tracks WHERE id = ?')->execute([$track_id]) ) {}
        $t_exists = (int)$pdo->prepare('SELECT COUNT(*) FROM tracks WHERE id = ?')->execute([$track_id]);
        // We use scalar approach:
        $t_stmt = $pdo->prepare('SELECT id FROM tracks WHERE id = ? LIMIT 1');
        $t_stmt->execute([$track_id]);
        if (!$t_stmt->fetchColumn()) {
            mc1_api_respond(['error' => 'Track not found'], 404);
            return;
        }
        $p_stmt = $pdo->prepare('SELECT id FROM playlists WHERE id = ? LIMIT 1');
        $p_stmt->execute([$playlist_id]);
        if (!$p_stmt->fetchColumn()) {
            mc1_api_respond(['error' => 'Playlist not found'], 404);
            return;
        }

        $pos_stmt = $pdo->prepare(
            'SELECT COALESCE(MAX(position), 0) + 1 FROM playlist_tracks WHERE playlist_id = ?'
        );
        $pos_stmt->execute([$playlist_id]);
        $position = (int)$pos_stmt->fetchColumn();

        $pdo->prepare(
            'INSERT IGNORE INTO playlist_tracks (playlist_id, track_id, position, weight, added_at)
             VALUES (?, ?, ?, ?, NOW())'
        )->execute([$playlist_id, $track_id, $position, $weight]);

        $pdo->prepare(
            'UPDATE playlists
             SET track_count = (SELECT COUNT(*) FROM playlist_tracks WHERE playlist_id = ?),
                 modified_at = NOW()
             WHERE id = ?'
        )->execute([$playlist_id, $playlist_id]);

        mc1_api_respond(['ok' => true, 'playlist_id' => $playlist_id, 'track_id' => $track_id, 'weight' => $weight]);
        return;
    } catch (Exception $e) {
        mc1_api_respond(['error' => 'Failed to add: ' . $e->getMessage()], 500);
        return;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: create_playlist_add — Create new playlist and add a track atomically
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'create_playlist_add') {
    $track_id = (int)($body['track_id'] ?? 0);
    $name     = trim((string)($body['name'] ?? ''));
    $type     = in_array($body['type'] ?? '', ['static','smart','ai','clock']) ? $body['type'] : 'static';
    $desc     = trim((string)($body['description'] ?? ''));
    $weight   = isset($body['weight']) ? max(0.1, min(10.0, (float)$body['weight'])) : 1.0;

    if ($track_id < 1) { mc1_api_respond(['error' => 'track_id required'], 400); return; }
    if ($name === '')  { mc1_api_respond(['error' => 'Playlist name required'], 400); return; }

    try {
        $pdo    = mc1_db('mcaster1_media');
        $t_stmt = $pdo->prepare('SELECT id FROM tracks WHERE id = ? LIMIT 1');
        $t_stmt->execute([$track_id]);
        if (!$t_stmt->fetchColumn()) {
            mc1_api_respond(['error' => 'Track not found'], 404);
            return;
        }

        $pdo->prepare(
            'INSERT INTO playlists (name, type, description, track_count, created_at, modified_at)
             VALUES (?, ?, ?, 0, NOW(), NOW())'
        )->execute([$name, $type, $desc]);
        $playlist_id = (int)$pdo->lastInsertId();

        $pdo->prepare(
            'INSERT INTO playlist_tracks (playlist_id, track_id, position, weight, added_at)
             VALUES (?, ?, 1, ?, NOW())'
        )->execute([$playlist_id, $track_id, $weight]);

        $pdo->prepare('UPDATE playlists SET track_count = 1 WHERE id = ?')->execute([$playlist_id]);

        mc1_api_respond([
            'ok' => true, 'playlist_id' => $playlist_id,
            'name' => $name, 'track_id' => $track_id, 'weight' => $weight,
        ]);
        return;
    } catch (Exception $e) {
        mc1_api_respond(['error' => 'Create playlist failed: ' . $e->getMessage()], 500);
        return;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: scan — Recursively scan a directory and add audio files to library
 * We accept any path stored in media_library_paths, MC1_AUDIO_ROOT,
 * or the legacy safe_prefixes list so operators can scan any mounted share.
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'scan') {
    $dir_input = trim((string)($body['directory'] ?? MC1_AUDIO_ROOT));
    $real_dir  = realpath($dir_input);

    if ($real_dir === false || !is_dir($real_dir)) {
        mc1_api_respond(['error' => 'Directory not found: ' . $dir_input], 400);
        return;
    }

    /* We build all allowed scan roots from built-in config + DB-stored paths */
    $allowed = [MC1_AUDIO_ROOT, '/var/www/', '/home/mediacast1/', '/mnt/', '/media/'];
    try {
        $db_paths = mc1_db('mcaster1_media')->query(
            'SELECT path FROM media_library_paths WHERE is_active = 1'
        )->fetchAll(\PDO::FETCH_COLUMN);
        $allowed = array_merge($allowed, $db_paths);
    } catch (\Exception $ignored) {}

    $is_safe = false;
    foreach ($allowed as $pfx) {
        if (strncmp($real_dir, rtrim($pfx, '/'), strlen(rtrim($pfx, '/'))) === 0) {
            $is_safe = true;
            break;
        }
    }
    if (!$is_safe) {
        mc1_api_respond(['error' => 'Directory outside allowed paths'], 403);
        return;
    }

    $allowed_ext = ['mp3','ogg','flac','opus','aac','m4a','wav'];
    $added   = 0;
    $skipped = 0;
    $errors  = [];
    $scanned = 0;
    $pdo     = mc1_db('mcaster1_media');

    try {
        $iter = new \RecursiveIteratorIterator(
            new \RecursiveDirectoryIterator(
                $real_dir,
                \FilesystemIterator::SKIP_DOTS | \FilesystemIterator::FOLLOW_SYMLINKS
            ),
            \RecursiveIteratorIterator::LEAVES_ONLY
        );

        foreach ($iter as $file) {
            if ($scanned >= MC1_SCAN_FILE_LIMIT) {
                $errors[] = 'File limit (' . MC1_SCAN_FILE_LIMIT . ') reached — run scan again for more';
                break;
            }
            $ext = strtolower($file->getExtension());
            if (!in_array($ext, $allowed_ext)) {
                continue;
            }
            $scanned++;
            $fp = $file->getRealPath();

            /* We parse "Artist - Title" from the filename as a best-effort tag */
            $base   = pathinfo($fp, PATHINFO_FILENAME);
            $title  = $base;
            $artist = '';
            if (preg_match('/^(.+?)\s+-\s+(.+)$/', $base, $nm)) {
                $artist = trim($nm[1]);
                $title  = trim($nm[2]);
            }

            try {
                $stmt = $pdo->prepare(
                    'INSERT IGNORE INTO tracks (file_path, title, artist, file_size_bytes, date_added, date_scanned)
                     VALUES (?, ?, ?, ?, NOW(), NOW())'
                );
                $stmt->execute([$fp, $title, $artist, $file->getSize()]);
                $added   += ($stmt->rowCount() > 0) ? 1 : 0;
                $skipped += ($stmt->rowCount() === 0) ? 1 : 0;
            } catch (\Exception $e) {
                $errors[] = basename($fp) . ': ' . $e->getMessage();
            }
        }
    } catch (\Exception $e) {
        mc1_api_respond(['error' => 'Scan error: ' . $e->getMessage()], 500);
        return;
    }

    mc1_log(4, 'tracks scan complete', json_encode(['dir' => $real_dir, 'added' => $added, 'skipped' => $skipped]));
    mc1_api_respond(['ok' => true, 'added' => $added, 'skipped' => $skipped, 'errors' => $errors]);
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: playlist_files — Discover playlist files on disk
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'playlist_files') {
    $extra_dir    = trim((string)($body['extra_dir'] ?? ''));
    $scan_dirs    = MC1_PLAYLIST_SCAN_DIRS;
    $playlist_ext = ['m3u','m3u8','pls','xspf','txt'];

    if ($extra_dir !== '') {
        $real_extra = realpath($extra_dir);
        $safe = ['/var/www/', '/var/music/', '/home/mediacast1/', '/mnt/'];
        $ok   = false;
        foreach ($safe as $s) {
            if ($real_extra !== false && strncmp($real_extra, $s, strlen($s)) === 0) {
                $ok = true;
                break;
            }
        }
        if ($ok) {
            $scan_dirs[] = $real_extra;
        }
    }

    $seen  = [];
    $files = [];

    foreach ($scan_dirs as $scan_dir) {
        if (!is_dir($scan_dir)) {
            continue;
        }
        try {
            foreach (new \DirectoryIterator($scan_dir) as $item) {
                if ($item->isDot() || !$item->isFile()) {
                    continue;
                }
                $ext = strtolower($item->getExtension());
                if (!in_array($ext, $playlist_ext)) {
                    continue;
                }
                $fp = $item->getRealPath();
                if (isset($seen[$fp])) {
                    continue;
                }
                $seen[$fp] = true;

                $count   = 0;
                $content = @file_get_contents($fp);
                if ($content !== false) {
                    if ($ext === 'm3u' || $ext === 'm3u8') {
                        $count = count(array_filter(
                            preg_split('/\r?\n/', $content),
                            fn($l) => trim($l) !== '' && ($l[0] ?? '') !== '#'
                        ));
                    } elseif ($ext === 'pls') {
                        preg_match_all('/^File\d+=/mi', $content, $mm);
                        $count = count($mm[0]);
                    } elseif ($ext === 'xspf') {
                        $count = substr_count($content, '<location>');
                    } else {
                        $count = count(array_filter(
                            preg_split('/\r?\n/', $content),
                            fn($l) => trim($l) !== ''
                        ));
                    }
                }

                $files[] = [
                    'name'        => $item->getFilename(),
                    'path'        => $fp,
                    'format'      => strtoupper($ext),
                    'track_count' => $count,
                    'size_bytes'  => $item->getSize(),
                ];
            }
        } catch (\Exception $ignored) {}

        if (count($files) >= MC1_PLAYLIST_FILE_LIMIT) {
            break;
        }
    }

    usort($files, fn($a, $b) => strcmp($a['path'], $b['path']));
    mc1_api_respond(['ok' => true, 'files' => $files, 'count' => count($files)]);
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: list_categories — Return all user-created and genre categories
 * We also return the track count per category so the UI can show it.
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'list_categories') {
    try {
        $pdo  = mc1_db('mcaster1_media');
        $stmt = $pdo->query(
            'SELECT c.id, c.name, c.color_hex, c.icon, c.type, c.sort_order, c.created_at,
                    COUNT(tc.track_id) AS track_count
             FROM categories c
             LEFT JOIN track_categories tc ON tc.category_id = c.id
             GROUP BY c.id
             ORDER BY c.sort_order ASC, c.name ASC'
        );
        $rows = $stmt->fetchAll(\PDO::FETCH_ASSOC);
        foreach ($rows as &$r) {
            $r['id']          = (int)$r['id'];
            $r['sort_order']  = (int)$r['sort_order'];
            $r['track_count'] = (int)$r['track_count'];
        }
        unset($r);
        mc1_api_respond(['ok' => true, 'categories' => $rows]);
        return;
    } catch (Exception $e) {
        mc1_api_respond(['error' => 'Database error: ' . $e->getMessage()], 500);
        return;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: create_category — Create a new custom media library category
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'create_category') {
    $name      = trim((string)($body['name']      ?? ''));
    $color_hex = trim((string)($body['color_hex'] ?? '#14b8a6'));
    $icon      = trim((string)($body['icon']      ?? 'fa-tag'));
    $type      = in_array($body['type'] ?? '', ['genre','custom']) ? $body['type'] : 'custom';

    if ($name === '') {
        mc1_api_respond(['error' => 'name required'], 400);
        return;
    }
    /* We validate hex color format */
    if (!preg_match('/^#[0-9a-fA-F]{6}$/', $color_hex)) {
        $color_hex = '#14b8a6';
    }

    try {
        $pdo  = mc1_db('mcaster1_media');
        $stmt = $pdo->prepare(
            'INSERT INTO categories (name, color_hex, icon, type) VALUES (?, ?, ?, ?)
             ON DUPLICATE KEY UPDATE color_hex = VALUES(color_hex), icon = VALUES(icon)'
        );
        $stmt->execute([$name, $color_hex, $icon, $type]);
        $new_id = (int)$pdo->lastInsertId();
        mc1_api_respond(['ok' => true, 'id' => $new_id, 'name' => $name]);
        return;
    } catch (Exception $e) {
        mc1_api_respond(['error' => 'Create failed: ' . $e->getMessage()], 500);
        return;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: update_category — Rename or recolor an existing category
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'update_category') {
    $id        = (int)($body['id']        ?? 0);
    $name      = trim((string)($body['name']      ?? ''));
    $color_hex = trim((string)($body['color_hex'] ?? ''));
    $icon      = trim((string)($body['icon']      ?? ''));

    if ($id < 1) {
        mc1_api_respond(['error' => 'id required'], 400);
        return;
    }

    $sets   = [];
    $params = [];
    if ($name !== '')      { $sets[] = 'name = ?';      $params[] = $name;      }
    if ($color_hex !== '') { $sets[] = 'color_hex = ?'; $params[] = $color_hex; }
    if ($icon !== '')      { $sets[] = 'icon = ?';      $params[] = $icon;      }

    if (!$sets) {
        mc1_api_respond(['error' => 'Nothing to update'], 400);
        return;
    }
    $params[] = $id;

    try {
        $pdo = mc1_db('mcaster1_media');
        $pdo->prepare('UPDATE categories SET ' . implode(', ', $sets) . ' WHERE id = ?')
            ->execute($params);
        mc1_api_respond(['ok' => true, 'id' => $id]);
        return;
    } catch (Exception $e) {
        mc1_api_respond(['error' => 'Update failed: ' . $e->getMessage()], 500);
        return;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: delete_category — Remove a category and all its track assignments
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'delete_category') {
    $id = (int)($body['id'] ?? 0);
    if ($id < 1) { mc1_api_respond(['error' => 'id required'], 400); return; }
    try {
        $pdo = mc1_db('mcaster1_media');
        $pdo->prepare('DELETE FROM track_categories WHERE category_id = ?')->execute([$id]);
        $pdo->prepare('DELETE FROM categories WHERE id = ?')->execute([$id]);
        mc1_api_respond(['ok' => true, 'id' => $id]);
        return;
    } catch (Exception $e) {
        mc1_api_respond(['error' => 'Delete failed: ' . $e->getMessage()], 500);
        return;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: add_to_category — Add one or more tracks to a category
 * We accept either track_id (int) or track_ids (array of ints) for bulk adds.
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'add_to_category') {
    $category_id = (int)($body['category_id'] ?? 0);
    if ($category_id < 1) { mc1_api_respond(['error' => 'category_id required'], 400); return; }

    /* We support both single track_id and bulk track_ids array */
    $track_ids = [];
    if (!empty($body['track_ids']) && is_array($body['track_ids'])) {
        $track_ids = array_map('intval', $body['track_ids']);
    } elseif (!empty($body['track_id'])) {
        $track_ids = [(int)$body['track_id']];
    }
    $track_ids = array_filter($track_ids, fn($id) => $id > 0);

    if (!$track_ids) { mc1_api_respond(['error' => 'track_id or track_ids required'], 400); return; }

    try {
        $pdo = mc1_db('mcaster1_media');
        /* We verify the category exists */
        $c_stmt = $pdo->prepare('SELECT id FROM categories WHERE id = ? LIMIT 1');
        $c_stmt->execute([$category_id]);
        if (!$c_stmt->fetchColumn()) {
            mc1_api_respond(['error' => 'Category not found'], 404);
            return;
        }
        $added = 0;
        $ins   = $pdo->prepare(
            'INSERT IGNORE INTO track_categories (track_id, category_id, added_at) VALUES (?, ?, NOW())'
        );
        foreach ($track_ids as $tid) {
            $ins->execute([$tid, $category_id]);
            $added += $ins->rowCount();
        }
        mc1_api_respond(['ok' => true, 'added' => $added, 'category_id' => $category_id]);
        return;
    } catch (Exception $e) {
        mc1_api_respond(['error' => 'Add to category failed: ' . $e->getMessage()], 500);
        return;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: remove_from_category — Remove a track from a category
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'remove_from_category') {
    $track_id    = (int)($body['track_id']    ?? 0);
    $category_id = (int)($body['category_id'] ?? 0);
    if ($track_id < 1 || $category_id < 1) {
        mc1_api_respond(['error' => 'track_id and category_id required'], 400);
        return;
    }
    try {
        mc1_db('mcaster1_media')
            ->prepare('DELETE FROM track_categories WHERE track_id = ? AND category_id = ?')
            ->execute([$track_id, $category_id]);
        mc1_api_respond(['ok' => true]);
        return;
    } catch (Exception $e) {
        mc1_api_respond(['error' => 'Remove failed: ' . $e->getMessage()], 500);
        return;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: list_category_tracks — Return tracks assigned to a category
 * We return the same fields as the list action so the right pane can
 * display category results identically to folder browse results.
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'list_category_tracks') {
    $category_id = (int)($body['category_id'] ?? 0);
    $page        = max(1, (int)($body['page']  ?? 1));
    $limit       = min(200, max(1, (int)($body['limit'] ?? 50)));

    if ($category_id < 1) { mc1_api_respond(['error' => 'category_id required'], 400); return; }

    $offset = ($page - 1) * $limit;

    try {
        $pdo      = mc1_db('mcaster1_media');
        $cnt      = $pdo->prepare(
            'SELECT COUNT(*) FROM track_categories tc JOIN tracks t ON t.id = tc.track_id WHERE tc.category_id = ?'
        );
        $cnt->execute([$category_id]);
        $total = (int)$cnt->fetchColumn();
        $pages = $total > 0 ? (int)ceil($total / $limit) : 1;

        $sel  = $pdo->prepare(
            "SELECT t.id, t.file_path, t.title, t.artist, t.album, t.genre, t.year,
                    t.duration_ms, t.bitrate_kbps, t.bpm, t.energy_level, t.mood_tag,
                    t.rating, t.play_count, t.is_missing, t.is_jingle, t.is_sweeper, t.is_spot,
                    t.cover_art_hash, t.date_added, t.last_played_at
             FROM track_categories tc
             JOIN tracks t ON t.id = tc.track_id
             WHERE tc.category_id = ?
             ORDER BY t.artist ASC, t.title ASC
             LIMIT $limit OFFSET $offset"
        );
        $sel->execute([$category_id]);
        $rows = $sel->fetchAll(\PDO::FETCH_ASSOC);

        foreach ($rows as &$r) {
            $r['id']           = (int)$r['id'];
            $r['duration_ms']  = (int)$r['duration_ms'];
            $r['bitrate_kbps'] = (int)$r['bitrate_kbps'];
            $r['bpm']          = $r['bpm']          !== null ? (float)$r['bpm']          : null;
            $r['energy_level'] = $r['energy_level'] !== null ? (float)$r['energy_level'] : null;
            $r['rating']       = (int)$r['rating'];
            $r['play_count']   = (int)$r['play_count'];
            $r['is_missing']   = (bool)$r['is_missing'];
            $r['is_jingle']    = (bool)$r['is_jingle'];
            $r['is_sweeper']   = (bool)$r['is_sweeper'];
            $r['is_spot']      = (bool)$r['is_spot'];
            $r['art_url']      = $r['cover_art_hash'] ? '/app/api/artwork.php?h=' . $r['cover_art_hash'] : null;
        }
        unset($r);

        mc1_api_respond(['ok' => true, 'total' => $total, 'pages' => $pages, 'page' => $page, 'data' => $rows]);
        return;
    } catch (Exception $e) {
        mc1_api_respond(['error' => 'Database error: ' . $e->getMessage()], 500);
        return;
    }
}

/* We return 400 for any action we do not recognize */
/* ══════════════════════════════════════════════════════════════════════════
 * action: wipe_library — Mass-delete ALL tracks from the database.
 * We do NOT delete physical files — only the DB records are removed.
 * We also clear playlist_tracks and track_categories referencing these tracks.
 * This is a destructive operation requiring confirmation from the UI.
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'wipe_library') {
    try {
        $pdo = mc1_db('mcaster1_media');
        /* We count first so we can report how many were removed */
        $count = (int)$pdo->query('SELECT COUNT(*) FROM tracks')->fetchColumn();
        /* We remove all cross-reference rows first, then the tracks themselves */
        $pdo->exec('DELETE FROM track_categories');
        $pdo->exec('DELETE FROM playlist_tracks');
        $pdo->exec('DELETE FROM tracks');
        /* We reset the auto-increment counter for a clean slate */
        $pdo->exec('ALTER TABLE tracks AUTO_INCREMENT = 1');
        mc1_log(3, 'wipe_library executed', json_encode(['deleted' => $count]));
        mc1_api_respond(['ok' => true, 'deleted' => $count]);
    } catch (Exception $e) {
        mc1_log(2, 'wipe_library failed', json_encode(['err' => $e->getMessage()]));
        mc1_api_respond(['error' => 'Wipe failed: ' . $e->getMessage()], 500);
    }
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: delete_folder — Remove all DB track records under a folder path
 * Params: folder (string, required)
 * Removes related playlist_tracks and track_categories rows too.
 * Does NOT delete files from disk.
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'delete_folder') {
    $folder_input = trim((string)($body['folder'] ?? ''));
    if ($folder_input === '') {
        mc1_api_respond(['error' => 'folder required'], 400);
        return;
    }

    $safe_prefixes = [MC1_AUDIO_ROOT, '/var/www/', '/home/mediacast1/', '/mnt/', '/media/'];
    try {
        $db_paths = mc1_db('mcaster1_media')->query(
            'SELECT path FROM media_library_paths WHERE is_active = 1'
        )->fetchAll(\PDO::FETCH_COLUMN);
        $safe_prefixes = array_merge($safe_prefixes, $db_paths);
    } catch (\Exception $ignored) {}

    $is_safe = false;
    foreach ($safe_prefixes as $pfx) {
        if (strncmp(rtrim($folder_input, '/'), rtrim($pfx, '/'), strlen(rtrim($pfx, '/'))) === 0) {
            $is_safe = true;
            break;
        }
    }
    if (!$is_safe) {
        mc1_api_respond(['error' => 'Folder outside allowed paths'], 403);
        return;
    }

    try {
        $pdo    = mc1_db('mcaster1_media');
        $prefix = rtrim($folder_input, '/') . '/';

        /* We collect affected track IDs first for cross-reference cleanup */
        $ids_stmt = $pdo->prepare(
            'SELECT id FROM tracks WHERE file_path LIKE ? OR file_path = ?'
        );
        $ids_stmt->execute([$prefix . '%', rtrim($folder_input, '/')]);
        $ids = array_column($ids_stmt->fetchAll(\PDO::FETCH_ASSOC), 'id');

        if (empty($ids)) {
            mc1_api_respond(['ok' => true, 'deleted' => 0, 'msg' => 'No tracks found in that folder']);
            return;
        }

        $placeholders = implode(',', array_fill(0, count($ids), '?'));
        $pdo->prepare("DELETE FROM track_categories WHERE track_id IN ($placeholders)")->execute($ids);
        $pdo->prepare("DELETE FROM playlist_tracks  WHERE track_id IN ($placeholders)")->execute($ids);
        $pdo->prepare("DELETE FROM tracks           WHERE id        IN ($placeholders)")->execute($ids);

        mc1_log(4, 'delete_folder', json_encode(['folder' => $folder_input, 'deleted' => count($ids)]));
        mc1_api_respond(['ok' => true, 'deleted' => count($ids)]);
    } catch (\Exception $e) {
        mc1_api_respond(['error' => 'Delete failed: ' . $e->getMessage()], 500);
    }
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: create_playlist_from_folder
 * Params: folder (string), name (string), description (string optional)
 * Creates a new static playlist and bulk-adds all DB tracks under folder.
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'create_playlist_from_folder') {
    $folder = rtrim(trim((string)($body['folder'] ?? '')), '/');
    $name   = trim((string)($body['name'] ?? ''));
    if ($folder === '') { mc1_api_respond(['error' => 'folder required'], 400); return; }
    if ($name   === '') { mc1_api_respond(['error' => 'name required'],   400); return; }
    $desc = trim((string)($body['description'] ?? ''));

    try {
        $pdo = mc1_db('mcaster1_media');

        /* Create the playlist */
        $pdo->prepare(
            'INSERT INTO playlists (name, type, description, track_count, created_at, modified_at)
             VALUES (?, "static", ?, 0, NOW(), NOW())'
        )->execute([$name, $desc]);
        $playlist_id = (int)$pdo->lastInsertId();

        /* Bulk-insert tracks from the folder (ordered by file_path for alphabetical order) */
        $prefix    = $folder . '/';
        $ins_stmt  = $pdo->prepare(
            'INSERT IGNORE INTO playlist_tracks (playlist_id, track_id, position, weight, added_at)
             SELECT ?, id,
                    (@pos := @pos + 1),
                    1.0, NOW()
             FROM   tracks, (SELECT @pos := 0) vars
             WHERE  (file_path LIKE ? OR file_path = ?)
               AND  is_missing = 0
             ORDER  BY file_path'
        );
        $ins_stmt->execute([$playlist_id, $prefix . '%', $folder]);
        $added = (int)$ins_stmt->rowCount();

        /* Sync track_count */
        $pdo->prepare('UPDATE playlists SET track_count=? WHERE id=?')->execute([$added, $playlist_id]);

        mc1_log(4, 'create_playlist_from_folder', json_encode([
            'folder' => $folder, 'playlist_id' => $playlist_id,
            'name' => $name, 'tracks' => $added,
        ]));
        mc1_api_respond(['ok' => true, 'playlist_id' => $playlist_id, 'track_count' => $added, 'name' => $name]);
    } catch (\Exception $e) {
        mc1_api_respond(['error' => 'Create playlist failed: ' . $e->getMessage()], 500);
    }
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: sync_library
 * Scans all audio roots, marks missing files, finds duplicate entries.
 * Returns: added, missing_marked, dupes_found (same title+artist+duration)
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'sync_library') {
    $pdo         = mc1_db('mcaster1_media');
    $allowed_ext = ['mp3','ogg','flac','opus','aac','m4a','wav','wma','aiff','aif','ape','alac','m4p'];
    $added       = 0;
    $skipped     = 0;
    $missing     = 0;
    $dupes       = 0;
    $errors      = [];

    /* ── Step 1: Collect scan roots (MC1_AUDIO_ROOT + active library paths) ── */
    $scan_roots = [MC1_AUDIO_ROOT];
    try {
        $dp = $pdo->query('SELECT path FROM media_library_paths WHERE is_active=1')->fetchAll(\PDO::FETCH_COLUMN);
        foreach ($dp as $p) {
            if ($p && is_dir($p)) $scan_roots[] = $p;
        }
    } catch (\Exception $ignored) {}
    $scan_roots = array_unique($scan_roots);

    /* ── Step 2: Scan all roots and add new files ─────────────────────────── */
    $scanned = 0;
    foreach ($scan_roots as $root) {
        if (!is_dir($root)) continue;
        try {
            $iter = new \RecursiveIteratorIterator(
                new \RecursiveDirectoryIterator($root,
                    \FilesystemIterator::SKIP_DOTS | \FilesystemIterator::FOLLOW_SYMLINKS),
                \RecursiveIteratorIterator::LEAVES_ONLY
            );
            foreach ($iter as $file) {
                if ($scanned >= MC1_SCAN_FILE_LIMIT) {
                    $errors[] = 'File limit (' . MC1_SCAN_FILE_LIMIT . ') reached — run sync again';
                    break 2;
                }
                $ext = strtolower($file->getExtension());
                if (!in_array($ext, $allowed_ext)) continue;
                $scanned++;
                $fp = $file->getRealPath();

                $base   = pathinfo($fp, PATHINFO_FILENAME);
                $title  = $base;
                $artist = '';
                if (preg_match('/^(.+?)\s+-\s+(.+)$/', $base, $nm)) {
                    $artist = trim($nm[1]);
                    $title  = trim($nm[2]);
                }

                try {
                    $stmt = $pdo->prepare(
                        'INSERT IGNORE INTO tracks (file_path, title, artist, file_size_bytes, date_added, date_scanned)
                         VALUES (?, ?, ?, ?, NOW(), NOW())'
                    );
                    $stmt->execute([$fp, $title, $artist, $file->getSize()]);
                    if ($stmt->rowCount() > 0) {
                        $added++;
                    } else {
                        $skipped++;
                        /* If the record existed, update is_missing=0 and date_scanned */
                        $pdo->prepare(
                            'UPDATE tracks SET is_missing=0, date_scanned=NOW()
                             WHERE file_path=? AND is_missing=1'
                        )->execute([$fp]);
                    }
                } catch (\Exception $e) {
                    $errors[] = basename($fp) . ': ' . $e->getMessage();
                }
            }
        } catch (\Exception $e) {
            $errors[] = 'Scan error in ' . $root . ': ' . $e->getMessage();
        }
    }

    /* ── Step 3: Mark files that no longer exist on disk as is_missing=1 ──── */
    try {
        $rows = $pdo->query('SELECT id, file_path FROM tracks WHERE is_missing=0 OR is_missing IS NULL')
                    ->fetchAll(\PDO::FETCH_ASSOC);
        $mark_stmt = $pdo->prepare('UPDATE tracks SET is_missing=1 WHERE id=?');
        foreach ($rows as $row) {
            if (!file_exists($row['file_path'])) {
                $mark_stmt->execute([$row['id']]);
                $missing++;
            }
        }
    } catch (\Exception $e) {
        $errors[] = 'Missing-check error: ' . $e->getMessage();
    }

    /* ── Step 4: Find duplicate entries (same title + artist + duration_ms) ─ */
    try {
        $dupe_res = $pdo->query(
            'SELECT COUNT(*) AS cnt, title, artist, duration_ms
             FROM tracks
             WHERE is_missing = 0
             GROUP BY title, artist, duration_ms
             HAVING cnt > 1'
        )->fetchAll(\PDO::FETCH_ASSOC);
        $dupes = count($dupe_res);
    } catch (\Exception $e) {
        $errors[] = 'Duplicate-check error: ' . $e->getMessage();
    }

    mc1_log(4, 'sync_library', json_encode([
        'added' => $added, 'skipped' => $skipped,
        'missing' => $missing, 'dupes' => $dupes,
    ]));
    mc1_api_respond([
        'ok'             => true,
        'added'          => $added,
        'skipped'        => $skipped,
        'missing_marked' => $missing,
        'dupes_found'    => $dupes,
        'errors'         => $errors,
    ]);
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: remove_missing — Delete all tracks flagged as is_missing=1
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'remove_missing') {
    try {
        $pdo     = mc1_db('mcaster1_media');
        $ids_st  = $pdo->query('SELECT id FROM tracks WHERE is_missing=1');
        $ids     = array_column($ids_st->fetchAll(\PDO::FETCH_ASSOC), 'id');
        $deleted = 0;
        if (!empty($ids)) {
            $ph = implode(',', array_fill(0, count($ids), '?'));
            $pdo->prepare("DELETE FROM track_categories WHERE track_id IN ($ph)")->execute($ids);
            $pdo->prepare("DELETE FROM playlist_tracks  WHERE track_id IN ($ph)")->execute($ids);
            $pdo->prepare("DELETE FROM tracks           WHERE id        IN ($ph)")->execute($ids);
            $deleted = count($ids);
        }
        mc1_log(4, 'remove_missing', json_encode(['deleted' => $deleted]));
        mc1_api_respond(['ok' => true, 'deleted' => $deleted]);
    } catch (\Exception $e) {
        mc1_api_respond(['error' => 'Remove missing failed: ' . $e->getMessage()], 500);
    }
    return;
}

mc1_api_respond(['error' => 'Unknown action: ' . $action], 400);
