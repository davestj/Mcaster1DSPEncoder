<?php
/**
 * playlist.builder.algorithm.class.php — Core playlist generation algorithm engine.
 *
 * We handle all track pool building, algorithm dispatching, separation enforcement,
 * jingle interleaving, and AI-style multi-factor track scoring.
 *
 * No exit()/die() — uopz active.
 * All DB access uses the Mc1Db trait (raw SQL only).
 *
 * @author  Dave St. John <davestj@gmail.com>
 * @version 1.4.0
 * @since   2026-02-23
 *
 * Changelog:
 *   1.4.0 — Initial implementation: 8 algorithm types, weight-based selection,
 *            separation enforcement, jingle interleaving, AI composite scoring.
 */

if (!defined('MC1_BOOT')) {
    http_response_code(403);
    echo json_encode(['ok' => false, 'error' => 'Forbidden']);
    return;
}

require_once __DIR__ . '/traits.db.class.php';
require_once __DIR__ . '/db.php';

class PlaylistBuilderAlgorithm
{
    use Mc1Db;

    // ── Algorithm type constants ──────────────────────────────────────────
    // We use these string identifiers in rule_json and the generation wizard.

    const ALGO_WEIGHTED_RANDOM = 'weighted_random';
    const ALGO_SMART_ROTATION  = 'smart_rotation';
    const ALGO_HOT_ROTATION    = 'hot_rotation';
    const ALGO_CLOCK_WHEEL     = 'clock_wheel';
    const ALGO_GENRE_BLOCK     = 'genre_block';
    const ALGO_ENERGY_FLOW     = 'energy_flow';
    const ALGO_AI_ADAPTIVE     = 'ai_adaptive';
    const ALGO_DAYPART         = 'daypart';

    /**
     * We return the full catalog of supported algorithms with descriptions.
     * Used by the generation wizard to populate the algorithm selector.
     */
    public static function algo_catalog(): array
    {
        return [
            self::ALGO_WEIGHTED_RANDOM => [
                'label' => 'Weighted Random',
                'desc'  => 'Probability-weighted random selection. Higher-weight tracks appear more often. Good for general rotation.',
            ],
            self::ALGO_SMART_ROTATION => [
                'label' => 'Smart Rotation',
                'desc'  => 'Respects artist & song separation rules, prioritises un-played and under-played tracks. The default broadcast choice.',
            ],
            self::ALGO_HOT_ROTATION => [
                'label' => 'Hot Rotation',
                'desc'  => 'Prioritises high-play-count and high-rated tracks. Use for current hits and heavy rotation.',
            ],
            self::ALGO_CLOCK_WHEEL => [
                'label' => 'Clock Wheel',
                'desc'  => 'Follows your hour template segments (Clock Wheel editor). Each hour slot defines song/jingle/sweeper counts.',
            ],
            self::ALGO_GENRE_BLOCK => [
                'label' => 'Genre Block',
                'desc'  => 'Groups tracks into consecutive genre blocks. Ideal for specialty shows (all-country hour, etc.).',
            ],
            self::ALGO_ENERGY_FLOW => [
                'label' => 'Energy Flow',
                'desc'  => 'Sequences tracks by energy level: ascending (build-up), descending (cool-down), or wave (arc). Requires energy_level tags.',
            ],
            self::ALGO_AI_ADAPTIVE => [
                'label' => 'AI Adaptive',
                'desc'  => 'Multi-factor AI score: freshness, rating, weight, play history, and energy. Future listener analytics will auto-adjust weights.',
            ],
            self::ALGO_DAYPART => [
                'label' => 'Daypart',
                'desc'  => 'Matches track energy to time-of-day profile: high-energy morning, medium afternoon, relaxed overnight.',
            ],
        ];
    }

    // ── Pool building ────────────────────────────────────────────────────

