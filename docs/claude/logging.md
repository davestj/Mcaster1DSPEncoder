## Logging System

### C++ Logging (`mc1_logger.h`)

Singleton `Mc1Logger` with log levels 1-5:

| Level | Name | Meaning |
|-------|------|---------|
| 1 | CRITICAL | Fatal errors, crash conditions |
| 2 | ERROR | Non-fatal errors |
| 3 | WARN | Warnings, degraded operation |
| 4 | INFO | Normal events (default) |
| 5 | DEBUG | Verbose: raw data, request bodies, stack traces |

Log files (all in `/var/log/mcaster1/`):
- `access.log` — Apache combined format HTTP log (every request)
- `error.log` — Application errors + startup INFO
- `encoder.log` — Per-slot encoder events (start/stop/track change)
- `api.log` — API request/response bodies (level 5 only)

Macros:
```cpp
MC1_INFO("message");
MC1_ERR("error: " + std::string(e.what()));
MC1_WARN("slot " + std::to_string(slot) + " not found");
MC1_DBG("raw data: " + payload);
mc1log.encoder(slot_id, "START", "mount=" + mount);
mc1log.access(remote_addr, method, uri, status, bytes, duration_us, referer, ua);
```

### Set Log Level

Via YAML config:
```yaml
http-admin:
  log-dir:   "/var/log/mcaster1"
  log-level: 4          # 1-5
```

Via CLI:
```bash
./build/mcaster1-encoder --config ... --log-level 5 --log-dir /var/log/mcaster1
# -v also sets level 5
```

### PHP Logging (`app/inc/logger.php`)

Functions: `mc1_log($level, $msg, $ctx, $ex)`, `mc1_log_request()`, `mc1_log_exception($e)`, `mc1_api_respond($data, $status)`

PHP log files:
- `/var/log/mcaster1/php_error.log`
- `/var/log/mcaster1/php_access.log`
- `/var/log/mcaster1/php_fpm.log`

### Log Rotation

```
/etc/logrotate.d/mcaster1
```

Daily, 14-day retention. Postrotate:
```bash
pkill -HUP -f 'build/mcaster1-encoder' || true
systemctl reload php8.4-fpm || true
```

### View Logs

```bash
tail -f /var/log/mcaster1/access.log     # HTTP traffic
tail -f /var/log/mcaster1/error.log      # App errors
tail -f /var/log/mcaster1/encoder.log    # Encoder events
tail -f /var/log/mcaster1/php_fpm.log    # PHP-FPM pool log
tail -f /tmp/mc1enc.log                  # Startup stderr
```

### Enable Debug Logging (Level 5)

```bash
# CLI override (does not require config change):
./build/mcaster1-encoder --config ... --log-level 5
# Or: -v flag also sets level 5
```

At level 5:
- `api.log` records request + response bodies (truncated to 512 chars)
- Stack traces written to error.log on exceptions
- `access.log` gets per-request debug lines on stderr
