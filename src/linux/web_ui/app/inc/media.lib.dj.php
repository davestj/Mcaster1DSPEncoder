<?php
/**
 * media.lib.dj.php — DJ Queue Library for Dual-Deck Player
 *
 * File:    src/linux/web_ui/app/inc/media.lib.dj.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-04-03
 * Purpose: We bootstrap the dj_queue table and provide helper functions
 *          for the dual-deck DJ queue (Auto-DJ + manual mode).
 *          Required by app/api/dj.php and dualdeck-player.php.
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We use raw SQL only (no ORMs)
 *  - We use mc1_db() for all database access
 */

if (!defined('MC1_BOOT')) {
    http_response_code(403);
    echo '403 Forbidden';
    return;
}

/* We reuse the session key logic from the media player */
require_once __DIR__ . '/media.lib.player.php';

/* ── Bootstrap dj_queue table on every load ────────────────────────────────── */
(function() {
    try {
        mc1_db('mcaster1_media')->exec("
            CREATE TABLE IF NOT EXISTS dj_queue (
                id           INT UNSIGNED      NOT NULL AUTO_INCREMENT PRIMARY KEY,
                session_key  VARCHAR(128)      NOT NULL DEFAULT ''
                             COMMENT 'Scoped per DJ session via mc1_player_session_key()',
                track_id     INT UNSIGNED      NOT NULL,
                position     SMALLINT UNSIGNED NOT NULL DEFAULT 0,
                source       ENUM('manual','auto') NOT NULL DEFAULT 'manual'
                             COMMENT 'Whether added by DJ or auto-fill algorithm',
                added_at     DATETIME          NOT NULL DEFAULT CURRENT_TIMESTAMP,
                INDEX idx_session  (session_key),
                INDEX idx_position (session_key, position)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
            COMMENT='We store the DJ queue for the dual-deck player (auto-DJ + manual)'
        ");
    } catch (Exception $e) {
        mc1_log(2, 'dj_queue table bootstrap failed', json_encode(['err' => $e->getMessage()]));
    }
})();

/* ────────────────────────────────────────────────────────────────────────────
 * mc1_dj_queue_list($sk) — We return all DJ queue rows joined with track data.
 * ─────────────────────────────────────────────────────────────────────────── */
function mc1_dj_queue_list(string $sk): array {
    try {
        $pdo  = mc1_db('mcaster1_media');
        $stmt = $pdo->prepare(
            'SELECT dq.id AS queue_id, dq.position, dq.source, dq.added_at,
                    t.id AS track_id, t.title, t.artist, t.album, t.genre,
                    t.duration_ms, t.file_path
             FROM dj_queue dq
             JOIN tracks t ON t.id = dq.track_id
             WHERE dq.session_key = ?
             ORDER BY dq.position ASC, dq.id ASC'
        );
        $stmt->execute([$sk]);
        $rows = $stmt->fetchAll(\PDO::FETCH_ASSOC);
        foreach ($rows as &$r) {
            $r['queue_id']    = (int)$r['queue_id'];
            $r['position']    = (int)$r['position'];
            $r['track_id']    = (int)$r['track_id'];
            $r['duration_ms'] = (int)$r['duration_ms'];
        }
        unset($r);
        return $rows;
    } catch (Exception $e) {
        mc1_log(2, 'mc1_dj_queue_list failed', json_encode(['err' => $e->getMessage()]));
        return [];
    }
}

/* ────────────────────────────────────────────────────────────────────────────
 * mc1_dj_queue_add($sk, $track_ids, $source, $play_next) — We append or
 * insert track(s). If $play_next is true, we insert at position 1.
 * ─────────────────────────────────────────────────────────────────────────── */
function mc1_dj_queue_add(string $sk, array $track_ids, string $source = 'manual', bool $play_next = false): int {
    if (empty($track_ids)) return 0;
    try {
        $pdo = mc1_db('mcaster1_media');

        if ($play_next) {
            /* We shift all existing positions up to make room at position 1 */
            $pdo->prepare('UPDATE dj_queue SET position = position + ? WHERE session_key = ?')
                ->execute([count($track_ids), $sk]);
            $pos = 0;
        } else {
            $ms = $pdo->prepare('SELECT COALESCE(MAX(position), 0) FROM dj_queue WHERE session_key = ?');
            $ms->execute([$sk]);
            $pos = (int)$ms->fetchColumn();
        }

        $ins   = $pdo->prepare(
            'INSERT INTO dj_queue (session_key, track_id, position, source, added_at)
             VALUES (?, ?, ?, ?, NOW())'
        );
        $added = 0;
        foreach ($track_ids as $tid) {
            $tid = (int)$tid;
            if ($tid < 1) continue;
            $pos++;
            $ins->execute([$sk, $tid, $pos, $source]);
            $added += $ins->rowCount();
        }
        return $added;
    } catch (Exception $e) {
        mc1_log(2, 'mc1_dj_queue_add failed', json_encode(['err' => $e->getMessage()]));
        return 0;
    }
}

/* ────────────────────────────────────────────────────────────────────────────
 * mc1_dj_queue_remove($sk, $queue_id) — We remove one entry and renumber.
 * ─────────────────────────────────────────────────────────────────────────── */
function mc1_dj_queue_remove(string $sk, int $queue_id): void {
    try {
        $pdo = mc1_db('mcaster1_media');
        $pdo->prepare('DELETE FROM dj_queue WHERE id = ? AND session_key = ?')
            ->execute([$queue_id, $sk]);
        _mc1_dj_renumber($pdo, $sk);
    } catch (Exception $e) {
        mc1_log(2, 'mc1_dj_queue_remove failed', json_encode(['err' => $e->getMessage()]));
    }
}

/* ────────────────────────────────────────────────────────────────────────────
 * mc1_dj_queue_clear($sk) — We delete all DJ queue rows for this session.
 * ─────────────────────────────────────────────────────────────────────────── */
function mc1_dj_queue_clear(string $sk): void {
    try {
        mc1_db('mcaster1_media')
            ->prepare('DELETE FROM dj_queue WHERE session_key = ?')
            ->execute([$sk]);
    } catch (Exception $e) {
        mc1_log(2, 'mc1_dj_queue_clear failed', json_encode(['err' => $e->getMessage()]));
    }
}

/* ────────────────────────────────────────────────────────────────────────────
 * mc1_dj_queue_move_top($sk, $queue_id) — We move entry to position 1.
 * ─────────────────────────────────────────────────────────────────────────── */
function mc1_dj_queue_move_top(string $sk, int $queue_id): void {
    try {
        $pdo = mc1_db('mcaster1_media');
        /* We set target to 0, shift everything else up, then renumber */
        $pdo->prepare('UPDATE dj_queue SET position = 0 WHERE id = ? AND session_key = ?')
            ->execute([$queue_id, $sk]);
        _mc1_dj_renumber($pdo, $sk);
    } catch (Exception $e) {
        mc1_log(2, 'mc1_dj_queue_move_top failed', json_encode(['err' => $e->getMessage()]));
    }
}

/* ────────────────────────────────────────────────────────────────────────────
 * mc1_dj_queue_pop($sk) — We remove and return the first (position-1) entry.
 * ─────────────────────────────────────────────────────────────────────────── */
function mc1_dj_queue_pop(string $sk): ?array {
    try {
        $pdo  = mc1_db('mcaster1_media');
        $stmt = $pdo->prepare(
            'SELECT dq.id AS queue_id, dq.position, dq.source,
                    t.id AS track_id, t.title, t.artist, t.album,
                    t.duration_ms, t.file_path
             FROM dj_queue dq
             JOIN tracks t ON t.id = dq.track_id
             WHERE dq.session_key = ?
             ORDER BY dq.position ASC, dq.id ASC
             LIMIT 1'
        );
        $stmt->execute([$sk]);
        $row = $stmt->fetch(\PDO::FETCH_ASSOC);
        if (!$row) return null;

        $row['queue_id']    = (int)$row['queue_id'];
        $row['track_id']    = (int)$row['track_id'];
        $row['duration_ms'] = (int)$row['duration_ms'];

        /* We delete the popped entry and renumber */
        $pdo->prepare('DELETE FROM dj_queue WHERE id = ?')->execute([$row['queue_id']]);
        _mc1_dj_renumber($pdo, $sk);

        return $row;
    } catch (Exception $e) {
        mc1_log(2, 'mc1_dj_queue_pop failed', json_encode(['err' => $e->getMessage()]));
        return null;
    }
}

/* ────────────────────────────────────────────────────────────────────────────
 * mc1_dj_queue_count($sk) — We return the number of queued tracks.
 * ─────────────────────────────────────────────────────────────────────────── */
function mc1_dj_queue_count(string $sk): int {
    try {
        $stmt = mc1_db('mcaster1_media')
            ->prepare('SELECT COUNT(*) FROM dj_queue WHERE session_key = ?');
        $stmt->execute([$sk]);
        return (int)$stmt->fetchColumn();
    } catch (Exception $e) {
        return 0;
    }
}

/* ── Internal: renumber positions to be contiguous 1,2,3... ──────────────── */
function _mc1_dj_renumber(\PDO $pdo, string $sk): void {
    try {
        /* We use a subquery to assign sequential positions ordered by current position, id */
        $rows = $pdo->prepare(
            'SELECT id FROM dj_queue WHERE session_key = ? ORDER BY position ASC, id ASC'
        );
        $rows->execute([$sk]);
        $ids = $rows->fetchAll(\PDO::FETCH_COLUMN);
        $upd = $pdo->prepare('UPDATE dj_queue SET position = ? WHERE id = ?');
        foreach ($ids as $i => $id) {
            $upd->execute([$i + 1, $id]);
        }
    } catch (Exception $e) {
        /* We silently ignore renumber failures — positions may be non-contiguous but still functional */
    }
}