    /**
     * We build the raw candidate track pool from the database using the provided
     * filter options. Returns an array of track rows with all columns needed
     * by the scoring and algorithm methods.
     *
     * We use a single dynamically-constructed parameterised query so there is
     * no risk of SQL injection.
     */
    public static function build_pool(array $filters): array
    {
        $where  = ['t.is_missing = 0'];
        $params = [];

        // We always build the category JOIN first so its placeholders come first
        $cat_join = '';
        if (!empty($filters['category_ids']) && is_array($filters['category_ids'])) {
            $ids = array_values(array_filter(array_map('intval', $filters['category_ids'])));
            if ($ids) {
                $ph       = implode(',', array_fill(0, count($ids), '?'));
                $cat_join = "JOIN track_categories tc ON tc.track_id = t.id AND tc.category_id IN ($ph)";
                array_push($params, ...$ids);
            }
        }

        // Genre text filter (partial match)
        if (isset($filters['genre']) && trim($filters['genre']) !== '') {
            $where[]  = 't.genre LIKE ?';
            $params[] = '%' . trim($filters['genre']) . '%';
        }

        // Flags — We exclude jingles/sweepers/spots from music pools by default
        if (empty($filters['include_jingles']))  { $where[] = 't.is_jingle = 0'; }
        if (empty($filters['include_sweepers'])) { $where[] = 't.is_sweeper = 0'; }
        if (empty($filters['include_spots']))    { $where[] = 't.is_spot = 0'; }

        // Year range
        if (isset($filters['year_from']) && is_numeric($filters['year_from'])) {
            $where[]  = 't.year >= ?';
            $params[] = (int)$filters['year_from'];
        }
        if (isset($filters['year_to']) && is_numeric($filters['year_to'])) {
            $where[]  = 't.year <= ?';
            $params[] = (int)$filters['year_to'];
        }

        // BPM range
        if (isset($filters['bpm_min']) && is_numeric($filters['bpm_min'])) {
            $where[]  = '(t.bpm IS NULL OR t.bpm >= ?)';
            $params[] = (float)$filters['bpm_min'];
        }
        if (isset($filters['bpm_max']) && is_numeric($filters['bpm_max'])) {
            $where[]  = '(t.bpm IS NULL OR t.bpm <= ?)';
            $params[] = (float)$filters['bpm_max'];
        }

        // Minimum rating
        if (isset($filters['rating_min']) && is_numeric($filters['rating_min'])) {
            $where[]  = 't.rating >= ?';
            $params[] = (int)$filters['rating_min'];
        }

        $where_sql = implode(' AND ', $where);

        $sql = "SELECT t.id, t.file_path, t.title, t.artist, t.album, t.genre,
                       t.year, t.duration_ms, t.bpm, t.energy_level, t.mood_tag,
                       t.rating, t.play_count, t.last_played_at,
                       t.is_jingle, t.is_sweeper, t.is_spot,
                       1.0 AS weight
                FROM tracks t
                $cat_join
                WHERE $where_sql
                ORDER BY t.id";

        try {
            return self::rows('mcaster1_media', $sql, $params);
        } catch (\Throwable $e) {
            return [];
        }
    }

