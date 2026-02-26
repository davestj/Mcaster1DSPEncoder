<?php
/**
 * playlist.manager.pro.class.php — Playlist generation orchestrator.
 *
 * We tie together PlaylistBuilderAlgorithm, the database, and the filesystem.
 * Callers pass a flat opts array (from the generation wizard JSON payload) and
 * receive a fully-built playlist written to both MySQL and an M3U file.
 *
 * No exit()/die() — uopz active.
 * All DB access via Mc1Db trait (raw SQL only, no ORMs).
 *
 * @author  Dave St. John <davestj@gmail.com>
 * @version 1.4.0
 * @since   2026-02-23
 *
 * Changelog:
 *   1.4.0 — Initial implementation: generate(), preview(), write_to_db(),
 *            write_m3u(), estimate_pool_size(), upsert_playlist().
 */

if (!defined('MC1_BOOT')) {
    http_response_code(403);
    echo json_encode(['ok' => false, 'error' => 'Forbidden']);
    return;
}

require_once __DIR__ . '/traits.db.class.php';
require_once __DIR__ . '/db.php';
require_once __DIR__ . '/playlist.builder.algorithm.class.php';

class PlaylistManagerPro
{
    use Mc1Db;

    // ── Public API ─────────────────────────────────────────────────────────

    /**
     * We generate a complete playlist, write it to the database, and
     * optionally write an M3U file to MC1_PLAYLIST_DIR.
     *
     * Expected $opts keys:
     *   name             string  Playlist name (required)
     *   description      string  Optional description
     *   algorithm        string  One of PlaylistBuilderAlgorithm::ALGO_* constants
     *   track_count      int     Target number of tracks (default 50)
     *   duration_min     int     Alternative target: total minutes (overrides track_count)
     *   write_m3u        bool    Write an M3U file as well as DB rows (default true)
     *   filters          array   Pool filter options (genre, category_ids, year, bpm, etc.)
     *   rules            array   Rotation rules (artist_separation, song_separation_hrs, jingle_every_n)
     *   energy_direction string  For energy_flow algo: 'ascending'|'descending'|'wave'
     *   daypart_hour     int     For daypart algo: 0-23 (defaults to current hour)
     *   clock_hours      int[]   For clock_wheel algo: which hours to build template for
     *
     * Returns:
     *   ['ok' => true, 'playlist_id' => int, 'track_count' => int,
     *    'duration_sec' => int, 'm3u_path' => string|null, 'error' => string|null]
     */
    public static function generate(array $opts): array
    {
        $name      = trim($opts['name'] ?? '');
        if ($name === '') {
            return ['ok' => false, 'error' => 'Playlist name is required'];
        }

        $algo      = $opts['algorithm']    ?? PlaylistBuilderAlgorithm::ALGO_WEIGHTED_RANDOM;
        $filters   = $opts['filters']      ?? [];
        $rules     = $opts['rules']        ?? [];
        $write_m3u = isset($opts['write_m3u']) ? (bool)$opts['write_m3u'] : true;
        $dur_min   = isset($opts['duration_min']) && is_numeric($opts['duration_min'])
                     ? (int)$opts['duration_min'] : 0;

        // We resolve track_count from either explicit count or duration target
        $track_count = self::resolve_track_count($opts, $filters, $dur_min);

        // Step 1 — Build candidate pool
        $pool = PlaylistBuilderAlgorithm::build_pool($filters);
        if (empty($pool)) {
            return ['ok' => false, 'error' => 'No tracks match the selected filters'];
        }

        // Step 2 — Run the chosen algorithm
        $gen_opts = array_merge($opts, [
            'track_count'      => $track_count,
            'algorithm'        => $algo,
            'rules'            => $rules,
        ]);
        $tracks = PlaylistBuilderAlgorithm::generate($pool, $gen_opts);

        if (empty($tracks)) {
            return ['ok' => false, 'error' => 'Algorithm produced an empty playlist — try relaxing the filters'];
        }

        // Step 3 — Persist to database
        $desc     = trim($opts['description'] ?? '');
        $type     = self::algo_to_type($algo);
        $rule_json = json_encode([
            'algorithm'        => $algo,
            'filters'          => $filters,
            'rules'            => $rules,
            'energy_direction' => $opts['energy_direction'] ?? 'wave',
            'generated_at'     => date('Y-m-d H:i:s'),
        ]);

        try {
            $pl_id = self::upsert_playlist($name, $type, $desc, $rule_json, count($tracks));
        } catch (\Throwable $e) {
            return ['ok' => false, 'error' => 'DB error creating playlist: ' . $e->getMessage()];
        }

        try {
            self::write_to_db($pl_id, $tracks);
        } catch (\Throwable $e) {
            return ['ok' => false, 'error' => 'DB error writing tracks: ' . $e->getMessage()];
        }

        // Step 4 — Write M3U file (non-fatal if it fails)
        $m3u_path  = null;
        $m3u_error = null;
        if ($write_m3u) {
            try {
                $m3u_path = self::write_m3u($name, $tracks);
            } catch (\Throwable $e) {
                $m3u_error = $e->getMessage();
            }
        }

        $duration_sec = (int)array_sum(array_column($tracks, 'duration_ms')) / 1000;

        return [
            'ok'           => true,
            'playlist_id'  => $pl_id,
            'track_count'  => count($tracks),
            'pool_size'    => count($pool),
            'duration_sec' => $duration_sec,
            'm3u_path'     => $m3u_path,
            'm3u_error'    => $m3u_error,
        ];
    }

