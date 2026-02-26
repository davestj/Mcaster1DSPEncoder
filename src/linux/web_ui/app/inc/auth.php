<?php
// auth.php — Auth helper for PHP app pages.
// Note: C++ http_api blocks /app/inc/ routes so this file is never directly served.
// C++ with_auth() validates the session before forwarding to php-fpm,
// then adds HTTP_X_MC1_AUTHENTICATED=1 as a FastCGI param.

if (!defined('MC1_BOOT')) {
    http_response_code(403);
    echo '403 Forbidden';
    return;
}

// Returns true if the C++ server marked this request as authenticated.
function mc1_is_authed(): bool
{
    return ($_SERVER['HTTP_X_MC1_AUTHENTICATED'] ?? '') === '1';
}
