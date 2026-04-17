# CLAUDE.md — Mcaster1DSPEncoder Project Memory

**Maintainer:** Dave St. John <davestj@gmail.com>
**Project:** Mcaster1DSPEncoder — Dual-platform broadcast DSP encoder
**Repo root:** `/var/www/mcaster1.com/Mcaster1DSPEncoder/`
**Current branch:** `linux-dev`

---
For mysql connection work from the command line, we use 
mysql --defaults-extra-file=~/.my.cnf -e "SHOW DATABASES LIKE %yp%";

## Planning & Roadmap

**See `PLANNING.md`** at the project root for the full future phase roadmap.

### v2.0.0 — CURRENT RELEASE (2026-03-27)
All 40+ phases complete. Full podcaster/broadcaster production studio with quad-binary architecture:
DSP Producer (video capture, switcher, RTMP streaming), multi-track DAW, forensic audio analysis,
closed captions (Whisper), monetization (DAI), mode-based navigation, daemon health monitor,
plus all v1.8.0 features (VoicTune, pedalboard, mixer, podcast studio, remote recording, etc.).

### Next: v2.1.0 (Planned)
- macOS native build (CoreAudio backend)
- Plugin SDK for third-party DSP effects
- Multi-user collaborative editing
- Advanced VoicTune: de-breath, auto-tune, voice changer
- Multi-language UI localization (i18n)

Always consult PLANNING.md before starting a new development phase to check scope,
DB growth plan, and architectural decisions recorded there.

---

## Project Overview

Mcaster1DSPEncoder is a dual-platform (Windows + Linux) broadcast audio encoder and full podcaster/broadcaster production studio with:
- VoicTune voice analysis daemon (FFT, pitch, LUFS metering, AI coaching via Ollama)
- Podcast studio (recording, episode editor, multi-platform publishing, RSS, analytics)
- Visual pedalboard + virtual mixer console with 6 branded skins
- Song request system, webhooks, remote recording (WebRTC-style)
- Embedded HTTP/HTTPS admin server (cpp-httplib v0.18)
- Full PHP web UI (FastCGI → php-fpm)
- DSP chain (10-band EQ, AGC/limiter, reverb, delay, equal-power crossfader)
- Multi-format audio encoding (MP3/Vorbis/Opus/FLAC/AAC)
- Live broadcast to Icecast2 / Shoutcast / Mcaster1DNAS
- Two-layer auth: C++ in-memory sessions + MySQL PHP sessions

**Windows and Linux builds are parallel — they share NO source code.**
- Windows: `src/` (VS2022 project) — maintained separately
- Linux: `src/linux/` (custom shell build scripts) — this is the active dev branch

---

## Platform Split

| Aspect | Windows | Linux |
|--------|---------|-------|
| Source root | `src/` | `src/linux/` |
| Build system | VS2022 .sln | Autotools (canonical) + `make_phase4.sh` (legacy) |
| Vendor headers | `external/include/` (Windows only) | `src/linux/external/include/` (Linux only) |
| Audio backend | WASAPI / DirectSound | PortAudio (ALSA/PulseAudio/JACK) |
| HTTP server | cpp-httplib | cpp-httplib |
| IDE | Visual Studio 2022 | vim / neovim / Claude Code |
| Deploy path | Windows service | systemd service |

---

## Linux Build System

**Canonical build method: Autotools.** The `make_phase4.sh` shell script is retained as a legacy fallback but autotools is the standard going forward for all Linux and macOS builds.

### Step 1: Install Dependencies (first time or new machine)

```bash
# Auto-detects OS: apt / dnf / yum / zypper / pacman / brew
bash install-deps.sh
```

`install-deps.sh` handles: Debian/Ubuntu (apt), Fedora (dnf), RHEL/Rocky/Alma (yum/dnf), openSUSE (zypper), Arch/Manjaro (pacman), macOS (brew). It installs build toolchain, all audio/codec libs, PHP-FPM, and autoconf-archive. It also prints fdk-aac manual build instructions (not in standard repos).

Use `--build-only` to skip audio/codec libs if you only need to build the C++ binary.

### Step 2: Bootstrap Autotools (first time or after changes to configure.ac / Makefile.am)

```bash
cd /var/www/mcaster1.com/Mcaster1DSPEncoder
./autogen.sh
# Runs: autoreconf --force --install
# Generates: configure, Makefile.in files
```

If autotools tools are missing, autogen.sh will say: "Run: bash install-deps.sh --build-only"

### Step 3: Configure

```bash
./configure
# Detects all available codecs and sets AM_CONDITIONALs
# Output: config.h + Makefile hierarchy
```

### Step 4: Build

```bash
make -j$(nproc)
# Output: src/linux/mcaster1-encoder (installed to build/ by make install or run in place)
```

Or the full one-liner after bootstrapping once:

```bash
./configure && make -j$(nproc)
```

### Legacy Build (make_phase4.sh)

```bash
cd /var/www/mcaster1.com/Mcaster1DSPEncoder
bash src/linux/make_phase4.sh
# Output: build/mcaster1-encoder
```

Flags in make_phase4.sh:
- `-std=c++17`
- `-rdynamic` (enables backtrace symbol resolution for stack traces)
- All DSP + audio codec libs linked
- Explicit `-DHAVE_LAME`, `-DHAVE_VORBIS`, etc. (source files do not include config.h)

### IMPORTANT: Codec -DHAVE_XXX Flags

The Linux source files (`encoder_slot.cpp`, `file_source.cpp`, etc.) use `#ifdef HAVE_LAME` etc. but do **NOT** include `config.h`. This means autotools `AC_DEFINE([HAVE_LAME])` alone is not enough. The Makefile.am must explicitly pass `-DHAVE_LAME` (and all other codec defines) via `AM_CPPFLAGS` inside each `if HAVE_LAME` conditional block. This is done in `src/linux/Makefile.am` already. Do not remove these.

### Quad-Binary Architecture (v2.0.0)

Four binaries with fault isolation:

```
mcaster1-dsp-encoder-admin  (46MB) — Web UI, FastCGI, auth, supervisor
mcaster1-dsp-encoder        (29MB) — Audio pipeline, DSP, codecs, streaming
mcaster1-voictune           (21MB) — Voice analysis, FFT, pitch, coaching, Ollama AI
mcaster1-producer            (9.1MB) — Video encoding, multi-track mixdown, forensic FFT
```

