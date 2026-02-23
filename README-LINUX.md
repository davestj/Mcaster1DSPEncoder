# Mcaster1DSPEncoder — Linux Build Reference

**Maintainer:** Dave St. John <davestj@gmail.com>
**Binary:** `mcaster1-encoder`
**Version:** v1.3.0 (Phase L4 — DSP Chain + ICY2 + DNAS Stats)
**Platform:** Debian 12 / Ubuntu 22.04+ (amd64)

---

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Phase Status](#phase-status)
4. [Phase L1 — Infrastructure](#phase-l1--infrastructure-v100--complete)
5. [Phase L2 — HTTP Admin Server](#phase-l2--httphttps-admin-server-v111--complete)
6. [Phase L3 — Audio Engine](#phase-l3--audio-encoding--streaming-engine-v120--complete)
7. [Phase L4 — DSP + Media Library + AI](#phase-l4--dsp-chain--media-library--ai-playlist-v130--planned)
8. [Phase L5 — DNAS Integration](#phase-l5--mcaster1dnas-integration-v140--planned)
9. [Phase L6 — Analytics](#phase-l6--analytics--listener-metrics-v150--planned)
10. [Build Instructions](#build-instructions)
11. [Configuration](#configuration)
12. [API Reference](#api-reference)
13. [Database Schema](#database-schema)
14. [PHP App Layer](#php-app-layer)
15. [Default Credentials](#default-credentials)
16. [Deployment](#deployment)

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
| **L4** | **v1.3.0** | **DSP chain (EQ/AGC/crossfade) + ICY2 headers + DNAS stats proxy** | **COMPLETE** |
| L5 | v1.4.0 | Mcaster1DNAS deep integration + live dashboard | PLANNED |
| L6 | v1.5.0 | Analytics, listener metrics, engagement platform | PLANNED |

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

#### PHP App Files

```
web_ui/app/
  inc/
    auth.php           mc1_is_authed() — checks HTTP_X_MC1_AUTHENTICATED header
    db.php             mc1_db($dbname) — PDO connection cache + helpers (h(), mc1_format_duration())
    header.php         Page header with sidebar nav (expects $page_title, $active_nav)
    footer.php         Page footer + toast helper + data-confirm handlers
    user_auth.php      MySQL session-based auth (mc1_current_user, mc1_login, mc1_logout)
  api/
    tracks.php         POST /app/api/tracks.php — actions: list, search, add, delete, scan
    metrics.php        POST /app/api/metrics.php — actions: summary, daily_stats, sessions, top_tracks
  media.php            Media library browser (paginated track list with search)
  metrics.php          Listener metrics dashboard (stat cards + tables)
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

## Phase L5 — Mcaster1DNAS Integration `v1.4.0` — PLANNED

- Connect to Mcaster1DNAS stats API for real-time listener counts
- ICY 2.2 listener fingerprint / retention signals
- Multi-mount simulcast coordination
- DNAS-authenticated SOURCE with ICY 2.2 extended headers
- Live dashboard with WebSocket listener count updates

---

## Phase L6 — Analytics + Listener Metrics `v1.5.0` — PLANNED

- Extended `mcaster1_metrics` schema: hourly/weekly aggregates
- Listener geographic distribution (GeoIP)
- Per-track retention: did listeners stay through the track?
- Engagement score per track → Pulse AI feedback loop
- Export: CSV, JSON, RSS
- Web UI charts: Chart.js time-series, heatmaps, top-track leaderboard

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

### Full Phase 3 Build

```bash
cd /path/to/Mcaster1DSPEncoder
bash src/linux/make_phase3.sh
```

Output: `build/mcaster1-encoder`

Build time: ~30 seconds on a modern server.

### HTTP-only Test Build (no audio deps)

```bash
bash src/linux/make_phase2.sh
```

This builds with `-DMC1_HTTP_TEST_BUILD` — audio pipeline is stubbed out.

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
--http-port  PORT     HTTP listen port (default: 8080)
--https-port PORT     HTTPS listen port (requires --ssl-cert + --ssl-key)
--bind       ADDR     Bind address (default: 127.0.0.1)
--ssl-cert   PATH     PEM certificate file
--ssl-key    PATH     PEM private key file
--ssl-gencert SUBJECT Generate self-signed cert + key (e.g. /CN=myhost/O=MyOrg)
--config     PATH     YAML config file path
--help                Show help
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

## Completed (Phase L4 — v1.3.0)

- [x] DSP chain: `dsp/eq.cpp`, `dsp/agc.cpp`, `dsp/crossfader.cpp`, `dsp/dsp_chain.cpp`
- [x] EQ presets: flat, classic_rock, country, modern_rock, broadcast, spoken_word
- [x] DSP wired into `EncoderSlot::on_audio()` — EQ → AGC per buffer
- [x] Equal-power crossfader triggered at track boundaries
- [x] Live DSP reconfigure API: `PUT /api/v1/encoders/{slot}/dsp`
- [x] EQ preset API: `POST /api/v1/encoders/{slot}/dsp/eq/preset`
- [x] ICY2 extended headers in `StreamTarget` (Twitter/Facebook/Instagram/email/language/country/city)
- [x] DNAS stats proxy: `GET /api/v1/dnas/stats` → `dnas.mcaster1.com:9443`
- [x] Test station config: `config/mcaster1_rock_yolo.yaml` (3 slots, ICY2 headers, DSP presets)
- [x] Build script: `make_phase4.sh`

## Up Next (Phase L5)

- [ ] Mcaster1DNAS deep integration: live listener count via `/admin/mcaster1stats` polling
- [ ] WebSocket push: real-time listener count updates to dashboard
- [ ] Multi-mount simulcast coordination across slots
- [ ] Media library manager: `medialib/scanner.h/cpp`, `medialib/database.h/cpp`
- [ ] BPM + key detection: aubio integration
- [ ] Pulse AI playlist: `pulse/context_engine.h/cpp`, `pulse/track_scorer.h/cpp`
- [ ] LLM integration: Ollama + Claude API client
- [ ] Podcast RSS generation: `archive_writer.cpp` → `podcast_rss_gen()`
- [ ] Web UI: EQ graph, crossfade waveform, media library browser, Pulse dashboard
