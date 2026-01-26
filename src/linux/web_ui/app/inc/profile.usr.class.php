<?php
/**
 * profile.usr.class.php — User Profile Management
 *
 * File:    src/linux/web_ui/app/inc/profile.usr.class.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-02-24
 * Purpose: We provide all profile management operations: view/edit profile,
 *          change password, list/revoke sessions, and per-user session TTL.
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We use Mc1Db trait for all DB operations
 *  - We use first-person plural in all comments
 *  - We use raw SQL only (no ORMs)
 *
 * CHANGELOG:
 *  2026-02-24  v1.0.0  Initial implementation
 */

if (!defined('MC1_BOOT')) {
    http_response_code(403);
    echo '403 Forbidden';
}

require_once __DIR__ . '/traits.db.class.php';
require_once __DIR__ . '/db.php';
require_once __DIR__ . '/user_auth.php';

class Mc1UserProfile
{
    use Mc1Db;

    /* ── Fetch full profile for a given user ────────────────────────────── */

    public static function getProfile(int $user_id): ?array
    {
        if ($user_id < 1) return null;

        $row = self::row(
            'mcaster1_encoder',
            'SELECT u.id, u.username, u.display_name, u.email, u.bio, u.avatar_url,
                    u.is_active, u.last_login, u.created_at, u.session_ttl_override,
                    r.id AS role_id, r.name AS role_name, r.label AS role_label,
                    r.can_admin, r.can_encode_control, r.can_playlist, r.can_metadata,
                    r.can_media_library, r.can_metrics, r.can_podcast, r.can_schedule
             FROM users u
             JOIN roles r ON r.id = u.role_id
             WHERE u.id = ?
             LIMIT 1',
            [$user_id]
        );

        return $row ?: null;
    }

    /* ── Update profile fields (display_name, email, bio) ───────────────── */

    public static function updateProfile(int $user_id, array $data): bool
    {
        if ($user_id < 1) return false;

        $display_name = isset($data['display_name']) ? trim($data['display_name']) : null;
        $email        = isset($data['email'])        ? trim($data['email'])        : null;
        $bio          = isset($data['bio'])          ? trim($data['bio'])          : null;

        $sets = [];
        $vals = [];

        if ($display_name !== null) { $sets[] = 'display_name = ?'; $vals[] = $display_name; }
        if ($email        !== null) { $sets[] = 'email = ?';        $vals[] = $email;        }
        if ($bio          !== null) { $sets[] = 'bio = ?';          $vals[] = $bio;           }

        if (empty($sets)) return false;

        $vals[] = $user_id;

        try {
            self::run(
                'mcaster1_encoder',
                'UPDATE users SET ' . implode(', ', $sets) . ' WHERE id = ?',
                $vals
            );
            return true;
        } catch (Exception $e) {
            mc1_log(2, 'profile.updateProfile failed', json_encode(['uid' => $user_id, 'err' => $e->getMessage()]));
            return false;
        }
    }

    /* ── Change password (validates old password, hashes new) ───────────── */
    /* We return ['ok'=>bool, 'error'=>string|null]. */

    public static function changePassword(int $user_id, string $old_pass, string $new_pass): array
    {
        if ($user_id < 1)         return ['ok' => false, 'error' => 'Invalid user'];
        if (strlen($new_pass) < 8) return ['ok' => false, 'error' => 'New password must be at least 8 characters'];

        try {
            $pdo  = mc1_db('mcaster1_encoder');
            $stmt = $pdo->prepare('SELECT password_hash FROM users WHERE id = ? AND is_active = 1 LIMIT 1');
            $stmt->execute([$user_id]);
            $row  = $stmt->fetch(\PDO::FETCH_ASSOC);

            if (!$row) return ['ok' => false, 'error' => 'User not found'];

            if (!password_verify($old_pass, $row['password_hash'])) {
                return ['ok' => false, 'error' => 'Current password is incorrect'];
            }

            $new_hash = password_hash($new_pass, PASSWORD_BCRYPT);
            $pdo->prepare('UPDATE users SET password_hash = ? WHERE id = ?')
                ->execute([$new_hash, $user_id]);

            mc1_log(4, 'profile.changePassword', json_encode(['uid' => $user_id]));
            return ['ok' => true, 'error' => null];

        } catch (Exception $e) {
            return ['ok' => false, 'error' => 'Database error: ' . $e->getMessage()];
        }
    }

