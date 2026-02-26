<?php
/**
 * player.php — Media Player Queue API
 *
 * File:    src/linux/web_ui/app/api/player.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-02-24
 * Purpose: We provide all queue management operations for the browser media player.
 *          The queue is scoped per browser session via the mc1session cookie.
 *
 * Actions (all POST JSON, all require auth):
 *  queue_list    — return the full queue for the current session
 *  queue_add     — add one or more tracks to the end of the queue
 *  queue_remove  — remove a single entry by queue_id
 *  queue_clear   — delete all entries for the current session
 *  queue_reorder — reorder the queue by passing the new order of queue_ids
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We use mc1_api_respond() for all JSON responses
 *  - We use first-person plural throughout all comments
 *  - We use raw SQL only (no ORMs)
 *
 * CHANGELOG:
 *  2026-02-24  v1.0.0  Initial implementation — full queue CRUD
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/../inc/mc1_config.php';
require_once __DIR__ . '/../inc/db.php';
require_once __DIR__ . '/../inc/traits.db.class.php';
require_once __DIR__ . '/../inc/logger.php';
require_once __DIR__ . '/../inc/auth.php';
require_once __DIR__ . '/../inc/user_auth.php';          /* We need mc1_current_user() for user-scoped queue keys */
require_once __DIR__ . '/../inc/media.lib.player.php';  /* We load the queue helpers + table bootstrap */

header('Content-Type: application/json');

/* ── Auth gate ───────────────────────────────────────────────────────────── */
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
$sk     = mc1_player_session_key();   /* We scope all operations to this session */

/* ══════════════════════════════════════════════════════════════════════════
 * action: queue_list — return full queue for this session
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'queue_list') {
    $rows = mc1_player_queue_for_user($sk);
    mc1_api_respond(['ok' => true, 'queue' => $rows, 'count' => count($rows)]);
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: queue_add — add track(s) to end of queue
 * We accept either track_id (int) or track_ids (array of ints).
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

    /* We enforce a 500-track queue limit to prevent abuse */
    try {
        $pdo = mc1_db('mcaster1_media');
        $cnt_stmt = $pdo->prepare('SELECT COUNT(*) FROM player_queue WHERE session_key = ?');
        $cnt_stmt->execute([$sk]);
        $current_count = (int)$cnt_stmt->fetchColumn();
    } catch (Exception $e) {
        $current_count = 0;
    }

    if ($current_count + count($track_ids) > 500) {
        mc1_api_respond(['error' => 'Queue limit (500 tracks) reached — clear some tracks first'], 400);
        return;
    }

    $added = mc1_player_queue_add($sk, $track_ids);
    $queue = mc1_player_queue_for_user($sk);
    mc1_api_respond(['ok' => true, 'added' => $added, 'queue' => $queue]);
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: queue_remove — remove one entry by queue_id
 * We re-number positions after removal to keep them contiguous.
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'queue_remove') {
    $queue_id = (int)($body['queue_id'] ?? 0);
    if ($queue_id < 1) {
        mc1_api_respond(['error' => 'queue_id required'], 400);
        return;
    }
    try {
        $pdo = mc1_db('mcaster1_media');
        /* We verify session ownership before deleting */
        $del = $pdo->prepare('DELETE FROM player_queue WHERE id = ? AND session_key = ?');
        $del->execute([$queue_id, $sk]);

        /* We renumber positions using window function (MariaDB 10.11 supports ROW_NUMBER) */
        $pdo->prepare(
            'UPDATE player_queue pq
             JOIN (
                 SELECT id, ROW_NUMBER() OVER (ORDER BY position ASC, id ASC) AS new_pos
                 FROM player_queue WHERE session_key = ?
             ) r ON pq.id = r.id
             SET pq.position = r.new_pos
             WHERE pq.session_key = ?'
        )->execute([$sk, $sk]);
    } catch (Exception $e) {
        mc1_log(2, 'player queue_remove failed', json_encode(['qid' => $queue_id, 'err' => $e->getMessage()]));
        mc1_api_respond(['error' => 'Remove failed: ' . $e->getMessage()], 500);
        return;
    }
    $queue = mc1_player_queue_for_user($sk);
    mc1_api_respond(['ok' => true, 'queue' => $queue]);
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: queue_clear — delete all entries for this session
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'queue_clear') {
    mc1_player_queue_clear($sk);
    mc1_api_respond(['ok' => true, 'queue' => []]);
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: queue_reorder — we accept an ordered array of queue_ids
 * representing the desired play order, and update positions accordingly.
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'queue_reorder') {
    $order = $body['order'] ?? [];
    if (!is_array($order) || empty($order)) {
        mc1_api_respond(['error' => 'order array of queue_ids required'], 400);
        return;
    }
    try {
        $pdo = mc1_db('mcaster1_media');
        $upd = $pdo->prepare(
            'UPDATE player_queue SET position = ? WHERE id = ? AND session_key = ?'
        );
        $pos = 1;
        foreach ($order as $qid) {
            $upd->execute([$pos, (int)$qid, $sk]);
            $pos++;
        }
    } catch (Exception $e) {
        mc1_log(2, 'player queue_reorder failed', json_encode(['err' => $e->getMessage()]));
        mc1_api_respond(['error' => 'Reorder failed: ' . $e->getMessage()], 500);
        return;
    }
    $queue = mc1_player_queue_for_user($sk);
    mc1_api_respond(['ok' => true, 'queue' => $queue]);
    return;
}

/* We return 400 for unknown actions */
mc1_api_respond(['error' => 'Unknown action: ' . $action], 400);
