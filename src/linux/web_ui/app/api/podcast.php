<?php
/**
 * podcast.php — Podcast & Archive Management API
 *
 * File:    src/linux/web_ui/app/api/podcast.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Purpose: We provide CRUD for podcast shows and episodes, RSS feed generation,
 *          and archive directory scanning for import into episodes.
 *
 * Actions (all POST JSON):
 *  list_shows       — List all podcast shows
 *  get_show         — Single show details by id
 *  create_show      — Create new show (title, description, author, category, language)
 *  update_show      — Update show metadata
 *  delete_show      — Delete show + unlink episodes
 *  list_episodes    — List episodes for a show (with pagination)
 *  get_episode      — Single episode details
 *  create_episode   — Create episode from existing archive file
 *  update_episode   — Update episode metadata
 *  delete_episode   — Delete episode (optionally delete file)
 *  publish_episode  — Set is_published=true, set published_at
 *  unpublish_episode— Set is_published=false
 *  generate_rss     — Generate iTunes-compatible RSS XML for a show
 *  scan_archives    — Scan archive directory for unlinked recordings
 *
 * Chapter embedding & export:
 *  embed_chapters              — Embed chapter atoms into MP4/M4A via ffmpeg FFMETADATA
 *  generate_chapters_json      — Generate Podcasting 2.0 JSON chapters file + save to disk
 *  generate_youtube_description — Generate YouTube-compatible timestamp description text
 *
 * Public GET endpoints (no auth):
 *  GET ?action=chapters_json&episode_id=N — Serve Podcasting 2.0 JSON chapters (for RSS)
 *
 * Phase PC-3 — Multi-platform publishing:
 *  list_targets       — List publish targets for a show
 *  create_target      — Add platform target (platform, name, credentials, config)
 *  update_target      — Update target settings
 *  delete_target      — Remove a publish target
 *  publish_to_targets — Queue episode for publishing to selected targets
 *  schedule_publish   — Schedule episode for future publish (set scheduled_at)
 *  get_publish_status — Check publish queue status for an episode
 *  cancel_publish     — Cancel a pending/scheduled publish queue item
 *  retry_publish      — Retry a failed publish queue item
 *  process_queue      — Cron-compatible: process scheduled items whose time has arrived
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We use Mc1Db trait for all database access
 *  - We use first-person plural throughout all comments
 *  - We use raw SQL only, no ORMs
 *  - We use mc1_api_respond() for all JSON responses
 *  - We use mc1_is_authed() for auth gate
 */

define('MC1_BOOT', true);
$API_VERSION = '2.0.1';
require_once __DIR__ . '/../inc/mc1_config.php';
require_once __DIR__ . '/../inc/db.php';
require_once __DIR__ . '/../inc/traits.db.class.php';
require_once __DIR__ . '/../inc/logger.php';
require_once __DIR__ . '/../inc/auth.php';
require_once __DIR__ . '/../inc/user_auth.php';

/* ── Public download endpoint (no auth — podcast clients need access) ── */
if ($_SERVER['REQUEST_METHOD'] === 'GET' && ($_GET['action'] ?? '') === 'download') {
    $episode_id = (int)($_GET['episode_id'] ?? 0);
    if ($episode_id < 1) {
        http_response_code(400);
        header('Content-Type: application/json');
        echo json_encode(['error' => 'Missing episode_id']);
        return;
    }
    PodcastApi::handleDownload($episode_id);
    return;
}

/* ── Public chapters JSON endpoint (no auth — RSS readers need access) ── */
if ($_SERVER['REQUEST_METHOD'] === 'GET' && ($_GET['action'] ?? '') === 'chapters_json') {
    $episode_id = (int)($_GET['episode_id'] ?? 0);
    if ($episode_id < 1) {
        http_response_code(400);
        header('Content-Type: application/json');
        echo json_encode(['error' => 'Missing episode_id']);
        return;
    }
    PodcastApi::serveChaptersJson($episode_id);
    return;
}

header('Content-Type: application/json');

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    mc1_api_respond(['error' => 'POST required'], 405);
    return;
}

/* ── Parse request body ── */
$raw  = file_get_contents('php://input');
$data = json_decode($raw, true);
if (!is_array($data) || empty($data['action'])) {
    mc1_api_respond(['error' => 'Missing action'], 400);
    return;
}

$action = $data['action'];

/* ── Auth gate ── */
/* We allow process_queue from localhost without auth for cron usage:
 * curl -s http://127.0.0.1:8330/app/api/podcast.php -d '{"action":"process_queue","_internal":"1"}' */
$is_cron = (
    $action === 'process_queue'
    && !empty($data['_internal'])
    && in_array($_SERVER['REMOTE_ADDR'] ?? '', ['127.0.0.1', '::1'])
);
if (!$is_cron && !mc1_is_authed()) {
    mc1_api_respond(['error' => 'Unauthorized'], 403);
    return;
}

/* ── Podcast helper class ── */
class PodcastApi {
    use Mc1Db;

    const DB = 'mcaster1_media';

    /* ── Default archive directory ── */
    public static function archiveDir(): string
    {
        // We check for MC1_ARCHIVE_DIR constant first, then fall back to a default
        if (defined('MC1_ARCHIVE_DIR')) {
            return MC1_ARCHIVE_DIR;
        }
        return '/var/www/mcaster1.com/Mcaster1DSPEncoder/archives';
    }

    /* ── SHOWS ── */

    public static function listShows(): array
    {
        $shows = self::rows(self::DB,
            "SELECT s.*, (SELECT COUNT(*) FROM podcast_episodes e WHERE e.show_id = s.id) AS episode_count,
                    (SELECT COUNT(*) FROM podcast_episodes e WHERE e.show_id = s.id AND e.is_published = 1) AS published_count
             FROM podcast_shows s ORDER BY s.updated_at DESC"
        );
        return ['ok' => true, 'shows' => $shows];
    }

    public static function getShow(array $data): array
    {
        $id = (int)($data['id'] ?? 0);
        if ($id < 1) return ['error' => 'Missing show id'];

        $show = self::row(self::DB,
            "SELECT * FROM podcast_shows WHERE id = ?", [$id]);
        if (!$show) return ['error' => 'Show not found'];

        return ['ok' => true, 'show' => $show];
    }

    public static function createShow(array $data): array
    {
        $title = trim($data['title'] ?? '');
        if ($title === '') return ['error' => 'Title is required'];

        self::run(self::DB,
            "INSERT INTO podcast_shows (title, description, author, category, language, cover_art_path, website_url, feed_url)
             VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
            [
                $title,
                $data['description'] ?? '',
                $data['author'] ?? '',
                $data['category'] ?? 'Technology',
                $data['language'] ?? 'en',
                $data['cover_art_path'] ?? null,
                $data['website_url'] ?? '',
                $data['feed_url'] ?? '',
            ]
        );
        $id = self::lastId(self::DB);
        return ['ok' => true, 'id' => (int)$id, 'message' => 'Show created'];
    }

    public static function updateShow(array $data): array
    {
        $id = (int)($data['id'] ?? 0);
        if ($id < 1) return ['error' => 'Missing show id'];

        $title = trim($data['title'] ?? '');
        if ($title === '') return ['error' => 'Title is required'];

        self::run(self::DB,
            "UPDATE podcast_shows SET title=?, description=?, author=?, category=?,
             language=?, cover_art_path=?, website_url=?, feed_url=?, is_active=?,
             site_enabled=?, site_theme=?, site_accent_color=?, site_welcome_message=?,
             site_custom_domain=?
             WHERE id=?",
            [
                $title,
                $data['description'] ?? '',
                $data['author'] ?? '',
                $data['category'] ?? 'Technology',
                $data['language'] ?? 'en',
                $data['cover_art_path'] ?? null,
                $data['website_url'] ?? '',
                $data['feed_url'] ?? '',
                (int)($data['is_active'] ?? 1),
                (int)($data['site_enabled'] ?? 1),
                $data['site_theme'] ?? 'clean_light',
                $data['site_accent_color'] ?? '#14b8a6',
                $data['site_welcome_message'] ?? null,
                $data['site_custom_domain'] ?? null,
                $id,
            ]
        );
        return ['ok' => true, 'message' => 'Show updated'];
    }

    public static function deleteShow(array $data): array
    {
        $id = (int)($data['id'] ?? 0);
        if ($id < 1) return ['error' => 'Missing show id'];

        // We unlink episodes from the show but do not delete files
        self::run(self::DB, "DELETE FROM podcast_episodes WHERE show_id = ?", [$id]);
        self::run(self::DB, "DELETE FROM podcast_shows WHERE id = ?", [$id]);

        return ['ok' => true, 'message' => 'Show and episodes deleted'];
    }

    /* ── EPISODES ── */

    public static function listEpisodes(array $data): array
    {
        $show_id = (int)($data['show_id'] ?? 0);
        $page    = max(1, (int)($data['page'] ?? 1));
        $limit   = min(100, max(10, (int)($data['limit'] ?? 50)));
        $offset  = ($page - 1) * $limit;

        $where  = '';
        $params = [];
        if ($show_id > 0) {
            $where  = 'WHERE e.show_id = ?';
            $params = [$show_id];
        }

        $total = (int)self::scalar(self::DB,
            "SELECT COUNT(*) FROM podcast_episodes e $where", $params);

        $episodes = self::rows(self::DB,
            "SELECT e.*, s.title AS show_title
             FROM podcast_episodes e
             LEFT JOIN podcast_shows s ON s.id = e.show_id
             $where
             ORDER BY e.episode_number DESC, e.created_at DESC
             LIMIT $limit OFFSET $offset",
            $params
        );

        return [
            'ok'       => true,
            'episodes' => $episodes,
            'total'    => $total,
            'page'     => $page,
            'pages'    => max(1, (int)ceil($total / $limit)),
        ];
    }