Admin supervises encoder child via fork/exec + watchdog. If encoder crashes (SIGSEGV from bad codec config), admin auto-restarts it within 5 seconds. Web UI stays accessible during restart.

VoicTune runs independently on ports 8350/8354/8355 (HTTP/HTTPS/WebSocket). Producer runs on ports 8360/8364 (HTTP/HTTPS). Each has its own systemd unit, YAML config, and worker pool.

### Run (systemd — recommended)

```bash
sudo systemctl start mcaster1-dsp-encoder     # Start admin (auto-starts encoder child)
sudo systemctl stop mcaster1-dsp-encoder      # Stop both
sudo systemctl restart mcaster1-dsp-encoder   # Restart both
sudo systemctl status mcaster1-dsp-encoder    # Shows both PIDs in CGroup
sudo journalctl -u mcaster1-dsp-encoder -f    # Live logs
```

### Run (manual — legacy)

```bash
nohup ./build/mcaster1-dsp-encoder-admin \
  --config src/linux/config/mcaster1_rock_yolo.yaml \
  > /tmp/mc1enc.log 2>&1 & disown $!
```

The admin binary automatically forks the encoder child. Both processes visible in `ps aux`.

### Kill + Restart

```bash
# List all encoder processes
pgrep -af mcaster1-dsp

# Stop via systemd (recommended)
sudo systemctl restart mcaster1-dsp-encoder

# Manual kill (both processes)
pkill -9 -f 'mcaster1-dsp-encoder'
sleep 2
```

**Gotcha:** cpp-httplib uses `SO_REUSEPORT` — if an old instance is still running when you restart, BOTH bind successfully to the same ports. The new instance's sessions are not known to the old instance, causing 302 redirects to `/login`. Always kill all old instances before restarting.

---

## Directory Structure

