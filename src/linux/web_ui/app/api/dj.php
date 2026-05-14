<?php
/**
 * dj.php — DJ Queue + Auto-Fill API for Dual-Deck Player
 *
 * File:    src/linux/web_ui/app/api/dj.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-04-03
 * Purpose: We provide DJ queue CRUD and auto-fill operations for the dual-deck
 *          player. The queue is scoped per session via mc1_player_session_key().
 *
 * Actions (all POST JSON, all require auth):
 *  queue_list    — return the full DJ queue
 *  queue_add     — add track(s) to queue (end or play-next)
 *  queue_remove  — remove a single entry by queue_id
 *  queue_clear   — clear entire queue
 *  queue_move_top — move entry to position 1
 *  queue_pop     — remove and return the first track
 *  auto_fill     — server-side fill using PlaylistBuilderAlgorithm
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We use mc1_api_respond() for all JSON responses
 *  - We use raw SQL only (no ORMs)
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/../inc/mc1_config.php';
require_once __DIR__ . '/../inc/db.php';
require_once __DIR__ . '/../inc/traits.db.class.php';
require_once __DIR__ . '/../inc/logger.php';
require_once __DIR__ . '/../inc/auth.php';
require_once __DIR__ . '/../inc/user_auth.php';
require_once __DIR__ . '/../inc/media.lib.dj.php';

header('Content-Type: application/json; charset=utf-8');

if (!mc1_is_authed()) {
    mc1_api_respond(['error' => 'Unauthorized'], 403);
    return;
}
if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    mc1_api_respond(['error' => 'POST required'], 405);
    return;
}

$raw    = (string)file_get_contents('php://input');
$body   = json_decode($raw, true) ?: [];
$action = (string)($body['action'] ?? '');
$sk     = mc1_player_session_key();

/* ══════════════════════════════════════════════════════════════════════════
 * action: queue_list
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'queue_list') {
    $rows = mc1_dj_queue_list($sk);
    mc1_api_respond(['ok' => true, 'queue' => $rows, 'count' => count($rows)]);
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: queue_add — add track(s), optionally as play-next (position 1)
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'queue_add') {
    $track_ids = [];
    if (!empty($body['track_ids']) && is_array($body['track_ids'])) {
        $track_ids = array_map('intval', $body['track_ids']);
    } elseif (!empty($body['track_id'])) {
        $track_ids = [(int)$body['track_id']];
    }
    $track_ids = array_values(array_filter($track_ids, fn($id) => $id > 0));
    if (empty($track_ids)) {
        mc1_api_respond(['error' => 'track_id or track_ids required'], 400);
        return;
    }

    $play_next = !empty($body['play_next']);
    $source    = ($body['source'] ?? 'manual') === 'auto' ? 'auto' : 'manual';
    $added     = mc1_dj_queue_add($sk, $track_ids, $source, $play_next);
    $queue     = mc1_dj_queue_list($sk);
    mc1_api_respond(['ok' => true, 'added' => $added, 'queue' => $queue]);
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: queue_remove
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'queue_remove') {
    $queue_id = (int)($body['queue_id'] ?? 0);
    if ($queue_id < 1) {
        mc1_api_respond(['error' => 'queue_id required'], 400);
        return;
    }
    mc1_dj_queue_remove($sk, $queue_id);
    $queue = mc1_dj_queue_list($sk);
    mc1_api_respond(['ok' => true, 'queue' => $queue]);
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: queue_clear
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'queue_clear') {
    mc1_dj_queue_clear($sk);
    mc1_api_respond(['ok' => true, 'queue' => []]);
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: queue_move_top
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'queue_move_top') {
    $queue_id = (int)($body['queue_id'] ?? 0);
    if ($queue_id < 1) {
        mc1_api_respond(['error' => 'queue_id required'], 400);
        return;
    }
    mc1_dj_queue_move_top($sk, $queue_id);
    $queue = mc1_dj_queue_list($sk);
    mc1_api_respond(['ok' => true, 'queue' => $queue]);
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: queue_pop — remove and return the first track in the queue
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'queue_pop') {
    $track = mc1_dj_queue_pop($sk);
    if (!$track) {
        mc1_api_respond(['ok' => false, 'error' => 'Queue empty', 'track' => null]);
        return;
    }
    mc1_api_respond(['ok' => true, 'track' => $track, 'remaining' => mc1_dj_queue_count($sk)]);
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: auto_fill — server-side auto-DJ queue fill using playlist algorithms
 *
 * We use PlaylistBuilderAlgorithm to select tracks, respecting artist
 * separation and song repeat rules. We exclude tracks already in the queue.
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'auto_fill') {
    require_once __DIR__ . '/../inc/playlist.builder.algorithm.class.php';

    $algorithm   = (string)($body['algorithm'] ?? 'smart_rotation');
    $playlist_id = (int)($body['source_playlist_id'] ?? 0);
    $count       = max(1, min(50, (int)($body['count'] ?? 10)));
    $artist_sep  = max(0, min(20, (int)($body['artist_separation'] ?? 3)));
    $repeat_hrs  = max(0, min(168, (int)($body['song_repeat_hrs'] ?? 1)));

    /* We build the track pool */
    $filters = [];
    $pool = [];

    try {
        if ($playlist_id > 0) {
            /* We load tracks from a specific playlist */
            $pdo = mc1_db('mcaster1_media');
            $stmt = $pdo->prepare(
                'SELECT t.id, t.file_path, t.title, t.artist, t.album, t.genre,
                        t.year, t.duration_ms, t.bpm, t.rating, t.play_count,
                        t.last_played_at, COALESCE(t.weight, 1.0) AS weight,
                        0 AS energy_level, NULL AS mood_tag,
                        0 AS is_jingle, 0 AS is_sweeper, 0 AS is_spot
                 FROM playlist_tracks pt
                 JOIN tracks t ON t.id = pt.track_id
                 WHERE pt.playlist_id = ? AND t.is_missing = 0
                 ORDER BY pt.position'
            );
            $stmt->execute([$playlist_id]);
            $pool = $stmt->fetchAll(\PDO::FETCH_ASSOC);
        } else {
            /* We use all tracks via the standard pool builder */
            $pool = PlaylistBuilderAlgorithm::build_pool($filters);
        }
    } catch (Exception $e) {
        mc1_api_respond(['ok' => false, 'error' => 'Pool build failed: ' . $e->getMessage()], 500);
        return;
    }

    if (empty($pool)) {
        mc1_api_respond(['ok' => false, 'error' => 'No tracks available in the selected source']);
        return;
    }

    /* We get current queue track IDs so we can exclude them */
    $current_queue = mc1_dj_queue_list($sk);
    $exclude_ids = array_column($current_queue, 'track_id');

    /* We generate tracks using the selected algorithm */
    $opts = [
        'algorithm'   => $algorithm,
        'track_count' => $count + count($exclude_ids), /* We over-generate to account for exclusions */
        'rules'       => [
            'artist_separation'   => $artist_sep,
            'song_separation_hrs' => $repeat_hrs,
            'jingle_every_n'      => 0,
        ],
    ];

    $generated = PlaylistBuilderAlgorithm::generate($pool, $opts);

    /* We filter out tracks already in the queue and trim to requested count */
    $to_add = [];
    foreach ($generated as $track) {
        if (in_array((int)$track['id'], $exclude_ids, true)) continue;
        $to_add[] = (int)$track['id'];
        if (count($to_add) >= $count) break;
    }

    if (!empty($to_add)) {
        mc1_dj_queue_add($sk, $to_add, 'auto', false);
    }

    $queue = mc1_dj_queue_list($sk);
    mc1_api_respond([
        'ok'         => true,
        'added'      => count($to_add),
        'pool_size'  => count($pool),
        'queue'      => $queue,
    ]);
    return;
}

mc1_api_respond(['error' => 'Unknown action: ' . $action], 400);