    /**
     * We generate a preview (first $limit tracks) without writing anything to DB.
     * Used by the wizard Step 4 to show a sample before the user commits.
     *
     * Returns:
     *   ['ok' => true, 'tracks' => [...], 'pool_size' => int, 'estimated_total' => int,
     *    'estimated_duration_sec' => int]
     */
    public static function preview(array $opts, int $limit = 20): array
    {
        $algo      = $opts['algorithm']  ?? PlaylistBuilderAlgorithm::ALGO_WEIGHTED_RANDOM;
        $filters   = $opts['filters']    ?? [];
        $rules     = $opts['rules']      ?? [];
        $dur_min   = isset($opts['duration_min']) && is_numeric($opts['duration_min'])
                     ? (int)$opts['duration_min'] : 0;

        $pool = PlaylistBuilderAlgorithm::build_pool($filters);
        if (empty($pool)) {
            return ['ok' => true, 'tracks' => [], 'pool_size' => 0, 'estimated_total' => 0, 'estimated_duration_sec' => 0];
        }

        $track_count = self::resolve_track_count($opts, $filters, $dur_min);

        $gen_opts = array_merge($opts, [
            'track_count' => min($limit, $track_count),
            'algorithm'   => $algo,
            'rules'       => $rules,
        ]);

        // We temporarily suppress jingle interleaving in preview to keep it clean
        $rules_no_jingle              = $rules;
        $rules_no_jingle['jingle_every_n'] = 0;
        $gen_opts['rules']            = $rules_no_jingle;

        $tracks = PlaylistBuilderAlgorithm::generate($pool, $gen_opts);

        // We estimate full duration from pool average if duration_ms is sparse
        $avg_dur_ms = count($pool) > 0
            ? (int)(array_sum(array_column($pool, 'duration_ms')) / count($pool))
            : 210000; // 3.5 min default
        $avg_dur_ms = $avg_dur_ms > 0 ? $avg_dur_ms : 210000;

        $est_dur_sec = (int)($track_count * $avg_dur_ms / 1000);

        return [
            'ok'                    => true,
            'tracks'                => self::format_for_preview($tracks),
            'pool_size'             => count($pool),
            'estimated_total'       => $track_count,
            'estimated_duration_sec'=> $est_dur_sec,
        ];
    }

