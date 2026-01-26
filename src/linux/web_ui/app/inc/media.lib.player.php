<?php
/**
 * media.lib.player.php — Browser Media Player Library
 *
 * File:    src/linux/web_ui/app/inc/media.lib.player.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-02-24
 * Purpose: We bootstrap the player_queue table and provide helper functions
 *          for the per-session browser media player queue.
 *          Required by app/api/player.php and mediaplayer.php.
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We use first-person plural in all comments
 *  - We use raw SQL only (no ORMs)
 *  - We use mc1_db() for all database access
 *
 * CHANGELOG:
 *  2026-02-24  v1.0.0  Initial implementation — player_queue table + queue helpers
 */

if (!defined('MC1_BOOT')) {
    http_response_code(403);
    echo '403 Forbidden';
    return;
}

/* ── Bootstrap player_queue table on every load ──────────────────────────── */
(function() {
    try {
        mc1_db('mcaster1_media')->exec("
            CREATE TABLE IF NOT EXISTS player_queue (
                id           INT UNSIGNED      NOT NULL AUTO_INCREMENT PRIMARY KEY,
                user_id      INT UNSIGNED      NOT NULL DEFAULT 0
                             COMMENT '0 = session-based; future link to users.id',
                session_key  VARCHAR(128)      NOT NULL DEFAULT ''
                             COMMENT 'First 32 chars of mc1session cookie — scopes queue per browser session',
                track_id     INT UNSIGNED      NOT NULL,
                position     SMALLINT UNSIGNED NOT NULL DEFAULT 0,
                added_at     DATETIME          NOT NULL DEFAULT CURRENT_TIMESTAMP,
                INDEX idx_session  (session_key),
                INDEX idx_position (session_key, position)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
            COMMENT='We store the per-session preview/listen queue for the browser media player'
        ");
    } catch (Exception $e) {
        mc1_log(2, 'player_queue table bootstrap failed', json_encode(['err' => $e->getMessage()]));
    }
})();

/* ────────────────────────────────────────────────────────────────────────────
 * mc1_player_session_key() — We derive a unique queue scope key.
 * We use the first 32 hex chars of the C++ mc1session cookie.
 * This scopes each browser session's queue independently without a DB join.
 * ─────────────────────────────────────────────────────────────────────────── */
function mc1_player_session_key(): string {
    /* We prefer user-scoped queue keys so the queue survives logout/login for the
     * same user. mc1_current_user() is available when user_auth.php is required. */
    if (function_exists('mc1_current_user')) {
        $user = mc1_current_user();
        if ($user && !empty($user['id'])) {
            return 'u' . (int)$user['id'];
        }
    }
    /* We fall back to C++ session key when PHP session is not yet bootstrapped */
    $cookie = $_COOKIE['mc1session'] ?? '';
    if (strlen($cookie) >= 32) {
        return substr(preg_replace('/[^a-f0-9]/i', '', $cookie), 0, 32);
    }
    return substr(sha1(($_SERVER['REMOTE_ADDR'] ?? '') . '|' . ($_SERVER['HTTP_USER_AGENT'] ?? '')), 0, 32);
}

/* ────────────────────────────────────────────────────────────────────────────
 * mc1_player_queue_for_user($session_key) — We return all queue rows joined
 * with track data, ordered by position.
 * ─────────────────────────────────────────────────────────────────────────── */
function mc1_player_queue_for_user(string $session_key): array {
    try {
        $pdo  = mc1_db('mcaster1_media');
        $stmt = $pdo->prepare(
            'SELECT pq.id AS queue_id, pq.position, pq.added_at,
                    t.id AS track_id, t.title, t.artist, t.album, t.genre,
                    t.duration_ms, t.cover_art_hash, t.file_path
             FROM player_queue pq
             JOIN tracks t ON t.id = pq.track_id
             WHERE pq.session_key = ?
             ORDER BY pq.position ASC, pq.id ASC'
        );
        $stmt->execute([$session_key]);
        $rows = $stmt->fetchAll(\PDO::FETCH_ASSOC);
        foreach ($rows as &$r) {
            $r['queue_id']    = (int)$r['queue_id'];
            $r['position']    = (int)$r['position'];
            $r['track_id']    = (int)$r['track_id'];
            $r['duration_ms'] = (int)$r['duration_ms'];
            $r['art_url']     = $r['cover_art_hash']
                ? '/app/api/artwork.php?h=' . $r['cover_art_hash']
                : null;
        }
        unset($r);
        return $rows;
    } catch (Exception $e) {
        mc1_log(2, 'mc1_player_queue_for_user failed', json_encode(['sk' => substr($session_key, 0, 8), 'err' => $e->getMessage()]));
        return [];
    }
}

/* ────────────────────────────────────────────────────────────────────────────
 * mc1_player_queue_add($session_key, $track_ids) — We append track(s) to the
 * end of the queue and return the count of rows successfully inserted.
 * ─────────────────────────────────────────────────────────────────────────── */
function mc1_player_queue_add(string $session_key, array $track_ids): int {
    if (empty($track_ids)) { return 0; }
    try {
        $pdo = mc1_db('mcaster1_media');
        /* We get the current max position so new tracks append cleanly */
        $ms = $pdo->prepare('SELECT COALESCE(MAX(position), 0) FROM player_queue WHERE session_key = ?');
        $ms->execute([$session_key]);
        $pos = (int)$ms->fetchColumn();

        $ins   = $pdo->prepare(
            'INSERT INTO player_queue (session_key, track_id, position, added_at)
             VALUES (?, ?, ?, NOW())'
        );
        $added = 0;
        foreach ($track_ids as $tid) {
            $tid = (int)$tid;
            if ($tid < 1) { continue; }
            $pos++;
            $ins->execute([$session_key, $tid, $pos]);
            $added += $ins->rowCount();
        }
        return $added;
    } catch (Exception $e) {
        mc1_log(2, 'mc1_player_queue_add failed', json_encode(['sk' => substr($session_key, 0, 8), 'err' => $e->getMessage()]));
        return 0;
    }
}

/* ────────────────────────────────────────────────────────────────────────────
 * mc1_player_queue_clear($session_key) — We delete all queue rows for this
 * session. Used by the "Clear Queue" button.
 * ─────────────────────────────────────────────────────────────────────────── */
function mc1_player_queue_clear(string $session_key): void {
    try {
        mc1_db('mcaster1_media')
            ->prepare('DELETE FROM player_queue WHERE session_key = ?')
            ->execute([$session_key]);
    } catch (Exception $e) {
        mc1_log(2, 'mc1_player_queue_clear failed', json_encode(['sk' => substr($session_key, 0, 8), 'err' => $e->getMessage()]));
    }
}
