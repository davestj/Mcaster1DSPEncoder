<?php
// user_auth.php — MySQL-based user authentication and role enforcement.
// Provides session management independent of the C++ session system.
//
// Session cookie: mc1app_session (HttpOnly, SameSite=Strict)
// Sessions stored in mcaster1_encoder.user_sessions (SHA-256 token hash)

if (!defined('MC1_BOOT')) {
    http_response_code(403);
    echo '403 Forbidden';
}

// ── Session constants ──────────────────────────────────────────────────────
define('MC1_SESSION_TTL',    86400 * 7);   // 7 days
define('MC1_SESSION_COOKIE', 'mc1app_session');

// ── Token helpers ──────────────────────────────────────────────────────────

function mc1_gen_token(): string
{
    return bin2hex(random_bytes(32));
}

function mc1_hash_token(string $token): string
{
    return hash('sha256', $token);
}

// ── Session read ───────────────────────────────────────────────────────────
// Returns user row (with role columns) or null.

function mc1_current_user(): ?array
{
    static $cached = false;
    if ($cached !== false) return $cached;

    $cookie = $_COOKIE[MC1_SESSION_COOKIE] ?? '';
    if ($cookie === '') { $cached = null; return null; }

    $hash = mc1_hash_token($cookie);

    try {
        $db = mc1_db('mcaster1_encoder');
        $st = $db->prepare(
            "SELECT u.id, u.username, u.display_name, u.email, u.role_id,
                    r.name AS role_name, r.label AS role_label,
                    r.can_admin, r.can_encode_control, r.can_playlist,
                    r.can_metadata, r.can_media_library, r.can_metrics,
                    r.can_podcast, r.can_schedule
             FROM user_sessions s
             JOIN users u ON u.id = s.user_id
             JOIN roles  r ON r.id = u.role_id
             WHERE s.token_hash = ?
               AND s.expires_at > NOW()
               AND u.is_active  = 1"
        );
        $st->execute([$hash]);
        $row = $st->fetch();
        $cached = $row ?: null;
    } catch (Exception $e) {
        $cached = null;
    }
    return $cached;
}

// ── Login ──────────────────────────────────────────────────────────────────
// Returns [true, user_row] on success, [false, 'error message'] on failure.

function mc1_login(string $username, string $password): array
{
    if ($username === '' || $password === '') return [false, 'Credentials required'];

    try {
        $db = mc1_db('mcaster1_encoder');
        $st = $db->prepare(
            "SELECT id, username, display_name, email, password_hash, role_id, is_active
             FROM users WHERE username = ? LIMIT 1"
        );
        $st->execute([$username]);
        $user = $st->fetch();

        if (!$user || !$user['is_active'])
            return [false, 'Invalid username or password'];

        if (!password_verify($password, $user['password_hash']))
            return [false, 'Invalid username or password'];

        // Create session
        $token = mc1_gen_token();
        $hash  = mc1_hash_token($token);
        $ip    = $_SERVER['REMOTE_ADDR'] ?? '';
        $ua    = substr($_SERVER['HTTP_USER_AGENT'] ?? '', 0, 500);

        $is = $db->prepare(
            "INSERT INTO user_sessions (user_id, token_hash, ip_address, user_agent, expires_at)
             VALUES (?, ?, ?, ?, DATE_ADD(NOW(), INTERVAL ? SECOND))"
        );
        $is->execute([$user['id'], $hash, $ip, $ua, MC1_SESSION_TTL]);

        // Update last_login
        $db->prepare("UPDATE users SET last_login = NOW() WHERE id = ?")
           ->execute([$user['id']]);

        // Set cookie (path=/ so it works across /app/ and /api/)
        setcookie(MC1_SESSION_COOKIE, $token, [
            'expires'  => time() + MC1_SESSION_TTL,
            'path'     => '/',
            'httponly' => true,
            'samesite' => 'Strict',
        ]);

        return [true, $user];

    } catch (Exception $e) {
        return [false, 'Login error: ' . $e->getMessage()];
    }
}

// ── Logout ─────────────────────────────────────────────────────────────────

function mc1_logout(): void
{
    $cookie = $_COOKIE[MC1_SESSION_COOKIE] ?? '';
    if ($cookie !== '') {
        try {
            $hash = mc1_hash_token($cookie);
            mc1_db('mcaster1_encoder')
                ->prepare("DELETE FROM user_sessions WHERE token_hash = ?")
                ->execute([$hash]);
        } catch (Exception $e) {}
    }
    setcookie(MC1_SESSION_COOKIE, '', ['expires' => 1, 'path' => '/', 'httponly' => true]);
}

// ── Permission helpers ─────────────────────────────────────────────────────

function mc1_can(string $permission): bool
{
    $u = mc1_current_user();
    return $u && ($u['can_' . $permission] ?? false);
}

function mc1_require_role(string $permission): void
{
    // For API calls: if not authed or no permission, output JSON 403
    // For page calls: handled via mc1_current_user() in templates
    if (!mc1_can($permission)) {
        if (!headers_sent()) {
            http_response_code(403);
            header('Content-Type: application/json');
        }
        echo json_encode(['ok' => false, 'error' => 'Permission denied',
                          'required' => $permission]);
    }
}