    /**
     * We return the count of tracks matching the given filters without
     * loading all rows — useful for the wizard's "pool size" indicator.
     */
    public static function estimate_pool_size(array $filters): int
    {
        return PlaylistBuilderAlgorithm::count_pool($filters);
    }

    // ── Private helpers ────────────────────────────────────────────────────

    /**
     * We compute the target track count from either an explicit track_count,
     * a duration_min target, or a sensible default.
     */
    private static function resolve_track_count(array $opts, array $filters, int $dur_min): int
    {
        if ($dur_min > 0) {
            // We estimate from average track duration in the pool
            $pool = PlaylistBuilderAlgorithm::build_pool($filters);
            if (!empty($pool)) {
                $durations  = array_filter(array_column($pool, 'duration_ms'));
                $avg_ms     = $durations ? array_sum($durations) / count($durations) : 210000;
                $avg_ms     = max(60000, $avg_ms); // floor at 1 minute
                $needed     = (int)ceil($dur_min * 60 * 1000 / $avg_ms);
                return max(1, min(5000, $needed));
            }
            // No pool yet — estimate 3.5 min per track
            return max(1, min(5000, (int)ceil($dur_min * 60 / 210)));
        }

        if (isset($opts['track_count']) && is_numeric($opts['track_count'])) {
            return max(1, min(5000, (int)$opts['track_count']));
        }

        return 50; // sensible default
    }

    /**
     * We insert or replace the playlist row and clear any existing tracks.
     * Returns the playlist ID.
     */
    private static function upsert_playlist(
        string $name,
        string $type,
        string $desc,
        string $rule_json,
        int    $track_count
    ): int {
        $db = self::db('mcaster1_media');

        // We check for an existing playlist with the same name
        $st = $db->prepare("SELECT id FROM playlists WHERE name = ? LIMIT 1");
        $st->execute([$name]);
        $existing = $st->fetchColumn();

        if ($existing) {
            // We replace the existing playlist (regenerate in-place)
            $db->prepare(
                "UPDATE playlists SET type=?, description=?, rule_json=?, track_count=?, modified_at=NOW()
                 WHERE id=?"
            )->execute([$type, $desc, $rule_json, $track_count, $existing]);
            $db->prepare("DELETE FROM playlist_tracks WHERE playlist_id=?")->execute([$existing]);
            return (int)$existing;
        }

        $db->prepare(
            "INSERT INTO playlists (name, type, description, rule_json, track_count)
             VALUES (?, ?, ?, ?, ?)"
        )->execute([$name, $type, $desc, $rule_json, $track_count]);

        return (int)$db->lastInsertId();
    }

    /**
     * We insert all generated tracks into playlist_tracks in a single transaction.
     * Position is 1-based to match the M3U order.
     */
    private static function write_to_db(int $pl_id, array $tracks): void
    {
        $db = self::db('mcaster1_media');
        $st = $db->prepare(
            "INSERT INTO playlist_tracks (playlist_id, track_id, position, weight)
             VALUES (?, ?, ?, 1.0)"
        );

        $position = 1;
        foreach ($tracks as $t) {
            $st->execute([$pl_id, $t['id'], $position++]);
        }

        // We update the denormalised track_count to match what was actually written
        $db->prepare("UPDATE playlists SET track_count=? WHERE id=?")->execute([$position - 1, $pl_id]);
    }

