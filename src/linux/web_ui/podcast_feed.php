<?php
/**
 * podcast_feed.php — Public RSS feed endpoint for podcast shows
 *
 * File:    src/linux/web_ui/podcast_feed.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Purpose: We serve an iTunes-compatible RSS XML feed for a podcast show.
 *          This endpoint is publicly accessible (no auth required) so that
 *          podcast aggregators (Apple Podcasts, Spotify, etc.) can fetch the feed.
 *
 * URL: /podcast/{show_id}/feed.xml → rewritten to /podcast_feed.php?show_id=N
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We use Mc1Db trait for all database access
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/app/inc/mc1_config.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/traits.db.class.php';
require_once __DIR__ . '/app/inc/logger.php';

class PodcastFeed {
    use Mc1Db;

    const DB = 'mcaster1_media';

    public static function serve(int $show_id): void
    {
        $show = self::row(self::DB,
            "SELECT * FROM podcast_shows WHERE id = ? AND is_active = 1", [$show_id]);

        if (!$show) {
            http_response_code(404);
            header('Content-Type: application/xml; charset=UTF-8');
            echo '<?xml version="1.0" encoding="UTF-8"?><error>Show not found</error>';
            return;
        }

        $episodes = self::rows(self::DB,
            "SELECT * FROM podcast_episodes
             WHERE show_id = ? AND is_published = 1
             ORDER BY published_at DESC",
            [$show_id]
        );

        $base_url = rtrim($show['website_url'] ?: 'https://encoder.mcaster1.com:8344', '/');
        $cover_url = '';
        if (!empty($show['cover_art_path'])) {
            if (str_starts_with($show['cover_art_path'], 'http')) {
                $cover_url = $show['cover_art_path'];
            } else {
                $cover_url = $base_url . '/podcast/cover/' . $show_id;
            }
        }

        header('Content-Type: application/rss+xml; charset=UTF-8');
        header('Cache-Control: public, max-age=300');

        /* We check if this show has any dynamic ad placements to use DAI URLs */
        $has_dai = (bool)self::scalar(self::DB,
            "SELECT COUNT(*) FROM ad_placements p
             JOIN podcast_episodes e ON e.id = p.episode_id
             WHERE e.show_id = ? AND p.is_dynamic = 1",
            [$show_id]
        );

        $xml = '<?xml version="1.0" encoding="UTF-8"?>' . "\n";
        $xml .= '<rss version="2.0" xmlns:itunes="http://www.itunes.com/dtds/podcast-1.0.dtd" '
              . 'xmlns:content="http://purl.org/rss/1.0/modules/content/" '
              . 'xmlns:podcast="https://podcastindex.org/namespace/1.0">' . "\n";
        $xml .= "  <channel>\n";
        $xml .= '    <title>' . self::xe($show['title']) . "</title>\n";
        $xml .= '    <description>' . self::xe($show['description'] ?? '') . "</description>\n";
        $xml .= '    <language>' . self::xe($show['language'] ?? 'en') . "</language>\n";
        $xml .= '    <itunes:author>' . self::xe($show['author'] ?? '') . "</itunes:author>\n";
        $xml .= '    <itunes:category text="' . self::xa($show['category'] ?? 'Technology') . '"/>' . "\n";

        if (!empty($show['website_url'])) {
            $xml .= '    <link>' . self::xe($show['website_url']) . "</link>\n";
        }

        $feed_self = $base_url . '/podcast/' . $show_id . '/feed.xml';
        $xml .= '    <atom:link href="' . self::xa($feed_self) . '" rel="self" type="application/rss+xml" '
              . 'xmlns:atom="http://www.w3.org/2005/Atom"/>' . "\n";

        if ($cover_url !== '') {
            $xml .= "    <image>\n";
            $xml .= '      <url>' . self::xe($cover_url) . "</url>\n";
            $xml .= '      <title>' . self::xe($show['title']) . "</title>\n";
            if (!empty($show['website_url'])) {
                $xml .= '      <link>' . self::xe($show['website_url']) . "</link>\n";
            }
            $xml .= "    </image>\n";
            $xml .= '    <itunes:image href="' . self::xa($cover_url) . '"/>' . "\n";
        }

        /* We check if this show has any video episodes to set serial type */
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
            $ep_id = (int)$ep['id'];
            $fmt  = $ep['format'] ?? 'mp3';
            $mime = self::mimeFor($fmt);
            $is_video = self::isVideoFormat($fmt);

            /* We check if this episode has dynamic ad placements.
             * If so, we point the enclosure URL to the DAI serving endpoint
             * instead of the direct download. This enables on-the-fly ad insertion. */
            $ep_has_ads = false;
            if ($has_dai) {
                $ep_ad_count = (int)self::scalar(self::DB,
                    "SELECT COUNT(*) FROM ad_placements WHERE episode_id = ? AND is_dynamic = 1",
                    [$ep_id]
                );
                $ep_has_ads = ($ep_ad_count > 0);
            }

            if ($ep_has_ads) {
                $media_url = $base_url . '/app/api/ads.php?action=serve_episode&episode_id=' . $ep_id
                           . '&_t=' . time();
            } else {
                $media_url = $base_url . '/app/api/podcast.php?action=download&episode_id=' . $ep_id;
            }

            $xml .= "    <item>\n";
            $xml .= '      <title>' . self::xe($ep['title']) . "</title>\n";
            $xml .= '      <description>' . self::xe($ep['description'] ?? '') . "</description>\n";
            $xml .= '      <enclosure url="' . self::xa($media_url) . '" '
                  . 'length="' . (int)$ep['file_size_bytes'] . '" '
                  . 'type="' . $mime . '"/>' . "\n";

            $xml .= '      <itunes:episodeType>full</itunes:episodeType>' . "\n";

            $dur = (int)$ep['duration_sec'];
            $xml .= '      <itunes:duration>' . sprintf('%02d:%02d:%02d',
                (int)($dur / 3600), (int)(($dur % 3600) / 60), $dur % 60) . "</itunes:duration>\n";

            if ($ep['season'] !== null) {
                $xml .= '      <itunes:season>' . (int)$ep['season'] . "</itunes:season>\n";
            }
            if ($ep['episode_number'] !== null) {
                $xml .= '      <itunes:episode>' . (int)$ep['episode_number'] . "</itunes:episode>\n";
            }

            if (!empty($ep['published_at'])) {
                $ts = strtotime($ep['published_at']);
                if ($ts) {
                    $xml .= '      <pubDate>' . gmdate('D, d M Y H:i:s \G\M\T', $ts) . "</pubDate>\n";
                }
            }

            $xml .= '      <guid isPermaLink="false">mc1-podcast-ep-' . $ep_id . "</guid>\n";

            /* We add tracking pixels for each ad placement on this episode (IAB compliance) */
            if ($ep_has_ads) {
                $ep_placements = self::rows(self::DB,
                    "SELECT p.id AS placement_id, p.campaign_id
                     FROM ad_placements p
                     WHERE p.episode_id = ? AND p.is_dynamic = 1",
                    [$ep_id]
                );
                foreach ($ep_placements as $pl) {
                    $track_url = $base_url . '/app/api/ads.php?action=track_impression'
                               . '&placement_id=' . (int)$pl['placement_id']
                               . '&campaign_id=' . (int)$pl['campaign_id']
                               . '&episode_id=' . $ep_id
                               . '&event=impression';
                    $xml .= '      <podcast:tracker url="' . self::xa($track_url) . '" type="impression"/>' . "\n";
                }
            }

            /* We check if this episode has chapter markers and add podcast:chapters element */
            $chapterCount = (int)self::scalar(self::DB,
                "SELECT COUNT(*) FROM episode_markers WHERE episode_id = ? AND marker_type = 'chapter'",
                [$ep_id]);
            if ($chapterCount > 0) {
                $chaptersUrl = $base_url . '/app/api/podcast.php?action=chapters_json&episode_id=' . $ep_id;
                $xml .= '      <podcast:chapters url="' . self::xa($chaptersUrl) . '" type="application/json+chapters"/>' . "\n";

                /* We also add chapter timestamps in content:encoded for apps that support HTML */
                $chapterMarkers = self::rows(self::DB,
                    "SELECT timestamp_ms, title FROM episode_markers
                     WHERE episode_id = ? AND marker_type = 'chapter'
                     ORDER BY timestamp_ms ASC",
                    [$ep_id]);
                if (!empty($chapterMarkers)) {
                    $chapHtml = '<h3>Chapters</h3><ul>';
                    foreach ($chapterMarkers as $cm) {
                        $cmSec = (int)floor((int)$cm['timestamp_ms'] / 1000);
                        $cmTs = sprintf('%02d:%02d:%02d',
                            (int)($cmSec / 3600), (int)(($cmSec % 3600) / 60), $cmSec % 60);
                        $chapHtml .= '<li>' . $cmTs . ' - ' . self::xe($cm['title'] ?? 'Chapter') . '</li>';
                    }
                    $chapHtml .= '</ul>';
                    $xml .= '      <content:encoded>' . self::xe($chapHtml) . "</content:encoded>\n";
                }
            }

            /* We check if this episode has captions and add podcast:transcript elements */
            $captions = self::rows(self::DB,
                "SELECT language, format FROM episode_captions WHERE episode_id = ? ORDER BY language",
                [$ep_id]);
            foreach ($captions as $cap) {
                $cap_fmt  = $cap['format'] === 'vtt' ? 'vtt' : 'srt';
                $cap_mime = $cap_fmt === 'vtt' ? 'text/vtt' : 'application/x-subrip';
                $cap_url  = $base_url . '/app/api/captions.php?action=download&episode_id=' . $ep_id
                          . '&format=' . $cap_fmt . '&language=' . urlencode($cap['language']);
                $xml .= '      <podcast:transcript url="' . self::xa($cap_url) . '" '
                      . 'type="' . $cap_mime . '" language="' . self::xa($cap['language']) . '"/>' . "\n";
            }

            $xml .= "    </item>\n";
        }

        $xml .= "  </channel>\n";
        $xml .= "</rss>\n";

        echo $xml;
    }

    private static function xe(string $s): string
    {
        return htmlspecialchars($s, ENT_XML1 | ENT_QUOTES, 'UTF-8');
    }

    private static function xa(string $s): string
    {
        return htmlspecialchars($s, ENT_XML1 | ENT_QUOTES, 'UTF-8');
    }

    private static function mimeFor(string $fmt): string
    {
        return match ($fmt) {
            'mp3'         => 'audio/mpeg',
            'ogg', 'opus' => 'audio/ogg',
            'flac'        => 'audio/flac',
            'aac', 'm4a'  => 'audio/mp4',
            'wav'         => 'audio/wav',
            'mp4'         => 'video/mp4',
            'webm'        => 'video/webm',
            'mkv'         => 'video/x-matroska',
            default       => 'audio/mpeg',
        };
    }

    private static function isVideoFormat(string $fmt): bool
    {
        return in_array($fmt, ['mp4', 'webm', 'mkv', 'avi', 'mov'], true);
    }
}

/* ── Dispatch ── */
$show_id = (int)($_GET['show_id'] ?? 0);
if ($show_id < 1) {
    http_response_code(400);
    header('Content-Type: application/xml; charset=UTF-8');
    echo '<?xml version="1.0" encoding="UTF-8"?><error>Missing show_id parameter</error>';
    return;
}

try {
    PodcastFeed::serve($show_id);
} catch (Throwable $e) {
    mc1_log(MC1_LOG_ERROR, 'Podcast feed error: ' . $e->getMessage(), 'podcast_feed');
    http_response_code(500);
    header('Content-Type: application/xml; charset=UTF-8');
    echo '<?xml version="1.0" encoding="UTF-8"?><error>Internal server error</error>';
}
