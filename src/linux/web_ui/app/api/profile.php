<?php
/**
 * profile.php — User Profile API
 *
 * File:    src/linux/web_ui/app/api/profile.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-02-24
 * Purpose: We handle all profile management for the current authenticated user:
 *          view profile, update info, change password, list/revoke sessions.
 *          Admins can also set per-user session TTL overrides.
 *
 * Actions (all POST JSON, all require auth):
 *  get                  — fetch current user's full profile
 *  update               — save display_name, email, bio
 *  change_password      — validate old password, set new bcrypt hash
 *  get_sessions         — list all active sessions for the current user
 *  revoke_session       — revoke a specific session by its DB id
 *  revoke_all_others    — revoke all sessions except the current one
 *  set_session_ttl      — (admin) set session_ttl_override for any user
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We use mc1_api_respond() for all JSON responses
 *  - We use first-person plural in all comments
 *
 * CHANGELOG:
 *  2026-02-24  v1.0.0  Initial implementation
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/../inc/mc1_config.php';
require_once __DIR__ . '/../inc/db.php';
require_once __DIR__ . '/../inc/traits.db.class.php';
require_once __DIR__ . '/../inc/logger.php';
require_once __DIR__ . '/../inc/auth.php';
require_once __DIR__ . '/../inc/user_auth.php';
require_once __DIR__ . '/../inc/profile.usr.class.php';

header('Content-Type: application/json');

/* ── Auth gate — both layers required ───────────────────────────────────── */
if (!mc1_is_authed()) {
    mc1_api_respond(['error' => 'Unauthorized'], 403);
    return;
}

$user = mc1_current_user();
if (!$user) {
    mc1_api_respond(['error' => 'No PHP session — try refreshing'], 401);
    return;
}

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    mc1_api_respond(['error' => 'POST required'], 405);
    return;
}

$raw    = (string)file_get_contents('php://input');
$body   = json_decode($raw, true) ?: [];
$action = (string)($body['action'] ?? '');

$user_id      = (int)$user['id'];
$is_admin     = !empty($user['can_admin']);

/* ── We derive the current session token hash for session operations ─────── */
$current_cookie = $_COOKIE['mc1app_session'] ?? '';
$current_hash   = $current_cookie !== '' ? mc1_hash_token($current_cookie) : '';

/* ══════════════════════════════════════════════════════════════════════════
 * action: get — return full profile for current user
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'get') {
    $profile = Mc1UserProfile::getProfile($user_id);
    if (!$profile) {
        mc1_api_respond(['error' => 'Profile not found'], 404);
        return;
    }
    /* We attach session expiry info */
    $expiry = Mc1UserProfile::getSessionExpiry($current_hash);
    mc1_api_respond(['ok' => true, 'profile' => $profile, 'session' => $expiry]);
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: update — save display_name, email, bio
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'update') {
    $data = [
        'display_name' => $body['display_name'] ?? null,
        'email'        => $body['email']        ?? null,
        'bio'          => $body['bio']           ?? null,
    ];
    /* We strip null values so only provided fields are updated */
    $data = array_filter($data, fn($v) => $v !== null);

    if (empty($data)) {
        mc1_api_respond(['error' => 'No fields provided'], 400);
        return;
    }

    /* We validate email format if provided */
    if (isset($data['email']) && $data['email'] !== '' &&
        !filter_var($data['email'], FILTER_VALIDATE_EMAIL)) {
        mc1_api_respond(['error' => 'Invalid email address'], 400);
        return;
    }

    $ok = Mc1UserProfile::updateProfile($user_id, $data);
    if (!$ok) {
        mc1_api_respond(['error' => 'Profile update failed'], 500);
        return;
    }

    mc1_log(4, 'profile.update', json_encode(['uid' => $user_id, 'fields' => array_keys($data)]));
    mc1_api_respond(['ok' => true, 'message' => 'Profile updated']);
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: change_password — validate old password, set new bcrypt hash
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'change_password') {
    $old = (string)($body['old_password'] ?? '');
    $new = (string)($body['new_password'] ?? '');
    $cfm = (string)($body['confirm_password'] ?? '');

    if ($new !== $cfm) {
        mc1_api_respond(['error' => 'New passwords do not match'], 400);
        return;
    }

    $result = Mc1UserProfile::changePassword($user_id, $old, $new);
    if (!$result['ok']) {
        mc1_api_respond(['error' => $result['error']], 400);
        return;
    }

    mc1_log(3, 'profile.password_changed', json_encode(['uid' => $user_id]));
    mc1_api_respond(['ok' => true, 'message' => 'Password updated successfully']);
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: get_sessions — list all active sessions for the current user
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'get_sessions') {
    $sessions = Mc1UserProfile::getSessions($user_id, $current_hash);
    mc1_api_respond(['ok' => true, 'sessions' => $sessions, 'count' => count($sessions)]);
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: revoke_session — revoke a specific session by DB id
 * We verify ownership — users can only revoke their own sessions.
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'revoke_session') {
    $session_id = (int)($body['session_id'] ?? 0);
    if ($session_id < 1) {
        mc1_api_respond(['error' => 'session_id required'], 400);
        return;
    }

    $ok = Mc1UserProfile::revokeSession($session_id, $user_id);
    mc1_api_respond(['ok' => $ok]);
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: revoke_all_others — revoke all sessions except current
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'revoke_all_others') {
    $revoked = Mc1UserProfile::revokeAllOtherSessions($user_id, $current_hash);
    mc1_api_respond(['ok' => true, 'revoked' => $revoked]);
    return;
}

/* ══════════════════════════════════════════════════════════════════════════
 * action: set_session_ttl — (admin only) set per-user session TTL override
 * We accept target_user_id and ttl_override (NULL, 0, or N seconds).
 * ══════════════════════════════════════════════════════════════════════════ */
if ($action === 'set_session_ttl') {
    if (!$is_admin) {
        mc1_api_respond(['error' => 'Admin permission required'], 403);
        return;
    }

    $target_id   = (int)($body['user_id'] ?? 0);
    /* We accept null (default), 0 (never expire), or positive integer (seconds) */
    $ttl_raw = $body['ttl_override'] ?? 'default';
    if ($ttl_raw === 'default' || $ttl_raw === null || $ttl_raw === '') {
        $ttl = null;
    } elseif ((string)(int)$ttl_raw === (string)$ttl_raw) {
        $ttl = max(0, (int)$ttl_raw);
    } else {
        mc1_api_respond(['error' => 'ttl_override must be null, 0, or positive integer (seconds)'], 400);
        return;
    }

    if ($target_id < 1) {
        mc1_api_respond(['error' => 'user_id required'], 400);
        return;
    }

    $ok = Mc1UserProfile::setSessionTtlOverride($target_id, $ttl);
    mc1_log(3, 'profile.set_session_ttl', json_encode([
        'admin_id' => $user_id, 'target_id' => $target_id, 'ttl' => $ttl
    ]));
    mc1_api_respond(['ok' => $ok]);
    return;
}

/* We return 400 for unknown actions */
mc1_api_respond(['error' => 'Unknown action: ' . $action], 400);
