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

        $is_secure = (!empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off')
                   || (($_SERVER['HTTP_X_FORWARDED_PROTO'] ?? '') === 'https')
                   || (strpos($_SERVER['HTTP_HOST'] ?? '', '8344') !== false);
        setcookie(MC1_SESSION_COOKIE, $token, [
            'expires'  => time() + MC1_SESSION_TTL,
            'path'     => '/',
            'httponly' => true,
            'secure'   => $is_secure,
            'samesite' => 'Lax',
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

// ── heartbeat ─────────────────────────────────────────────────────────────
// Called every 30s by footer.php JS to update user presence + current page.

if ($action === 'heartbeat') {
    $u = mc1_current_user();
    if (!$u) {
        echo json_encode(['ok' => false, 'error' => 'No PHP session']);
        return;
    }

    $page = trim($req['page'] ?? 'dashboard');
    $page = substr(preg_replace('/[^a-zA-Z0-9_\-\/]/', '', $page), 0, 128);
    $ip   = $_SERVER['REMOTE_ADDR'] ?? '127.0.0.1';
    $token = $_COOKIE[MC1_SESSION_COOKIE] ?? '';
    $tokenHash = $token !== '' ? substr(mc1_hash_token($token), 0, 64) : '';

    try {
        $db = mc1_db('mcaster1_encoder');

        // Clean up stale sessions older than 5 minutes
        $db->exec("DELETE FROM active_sessions WHERE last_heartbeat < DATE_SUB(NOW(), INTERVAL 5 MINUTE)");

        // Upsert: update if same user_id exists, otherwise insert
        $st = $db->prepare(
            "INSERT INTO active_sessions (user_id, username, display_name, current_page, ip_address, session_token, last_heartbeat)
             VALUES (?, ?, ?, ?, ?, ?, NOW())
             ON DUPLICATE KEY UPDATE
               current_page = VALUES(current_page),
               ip_address = VALUES(ip_address),
               session_token = VALUES(session_token),
               last_heartbeat = NOW(),
               display_name = VALUES(display_name)"
        );
        $st->execute([
            (int)$u['id'],
            $u['username'],
            $u['display_name'] ?? $u['username'],
            $page,
            $ip,
            $tokenHash
        ]);

        echo json_encode(['ok' => true]);
    } catch (Exception $e) {
        http_response_code(500);
        echo json_encode(['ok' => false, 'error' => $e->getMessage()]);
    }
    return;
}

// ── active_users ──────────────────────────────────────────────────────────
// Returns list of users with heartbeat within last 60 seconds.

if ($action === 'active_users') {
    $u = mc1_current_user();
    if (!$u) {
        echo json_encode(['ok' => false, 'error' => 'No PHP session']);
        return;
    }

    try {
        $db = mc1_db('mcaster1_encoder');

        $rows = $db->query(
            "SELECT a.user_id, a.username, a.display_name, a.current_page, a.ip_address,
                    a.last_heartbeat,
                    COALESCE(u.role_id, 0) AS role_id,
                    COALESCE(r.name, 'user') AS role_name,
                    COALESCE(r.can_admin, 0) AS can_admin
             FROM active_sessions a
             LEFT JOIN users u ON u.id = a.user_id
             LEFT JOIN roles r ON r.id = u.role_id
             WHERE a.last_heartbeat >= DATE_SUB(NOW(), INTERVAL 60 SECOND)
             ORDER BY a.last_heartbeat DESC"
        )->fetchAll(\PDO::FETCH_ASSOC);

        $users = [];
        foreach ($rows as $row) {
            $users[] = [
                'user_id'      => (int)$row['user_id'],
                'username'     => $row['username'],
                'display_name' => $row['display_name'] ?: $row['username'],
                'current_page' => $row['current_page'],
                'role_name'    => $row['role_name'],
                'can_admin'    => (bool)$row['can_admin'],
                'is_self'      => ((int)$row['user_id'] === (int)$u['id']),
            ];
        }

        echo json_encode(['ok' => true, 'users' => $users]);
    } catch (Exception $e) {
        http_response_code(500);
        echo json_encode(['ok' => false, 'error' => $e->getMessage()]);
    }
    return;
}

// ── send_message ──────────────────────────────────────────────────────────
// Send a quick message to another active user.

if ($action === 'send_message') {
    $u = mc1_current_user();
    if (!$u) {
        echo json_encode(['ok' => false, 'error' => 'No PHP session']);
        return;
    }

    $toUserId = (int)($req['to_user_id'] ?? 0);
    $message  = trim($req['message'] ?? '');

    if ($toUserId < 1 || $message === '') {
        http_response_code(400);
        echo json_encode(['ok' => false, 'error' => 'to_user_id and message are required']);
        return;
    }

    if (mb_strlen($message) > 1000) {
        $message = mb_substr($message, 0, 1000);
    }

    try {
        $db = mc1_db('mcaster1_encoder');
        $st = $db->prepare(
            "INSERT INTO user_messages (from_user_id, to_user_id, message) VALUES (?, ?, ?)"
        );
        $st->execute([(int)$u['id'], $toUserId, $message]);

        echo json_encode(['ok' => true, 'id' => (int)$db->lastInsertId()]);
    } catch (Exception $e) {
        http_response_code(500);
        echo json_encode(['ok' => false, 'error' => $e->getMessage()]);
    }
    return;
}

// ── get_messages ──────────────────────────────────────────────────────────
// Get unread + recent messages for the current user.

if ($action === 'get_messages') {
    $u = mc1_current_user();
    if (!$u) {
        echo json_encode(['ok' => false, 'error' => 'No PHP session']);
        return;
    }

    $withUserId = (int)($req['with_user_id'] ?? 0);

    try {
        $db = mc1_db('mcaster1_encoder');

        if ($withUserId > 0) {
            // Get conversation with specific user (last 50 messages)
            $st = $db->prepare(
                "SELECT m.id, m.from_user_id, m.to_user_id, m.message, m.is_read, m.created_at,
                        u.username AS from_username, u.display_name AS from_display_name
                 FROM user_messages m
                 LEFT JOIN users u ON u.id = m.from_user_id
                 WHERE (m.from_user_id = ? AND m.to_user_id = ?)
                    OR (m.from_user_id = ? AND m.to_user_id = ?)
                 ORDER BY m.created_at DESC LIMIT 50"
            );
            $st->execute([(int)$u['id'], $withUserId, $withUserId, (int)$u['id']]);
            $messages = $st->fetchAll(\PDO::FETCH_ASSOC);

            // Mark messages from that user as read
            $st2 = $db->prepare(
                "UPDATE user_messages SET is_read = 1
                 WHERE from_user_id = ? AND to_user_id = ? AND is_read = 0"
            );
            $st2->execute([$withUserId, (int)$u['id']]);
        } else {
            // Get all unread messages grouped by sender
            $st = $db->prepare(
                "SELECT m.id, m.from_user_id, m.to_user_id, m.message, m.is_read, m.created_at,
                        u.username AS from_username, u.display_name AS from_display_name
                 FROM user_messages m
                 LEFT JOIN users u ON u.id = m.from_user_id
                 WHERE m.to_user_id = ? AND m.is_read = 0
                 ORDER BY m.created_at DESC LIMIT 50"
            );
            $st->execute([(int)$u['id']]);
            $messages = $st->fetchAll(\PDO::FETCH_ASSOC);
        }

        // Get unread count
        $st3 = $db->prepare(
            "SELECT COUNT(*) FROM user_messages WHERE to_user_id = ? AND is_read = 0"
        );
        $st3->execute([(int)$u['id']]);
        $unreadCount = (int)$st3->fetchColumn();

        $formatted = [];
        foreach ($messages as $msg) {
            $formatted[] = [
                'id'                => (int)$msg['id'],
                'from_user_id'      => (int)$msg['from_user_id'],
                'to_user_id'        => (int)$msg['to_user_id'],
                'from_username'     => $msg['from_username'],
                'from_display_name' => $msg['from_display_name'] ?: $msg['from_username'],
                'message'           => $msg['message'],
                'is_read'           => (bool)$msg['is_read'],
                'created_at'        => $msg['created_at'],
                'is_mine'           => ((int)$msg['from_user_id'] === (int)$u['id']),
            ];
        }

        echo json_encode(['ok' => true, 'messages' => $formatted, 'unread_count' => $unreadCount]);
    } catch (Exception $e) {
        http_response_code(500);
        echo json_encode(['ok' => false, 'error' => $e->getMessage()]);
    }
    return;
}

// ── mark_read ─────────────────────────────────────────────────────────────
// Mark messages from a specific user as read.

if ($action === 'mark_read') {
    $u = mc1_current_user();
    if (!$u) {
        echo json_encode(['ok' => false, 'error' => 'No PHP session']);
        return;
    }

    $fromUserId = (int)($req['from_user_id'] ?? 0);
    if ($fromUserId < 1) {
        http_response_code(400);
        echo json_encode(['ok' => false, 'error' => 'from_user_id required']);
        return;
    }

    try {
        $db = mc1_db('mcaster1_encoder');
        $st = $db->prepare(
            "UPDATE user_messages SET is_read = 1
             WHERE from_user_id = ? AND to_user_id = ? AND is_read = 0"
        );
        $st->execute([$fromUserId, (int)$u['id']]);

        echo json_encode(['ok' => true, 'marked' => $st->rowCount()]);
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
