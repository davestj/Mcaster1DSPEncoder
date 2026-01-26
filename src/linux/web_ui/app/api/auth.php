<?php
// api/auth.php — PHP-layer session login/logout + auto-bootstrap.
// The C++ layer validates mc1session; this layer manages mc1app_session (DB roles).
// No exit()/die() — uopz active. JSON responses only.

define('MC1_BOOT', true);
require_once __DIR__ . '/../inc/auth.php';
require_once __DIR__ . '/../inc/db.php';
require_once __DIR__ . '/../inc/user_auth.php';

header('Content-Type: application/json; charset=utf-8');

if (!mc1_is_authed()) {
    http_response_code(403);
    echo json_encode(['ok' => false, 'error' => 'Forbidden']);
    return;
}

$raw    = file_get_contents('php://input');
$req    = ($raw !== '') ? json_decode($raw, true) : [];
if (!is_array($req)) $req = [];
$action = (string)($req['action'] ?? '');

// ── login ─────────────────────────────────────────────────────────────────

if ($action === 'login') {
    $username = trim($req['username'] ?? '');
    $password = $req['password'] ?? '';

    [$ok, $result] = mc1_login($username, $password);

    if ($ok) {
        echo json_encode([
            'ok'           => true,
            'username'     => $result['username'],
            'display_name' => $result['display_name'] ?? $result['username'],
            'role'         => $result['role_id'],
        ]);
    } else {
        http_response_code(401);
        echo json_encode(['ok' => false, 'error' => $result]);
    }
    return;
}

// ── logout ────────────────────────────────────────────────────────────────

if ($action === 'logout') {
    mc1_logout();
    echo json_encode(['ok' => true]);
    return;
}

// ── auto_login ────────────────────────────────────────────────────────────
// When C++ auth is valid but no PHP session exists, auto-login matching
// DB user by the C++ cookie username stored in HTTP_X_MC1_USER (if available)
// or by matching the first active admin if no username header is present.
// This bridges the two auth layers transparently.

if ($action === 'auto_login') {
    // Already have a PHP session?
    if (mc1_current_user() !== null) {
        $u = mc1_current_user();
        echo json_encode(['ok' => true, 'already' => true,
            'username' => $u['username'], 'can_admin' => (bool)$u['can_admin']]);
        return;
    }

    // Try to match by username hint from HTTP_X_MC1_USER header (if C++ sends it)
    $hint = trim($_SERVER['HTTP_X_MC1_USER'] ?? '');

    try {
        $db = mc1_db('mcaster1_encoder');

        if ($hint !== '') {
            $st = $db->prepare(
                "SELECT id, username, display_name, password_hash, role_id, is_active
                 FROM users WHERE username = ? AND is_active = 1 LIMIT 1"
            );
            $st->execute([$hint]);
            $user = $st->fetch() ?: null;
        } else {
            // Fall back to first active admin user
            $user = $db->query(
                "SELECT u.id, u.username, u.display_name, u.password_hash, u.role_id, u.is_active
                 FROM users u JOIN roles r ON r.id=u.role_id
                 WHERE u.is_active=1 ORDER BY r.can_admin DESC, u.id ASC LIMIT 1"
            )->fetch() ?: null;
        }

        if (!$user) {
            http_response_code(404);
            echo json_encode(['ok' => false, 'error' => 'No matching DB user']);
            return;
        }

        // Create PHP session without password verification (C++ already authenticated)
        $token = mc1_gen_token();
        $hash  = mc1_hash_token($token);
        $ip    = $_SERVER['REMOTE_ADDR'] ?? '127.0.0.1';
        $ua    = substr($_SERVER['HTTP_USER_AGENT'] ?? '', 0, 500);

        $db->prepare(
            "INSERT INTO user_sessions (user_id, token_hash, ip_address, user_agent, expires_at)
             VALUES (?, ?, ?, ?, DATE_ADD(NOW(), INTERVAL ? SECOND))"
        )->execute([$user['id'], $hash, $ip, $ua, MC1_SESSION_TTL]);

        $db->prepare("UPDATE users SET last_login=NOW() WHERE id=?")
           ->execute([$user['id']]);

        setcookie(MC1_SESSION_COOKIE, $token, [
            'expires'  => time() + MC1_SESSION_TTL,
            'path'     => '/',
            'httponly' => true,
            'samesite' => 'Strict',
        ]);

        // Fetch role for response
        $role = $db->query(
            "SELECT can_admin FROM roles WHERE id=" . (int)$user['role_id']
        )->fetchColumn();

        echo json_encode([
            'ok'           => true,
            'bootstrapped' => true,
            'username'     => $user['username'],
            'can_admin'    => (bool)$role,
        ]);

    } catch (Exception $e) {
        http_response_code(500);
        echo json_encode(['ok' => false, 'error' => $e->getMessage()]);
    }
    return;
}

// ── whoami ────────────────────────────────────────────────────────────────

if ($action === 'whoami') {
    $u = mc1_current_user();
    if ($u) {
        echo json_encode([
            'ok'           => true,
            'username'     => $u['username'],
            'display_name' => $u['display_name'] ?? $u['username'],
            'role'         => $u['role_name'] ?? '',
            'can_admin'    => (bool)($u['can_admin'] ?? false),
        ]);
    } else {
        echo json_encode(['ok' => false, 'error' => 'No PHP session']);
    }
    return;
}

http_response_code(400);
echo json_encode(['ok' => false, 'error' => 'Unknown action: ' . htmlspecialchars($action, ENT_QUOTES, 'UTF-8')]);