```
Mcaster1DSPEncoder/
├── src/
│   ├── linux/                          ← Linux-only source
│   │   ├── http_api.cpp                ← HTTP/HTTPS server, routes, FastCGI, DSP API
│   │   ├── main_cli.cpp                ← main(), CLI option parsing
│   │   ├── config_loader.cpp           ← YAML → gAdminConfig + gSlotConfigs
│   │   ├── mc1_logger.h                ← C++ logging singleton (NEW Phase L5)
│   │   ├── audio_pipeline.h/cpp        ← Multi-slot orchestrator
│   │   ├── encoder_slot.h/cpp          ← Per-slot state machine
│   │   ├── stream_client.h/cpp         ← Icecast2/Shoutcast/DNAS SOURCE client
│   │   ├── file_source.h/cpp           ← libavcodec/libmpg123 file streaming
│   │   ├── audio_source.h/cpp          ← PortAudio device capture
│   │   ├── playlist_parser.h/cpp       ← M3U/PLS/XSPF/TXT parser
│   │   ├── fastcgi_client.h/cpp        ← Binary FastCGI/1.0 AF_UNIX client
│   │   ├── archive_writer.h/cpp        ← WAV + MP3 simultaneous archive
│   │   ├── dsp/
│   │   │   ├── eq.h/cpp                ← 10-band parametric EQ (RBJ biquad IIR)
│   │   │   ├── agc.h/cpp               ← AGC / compressor / hard limiter
│   │   │   ├── crossfader.h/cpp        ← Equal-power crossfader
│   │   │   └── dsp_chain.h/cpp         ← EQ → AGC orchestrator
│   │   ├── voictune/                    ← VoicTune daemon (Phase VT)
│   │   │   ├── main_voictune.cpp        ← Entry point, CLI, signal handling, subsystem init
│   │   │   ├── vt_http_api.h/cpp        ← HTTP/HTTPS server, routes, session auth
│   │   │   ├── vt_config.h/cpp          ← YAML config loader
│   │   │   ├── vt_logger.h              ← Logging singleton (→ voictune.log)
│   │   │   ├── vt_db.h/cpp              ← MariaDB client (mcaster1_voictune DB)
│   │   │   ├── vt_audio_capture.h/cpp   ← PortAudio mic capture wrapper
│   │   │   ├── vt_usb_monitor.h/cpp     ← USB/BT audio hotplug (inotify)
│   │   │   ├── vt_websocket.h/cpp       ← RFC 6455 WebSocket (browser mic, port 8355)
│   │   │   ├── vt_fft.h/cpp             ← FFT analysis (kiss_fft, spectral features)
│   │   │   ├── vt_meters.h/cpp          ← RMS, peak, LUFS (ITU-R BS.1770-4)
│   │   │   ├── vt_pitch.h/cpp           ← Pitch detection (autocorrelation, note mapping)
│   │   │   ├── vt_coach.h/cpp           ← Rule-based voice coaching engine
│   │   │   ├── vt_worker_pool.h/cpp     ← Thread pool for parallel FFT analysis
│   │   │   ├── vt_versions.h            ← VoicTune component version registry
│   │   │   ├── ollama_client.h/cpp      ← Ollama REST API client (cpp-httplib)
│   │   │   └── ai_prompt_templates.h    ← System prompts for AI coaching/EQ/NLP
│   │   ├── producer/                    ← Producer daemon (Phase VP/DAW/FA)
│   │   │   ├── main_producer.cpp        ← Entry point, CLI, signal handling
│   │   │   ├── pr_http_api.h/cpp        ← HTTP/HTTPS server, routes, session auth
│   │   │   ├── pr_config.h/cpp          ← YAML config loader
│   │   │   ├── pr_logger.h              ← Logging singleton (→ producer.log)
│   │   │   └── pr_worker_pool.h/cpp     ← Thread pool for video/audio/FFT jobs
│   │   ├── config/
│   │   │   ├── mcaster1_rock_yolo.yaml  ← Test station: 12 slots to dnas.mcaster1.com
│   │   │   ├── mcaster1_voictune.yaml   ← VoicTune daemon config (ports, audio, Ollama)
│   │   │   └── mcaster1_producer.yaml   ← Producer daemon config (ports, workers)
│   │   ├── web_ui/                     ← PHP frontend (served by FastCGI)
│   │   │   ├── login.html              ← Pre-auth login page (plain HTML, no PHP)
│   │   │   ├── style.css               ← Dark navy/teal theme
│   │   │   ├── dashboard.php           ← Live encoder status + Chart.js bandwidth
│   │   │   ├── encoders.php            ← Per-slot DSP controls + live stats
│   │   │   ├── media.php               ← Track library + folder scan + playlist files
│   │   │   ├── playlists.php           ← DB-backed playlist CRUD + load-to-slot
│   │   │   ├── metrics.php             ← Chart.js analytics dashboard
│   │   │   ├── settings.php            ← Server info + config overview
│   │   │   ├── voictune.php            ← VoicTune voice analysis UI
│   │   │   ├── mixer.php               ← Virtual mixer console (6 skins)
│   │   │   ├── podcast.php             ← Podcast show/episode management
│   │   │   ├── recording.php           ← Live recording studio
│   │   │   ├── episode-editor.php      ← Waveform editor with EDL
│   │   │   ├── podcast-site.php        ← Podcast website generator
│   │   │   ├── podcast-analytics.php   ← Podcast download analytics
│   │   │   ├── podcast-episode-page.php ← Public episode detail page
│   │   │   ├── requests.php            ← Song request DJ queue
│   │   │   ├── request-widget.php      ← Public listener request form
│   │   │   ├── widget.php              ← Embeddable now-playing widget
│   │   │   ├── remote-host.php         ← Remote recording host view
│   │   │   ├── remote-guest.php        ← Remote recording guest view
│   │   │   ├── daw.php                 ← Multi-track DAW (timeline, mixing, export)
│   │   │   ├── forensic.php            ← Forensic audio analysis (HQ spectrogram)
│   │   │   ├── producer.php            ← DSP Producer (video, switcher, RTMP)
│   │   │   ├── monetization.php        ← Ad campaigns, DAI, sponsors
│   │   │   ├── compliance.php          ← EBU R128 loudness compliance
│   │   │   ├── schedule.php            ← Clockwheel scheduler UI
│   │   │   ├── crossfader.php          ← 9-curve crossfader controls
│   │   │   ├── encoder-effects-rack.php ← Per-encoder effects rack
│   │   │   ├── effects-rack.php        ← Global effects rack
│   │   │   ├── dualdeck-player.php     ← Dual-deck popup DJ player
│   │   │   ├── jack.php                ← JACK audio routing matrix
│   │   │   ├── js/
│   │   │   │   ├── episode-editor.js   ← Waveform engine + EDL operations
│   │   │   │   ├── mixer-console.js    ← WebGL mixer faders + meters
│   │   │   │   ├── pedalboard.js       ← SVG pedalboard + cable routing
│   │   │   │   ├── pedal-svgs.js       ← SVG pedal faceplate definitions
│   │   │   │   ├── pedal-configs.js    ← Pedal parameter configurations
│   │   │   │   ├── voictune-viz.js     ← Oscilloscope + spectrum + pitch display
│   │   │   │   ├── voictune-audio.js   ← VoicTune audio capture + analysis
│   │   │   │   ├── remote.js           ← WebRTC remote recording client
│   │   │   │   ├── mobile-nav.js       ← Mobile responsive navigation
│   │   │   │   ├── daw-engine.js       ← DAW timeline + clip engine
│   │   │   │   ├── daw-waveform.js     ← DAW waveform rendering
│   │   │   │   ├── forensic-analyzer.js ← Forensic HQ spectrogram + analysis
│   │   │   │   ├── video-producer.js   ← Video capture + switcher + RTMP
│   │   │   │   ├── captions-engine.js  ← Whisper captions + SRT/VTT
│   │   │   │   ├── webgl-viz.js        ← WebGL visualization framework
│   │   │   │   ├── webgl-spectrogram-hq.js ← WebGL HQ spectrogram (65536 FFT)
│   │   │   │   ├── webgl-mixer.js      ← WebGL mixer rendering
│   │   │   │   ├── webgl-pedals.js     ← WebGL pedal knobs + meters
│   │   │   │   ├── webgl-dashboard.js  ← WebGL dashboard visualizations
│   │   │   │   └── webgl-video.js      ← WebGL video preview
│   │   │   ├── css/
│   │   │   │   ├── mixer.css           ← Mixer layout styles
│   │   │   │   ├── mixer-skins.css     ← 6 Mcaster1-branded mixer skins
│   │   │   │   └── responsive.css      ← Mobile responsive breakpoints
│   │   │   └── app/
│   │   │       ├── inc/
│   │   │       │   ├── auth.php        ← mc1_is_authed() (C++ auth gate)
│   │   │       │   ├── user_auth.php   ← MySQL session auth (mc1_current_user etc.)
│   │   │       │   ├── db.php          ← mc1_db() PDO cache + helpers
│   │   │       │   ├── traits.db.class.php ← Mc1Db PDO trait
│   │   │       │   ├── header.php      ← Sidebar nav, Chart.js flag, HTML head
│   │   │       │   ├── footer.php      ← Toasts, mc1Api(), auto PHP session bootstrap
│   │   │       │   ├── logger.php      ← PHP logging (mc1_log, mc1_log_request, etc.)
│   │   │       │   ├── voictune_coaching.php ← VoicTune AI coaching PHP helpers
│   │   │       │   ├── daemon_monitor.php   ← Multi-daemon health check helpers
│   │   │       │   └── media_picker.php     ← Reusable media picker component
│   │   │       └── api/
│   │   │           ├── tracks.php      ← Track CRUD + scan + playlist_files
│   │   │           ├── metrics.php     ← Listener analytics queries
│   │   │           ├── playlists.php   ← Playlist CRUD + load-to-slot
│   │   │           ├── encoders.php    ← Encoder admin actions (DB-only, no C++ proxy)
│   │   │           ├── auth.php        ← PHP session bridge (auto_login, whoami)
│   │   │           ├── podcast.php     ← Podcast CRUD + RSS + publishing API
│   │   │           ├── requests.php    ← Song request + dedication API
│   │   │           ├── webhooks.php    ← Webhook config + dispatch API
│   │   │           ├── remote.php      ← Remote recording session API
│   │   │           ├── daw.php         ← DAW project/clip/automation API
│   │   │           ├── forensic.php    ← Forensic analysis job API
│   │   │           ├── producer.php    ← Video producer job API
│   │   │           ├── ads.php         ← Ad campaign + DAI API
│   │   │           ├── captions.php    ← Closed caption generation API
│   │   │           ├── health.php      ← Multi-daemon health status API
│   │   │           ├── rtmp.php        ← RTMP stream management API
│   │   │           ├── schedule.php    ← Clockwheel schedule API
│   │   │           └── upload.php      ← File upload handler
│   │   ├── external/
│   │   │   └── include/                ← Linux-only vendor headers (header-only libs)
│   │   │       ├── httplib.h           ← cpp-httplib v0.18 (copied from root external/)
│   │   │       └── nlohmann/json.hpp   ← nlohmann/json v3.11
│   │   ├── Makefile.am                 ← Autotools build rules for Linux encoder
│   │   ├── make_phase4.sh              ← Legacy monolithic build script
│   │   ├── make_phase3.sh              ← Previous phase build
│   │   └── make_phase2.sh              ← HTTP-only test build (no audio)
│   └── libmcaster1dspencoder/
│       └── libmcaster1dspencoder.h     ← Shared structs: mc1AdminConfig, mc1SlotConfig
├── external/
│   └── include/                        ← Windows-only vendor headers (DO NOT use in Linux build)
│       ├── httplib.h                   ← cpp-httplib v0.18 (Windows reference copy)
│       ├── pthread.h                   ← pthreads-win32 (Windows only, NEVER use on Linux)
│       └── nlohmann/json.hpp           ← nlohmann/json v3.11
├── configure.ac                        ← Autotools: per-codec detection, MariaDB, libyaml
├── Makefile.am                         ← Autotools root (SUBDIRS = src/linux)
├── autogen.sh                          ← Bootstrap: autoreconf --force --install
├── install-deps.sh                     ← Cross-platform dep installer (apt/dnf/brew/etc)
├── config/                             ← Windows config (do not modify on Linux)
├── docs/
│   └── index.html                      ← Professional HTML5 project documentation
├── build/                              ← Compiled binaries (gitignored)
├── CLAUDE.md                           ← This file
└── README-LINUX.md                     ← Linux build + API reference
```

