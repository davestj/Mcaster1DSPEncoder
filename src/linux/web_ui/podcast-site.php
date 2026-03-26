<?php
/**
 * podcast-site.php — Public Podcast Landing Page
 *
 * File:    src/linux/web_ui/podcast-site.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Phase:   PC-5
 * Purpose: We provide a self-contained, SEO-optimized public landing page for a
 *          podcast show. No auth required. Displays show info, subscribe links,
 *          and a full episode list with embedded HTML5 audio players and chapters.
 *
 * URL patterns:
 *   /podcast-site.php?show_id=N         — direct
 *   /shows/{id}                         — clean URL (C++ route forwards here)
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - Self-contained: no header.php / footer.php
 *  - Public page: no auth required
 *  - h() for all user data rendered into HTML
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/app/inc/mc1_config.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/traits.db.class.php';

/* ── Helper class for public podcast data ── */
class PodcastSite {
    use Mc1Db;
    const DB = 'mcaster1_media';

    public static function getShow(int $id): ?array
    {
        $show = self::row(self::DB,
            "SELECT * FROM podcast_shows WHERE id = ? AND is_active = 1", [$id]);
        if (!$show) return null;
        if (empty($show['site_enabled']) && $show['site_enabled'] !== null && (int)$show['site_enabled'] === 0) {
            return null;
        }
        return $show;
    }

    public static function getPublishedEpisodes(int $show_id): array
    {
        return self::rows(self::DB,
            "SELECT * FROM podcast_episodes
             WHERE show_id = ? AND is_published = 1
             ORDER BY episode_number DESC, published_at DESC",
            [$show_id]
        );
    }

    public static function getEpisodeMarkers(int $episode_id): array
    {
        return self::rows(self::DB,
            "SELECT * FROM episode_markers
             WHERE episode_id = ?
             ORDER BY timestamp_ms ASC",
            [$episode_id]
        );
    }

    public static function getSubscribeLinks(int $show_id): array
    {
        return self::rows(self::DB,
            "SELECT platform, platform_name, feed_id, config_json
             FROM publish_targets
             WHERE show_id = ? AND is_active = 1
             ORDER BY platform ASC",
            [$show_id]
        );
    }

    public static function formatDuration(int $sec): string
    {
        if ($sec < 60) return $sec . 's';
        $h = (int)($sec / 3600);
        $m = (int)(($sec % 3600) / 60);
        $s = $sec % 60;
        if ($h > 0) return sprintf('%d:%02d:%02d', $h, $m, $s);
        return sprintf('%d:%02d', $m, $s);
    }

    public static function formatDurationMs(int $ms): string
    {
        return self::formatDuration((int)($ms / 1000));
    }

    public static function mimeForFormat(string $fmt): string
    {
        return match ($fmt) {
            'mp3'         => 'audio/mpeg',
            'ogg', 'opus' => 'audio/ogg',
            'flac'        => 'audio/flac',
            'aac', 'm4a'  => 'audio/mp4',
            'wav'         => 'audio/wav',
            default       => 'audio/mpeg',
        };
    }
}

/* ── Load show data ── */
$show_id = (int)($_GET['show_id'] ?? 0);
if ($show_id < 1) {
    http_response_code(404);
    echo '<!DOCTYPE html><html><head><title>Not Found</title></head><body><h1>404 — Show Not Found</h1></body></html>';
    return;
}

$show = PodcastSite::getShow($show_id);
if (!$show) {
    http_response_code(404);
    echo '<!DOCTYPE html><html><head><title>Not Found</title></head><body><h1>404 — Show Not Found</h1></body></html>';
    return;
}

$episodes   = PodcastSite::getPublishedEpisodes($show_id);
$targets    = PodcastSite::getSubscribeLinks($show_id);
$theme      = $show['site_theme'] ?? 'clean_light';
$accent     = $show['site_accent_color'] ?? '#14b8a6';
$welcome    = $show['site_welcome_message'] ?? '';

/* We sanitize the accent color */
$accent = preg_match('/^#[0-9a-fA-F]{6}$/', $accent) ? $accent : '#14b8a6';

