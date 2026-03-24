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
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We use Mc1Db trait for all database access
 *  - We use first-person plural throughout all comments
 *  - We use raw SQL only, no ORMs
 *  - We use mc1_api_respond() for all JSON responses
 *  - We use mc1_is_authed() for auth gate
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/../inc/mc1_config.php';
require_once __DIR__ . '/../inc/db.php';
require_once __DIR__ . '/../inc/traits.db.class.php';
require_once __DIR__ . '/../inc/logger.php';
require_once __DIR__ . '/../inc/auth.php';
require_once __DIR__ . '/../inc/user_auth.php';

header('Content-Type: application/json');

/* ── Auth gate ── */
if (!mc1_is_authed()) {
    mc1_api_respond(['error' => 'Unauthorized'], 403);
    return;
}

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
             language=?, cover_art_path=?, website_url=?, feed_url=?, is_active=?
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

        foreach ($episodes as $ep) {
            // We construct the audio URL for the enclosure
            $audio_url = $base_url . '/podcast/episode/' . $ep['id'] . '/audio';
            $mime_type = self::mimeForFormat($ep['format'] ?? 'mp3');

            $xml .= "    <item>\n";
            $xml .= '      <title>' . self::xmlEscape($ep['title']) . "</title>\n";
            $xml .= '      <description>' . self::xmlEscape($ep['description'] ?? '') . "</description>\n";
            $xml .= '      <enclosure url="' . self::xmlAttrEscape($audio_url) . '" '
                  . 'length="' . (int)$ep['file_size_bytes'] . '" '
                  . 'type="' . $mime_type . '"/>' . "\n";

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

        // We find all audio files in the directory
        $extensions = ['mp3', 'wav', 'ogg', 'opus', 'flac', 'aac', 'm4a'];
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
            default         => 'audio/mpeg',
        };
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
        default             => ['error' => 'Unknown action: ' . $action],
    };

    $status = isset($result['error']) ? 400 : 200;
    mc1_api_respond($result, $status);
} catch (Throwable $e) {
    mc1_log_exception($e, 'podcast');
    mc1_api_respond(['error' => mc1_safe_error($e, 'Podcast API error')], 500);
}