---

## Authentication System (Two Layers)

### Layer 1: C++ In-Memory Auth (`mc1session` cookie)

- All requests require `mc1session=<64-byte-hex-token>` cookie (or `X-API-Token` header)
- Sessions stored in `std::map<string, SessionEntry>` in `http_api.cpp`
- Session TTL: configurable (default 3600s)
- **Login:** `POST /api/v1/auth/login` → `{ username, password }`
- **Logout:** `POST /api/v1/auth/logout`

### Layer 2: PHP/MySQL Auth (`mc1app_session` cookie)

- PHP pages use `app/inc/user_auth.php` → `mc1_current_user()`
- Sessions stored in `mcaster1_encoder.user_sessions` table
- Cookie: `mc1app_session=<token>` (HttpOnly, SameSite=Lax)

### Auto-Bootstrap Bridge

On every PHP page load, `footer.php` calls `POST /app/api/auth.php { action: "auto_login" }`.
- If C++ auth is valid but no PHP session exists → creates PHP session for first active admin user
- Uses `sessionStorage` flag `mc1php_ok` to avoid repeated calls per tab
- This transparently bridges the two auth layers without requiring the user to log in twice

### Credentials (Test Station)

| System | Username | Password |
|--------|----------|----------|
| C++ admin | `djpulse` | `hackme` |
| MySQL user | `djpulse` | `hackme` (bcrypt in DB) |
| Legacy C++ | `admin` | `changeme` |

**MySQL CLI:** Always use `mysql --defaults-extra-file=~/.my.cnf` (no inline passwords)

---

## PHP-FPM Configuration

### Pool File

```
/etc/php/8.2/fpm/pool.d/mcaster1.conf
```

Key settings:
```ini
[mcaster1]
user = mediacast1
group = www-data
listen = /run/php/php8.2-fpm-mc1.sock
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
/run/php/php8.2-fpm-mc1.sock
```

This is passed to `FastCgiClient` in `http_api.cpp`. If FPM is not running or socket is missing, PHP pages return 502.

### Reload After Config Changes

```bash
sudo systemctl reload php8.2-fpm
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

---

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
systemctl reload php8.2-fpm || true
```

---

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

---

## API Reference (Quick)

All `/api/v1/` endpoints require `mc1session` cookie or `X-API-Token` header.

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/v1/auth/login` | Login → Set-Cookie: mc1session |
| POST | `/api/v1/auth/logout` | Logout → 302 /login |
| GET | `/api/v1/status` | Version, uptime, encoder summary |
| GET | `/api/v1/encoders` | All slots: state, track, bytes, stats |
| POST | `/api/v1/encoders/{slot}/start` | Start encoding + streaming |
| POST | `/api/v1/encoders/{slot}/stop` | Stop gracefully |
| POST | `/api/v1/encoders/{slot}/restart` | Reconnect |
| GET | `/api/v1/encoders/{slot}/stats` | Single-slot live stats |
| GET | `/api/v1/encoders/{slot}/dsp` | DSP config (eq preset, agc, crossfade) |
| PUT | `/api/v1/encoders/{slot}/dsp` | Live-update DSP config |
| POST | `/api/v1/encoders/{slot}/dsp/eq/preset` | Apply named EQ preset |
| GET | `/api/v1/devices` | PortAudio input device list |
| PUT | `/api/v1/metadata` | Push StreamTitle (slot=-1 for all) |
| PUT | `/api/v1/volume` | Master or per-slot volume (0.0–2.0) |
| POST | `/api/v1/playlist/skip` | Skip to next track |
| POST | `/api/v1/playlist/load` | Load playlist file by path |
| GET | `/api/v1/dnas/stats` | Proxy DNAS XML stats as JSON |

### DNAS Stats Quirk

`GET /api/v1/dnas/stats` returns:
```json
{ "ok": true, "body": "<?xml ...<listeners>12</listeners>...", "source": "dnas.mcaster1.com:9443" }
```

The XML is in `d.body` — parse with JS regex:
```js
const m = d.body.match(/<listeners>(\d+)<\/listeners>/i);
const count = m ? parseInt(m[1]) : 0;
```

### C++ Recording API

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/v1/recording/start` | Start recording on a slot |
| POST | `/api/v1/recording/stop` | Stop recording |
| POST | `/api/v1/recording/marker` | Add chapter marker |
| GET | `/api/v1/recording/status` | Recording state for all slots |
| POST | `/api/v1/recording/split` | Split current recording |

