# Mcaster1DSPEncoder — Linux Build Reference

**Maintainer:** Dave St. John <davestj@gmail.com>
**Binary:** `mcaster1-encoder`
**Version:** v1.4.5 (Phase L5.5 — Standalone Popup Player + Category UX Overhaul)
**Platform:** Debian 12 / Ubuntu 22.04+ (amd64)

---

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Phase Status](#phase-status)
4. [Phase L1 — Infrastructure](#phase-l1--infrastructure-v100--complete)
5. [Phase L2 — HTTP Admin Server](#phase-l2--httphttps-admin-server-v111--complete)
6. [Phase L3 — Audio Engine](#phase-l3--audio-encoding--streaming-engine-v120--complete)
7. [Phase L4 — DSP + ICY2 + DNAS Stats](#phase-l4--dsp-chain--icy2-headers--dnas-stats-v130--complete)
8. [Phase L5 — PHP Frontend + Logging](#phase-l5--full-php-frontend-overhaul--logging-v140--complete)
9. [Phase L6 — Analytics](#phase-l6--analytics--listener-metrics-v150--planned)
10. [Build Instructions](#build-instructions)
11. [PHP-FPM Setup](#php-fpm-setup)
12. [Logging System](#logging-system)
13. [Authentication System](#authentication-system)
14. [Configuration](#configuration)
15. [API Reference](#api-reference)
16. [Database Schema](#database-schema)
17. [PHP App Layer](#php-app-layer)
18. [Default Credentials](#default-credentials)
19. [Deployment](#deployment)

---

## Overview

`mcaster1-encoder` is the Linux CLI + web admin build of the Mcaster1DSPEncoder project.
It runs as a standalone binary providing:

- An embedded HTTP/HTTPS admin server (cpp-httplib)
- A FastCGI bridge to php-fpm for the PHP web UI
- A full audio encoding and streaming engine (PortAudio + libavcodec + LAME/Vorbis/Opus/FLAC)
- Live broadcast to Icecast2 / Shoutcast / Mcaster1DNAS

The Windows build (VS2022) and Linux build share no code — they are maintained in parallel.
All Linux source lives under `src/linux/`.

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                  mcaster1-encoder binary                 │
│                                                         │
│  ┌────────────┐    ┌──────────────────────────────────┐ │
│  │  HTTP/S    │    │       Audio Pipeline             │ │
│  │  Admin     │    │                                  │ │
│  │  Server    │◄───┤  AudioSource  (PortAudio/File)   │ │
│  │            │    │       ↓                          │ │
│  │  cpp-      │    │  EncoderSlot  (per stream)       │ │
│  │  httplib   │    │   MP3/Vorbis/Opus/FLAC           │ │
│  │            │    │       ↓                          │ │
│  │  FastCGI   │    │  StreamClient (Icecast2/Shout)   │ │
│  │  Bridge────┼───►│       ↓                          │ │
│  │            │    │  ArchiveWriter (WAV/MP3 file)    │ │
│  └────────────┘    └──────────────────────────────────┘ │
│         │                                               │
│         ▼                                               │
│   php-fpm Unix socket                                   │
│   /run/php/php8.2-fpm-mc1.sock                         │
│         │                                               │
│         ▼                                               │
│   PHP Web UI  (media, metrics, playlists)               │
│   MySQL: mcaster1_encoder / mcaster1_media / mcaster1_metrics │
└─────────────────────────────────────────────────────────┘
```

---

## Phase Status

| Phase | Version | Name | Status |
|-------|---------|------|--------|
| L1 | v1.0.0 | Infrastructure (platform.h, ICY 2.2 structs, build system) | **COMPLETE** |
| L2 | v1.1.1 | HTTP/HTTPS admin server + login + web UI | **COMPLETE** |
| L3 | v1.2.0 | Audio encoding + streaming engine + FastCGI + PHP app | **COMPLETE** |
| L4 | v1.3.0 | DSP chain (EQ/AGC/crossfade) + ICY2 headers + DNAS stats proxy | **COMPLETE** |
| L5 | v1.4.0 | Full PHP frontend overhaul + logging system + auth bridge + rAF progress | **COMPLETE** |
| L5.1 | v1.4.1 | Media library — folder browser, track tags, artwork, playlists, DB paths | **COMPLETE** |
| L5.2 | v1.4.2 | Browser media player — queue, audio streaming (206 Range), drag-select | **COMPLETE** |
| L5.3 | v1.4.3 | Session fix, Content-Range fix (Firefox), 30-day cookie TTL | **COMPLETE** |
| L5.4 | v1.4.4 | Encoder editor, playlist generator wizard (8 algorithms), user profile | **COMPLETE** |
| **L5.5** | **v1.4.5** | **Standalone popup player, category UX overhaul, HTML quoting bug sweep** | **COMPLETE** |
| L6 | v1.5.0 | Streaming Server Relay Monitor (SAM Broadcaster-style multi-server) | PLANNED |
| L7 | v1.6.0 | Listener Analytics & Full Metrics Dashboard | PLANNED |
| L8 | v1.7.0 | System Health Monitoring (CPU/Mem/Net via /proc) | PLANNED |

---

## Phase L1 — Infrastructure `v1.0.0` — COMPLETE

**Goal:** Platform portability layer and build scaffolding.

### What Was Done

| Component | File | Details |
|-----------|------|---------|
| Platform abstraction | `src/platform.h` | Unified `#ifdef MC1_PLATFORM_LINUX` macros, socket types, path separators |
| ICY 2.2 structs | `src/libmcaster1dspencoder/libmcaster1dspencoder.h` | `mc1IcyInfo`, `mc1ListenSocket`, `mc1AdminConfig` structs ported from Windows |
| YAML config | `src/libmcaster1dspencoder/` | Multi-station YAML config parser via `libyaml` |
| Config stub | `src/linux/config_stub.cpp` | Minimal `gAdminConfig` for HTTP-test builds |
| Build system | `src/linux/Makefile.am` | autotools skeleton (not primary build path) |
| Phase scripts | `src/linux/make_phase2.sh` | HTTP-only build (for CI/regression) |

---

## Phase L2 — HTTP/HTTPS Admin Server `v1.1.1` — COMPLETE

**Goal:** Embedded web admin server with login, session management, and static file serving.

### What Was Done

| Component | File | Details |
|-----------|------|---------|
| HTTP server | `http_api.cpp` | cpp-httplib v0.18 + nlohmann/json v3.11, per-listener thread model |
| HTTPS support | `http_api.cpp` | SSLServer with cert/key per socket, `--ssl-gencert` auto-generate |
| Session store | `http_api.cpp` | In-memory map, 64-byte hex token, configurable TTL |
| Cookie auth | `http_api.cpp` | `mc1session=<token>` HttpOnly + SameSite=Strict |
| Token auth | `http_api.cpp` | `X-API-Token: <token>` header for script access |
| Login page | `web_ui/login.html` | Dark theme SPA, POST /api/v1/auth/login |
| Dashboard | `web_ui/index.html` | Sidebar nav, volume slider, encoder status cards |
| CLI | `main_cli.cpp` | `--http-port`, `--https-port`, `--bind`, `--ssl-cert`, `--ssl-key`, `--ssl-gencert`, `--config` |

### API Endpoints (Phase L2)

```
POST /api/v1/auth/login          { username, password } → Set-Cookie: mc1session
POST /api/v1/auth/logout         Clears session cookie + session store
GET  /api/v1/status              Server version, uptime, encoder summary
```

### Web Routes

```
GET  /                           Redirects to /dashboard or /login
GET  /login                      Login page (no auth required)
GET  /dashboard                  Main SPA (requires session)
GET  /*.css, *.js, *.ico ...     Static assets (no auth — login page uses them)
```

---

## Phase L3 — Audio Encoding + Streaming Engine `v1.2.0` — COMPLETE

**Goal:** Wire the admin server to real audio — device capture, playlist streaming,
multi-format encoding, and live broadcast.

### New Source Files

```
src/linux/
  audio_source.h/cpp        AudioSource interface + PortAudio backend
  file_source.h/cpp         File streaming (libmpg123 + libavcodec)
  playlist_parser.h/cpp     M3U / PLS / XSPF / TXT parser
  encoder_slot.h/cpp        Per-slot state machine (IDLE/CONNECTING/LIVE/ERROR)
  stream_client.h/cpp       Icecast2 / Shoutcast v1/v2 SOURCE client
  archive_writer.h/cpp      Simultaneous WAV + MP3 archive output
  audio_pipeline.h/cpp      Master pipeline orchestrator (multi-slot)
  fastcgi_client.h/cpp      Binary FastCGI/1.0 client (AF_UNIX socket)
```

### Audio Input

#### PortAudio Device Capture

```
Backend   Library              Notes
ALSA      portaudio + pa_alsa  Native Linux kernel audio
PulseAudio portaudio + pa_pulse Desktop audio servers
PipeWire  via PulseAudio compat Modern replacement (Ubuntu 22.04+)
JACK      portaudio + pa_jack  Pro audio / low-latency
```

- Device enumeration via `GET /api/v1/devices`
- Sample formats: float32 stereo/mono
- Configurable sample rate (44100 / 48000)

#### Playlist File Streaming (`FileSource`)

- **MP3**: libmpg123 backend (native MP3 decoding)
- **OGG/FLAC/Opus/AAC/M4A/WAV**: libavformat + libavcodec + libswresample
- Automatic format selection by file extension
- Background decode thread with PCM callback
- Metadata extraction (title, artist, album, genre, year, duration, bitrate)
- Track seek support
- Position tracking in milliseconds
- EOF callback for automatic playlist advance

#### Playlist Parser

| Format | Extension | Features |
|--------|-----------|----------|
| Extended M3U | `.m3u`, `.m3u8` | `#EXTINF` duration + title, relative/absolute paths |
| Winamp PLS | `.pls` | `File1=`, `Title1=`, `Length1=` |
| XSPF | `.xspf` | XML Shareable Playlist Format |
| Plain paths | `.txt` | One path per line |

### Encoding Codecs

| Codec | Library | Container | Status |
|-------|---------|-----------|--------|
| MP3 | libmp3lame | raw MPEG | **COMPLETE** |
| Ogg Vorbis | libvorbis + libogg | Ogg | **COMPLETE** |
| Opus | libopusenc | Ogg | **COMPLETE** |
| FLAC | libFLAC | native FLAC | **COMPLETE** |

All codecs use callback-based output buffers — no temp files, direct stream write.

### Streaming Protocols (`StreamClient`)

| Protocol | Auth | ICY Metadata |
|----------|------|-------------|
| Icecast2 HTTP PUT | Basic (user:pass) | `GET /admin/metadata` |
| Shoutcast v1 ICY SOURCE | Password only | Inline 16-byte-aligned blocks |
| Shoutcast v2 Ultravox | Password only | Extended headers |

Features:
- Auto-reconnect watchdog with exponential backoff
- Connection health monitoring (MSG_PEEK)
- ICY 2.2 extended headers on SOURCE connect
- Bytes-sent and uptime tracking

### Archive Writer

- Simultaneous WAV + MP3 output alongside live stream
- Filename: `{station}_{YYYY-MM-DD}_{HH-MM-SS}.wav/.mp3`
- WAV: 16-bit PCM with proper header (patched on close)
- MP3: LAME CBR encoding at configured bitrate
- Podcast RSS generation: TODO (Phase L4)

### FastCGI Bridge

The `FastCgiClient` class implements FastCGI/1.0 over AF_UNIX:

```
C++ HTTP server → FastCgiClient → php-fpm → PHP response → C++ server → browser
```

- Socket: `/run/php/php8.2-fpm-mc1.sock`
- Pool: `mcaster1` (runs as `mediacast1:www-data`)
- Auth gate: authenticated requests get `HTTP_X_MC1_AUTHENTICATED: 1`
- PHP files exposed: `/app/*.php` and `/app/api/*.php`
- `inc/` files blocked (not matched by C++ route patterns)

### New API Endpoints (Phase L3)

```
GET  /api/v1/encoders                    List all encoder slots + stats
POST /api/v1/encoders/{slot}/start       Start encoding + streaming
POST /api/v1/encoders/{slot}/stop        Stop gracefully
POST /api/v1/encoders/{slot}/restart     Reconnect without full stop
GET  /api/v1/encoders/{slot}/stats       Live stats: bytes, uptime, track, position
GET  /api/v1/devices                     List PortAudio input devices
PUT  /api/v1/metadata                    Push StreamTitle= to live slots
PUT  /api/v1/volume                      Master or per-slot gain (0.0–2.0)
POST /api/v1/playlist/skip               Skip to next track
POST /api/v1/playlist/load               Load playlist file by path
```

### PHP App Layer

#### PHP Pool Configuration

```ini
; /etc/php/8.2/fpm/pool.d/mcaster1.conf
[mcaster1]
user = mediacast1
group = www-data
listen = /run/php/php8.2-fpm-mc1.sock
listen.owner = mediacast1
listen.group = www-data
```

#### PHP App Files (Phase L5.5 — current)

```
web_ui/
  login.html              Pre-auth login page (plain HTML, no PHP needed)
  style.css               Dark navy/teal theme + responsive sidebar + Chart.js styles
  dashboard.php           Live encoder cards + rAF 60fps progress bars + Chart.js bandwidth
  encoders.php            Per-slot DSP panel + live stats + SLEEP/Wake state handling
  edit_encoders.php       Full 6-tab per-slot encoder config editor
  media.php               Track library (folder browser) + categories + scan + playlist files
  mediaplayer.php         Embedded browser audio player with DB-backed queue
  mediaplayerpro.php      Standalone WMP-style 3-column popup player (NEW in L5.5)
  playlists.php           DB-backed playlist CRUD + 4-step generation wizard (8 algorithms)
  metrics.php             Chart.js listener analytics
  profile.php             User profile page (display_name, email, password change)
  settings.php            Server info + encoder config table + DB admin panel
  app/
    inc/
      auth.php            mc1_is_authed() — checks HTTP_X_MC1_AUTHENTICATED header
      db.php              mc1_db($dbname) — PDO connection cache + helpers (h(), fmtDur())
      traits.db.class.php Mc1Db trait — rows(), row(), run(), scalar(), lastId()
      header.php          HTML head, sidebar nav, Chart.js flag
      footer.php          Toasts, mc1Api(), mc1Toast(), mc1State, auto PHP session bootstrap
      user_auth.php       MySQL session auth — mc1_current_user(), mc1_require_auth(), mc1_can()
      logger.php          mc1_log(), mc1_log_request(), mc1_api_respond()
      mc1_config.php      Constants: MC1_AUDIO_ROOT, MC1_PLAYLIST_DIR, MC1_ARTWORK_DIR
      edit.encoders.class.php  EditEncoders: load(), all(), validate(), save(), delete()
      playlist.builder.algorithm.class.php  PlaylistBuilderAlgorithm: 8 generation algorithms
      playlist.manager.pro.class.php  PlaylistManagerPro: generate(), preview() orchestrator
      profile.usr.class.php  ProfileUser: get(), update(), change_password()
      metrics.serverstats.class.php  Mc1MetricsServerStats: server stat queries
      media.lib.player.php  Player queue helpers (add, list, remove, clear)
      icons.php            SVG icon helper functions
    api/
      tracks.php          list, search, add, update, delete, scan, playlist_files,
                          browse, get_categories, create_category, delete_category,
                          add_to_category, list_category_tracks, remove_from_category,
                          get_artwork, set_artwork, fetch_artwork_musicbrainz
      metrics.php         summary, daily_stats, sessions, top_tracks
      playlists.php       list, get_tracks, create, delete, add_track, remove_track,
                          load, get_categories, get_algorithms, get_rotation_rules,
                          preview, generate, get_clock_templates, save_clock_template
      player.php          queue_add, queue_list, queue_remove, queue_clear, queue_move_top
      audio.php           HTTP Range (206) audio file streaming
      artwork.php         Cover art serving by hash
      encoders.php        list_configs, get_config, save_config, delete_config,
                          update_playlist, add_user, update_user, delete_user,
                          toggle_user, reset_password, db_stats, get_token, generate_token
      auth.php            login, logout, auto_login, whoami
      servers.php         CRUD for streaming_servers table + live stat proxy
      serverstats.php     Server statistics queries
      profile.php         get_profile, update_profile, change_password
```

**Note on `uopz` PHP extension:** This server has the `uopz` extension active, which
intercepts all `exit()`/`die()` calls. All PHP files use `if/elseif` chains — no `exit()`.

#### MySQL Databases

| Database | Purpose |
|----------|---------|
| `mcaster1_encoder` | Users, roles, sessions, encoder config |
| `mcaster1_media` | Track library (title, artist, album, genre, bitrate, play_count) |
| `mcaster1_metrics` | Listener sessions, daily stats, bytes |

---

## Phase L4 — DSP Chain + ICY2 Headers + DNAS Stats `v1.3.0` — COMPLETE

### DSP Processing Chain

```
[AudioSource: PCM float32]
         ↓
┌────────────────────────────────────┐
│           DspChain                 │
│   [10-band Parametric EQ]          │
│         ↓                          │
│   [AGC / Compressor / Limiter]     │
└────────────────────────────────────┘
         ↓   (crossfader applied at track boundaries by EncoderSlot)
[Encoder Pool: MP3/Opus/Vorbis/FLAC]
         ↓
[StreamClient — Icecast2 / Mcaster1DNAS]
```

| Component | File | Details |
|-----------|------|---------|
| 10-band parametric EQ | `dsp/eq.h`, `dsp/eq.cpp` | RBJ Audio EQ Cookbook biquad IIR; presets: `flat`, `classic_rock`, `country`, `modern_rock`, `broadcast`, `spoken_word` |
| AGC / compressor | `dsp/agc.h`, `dsp/agc.cpp` | Feedforward peak compressor, attack/release smoothing, makeup gain, hard limiter ceiling |
| Equal-power crossfader | `dsp/crossfader.h`, `dsp/crossfader.cpp` | `cos(t·π/2)` / `sin(t·π/2)` curves; single-buffer fade-in/fade-out and dual-buffer mix modes |
| DSP chain orchestrator | `dsp/dsp_chain.h`, `dsp/dsp_chain.cpp` | Wires EQ → AGC; crossfader called externally at track boundaries |
| DSP wiring in encoder | `encoder_slot.cpp` | `on_audio()` allocates mutable buffer and calls `dsp_chain_->process()` after volume; `stop()` resets chain |
| Live DSP reconfigure | `encoder_slot.cpp`, `audio_pipeline.cpp` | `reconfigure_dsp()` updates running chain without restarting encoder |

### ICY2 Extended Headers

`StreamTarget` now carries social/identity fields sent as `Ice-*` / `Icy-*` headers
at connect time (Icecast 2.4+ / Mcaster1DNAS):

| Field | Header | Example |
|-------|--------|---------|
| `icy2_twitter` | `Icy-Twitter` | `@Mcaster1Radio` |
| `icy2_facebook` | `Icy-Facebook` | `https://facebook.com/mcaster1radio` |
| `icy2_instagram` | `Icy-Instagram` | `@mcaster1radio` |
| `icy2_email` | `Icy-Email` | `studio@mcaster1.com` |
| `icy2_language` | `Icy-Language` | `en` |
| `icy2_country` | `Icy-Country` | `US` |
| `icy2_city` | `Icy-City` | `Miami, FL` |
| `icy2_is_public` | `Ice-Public` | `1` |

### DSP HTTP API Endpoints (Phase L4 additions)

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/api/v1/encoders/{slot}/dsp` | Get DSP config (preset, enabled flags, durations) |
| `PUT` | `/api/v1/encoders/{slot}/dsp` | Live-update DSP config without restarting |
| `POST` | `/api/v1/encoders/{slot}/dsp/eq/preset` | Apply a named EQ preset instantly |
| `GET` | `/api/v1/dnas/stats` | Proxy live stats from Mcaster1DNAS (`dnas.mcaster1.com:9443`) |

### Test Station — Mcaster1 Rock & Roll Yolo!

Config: `src/linux/config/mcaster1_rock_yolo.yaml`

Three encoder slots targeting `dnas.mcaster1.com:9443`:

| Slot | Mount | Genre | EQ Preset | Bitrate |
|------|-------|-------|-----------|---------|
| 1 | `/yolo-rock` | Classic Rock | `classic_rock` | 128 kbps |
| 2 | `/yolo-country` | Country | `country` | 128 kbps |
| 3 | `/yolo-modern` | Modern Rock | `modern_rock` | 192 kbps |

All slots include ICY2 social headers, AGC enabled, equal-power crossfade.

### Build

```bash
bash src/linux/make_phase4.sh
# Binary: build/mcaster1-encoder
```

---

## Phase L5 — Full PHP Frontend Overhaul + Logging `v1.4.0` — COMPLETE

**Goal:** Replace the single-page HTML SPA with a full PHP-driven multi-page web UI,
add production-grade logging, and wire all pages to the C++ API.

### New PHP Pages (all served at root level via FastCGI)

| Page | URL | Description |
|------|-----|-------------|
| `dashboard.php` | `/dashboard.php` | Live encoder status cards + Chart.js bandwidth graph |
| `encoders.php` | `/encoders.php` | Per-slot DSP controls, volume, metadata push |
| `media.php` | `/media.php` | Track library, folder scan, playlist file manager |
| `playlists.php` | `/playlists.php` | DB-backed playlist CRUD + load-to-slot |
| `metrics.php` | `/metrics.php` | Chart.js analytics: daily listeners, top tracks, sessions |
| `settings.php` | `/settings.php` | Server info, DNAS status, SSL cert, API token |

### New PHP API Files

| API | URL | Actions |
|-----|-----|---------|
| `app/api/auth.php` | `/app/api/auth.php` | login, logout, auto_login, whoami |
| `app/api/playlists.php` | `/app/api/playlists.php` | list, get_tracks, create, add_track, remove_track, load |
| `app/api/encoders.php` | `/app/api/encoders.php` | DB admin: add_user, delete_user, toggle_user |
| `app/api/tracks.php` | `/app/api/tracks.php` | Extended: + playlist_files action |

### New Include Files

| File | Purpose |
|------|---------|
| `app/inc/traits.db.class.php` | `Mc1Db` PDO trait — row(), rows(), run(), lastId() |
| `app/inc/header.php` | Full sidebar nav with MONITOR/LIBRARY/ANALYTICS/SYSTEM sections |
| `app/inc/footer.php` | Toasts, `mc1Api()`, `mc1Toast()`, auto PHP session bootstrap |
| `app/inc/logger.php` | PHP logging — mc1_log(), mc1_log_request(), mc1_api_respond() |

### C++ Route Changes (http_api.cpp)

```cpp
// 1. Block include files from direct HTTP access
svr.Get(R"(/app/inc/.*)", [](req, res){ res.status=403; ... });

// 2. Root-level PHP pages (new in L5)
svr.Get(R"(/([a-zA-Z0-9_\-]+\.php))",  handle_php_authed);
svr.Post(R"(/([a-zA-Z0-9_\-]+\.php))", handle_php_authed);

// 3. /dashboard now redirects to /dashboard.php
svr.Get("/dashboard", [](req, res){
    if (!request_is_authed(req)) { res.status=302; res.set_header("Location","/login"); return; }
    res.status=302; res.set_header("Location","/dashboard.php");
});
```

### Logging System (`mc1_logger.h`)

New C++ singleton logger with 5 verbosity levels:

```
Level 1 = CRITICAL  — fatal errors
Level 2 = ERROR     — non-fatal errors
Level 3 = WARN      — warnings
Level 4 = INFO      — normal events (default)
Level 5 = DEBUG     — verbose: raw data, request/response bodies, stack traces
```

Log files (all under `/var/log/mcaster1/`):
- `access.log` — Apache combined format (every HTTP request)
- `error.log` — Errors + startup messages
- `encoder.log` — Slot events (start/stop/track change)
- `api.log` — API request/response bodies (level 5 only)
- `php_error.log` — PHP application errors
- `php_access.log` — PHP page request log
- `php_fpm.log` — PHP-FPM pool log

Enable level 5 in YAML:
```yaml
http-admin:
  log-dir:   "/var/log/mcaster1"
  log-level: 5
```

Or via CLI: `--log-level 5` or `-v`

### Two-Layer Auth Bridge

- **C++ layer**: `mc1session` cookie, in-memory session map
- **PHP layer**: `mc1app_session` cookie, MySQL `user_sessions` table
- **Bridge**: `POST /app/api/auth.php {action:"auto_login"}` — called by `footer.php` on every page load using a `sessionStorage` guard. Transparently creates PHP session from valid C++ session.

### Build (same script, now with -rdynamic)

```bash
bash src/linux/make_phase4.sh
```

`-rdynamic` added to LDFLAGS for backtrace symbol resolution at level 5.

---

## Phase L6 — Analytics + Listener Metrics `v1.5.0` — PLANNED

- Extended `mcaster1_metrics` schema: hourly/weekly aggregates
- Listener geographic distribution (GeoIP)
- Per-track retention: did listeners stay through the track?
- Engagement score per track → Pulse AI feedback loop
- Export: CSV, JSON, RSS
- Web UI charts: Chart.js time-series, heatmaps, top-track leaderboard

---

## PHP-FPM Setup

### Pool Configuration

The encoder uses a dedicated PHP-FPM pool named `mcaster1`.

**Pool file:** `/etc/php/8.2/fpm/pool.d/mcaster1.conf`

```ini
[mcaster1]
user = mediacast1
group = www-data
listen = /run/php/php8.2-fpm-mc1.sock
listen.owner = mediacast1
listen.group = www-data
pm = dynamic
pm.max_children = 10
pm.start_servers = 2
pm.min_spare_servers = 1
pm.max_spare_servers = 5
error_log = /var/log/mcaster1/php_fpm.log

; Logging environment variables
env[MC1_LOG_DIR] = /var/log/mcaster1
env[MC1_LOG_LEVEL] = 4

; PHP settings
php_admin_value[error_reporting] = E_ALL
php_admin_flag[display_errors] = Off
php_admin_value[error_log] = /var/log/mcaster1/php_error.log
```

### Socket Path

The FastCGI socket is:
```
/run/php/php8.2-fpm-mc1.sock
```

The C++ `FastCgiClient` connects to this socket for all PHP page requests.

### Reload After Config Changes

```bash
sudo systemctl reload php8.2-fpm
```

### uopz Extension Warning

This server has the `uopz` extension active. It intercepts ALL `exit()` / `die()` calls.

**RULE: Never use `exit()` or `die()` in PHP files.**

```php
// WRONG — breaks with uopz:
if (!mc1_is_authed()) { http_response_code(401); die(); }

// CORRECT:
if (!mc1_is_authed()) {
    http_response_code(401);
    echo json_encode(['error' => 'Unauthorized']);
    return;
}
```

---

## Logging System

### C++ Logger (`mc1_logger.h`)

Thread-safe singleton logger with Apache combined format access log:

```cpp
#include "mc1_logger.h"

// Macros
MC1_INFO("encoder starting");
MC1_WARN("slot " + std::to_string(slot) + " not found");
MC1_ERR("connection failed: " + msg);
MC1_DBG("raw response: " + body);

// Encoder events
mc1log.encoder(slot_id, "START", "mount=/yolo-rock bitrate=128");
mc1log.encoder(slot_id, "TRACK", "title=Highway to Hell artist=AC/DC");

// Access log (called automatically via set_logger() callback)
mc1log.access(remote_addr, method, uri, status, bytes, duration_us, referer, ua);
```

### Log Level Configuration

Via YAML:
```yaml
http-admin:
  log-dir:   "/var/log/mcaster1"
  log-level: 4          # 1=CRIT 2=ERR 3=WARN 4=INFO 5=DEBUG
```

Via CLI:
```bash
./build/mcaster1-encoder --config ... --log-level 5
./build/mcaster1-encoder --config ... -v   # same as --log-level 5
```

### Log Files

| File | Location | Content |
|------|----------|---------|
| `access.log` | `/var/log/mcaster1/` | All HTTP requests (Apache combined format) |
| `error.log` | `/var/log/mcaster1/` | App errors + startup messages |
| `encoder.log` | `/var/log/mcaster1/` | Slot start/stop/track events |
| `api.log` | `/var/log/mcaster1/` | API req/resp bodies (level 5 only) |
| `php_error.log` | `/var/log/mcaster1/` | PHP application errors |
| `php_access.log` | `/var/log/mcaster1/` | PHP page request log |
| `php_fpm.log` | `/var/log/mcaster1/` | PHP-FPM pool log |

### Log Rotation

```
/etc/logrotate.d/mcaster1
```

- Daily rotation, 14-day retention, `compress`, `delaycompress`, `missingok`
- Postrotate: sends `SIGHUP` to encoder (reopen files) + reloads PHP-FPM

---

## Authentication System

### Layer 1: C++ In-Memory Auth

- **Cookie:** `mc1session=<64-byte-hex-token>` (HttpOnly, SameSite=Strict)
- **Token:** `X-API-Token: <token>` header (for scripts)
- **Storage:** `std::map<string, SessionEntry>` in memory
- **TTL:** configurable in YAML (`session_timeout_secs`)
- **Login:** `POST /api/v1/auth/login` → `{ username, password }`

### Layer 2: PHP/MySQL Auth

- **Cookie:** `mc1app_session=<token>` (HttpOnly, SameSite=Strict)
- **Storage:** `mcaster1_encoder.user_sessions` table
- **Check:** `mc1_current_user()` in `app/inc/user_auth.php`

### Auto-Bootstrap Bridge

`footer.php` calls `POST /app/api/auth.php {action:"auto_login"}` on every page load:

1. If C++ auth is valid but no PHP session → creates PHP session for first active admin user
2. Uses `sessionStorage.getItem('mc1php_ok')` as guard (reset each browser tab)
3. No password required — C++ already authenticated the user

```javascript
// footer.php auto-bootstrap (runs once per tab)
(function(){
  if (sessionStorage.getItem('mc1php_ok')) return;
  mc1Api('POST', '/app/api/auth.php', {action:'auto_login'}).then(function(d){
    if (d && d.ok) sessionStorage.setItem('mc1php_ok', '1');
  });
})();
```

### Credentials (Test Station)

| System | Username | Password | Notes |
|--------|----------|----------|-------|
| C++ admin | `djpulse` | `hackme` | In YAML config |
| MySQL DB | `djpulse` | `hackme` | bcrypt in `mcaster1_encoder.users` |
| Legacy | `admin` | `changeme` | Fallback admin |

---

## Build Instructions

### Prerequisites

```bash
sudo apt install \
  g++ pkg-config \
  libssl-dev \
  libportaudio2 portaudio19-dev \
  libmpg123-dev \
  libavformat-dev libavcodec-dev libavutil-dev libswresample-dev \
  libmp3lame-dev \
  libvorbis-dev libogg-dev \
  libopus-dev libopusenc-dev \
  libflac-dev \
  libtag1-dev \
  libyaml-dev \
  php8.2-fpm php8.2-mysql
```

### Vendor Headers (already in repo)

```
external/include/httplib.h         cpp-httplib v0.18+
external/include/nlohmann/json.hpp nlohmann/json v3.11+
```

### Full Build (Phase L4/L5)

```bash
cd /var/www/mcaster1.com/Mcaster1DSPEncoder
bash src/linux/make_phase4.sh
```

Output: `build/mcaster1-encoder`

Build time: ~30–45 seconds on a modern server.
Includes: `-rdynamic` (for backtrace symbol resolution at log level 5)

### HTTP-only Test Build (no audio deps)

```bash
bash src/linux/make_phase2.sh
```

This builds with `-DMC1_HTTP_TEST_BUILD` — audio pipeline is stubbed out.
Useful for fast iteration on the HTTP/PHP layer.

### Run (daemon mode)

```bash
nohup ./build/mcaster1-encoder \
  --config src/linux/config/mcaster1_rock_yolo.yaml \
  > /tmp/mc1enc.log 2>&1 & disown $!

echo "Started PID: $!"
tail -f /tmp/mc1enc.log
```

**Always use `disown $!`** — the shell sends SIGHUP on exit which stops the encoder.

### Kill + Restart

```bash
pkill -9 -f 'build/mcaster1-encoder'
sleep 2
nohup ./build/mcaster1-encoder \
  --config src/linux/config/mcaster1_rock_yolo.yaml \
  > /tmp/mc1enc.log 2>&1 & disown $!
```

**Warning:** cpp-httplib uses `SO_REUSEPORT`. If the old instance is still alive when you restart,
BOTH will bind to the same ports. Always kill all old instances first.

### Deploy Script

```bash
bash src/linux/deploy.sh
```

Builds, installs to `/opt/mcaster1/`, and restarts the systemd service.

---

## Configuration

The binary reads a YAML config file. Default search order:
1. `--config /path/to/config.yaml`
2. `./mcaster1.yaml`
3. `/etc/mcaster1/mcaster1.yaml`

### Minimal Config Example

```yaml
admin:
  username: admin
  password: changeme
  api_token: ""
  session_timeout_secs: 3600

sockets:
  - port: 8330
    bind_address: "0.0.0.0"
    ssl_enabled: false

webroot: ./web_ui

encoders:
  - slot_id: 1
    name: "Main Stream"
    input_type: playlist
    playlist_path: /media/music/rock.m3u
    codec: mp3
    bitrate_kbps: 128
    sample_rate: 44100
    channels: 2
    volume: 1.0
    shuffle: false
    repeat_all: true
    stream_target:
      host: icecast.example.com
      port: 8000
      mount: /live
      username: source
      password: hackme
      protocol: icecast2
    archive_enabled: false
```

### CLI Options

```
--http-port   PORT     HTTP listen port (default: 8080)
--https-port  PORT     HTTPS listen port (requires --ssl-cert + --ssl-key)
--bind        ADDR     Bind address (default: 127.0.0.1)
--ssl-cert    PATH     PEM certificate file
--ssl-key     PATH     PEM private key file
--ssl-gencert SUBJECT  Generate self-signed cert + key (e.g. /CN=myhost/O=MyOrg)
--config      PATH     YAML config file path
--log-level   N        Log verbosity 1-5 (1=CRIT 2=ERR 3=WARN 4=INFO 5=DEBUG)
--log-dir     PATH     Log directory (default: /var/log/mcaster1)
-v                     Verbose — same as --log-level 5
--help                 Show help
```

---

## API Reference

All API endpoints under `/api/v1/` require a valid session cookie (`mc1session=<token>`)
or `X-API-Token: <token>` header.

### Auth

```http
POST /api/v1/auth/login
Content-Type: application/json

{ "username": "admin", "password": "changeme" }

→ 200: { "ok": true, "redirect": "/dashboard" }
   Set-Cookie: mc1session=<token>; HttpOnly; SameSite=Strict

POST /api/v1/auth/logout
→ 302 → /login  (clears session cookie)
```

### Server Status

```http
GET /api/v1/status

→ {
    "version": "1.2.0",
    "platform": "linux",
    "uptime": "00:42:17",
    "encoders_connected": 1,
    "encoders_total": 2
  }
```

### Encoder Control

```http
GET /api/v1/encoders
→ [ { slot_id, state, is_live, bytes_sent, uptime_sec, track_title,
       track_artist, position_ms, duration_ms, track_index, track_count,
       volume, last_error }, ... ]

POST /api/v1/encoders/{slot}/start
→ { "ok": true }

POST /api/v1/encoders/{slot}/stop
→ { "ok": true }

POST /api/v1/encoders/{slot}/restart
→ { "ok": true }

GET /api/v1/encoders/{slot}/stats
→ { state, is_live, bytes_sent, uptime_sec, track_title, track_artist,
    position_ms, duration_ms, track_index, track_count, volume, last_error }
```

### Devices

```http
GET /api/v1/devices
→ [ { index, name, max_input_channels, default_sample_rate, is_default_input }, ... ]
```

### Metadata & Volume

```http
PUT /api/v1/metadata
{ "title": "Now Playing", "artist": "Artist Name", "slot": -1 }
→ { "ok": true, "title": "...", "artist": "..." }

PUT /api/v1/volume
{ "volume": 0.8, "slot": -1 }   (-1 = master volume)
→ { "ok": true, "volume": 0.8 }
```

### Playlist

```http
POST /api/v1/playlist/skip
{ "slot": 1 }
→ { "ok": true }

POST /api/v1/playlist/load
{ "slot": 1, "path": "/media/music/playlist.m3u" }
→ { "ok": true }
```

### PHP JSON APIs

All PHP APIs are POST-only, require authenticated session, return JSON.

```http
POST /app/api/tracks.php
{ "action": "list", "page": 1, "limit": 50 }
→ { "ok": true, "total": 1234, "pages": 25, "page": 1, "data": [...] }

{ "action": "search", "q": "beatles" }
{ "action": "add", "file_path": "/media/music/song.mp3", "title": "...", "artist": "..." }
{ "action": "delete", "id": 42 }
{ "action": "scan", "directory": "/media/music" }

POST /app/api/metrics.php
{ "action": "summary" }
→ { "ok": true, "data": { total_sessions, unique_ips, total_hours, avg_listen_min, total_bytes } }

{ "action": "daily_stats", "days": 30, "mount": "/live" }
{ "action": "sessions", "page": 1, "limit": 50, "mount": "/live" }
{ "action": "top_tracks", "limit": 20 }
```

---

## Database Schema

### `mcaster1_encoder` — User Auth + Roles

```sql
CREATE TABLE roles (
    id          INT AUTO_INCREMENT PRIMARY KEY,
    name        VARCHAR(50) UNIQUE NOT NULL,      -- 'admin', 'dj', 'podcaster', ...
    label       VARCHAR(100) NOT NULL,
    description TEXT,
    can_admin           TINYINT(1) DEFAULT 0,
    can_encode_control  TINYINT(1) DEFAULT 0,
    can_playlist        TINYINT(1) DEFAULT 0,
    can_metadata        TINYINT(1) DEFAULT 0,
    can_media_library   TINYINT(1) DEFAULT 0,
    can_metrics         TINYINT(1) DEFAULT 0,
    can_podcast         TINYINT(1) DEFAULT 0,
    can_schedule        TINYINT(1) DEFAULT 0,
    created_at  DATETIME DEFAULT NOW()
);

CREATE TABLE users (
    id            INT AUTO_INCREMENT PRIMARY KEY,
    username      VARCHAR(80) UNIQUE NOT NULL,
    email         VARCHAR(200),
    display_name  VARCHAR(200),
    password_hash VARCHAR(255) NOT NULL,    -- bcrypt $2y$12$...
    role_id       INT NOT NULL REFERENCES roles(id),
    is_active     TINYINT(1) DEFAULT 1,
    last_login    DATETIME,
    created_at    DATETIME DEFAULT NOW()
);

CREATE TABLE user_sessions (
    id          BIGINT AUTO_INCREMENT PRIMARY KEY,
    user_id     INT NOT NULL REFERENCES users(id),
    token_hash  CHAR(64) NOT NULL UNIQUE,   -- SHA-256 of cookie token
    ip_address  VARCHAR(45),
    user_agent  VARCHAR(500),
    created_at  DATETIME DEFAULT NOW(),
    expires_at  DATETIME NOT NULL
);
```

**Default Roles:** admin, dj, podcaster, social_dj, influencer, news_media

### `mcaster1_media` — Track Library

```sql
CREATE TABLE tracks (
    id              INT AUTO_INCREMENT PRIMARY KEY,
    file_path       VARCHAR(1000) UNIQUE NOT NULL,
    title           VARCHAR(500),
    artist          VARCHAR(300),
    album           VARCHAR(300),
    genre           VARCHAR(100),
    year            SMALLINT,
    duration_ms     INT,
    bitrate_kbps    SMALLINT,
    file_size_bytes BIGINT,
    play_count      INT DEFAULT 0,
    rating          TINYINT DEFAULT 3,
    is_missing      TINYINT(1) DEFAULT 0,
    last_played_at  DATETIME,
    date_added      DATETIME DEFAULT NOW()
);
```

### `mcaster1_metrics` — Listener Sessions

```sql
CREATE TABLE listener_sessions (
    id              BIGINT AUTO_INCREMENT PRIMARY KEY,
    client_ip       VARCHAR(45),
    user_agent      VARCHAR(500),
    stream_mount    VARCHAR(200),
    connected_at    DATETIME,
    disconnected_at DATETIME,
    duration_sec    INT,
    bytes_sent      BIGINT
);

CREATE TABLE daily_stats (
    id              INT AUTO_INCREMENT PRIMARY KEY,
    stat_date       DATE NOT NULL,
    mount           VARCHAR(200),
    peak_listeners  INT DEFAULT 0,
    total_sessions  INT DEFAULT 0,
    total_hours     DECIMAL(10,2) DEFAULT 0
);
```

---

## PHP App Layer

### Authentication Flow

1. C++ HTTP server authenticates the request (session cookie or API token)
2. Authenticated requests forwarded to php-fpm with `HTTP_X_MC1_AUTHENTICATED: 1`
3. PHP's `mc1_is_authed()` checks `$_SERVER['HTTP_X_MC1_AUTHENTICATED'] === '1'`
4. No passwords or tokens pass through to PHP — auth is enforced at the C++ layer

### MySQL-based User Auth (for future web login)

`inc/user_auth.php` provides MySQL session management independent of the C++ sessions:

```php
mc1_current_user()     → user row with role columns, or null
mc1_login($u, $p)      → [true, $user_row] or [false, 'error message']
mc1_logout()           → deletes session row, clears cookie
mc1_can('admin')       → bool — checks role column can_admin
mc1_require_role('dj') → JSON 403 if no permission
```

Session cookie: `mc1app_session` (HttpOnly, SameSite=Strict, 7-day TTL)
Token stored as SHA-256 hash in `user_sessions.token_hash`

---

## Default Credentials

### HTTP Admin Server (YAML / C++ sessions)

| Field | Value |
|-------|-------|
| Username | `admin` |
| Password | Set in `mcaster1.yaml` → `admin.password` |
| Default (make_phase3.sh) | `admin` / `hackme` |

**Change immediately in production.**

### MySQL Admin User (application credentials)

| Field | Value |
|-------|-------|
| MySQL user | `TheDataCasterMaster` |
| MySQL password | `#!3wrvNN57761` |
| Databases | `mcaster1_encoder`, `mcaster1_media`, `mcaster1_metrics` |

Defined in `web_ui/app/inc/db.php`. This file is **not committed** — only
`db.example.php` (with placeholder credentials) is committed.

### Default Web Admin User (MySQL `users` table)

| Field | Value |
|-------|-------|
| Username | `admin` |
| Password | `Admin1234!` |
| Role | `admin` (all permissions) |

---

## Deployment

### systemd Service

```ini
# /etc/systemd/system/mcaster1-encoder.service
[Unit]
Description=Mcaster1 DSP Encoder
After=network.target mysql.service php8.2-fpm.service

[Service]
Type=simple
User=mediacast1
Group=www-data
WorkingDirectory=/opt/mcaster1
ExecStart=/opt/mcaster1/bin/mcaster1-encoder \
    --config /opt/mcaster1/config/mcaster1.yaml
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now mcaster1-encoder
sudo systemctl status mcaster1-encoder
```

### nginx Reverse Proxy (optional)

```nginx
server {
    listen 443 ssl;
    server_name encoder.example.com;

    ssl_certificate     /etc/letsencrypt/live/encoder.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/encoder.example.com/privkey.pem;

    location / {
        proxy_pass http://127.0.0.1:8330;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
    }
}
```

### php-fpm Pool

```bash
# /etc/php/8.2/fpm/pool.d/mcaster1.conf
[mcaster1]
user = mediacast1
group = www-data
listen = /run/php/php8.2-fpm-mc1.sock
listen.owner = mediacast1
listen.group = www-data
listen.mode = 0660
pm = dynamic
pm.max_children = 5
pm.start_servers = 2
pm.min_spare_servers = 1
pm.max_spare_servers = 3
```

```bash
sudo systemctl restart php8.2-fpm
```

### File Permissions

```bash
sudo chown -R mediacast1:www-data /opt/mcaster1/web_ui
sudo chmod -R 750 /opt/mcaster1/web_ui
sudo chmod 660 /run/php/php8.2-fpm-mc1.sock
```

---

## Completed — Phase L5 Series (v1.4.0–v1.4.5)

### Phase L5 (v1.4.0)
- [x] `mc1_logger.h`: 5-level C++ singleton logger (access/error/encoder/api logs)
- [x] Autotools canonical build (`configure.ac`, `Makefile.am`, `autogen.sh`)
- [x] `install-deps.sh`: multi-distro dependency installer
- [x] `dashboard.php`: rAF 60fps progress bars + Chart.js bandwidth graph
- [x] `encoders.php`: per-slot DSP panel + live stats
- [x] PHP session auto-bootstrap bridge (footer.php → auth.php)
- [x] ICY metadata reconnect fix (`first_connect_done_` atomic + `meta_gen_` atomic)

### Phase L5.1 (v1.4.1)
- [x] `media.php` v2: split-pane folder tree + track library (3 tabs)
- [x] Track right-click menu: Edit Tags, Fetch Art, Add to Playlist, Add to Category, Delete
- [x] Edit Tags modal with full ID3 metadata + ffmpeg write
- [x] Category management: create, color-pick, bulk-add tracks
- [x] MusicBrainz album art auto-fetch
- [x] `app/api/tracks.php`: extended track CRUD + scan + folder browse + category actions
- [x] `app/inc/mc1_config.php`: `MC1_AUDIO_ROOT`, `MC1_PLAYLIST_DIR`, `MC1_ARTWORK_DIR`

### Phase L5.2 (v1.4.2)
- [x] `mediaplayer.php`: embedded browser audio player with DB-backed queue
- [x] `app/api/player.php`: queue CRUD (add, list, remove, clear)
- [x] `app/api/audio.php`: HTTP Range (206) audio streaming
- [x] `app/api/artwork.php`: cover art serving by hash
- [x] Drag-select multi-select in track table
- [x] `playlists.php`: DB-backed playlist CRUD + load-to-slot
- [x] `app/api/playlists.php`: full playlist API

### Phase L5.3 (v1.4.3)
- [x] Content-Range duplicate header fix (C++ httplib + PHP both setting it)
- [x] Firefox audio streaming 416 fix
- [x] 30-day session cookie TTL (was session-scoped)
- [x] Per-user session TTL override from `users.session_ttl_override`

### Phase L5.4 (v1.4.4)
- [x] `edit_encoders.php`: full 6-tab per-slot encoder config editor
- [x] `app/inc/edit.encoders.class.php`: EditEncoders static class
- [x] Playlist generator wizard in `playlists.php`: 4-step modal, 8 algorithms
- [x] `app/inc/playlist.builder.algorithm.class.php`: 8 generation algorithms
- [x] `app/inc/playlist.manager.pro.class.php`: orchestrator
- [x] SLEEP slot state: orange badge, Wake button, forced-Start button
- [x] `profile.php`: user profile page (display_name, email, password change)

### Phase L5.5 (v1.4.5) — 2026-02-24
- [x] `mediaplayerpro.php` (NEW): standalone WMP-style 3-column popup player
  - Left: library tree, Center: track browser, Right: queue
  - Transport bar: Play/Pause, Prev, Next, Shuffle, Repeat All, Volume
  - Resizable panes (CSS vars + localStorage persistence)
  - Album art with MusicBrainz auto-fetch
  - 13-item track right-click menu + queue right-click menu
  - Multi-select: Ctrl+click + floating action bar
  - Full Edit Tags modal (title, artist, album, genre, year, BPM, rating, weight)
  - Auto-advance: `audio.ended` → next track + queue cleanup
  - Self-contained: inlines mc1Api, mc1Toast, mc1State (no footer.php dependency)
- [x] `mediaplayer.php`: queue right-click menu, art auto-fetch, "Launch Standalone Player" button
- [x] `media.php` category system complete overhaul:
  - **Root cause fixed**: `json_encode()` in HTML attributes produces double-quoted strings (`"Rock"`)
    which break double-quoted HTML attributes → silent JS SyntaxError → all handlers broken.
    Fix: `h(json_encode($name))` in PHP; `esc(JSON.stringify(name))` in JS-generated HTML
  - `oncontextmenu` added to dynamically-created categories
  - `doAddToCategory`: live badge count increment + category track list refresh (no page reload)
  - `catCtxQueue` / `catCtxAddToPlaylist`: `per_page: 500` → `limit: 200`
  - Context menu dismissal: `click` → `mousedown` + `.contains()` guard (race condition fix)
  - Batch upload: `dir` param → `directory` param for `tracks.php?action=scan`

---

## Up Next — Phase L6 (v1.5.0)

**Streaming Server Relay Monitor** (SAM Broadcaster-style multi-server management):

- `streaming_servers` table in `mcaster1_encoder`
- `app/api/servers.php`: CRUD + live stat proxy for Icecast2, Shoutcast v1/v2, Mcaster1DNAS
- `settings.php` multi-server management panel with per-server listener/bitrate stats
- Color-coded status: Online/Offline/Unknown
- Client-side polling at 30s intervals
- Listener session reconciliation: detect new/departed IPs across polls

**Phase L7 (v1.6.0) — Metrics Dashboard:**
- Real-time listener count graph (Chart.js)
- Unique listeners / geographic breakdown / top tracks
- `metrics.php` full rewrite with System Health + Encoder Slots + Server Monitor tabs

**Phase L8 (v1.7.0) — System Health:**
- `/proc/stat` CPU, `/proc/meminfo` memory, `/proc/net/dev` network
- C++ background thread → `/api/v1/system/health` JSON endpoint
- Live gauges + history sparklines in metrics.php
