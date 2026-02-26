<?php
/**
 * artwork.php — Cover Art Image Server
 *
 * File:    src/linux/web_ui/app/api/artwork.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-02-23
 * Purpose: We serve cover art images fetched from MusicBrainz/Cover Art Archive.
 *          GET  ?h={sha1}         — serve raw image bytes from cover_art DB table or fallback file
 *          POST {action:'info'}   — return artwork metadata for a track_id (JSON)
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We use Mc1Db trait for all database access
 *  - We use first-person plural in all comments
 *  - We use raw SQL only (no ORMs or 3rd-party libs)
 *
 * CHANGELOG:
 *  2026-02-23  v1.0.0  Initial implementation — artwork serving + cover_art table bootstrap
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/../inc/mc1_config.php';
require_once __DIR__ . '/../inc/db.php';
require_once __DIR__ . '/../inc/traits.db.class.php';
require_once __DIR__ . '/../inc/logger.php';
require_once __DIR__ . '/../inc/auth.php';

/* ── Bootstrap the cover_art table if it does not exist yet ─────────────── */
/*
 * We run this CREATE TABLE IF NOT EXISTS on every request so any environment
 * that hasn't run a separate migration script will still work automatically.
 */
(function() {
    try {
        $pdo = mc1_db('mcaster1_media');
        $pdo->exec("
            CREATE TABLE IF NOT EXISTS cover_art (
                hash      CHAR(40)          NOT NULL PRIMARY KEY  COMMENT 'SHA1 hex of raw image bytes',
                data      MEDIUMBLOB        NOT NULL              COMMENT 'Raw JPEG/PNG image data — up to 16 MB',
                mime_type VARCHAR(50)       NOT NULL DEFAULT 'image/jpeg',
                width     SMALLINT UNSIGNED          DEFAULT NULL,
                height    SMALLINT UNSIGNED          DEFAULT NULL,
                source    VARCHAR(512)               DEFAULT NULL  COMMENT 'Source URL (MusicBrainz CAA or user upload)',
                created_at DATETIME         NOT NULL DEFAULT CURRENT_TIMESTAMP
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
            COMMENT='We store fetched album art here, referenced by tracks.cover_art_hash'
        ");
    } catch (Exception $e) {
        /* We log the error but do not block the request — image serving can still work from files */
        mc1_log(2, 'cover_art table bootstrap failed', json_encode(['err' => $e->getMessage()]));
    }
})();

/* ── Ensure our local artwork directory exists ───────────────────────────── */
if (!is_dir(MC1_ARTWORK_DIR)) {
    @mkdir(MC1_ARTWORK_DIR, 0755, true);
}

/* ──────────────────────────────────────────────────────────────────────────
 * GET request — serve a cover art image by hash
 * We check the DB first, fall back to the filesystem file.
 * We do NOT require auth for image serving (artwork is non-sensitive,
 * and <img> tags in the browser can't easily pass cookies for sub-resources).
 * ────────────────────────────────────────────────────────────────────────── */
if ($_SERVER['REQUEST_METHOD'] === 'GET') {
    $hash = isset($_GET['h']) ? preg_replace('/[^a-f0-9]/i', '', (string)$_GET['h']) : '';

    if (strlen($hash) !== 40) {
        http_response_code(400);
        header('Content-Type: application/json');
        echo json_encode(['error' => 'Invalid or missing hash parameter']);
        return;
    }

    /* We check the local file cache first — it is faster than a DB MEDIUMBLOB read */
    $file_path = MC1_ARTWORK_DIR . '/' . $hash . '.jpg';
    if (is_readable($file_path)) {
        $mime = 'image/jpeg';
        /* We set long cache headers since artwork rarely changes */
        header('Content-Type: ' . $mime);
        header('Cache-Control: public, max-age=86400');
        header('Content-Length: ' . filesize($file_path));
        header('X-Artwork-Source: file');
        readfile($file_path);
        return;
    }

    /* We fall back to the DB MEDIUMBLOB if the file is missing */
    try {
        $pdo = mc1_db('mcaster1_media');
        $stmt = $pdo->prepare('SELECT data, mime_type FROM cover_art WHERE hash = ? LIMIT 1');
        $stmt->execute([$hash]);
        $row = $stmt->fetch(\PDO::FETCH_ASSOC);

        if ($row && !empty($row['data'])) {
            $mime = $row['mime_type'] ?: 'image/jpeg';
            /* We also write to the file cache so subsequent requests are faster */
            @file_put_contents($file_path, $row['data']);
            header('Content-Type: ' . $mime);
            header('Cache-Control: public, max-age=86400');
            header('Content-Length: ' . strlen($row['data']));
            header('X-Artwork-Source: db');
            echo $row['data'];
            return;
        }
    } catch (Exception $e) {
        mc1_log(2, 'artwork.php DB read failed', json_encode(['hash' => $hash, 'err' => $e->getMessage()]));
    }

    /* We return a 404 — no artwork found for this hash */
    http_response_code(404);
    header('Content-Type: application/json');
    echo json_encode(['error' => 'Artwork not found', 'hash' => $hash]);
    return;
}

/* ──────────────────────────────────────────────────────────────────────────
 * POST request — JSON API actions
 * We require authentication for all POST actions (write/query operations).
 * ────────────────────────────────────────────────────────────────────────── */
if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    http_response_code(405);
    header('Content-Type: application/json');
    echo json_encode(['error' => 'Method not allowed']);
    return;
}

header('Content-Type: application/json');

/* We gate all POST actions behind C++ session auth */
if (!mc1_is_authed()) {
    mc1_api_respond(['error' => 'Unauthorized'], 403);
    return;
}

require_once __DIR__ . '/../inc/user_auth.php';

$raw  = (string)file_get_contents('php://input');
$body = json_decode($raw, true) ?: [];
$action = (string)($body['action'] ?? '');

/* ── action: info — return artwork metadata for a track ─────────────────── */
if ($action === 'info') {
    $track_id = (int)($body['track_id'] ?? 0);
    if ($track_id < 1) {
        mc1_api_respond(['error' => 'track_id required'], 400);
        return;
    }

    try {
        $pdo  = mc1_db('mcaster1_media');
        /* We join tracks with cover_art to return full artwork metadata */
        $stmt = $pdo->prepare(
            'SELECT t.id, t.cover_art_hash, c.mime_type, c.width, c.height, c.source, c.created_at
             FROM tracks t
             LEFT JOIN cover_art c ON c.hash = t.cover_art_hash
             WHERE t.id = ?
             LIMIT 1'
        );
        $stmt->execute([$track_id]);
        $row = $stmt->fetch(\PDO::FETCH_ASSOC);

        if (!$row) {
            mc1_api_respond(['error' => 'Track not found'], 404);
            return;
        }

        $hash = $row['cover_art_hash'] ?? '';
        mc1_api_respond([
            'ok'       => true,
            'track_id' => $track_id,
            'has_art'  => !empty($hash),
            'hash'     => $hash,
            'url'      => $hash ? '/app/api/artwork.php?h=' . $hash : null,
            'mime'     => $row['mime_type'],
            'width'    => $row['width'] ? (int)$row['width'] : null,
            'height'   => $row['height'] ? (int)$row['height'] : null,
            'source'   => $row['source'],
            'fetched'  => $row['created_at'],
        ]);
        return;
    } catch (Exception $e) {
        mc1_log(2, 'artwork.php info failed', json_encode(['err' => $e->getMessage()]));
        mc1_api_respond(['error' => 'Database error'], 500);
        return;
    }
}

/* We return 400 for unknown actions */
mc1_api_respond(['error' => 'Unknown action: ' . $action], 400);
