## Web Routes (in registration order)

Routes in `http_api.cpp` must be registered most-specific first:

```
1.  GET  /                         → 302 /dashboard or /login
2.  GET  /login                    → login.html (no auth)
3.  GET  /style.css, /app.js...    → static assets (no auth — login page uses them)
4.  GET  /dashboard                → 302 /dashboard.php (with auth check)
5.  GET  /app/inc/.*               → 403 BLOCKED (never serve include files)
6.  GET  /app/api/*.php            → handle_php (C++ auth + FastCGI)
7.  POST /app/api/*.php            → handle_php
8.  PUT  /app/api/*.php            → handle_php
9.  DELETE /app/api/*.php          → handle_php
10. GET  /app/*.php                → handle_php
11. POST /app/*.php                → handle_php
12. GET  /*.php                    → handle_php (root-level PHP pages)
13. POST /*.php                    → handle_php
14. GET  /api/v1/...               → C++ API routes
15. 404  catch-all
```

## Test Station Config

**File:** `src/linux/config/mcaster1_rock_yolo.yaml`

```yaml
http-admin:
  username: djpulse
  password: hackme
  api_token: ""
  log-dir:   "/var/log/mcaster1"
  log-level: 4

sockets:
  - { port: 8330, bind_address: "0.0.0.0", ssl_enabled: false }
  - { port: 8344, bind_address: "0.0.0.0", ssl_enabled: true,
      ssl_cert: "/etc/ssl/certs/encoder.mcaster1.com.pem",
      ssl_key:  "/etc/ssl/private/encoder.mcaster1.com.key" }

webroot: src/linux/web_ui
```

| Slot | Mount | Genre | EQ Preset | Bitrate | DNAS Target |
|------|-------|-------|-----------|---------|-------------|
| 1 | `/yolo-rock` | Classic Rock | `classic_rock` | 128 kbps | dnas.mcaster1.com:9443 |
| 2 | `/yolo-country` | Country | `country` | 128 kbps | dnas.mcaster1.com:9443 |
| 3 | `/yolo-modern` | Modern Rock | `modern_rock` | 192 kbps | dnas.mcaster1.com:9443 |

## E2E Test Flow (Phase L5)

```bash
# 1. Build (autotools canonical)
./configure && make -j$(nproc)

# 2. Start
nohup ./build/mcaster1-encoder \
  --config src/linux/config/mcaster1_rock_yolo.yaml \
  > /tmp/mc1enc.log 2>&1 & disown $!

# 3. Verify startup
tail -f /tmp/mc1enc.log
tail -f /var/log/mcaster1/error.log

# 4. Login via curl
TOKEN=$(curl -s -c /tmp/mc1.jar -b /tmp/mc1.jar \
  http://127.0.0.1:8330/api/v1/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"username":"djpulse","password":"hackme"}' | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('token',''))")

# 5. Check status
curl -s -c /tmp/mc1.jar -b /tmp/mc1.jar http://127.0.0.1:8330/api/v1/status | python3 -m json.tool
```

## Common Tasks

### Check PHP-FPM

```bash
systemctl status php8.4-fpm
sudo systemctl reload php8.4-fpm      # After config changes
ls -la /run/php/php8.4-fpm-mc1.sock  # Verify socket exists
```

### Add MySQL User

```bash
mysql --defaults-extra-file=~/.my.cnf mcaster1_encoder
```
```sql
INSERT INTO users (username, email, password_hash, role_id, is_active)
VALUES ('newdj', 'dj@example.com', '$2y$10$...bcrypt...', 2, 1);
```

### SSL Certificate (encoder.mcaster1.com)

```
Cert: /etc/ssl/certs/encoder.mcaster1.com.pem
Key:  /etc/ssl/private/encoder.mcaster1.com.key
```

Or generate self-signed:
```bash
./build/mcaster1-encoder --ssl-gencert "/CN=encoder.mcaster1.com/O=Mcaster1"
```