### PHP JSON APIs

All POST-only, require authenticated session.

```
POST /app/api/tracks.php      actions: list, search, add, delete, scan, playlist_files
POST /app/api/metrics.php     actions: summary, daily_stats, sessions, top_tracks
POST /app/api/playlists.php   actions: list, get_tracks, create, add_track, remove_track, load
POST /app/api/encoders.php    actions: add_user, delete_user, toggle_user (DB admin only)
POST /app/api/auth.php        actions: login, logout, auto_login, whoami
POST /app/api/podcast.php     actions: list_shows, create_show, list_episodes, create_episode, publish_episode, generate_rss, export_episode, list_targets, schedule_episode, etc.
POST /app/api/requests.php    actions: list, submit, approve, reject, dedicate, get_widget_config
POST /app/api/webhooks.php    actions: list, create, update, delete, test, get_logs
POST /app/api/remote.php      actions: create_session, join, leave, start_recording, stop_recording, get_chat
POST /app/api/daw.php         actions: list_projects, create_project, get_project, add_clip, move_clip, delete_clip, add_automation, mix, export, freeze_track, noise_reduce
POST /app/api/forensic.php    actions: analyze, get_result, compare, generate_report, get_spectrogram, peak_detect
POST /app/api/producer.php    actions: list_sources, add_source, switch_source, start_stream, stop_stream, get_status, set_overlay
POST /app/api/ads.php         actions: list_campaigns, create_campaign, schedule_ad, get_impressions, get_cpm_report
POST /app/api/captions.php    actions: generate, get_status, download_srt, download_vtt, get_live, burn_in
POST /app/api/health.php      actions: check_all, check_daemon (returns status of all 4 daemons)
POST /app/api/rtmp.php        actions: list_streams, start, stop, get_stats
POST /app/api/schedule.php    actions: list, create, update, delete, get_active
POST /app/api/upload.php      actions: upload (multipart file upload handler)
```

**CRITICAL:** Do NOT proxy encoder start/stop/restart from PHP via curl back to C++ on port 8330/8344.
This causes a thread pool deadlock — C++ thread blocked in FastCGI waiting for PHP, PHP curl waiting for C++.
Browser JS must call `/api/v1/encoders/{slot}/start` directly (it has the `mc1session` cookie already).

### VoicTune API (ports 8350/8354)

All endpoints require `mc1vt_session` cookie or `X-API-Token` header (except `/health`).

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/v1/voictune/health` | Health check (no auth) — version, uptime |
| POST | `/api/v1/voictune/auth/login` | Login → Set-Cookie: mc1vt_session |
| POST | `/api/v1/voictune/auth/logout` | Logout |
| GET | `/api/v1/voictune/status` | Config, audio device, WebSocket port, Ollama endpoint |
| GET | `/api/v1/voictune/devices` | PortAudio input/output devices with USB/BT flags |
| GET | `/api/v1/voictune/meters` | RMS, peak, LUFS, pitch_hz, note, cents (stub until VT-2) |
| GET | `/api/v1/voictune/spectrum` | FFT magnitude bins (stub until VT-2) |
| GET | `/api/v1/ai/status` | Ollama availability, model list |

### Producer API (ports 8360/8364)

All endpoints require `mc1pr_session` cookie or `X-API-Token` header (except `/health`).

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/v1/producer/health` | Health check (no auth) — version, uptime |
| POST | `/api/v1/producer/auth/login` | Login |
| GET | `/api/v1/producer/status` | Worker queue depths, active jobs |

---

## Database Reference

MySQL connection via `~/.my.cnf` (never inline credentials).

| Database | Purpose |
|----------|---------|
| `mcaster1_encoder` | users, roles, user_sessions, encoder_configs, streaming_servers, pedalboard_layouts, mixer_configs, mixer_custom_units, webhook_configs, ad_campaigns, ad_schedule, ad_impressions, sponsor_configs |
| `mcaster1_media` | tracks, playlists, playlist_tracks, player_queue, cover_art, track_categories, categories, media_library_paths, podcast_shows, podcast_episodes, episode_markers, publish_targets, publish_queue, podcast_downloads, song_requests, dedications, remote_sessions, remote_participants, remote_chat, daw_projects, daw_tracks, daw_clips, daw_automation, captions, caption_segments |
| `mcaster1_metrics` | listener_sessions, daily_stats, ad_impressions_log |
| `mcaster1_voictune` | sessions, voice_profiles, analysis_snapshots, ai_interactions |
| `mcaster1_producer` | jobs, job_results, forensic_reports |

### Key Tables

