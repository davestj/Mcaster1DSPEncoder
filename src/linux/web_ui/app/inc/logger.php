<?php
// logger.php — PHP-side logging for Mcaster1 web UI.
// Writes to /var/log/mcaster1/php_error.log and php_access.log.
// Log level set via MC1_LOG_LEVEL env or defaults to 4 (INFO).
//
// Levels: 1=CRIT 2=ERR 3=WARN 4=INFO 5=DEBUG
//
// Usage:
//   mc1_log(MC1_INFO, 'User logged in', 'user_auth');
//   mc1_log_request();   // call at end of API request handlers

if (!defined('MC1_BOOT')) {
    http_response_code(403); echo '403 Forbidden'; return;
}

defined('MC1_LOG_CRIT')  || define('MC1_LOG_CRIT',  1);
defined('MC1_LOG_ERROR') || define('MC1_LOG_ERROR', 2);
defined('MC1_LOG_WARN')  || define('MC1_LOG_WARN',  3);
defined('MC1_LOG_INFO')  || define('MC1_LOG_INFO',  4);
defined('MC1_LOG_DEBUG') || define('MC1_LOG_DEBUG', 5);

// ── Config ───────────────────────────────────────────────────────────────────

function mc1_log_dir(): string
{
    static $d = null;
    if ($d === null) {
        $d = getenv('MC1_LOG_DIR') ?: '/var/log/mcaster1';
    }
    return $d;
}

function mc1_log_level(): int
{
    static $lv = null;
    if ($lv === null) {
        $env = (int)(getenv('MC1_LOG_LEVEL') ?: 0);
        $lv  = ($env >= 1 && $env <= 5) ? $env : MC1_LOG_INFO;
    }
    return $lv;
}

// ── Core log function ────────────────────────────────────────────────────────

function mc1_log(int $level, string $msg, string $ctx = '', ?Throwable $ex = null): void
{
    if ($level > mc1_log_level()) return;

    $labels = ['', 'CRIT ', 'ERROR', 'WARN ', 'INFO ', 'DEBUG'];
    $label  = $labels[$level] ?? 'INFO ';
    $ts     = gmdate('Y-m-d\TH:i:s\Z');
    $line   = "$ts [$label]";
    if ($ctx !== '') $line .= " [$ctx]";
    $line .= " $msg";

    if ($ex !== null) {
        $line .= " | Exception: " . $ex->getMessage();
        if (mc1_log_level() >= MC1_LOG_DEBUG) {
            $line .= "\n" . $ex->getTraceAsString();
        }
    }

    $path = mc1_log_dir() . '/php_error.log';
    @file_put_contents($path, $line . "\n", FILE_APPEND | LOCK_EX);

    // At debug level, also write to stderr (visible in php-fpm pool log)
    if (mc1_log_level() >= MC1_LOG_DEBUG) {
        fwrite(STDERR, $line . "\n");
    }
}

// ── Request/access log ───────────────────────────────────────────────────────

function mc1_log_request(int $status = 0, string $extra = ''): void
{
    if (mc1_log_level() < MC1_LOG_INFO) return;

    $method  = $_SERVER['REQUEST_METHOD'] ?? 'GET';
    $uri     = $_SERVER['REQUEST_URI']    ?? '/';
    $ip      = $_SERVER['REMOTE_ADDR']    ?? '-';
    $ua      = substr($_SERVER['HTTP_USER_AGENT'] ?? '-', 0, 200);
    $ref     = $_SERVER['HTTP_REFERER']   ?? '-';
    $code    = $status > 0 ? $status : http_response_code();

    $ts = gmdate('d/M/Y:H:i:s \+0000');
    $line = "$ip - - [$ts] \"$method $uri HTTP/1.1\" $code - \"$ref\" \"$ua\"";
    if ($extra !== '') $line .= " | $extra";

    $path = mc1_log_dir() . '/php_access.log';
    @file_put_contents($path, $line . "\n", FILE_APPEND | LOCK_EX);
}

// ── Exception handler ────────────────────────────────────────────────────────

function mc1_log_exception(Throwable $e, string $ctx = ''): void
{
    mc1_log(MC1_LOG_ERROR, $e->getMessage(), $ctx, $e);
}

// ── API response helper: log + echo JSON ─────────────────────────────────────

function mc1_api_respond(array $data, int $http_status = 200): void
{
    if (!headers_sent()) http_response_code($http_status);

    $json = json_encode($data);
    echo $json;

    mc1_log_request($http_status);
    if (mc1_log_level() >= MC1_LOG_DEBUG) {
        $ctx = ($_SERVER['REQUEST_URI'] ?? '');
        mc1_log(MC1_LOG_DEBUG, 'REQ: ' . file_get_contents('php://input'), $ctx);
        mc1_log(MC1_LOG_DEBUG, 'RESP: ' . substr($json, 0, 512), $ctx);
    }
}