    /* ── List all active sessions for a user ────────────────────────────── */

    public static function getSessions(int $user_id, string $current_token_hash = ''): array
    {
        if ($user_id < 1) return [];

        $rows = self::rows(
            'mcaster1_encoder',
            "SELECT id, token_hash, ip_address, user_agent, created_at, expires_at
             FROM user_sessions
             WHERE user_id = ?
               AND expires_at > NOW()
             ORDER BY created_at DESC",
            [$user_id]
        );

        /* We mark the current session so the UI can show "this session" */
        foreach ($rows as &$row) {
            $row['is_current'] = ($row['token_hash'] === $current_token_hash);
            /* We strip the hash from the output — it is sensitive */
            unset($row['token_hash']);
        }
        unset($row);

        return $rows;
    }

    /* ── Revoke a single session by its DB id (not token hash) ──────────── */
    /* We verify user_id ownership before deleting. */

    public static function revokeSession(int $session_id, int $user_id): bool
    {
        if ($session_id < 1 || $user_id < 1) return false;

        try {
            self::run(
                'mcaster1_encoder',
                'DELETE FROM user_sessions WHERE id = ? AND user_id = ?',
                [$session_id, $user_id]
            );
            return true;
        } catch (Exception $e) {
            return false;
        }
    }

    /* ── Revoke all sessions except the current one ─────────────────────── */

    public static function revokeAllOtherSessions(int $user_id, string $current_token_hash): int
    {
        if ($user_id < 1) return 0;

        try {
            $pdo  = mc1_db('mcaster1_encoder');
            $stmt = $pdo->prepare(
                'DELETE FROM user_sessions WHERE user_id = ? AND token_hash != ?'
            );
            $stmt->execute([$user_id, $current_token_hash]);
            return (int)$stmt->rowCount();
        } catch (Exception $e) {
            return 0;
        }
    }

    /* ── Admin: set per-user session TTL override ───────────────────────── */
    /* NULL = use system default (30d), 0 = never expire, N = seconds. */

    public static function setSessionTtlOverride(int $target_user_id, ?int $ttl): bool
    {
        if ($target_user_id < 1) return false;

        try {
            self::run(
                'mcaster1_encoder',
                'UPDATE users SET session_ttl_override = ? WHERE id = ?',
                [$ttl, $target_user_id]
            );
            return true;
        } catch (Exception $e) {
            return false;
        }
    }

    /* ── Get effective session TTL for a user ───────────────────────────── */
    /* Returns seconds (0 = never expire, represented as 99 years). */

    public static function getEffectiveTtl(int $user_id): int
    {
        try {
            $pdo  = mc1_db('mcaster1_encoder');
            $stmt = $pdo->prepare('SELECT session_ttl_override FROM users WHERE id = ? LIMIT 1');
            $stmt->execute([$user_id]);
            $val  = $stmt->fetchColumn();

            if ($val === false || $val === null) {
                /* We use the system default: 30 days */
                return defined('MC1_SESSION_TTL') ? MC1_SESSION_TTL : 86400 * 30;
            }

            $ttl = (int)$val;
            /* We treat 0 as "never expire" = 10 years in seconds */
            return ($ttl === 0) ? (86400 * 3650) : $ttl;

        } catch (Exception $e) {
            return 86400 * 30;
        }
    }

    /* ── Get session expiry info for the current session ────────────────── */

    public static function getSessionExpiry(string $token_hash): ?array
    {
        if ($token_hash === '') return null;

        $row = self::row(
            'mcaster1_encoder',
            'SELECT expires_at,
                    TIMESTAMPDIFF(SECOND, NOW(), expires_at) AS seconds_remaining
             FROM user_sessions WHERE token_hash = ? LIMIT 1',
            [$token_hash]
        );

        if (!$row) return null;

        $secs = (int)$row['seconds_remaining'];
        return [
            'expires_at'        => $row['expires_at'],
            'seconds_remaining' => $secs,
            'days_remaining'    => max(0, (int)($secs / 86400)),
            'never_expires'     => ($secs > 86400 * 3000), /* > 8 years = effectively never */
        ];
    }
}
