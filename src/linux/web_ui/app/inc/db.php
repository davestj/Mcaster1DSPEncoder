<?php
// db.php — PDO connection factory (backward-compat wrapper + shared helpers).
// Now delegates to traits.db.class.php; also exposes global mc1_db() function.
// No foreign key or unique checks. No exit() — uopz active.

if (!defined('MC1_BOOT')) {
    http_response_code(403);
    echo '403 Forbidden';
    return;
}

require_once __DIR__ . '/mc1_config.php';
require_once __DIR__ . '/traits.db.class.php';
require_once __DIR__ . '/logger.php';

// ── Global function wrapper (used by legacy code + user_auth.php) ──────────

function mc1_db(string $dbname): PDO
{
    static $conns = [];
    if (!isset($conns[$dbname])) {
        // We read credentials from mc1_config.php constants (loaded via require_once above).
        // MC1_DB_HOST / MC1_DB_PORT / MC1_DB_USER / MC1_DB_PASS are the single source.
        $dsn = 'mysql:host=' . MC1_DB_HOST . ';port=' . MC1_DB_PORT
             . ';dbname=' . $dbname . ';charset=utf8mb4';

        $pdo = new PDO($dsn, MC1_DB_USER, MC1_DB_PASS, [
            PDO::ATTR_ERRMODE            => PDO::ERRMODE_EXCEPTION,
            PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
            PDO::ATTR_EMULATE_PREPARES   => false,
        ]);
        $pdo->exec('SET foreign_key_checks=0, unique_checks=0, sql_mode=""');
        $conns[$dbname] = $pdo;
    }
    return $conns[$dbname];
}

// ── Shared helpers ──────────────────────────────────────────────────────────

function mc1_format_duration(int $ms): string
{
    $s = (int)($ms / 1000);
    $h = (int)($s / 3600);
    $m = (int)(($s % 3600) / 60);
    $s = $s % 60;
    if ($h > 0) return sprintf('%d:%02d:%02d', $h, $m, $s);
    return sprintf('%d:%02d', $m, $s);
}

function h(string $s): string
{
    return htmlspecialchars($s, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
}