/* We build base URL for audio/cover endpoints */
$proto   = (!empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off') ? 'https' : 'http';
$host    = $_SERVER['HTTP_HOST'] ?? 'encoder.mcaster1.com:8344';
$baseUrl = $proto . '://' . $host;

/* We build the RSS feed URL */
$feedUrl = $baseUrl . '/podcast/' . $show_id . '/feed.xml';

/* We build cover art URL */
$coverUrl = '';
if (!empty($show['cover_art_path'])) {
    if (str_starts_with($show['cover_art_path'], 'http')) {
        $coverUrl = $show['cover_art_path'];
    } else {
        $coverUrl = $baseUrl . '/podcast/cover/' . $show_id;
    }
}

/* ── Build subscribe link map ── */
$subscribeLinks = [];
foreach ($targets as $t) {
    $cfg = json_decode($t['config_json'] ?? '{}', true) ?: [];
    $url = $cfg['subscribe_url'] ?? $cfg['url'] ?? '';
    if ($url === '' && $t['feed_id']) {
        // We try to construct platform URLs from feed_id
        $url = match($t['platform']) {
            'apple'   => 'https://podcasts.apple.com/podcast/id' . $t['feed_id'],
            'spotify' => 'https://open.spotify.com/show/' . $t['feed_id'],
            'google'  => 'https://podcasts.google.com/feed/' . $t['feed_id'],
            'amazon'  => 'https://music.amazon.com/podcasts/' . $t['feed_id'],
            'youtube' => 'https://www.youtube.com/playlist?list=' . $t['feed_id'],
            default   => '',
        };
    }
    if ($url !== '') {
        $subscribeLinks[$t['platform']] = [
            'url'  => $url,
            'name' => $t['platform_name'],
        ];
    }
}

/* We always include RSS as a subscribe option */
if (!isset($subscribeLinks['rss'])) {
    $subscribeLinks['rss'] = ['url' => $feedUrl, 'name' => 'RSS Feed'];
}

/* ── Preload episode markers ── */
$episodeMarkers = [];
foreach ($episodes as $ep) {
    $markers = PodcastSite::getEpisodeMarkers((int)$ep['id']);
    if (!empty($markers)) {
        $episodeMarkers[(int)$ep['id']] = $markers;
    }
}

/* ── Check for single episode view ── */
$episode_id  = (int)($_GET['episode_id'] ?? 0);
$singleEp    = null;
$prevEp      = null;
$nextEp      = null;
if ($episode_id > 0) {
    foreach ($episodes as $i => $ep) {
        if ((int)$ep['id'] === $episode_id) {
            $singleEp = $ep;
            $prevEp = $episodes[$i + 1] ?? null; // older (lower index = newer, so i+1 = older)
            $nextEp = $episodes[$i - 1] ?? null; // newer
            break;
        }
    }
}

/* ── Theme definitions ── */
$themes = [
    'clean_light' => [
        'bg'        => '#ffffff',
        'bg2'       => '#f8fafc',
        'text'      => '#1e293b',
        'textDim'   => '#64748b',
        'border'    => '#e2e8f0',
        'card'      => '#ffffff',
        'cardHover' => '#f1f5f9',
        'font'      => "-apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif",
        'shadow'    => '0 1px 3px rgba(0,0,0,.08)',
    ],
    'dark_modern' => [
        'bg'        => '#1a1a2e',
        'bg2'       => '#16213e',
        'text'      => '#e2e8f0',
        'textDim'   => '#94a3b8',
        'border'    => '#334155',
        'card'      => '#1e293b',
        'cardHover' => '#334155',
        'font'      => "-apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif",
        'shadow'    => '0 1px 3px rgba(0,0,0,.3)',
    ],
    'warm_podcast' => [
        'bg'        => '#faf7f2',
        'bg2'       => '#f5f0e8',
        'text'      => '#3d2c1e',
        'textDim'   => '#8b7355',
        'border'    => '#e0d5c5',
        'card'      => '#ffffff',
        'cardHover' => '#faf5ed',
        'font'      => "Georgia, 'Times New Roman', Times, serif",
        'shadow'    => '0 1px 3px rgba(61,44,30,.08)',
    ],
];
$t = $themes[$theme] ?? $themes['clean_light'];

/* ── Structured Data (JSON-LD) ── */
$jsonLd = [
    '@context' => 'https://schema.org',
    '@type'    => 'PodcastSeries',
    'name'     => $show['title'],
    'description' => $show['description'] ?? '',
    'author'   => ['@type' => 'Person', 'name' => $show['author'] ?? ''],
    'url'      => $baseUrl . '/shows/' . $show_id,
    'inLanguage' => $show['language'] ?? 'en',
];
if ($coverUrl) {
    $jsonLd['image'] = $coverUrl;
}
if (!empty($show['website_url'])) {
    $jsonLd['webFeed'] = $show['website_url'];
}

$episodeJsonLd = [];
foreach ($episodes as $ep) {
    $epLd = [
        '@type'         => 'PodcastEpisode',
        'name'          => $ep['title'],
        'description'   => $ep['description'] ?? '',
        'episodeNumber' => (int)($ep['episode_number'] ?? 0),
        'url'           => $baseUrl . '/shows/' . $show_id . '/episodes/' . $ep['id'],
        'datePublished' => $ep['published_at'] ?? $ep['created_at'],
        'duration'      => 'PT' . (int)$ep['duration_sec'] . 'S',
        'partOfSeries'  => ['@type' => 'PodcastSeries', 'name' => $show['title']],
        'associatedMedia' => [
            '@type'      => 'MediaObject',
            'contentUrl' => $baseUrl . '/podcast/episode/' . $ep['id'] . '/audio',
        ],
    ];
    $episodeJsonLd[] = $epLd;
}

/* ── OG / Twitter meta ── */
$ogTitle       = h($show['title']);
$ogDescription = h(mb_substr($show['description'] ?? '', 0, 200));
$ogUrl         = $baseUrl . '/shows/' . $show_id;
if ($singleEp) {
    $ogTitle       = h($singleEp['title']) . ' — ' . h($show['title']);
    $ogDescription = h(mb_substr($singleEp['description'] ?? '', 0, 200));
    $ogUrl         = $baseUrl . '/shows/' . $show_id . '/episodes/' . $singleEp['id'];
}

/* ── Subscribe button icons (FontAwesome) ── */
$platformIcons = [
    'apple'    => 'fa-brands fa-apple',
    'spotify'  => 'fa-brands fa-spotify',
    'google'   => 'fa-brands fa-google',
    'amazon'   => 'fa-brands fa-amazon',
    'youtube'  => 'fa-brands fa-youtube',
    'rss'      => 'fa-solid fa-rss',
    'podbean'  => 'fa-solid fa-podcast',
    'buzzsprout'=> 'fa-solid fa-podcast',
    'custom'   => 'fa-solid fa-link',
];
$platformColors = [
    'apple'   => '#9b59b6',
    'spotify' => '#1db954',
    'google'  => '#4285f4',
    'amazon'  => '#ff9900',
    'youtube' => '#ff0000',
    'rss'     => '#f26522',
    'podbean' => '#6fb811',
    'buzzsprout' => '#00c7c7',
    'custom'  => '#6366f1',
];
?>
<!DOCTYPE html>
<html lang="<?= h($show['language'] ?? 'en') ?>">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title><?= $ogTitle ?></title>
<meta name="description" content="<?= $ogDescription ?>">
<meta name="author" content="<?= h($show['author'] ?? '') ?>">

<!-- Open Graph -->
<meta property="og:type" content="website">
<meta property="og:title" content="<?= $ogTitle ?>">
<meta property="og:description" content="<?= $ogDescription ?>">
<meta property="og:url" content="<?= h($ogUrl) ?>">
<?php if ($coverUrl): ?>
<meta property="og:image" content="<?= h($coverUrl) ?>">
<?php endif; ?>

<!-- Twitter Card -->
<meta name="twitter:card" content="summary_large_image">
<meta name="twitter:title" content="<?= $ogTitle ?>">
<meta name="twitter:description" content="<?= $ogDescription ?>">
<?php if ($coverUrl): ?>
<meta name="twitter:image" content="<?= h($coverUrl) ?>">
<?php endif; ?>

<!-- RSS autodiscovery -->
<link rel="alternate" type="application/rss+xml" title="<?= h($show['title']) ?> RSS Feed" href="<?= h($feedUrl) ?>">

<!-- Structured data -->
<script type="application/ld+json"><?= json_encode($jsonLd, JSON_UNESCAPED_SLASHES | JSON_PRETTY_PRINT) ?></script>
<?php if (!empty($episodeJsonLd)): ?>
<script type="application/ld+json"><?= json_encode(['@context' => 'https://schema.org', '@graph' => $episodeJsonLd], JSON_UNESCAPED_SLASHES | JSON_PRETTY_PRINT) ?></script>
<?php endif; ?>

<!-- FontAwesome CDN -->
<link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.5.1/css/all.min.css" crossorigin="anonymous" referrerpolicy="no-referrer">

<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{
  --accent:<?= h($accent) ?>;
  --bg:<?= $t['bg'] ?>;
  --bg2:<?= $t['bg2'] ?>;
  --text:<?= $t['text'] ?>;
  --text-dim:<?= $t['textDim'] ?>;
  --border:<?= $t['border'] ?>;
  --card:<?= $t['card'] ?>;
  --card-hover:<?= $t['cardHover'] ?>;
  --font:<?= $t['font'] ?>;
  --shadow:<?= $t['shadow'] ?>;
  --radius:12px;
}
body{font-family:var(--font);background:var(--bg);color:var(--text);line-height:1.6}
a{color:var(--accent);text-decoration:none}
a:hover{text-decoration:underline}

/* Layout */
.site-wrap{max-width:860px;margin:0 auto;padding:24px 20px 60px}

/* Hero */
.hero{text-align:center;padding:40px 0 30px}
.hero-cover{width:220px;height:220px;border-radius:18px;object-fit:cover;box-shadow:0 8px 30px rgba(0,0,0,.12);margin:0 auto 20px}
.hero-placeholder{width:220px;height:220px;border-radius:18px;background:linear-gradient(135deg,var(--accent),#0891b2);display:flex;align-items:center;justify-content:center;font-size:72px;color:#fff;margin:0 auto 20px;box-shadow:0 8px 30px rgba(0,0,0,.12)}
.hero h1{font-size:28px;font-weight:800;margin-bottom:6px}
.hero .author{font-size:15px;color:var(--text-dim);margin-bottom:8px}
.hero .description{font-size:14px;color:var(--text-dim);max-width:600px;margin:0 auto 16px;line-height:1.6}
.hero .welcome{font-size:15px;color:var(--text);margin-bottom:16px;font-style:italic}
.hero .ep-count{font-size:13px;color:var(--text-dim)}

/* Subscribe buttons */
.subscribe-bar{display:flex;gap:10px;justify-content:center;flex-wrap:wrap;margin:20px 0}
.sub-btn{display:inline-flex;align-items:center;gap:6px;padding:8px 16px;border-radius:8px;font-size:13px;font-weight:600;color:#fff;text-decoration:none;transition:transform .15s,box-shadow .15s}
.sub-btn:hover{transform:translateY(-1px);box-shadow:0 4px 12px rgba(0,0,0,.15);text-decoration:none;color:#fff}
.sub-btn i{font-size:15px}

/* Episode list */
.ep-section-title{font-size:18px;font-weight:700;margin:30px 0 16px;padding-bottom:10px;border-bottom:1px solid var(--border)}
.ep-card{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:18px;margin-bottom:14px;box-shadow:var(--shadow);transition:border-color .15s,box-shadow .15s}
.ep-card:hover{border-color:var(--accent);box-shadow:0 4px 12px rgba(0,0,0,.06)}
.ep-header{display:flex;align-items:flex-start;gap:14px;margin-bottom:10px}
.ep-num{min-width:40px;height:40px;border-radius:10px;background:var(--accent);color:#fff;display:flex;align-items:center;justify-content:center;font-weight:700;font-size:14px;flex-shrink:0}
.ep-info{flex:1;min-width:0}
.ep-title{font-size:16px;font-weight:700;margin-bottom:2px}
.ep-title a{color:var(--text)}
.ep-title a:hover{color:var(--accent);text-decoration:none}
.ep-meta{display:flex;gap:14px;font-size:12px;color:var(--text-dim);flex-wrap:wrap;margin-bottom:8px}
.ep-meta i{margin-right:3px}
.ep-desc{font-size:13px;color:var(--text-dim);line-height:1.5;margin-bottom:10px}
.ep-desc.collapsed{max-height:60px;overflow:hidden;position:relative}
.ep-desc.collapsed::after{content:'';position:absolute;bottom:0;left:0;right:0;height:30px;background:linear-gradient(transparent,var(--card))}
.ep-expand{font-size:12px;cursor:pointer;color:var(--accent);border:none;background:none;padding:0;font-family:inherit}
.ep-expand:hover{text-decoration:underline}

/* Audio player */
.ep-player{margin:10px 0;display:flex;align-items:center;gap:10px}
.ep-player audio{flex:1;height:36px;border-radius:8px}
.ep-download{font-size:12px;padding:6px 12px;background:var(--bg2);border:1px solid var(--border);border-radius:6px;color:var(--text);text-decoration:none;white-space:nowrap;display:inline-flex;align-items:center;gap:4px}
.ep-download:hover{border-color:var(--accent);color:var(--accent);text-decoration:none}

/* Chapters */
.chapters{margin:8px 0 4px;padding:0;list-style:none}
.chapters li{display:flex;align-items:center;gap:8px;padding:5px 8px;font-size:12px;border-radius:6px;cursor:pointer;transition:background .15s}
.chapters li:hover{background:var(--bg2)}
.ch-time{font-weight:600;color:var(--accent);min-width:50px;font-variant-numeric:tabular-nums}
.ch-title{color:var(--text-dim)}

/* Single episode view */
.single-ep{margin-top:20px}
.single-ep .full-desc{font-size:14px;line-height:1.7;margin:16px 0}
.single-ep .full-desc p{margin-bottom:12px}
.ep-nav{display:flex;justify-content:space-between;margin:24px 0;gap:12px}
.ep-nav a{display:inline-flex;align-items:center;gap:6px;padding:10px 16px;background:var(--card);border:1px solid var(--border);border-radius:8px;font-size:13px;color:var(--text)}
.ep-nav a:hover{border-color:var(--accent);color:var(--accent);text-decoration:none}
.share-bar{display:flex;gap:8px;margin:16px 0;flex-wrap:wrap}
.share-btn{display:inline-flex;align-items:center;gap:4px;padding:6px 12px;border-radius:6px;font-size:12px;border:1px solid var(--border);background:var(--card);color:var(--text);cursor:pointer;font-family:inherit}
.share-btn:hover{border-color:var(--accent);color:var(--accent)}

/* Footer */
.site-footer{text-align:center;padding:30px 0;font-size:12px;color:var(--text-dim);border-top:1px solid var(--border);margin-top:30px}
.site-footer a{color:var(--text-dim)}

/* Back link */
.back-link{display:inline-flex;align-items:center;gap:6px;font-size:13px;margin-bottom:16px;color:var(--text-dim)}
.back-link:hover{color:var(--accent)}

/* Responsive */
@media(max-width:600px){
  .hero-cover,.hero-placeholder{width:160px;height:160px;border-radius:14px}
  .hero h1{font-size:22px}
  .site-wrap{padding:16px 14px 40px}
  .ep-header{flex-direction:column;gap:8px}
}
</style>
</head>
<body>

<div class="site-wrap">

<?php if ($singleEp): ?>
<!-- ══════════ Single Episode View ══════════ -->
<a href="/shows/<?= (int)$show_id ?>" class="back-link"><i class="fa-solid fa-arrow-left"></i> Back to <?= h($show['title']) ?></a>

<div class="single-ep">
  <div class="ep-header">
    <div class="ep-num"><?= (int)($singleEp['episode_number'] ?? 0) ?></div>
    <div class="ep-info">
      <div class="ep-title"><?= h($singleEp['title']) ?></div>
      <div class="ep-meta">
        <?php if (!empty($singleEp['published_at'])): ?>
        <span><i class="fa-regular fa-calendar"></i> <?= h(date('M j, Y', strtotime($singleEp['published_at']))) ?></span>
        <?php endif; ?>
        <?php if ((int)$singleEp['duration_sec'] > 0): ?>
        <span><i class="fa-regular fa-clock"></i> <?= h(PodcastSite::formatDuration((int)$singleEp['duration_sec'])) ?></span>
        <?php endif; ?>
        <?php if ($singleEp['season']): ?>
        <span><i class="fa-solid fa-layer-group"></i> Season <?= (int)$singleEp['season'] ?></span>
        <?php endif; ?>
        <?php if ((int)$singleEp['file_size_bytes'] > 0): ?>
        <span><i class="fa-solid fa-file-audio"></i> <?= h(number_format((int)$singleEp['file_size_bytes'] / 1048576, 1)) ?> MB</span>
        <?php endif; ?>
      </div>
    </div>
  </div>

  <!-- Player -->
  <div class="ep-player">
    <audio controls preload="metadata" id="ep-audio-single"
      src="<?= h($baseUrl . '/podcast/episode/' . (int)$singleEp['id'] . '/audio') ?>"
      type="<?= h(PodcastSite::mimeForFormat($singleEp['format'] ?? 'mp3')) ?>">
    </audio>
    <a class="ep-download" href="<?= h($baseUrl . '/podcast/episode/' . (int)$singleEp['id'] . '/audio') ?>" download>
      <i class="fa-solid fa-download"></i> Download
    </a>
  </div>

  <!-- Chapters -->
  <?php
  $markers = $episodeMarkers[(int)$singleEp['id']] ?? [];
  if (!empty($markers)): ?>
  <h3 style="font-size:14px;font-weight:700;margin:16px 0 8px">Chapters</h3>
  <ul class="chapters">
    <?php foreach ($markers as $mk): ?>
    <li onclick="seekTo('ep-audio-single',<?= (int)$mk['timestamp_ms'] ?>)">
      <span class="ch-time"><?= h(PodcastSite::formatDurationMs((int)$mk['timestamp_ms'])) ?></span>
      <span class="ch-title"><?= h($mk['title'] ?? '') ?></span>
    </li>
    <?php endforeach; ?>
  </ul>
  <?php endif; ?>

  <!-- Full description -->
  <div class="full-desc"><?= nl2br(h($singleEp['description'] ?? '')) ?></div>

  <!-- Share buttons -->
  <h3 style="font-size:14px;font-weight:700;margin:16px 0 8px">Share this episode</h3>
  <div class="share-bar">
    <button class="share-btn" onclick="copyLink()"><i class="fa-solid fa-link"></i> Copy Link</button>
    <a class="share-btn" href="https://twitter.com/intent/tweet?text=<?= urlencode($singleEp['title'] . ' — ' . $show['title']) ?>&url=<?= urlencode($ogUrl) ?>" target="_blank" rel="noopener"><i class="fa-brands fa-x-twitter"></i> Post</a>
    <a class="share-btn" href="https://www.facebook.com/sharer/sharer.php?u=<?= urlencode($ogUrl) ?>" target="_blank" rel="noopener"><i class="fa-brands fa-facebook"></i> Share</a>
  </div>

  <!-- Subscribe section -->
  <?php if (!empty($subscribeLinks)): ?>
  <h3 style="font-size:14px;font-weight:700;margin:24px 0 8px">Subscribe to <?= h($show['title']) ?></h3>
  <div class="subscribe-bar" style="justify-content:flex-start">
    <?php foreach ($subscribeLinks as $plat => $info):
      $icon  = $platformIcons[$plat] ?? 'fa-solid fa-podcast';
      $color = $platformColors[$plat] ?? '#6366f1';
    ?>
    <a class="sub-btn" href="<?= h($info['url']) ?>" target="_blank" rel="noopener" style="background:<?= h($color) ?>">
      <i class="<?= h($icon) ?>"></i> <?= h($info['name']) ?>
    </a>
    <?php endforeach; ?>
  </div>
  <?php endif; ?>

  <!-- Prev/Next navigation -->
  <div class="ep-nav">
    <?php if ($prevEp): ?>
    <a href="/shows/<?= (int)$show_id ?>/episodes/<?= (int)$prevEp['id'] ?>">
      <i class="fa-solid fa-chevron-left"></i> Ep. <?= (int)$prevEp['episode_number'] ?>: <?= h(mb_substr($prevEp['title'], 0, 40)) ?>
    </a>
    <?php else: ?>
    <span></span>
    <?php endif; ?>
    <?php if ($nextEp): ?>
    <a href="/shows/<?= (int)$show_id ?>/episodes/<?= (int)$nextEp['id'] ?>">
      Ep. <?= (int)$nextEp['episode_number'] ?>: <?= h(mb_substr($nextEp['title'], 0, 40)) ?> <i class="fa-solid fa-chevron-right"></i>
    </a>
    <?php else: ?>
    <span></span>
    <?php endif; ?>
  </div>
</div>

<?php else: ?>
<!-- ══════════ Show Landing Page ══════════ -->

<!-- Hero -->
<div class="hero">
  <?php if ($coverUrl): ?>
  <img class="hero-cover" src="<?= h($coverUrl) ?>" alt="<?= h($show['title']) ?>">
  <?php else: ?>
  <div class="hero-placeholder"><i class="fa-solid fa-podcast"></i></div>
  <?php endif; ?>
  <h1><?= h($show['title']) ?></h1>
  <?php if (!empty($show['author'])): ?>
  <div class="author">by <?= h($show['author']) ?></div>
  <?php endif; ?>
  <?php if ($welcome): ?>
  <div class="welcome"><?= h($welcome) ?></div>
  <?php endif; ?>
  <?php if (!empty($show['description'])): ?>
  <div class="description"><?= nl2br(h($show['description'])) ?></div>
  <?php endif; ?>
  <div class="ep-count"><?= count($episodes) ?> episode<?= count($episodes) !== 1 ? 's' : '' ?></div>
</div>

<!-- Subscribe buttons -->
<?php if (!empty($subscribeLinks)): ?>
<div class="subscribe-bar">
  <?php foreach ($subscribeLinks as $plat => $info):
    $icon  = $platformIcons[$plat] ?? 'fa-solid fa-podcast';
    $color = $platformColors[$plat] ?? '#6366f1';
  ?>
  <a class="sub-btn" href="<?= h($info['url']) ?>" target="_blank" rel="noopener" style="background:<?= h($color) ?>">
    <i class="<?= h($icon) ?>"></i> <?= h($info['name']) ?>
  </a>
  <?php endforeach; ?>
</div>
<?php endif; ?>

<!-- Episode list -->
<?php if (!empty($episodes)): ?>
<div class="ep-section-title">Episodes</div>

<?php foreach ($episodes as $idx => $ep):
  $epId      = (int)$ep['id'];
  $markers   = $episodeMarkers[$epId] ?? [];
  $audioUrl  = $baseUrl . '/podcast/episode/' . $epId . '/audio';
  $mime      = PodcastSite::mimeForFormat($ep['format'] ?? 'mp3');
  $desc      = $ep['description'] ?? '';
  $isLong    = mb_strlen($desc) > 200;
?>
<div class="ep-card" id="ep-<?= $epId ?>">
  <div class="ep-header">
    <div class="ep-num"><?= (int)($ep['episode_number'] ?? ($idx + 1)) ?></div>
    <div class="ep-info">
      <div class="ep-title"><a href="/shows/<?= (int)$show_id ?>/episodes/<?= $epId ?>"><?= h($ep['title']) ?></a></div>
      <div class="ep-meta">
        <?php if (!empty($ep['published_at'])): ?>
        <span><i class="fa-regular fa-calendar"></i> <?= h(date('M j, Y', strtotime($ep['published_at']))) ?></span>
        <?php endif; ?>
        <?php if ((int)$ep['duration_sec'] > 0): ?>
        <span><i class="fa-regular fa-clock"></i> <?= h(PodcastSite::formatDuration((int)$ep['duration_sec'])) ?></span>
        <?php endif; ?>
        <?php if ($ep['season']): ?>
        <span><i class="fa-solid fa-layer-group"></i> S<?= (int)$ep['season'] ?></span>
        <?php endif; ?>
      </div>
    </div>
  </div>

  <?php if ($desc): ?>
  <div class="ep-desc <?= $isLong ? 'collapsed' : '' ?>" id="desc-<?= $epId ?>"><?= nl2br(h($desc)) ?></div>
  <?php if ($isLong): ?>
  <button class="ep-expand" onclick="toggleDesc(<?= $epId ?>)" id="expand-<?= $epId ?>">Show more</button>
  <?php endif; ?>
  <?php endif; ?>

  <div class="ep-player">
    <audio controls preload="none" id="ep-audio-<?= $epId ?>"
      src="<?= h($audioUrl) ?>"
      type="<?= h($mime) ?>">
    </audio>
    <a class="ep-download" href="<?= h($audioUrl) ?>" download>
      <i class="fa-solid fa-download"></i>
    </a>
  </div>

  <?php if (!empty($markers)): ?>
  <ul class="chapters">
    <?php foreach ($markers as $mk): ?>
    <li onclick="seekTo('ep-audio-<?= $epId ?>',<?= (int)$mk['timestamp_ms'] ?>)">
      <span class="ch-time"><?= h(PodcastSite::formatDurationMs((int)$mk['timestamp_ms'])) ?></span>
      <span class="ch-title"><?= h($mk['title'] ?? '') ?></span>
    </li>
    <?php endforeach; ?>
  </ul>
  <?php endif; ?>
</div>
<?php endforeach; ?>

<?php else: ?>
<div style="text-align:center;padding:60px 20px;color:var(--text-dim)">
  <i class="fa-solid fa-podcast" style="font-size:48px;display:block;margin-bottom:12px;opacity:.3"></i>
  <p>No episodes published yet. Check back soon.</p>
</div>
<?php endif; ?>

<?php endif; /* end show vs single ep */ ?>

<!-- Footer -->
<div class="site-footer">
  <?php if (!empty($show['website_url'])): ?>
  <a href="<?= h($show['website_url']) ?>"><?= h($show['title']) ?></a> &middot;
  <?php endif; ?>
  &copy; <?= date('Y') ?> <?= h($show['author'] ?? $show['title']) ?>
  &middot; Powered by <a href="https://mcaster1.com" target="_blank" rel="noopener">Mcaster1</a>
</div>

</div><!-- .site-wrap -->

<script>
function toggleDesc(epId) {
    var desc = document.getElementById('desc-' + epId);
    var btn  = document.getElementById('expand-' + epId);
    if (!desc || !btn) return;
    if (desc.classList.contains('collapsed')) {
        desc.classList.remove('collapsed');
        btn.textContent = 'Show less';
    } else {
        desc.classList.add('collapsed');
        btn.textContent = 'Show more';
    }
}

function seekTo(audioId, ms) {
    var audio = document.getElementById(audioId);
    if (!audio) return;
    audio.currentTime = ms / 1000;
    if (audio.paused) {
        audio.play().catch(function(){});
    }
}

function copyLink() {
    var url = window.location.href;
    if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(url).then(function() {
            alert('Link copied to clipboard');
        });
    } else {
        /* Fallback for older browsers */
        var ta = document.createElement('textarea');
        ta.value = url;
        document.body.appendChild(ta);
        ta.select();
        document.execCommand('copy');
        document.body.removeChild(ta);
        alert('Link copied to clipboard');
    }
}
</script>

</body>
</html>
