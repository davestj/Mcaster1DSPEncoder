<?php
/**
 * edit.encoders.class.php — Encoder config load, validate, and save.
 *
 * We centralise all MySQL CRUD for encoder_configs here so that
 * edit_encoders.php stays a thin rendering layer.
 *
 * No exit()/die() — uopz active.
 * All DB access via Mc1Db trait (raw SQL only).
 *
 * @author  Dave St. John <davestj@gmail.com>
 * @version 1.4.0
 * @since   2026-02-24
 */

if (!defined('MC1_BOOT')) {
    http_response_code(403);
    echo json_encode(['ok' => false, 'error' => 'Forbidden']);
    return;
}

require_once __DIR__ . '/traits.db.class.php';
require_once __DIR__ . '/db.php';

class EditEncoders
{
    use Mc1Db;

    // ── Allowed codec / protocol / preset values ──────────────────────────

    const CODECS = ['mp3', 'vorbis', 'opus', 'flac', 'aac_lc', 'aac_he', 'aac_hev2', 'aac_eld'];

    const CODEC_LABELS = [
        'mp3'     => 'MP3',
        'vorbis'  => 'Ogg Vorbis',
        'opus'    => 'Opus',
        'flac'    => 'FLAC (lossless)',
        'aac_lc'  => 'AAC-LC',
        'aac_he'  => 'HE-AAC v1 (AAC+)',
        'aac_hev2'=> 'HE-AAC v2 (AAC++)',
        'aac_eld' => 'AAC-ELD',
    ];

    const PROTOCOLS = ['icecast2', 'shoutcast1', 'shoutcast2', 'mcaster1'];

    const PROTOCOL_LABELS = [
        'icecast2'   => 'Icecast2',
        'shoutcast1' => 'Shoutcast v1',
        'shoutcast2' => 'Shoutcast v2',
        'mcaster1'   => 'Mcaster1DNAS',
    ];

    const EQ_PRESETS = [
        'flat'         => 'Flat (No EQ)',
        'classic_rock' => 'Classic Rock',
        'country'      => 'Country',
        'modern_rock'  => 'Modern Rock',
        'broadcast'    => 'Broadcast',
        'spoken_word'  => 'Spoken Word',
    ];

    const SAMPLE_RATES = [
        22050 => '22,050 Hz',
        32000 => '32,000 Hz',
        44100 => '44,100 Hz (CD Quality)',
        48000 => '48,000 Hz (Pro)',
        88200 => '88,200 Hz (FLAC)',
        96000 => '96,000 Hz (FLAC Hi-Res)',
    ];

    const INPUT_TYPES = [
        'playlist' => 'Playlist File (M3U/PLS/TXT)',
        'url'      => 'Stream URL (relay)',
        'device'   => 'Audio Device (PortAudio)',
    ];

    const ENCODE_MODES = [
        'cbr' => 'CBR — Constant Bitrate',
        'vbr' => 'VBR — Variable Bitrate',
        'abr' => 'ABR — Average Bitrate',
    ];

    const CHANNEL_MODES = [
        'joint'  => 'Joint Stereo (recommended)',
        'stereo' => 'Stereo (independent L/R)',
        'mono'   => 'Mono (1 channel)',
    ];

    // ── Loaders ───────────────────────────────────────────────────────────

    /**
     * We load a full encoder config row by its MySQL primary key.
     * Returns null if the row does not exist.
     */
    public static function load(int $id): ?array
    {
        if ($id <= 0) return null;
        try {
            return self::row('mcaster1_encoder', 'SELECT * FROM encoder_configs WHERE id=? LIMIT 1', [$id]);
        } catch (\Throwable $e) {
            return null;
        }
    }

    /**
     * We load a full encoder config row by slot_id.
     * Useful when only the live C++ slot number is known.
     */
    public static function load_by_slot(int $slot_id): ?array
    {
        if ($slot_id <= 0) return null;
        try {
            return self::row('mcaster1_encoder', 'SELECT * FROM encoder_configs WHERE slot_id=? ORDER BY id LIMIT 1', [$slot_id]);
        } catch (\Throwable $e) {
            return null;
        }
    }