```sql
-- mcaster1_encoder.users
id, username, email, display_name, password_hash (bcrypt), role_id, is_active, last_login

-- mcaster1_encoder.pedalboard_layouts (v2.0.0)
id, user_id, layout_name, layout_json, is_default

-- mcaster1_encoder.mixer_configs (v2.0.0)
id, user_id, config_name, skin, channel_json, master_json

-- mcaster1_encoder.mixer_custom_units (v2.0.0)
id, mixer_config_id, unit_type, params_json, position

-- mcaster1_encoder.webhook_configs (v2.0.0)
id, event_type, url, secret, is_active, last_triggered_at

-- mcaster1_media.tracks
id, file_path, title, artist, album, genre, year, duration_ms, bitrate_kbps,
file_size_bytes, play_count, rating, is_missing, bpm, key_sig, last_played_at

-- mcaster1_media.playlists
id, name, description, created_at, updated_at

-- mcaster1_media.playlist_tracks
id, playlist_id, track_id, position, added_at

-- mcaster1_media.podcast_shows (v2.0.0)
id, title, description, author, category, language, cover_art_path, website_url, feed_url, is_active

-- mcaster1_media.podcast_episodes (v2.0.0)
id, show_id, title, description, file_path, duration_sec, format, bitrate_kbps, season, episode_number, published_at, is_published, tags

-- mcaster1_media.episode_markers (v2.0.0)
id, episode_id, marker_type (chapter/note/highlight/ad_break), timestamp_ms, title, url

-- mcaster1_media.publish_targets (v2.0.0)
id, show_id, platform (rss/apple/spotify/youtube/etc.), api_key, config_json, is_active

-- mcaster1_media.publish_queue (v2.0.0)
id, episode_id, target_id, status (pending/scheduled/publishing/published/failed), scheduled_at

-- mcaster1_media.podcast_downloads (v2.0.0)
id, episode_id, client_ip, user_agent, downloaded_at, bytes_sent

-- mcaster1_media.song_requests (v2.0.0)
id, track_id, requester_name, requester_email, message, status (pending/approved/rejected/played), slot_id

-- mcaster1_media.dedications (v2.0.0)
id, request_id, dedication_to, dedication_from, message

-- mcaster1_media.remote_sessions (v2.0.0)
id, host_user_id, session_name, invite_token, status (waiting/active/recording/ended), started_at

-- mcaster1_media.remote_participants (v2.0.0)
id, session_id, display_name, role (host/guest), joined_at, track_file_path

-- mcaster1_media.remote_chat (v2.0.0)
id, session_id, participant_id, message, sent_at

-- mcaster1_metrics.listener_sessions
id, client_ip, user_agent, stream_mount, connected_at, disconnected_at, duration_sec, bytes_sent

-- mcaster1_voictune.sessions
id, session_name, user_id, started_at, ended_at, duration_sec, notes

-- mcaster1_voictune.voice_profiles
id, user_id, profile_name, fundamental_hz, voice_type, avg_lufs, avg_rms_db,
eq_preset_json, effects_chain_json, analysis_json

-- mcaster1_voictune.analysis_snapshots
id, session_id, timestamp_ms, rms_db, peak_db, lufs, pitch_hz, note_name, cents_off, spectrum_json

-- mcaster1_voictune.ai_interactions
id, user_id, context, prompt_text, response_text, model_used, latency_ms
```

---

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

---

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

---

## E2E Test Flow (Phase L5)

```bash
# 1. Build (autotools canonical)
./configure && make -j$(nproc)

# Or legacy:
# bash src/linux/make_phase4.sh

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

# 6. Browser test
# https://encoder.mcaster1.com:8344 → login → /dashboard.php
# Check: 3 encoder cards show IDLE
# Media → Playlist Files → find lyndenradio-all.m3u (1079 tracks)
# Load to Slot 1 → /api/v1/playlist/load
# Dashboard → Slot 1 Start → connects to dnas.mcaster1.com:9443/yolo-rock
# Metrics → Chart.js shows live bytes_sent
```

---

## Common Tasks

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

### Check PHP-FPM

