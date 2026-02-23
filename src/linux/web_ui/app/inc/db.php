<?php
// db.php — PDO connection factory for Mcaster1 databases.
// Note: C++ routing blocks /app/inc/ routes; this file is include-only.

if (!defined('MC1_BOOT')) {
    http_response_code(403);
    echo '403 Forbidden';
}

function mc1_db(string $dbname): PDO
{
    static $conns = [];
    if (!isset($conns[$dbname])) {
        $conns[$dbname] = new PDO(
            'mysql:host=127.0.0.1;dbname=' . $dbname . ';charset=utf8mb4',
            'TheDataCasterMaster',
            '#!3wrvNN57761',
            [
                PDO::ATTR_ERRMODE            => PDO::ERRMODE_EXCEPTION,
                PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
                PDO::ATTR_EMULATE_PREPARES   => false,
            ]
        );
    }
    return $conns[$dbname];
}

// Format milliseconds as M:SS or H:MM:SS
function mc1_format_duration(int $ms): string
{
    $s = (int)($ms / 1000);
    $h = (int)($s / 3600);
    $m = (int)(($s % 3600) / 60);
    $s = $s % 60;
    if ($h > 0) return sprintf('%d:%02d:%02d', $h, $m, $s);
    return sprintf('%d:%02d', $m, $s);
}

// Safe HTML-encode
function h(string $s): string
{
    return htmlspecialchars($s, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
}