    /**
     * We write an Extended M3U file to MC1_PLAYLIST_DIR.
     * Filename format: {safe-name}_{YYYYMMDD_HHMM}.m3u
     * Returns the full filesystem path of the written file.
     */
    private static function write_m3u(string $name, array $tracks): string
    {
        $dir = defined('MC1_PLAYLIST_DIR') ? MC1_PLAYLIST_DIR : '/var/www/mcaster1.com/audio/playlists';

        if (!is_dir($dir)) {
            mkdir($dir, 0755, true);
        }

        // We sanitise the name for use as a filename
        $safe = preg_replace('/[^a-zA-Z0-9_\-]/', '_', $name);
        $safe = preg_replace('/_+/', '_', trim($safe, '_'));
        $safe = substr($safe, 0, 64);
        $ts   = date('Ymd_Hi');
        $path = rtrim($dir, '/') . '/' . $safe . '_' . $ts . '.m3u';

        $lines   = ["#EXTM3U", "# Generated by Mcaster1DSPEncoder PlaylistManagerPro — " . date('Y-m-d H:i:s')];
        $tot_sec = 0;

        foreach ($tracks as $t) {
            $dur_sec  = isset($t['duration_ms']) && $t['duration_ms'] > 0
                        ? (int)ceil($t['duration_ms'] / 1000) : -1;
            $tot_sec += max(0, $dur_sec);
            $artist   = $t['artist'] ?? '';
            $title    = $t['title']  ?? '';
            $display  = $artist !== '' ? "$artist - $title" : $title;
            $lines[]  = "#EXTINF:$dur_sec,$display";
            $lines[]  = $t['file_path'];
        }

        $lines[] = '# Total runtime: ' . self::format_duration($tot_sec);

        file_put_contents($path, implode("\n", $lines) . "\n");

        return $path;
    }

    /**
     * We format track rows into a compact array safe for JSON preview output.
     */
    private static function format_for_preview(array $tracks): array
    {
        $out = [];
        foreach ($tracks as $t) {
            $dur_sec  = isset($t['duration_ms']) && $t['duration_ms'] > 0
                        ? (int)round($t['duration_ms'] / 1000) : null;
            $out[] = [
                'id'         => $t['id'],
                'title'      => $t['title']  ?? '',
                'artist'     => $t['artist'] ?? '',
                'album'      => $t['album']  ?? '',
                'genre'      => $t['genre']  ?? '',
                'duration'   => $dur_sec !== null ? self::format_duration($dur_sec) : '—',
                'bpm'        => isset($t['bpm']) ? round((float)$t['bpm']) : null,
                'rating'     => (int)($t['rating'] ?? 3),
                'energy'     => isset($t['energy_level']) ? round((float)$t['energy_level'], 2) : null,
                'is_jingle'  => (bool)($t['is_jingle'] ?? false),
                'is_sweeper' => (bool)($t['is_sweeper'] ?? false),
            ];
        }
        return $out;
    }

    /**
     * We convert an algorithm string to the playlists.type enum value.
     */
    private static function algo_to_type(string $algo): string
    {
        $map = [
            PlaylistBuilderAlgorithm::ALGO_WEIGHTED_RANDOM => 'static',
            PlaylistBuilderAlgorithm::ALGO_SMART_ROTATION  => 'smart',
            PlaylistBuilderAlgorithm::ALGO_HOT_ROTATION    => 'smart',
            PlaylistBuilderAlgorithm::ALGO_CLOCK_WHEEL     => 'clock',
            PlaylistBuilderAlgorithm::ALGO_GENRE_BLOCK     => 'static',
            PlaylistBuilderAlgorithm::ALGO_ENERGY_FLOW     => 'smart',
            PlaylistBuilderAlgorithm::ALGO_AI_ADAPTIVE     => 'ai',
            PlaylistBuilderAlgorithm::ALGO_DAYPART         => 'ai',
        ];
        return $map[$algo] ?? 'static';
    }

    /**
     * We format seconds as MM:SS or H:MM:SS for display.
     */
    private static function format_duration(int $sec): string
    {
        if ($sec < 0) return '—';
        $h = (int)($sec / 3600);
        $m = (int)(($sec % 3600) / 60);
        $s = $sec % 60;
        if ($h > 0) {
            return sprintf('%d:%02d:%02d', $h, $m, $s);
        }
        return sprintf('%d:%02d', $m, $s);
    }
}