```bash
systemctl status php8.2-fpm
sudo systemctl reload php8.2-fpm      # After config changes
ls -la /run/php/php8.2-fpm-mc1.sock  # Verify socket exists
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

---

## Phase Status

| Phase | Version | Description | Status |
|-------|---------|-------------|--------|
| L1 | v1.0.0 | Infrastructure (platform.h, build system) | **COMPLETE** |
| L2 | v1.1.1 | HTTP/HTTPS admin server + login + web UI | **COMPLETE** |
| L3 | v1.2.0 | Audio encoding + streaming + FastCGI + PHP | **COMPLETE** |
| L4 | v1.3.0 | DSP chain (EQ/AGC/xfade) + ICY2 + DNAS stats | **COMPLETE** |
| L5 | v1.4.0 | Full PHP frontend overhaul + logging system | **COMPLETE** |
| L5.1-L5.5 | v1.4.1-v1.4.5 | Media library, player, editor, popup player, categories | **COMPLETE** |
| L6 | v1.5.0 | Streaming Server Relay Monitor | **COMPLETE** |
| L7 | v1.5.1 | Listener Analytics + CSV export + GeoIP | **COMPLETE** |
| DJ-1..6 | v1.6.0 | 9-curve crossfader, effects rack, PTT, JACK, dual-deck, per-slot FX | **COMPLETE** |
| L8-SPLIT | v1.7.0 | Dual binary: admin + encoder (fault isolation) | **COMPLETE** |
| L9 | v1.7.0 | Clockwheel scheduler + dead air detection | **COMPLETE** |
| L-METRICS | v1.7.0 | System Health Dashboard (disk, codecs, FPM, SSL) | **COMPLETE** |
| L-MEDIA | v1.7.0 | Folder browser, scan progress, category types/weights | **COMPLETE** |
| VT-1..4 | v1.8.0 | VoicTune daemon (FFT, pitch, meters, coaching, web UI) | **COMPLETE** |
| PB-1..3 | v1.8.0 | Visual pedalboard (SVG pedals, cable routing, live meters) | **COMPLETE** |
| AI-1..4 | v1.8.0 | Ollama AI (coaching, EQ/chain, NLP, content analysis, playlists) | **COMPLETE** |
| MX-1..3 | v1.8.0 | Virtual mixer console (channel strips, 6 skins, presets) | **COMPLETE** |
| L10 | v1.8.0 | Podcast archive management + RSS feed generation | **COMPLETE** |
| L11 | v1.8.0 | Song requests, dedications, webhooks, embeddable widget | **COMPLETE** |
| PC-1..7 | v1.8.0 | Podcast studio (recording, editor, publishing, analytics, website, AI, remote) | **COMPLETE** |
| VP-1 | v1.9.0 | DSP Producer daemon (video capture, RTMP streaming) | **COMPLETE** |
| VP-2 | v1.9.0 | Video switcher + multicam + overlays | **COMPLETE** |
| VP-3 | v1.9.0 | Vodcast support (video + audio encoding) | **COMPLETE** |
| DAW-1 | v1.9.0 | Multi-track DAW (timeline, clip editing, automation) | **COMPLETE** |
| DAW-2 | v1.9.0 | DAW effects chain + mixing + export | **COMPLETE** |
| DAW-3 | v1.9.0 | DAW noise reduction + freeze tracks | **COMPLETE** |
| FA-1 | v1.9.0 | Forensic audio (HQ spectrogram, 65536 FFT, WSOLA) | **COMPLETE** |
| FA-2 | v1.9.0 | Forensic compare, peak detection, reports, AI | **COMPLETE** |
| FA-3 | v1.9.0 | Goniometer + noise subtraction + EBU R128 | **COMPLETE** |
| CC-1 | v1.9.0 | Closed captions (Whisper, SRT/VTT, live, burn-in, RSS) | **COMPLETE** |
| MON-1 | v1.9.0 | Monetization (DAI, campaigns, CPM, impressions, sponsors) | **COMPLETE** |
| NAV-1 | v1.9.0 | Mode-based navigation (5 modes), daemon health monitor | **COMPLETE** |
| MOBILE-1 | v1.9.0 | Mobile responsive UI, media picker component | **COMPLETE** |
| VIZ-1 | v1.9.0 | WebGL visualizations (spectrogram, 3D spectrum, globe, knobs) | **COMPLETE** |

---

## Windows Dev Notes (VS2022)

The Windows build lives in `src/` (NOT `src/linux/`). Key differences:

| Aspect | Windows |
|--------|---------|
| Audio | WASAPI / DirectSound via Windows SDK |
| Build | Visual Studio 2022, `Mcaster1DSPEncoder.sln` |
| PHP bridge | None — Windows build is API-only |
| Deploy | Windows service via `sc create` |
| Config | `config/mcaster1.yaml` (in `config/` root) |
| Credentials | See `config/` directory (gitignored sensitive files) |

Windows-specific source files:
- `src/audio_wasapi.cpp` — WASAPI device capture
- `src/audio_directsound.cpp` — DirectSound output
- `src/win_service.cpp` — Windows service wrapper
- `src/win_tray.cpp` — System tray icon

**Note:** `config/*.yaml` is in `.gitignore` to prevent credentials from being committed.
For Windows config, manually copy from `config/mcaster1.yaml.example`.

---

## Key Gotchas

1. **SO_REUSEPORT deadlock**: Killing and restarting while old instance is alive → both bind to same ports → session confusion. Always `pkill -9 -f mcaster1-encoder && sleep 2` before restart.

2. **uopz blocks exit()**: Never use `exit()` or `die()` in PHP. Use `return` inside functions.

3. **PHP→C++ loopback deadlock**: Never curl from PHP to the same C++ server (127.0.0.1:8330). C++ thread pool exhaustion. Browser JS should call `/api/v1/...` directly.

4. **httponly cookie not readable by JS**: The `mc1app_session` cookie is httponly — `document.cookie` won't show it. Use `sessionStorage` flags instead to track PHP auth state in JS.

5. **DNAS stats XML-in-JSON**: `/api/v1/dnas/stats` wraps XML in a JSON `body` field. Parse with regex in JS.

6. **MySQL CLI credentials**: Always `mysql --defaults-extra-file=~/.my.cnf` — never inline passwords.

7. **Stack traces need -rdynamic**: Symbol names in backtraces require `-rdynamic` linker flag (added to `make_phase4.sh`).

8. **PHP-FPM socket must be readable by encoder process**: Socket is `mediacast1:www-data`. Encoder runs as `mediacast1`, so it can read the socket.

9. **mc1app_session auto-bootstrap**: footer.php uses `sessionStorage.getItem('mc1php_ok')` as guard — cleared on tab/window close. Each new tab re-runs auto_login once.

10. **Windows vendor headers shadow Linux system headers**: `external/include/pthread.h` is pthreads-win32. If the Linux build ever references `external/include/` with `-I`, it will shadow `/usr/include/pthread.h`. The Linux build uses `src/linux/external/include/` exclusively (only httplib.h and nlohmann/json.hpp). The Windows `external/include/` directory must never be added to Linux include paths.

11. **Codec -DHAVE_XXX not automatic from config.h**: `encoder_slot.cpp`, `file_source.cpp`, etc. use `#ifdef HAVE_LAME` but never `#include "config.h"`. Autotools `AC_DEFINE([HAVE_LAME])` writes to `config.h` but the source never reads it. Each `if HAVE_LAME` block in `src/linux/Makefile.am` must explicitly add `-DHAVE_LAME` to `AM_CPPFLAGS`. Do not remove these defines or encoding will silently fall back with "LAME not compiled in" at runtime.

12. **DOMContentLoaded ordering in PHP pages**: `dashboard.php` and `encoders.php` define `poll()` / `refreshLive()` in a `<script>` block in the page body. `footer.php` (included after) defines `mc1Api`. Any code that calls `mc1Api` or `poll()` directly in a `<script>` block (not inside a function or DOMContentLoaded listener) will throw TypeError and prevent `setInterval` from being set up. Always wrap startup calls (`poll()`, `setInterval`, `requestAnimationFrame(tickProgress)`) in `document.addEventListener('DOMContentLoaded', ...)`.

13. **ICY metadata first-connect vs reconnect**: `StreamClient` fires the `CONNECTED` state callback on both first connect and watchdog reconnects. Use `first_connect_done_.exchange(true)` (atomic bool, reset to false on `start()`) to distinguish them. The first call returns `false` (skip re-push, track change will push it). Subsequent calls return `true` (reconnect: re-push current track after 500ms delay). Never use `source_->is_running()` as the guard — the source IS running by the time the TCP CONNECTED event fires.

14. **Binary "Text file busy" on rebuild**: If the encoder is running and you try to overwrite the binary with `cp`, you get "Text file busy". Always `pkill -9 -f mcaster1-encoder && sleep 1` before copying or rebuilding.

15. **Vorbis quality scale**: `vorbis_encode_init_vbr()` takes a float 0.0–1.0. We store `quality INT 0–10` in the DB and divide by 10.0 at init time. 0≈64kbps, 5≈128kbps, 9≈256kbps, 10≈320kbps+.

16. **HE-AAC v2 must have stereo input**: PS (Parametric Stereo) expects 2-channel input and derives the spatial cue from a mono downmix. Sending mono to fdk-aac with AOT=29 causes an encoder init failure. The web UI must lock channels=2 when codec=aac_hev2.

17. **Opus always resamples to 48kHz internally**: The `sample_rate` passed to `ope_encoder_create_callbacks()` is the input sample rate. libopusenc (or libopus directly) resamples internally to 48kHz. The stream always carries 48kHz. Setting sample_rate=44100 is fine — it will be resampled.

18. **FLAC compression_level hardcoded**: Currently `FLAC__stream_encoder_set_compression_level(state->enc, 5)`. Should be `std::clamp(cfg_.quality, 0, 8)` after task #26 is done.

19. **MP3 lame_set_quality hardcoded**: Currently `lame_set_quality(gfp, 2)` — this is the encoding ALGORITHM quality (0=slowest/best, 9=fastest/worst), not the VBR level. Currently CBR only. Task #26 adds VBR/ABR mode support.

20. **cpp-httplib route limit**: Keep individual route registrations (one per endpoint). Do not use catch-all route dispatchers — cpp-httplib requires exact path patterns or simple wildcards. Complex regex dispatching causes silent route misses.

21. **Cookie SameSite=Lax not Strict**: Session cookies must use `SameSite=Lax` (not `Strict`). `Strict` breaks cross-port navigation (e.g., 8330 to 8344) because the browser treats different ports as different sites and strips `SameSite=Strict` cookies on the redirect.

22. **Conditional Secure flag on cookies**: Only set the `Secure` flag on session cookies when the connection is HTTPS. HTTP port 8330 cookies with `Secure` are silently dropped by the browser, causing login loops. Check `req.has_header("X-Forwarded-Proto")` or socket SSL state before setting the flag.

23. **Kill competing old service instances before restart**: Due to `SO_REUSEPORT`, always `pkill -9 -f mcaster1-dsp-encoder && sleep 2` before restarting. Old and new instances binding to the same port causes session token mismatches and 302 redirect loops.

24. **Encoder proc_only health**: The encoder child process has a `/api/v1/health` endpoint that returns `proc_only: true` to indicate it does not serve the web UI. The admin process proxies health checks. Do not send web UI requests directly to the encoder child.

25. **CSS Grid children and sidebar nav**: Mode-based navigation uses CSS Grid with `grid-template-columns`. Adding new sidebar items requires updating the grid column count in `header.php` and `responsive.css` to avoid layout overflow on smaller screens.

26. **Mode nav state persistence**: The active UI mode (Broadcast, Podcast, Producer, Forensic, DAW) is stored in `localStorage['mc1_mode']`. The sidebar filters visible pages based on this value. Changing modes does not reload the page — it toggles CSS visibility classes.

---

## Encoder Editor (web_ui) — Phase L5.1

Created during the 2026-02-24 session:

### New Files

| File | Purpose |
|------|---------|
| `web_ui/edit_encoders.php` | Full-page per-slot encoder config editor (6 tabs) |
| `web_ui/app/inc/edit.encoders.class.php` | EditEncoders static class: load/all/validate/save/delete |

### Encoder Slot Concept

A **slot** is a numbered instance of the C++ `EncoderSlot` object. Each slot has an independent DSP chain, audio source, codec encoder, and stream client. Multiple slots run simultaneously to broadcast different streams (different mounts, codecs, or servers).

- `slot_id` in DB = slot number in C++ (1, 2, 3…)
- `AudioPipeline` owns a `std::vector<EncoderSlot>` and manages all slots
- Each slot reads its config from YAML at startup via `config_loader.cpp`
- The DB `encoder_configs` table stores the web UI's copy of config; C++ reads YAML
- The "Add Encoder" wizard quick-creates a DB row then redirects to `edit_encoders.php`
- Gear icon on `encoders.php` now links to `/edit_encoders.php?id={db_id}`
- Edit button on `settings.php` now links to `/edit_encoders.php?id={db_id}`

### Codec Parameter Matrix

What each codec reads from `EncoderConfig` and what the DB stores:

| Codec | bitrate_kbps | quality (0-10) | encode_mode | channels | sample_rate | Notes |
|-------|-------------|----------------|-------------|----------|-------------|-------|
| mp3 | ✓ CBR brate | algorithm 0-9 (hardcoded=2) | cbr/vbr/abr (planned #26) | MONO or JOINT_STEREO | any | Joint stereo default |
| vorbis | ✗ not used | ✓ VBR quality (÷10) | always vbr | 1-2 | 22050/44100/48000 | Quality 5 ≈ 128kbps |
| opus | ✓ kbps×1000 | not used | vbr default (planned #26) | 1-2 | any (→48k internal) | 64-320 for music |
| flac | ✗ lossless | compress 0-8 (hardcoded=5) | n/a lossless | 1-8 | 44100/48000+ | For archival only |
| aac_lc | ✓ kbps×1000 | not used (afterburner=1) | cbr | 1-2 | 22050-48000 | 64-320 kbps range |
| aac_he | ✓ kbps×1000 | not used | cbr | 1-2 | 32000-48000 | 32-128 kbps sweet spot |
| aac_hev2 | ✓ kbps×1000 | not used | cbr | MUST be 2 | 32000-48000 | 16-64 kbps, PS locks stereo |
| aac_eld | ✓ kbps×1000 | not used | cbr | 1-2 | 32000-48000 | 512-sample granule, SBR-ELD |

### Pending DB Migration (Task #24)

```sql
ALTER TABLE encoder_configs
  ADD COLUMN quality      INT          NOT NULL DEFAULT 5    AFTER channels,
  ADD COLUMN encode_mode  ENUM('cbr','vbr','abr') NOT NULL DEFAULT 'cbr' AFTER quality,
  ADD COLUMN channel_mode ENUM('stereo','joint','mono','parametric') NOT NULL DEFAULT 'joint' AFTER encode_mode;
```

### Valid Bitrate Ranges per Codec (for UI validation)

| Codec | Min kbps | Max kbps | Recommended |
|-------|----------|----------|-------------|
| mp3 CBR | 32 | 320 | 128-256 |
| mp3 VBR | — (V0-V9 quality) | — | V2=~190kbps |
| vorbis | — (quality slider) | — | quality 5-7 |
| opus | 32 | 320 | 64-192 (music) |
| flac | — (lossless) | — | n/a |
| aac_lc | 64 | 320 | 128 |
| aac_he | 24 | 128 | 64-96 stereo |
| aac_hev2 | 16 | 64 | 32-48 |
| aac_eld | 24 | 192 | 48-96 |

### Valid Sample Rates per Codec

| Codec | Valid sample rates |
|-------|-------------------|
| mp3 | 22050, 32000, 44100, 48000 |
| vorbis | 22050, 44100, 48000 |
| opus | any (resampled to 48kHz internally) |
| flac | 44100, 48000, 88200, 96000 |
| aac_lc | 22050, 32000, 44100, 48000 |
| aac_he | 32000, 44100, 48000 |
| aac_hev2 | 32000, 44100, 48000 |
| aac_eld | 32000, 44100, 48000 |
