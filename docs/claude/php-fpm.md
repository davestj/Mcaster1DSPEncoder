## PHP-FPM Configuration

### Pool File

```
/etc/php/8.4/fpm/pool.d/mcaster1.conf
```

> **Version note:** The PHP minor-version directory (`8.4/`) and the socket
> filename below (`php8.4-fpm-mc1.sock`) are tied to whatever PHP-FPM Debian
> package is currently installed. When the OS bumps PHP, the pool config is
> migrated automatically by Debian, but the encoder's view of the socket path
> is **YAML-driven** (`http-admin.php-fpm-socket` in
> `src/linux/config/*.yaml`) — there is no compiled-in default. See "Socket
> Path" below.

Key settings:
```ini
[mcaster1]
user = mediacast1
group = www-data
listen = /run/php/php8.4-fpm-mc1.sock
listen.owner = mediacast1
listen.group = www-data
pm = dynamic
pm.max_children = 10
error_log = /var/log/mcaster1/php_fpm.log

; Logging env vars
env[MC1_LOG_DIR] = /var/log/mcaster1
env[MC1_LOG_LEVEL] = 4

; PHP settings
php_admin_value[error_reporting] = E_ALL
php_admin_flag[display_errors] = Off
php_admin_value[error_log] = /var/log/mcaster1/php_error.log
```

### Socket Path

```
/run/php/php8.4-fpm-mc1.sock
```

The encoder reads this path from YAML at startup:

```yaml
http-admin:
  php-fpm-socket: "/run/php/php8.4-fpm-mc1.sock"
```

It is then passed to `FastCgiClient` in `http_api.cpp`. There is no
compiled-in default — if `php-fpm-socket` is missing from the YAML, the
encoder logs a fatal `[http_api]` error to stderr and skips FastCGI
initialization (PHP pages will 502, C++ `/api/v1/*` keeps working). When
upgrading PHP, the only required action is to update this YAML key (and
restart `mcaster1-encoder`); no recompile.

If FPM is not running or socket is missing at request time, PHP pages
return 502.

### Reload After Config Changes

```bash
sudo systemctl reload php8.4-fpm
```

### Important PHP Gotcha: uopz Extension

The server has the `uopz` extension active, which intercepts ALL `exit()` / `die()` calls.

**RULE: Never use `exit()` or `die()` in any PHP file.**

Always use `if/elseif` chains and `return` instead:
```php
// WRONG — breaks with uopz:
if (!mc1_is_authed()) { http_response_code(401); die(); }

// CORRECT:
if (!mc1_is_authed()) { http_response_code(401); echo json_encode(['error'=>'Unauthorized']); return; }
```

## PHP Architecture Rules (No FK, No exit)

### Database PDO Rules

All PDO connections run:
```sql
SET foreign_key_checks=0, unique_checks=0, sql_mode=""
```

No FK constraints are enforced at the PDO layer. Use `Mc1Db` trait:
```php
class MyClass {
    use Mc1Db;
    function getData() {
        return self::rows('mcaster1_media', 'SELECT * FROM tracks LIMIT 10');
    }
}
```

### Output Escaping

All user-derived data rendered into HTML goes through `h()`:
```php
echo h($track['title']);  // htmlspecialchars(ENT_QUOTES|ENT_HTML5)
```
