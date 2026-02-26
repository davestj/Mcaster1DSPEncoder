<?php
/**
 * mc1_config.php — Mcaster1DSPEncoder Central Configuration
 *
 * All path constants and shared settings live here.
 * PHP-FPM pool sets env vars MC1_LOG_DIR and MC1_LOG_LEVEL.
 * Sensitive credentials come from db.php (PDO) — never echoed to browser.
 */

if (!defined('MC1_BOOT')) { http_response_code(403); echo '403 Forbidden'; return; }

/* ── Audio library paths ──────────────────────────────────────────────── */

// Root audio directory — all MP3/audio files live under genre subdirs
define('MC1_AUDIO_ROOT',      '/var/www/mcaster1.com/audio');

// Playlist files directory — .m3u, .m3u8, .pls, .xspf, .txt
define('MC1_PLAYLIST_DIR',    '/var/www/mcaster1.com/audio/playlists');

// Additional genre subdirectories under MC1_AUDIO_ROOT
define('MC1_AUDIO_GENRE_DIRS', [
    '/var/www/mcaster1.com/audio/alt',
    '/var/www/mcaster1.com/audio/christian',
    '/var/www/mcaster1.com/audio/country',
    '/var/www/mcaster1.com/audio/pop',
    '/var/www/mcaster1.com/audio/podcasts',
]);

/* ── All directories scanned by playlist_files action ────────────────── */
define('MC1_PLAYLIST_SCAN_DIRS', [
    '/var/www/mcaster1.com/audio/playlists',   // PRIMARY — always first
    '/var/www/mcaster1.com/audio',             // root (in case playlists stored here)
]);

/* ── Album artwork cache directory — fetched from MusicBrainz/CAA ─────── */
// We store fetched cover art here as {sha1}.jpg and also as MEDIUMBLOB in cover_art table
define('MC1_ARTWORK_DIR', '/var/www/mcaster1.com/audio/artwork');

/* ── MySQL / MariaDB connection ───────────────────────────────────────── */
define('MC1_DB_HOST', '127.0.0.1');
define('MC1_DB_PORT', 3306);
define('MC1_DB_USER', 'TheDataCasterMaster');
define('MC1_DB_PASS', '#!3wrvNN57761');

/* ── C++ API connection ───────────────────────────────────────────────── */
define('MC1_API_HOST',  '127.0.0.1');
define('MC1_API_PORT',  8330);                 // internal HTTP (no TLS, loopback only)
define('MC1_API_BASE',  'http://127.0.0.1:8330');

/* ── SSL / HTTPS ──────────────────────────────────────────────────────── */
define('MC1_SSL_CERT',  '/etc/ssl/mcaster1.com/mcaster1.com-fullchain.crt');
define('MC1_SSL_KEY',   '/etc/ssl/mcaster1.com/mcaster1.key');

/* ── Logging ──────────────────────────────────────────────────────────── */
define('MC1_LOG_DIR',   getenv('MC1_LOG_DIR')   ?: '/var/log/mcaster1');
define('MC1_LOG_LEVEL', (int)(getenv('MC1_LOG_LEVEL') ?: 4));

/* ── Encoder YAML config path — written by generate_token to update api-token ── */
define('MC1_CONFIG_FILE', '/var/www/mcaster1.com/Mcaster1DSPEncoder/src/linux/config/mcaster1_rock_yolo.yaml');

/* ── App constants ────────────────────────────────────────────────────── */
define('MC1_SESSION_COOKIE', 'mc1app_session');
define('MC1_SESSION_TTL',    604800);           // 7 days in seconds
define('MC1_APP_VERSION',    '1.4.0');

/* ── Scan limits ──────────────────────────────────────────────────────── */
define('MC1_SCAN_FILE_LIMIT',     5000);
define('MC1_PLAYLIST_FILE_LIMIT',  500);

/* ── Encoder protocol labels (for display) ───────────────────────────── */
define('MC1_PROTOCOLS', [
    'icecast2'    => 'Icecast2',
    'shoutcast1'  => 'Shoutcast v1',
    'shoutcast2'  => 'Shoutcast v2',
    'mcaster1'    => 'Mcaster1DNAS',
]);

/* ── EQ presets (for display in encoder config forms) ───────────────── */
define('MC1_EQ_PRESETS', [
    'flat'         => 'Flat (No EQ)',
    'classic_rock' => 'Classic Rock',
    'country'      => 'Country',
    'modern_rock'  => 'Modern Rock',
    'broadcast'    => 'Broadcast',
    'spoken_word'  => 'Spoken Word',
]);