    /**
     * We return all encoder configs ordered by slot_id (for the selector page).
     */
    public static function all(): array
    {
        try {
            return self::rows('mcaster1_encoder',
                'SELECT id, slot_id, name, codec, bitrate_kbps, server_host, server_port,
                        server_mount, server_protocol, is_active, eq_preset
                 FROM encoder_configs ORDER BY slot_id');
        } catch (\Throwable $e) {
            return [];
        }
    }

    // ── Validation ────────────────────────────────────────────────────────

    /**
     * We validate the raw POST fields and return an array of human-readable
     * error strings. An empty array means all fields are valid.
     */
    public static function validate(array $f): array
    {
        $errors = [];

        if (!isset($f['slot_id']) || (int)$f['slot_id'] < 1 || (int)$f['slot_id'] > 99) {
            $errors[] = 'Slot ID must be a number between 1 and 99';
        }

        if (trim($f['name'] ?? '') === '') {
            $errors[] = 'Display name is required';
        }

        if (!in_array($f['codec'] ?? '', self::CODECS, true)) {
            $errors[] = 'Invalid codec value';
        }

        if (trim($f['server_host'] ?? '') === '') {
            $errors[] = 'Server host is required';
        }

        $port = (int)($f['server_port'] ?? 0);
        if ($port < 1 || $port > 65535) {
            $errors[] = 'Server port must be between 1 and 65535';
        }

        if (trim($f['server_mount'] ?? '') === '') {
            $errors[] = 'Mount point is required (e.g. /live)';
        }

        if (!in_array($f['server_protocol'] ?? '', self::PROTOCOLS, true)) {
            $errors[] = 'Invalid server protocol';
        }

        return $errors;
    }

    // ── Save ──────────────────────────────────────────────────────────────

    /**
     * We validate then persist an encoder config.
     * Pass $existing_id > 0 for an UPDATE, 0 for an INSERT.
     *
     * When editing, if server_pass is empty we keep the existing stored password.
     *
     * Returns: ['ok' => bool, 'id' => int, 'errors' => string[]]
     */
    public static function save(array $raw, int $existing_id = 0): array
    {
        $errors = self::validate($raw);
        if ($errors) {
            return ['ok' => false, 'id' => 0, 'errors' => $errors];
        }

        // We fetch the current password to preserve it when the field is left blank
        $existing_pass = '';
        if ($existing_id > 0) {
            try {
                $existing_pass = (string)self::scalar('mcaster1_encoder',
                    'SELECT server_pass FROM encoder_configs WHERE id=? LIMIT 1', [$existing_id]);
            } catch (\Throwable $e) {}
        }

        $pass = trim($raw['server_pass'] ?? '');
        if ($pass === '' && $existing_id > 0) {
            $pass = $existing_pass; // We preserve the existing password when blank
        }

        $codec = in_array($raw['codec'] ?? '', self::CODECS) ? $raw['codec'] : 'mp3';

        // We clamp bitrate to the valid range for the selected codec.
        $br_ranges = [
            'mp3'     => [32,  320], 'vorbis'  => [32, 500],
            'opus'    => [32,  320], 'flac'    => [32, 320],
            'aac_lc'  => [64,  320], 'aac_he'  => [24, 128],
            'aac_hev2'=> [16,   64], 'aac_eld' => [24, 192],
        ];
        [$br_min, $br_max] = $br_ranges[$codec] ?? [32, 320];

        // We force quality into range: 0-10 (Vorbis VBR) / 0-8 (FLAC compression) / 0-9 (MP3 algo).
        $quality = min(10, max(0, (int)($raw['quality'] ?? 5)));

        // We force channels=2 for HE-AAC v2 (Parametric Stereo requires stereo input).
        $channels = ($codec === 'aac_hev2') ? 2
                  : (in_array((int)($raw['channels'] ?? 2), [1, 2]) ? (int)$raw['channels'] : 2);

        $fields = [
            'slot_id'               => (int)$raw['slot_id'],
            'name'                  => trim($raw['name']),
            'input_type'            => in_array($raw['input_type'] ?? '', ['device','playlist','url']) ? $raw['input_type'] : 'playlist',
            'playlist_path'         => trim($raw['playlist_path'] ?? ''),
            'device_index'          => (int)($raw['device_index'] ?? -1),
            'codec'                 => $codec,
            'bitrate_kbps'          => min($br_max, max($br_min, (int)($raw['bitrate_kbps'] ?? 128))),
            'sample_rate'           => in_array((int)($raw['sample_rate'] ?? 44100), array_keys(self::SAMPLE_RATES)) ? (int)$raw['sample_rate'] : 44100,
            'channels'              => $channels,
            'quality'               => $quality,
            'encode_mode'           => in_array($raw['encode_mode'] ?? '', array_keys(self::ENCODE_MODES)) ? $raw['encode_mode'] : 'cbr',
            'channel_mode'          => in_array($raw['channel_mode'] ?? '', array_keys(self::CHANNEL_MODES)) ? $raw['channel_mode'] : 'joint',
            'server_host'           => trim($raw['server_host']),
            'server_port'           => min(65535, max(1, (int)($raw['server_port'] ?? 8000))),
            'server_mount'          => trim($raw['server_mount'] ?? '/live'),
            'server_user'           => trim($raw['server_user'] ?? 'source'),
            'server_pass'           => $pass,
            'server_protocol'       => in_array($raw['server_protocol'] ?? '', self::PROTOCOLS) ? $raw['server_protocol'] : 'icecast2',
            'station_name'          => trim($raw['station_name'] ?? ''),
            'description'           => trim($raw['description'] ?? ''),
            'genre'                 => trim($raw['genre'] ?? ''),
            'website_url'           => trim($raw['website_url'] ?? ''),
            'icy2_twitter'          => trim($raw['icy2_twitter']   ?? ''),
            'icy2_facebook'         => trim($raw['icy2_facebook']  ?? ''),
            'icy2_instagram'        => trim($raw['icy2_instagram'] ?? ''),
            'icy2_email'            => trim($raw['icy2_email']     ?? ''),
            'icy2_language'         => preg_replace('/[^a-z]/', '', strtolower($raw['icy2_language'] ?? 'en')),
            'icy2_country'          => preg_replace('/[^A-Z]/', '', strtoupper($raw['icy2_country']  ?? 'US')),
            'icy2_city'             => trim($raw['icy2_city'] ?? ''),
            'icy2_is_public'        => (int)(bool)($raw['icy2_is_public'] ?? true),
            'archive_enabled'       => (int)(bool)($raw['archive_enabled'] ?? false),
            'archive_dir'           => trim($raw['archive_dir'] ?? ''),
            'volume'                => min(2.0, max(0.0, (float)($raw['volume'] ?? 1.0))),
            'shuffle'               => (int)(bool)($raw['shuffle']     ?? false),
            'repeat_all'            => (int)(bool)($raw['repeat_all']  ?? true),
            'eq_enabled'            => (int)(bool)($raw['eq_enabled']  ?? false),
            'eq_preset'             => in_array($raw['eq_preset'] ?? '', array_keys(self::EQ_PRESETS)) ? $raw['eq_preset'] : 'flat',
            'agc_enabled'           => (int)(bool)($raw['agc_enabled']       ?? false),
            'crossfade_enabled'     => (int)(bool)($raw['crossfade_enabled'] ?? true),
            'crossfade_duration'    => min(10.0, max(1.0, (float)($raw['crossfade_duration'] ?? 3.0))),
            'is_active'             => (int)(bool)($raw['is_active'] ?? true),
            // Auto-start
            'auto_start'            => (int)(bool)($raw['auto_start']            ?? false),
            'auto_start_delay'      => min(300, max(0, (int)($raw['auto_start_delay'] ?? 0))),
            // Reconnect / SLEEP
            'auto_reconnect'        => (int)(bool)($raw['auto_reconnect']        ?? true),
            'reconnect_interval'    => min(300, max(1, (int)($raw['reconnect_interval']     ?? 5))),
            'reconnect_max_attempts'=> min(999, max(0, (int)($raw['reconnect_max_attempts'] ?? 0))),
        ];

        try {
            $db = self::db('mcaster1_encoder');

            if ($existing_id > 0) {
                $sets = implode(', ', array_map(fn($k) => "$k = ?", array_keys($fields)));
                $vals = array_values($fields);
                $vals[] = $existing_id;
                $db->prepare("UPDATE encoder_configs SET $sets, modified_at = NOW() WHERE id = ?")->execute($vals);
                return ['ok' => true, 'id' => $existing_id, 'errors' => []];
            }

            $cols = implode(', ', array_keys($fields));
            $phs  = implode(', ', array_fill(0, count($fields), '?'));
            $db->prepare("INSERT INTO encoder_configs ($cols) VALUES ($phs)")->execute(array_values($fields));
            return ['ok' => true, 'id' => (int)$db->lastInsertId(), 'errors' => []];

        } catch (\Throwable $e) {
            return ['ok' => false, 'id' => 0, 'errors' => [$e->getMessage()]];
        }
    }

    // ── Delete ────────────────────────────────────────────────────────────

    /**
     * We delete an encoder config by id.
     * Returns ['ok' => bool, 'error' => string|null].
     */
    public static function delete(int $id): array
    {
        if ($id <= 0) return ['ok' => false, 'error' => 'id required'];
        try {
            self::run('mcaster1_encoder', 'DELETE FROM encoder_configs WHERE id = ?', [$id]);
            return ['ok' => true, 'error' => null];
        } catch (\Throwable $e) {
            return ['ok' => false, 'error' => $e->getMessage()];
        }
    }
}