    public static function getEpisode(array $data): array
    {
        $id = (int)($data['id'] ?? 0);
        if ($id < 1) return ['error' => 'Missing episode id'];

        $ep = self::row(self::DB,
            "SELECT e.*, s.title AS show_title
             FROM podcast_episodes e
             LEFT JOIN podcast_shows s ON s.id = e.show_id
             WHERE e.id = ?", [$id]);
        if (!$ep) return ['error' => 'Episode not found'];

        return ['ok' => true, 'episode' => $ep];
    }

    public static function createEpisode(array $data): array
    {
        $show_id   = (int)($data['show_id'] ?? 0);
        $title     = trim($data['title'] ?? '');
        $file_path = trim($data['file_path'] ?? '');

        if ($show_id < 1) return ['error' => 'Missing show_id'];
        if ($title === '') return ['error' => 'Title is required'];
        if ($file_path === '') return ['error' => 'File path is required'];

        // We verify the file exists
        if (!file_exists($file_path)) {
            return ['error' => 'File not found: ' . basename($file_path)];
        }

        $file_size = filesize($file_path);
        $format    = strtolower(pathinfo($file_path, PATHINFO_EXTENSION));
        if ($format === '') $format = 'mp3';

        // We try to get duration via ffprobe if available
        $duration_sec = (int)($data['duration_sec'] ?? 0);
        if ($duration_sec < 1 && function_exists('exec')) {
            $cmd = 'ffprobe -v quiet -show_entries format=duration -of csv=p=0 '
                 . escapeshellarg($file_path) . ' 2>/dev/null';
            $out = '';
            @exec($cmd, $lines);
            if (!empty($lines[0])) {
                $duration_sec = (int)round((float)$lines[0]);
            }
        }

        // We auto-compute episode number if not provided
        $ep_num = $data['episode_number'] ?? null;
        if ($ep_num === null || $ep_num === '') {
            $max_num = (int)self::scalar(self::DB,
                "SELECT COALESCE(MAX(episode_number), 0) FROM podcast_episodes WHERE show_id = ?",
                [$show_id]
            );
            $ep_num = $max_num + 1;
        }

        self::run(self::DB,
            "INSERT INTO podcast_episodes
             (show_id, title, description, file_path, file_size_bytes, duration_sec,
              format, bitrate_kbps, season, episode_number, slot_id, tags)
             VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            [
                $show_id,
                $title,
                $data['description'] ?? '',
                $file_path,
                $file_size,
                $duration_sec,
                $format,
                (int)($data['bitrate_kbps'] ?? 128),
                $data['season'] ?? null,
                (int)$ep_num,
                $data['slot_id'] ?? null,
                $data['tags'] ?? null,
            ]
        );
        $id = self::lastId(self::DB);
        return ['ok' => true, 'id' => (int)$id, 'message' => 'Episode created'];
    }

    public static function updateEpisode(array $data): array
    {
        $id = (int)($data['id'] ?? 0);
        if ($id < 1) return ['error' => 'Missing episode id'];

        $title = trim($data['title'] ?? '');
        if ($title === '') return ['error' => 'Title is required'];

        self::run(self::DB,
            "UPDATE podcast_episodes SET
             title=?, description=?, season=?, episode_number=?, tags=?,
             bitrate_kbps=?, format=?
             WHERE id=?",
            [
                $title,
                $data['description'] ?? '',
                $data['season'] ?? null,
                $data['episode_number'] ?? null,
                $data['tags'] ?? null,
                (int)($data['bitrate_kbps'] ?? 128),
                $data['format'] ?? 'mp3',
                $id,
            ]
        );
        return ['ok' => true, 'message' => 'Episode updated'];
    }

    public static function deleteEpisode(array $data): array
    {
        $id = (int)($data['id'] ?? 0);
        if ($id < 1) return ['error' => 'Missing episode id'];

        $delete_file = !empty($data['delete_file']);

        if ($delete_file) {
            $ep = self::row(self::DB,
                "SELECT file_path FROM podcast_episodes WHERE id = ?", [$id]);
            if ($ep && !empty($ep['file_path']) && file_exists($ep['file_path'])) {
                @unlink($ep['file_path']);
            }
        }

        self::run(self::DB, "DELETE FROM podcast_episodes WHERE id = ?", [$id]);
        return ['ok' => true, 'message' => 'Episode deleted'];
    }

    public static function publishEpisode(array $data): array
    {
        $id = (int)($data['id'] ?? 0);
        if ($id < 1) return ['error' => 'Missing episode id'];

        self::run(self::DB,
            "UPDATE podcast_episodes SET is_published = 1, published_at = NOW() WHERE id = ?",
            [$id]
        );
        return ['ok' => true, 'message' => 'Episode published'];
    }

    public static function unpublishEpisode(array $data): array
    {
        $id = (int)($data['id'] ?? 0);
        if ($id < 1) return ['error' => 'Missing episode id'];

        self::run(self::DB,
            "UPDATE podcast_episodes SET is_published = 0 WHERE id = ?", [$id]
        );
        return ['ok' => true, 'message' => 'Episode unpublished'];
    }

    /* ── RSS FEED GENERATION ── */

    public static function generateRss(array $data): array
    {
        $show_id = (int)($data['show_id'] ?? 0);
        if ($show_id < 1) return ['error' => 'Missing show_id'];

        $show = self::row(self::DB,
            "SELECT * FROM podcast_shows WHERE id = ?", [$show_id]);
        if (!$show) return ['error' => 'Show not found'];

        $episodes = self::rows(self::DB,
            "SELECT * FROM podcast_episodes
             WHERE show_id = ? AND is_published = 1
             ORDER BY published_at DESC",
            [$show_id]
        );

        // We determine the base URL for enclosures from the show feed_url
        // or fall back to the server host
        $base_url = rtrim($show['website_url'] ?: 'https://encoder.mcaster1.com:8344', '/');
        $cover_url = '';
        if (!empty($show['cover_art_path'])) {
            // If cover_art_path is a URL, use it directly; otherwise construct one
            if (str_starts_with($show['cover_art_path'], 'http')) {
                $cover_url = $show['cover_art_path'];
            } else {
                $cover_url = $base_url . '/podcast/cover/' . $show_id;
            }
        }

        $xml = '<?xml version="1.0" encoding="UTF-8"?>' . "\n";
        $xml .= '<rss version="2.0" xmlns:itunes="http://www.itunes.com/dtds/podcast-1.0.dtd" '
              . 'xmlns:content="http://purl.org/rss/1.0/modules/content/">' . "\n";
        $xml .= "  <channel>\n";
        $xml .= '    <title>' . self::xmlEscape($show['title']) . "</title>\n";
        $xml .= '    <description>' . self::xmlEscape($show['description'] ?? '') . "</description>\n";
        $xml .= '    <language>' . self::xmlEscape($show['language'] ?? 'en') . "</language>\n";
        $xml .= '    <itunes:author>' . self::xmlEscape($show['author'] ?? '') . "</itunes:author>\n";
        $xml .= '    <itunes:category text="' . self::xmlAttrEscape($show['category'] ?? 'Technology') . '"/>' . "\n";

        if (!empty($show['website_url'])) {
            $xml .= '    <link>' . self::xmlEscape($show['website_url']) . "</link>\n";
        }

        if ($cover_url !== '') {
            $xml .= "    <image>\n";
            $xml .= '      <url>' . self::xmlEscape($cover_url) . "</url>\n";
            $xml .= '      <title>' . self::xmlEscape($show['title']) . "</title>\n";
            if (!empty($show['website_url'])) {
                $xml .= '      <link>' . self::xmlEscape($show['website_url']) . "</link>\n";
            }
            $xml .= "    </image>\n";
            $xml .= '    <itunes:image href="' . self::xmlAttrEscape($cover_url) . '"/>' . "\n";
        }

        // We check if this show has any video episodes to set serial type
        $has_video = false;
        foreach ($episodes as $ep) {
            if (self::isVideoFormat($ep['format'] ?? '')) {
                $has_video = true;
                break;
            }
        }
        if ($has_video) {
            $xml .= '    <itunes:type>serial</itunes:type>' . "\n";
        }

        foreach ($episodes as $ep) {
            // We construct the media URL for the enclosure
            $media_url = $base_url . '/app/api/podcast.php?action=download&episode_id=' . (int)$ep['id'];
            $fmt       = $ep['format'] ?? 'mp3';
            $mime_type = self::mimeForFormat($fmt);

            $xml .= "    <item>\n";
            $xml .= '      <title>' . self::xmlEscape($ep['title']) . "</title>\n";
            $xml .= '      <description>' . self::xmlEscape($ep['description'] ?? '') . "</description>\n";
            $xml .= '      <enclosure url="' . self::xmlAttrEscape($media_url) . '" '
                  . 'length="' . (int)$ep['file_size_bytes'] . '" '
                  . 'type="' . $mime_type . '"/>' . "\n";

            $xml .= '      <itunes:episodeType>full</itunes:episodeType>' . "\n";

            // We format duration as HH:MM:SS
            $dur = (int)$ep['duration_sec'];
            $xml .= '      <itunes:duration>' . sprintf('%02d:%02d:%02d',
                (int)($dur / 3600), (int)(($dur % 3600) / 60), $dur % 60) . "</itunes:duration>\n";

            if ($ep['season'] !== null) {
                $xml .= '      <itunes:season>' . (int)$ep['season'] . "</itunes:season>\n";
            }
            if ($ep['episode_number'] !== null) {
                $xml .= '      <itunes:episode>' . (int)$ep['episode_number'] . "</itunes:episode>\n";
            }

            // We use RFC 2822 date format for pubDate
            if (!empty($ep['published_at'])) {
                $ts = strtotime($ep['published_at']);
                if ($ts) {
                    $xml .= '      <pubDate>' . gmdate('D, d M Y H:i:s \G\M\T', $ts) . "</pubDate>\n";
                }
            }

            $xml .= '      <guid isPermaLink="false">mc1-podcast-ep-' . (int)$ep['id'] . "</guid>\n";
            $xml .= "    </item>\n";
        }

        $xml .= "  </channel>\n";
        $xml .= "</rss>\n";

        return ['ok' => true, 'rss' => $xml, 'episode_count' => count($episodes)];
    }

    /* ── ARCHIVE SCANNER ── */

    public static function scanArchives(array $data): array
    {
        $dir = trim($data['directory'] ?? self::archiveDir());

        if (!is_dir($dir)) {
            return ['error' => 'Archive directory not found: ' . basename($dir)];
        }

        // We find all audio and video files in the directory
        $extensions = ['mp3', 'wav', 'ogg', 'opus', 'flac', 'aac', 'm4a',
                       'mp4', 'webm', 'mkv'];
        $files = [];
        $limit = 500;

        $iter = new RecursiveIteratorIterator(
            new RecursiveDirectoryIterator($dir, RecursiveDirectoryIterator::SKIP_DOTS),
            RecursiveIteratorIterator::SELF_FIRST
        );

        // We get all file_paths already linked to episodes
        $linked = [];
        $rows = self::rows(self::DB, "SELECT file_path FROM podcast_episodes");
        foreach ($rows as $r) {
            $linked[$r['file_path']] = true;
        }

        foreach ($iter as $file) {
            if (count($files) >= $limit) break;
            if (!$file->isFile()) continue;

            $ext = strtolower($file->getExtension());
            if (!in_array($ext, $extensions)) continue;

            $path = $file->getPathname();
            if (isset($linked[$path])) continue; // We skip already linked files

            $info = [
                'file_path'  => $path,
                'filename'   => $file->getFilename(),
                'format'     => $ext,
                'file_size'  => $file->getSize(),
                'modified'   => date('Y-m-d H:i:s', $file->getMTime()),
                'duration_sec' => 0,
            ];

            // We try to get duration via ffprobe
            if (function_exists('exec')) {
                $cmd = 'ffprobe -v quiet -show_entries format=duration -of csv=p=0 '
                     . escapeshellarg($path) . ' 2>/dev/null';
                $lines = [];
                @exec($cmd, $lines);
                if (!empty($lines[0])) {
                    $info['duration_sec'] = (int)round((float)$lines[0]);
                }
            }

            $files[] = $info;
        }

        return [
            'ok'    => true,
            'files' => $files,
            'directory' => $dir,
            'total' => count($files),
        ];
    }

    /* ── MARKERS ── */

    public static function listMarkers(array $data): array
    {
        $episode_id = (int)($data['episode_id'] ?? 0);
        if ($episode_id < 1) return ['error' => 'Missing episode_id'];

        $markers = self::rows(self::DB,
            "SELECT * FROM episode_markers WHERE episode_id = ? ORDER BY timestamp_ms ASC",
            [$episode_id]
        );
        return ['ok' => true, 'markers' => $markers];
    }

    public static function deleteMarker(array $data): array
    {
        $id = (int)($data['id'] ?? 0);
        if ($id < 1) return ['error' => 'Missing marker id'];

        self::run(self::DB, "DELETE FROM episode_markers WHERE id = ?", [$id]);
        return ['ok' => true, 'message' => 'Marker deleted'];
    }

    public static function updateMarker(array $data): array
    {
        $id = (int)($data['id'] ?? 0);
        if ($id < 1) return ['error' => 'Missing marker id'];

        $title = trim($data['title'] ?? '');
        if ($title === '') return ['error' => 'Title is required'];

        self::run(self::DB,
            "UPDATE episode_markers SET title=?, marker_type=?, url=?, image_url=? WHERE id=?",
            [
                $title,
                $data['marker_type'] ?? 'chapter',
                $data['url'] ?? '',
                $data['image_url'] ?? '',
                $id,
            ]
        );
        return ['ok' => true, 'message' => 'Marker updated'];
    }

    public static function addMarker(array $data): array
    {
        $episode_id   = (int)($data['episode_id'] ?? 0);
        $timestamp_ms = (int)($data['timestamp_ms'] ?? -1);
        $title        = trim($data['title'] ?? '');

        if ($episode_id < 1) return ['error' => 'Missing episode_id'];
        if ($timestamp_ms < 0) return ['error' => 'Missing timestamp_ms'];
        if ($title === '') return ['error' => 'Title is required'];

        self::run(self::DB,
            "INSERT INTO episode_markers (episode_id, timestamp_ms, title, marker_type, url, image_url)
             VALUES (?, ?, ?, ?, ?, ?)",
            [
                $episode_id,
                $timestamp_ms,
                $title,
                $data['marker_type'] ?? 'chapter',
                $data['url'] ?? '',
                $data['image_url'] ?? '',
            ]
        );
        $id = self::lastId(self::DB);
        return ['ok' => true, 'id' => (int)$id, 'message' => 'Marker created'];
    }

    /* ── CHAPTER EMBEDDING & EXPORT ── */

    /**
     * We build a Podcasting 2.0 JSON chapters object from episode_markers.
     * Spec: https://github.com/Podcastindex-org/podcast-namespace/blob/main/chapters/jsonChapters.md
     */
    public static function buildChaptersJson(int $episode_id): array
    {
        $markers = self::rows(self::DB,
            "SELECT * FROM episode_markers
             WHERE episode_id = ? AND marker_type = 'chapter'
             ORDER BY timestamp_ms ASC",
            [$episode_id]
        );

        $chapters = [];
        foreach ($markers as $m) {
            $ch = [
                'startTime' => round((int)$m['timestamp_ms'] / 1000, 3),
                'title'     => $m['title'] ?? 'Chapter',
            ];
            if (!empty($m['image_url'])) {
                $ch['img'] = $m['image_url'];
            }
            if (!empty($m['url'])) {
                $ch['url'] = $m['url'];
            }
            $chapters[] = $ch;
        }

        return [
            'version'  => '1.2.0',
            'chapters' => $chapters,
        ];
    }

    /**
     * We serve a Podcasting 2.0 JSON chapters file publicly (no auth).
     * Called from the GET ?action=chapters_json&episode_id=N endpoint.
     */
    public static function serveChaptersJson(int $episode_id): void
    {
        $ep = self::row(self::DB,
            "SELECT id FROM podcast_episodes WHERE id = ?", [$episode_id]);
        if (!$ep) {
            http_response_code(404);
            header('Content-Type: application/json');
            echo json_encode(['error' => 'Episode not found']);
            return;
        }

        $json = self::buildChaptersJson($episode_id);

        header('Content-Type: application/json+chapters; charset=UTF-8');
        header('Cache-Control: public, max-age=300');
        header('Access-Control-Allow-Origin: *');
        echo json_encode($json, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES);
    }

    /**
     * We generate a Podcasting 2.0 JSON chapters file and save it to disk.
     * Returns the JSON data and file path.
     */
    public static function generateChaptersJson(array $data): array
    {
        $episode_id = (int)($data['episode_id'] ?? 0);
        if ($episode_id < 1) return ['error' => 'Missing episode_id'];

        $ep = self::row(self::DB,
            "SELECT * FROM podcast_episodes WHERE id = ?", [$episode_id]);
        if (!$ep) return ['error' => 'Episode not found'];

        $json = self::buildChaptersJson($episode_id);

        if (empty($json['chapters'])) {
            return ['error' => 'No chapter markers found for this episode'];
        }

        /* We save the JSON file alongside the episode audio or in the archive dir */
        $dir = !empty($ep['file_path']) && is_dir(dirname($ep['file_path']))
            ? dirname($ep['file_path'])
            : self::archiveDir();

        $filename = $episode_id . '_chapters.json';
        $filepath = $dir . '/' . $filename;
        $written = file_put_contents($filepath, json_encode($json, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES));

        if ($written === false) {
            return ['error' => 'Failed to write chapters file to ' . basename($dir)];
        }

        return [
            'ok'             => true,
            'chapters_json'  => $json,
            'file_path'      => $filepath,
            'chapter_count'  => count($json['chapters']),
            'message'        => 'Chapters JSON generated (' . count($json['chapters']) . ' chapters)',
        ];
    }

    /**
     * We embed chapter markers into an MP4/M4A file using ffmpeg FFMETADATA format.
     * For MP3 files we skip embedding (limited chapter support in ID3v2).
     */
    public static function embedChapters(array $data): array
    {
        $episode_id = (int)($data['episode_id'] ?? 0);
        if ($episode_id < 1) return ['error' => 'Missing episode_id'];

        $ep = self::row(self::DB,
            "SELECT * FROM podcast_episodes WHERE id = ?", [$episode_id]);
        if (!$ep) return ['error' => 'Episode not found'];

        $file_path = $ep['file_path'] ?? '';
        if (!file_exists($file_path)) {
            return ['error' => 'Source file not found: ' . basename($file_path)];
        }

        $format = strtolower($ep['format'] ?? pathinfo($file_path, PATHINFO_EXTENSION));

        /* We only support chapter embedding for MP4/M4A containers */
        $supported = ['mp4', 'm4a', 'mov'];
        if (!in_array($format, $supported)) {
            return ['error' => 'Chapter embedding is only supported for MP4/M4A/MOV files. This episode is ' . strtoupper($format) . '.'];
        }

        /* We check that ffmpeg is available */
        $ffmpegPath = '';
        if (function_exists('exec')) {
            $lines = [];
            @exec('which ffmpeg 2>/dev/null', $lines);
            $ffmpegPath = $lines[0] ?? '';
        }
        if ($ffmpegPath === '') {
            return ['error' => 'ffmpeg not found on this server'];
        }

        /* We load chapter markers */
        $markers = self::rows(self::DB,
            "SELECT * FROM episode_markers
             WHERE episode_id = ? AND marker_type = 'chapter'
             ORDER BY timestamp_ms ASC",
            [$episode_id]
        );

        if (empty($markers)) {
            return ['error' => 'No chapter markers found for this episode'];
        }

        /* We get the total duration in milliseconds */
        $duration_ms = (int)($ep['duration_sec'] ?? 0) * 1000;
        if ($duration_ms < 1) {
            /* We try to get duration via ffprobe */
            if (function_exists('exec')) {
                $dlines = [];
                @exec('ffprobe -v quiet -show_entries format=duration -of csv=p=0 '
                    . escapeshellarg($file_path) . ' 2>/dev/null', $dlines);
                if (!empty($dlines[0])) {
                    $duration_ms = (int)round((float)$dlines[0] * 1000);
                }
            }
        }

        /* We build the FFMETADATA file content */
        $meta = ";FFMETADATA1\n";
        for ($i = 0; $i < count($markers); $i++) {
            $start_ms = (int)$markers[$i]['timestamp_ms'];
            /* End time is either the next chapter start or the episode end */
            $end_ms = ($i + 1 < count($markers))
                ? (int)$markers[$i + 1]['timestamp_ms']
                : max($duration_ms, $start_ms + 1000);

            $title = str_replace(['=', ';', '#', '\\', "\n"], ['\\=', '\\;', '\\#', '\\\\', ' '], $markers[$i]['title'] ?? 'Chapter');

            $meta .= "[CHAPTER]\n";
            $meta .= "TIMEBASE=1/1000\n";
            $meta .= "START=" . $start_ms . "\n";
            $meta .= "END=" . $end_ms . "\n";
            $meta .= "title=" . $title . "\n";
        }

        /* We write the metadata to a temp file */
        $metaFile = sys_get_temp_dir() . '/mc1_chapters_' . $episode_id . '_' . time() . '.txt';
        if (file_put_contents($metaFile, $meta) === false) {
            return ['error' => 'Failed to write temporary metadata file'];
        }

        /* We create the output file path (same dir, with _chapters suffix) */
        $dir  = dirname($file_path);
        $base = pathinfo($file_path, PATHINFO_FILENAME);
        $ext  = pathinfo($file_path, PATHINFO_EXTENSION);
        $outputPath = $dir . '/' . $base . '_chapters.' . $ext;

        /* We run ffmpeg to embed chapters: copy streams, apply chapter metadata */
        $cmd = escapeshellarg($ffmpegPath) . ' -y'
             . ' -i ' . escapeshellarg($file_path)
             . ' -i ' . escapeshellarg($metaFile)
             . ' -map_metadata 1'
             . ' -c copy'
             . ' ' . escapeshellarg($outputPath)
             . ' 2>&1';

        $output = [];
        $retval = -1;
        if (function_exists('exec')) {
            @exec($cmd, $output, $retval);
        }

        /* We clean up the temp metadata file */
        @unlink($metaFile);

        if ($retval !== 0) {
            $errMsg = implode("\n", array_slice($output, -5));
            mc1_log(2, 'ffmpeg chapter embed failed: ' . $errMsg, 'podcast');
            return ['error' => 'FFmpeg chapter embedding failed (code ' . $retval . '): ' . $errMsg];
        }

        if (!file_exists($outputPath)) {
            return ['error' => 'Output file was not created'];
        }

        /* We update the episode record to point to the new file */
        $outSize = filesize($outputPath);
        self::run(self::DB,
            "UPDATE podcast_episodes SET file_path=?, file_size_bytes=? WHERE id=?",
            [$outputPath, $outSize, $episode_id]
        );

        return [
            'ok'              => true,
            'chapters_embedded' => count($markers),
            'file_path'       => $outputPath,
            'file_size'       => $outSize,
            'message'         => count($markers) . ' chapters embedded into ' . basename($outputPath),
        ];
    }

    /**
     * We generate YouTube-compatible timestamp format for video description.
     * YouTube auto-detects these as chapters when first timestamp is 0:00
     * and there are 3+ chapters with 10s minimum per chapter.
     */
    public static function generateYoutubeDescription(array $data): array
    {
        $episode_id = (int)($data['episode_id'] ?? 0);
        if ($episode_id < 1) return ['error' => 'Missing episode_id'];

        $ep = self::row(self::DB,
            "SELECT * FROM podcast_episodes WHERE id = ?", [$episode_id]);
        if (!$ep) return ['error' => 'Episode not found'];

        $markers = self::rows(self::DB,
            "SELECT * FROM episode_markers
             WHERE episode_id = ? AND marker_type = 'chapter'
             ORDER BY timestamp_ms ASC",
            [$episode_id]
        );

        if (empty($markers)) {
            return ['error' => 'No chapter markers found for this episode'];
        }

        /* We build YouTube timestamp lines.
         * YouTube requires first timestamp to be 0:00 for auto-detection. */
        $lines = [];
        $has_zero = false;

        foreach ($markers as $m) {
            $ms = (int)$m['timestamp_ms'];
            $sec = (int)floor($ms / 1000);
            $h = (int)floor($sec / 3600);
            $min = (int)floor(($sec % 3600) / 60);
            $s = $sec % 60;

            if ($sec === 0) $has_zero = true;

            /* We use H:MM:SS for episodes over 1 hour, otherwise M:SS */
            if ($h > 0) {
                $ts = $h . ':' . str_pad((string)$min, 2, '0', STR_PAD_LEFT) . ':' . str_pad((string)$s, 2, '0', STR_PAD_LEFT);
            } else {
                $ts = $min . ':' . str_pad((string)$s, 2, '0', STR_PAD_LEFT);
            }

            $lines[] = $ts . ' ' . ($m['title'] ?? 'Chapter');
        }

        /* We ensure first timestamp is 0:00 for YouTube auto-detection */
        if (!$has_zero && !empty($lines)) {
            array_unshift($lines, '0:00 Introduction');
        }

        $text = implode("\n", $lines);

        /* We include validation warnings */
        $warnings = [];
        if (count($lines) < 3) {
            $warnings[] = 'YouTube requires at least 3 chapters for auto-detection.';
        }

        return [
            'ok'             => true,
            'description'    => $text,
            'chapter_count'  => count($lines),
            'warnings'       => $warnings,
            'message'        => 'YouTube chapter description generated (' . count($lines) . ' chapters)',
        ];
    }

    /* ── EPISODE EXPORT (PC-2) ── */

    public static function exportEpisode(array $data): array
    {
        $episode_id = (int)($data['episode_id'] ?? 0);
        if ($episode_id < 1) return ['error' => 'Missing episode_id'];

        $format  = $data['format'] ?? 'mp3';
        $bitrate = $data['bitrate'] ?? '128k';
        $edl     = $data['edl'] ?? null;

        // We validate the format
        $validFormats = ['mp3', 'aac', 'opus', 'flac'];
        if (!in_array($format, $validFormats)) {
            return ['error' => 'Invalid format: ' . $format];
        }

        // We look up the episode to get the source file path
        $ep = self::row(self::DB,
            "SELECT * FROM podcast_episodes WHERE id = ?", [$episode_id]);
        if (!$ep) return ['error' => 'Episode not found'];

        $sourcePath = $ep['file_path'] ?? '';
        if (!file_exists($sourcePath)) {
            return ['error' => 'Source file not found: ' . basename($sourcePath)];
        }

        // We check that ffmpeg is available
        $ffmpegPath = '';
        if (function_exists('exec')) {
            $lines = [];
            @exec('which ffmpeg 2>/dev/null', $lines);
            $ffmpegPath = $lines[0] ?? '';
        }
        if ($ffmpegPath === '') {
            return ['error' => 'ffmpeg not found on this server'];
        }

        // We construct the output filename alongside the original
        $dir = dirname($sourcePath);
        $base = pathinfo($sourcePath, PATHINFO_FILENAME);
        $ext = match ($format) {
            'mp3'  => 'mp3',
            'aac'  => 'm4a',
            'opus' => 'opus',
            'flac' => 'flac',
            default => 'mp3',
        };
        $outputPath = $dir . '/' . $base . '_edited_' . date('Ymd_His') . '.' . $ext;

        // We build the ffmpeg filter chain from the EDL operations
        $filters = [];
        $operations = (is_array($edl) && !empty($edl['operations'])) ? $edl['operations'] : [];

        foreach ($operations as $op) {
            $type = $op['type'] ?? '';
            switch ($type) {
                case 'cut':
                    $startSec = round(($op['start_ms'] ?? 0) / 1000, 3);
                    $endSec   = round(($op['end_ms'] ?? 0) / 1000, 3);
                    $filters[] = "aselect='not(between(t\\," . $startSec . "\\," . $endSec . "))',asetpts=N/SR/TB";
                    break;

                case 'trim':
                    $startSec = round(($op['start_ms'] ?? 0) / 1000, 3);
                    $endSec   = round(($op['end_ms'] ?? 0) / 1000, 3);
                    $filters[] = "atrim=start=" . $startSec . ":end=" . $endSec . ",asetpts=N/SR/TB";
                    break;

                case 'silence':
                    $startSec = round(($op['start_ms'] ?? 0) / 1000, 3);
                    $endSec   = round(($op['end_ms'] ?? 0) / 1000, 3);
                    $filters[] = "volume=enable='between(t\\," . $startSec . "\\," . $endSec . ")':volume=0";
                    break;

                case 'fade_in':
                    $startSec = round(($op['start_ms'] ?? 0) / 1000, 3);
                    $durSec   = round(($op['duration_ms'] ?? 2000) / 1000, 3);
                    $curve = ($op['curve'] ?? 'linear') === 'exponential' ? 'exp' : 'tri';
                    $filters[] = "afade=t=in:st=" . $startSec . ":d=" . $durSec . ":curve=" . $curve;
                    break;

                case 'fade_out':
                    $startSec = round(($op['start_ms'] ?? 0) / 1000, 3);
                    $durSec   = round(($op['duration_ms'] ?? 2000) / 1000, 3);
                    $curve = ($op['curve'] ?? 'linear') === 'exponential' ? 'exp' : 'tri';
                    $filters[] = "afade=t=out:st=" . $startSec . ":d=" . $durSec . ":curve=" . $curve;
                    break;

                case 'normalize':
                    $targetDb = (float)($op['target_db'] ?? -1.0);
                    $filters[] = "loudnorm=I=-16:TP=" . $targetDb . ":LRA=11";
                    break;
            }
        }

        // We build the codec arguments
        $codecArgs = match ($format) {
            'mp3'  => '-c:a libmp3lame -b:a ' . escapeshellarg($bitrate),
            'aac'  => '-c:a aac -b:a ' . escapeshellarg($bitrate),
            'opus' => '-c:a libopus -b:a ' . escapeshellarg($bitrate),
            'flac' => '-c:a flac',
            default => '-c:a libmp3lame -b:a 128k',
        };

        $filterStr = '';
        if (!empty($filters)) {
            $filterStr = '-af ' . escapeshellarg(implode(',', $filters));
        }

        $cmd = escapeshellarg($ffmpegPath) . ' -y -i ' . escapeshellarg($sourcePath)
             . ' ' . $filterStr . ' ' . $codecArgs
             . ' ' . escapeshellarg($outputPath)
             . ' 2>&1';

        $output = [];
        $retval = -1;
        if (function_exists('exec')) {
            @exec($cmd, $output, $retval);
        }

        if ($retval !== 0) {
            $errMsg = implode("\n", array_slice($output, -5));
            mc1_log(2, 'ffmpeg export failed: ' . $errMsg, 'podcast');
            return ['error' => 'FFmpeg export failed (code ' . $retval . '): ' . $errMsg];
        }

        // We get the exported file size and duration
        $outSize = file_exists($outputPath) ? filesize($outputPath) : 0;
        $outDur  = 0;
        if (function_exists('exec') && file_exists($outputPath)) {
            $dlines = [];
            @exec('ffprobe -v quiet -show_entries format=duration -of csv=p=0 '
                . escapeshellarg($outputPath) . ' 2>/dev/null', $dlines);
            if (!empty($dlines[0])) {
                $outDur = (int)round((float)$dlines[0]);
            }
        }

        // We update the episode record with the new file path
        self::run(self::DB,
            "UPDATE podcast_episodes SET file_path=?, file_size_bytes=?, duration_sec=?, format=? WHERE id=?",
            [$outputPath, $outSize, $outDur, $format, $episode_id]
        );

        return [
            'ok'          => true,
            'output_file' => basename($outputPath),
            'file_size'   => $outSize,
            'duration_sec'=> $outDur,
            'message'     => 'Episode exported successfully',
        ];
    }

    /* ── PUBLISH TARGETS (PC-3) ── */

    public static function listTargets(array $data): array
    {
        $show_id = (int)($data['show_id'] ?? 0);
        if ($show_id < 1) return ['error' => 'Missing show_id'];

        $targets = self::rows(self::DB,
            "SELECT * FROM publish_targets WHERE show_id = ? ORDER BY platform_name ASC",
            [$show_id]
        );
        // We decode config_json for each target
        foreach ($targets as &$t) {
            $t['config'] = $t['config_json'] ? json_decode($t['config_json'], true) : [];
            unset($t['config_json']);
            // We mask API credentials — never send secrets to the browser
            if (!empty($t['api_key'])) $t['api_key'] = '***' . substr($t['api_key'], -4);
            if (!empty($t['api_secret'])) $t['api_secret'] = '****';
        }
        unset($t);
        return ['ok' => true, 'targets' => $targets];
    }

    public static function createTarget(array $data): array
    {
        $show_id       = (int)($data['show_id'] ?? 0);
        $platform      = $data['platform'] ?? '';
        $platform_name = trim($data['platform_name'] ?? '');

        if ($show_id < 1) return ['error' => 'Missing show_id'];
        if ($platform_name === '') return ['error' => 'Platform name is required'];

        $validPlatforms = ['rss','apple','spotify','google','amazon','youtube','podbean','buzzsprout','custom'];
        if (!in_array($platform, $validPlatforms)) return ['error' => 'Invalid platform'];

        $config_json = null;
        if (!empty($data['config']) && is_array($data['config'])) {
            $config_json = json_encode($data['config']);
        }

        self::run(self::DB,
            "INSERT INTO publish_targets (show_id, platform, platform_name, api_key, api_secret, feed_id, is_active, config_json)
             VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
            [
                $show_id,
                $platform,
                $platform_name,
                $data['api_key'] ?? null,
                $data['api_secret'] ?? null,
                $data['feed_id'] ?? null,
                (int)($data['is_active'] ?? 1),
                $config_json,
            ]
        );
        return ['ok' => true, 'id' => (int)self::lastId(self::DB), 'message' => 'Publish target created'];
    }

    public static function updateTarget(array $data): array
    {
        $id = (int)($data['id'] ?? 0);
        if ($id < 1) return ['error' => 'Missing target id'];

        $platform_name = trim($data['platform_name'] ?? '');
        if ($platform_name === '') return ['error' => 'Platform name is required'];

        $config_json = null;
        if (!empty($data['config']) && is_array($data['config'])) {
            $config_json = json_encode($data['config']);
        }

        // We only update api_key/api_secret if they are not masked placeholders
        $api_key_sql    = '';
        $api_secret_sql = '';
        $params = [$platform_name];

        $set_parts = ['platform_name=?'];

        if (isset($data['api_key']) && !str_starts_with($data['api_key'] ?? '', '***')) {
            $set_parts[] = 'api_key=?';
            $params[]    = $data['api_key'];
        }
        if (isset($data['api_secret']) && ($data['api_secret'] ?? '') !== '****') {
            $set_parts[] = 'api_secret=?';
            $params[]    = $data['api_secret'];
        }

        $set_parts[] = 'feed_id=?';
        $params[]    = $data['feed_id'] ?? null;
        $set_parts[] = 'is_active=?';
        $params[]    = (int)($data['is_active'] ?? 1);
        $set_parts[] = 'config_json=?';
        $params[]    = $config_json;

        $params[] = $id;

        self::run(self::DB,
            "UPDATE publish_targets SET " . implode(', ', $set_parts) . " WHERE id=?",
            $params
        );
        return ['ok' => true, 'message' => 'Publish target updated'];
    }

    public static function deleteTarget(array $data): array
    {
        $id = (int)($data['id'] ?? 0);
        if ($id < 1) return ['error' => 'Missing target id'];

        // We also remove any pending queue items for this target
        self::run(self::DB, "DELETE FROM publish_queue WHERE target_id = ? AND status IN ('pending','scheduled')", [$id]);
        self::run(self::DB, "DELETE FROM publish_targets WHERE id = ?", [$id]);
        return ['ok' => true, 'message' => 'Publish target deleted'];
    }

    /* ── PUBLISH QUEUE (PC-3) ── */

    public static function publishToTargets(array $data): array
    {
        $episode_id = (int)($data['episode_id'] ?? 0);
        $target_ids = $data['target_ids'] ?? [];

        if ($episode_id < 1) return ['error' => 'Missing episode_id'];
        if (!is_array($target_ids) || empty($target_ids)) return ['error' => 'No targets selected'];

        // We verify the episode exists and is published
        $ep = self::row(self::DB, "SELECT * FROM podcast_episodes WHERE id = ?", [$episode_id]);
        if (!$ep) return ['error' => 'Episode not found'];

        $queued = 0;
        foreach ($target_ids as $tid) {
            $tid = (int)$tid;
            if ($tid < 1) continue;

            // We skip if already queued and not failed
            $existing = self::row(self::DB,
                "SELECT id, status FROM publish_queue WHERE episode_id = ? AND target_id = ? AND status NOT IN ('failed')",
                [$episode_id, $tid]
            );
            if ($existing) continue;

            self::run(self::DB,
                "INSERT INTO publish_queue (episode_id, target_id, status) VALUES (?, ?, 'pending')",
                [$episode_id, $tid]
            );
            $queued++;
        }

        // We immediately process pending items
        self::processQueueItems($episode_id);

        return ['ok' => true, 'queued' => $queued, 'message' => "Queued $queued item(s) for publishing"];
    }

    public static function schedulePublish(array $data): array
    {
        $episode_id   = (int)($data['episode_id'] ?? 0);
        $target_ids   = $data['target_ids'] ?? [];
        $scheduled_at = trim($data['scheduled_at'] ?? '');

        if ($episode_id < 1) return ['error' => 'Missing episode_id'];
        if (!is_array($target_ids) || empty($target_ids)) return ['error' => 'No targets selected'];
        if ($scheduled_at === '') return ['error' => 'Missing scheduled_at datetime'];

        // We validate the datetime
        $ts = strtotime($scheduled_at);
        if (!$ts || $ts <= time()) return ['error' => 'Scheduled time must be in the future'];

        $queued = 0;
        foreach ($target_ids as $tid) {
            $tid = (int)$tid;
            if ($tid < 1) continue;

            // We skip if already queued and not failed
            $existing = self::row(self::DB,
                "SELECT id FROM publish_queue WHERE episode_id = ? AND target_id = ? AND status NOT IN ('failed')",
                [$episode_id, $tid]
            );
            if ($existing) continue;

            self::run(self::DB,
                "INSERT INTO publish_queue (episode_id, target_id, status, scheduled_at) VALUES (?, ?, 'scheduled', ?)",
                [$episode_id, $tid, date('Y-m-d H:i:s', $ts)]
            );
            $queued++;
        }

        return ['ok' => true, 'queued' => $queued, 'message' => "Scheduled $queued item(s) for " . date('Y-m-d H:i', $ts)];
    }

    public static function getPublishStatus(array $data): array
    {
        $episode_id = (int)($data['episode_id'] ?? 0);
        if ($episode_id < 1) return ['error' => 'Missing episode_id'];

        $items = self::rows(self::DB,
            "SELECT pq.*, pt.platform, pt.platform_name
             FROM publish_queue pq
             LEFT JOIN publish_targets pt ON pt.id = pq.target_id
             WHERE pq.episode_id = ?
             ORDER BY pq.created_at DESC",
            [$episode_id]
        );

        return ['ok' => true, 'queue' => $items];
    }

    public static function cancelPublish(array $data): array
    {
        $id = (int)($data['id'] ?? 0);
        if ($id < 1) return ['error' => 'Missing queue item id'];

        $item = self::row(self::DB, "SELECT status FROM publish_queue WHERE id = ?", [$id]);
        if (!$item) return ['error' => 'Queue item not found'];
        if (!in_array($item['status'], ['pending', 'scheduled'])) {
            return ['error' => 'Can only cancel pending or scheduled items'];
        }

        self::run(self::DB, "DELETE FROM publish_queue WHERE id = ?", [$id]);
        return ['ok' => true, 'message' => 'Publish cancelled'];
    }

    public static function retryPublish(array $data): array
    {
        $id = (int)($data['id'] ?? 0);
        if ($id < 1) return ['error' => 'Missing queue item id'];

        $item = self::row(self::DB, "SELECT * FROM publish_queue WHERE id = ?", [$id]);
        if (!$item) return ['error' => 'Queue item not found'];
        if ($item['status'] !== 'failed') return ['error' => 'Can only retry failed items'];

        self::run(self::DB,
            "UPDATE publish_queue SET status = 'pending', error_message = NULL WHERE id = ?",
            [$id]
        );

        // We process it immediately
        self::processQueueItems((int)$item['episode_id']);

        return ['ok' => true, 'message' => 'Retrying publish'];
    }

    /**
     * We process the publish queue — called by cron or after immediate publish.
     * For cron usage: POST /app/api/podcast.php {action: "process_queue", _internal: "1"}
     */
    public static function processQueue(): array
    {
        // We find all items that are ready: pending OR (scheduled AND scheduled_at <= NOW)
        $items = self::rows(self::DB,
            "SELECT pq.*, pt.platform, pt.platform_name, pt.api_key, pt.api_secret,
                    pt.feed_id, pt.config_json, pt.show_id
             FROM publish_queue pq
             JOIN publish_targets pt ON pt.id = pq.target_id
             WHERE (pq.status = 'pending')
                OR (pq.status = 'scheduled' AND pq.scheduled_at <= NOW())
             ORDER BY pq.created_at ASC
             LIMIT 50"
        );

        $processed = 0;
        $errors    = [];

        foreach ($items as $item) {
            $result = self::processPublishItem($item);
            if ($result['ok']) {
                $processed++;
            } else {
                $errors[] = $item['platform_name'] . ': ' . ($result['error'] ?? 'Unknown error');
            }
        }

        return ['ok' => true, 'processed' => $processed, 'errors' => $errors];
    }

    /**
     * We process queue items for a specific episode (used for immediate publish).
     */
    private static function processQueueItems(int $episode_id): void
    {
        $items = self::rows(self::DB,
            "SELECT pq.*, pt.platform, pt.platform_name, pt.api_key, pt.api_secret,
                    pt.feed_id, pt.config_json, pt.show_id
             FROM publish_queue pq
             JOIN publish_targets pt ON pt.id = pq.target_id
             WHERE pq.episode_id = ? AND pq.status = 'pending'
             ORDER BY pq.created_at ASC",
            [$episode_id]
        );

        foreach ($items as $item) {
            self::processPublishItem($item);
        }
    }

    /**
     * We process a single publish queue item based on platform type.
     */
    private static function processPublishItem(array $item): array
    {
        $qid      = (int)$item['id'];
        $platform = $item['platform'];
        $config   = $item['config_json'] ? json_decode($item['config_json'], true) : [];

        // We mark as publishing
        self::run(self::DB, "UPDATE publish_queue SET status = 'publishing' WHERE id = ?", [$qid]);

        // We fetch the episode data
        $ep = self::row(self::DB, "SELECT * FROM podcast_episodes WHERE id = ?", [(int)$item['episode_id']]);
        if (!$ep) {
            self::run(self::DB,
                "UPDATE publish_queue SET status = 'failed', error_message = 'Episode not found' WHERE id = ?",
                [$qid]
            );
            return ['ok' => false, 'error' => 'Episode not found'];
        }

        try {
            $result = match ($platform) {
                'rss'     => self::publishRss($item, $ep, $config),
                'youtube' => self::publishYouTube($item, $ep, $config),
                'custom'  => self::publishCustomWebhook($item, $ep, $config),
                default   => self::publishManualPlatform($item, $ep, $config),
            };

            if ($result['ok']) {
                self::run(self::DB,
                    "UPDATE publish_queue SET status = 'published', published_at = NOW(), platform_url = ? WHERE id = ?",
                    [$result['url'] ?? null, $qid]
                );
                // We update the target's last_published_at
                self::run(self::DB,
                    "UPDATE publish_targets SET last_published_at = NOW() WHERE id = ?",
                    [(int)$item['target_id']]
                );

                // We fire webhooks for social cross-post if configured
                self::fireSocialWebhooks($ep, $item, $config);

                return ['ok' => true];
            } else {
                self::run(self::DB,
                    "UPDATE publish_queue SET status = 'failed', error_message = ? WHERE id = ?",
                    [$result['error'] ?? 'Unknown error', $qid]
                );
                return ['ok' => false, 'error' => $result['error']];
            }
        } catch (\Throwable $e) {
            mc1_log(MC1_LOG_ERROR, 'Publish error for queue item ' . $qid . ': ' . $e->getMessage(), 'podcast');
            self::run(self::DB,
                "UPDATE publish_queue SET status = 'failed', error_message = ? WHERE id = ?",
                ['Exception: ' . $e->getMessage(), $qid]
            );
            return ['ok' => false, 'error' => 'Publish operation failed. Check server logs.'];
        }
    }

    /**
     * We publish to RSS — the feed is already generated by generate_rss().
     * We just mark the episode as published if it isn't already and record the feed URL.
     */
    private static function publishRss(array $item, array $ep, array $config): array
    {
        // We ensure the episode is marked as published in the DB
        if (!$ep['is_published']) {
            self::run(self::DB,
                "UPDATE podcast_episodes SET is_published = 1, published_at = NOW() WHERE id = ?",
                [(int)$ep['id']]
            );
        }

        $show = self::row(self::DB, "SELECT * FROM podcast_shows WHERE id = ?", [(int)$item['show_id']]);
        $base = $show && !empty($show['website_url'])
            ? rtrim($show['website_url'], '/')
            : 'https://encoder.mcaster1.com:8344';

        $url = $base . '/podcast/' . (int)$item['show_id'] . '/feed.xml';

        return ['ok' => true, 'url' => $url];
    }

    /**
     * We generate a static video for YouTube upload via ffmpeg.
     * We create the video file and store the path for manual upload.
     */
    private static function publishYouTube(array $item, array $ep, array $config): array
    {
        if (empty($ep['file_path']) || !file_exists($ep['file_path'])) {
            return ['ok' => false, 'error' => 'Audio file not found'];
        }

        // We look for cover art: show cover, then a default placeholder
        $show = self::row(self::DB, "SELECT cover_art_path FROM podcast_shows WHERE id = ?", [(int)$item['show_id']]);
        $cover = $show['cover_art_path'] ?? '';

        // We determine the output directory
        $outDir = dirname($ep['file_path']);
        $outFile = $outDir . '/' . pathinfo($ep['file_path'], PATHINFO_FILENAME) . '_youtube.mp4';

        if ($cover !== '' && file_exists($cover)) {
            // We generate a video with cover art + audio
            $cmd = sprintf(
                'ffmpeg -y -loop 1 -i %s -i %s -c:v libx264 -tune stillimage -c:a aac -b:a 192k '
                . '-shortest -pix_fmt yuv420p -vf "scale=1920:1080:force_original_aspect_ratio=decrease,pad=1920:1080:-1:-1" %s 2>&1',
                escapeshellarg($cover),
                escapeshellarg($ep['file_path']),
                escapeshellarg($outFile)
            );
        } else {
            // We generate a video with a black background + audio
            $dur = (int)($ep['duration_sec'] ?? 0);
            $durArg = $dur > 0 ? '-t ' . $dur : '';
            $cmd = sprintf(
                'ffmpeg -y -f lavfi -i color=c=black:s=1920x1080:r=1 -i %s -c:v libx264 -tune stillimage '
                . '-c:a aac -b:a 192k -shortest -pix_fmt yuv420p %s %s 2>&1',
                escapeshellarg($ep['file_path']),
                $durArg,
                escapeshellarg($outFile)
            );
        }

        $output = [];
        $rc     = 0;
        if (function_exists('exec')) {
            @exec($cmd, $output, $rc);
        } else {
            return ['ok' => false, 'error' => 'exec() not available for ffmpeg'];
        }

        if ($rc !== 0 || !file_exists($outFile)) {
            mc1_log(MC1_LOG_ERROR, 'ffmpeg YouTube video generation failed (rc=' . $rc . '): '
                . implode("\n", array_slice($output, -5)), 'podcast');
            return ['ok' => false, 'error' => 'Video generation failed (ffmpeg rc=' . $rc . ')'];
        }

        mc1_log(MC1_LOG_INFO, 'YouTube video generated: ' . $outFile . ' ('
            . round(filesize($outFile) / 1048576, 1) . ' MB)', 'podcast');

        return [
            'ok'  => true,
            'url' => 'file://' . $outFile,
            'note' => 'Video generated at ' . $outFile . '. Upload manually to YouTube.',
        ];
    }

    /**
     * We publish to a custom webhook endpoint — sends episode metadata as JSON POST.
     */
    private static function publishCustomWebhook(array $item, array $ep, array $config): array
    {
        $webhook_url = $config['webhook_url'] ?? ($item['feed_id'] ?? '');
        if (empty($webhook_url) || !filter_var($webhook_url, FILTER_VALIDATE_URL)) {
            return ['ok' => false, 'error' => 'No valid webhook URL configured for custom target'];
        }

        $show = self::row(self::DB, "SELECT * FROM podcast_shows WHERE id = ?", [(int)$item['show_id']]);

        $payload = json_encode([
            'event'    => 'episode_published',
            'show'     => [
                'id'    => (int)($show['id'] ?? 0),
                'title' => $show['title'] ?? '',
                'author'=> $show['author'] ?? '',
            ],
            'episode'  => [
                'id'            => (int)$ep['id'],
                'title'         => $ep['title'],
                'description'   => $ep['description'] ?? '',
                'duration_sec'  => (int)$ep['duration_sec'],
                'format'        => $ep['format'],
                'episode_number'=> $ep['episode_number'],
                'season'        => $ep['season'],
                'published_at'  => $ep['published_at'] ?? date('Y-m-d H:i:s'),
            ],
            'timestamp' => date('c'),
        ]);

        $opts = [
            'http' => [
                'method'  => 'POST',
                'header'  => "Content-Type: application/json\r\nUser-Agent: Mcaster1DSPEncoder/1.8.0\r\n",
                'content' => $payload,
                'timeout' => 15,
                'ignore_errors' => true,
            ],
            'ssl' => [
                'verify_peer'      => true,
                'verify_peer_name' => true,
            ],
        ];

        // We add API key as Authorization header if provided
        if (!empty($item['api_key'])) {
            $opts['http']['header'] .= "Authorization: Bearer " . $item['api_key'] . "\r\n";
        }

        $context  = stream_context_create($opts);
        $response = @file_get_contents($webhook_url, false, $context);

        if ($response === false) {
            $err = error_get_last();
            return ['ok' => false, 'error' => 'Webhook failed: ' . ($err['message'] ?? 'connection error')];
        }

        $httpCode = 0;
        if (isset($http_response_header) && is_array($http_response_header)) {
            foreach ($http_response_header as $hdr) {
                if (preg_match('/^HTTP\/[\d.]+ (\d+)/', $hdr, $m)) {
                    $httpCode = (int)$m[1];
                }
            }
        }

        if ($httpCode >= 200 && $httpCode < 300) {
            return ['ok' => true, 'url' => $webhook_url];
        }

        return ['ok' => false, 'error' => 'Webhook returned HTTP ' . $httpCode];
    }

    /**
     * We handle platforms that need manual setup (Apple, Spotify, Google, Amazon, Podbean, Buzzsprout).
     * For v1, we record the publish attempt and provide setup instructions.
     * Once the user has configured their account and submitted their RSS feed,
     * episodes published to the RSS feed are automatically picked up.
     */
    private static function publishManualPlatform(array $item, array $ep, array $config): array
    {
        // We ensure the episode is published (RSS is the distribution mechanism for these platforms)
        if (!$ep['is_published']) {
            self::run(self::DB,
                "UPDATE podcast_episodes SET is_published = 1, published_at = NOW() WHERE id = ?",
                [(int)$ep['id']]
            );
        }

        // We build the platform-specific instructions/URLs
        $urls = [
            'apple'     => 'https://podcastsconnect.apple.com/',
            'spotify'   => 'https://podcasters.spotify.com/',
            'google'    => 'https://podcastsmanager.google.com/',
            'amazon'    => 'https://podcasters.amazon.com/',
            'podbean'   => 'https://www.podbean.com/dashboard',
            'buzzsprout' => 'https://www.buzzsprout.com/',
        ];

        $platform = $item['platform'];
        $url = $urls[$platform] ?? '';

        // We record it as published since RSS is the actual distribution vector
        mc1_log(MC1_LOG_INFO, 'Episode "' . $ep['title'] . '" marked as published on '
            . $item['platform_name'] . ' (RSS-distributed)', 'podcast');

        return ['ok' => true, 'url' => $url ?: null];
    }

    /**
     * We fire social webhook notifications after an episode is published.
     * We check for Discord/Slack/Custom webhooks in the target's config_json.
     */
    private static function fireSocialWebhooks(array $ep, array $item, array $config): void
    {
        // We look for social webhook URLs in config
        $socialHooks = $config['social_webhooks'] ?? [];
        if (empty($socialHooks) || !is_array($socialHooks)) return;

        $show = self::row(self::DB, "SELECT title FROM podcast_shows WHERE id = ?", [(int)$item['show_id']]);
        $showTitle = $show['title'] ?? 'Podcast';

        foreach ($socialHooks as $hook) {
            $hookUrl     = $hook['url'] ?? '';
            $hookService = $hook['service'] ?? 'custom';
            $hookTemplate = $hook['template'] ?? '';

            if (empty($hookUrl) || !filter_var($hookUrl, FILTER_VALIDATE_URL)) continue;

            // We build the message
            $msg = $hookTemplate ?: 'New episode published: "' . ($ep['title'] ?? 'Untitled')
                 . '" on ' . $showTitle;

            // We do placeholder replacement
            $msg = str_replace(
                ['{title}', '{show}', '{episode_number}', '{description}'],
                [$ep['title'] ?? '', $showTitle, $ep['episode_number'] ?? '', $ep['description'] ?? ''],
                $msg
            );

            // We construct the payload based on service type
            $payload = match ($hookService) {
                'discord' => json_encode(['content' => $msg]),
                'slack'   => json_encode(['text' => $msg]),
                default   => json_encode([
                    'message' => $msg,
                    'event'   => 'episode_published',
                    'episode' => ['title' => $ep['title'], 'id' => $ep['id']],
                ]),
            };

            $opts = [
                'http' => [
                    'method'  => 'POST',
                    'header'  => "Content-Type: application/json\r\nUser-Agent: Mcaster1DSPEncoder/1.8.0\r\n",
                    'content' => $payload,
                    'timeout' => 10,
                    'ignore_errors' => true,
                ],
                'ssl' => ['verify_peer' => true, 'verify_peer_name' => true],
            ];

            $ctx = stream_context_create($opts);
            $resp = @file_get_contents($hookUrl, false, $ctx);
            if ($resp === false) {
                mc1_log(MC1_LOG_WARN, 'Social webhook failed to ' . $hookUrl, 'podcast');
            } else {
                mc1_log(MC1_LOG_INFO, 'Social webhook fired to ' . $hookUrl, 'podcast');
            }
        }
    }

    /* ── DOWNLOAD TRACKING (PC-4) ── */

    /**
     * We handle public download requests from podcast clients.
     * We log the download to podcast_downloads and serve the audio file.
     */
    public static function handleDownload(int $episode_id): void
    {
        $ep = self::row(self::DB,
            "SELECT e.*, s.title AS show_title
             FROM podcast_episodes e
             LEFT JOIN podcast_shows s ON s.id = e.show_id
             WHERE e.id = ? AND e.is_published = 1",
            [$episode_id]
        );

        if (!$ep || empty($ep['file_path']) || !file_exists($ep['file_path'])) {
            http_response_code(404);
            header('Content-Type: application/json');
            echo json_encode(['error' => 'Episode not found']);
            return;
        }

        $file_path  = $ep['file_path'];
        $file_size  = (int)filesize($file_path);
        $mime       = self::mimeForFormat($ep['format'] ?? 'mp3');
        $client_ip  = $_SERVER['REMOTE_ADDR'] ?? '';
        $user_agent = $_SERVER['HTTP_USER_AGENT'] ?? '';
        $referer    = $_SERVER['HTTP_REFERER'] ?? '';
        $platform   = self::detectPlatform($user_agent);

        // We attempt country detection via geoiplookup
        $country = '';
        if ($client_ip && function_exists('exec')) {
            $out = '';
            @exec('geoiplookup ' . escapeshellarg($client_ip) . ' 2>/dev/null', $lines);
            if (!empty($lines[0]) && preg_match('/: ([A-Z]{2}),/', $lines[0], $m)) {
                $country = $m[1];
            }
        }

        // We log the download
        try {
            self::run(self::DB,
                "INSERT INTO podcast_downloads (episode_id, client_ip, user_agent, referer, platform, country, bytes_sent, completed)
                 VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                [
                    $episode_id,
                    $client_ip,
                    substr($user_agent, 0, 2000),
                    substr($referer, 0, 512),
                    $platform,
                    $country ?: null,
                    $file_size,
                    1,
                ]
            );
        } catch (\Throwable $e) {
            // We log but do not fail the download
            mc1_log(MC1_LOG_ERROR, 'Download tracking insert failed: ' . $e->getMessage(), 'podcast');
        }

        // We serve the file
        header('Content-Type: ' . $mime);
        header('Content-Length: ' . $file_size);
        header('Content-Disposition: inline; filename="' . basename($file_path) . '"');
        header('Accept-Ranges: bytes');
        header('Cache-Control: public, max-age=86400');

        // We support HTTP Range requests for partial downloads
        if (!empty($_SERVER['HTTP_RANGE'])) {
            $range = $_SERVER['HTTP_RANGE'];
            if (preg_match('/bytes=(\d+)-(\d*)/', $range, $rm)) {
                $start = (int)$rm[1];
                $end   = $rm[2] !== '' ? (int)$rm[2] : $file_size - 1;
                if ($start >= $file_size || $end >= $file_size || $start > $end) {
                    http_response_code(416);
                    header('Content-Range: bytes */' . $file_size);
                    return;
                }
                $length = $end - $start + 1;
                http_response_code(206);
                header('Content-Range: bytes ' . $start . '-' . $end . '/' . $file_size);
                header('Content-Length: ' . $length);

                // We update bytes_sent for partial downloads
                if ($length < $file_size) {
                    try {
                        $last_id = self::lastId(self::DB);
                        if ($last_id) {
                            self::run(self::DB,
                                "UPDATE podcast_downloads SET bytes_sent = ?, completed = ? WHERE id = ?",
                                [$length, ($length >= $file_size ? 1 : 0), $last_id]
                            );
                        }
                    } catch (\Throwable $e) {}
                }

                $fp = fopen($file_path, 'rb');
                if ($fp) {
                    fseek($fp, $start);
                    $remaining = $length;
                    while ($remaining > 0 && !feof($fp)) {
                        $chunk = min(8192, $remaining);
                        echo fread($fp, $chunk);
                        $remaining -= $chunk;
                        if (connection_aborted()) break;
                    }
                    fclose($fp);
                }
                return;
            }
        }

        readfile($file_path);
    }

    /**
     * We detect the podcast platform from the User-Agent string.
     */
    private static function detectPlatform(string $ua): string
    {
        $ua = strtolower($ua);
        if (strpos($ua, 'applecoremedia') !== false || strpos($ua, 'itunes') !== false || strpos($ua, 'apple podcasts') !== false) return 'apple_podcasts';
        if (strpos($ua, 'spotify') !== false) return 'spotify';
        if (strpos($ua, 'overcast') !== false) return 'overcast';
        if (strpos($ua, 'pocket casts') !== false || strpos($ua, 'pocketcasts') !== false) return 'pocket_casts';
        if (strpos($ua, 'castbox') !== false) return 'castbox';
        if (strpos($ua, 'castro') !== false) return 'castro';
        if (strpos($ua, 'podcastaddict') !== false || strpos($ua, 'podcast addict') !== false) return 'podcast_addict';
        if (strpos($ua, 'google-podcast') !== false || strpos($ua, 'google podcasts') !== false) return 'google_podcasts';
        if (strpos($ua, 'stitcher') !== false) return 'stitcher';
        if (strpos($ua, 'tunein') !== false) return 'tunein';
        if (strpos($ua, 'deezer') !== false) return 'deezer';
        if (strpos($ua, 'mozilla') !== false || strpos($ua, 'chrome') !== false || strpos($ua, 'safari') !== false || strpos($ua, 'firefox') !== false) return 'browser';
        if (strpos($ua, 'feedparser') !== false || strpos($ua, 'feedfetcher') !== false || strpos($ua, 'rss') !== false || strpos($ua, 'feed') !== false) return 'rss_reader';
        return 'unknown';
    }

    /* ── ANALYTICS (PC-4) ── */

    /**
     * We return download stats per episode over time (daily/weekly/monthly).
     */
    public static function downloadStats(array $data): array
    {
        $show_id  = (int)($data['show_id'] ?? 0);
        $period   = $data['period'] ?? 'daily';
        $days     = min(365, max(1, (int)($data['days'] ?? 30)));

        $group_fmt = match ($period) {
            'weekly'  => '%x-W%v',
            'monthly' => '%Y-%m',
            default   => '%Y-%m-%d',
        };

        $where  = 'WHERE d.downloaded_at >= DATE_SUB(NOW(), INTERVAL ? DAY)';
        $params = [$days];

        if ($show_id > 0) {
            $where .= ' AND e.show_id = ?';
            $params[] = $show_id;
        }

        $rows = self::rows(self::DB,
            "SELECT DATE_FORMAT(d.downloaded_at, '$group_fmt') AS period_label,
                    e.id AS episode_id, e.title AS episode_title,
                    COUNT(*) AS downloads,
                    SUM(d.completed) AS completed_downloads
             FROM podcast_downloads d
             JOIN podcast_episodes e ON e.id = d.episode_id
             $where
             GROUP BY period_label, e.id
             ORDER BY period_label ASC, downloads DESC",
            $params
        );

        // We also compute totals per period
        $totals = self::rows(self::DB,
            "SELECT DATE_FORMAT(d.downloaded_at, '$group_fmt') AS period_label,
                    COUNT(*) AS downloads
             FROM podcast_downloads d
             JOIN podcast_episodes e ON e.id = d.episode_id
             $where
             GROUP BY period_label
             ORDER BY period_label ASC",
            $params
        );

        return ['ok' => true, 'by_episode' => $rows, 'totals' => $totals, 'period' => $period, 'days' => $days];
    }

    /**
     * We return downloads grouped by detected platform.
     */
    public static function platformBreakdown(array $data): array
    {
        $show_id = (int)($data['show_id'] ?? 0);
        $days    = min(365, max(1, (int)($data['days'] ?? 30)));

        $where  = 'WHERE d.downloaded_at >= DATE_SUB(NOW(), INTERVAL ? DAY)';
        $params = [$days];

        if ($show_id > 0) {
            $where .= ' AND e.show_id = ?';
            $params[] = $show_id;
        }

        $rows = self::rows(self::DB,
            "SELECT COALESCE(d.platform, 'unknown') AS platform, COUNT(*) AS downloads,
                    SUM(d.completed) AS completed
             FROM podcast_downloads d
             JOIN podcast_episodes e ON e.id = d.episode_id
             $where
             GROUP BY d.platform
             ORDER BY downloads DESC",
            $params
        );

        return ['ok' => true, 'platforms' => $rows];
    }

    /**
     * We return the most downloaded episodes.
     */
    public static function topEpisodes(array $data): array
    {
        $show_id = (int)($data['show_id'] ?? 0);
        $days    = min(365, max(1, (int)($data['days'] ?? 30)));
        $limit   = min(50, max(5, (int)($data['limit'] ?? 10)));

        $where  = 'WHERE d.downloaded_at >= DATE_SUB(NOW(), INTERVAL ? DAY)';
        $params = [$days];

        if ($show_id > 0) {
            $where .= ' AND e.show_id = ?';
            $params[] = $show_id;
        }

        $rows = self::rows(self::DB,
            "SELECT e.id, e.title, e.episode_number, s.title AS show_title,
                    COUNT(*) AS downloads, SUM(d.completed) AS completed,
                    COUNT(DISTINCT d.client_ip) AS unique_listeners
             FROM podcast_downloads d
             JOIN podcast_episodes e ON e.id = d.episode_id
             LEFT JOIN podcast_shows s ON s.id = e.show_id
             $where
             GROUP BY e.id
             ORDER BY downloads DESC
             LIMIT $limit",
            $params
        );

        return ['ok' => true, 'episodes' => $rows];
    }

    /**
     * We return total downloads per week/month over time (cumulative growth).
     */
    public static function growthTrend(array $data): array
    {
        $show_id = (int)($data['show_id'] ?? 0);
        $period  = $data['period'] ?? 'weekly';
        $days    = min(365, max(7, (int)($data['days'] ?? 90)));

        $group_fmt = $period === 'monthly' ? '%Y-%m' : '%x-W%v';

        $where  = 'WHERE d.downloaded_at >= DATE_SUB(NOW(), INTERVAL ? DAY)';
        $params = [$days];

        if ($show_id > 0) {
            $where .= ' AND e.show_id = ?';
            $params[] = $show_id;
        }

        $rows = self::rows(self::DB,
            "SELECT DATE_FORMAT(d.downloaded_at, '$group_fmt') AS period_label,
                    COUNT(*) AS downloads,
                    COUNT(DISTINCT d.client_ip) AS unique_listeners
             FROM podcast_downloads d
             JOIN podcast_episodes e ON e.id = d.episode_id
             $where
             GROUP BY period_label
             ORDER BY period_label ASC",
            $params
        );

        // We compute cumulative totals
        $cumulative = 0;
        foreach ($rows as &$r) {
            $cumulative += (int)$r['downloads'];
            $r['cumulative'] = $cumulative;
        }
        unset($r);

        return ['ok' => true, 'trend' => $rows, 'period' => $period];
    }

    /**
     * We return partial vs complete download counts per episode.
     */
    public static function episodeRetention(array $data): array
    {
        $show_id = (int)($data['show_id'] ?? 0);
        $days    = min(365, max(1, (int)($data['days'] ?? 30)));

        $where  = 'WHERE d.downloaded_at >= DATE_SUB(NOW(), INTERVAL ? DAY)';
        $params = [$days];

        if ($show_id > 0) {
            $where .= ' AND e.show_id = ?';
            $params[] = $show_id;
        }

        $rows = self::rows(self::DB,
            "SELECT e.id, e.title, e.episode_number,
                    COUNT(*) AS total_downloads,
                    SUM(d.completed = 1) AS completed,
                    SUM(d.completed = 0) AS partial,
                    ROUND(SUM(d.completed = 1) / COUNT(*) * 100, 1) AS completion_rate
             FROM podcast_downloads d
             JOIN podcast_episodes e ON e.id = d.episode_id
             $where
             GROUP BY e.id
             ORDER BY total_downloads DESC",
            $params
        );

        return ['ok' => true, 'retention' => $rows];
    }

    /**
     * We return downloads by country (geography breakdown).
     */
    public static function listenerGeography(array $data): array
    {
        $show_id = (int)($data['show_id'] ?? 0);
        $days    = min(365, max(1, (int)($data['days'] ?? 30)));

        $where  = 'WHERE d.downloaded_at >= DATE_SUB(NOW(), INTERVAL ? DAY) AND d.country IS NOT NULL AND d.country != \'\'';
        $params = [$days];

        if ($show_id > 0) {
            $where .= ' AND e.show_id = ?';
            $params[] = $show_id;
        }

        $rows = self::rows(self::DB,
            "SELECT d.country, COUNT(*) AS downloads,
                    COUNT(DISTINCT d.client_ip) AS unique_listeners
             FROM podcast_downloads d
             JOIN podcast_episodes e ON e.id = d.episode_id
             $where
             GROUP BY d.country
             ORDER BY downloads DESC
             LIMIT 50",
            $params
        );

        return ['ok' => true, 'countries' => $rows];
    }

    /**
     * We return recent downloads for the activity table.
     */
    public static function recentDownloads(array $data): array
    {
        $show_id = (int)($data['show_id'] ?? 0);
        $limit   = min(100, max(10, (int)($data['limit'] ?? 50)));

        $where  = '';
        $params = [];

        if ($show_id > 0) {
            $where  = 'WHERE e.show_id = ?';
            $params = [$show_id];
        }

        $rows = self::rows(self::DB,
            "SELECT d.id, d.episode_id, e.title AS episode_title, e.episode_number,
                    s.title AS show_title,
                    d.platform, d.country, d.downloaded_at, d.completed, d.bytes_sent
             FROM podcast_downloads d
             JOIN podcast_episodes e ON e.id = d.episode_id
             LEFT JOIN podcast_shows s ON s.id = e.show_id
             $where
             ORDER BY d.downloaded_at DESC
             LIMIT $limit",
            $params
        );

        return ['ok' => true, 'downloads' => $rows];
    }

    /**
     * We export analytics data as CSV.
     */
    public static function exportCsv(array $data): array
    {
        $show_id = (int)($data['show_id'] ?? 0);
        $days    = min(365, max(1, (int)($data['days'] ?? 30)));
        $type    = $data['type'] ?? 'downloads';

        $where  = 'WHERE d.downloaded_at >= DATE_SUB(NOW(), INTERVAL ? DAY)';
        $params = [$days];

        if ($show_id > 0) {
            $where .= ' AND e.show_id = ?';
            $params[] = $show_id;
        }

        $rows = self::rows(self::DB,
            "SELECT d.downloaded_at, e.title AS episode_title, e.episode_number,
                    s.title AS show_title, d.platform, d.country,
                    d.client_ip, d.completed, d.bytes_sent
             FROM podcast_downloads d
             JOIN podcast_episodes e ON e.id = d.episode_id
             LEFT JOIN podcast_shows s ON s.id = e.show_id
             $where
             ORDER BY d.downloaded_at DESC",
            $params
        );

        $csv = "Downloaded At,Episode,Episode #,Show,Platform,Country,Client IP,Completed,Bytes Sent\n";
        foreach ($rows as $r) {
            $csv .= '"' . ($r['downloaded_at'] ?? '') . '","'
                  . str_replace('"', '""', $r['episode_title'] ?? '') . '",'
                  . (int)($r['episode_number'] ?? 0) . ',"'
                  . str_replace('"', '""', $r['show_title'] ?? '') . '","'
                  . ($r['platform'] ?? '') . '","'
                  . ($r['country'] ?? '') . '","'
                  . ($r['client_ip'] ?? '') . '",'
                  . (int)($r['completed'] ?? 0) . ','
                  . (int)($r['bytes_sent'] ?? 0) . "\n";
        }

        return ['ok' => true, 'csv' => $csv, 'rows' => count($rows)];
    }

    /* ── Helpers ── */

    private static function xmlEscape(string $s): string
    {
        return htmlspecialchars($s, ENT_XML1 | ENT_QUOTES, 'UTF-8');
    }

    private static function xmlAttrEscape(string $s): string
    {
        return htmlspecialchars($s, ENT_XML1 | ENT_QUOTES, 'UTF-8');
    }

    private static function mimeForFormat(string $fmt): string
    {
        return match ($fmt) {
            'mp3'           => 'audio/mpeg',
            'ogg', 'opus'   => 'audio/ogg',
            'flac'          => 'audio/flac',
            'aac', 'm4a'    => 'audio/mp4',
            'wav'           => 'audio/wav',
            'mp4'           => 'video/mp4',
            'webm'          => 'video/webm',
            'mkv'           => 'video/x-matroska',
            default         => 'audio/mpeg',
        };
    }

    private static function isVideoFormat(string $fmt): bool
    {
        return in_array($fmt, ['mp4', 'webm', 'mkv', 'avi', 'mov'], true);
    }
}

/* ── Action dispatch ── */

try {
    $result = match ($action) {
        'list_shows'        => PodcastApi::listShows(),
        'get_show'          => PodcastApi::getShow($data),
        'create_show'       => PodcastApi::createShow($data),
        'update_show'       => PodcastApi::updateShow($data),
        'delete_show'       => PodcastApi::deleteShow($data),
        'list_episodes'     => PodcastApi::listEpisodes($data),
        'get_episode'       => PodcastApi::getEpisode($data),
        'create_episode'    => PodcastApi::createEpisode($data),
        'update_episode'    => PodcastApi::updateEpisode($data),
        'delete_episode'    => PodcastApi::deleteEpisode($data),
        'publish_episode'   => PodcastApi::publishEpisode($data),
        'unpublish_episode' => PodcastApi::unpublishEpisode($data),
        'generate_rss'      => PodcastApi::generateRss($data),
        'scan_archives'     => PodcastApi::scanArchives($data),
        'list_markers'      => PodcastApi::listMarkers($data),
        'delete_marker'     => PodcastApi::deleteMarker($data),
        'update_marker'     => PodcastApi::updateMarker($data),
        'add_marker'        => PodcastApi::addMarker($data),
        /* PC-2: Episode export */
        'export_episode'    => PodcastApi::exportEpisode($data),
        /* Chapter embedding & export */
        'embed_chapters'              => PodcastApi::embedChapters($data),
        'generate_chapters_json'      => PodcastApi::generateChaptersJson($data),
        'generate_youtube_description'=> PodcastApi::generateYoutubeDescription($data),
        /* PC-3: Multi-platform publishing */
        'list_targets'      => PodcastApi::listTargets($data),
        'create_target'     => PodcastApi::createTarget($data),
        'update_target'     => PodcastApi::updateTarget($data),
        'delete_target'     => PodcastApi::deleteTarget($data),
        'publish_to_targets'=> PodcastApi::publishToTargets($data),
        'schedule_publish'  => PodcastApi::schedulePublish($data),
        'get_publish_status'=> PodcastApi::getPublishStatus($data),
        'cancel_publish'    => PodcastApi::cancelPublish($data),
        'retry_publish'     => PodcastApi::retryPublish($data),
        'process_queue'     => PodcastApi::processQueue(),
        /* PC-4: Analytics */
        'download_stats'    => PodcastApi::downloadStats($data),
        'platform_breakdown'=> PodcastApi::platformBreakdown($data),
        'top_episodes'      => PodcastApi::topEpisodes($data),
        'growth_trend'      => PodcastApi::growthTrend($data),
        'episode_retention' => PodcastApi::episodeRetention($data),
        'listener_geography'=> PodcastApi::listenerGeography($data),
        'recent_downloads'  => PodcastApi::recentDownloads($data),
        'export_csv'        => PodcastApi::exportCsv($data),
        default             => ['error' => 'Unknown action: ' . $action],
    };

    $status = isset($result['error']) ? 400 : 200;
    mc1_api_respond($result, $status);
} catch (Throwable $e) {
    mc1_log_exception($e, 'podcast');
    mc1_api_respond(['error' => mc1_safe_error($e, 'Podcast API error')], 500);
}