    /**
     * We build a separate jingle pool for interleaving into music tracks.
     * Returns up to 500 jingles in random order.
     */
    public static function build_jingle_pool(): array
    {
        try {
            return self::rows('mcaster1_media',
                "SELECT id, file_path, title, artist, duration_ms,
                        is_jingle, 1.0 AS weight, play_count, last_played_at,
                        energy_level, bpm, rating, genre, album, year,
                        is_sweeper, is_spot
                 FROM tracks
                 WHERE is_jingle = 1 AND is_missing = 0
                 ORDER BY RAND()
                 LIMIT 500");
        } catch (\Throwable $e) {
            return [];
        }
    }

    /**
     * We count how many tracks would be in the pool without fetching rows.
     * Used by the wizard to show "X tracks available" before generating.
     */
    public static function count_pool(array $filters): int
    {
        // We re-use build_pool logic but swap the SELECT for COUNT(*)
        // The simplest approach is to call build_pool and count — pool is typically
        // under 5 000 rows so this is acceptable for wizard preview counts.
        return count(self::build_pool($filters));
    }

    // ── Main dispatch ─────────────────────────────────────────────────────

    /**
     * We dispatch to the correct algorithm based on $opts['algorithm'].
     * Returns an ordered array of track rows ready for DB insertion.
     */
    public static function generate(array $pool, array $opts): array
    {
        $algo  = $opts['algorithm'] ?? self::ALGO_WEIGHTED_RANDOM;
        $count = max(1, min(5000, (int)($opts['track_count'] ?? 50)));
        $rules = $opts['rules'] ?? [];

        // We cap the pool to avoid pathological runtime on huge libraries
        if (count($pool) > 5000) {
            shuffle($pool);
            $pool = array_slice($pool, 0, 5000);
        }

        switch ($algo) {
            case self::ALGO_SMART_ROTATION:
                $result = self::algo_smart_rotation($pool, $count, $rules);
                break;
            case self::ALGO_HOT_ROTATION:
                $result = self::algo_hot_rotation($pool, $count);
                break;
            case self::ALGO_CLOCK_WHEEL:
                $result = self::algo_clock_wheel($pool, $opts);
                break;
            case self::ALGO_GENRE_BLOCK:
                $result = self::algo_genre_block($pool, $count);
                break;
            case self::ALGO_ENERGY_FLOW:
                $dir    = $opts['energy_direction'] ?? 'wave';
                $result = self::algo_energy_flow($pool, $count, $dir);
                break;
            case self::ALGO_AI_ADAPTIVE:
                $result = self::algo_ai_adaptive($pool, $count, $rules);
                break;
            case self::ALGO_DAYPART:
                $result = self::algo_daypart($pool, $count, $opts);
                break;
            case self::ALGO_WEIGHTED_RANDOM:
            default:
                $result = self::algo_weighted_random($pool, $count);
                break;
        }

        // We interleave jingles if the rule is set and jingles exist in the library
        if (!empty($rules['jingle_every_n']) && (int)$rules['jingle_every_n'] > 0) {
            $jingles = self::build_jingle_pool();
            if ($jingles) {
                self::interleave_jingles($result, $jingles, (int)$rules['jingle_every_n']);
            }
        }

        return $result;
    }

    // ── Algorithm implementations ────────────────────────────────────────

    /**
     * Weighted random selection without replacement.
     * We use cumulative-weight sampling so higher-weight tracks are proportionally
     * more likely to be selected on each pass.
     */
    private static function algo_weighted_random(array $pool, int $count): array
    {
        $result   = [];
        $used_ids = [];
        $attempts = 0;
        $max_att  = $count * 15 + 100;

        while (count($result) < $count && count($used_ids) < count($pool) && $attempts++ < $max_att) {
            $pick = self::weighted_pick($pool, $used_ids);
            if ($pick === null) break;
            $result[]              = $pick;
            $used_ids[$pick['id']] = true;
        }

        return $result;
    }

    /**
     * Smart rotation: respects artist separation and song repeat windows.
     * We score every candidate and pick the best eligible track at each step.
     * If separation rules make us short, we relax them progressively.
     */
    private static function algo_smart_rotation(array $pool, int $count, array $rules): array
    {
        $artist_sep = max(0, (int)($rules['artist_separation']   ?? 3));
        $song_hrs   = max(0, (int)($rules['song_separation_hrs'] ?? 4));
        $now_ts     = time();

        $result         = [];
        $used_ids       = [];
        $recent_artists = []; // ring buffer of recently-placed artist strings

        // We pre-score all tracks to speed up the inner loop
        $scored = [];
        foreach ($pool as $t) {
            $scored[] = ['track' => $t, 'base_score' => self::score_track($t, $rules)];
        }
        usort($scored, fn($a, $b) => $a['base_score'] <=> $b['base_score']);

        $relax   = 0; // relaxation level — we increase when stuck
        $max_att = $count * 25 + 200;
        $attempts = 0;

        while (count($result) < $count && count($used_ids) < count($pool) && $attempts++ < $max_att) {
            $best = null;

            foreach ($scored as &$s) {
                $t = $s['track'];
                if (isset($used_ids[$t['id']])) continue;

                // We enforce artist separation (relaxed progressively if stuck)
                $sep_to_check = max(0, $artist_sep - $relax);
                if ($sep_to_check > 0) {
                    $window = array_slice($recent_artists, -$sep_to_check);
                    if (in_array($t['artist'], $window, true)) continue;
                }

                // We enforce song repeat window (not relaxed — it prevents duplicate songs)
                if ($song_hrs > 0 && !empty($t['last_played_at'])) {
                    $elapsed_hrs = ($now_ts - (int)strtotime($t['last_played_at'])) / 3600.0;
                    if ($elapsed_hrs < $song_hrs) continue;
                }

                $best = $t;
                break; // pre-sorted, so first eligible = best
            }
            unset($s);

            if ($best !== null) {
                $result[]              = $best;
                $used_ids[$best['id']] = true;
                $recent_artists[]      = $best['artist'];
                $relax                 = 0; // reset relaxation on success
            } else {
                $relax++; // progressively relax on failure
                if ($relax > $artist_sep + 2) break;
            }
        }

        return $result;
    }

    /**
     * Hot rotation: probability-weighted selection using play_count × rating as weight.
     * Frequently-played, highly-rated tracks dominate the selection pool.
     */
    private static function algo_hot_rotation(array $pool, int $count): array
    {
        // We compute hot weight: rating × log(1 + play_count) to avoid log(0)
        $hot_pool = array_map(function ($t) {
            $rate = max(1.0, (float)($t['rating']     ?? 3));
            $play = max(0.0, (float)($t['play_count'] ?? 0));
            $t['weight'] = $rate * (1.0 + log1p($play));
            return $t;
        }, $pool);

        $result   = [];
        $used_ids = [];
        $attempts = 0;
        $max_att  = $count * 15 + 100;

        while (count($result) < $count && count($used_ids) < count($hot_pool) && $attempts++ < $max_att) {
            $pick = self::weighted_pick($hot_pool, $used_ids);
            if ($pick === null) break;
            $result[]              = $pick;
            $used_ids[$pick['id']] = true;
        }

        return $result;
    }

    /**
     * Clock wheel: we read clock_hours templates from DB and fill each segment
     * slot with tracks matching the segment type and (optional) category.
     * Falls back to smart_rotation if no templates are configured.
     */
    private static function algo_clock_wheel(array $pool, array $opts): array
    {
        $hours = $opts['clock_hours'] ?? [];
        if (empty($hours)) {
            // Default: use a 1-hour template if none specified
            $hours = [(int)date('G')];
        }
        $hours = array_map('intval', $hours);

        $ph        = implode(',', array_fill(0, count($hours), '?'));
        $templates = [];
        try {
            $templates = self::rows('mcaster1_media',
                "SELECT hour, segment_json FROM clock_hours
                 WHERE hour IN ($ph) AND is_active = 1
                 ORDER BY hour",
                $hours);
        } catch (\Throwable $e) {
            // DB error — fall through to fallback
        }

        if (empty($templates)) {
            // No clock template configured — fall back to smart rotation
            return self::algo_smart_rotation($pool, $opts['track_count'] ?? 50, $opts['rules'] ?? []);
        }

        $result   = [];
        $used_ids = [];

        foreach ($templates as $tmpl) {
            $segs = json_decode($tmpl['segment_json'] ?? '[]', true);
            if (!is_array($segs)) continue;

            foreach ($segs as $seg) {
                $seg_type  = $seg['type']  ?? 'song';
                $seg_count = max(1, (int)($seg['count'] ?? 1));

                // We filter the pool to match the segment type
                $seg_pool = array_values(array_filter($pool, function ($t) use ($seg_type) {
                    if ($seg_type === 'jingle')  return (bool)$t['is_jingle'];
                    if ($seg_type === 'sweeper') return (bool)$t['is_sweeper'];
                    if ($seg_type === 'spot')    return (bool)$t['is_spot'];
                    // Default 'song': exclude special types
                    return !$t['is_jingle'] && !$t['is_sweeper'] && !$t['is_spot'];
                }));

                for ($i = 0; $i < $seg_count; $i++) {
                    $pick = self::weighted_pick($seg_pool, $used_ids);
                    if ($pick === null) break;
                    $result[]              = $pick;
                    $used_ids[$pick['id']] = true;
                }
            }
        }

        if (empty($result)) {
            return self::algo_weighted_random($pool, $opts['track_count'] ?? 50);
        }

        return $result;
    }

    /**
     * Genre block: we group tracks by genre and output consecutive blocks.
     * The block size scales with the number of genres and requested count.
     */
    private static function algo_genre_block(array $pool, int $count): array
    {
        // Group by genre
        $by_genre = [];
        foreach ($pool as $t) {
            $g = trim($t['genre']) ?: 'Unknown';
            $by_genre[$g][] = $t;
        }

        // We shuffle within each genre group to avoid alphabetic ordering
        foreach ($by_genre as &$gtracks) {
            shuffle($gtracks);
        }
        unset($gtracks);

        $block_size = max(3, (int)round($count / max(1, count($by_genre))));
        $result     = [];
        $rounds     = 0;

        while (count($result) < $count && $rounds++ < 200) {
            $any_added = false;
            foreach ($by_genre as &$gtracks) {
                if (empty($gtracks) || count($result) >= $count) break;
                $block = array_splice($gtracks, 0, $block_size);
                foreach ($block as $t) {
                    if (count($result) >= $count) break;
                    $result[]  = $t;
                    $any_added = true;
                }
            }
            unset($gtracks);
            if (!$any_added) break;
        }

        return $result;
    }

    /**
     * Energy flow: we sequence tracks by energy_level.
     * Tracks without energy data are assigned 0.5 (neutral).
     * direction: 'ascending' | 'descending' | 'wave'
     */
    private static function algo_energy_flow(array $pool, int $count, string $direction = 'wave'): array
    {
        // We assign neutral energy to un-tagged tracks
        $pool = array_map(function ($t) {
            $t['energy_level'] = ($t['energy_level'] !== null) ? (float)$t['energy_level'] : 0.5;
            return $t;
        }, $pool);

        if ($direction === 'ascending') {
            usort($pool, fn($a, $b) => $a['energy_level'] <=> $b['energy_level']);
        } elseif ($direction === 'descending') {
            usort($pool, fn($a, $b) => $b['energy_level'] <=> $a['energy_level']);
        } else {
            // wave: ascending first half, descending second half — creates an energy arc
            usort($pool, fn($a, $b) => $a['energy_level'] <=> $b['energy_level']);
            $mid  = (int)ceil(count($pool) / 2);
            $asc  = array_slice($pool, 0, $mid);
            $desc = array_reverse(array_slice($pool, $mid));
            $pool = array_merge($asc, $desc);
        }

        return array_slice($pool, 0, $count);
    }

    /**
     * AI adaptive: multi-factor composite score.
     * We weight five factors: freshness, rating, weight, play count, energy.
     * Artist separation is enforced as a hard constraint.
     * Future phases will feed listener engagement delta back into track weights.
     */
    private static function algo_ai_adaptive(array $pool, int $count, array $rules): array
    {
        $now_ts     = time();
        $artist_sep = max(1, (int)($rules['artist_separation'] ?? 3));

        // Normalise play_count over the pool
        $plays       = array_column($pool, 'play_count');
        $max_plays   = max(1, max($plays ?: [1]));

        // We compute AI scores for all candidates up front
        $scored = array_map(function ($t) use ($now_ts, $max_plays) {
            $play_norm   = 1.0 - (float)($t['play_count'] ?? 0) / $max_plays;      // 0=hot, 1=fresh
            $rating_norm = ((float)($t['rating'] ?? 3) - 1.0) / 4.0;              // 0-1
            $weight_norm = min(1.0, max(0.0, (float)($t['weight'] ?? 1.0) / 10.0));// 0-1
            $energy_norm = (float)($t['energy_level'] ?? 0.5);                     // 0-1

            $freshness = 1.0;
            if (!empty($t['last_played_at'])) {
                $hrs       = ($now_ts - (int)strtotime($t['last_played_at'])) / 3600.0;
                $freshness = min(1.0, $hrs / 168.0); // 0 = played now, 1 = ≥ 1 week ago
            }

            // Weighted sum: freshness 35 %, rating 30 %, weight 20 %, play 10 %, energy 5 %
            $t['_ai_score'] = $freshness * 0.35
                            + $rating_norm * 0.30
                            + $weight_norm * 0.20
                            + $play_norm   * 0.10
                            + $energy_norm * 0.05;
            return $t;
        }, $pool);

        // Sort by AI score descending (best first)
        usort($scored, fn($a, $b) => $b['_ai_score'] <=> $a['_ai_score']);

        $result         = [];
        $used_ids       = [];
        $recent_artists = [];

        // First pass: respect artist separation
        foreach ($scored as $t) {
            if (count($result) >= $count) break;
            if (isset($used_ids[$t['id']])) continue;
            $window = array_slice($recent_artists, -$artist_sep);
            if (in_array($t['artist'], $window, true)) continue;
            $result[]              = $t;
            $used_ids[$t['id']]    = true;
            $recent_artists[]      = $t['artist'];
        }

        // Second pass: fill remaining slots without separation constraint
        if (count($result) < $count) {
            foreach ($scored as $t) {
                if (count($result) >= $count) break;
                if (isset($used_ids[$t['id']])) continue;
                $result[]           = $t;
                $used_ids[$t['id']] = true;
            }
        }

        return $result;
    }

    /**
     * Daypart: we select tracks whose energy_level fits the current time-of-day.
     * Morning (06–12): high energy 0.6–1.0
     * Afternoon (12–18): medium-high 0.4–0.8
     * Evening (18–22): medium 0.3–0.7
     * Overnight (22–06): low 0.0–0.5
     * Falls back to the full pool if too few tracks have energy data.
     */
    private static function algo_daypart(array $pool, int $count, array $opts): array
    {
        $hour = isset($opts['daypart_hour']) ? (int)$opts['daypart_hour'] : (int)date('G');

        if ($hour >= 6 && $hour < 12) {
            $min_e = 0.6; $max_e = 1.0;
        } elseif ($hour >= 12 && $hour < 18) {
            $min_e = 0.4; $max_e = 0.8;
        } elseif ($hour >= 18 && $hour < 22) {
            $min_e = 0.3; $max_e = 0.7;
        } else {
            $min_e = 0.0; $max_e = 0.5;
        }

        $daypart_pool = array_values(array_filter($pool, function ($t) use ($min_e, $max_e) {
            $e = ($t['energy_level'] !== null) ? (float)$t['energy_level'] : 0.5;
            return $e >= $min_e && $e <= $max_e;
        }));

        // We need at least 10 tracks to use daypart filtering; otherwise fall back
        if (count($daypart_pool) < 10) {
            $daypart_pool = $pool;
        }

        return self::algo_weighted_random($daypart_pool, $count);
    }

    // ── Helper methods ────────────────────────────────────────────────────

    /**
     * We compute a composite score for smart_rotation candidate selection.
     * Lower score = better candidate (we pick the minimum).
     */
    private static function score_track(array $t, array $rules): float
    {
        $play_penalty  = (float)($t['play_count'] ?? 0) * 2.0;
        $rating_bonus  = ((float)($t['rating'] ?? 3) - 1.0) * -5.0; // higher rating → lower score
        $weight_bonus  = ((float)($t['weight']  ?? 1.0) - 1.0) * -3.0;
        $fresh_bonus   = 0.0;

        if (!empty($t['last_played_at'])) {
            $hrs         = (time() - (int)strtotime($t['last_played_at'])) / 3600.0;
            $fresh_bonus = -min(20.0, $hrs / 24.0 * 5.0); // more hours since play = lower score
        }

        return $play_penalty + $rating_bonus + $weight_bonus + $fresh_bonus;
    }

    /**
     * We draw a single track from the pool using cumulative-weight sampling,
     * skipping any track ID in $used_ids.
     *
     * Returns null when the entire eligible pool is exhausted.
     */
    private static function weighted_pick(array $pool, array $used_ids = []): ?array
    {
        $eligible = array_values(array_filter($pool, fn($t) => !isset($used_ids[$t['id']])));
        if (empty($eligible)) return null;

        $total = 0.0;
        foreach ($eligible as $t) {
            $total += max(0.0001, (float)($t['weight'] ?? 1.0));
        }

        $rand = (mt_rand() / mt_getrandmax()) * $total;
        $cumulative = 0.0;
        foreach ($eligible as $t) {
            $cumulative += max(0.0001, (float)($t['weight'] ?? 1.0));
            if ($rand <= $cumulative) return $t;
        }

        return end($eligible) ?: null;
    }

    /**
     * We interleave jingles into a result array every $every_n music tracks.
     * Jingles rotate through the jingle pool cyclically.
     */
    private static function interleave_jingles(array &$result, array $jingles, int $every_n): void
    {
        if ($every_n < 1 || empty($jingles)) return;

        $j_idx  = 0;
        $j_cnt  = count($jingles);
        $i      = $every_n; // position of first insertion

        while ($i < count($result)) {
            $jingle = $jingles[$j_idx % $j_cnt];
            array_splice($result, $i, 0, [$jingle]);
            $j_idx++;
            $i += $every_n + 1; // advance past inserted jingle
        }
    }
}
