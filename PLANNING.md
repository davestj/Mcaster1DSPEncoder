# PLANNING.md — Mcaster1DSPEncoder Future Roadmap

**Project:** Mcaster1DSPEncoder — Dual-platform broadcast DSP encoder
**Maintainer:** Dave St. John <davestj@gmail.com>
**Last Updated:** 2026-02-24

This document captures all planned future phases. Reference it in CLAUDE.md for context.

---

## Completed Phases

| Phase | Version | Description | Status |
|-------|---------|-------------|--------|
| L1 | v1.0.0 | Infrastructure, build system, platform abstraction | COMPLETE |
| L2 | v1.1.1 | HTTP/HTTPS admin server + login + web UI shell | COMPLETE |
| L3 | v1.2.0 | Audio encoding + streaming + FastCGI + PHP app layer | COMPLETE |
| L4 | v1.3.0 | DSP chain (EQ/AGC/crossfade) + ICY2 + DNAS stats proxy | COMPLETE |
| L5 | v1.4.0 | PHP frontend overhaul + logging + autotools + rAF progress | COMPLETE |
| L5.1 | v1.4.1 | Media library (folder browser, track tags, artwork, playlists) | COMPLETE |
| L5.2 | v1.4.2 | Browser media player (queue, audio streaming, drag-select) | COMPLETE |
| L5.3 | v1.4.3 | Session fix (30-day cookie), Content-Range fix for Firefox | COMPLETE |
| L5.4 | v1.4.4 | User profile page, broadcast roles, per-user session TTL | COMPLETE |
| L5.5 | v1.4.5 | Standalone popup player (mediaplayerpro.php), category UX overhaul, HTML attr quoting bug sweep | COMPLETE |

---

## Planned Phases

---

### Phase L6 — Streaming Server Relay Monitor (v1.5.0)
**SAM Broadcaster-style stat relay — priority: HIGH**

The Settings → DNAS Connection panel becomes a full multi-server management UI.
Operators add the streaming servers they send audio to — Icecast2, Shoutcast v1/v2,
Steamcast, or Mcaster1DNAS — with per-mount-point credentials for stats polling.

**Features:**
- `streaming_servers` table in `mcaster1_encoder` DB
- Per-server: name, type, host, port, mount point, stat credentials, SSL flag
- Server types:
  - **Icecast2** — poll `/admin/stats.xml` with admin:password, parse XML
  - **Shoutcast v1** — poll `/admin.cgi?action=stats&pass=` XML or `/7.html`
  - **Shoutcast v2** — poll `/admin/stats` XML with admin/password
  - **Steamcast** — poll `/admin/stats.xml` (Icecast-compatible format)
  - **Mcaster1DNAS** — poll `/admin/mcaster1stats` with JSON + XML hybrid
- Live stats widget per server: listeners, max listeners, bitrate, stream title, uptime
- Stats polling: client-side `setInterval` at 30s, no PHP cron needed
- Color-coded badge: Online (green), Offline (red), Unknown (gray)
- Assign encoder slot → server mount (slot 1 → /yolo-rock on server A)

**Files:**
- `settings.php` — multi-server panel replaces current single DNAS card
- `app/api/servers.php` — CRUD for `streaming_servers` + live stat proxy
- `streaming_servers` table (created in L5.4 phase DB migration)

---

### Phase L7 — Listener Analytics & Metrics Dashboard (v1.6.0)
**Full metrics overhaul — priority: HIGH**

Replace the placeholder `metrics.php` with a real-time analytics dashboard.

**Features:**
- `listener_sessions` table in `mcaster1_metrics` DB (already exists)
- Real-time listener count graph (Chart.js, 5-second polling)
- Unique listeners today / this week / all-time
- Geographic breakdown (GeoIP from MaxMind free DB)
- Browser / client breakdown (parse User-Agent)
- Top 10 tracks played (join metrics ↔ tracks)
- Stream health: bitrate stability, reconnect events, buffer underruns
- Export: CSV export of listener sessions, daily stats
- Date range picker for historical reports

**Files:**
- `metrics.php` — full rewrite with Chart.js dashboards
- `app/api/metrics.php` — enhanced with geographic, track, and session queries
- `app/inc/geoip.php` — MaxMind GeoLite2 IP lookup helper

---

### Phase L8 — System Health Monitoring (v1.7.0)
**Server resource monitoring — priority: MEDIUM**

Add a System Health tab to the dashboard (or dedicated `health.php` page).

**Features:**
- **CPU:** `/proc/loadavg` + `/proc/stat` polling → Chart.js sparklines
- **Memory:** `/proc/meminfo` → used/free/cached bars
- **Network:** `/proc/net/dev` → bytes in/out per interface, bandwidth gauge
- **Disk:** `disk_free_space()` for audio root, archive dir, log dir
- **Process:** encoder PID status, uptime, thread count
- **FFmpeg/codec:** detect installed codecs (lame, opus, flac, vorbis, fdkaac)
- **PHP-FPM:** `/run/php/php8.2-fpm-mc1.sock` status
- **SSL cert expiry:** days-until-expiry badge with warning at <30 days
- Refresh every 10 seconds with rAF animation for gauges

**Files:**
- `app/api/health.php` — reads `/proc/*` and `disk_free_space()`, returns JSON
- `health.php` or new "System" tab in `settings.php`

---

### Phase L9 — Advanced Automation & Scheduling (v1.8.0)
**Clockwheel scheduling, smart playlists, rotation rules — priority: MEDIUM**

**Features:**
- **Clockwheel:** Hour-by-hour rotation clock (SAM Broadcaster `clock_hours` table)
  - Visual drag-and-drop hour editor (24 slots, each slot = category or playlist)
  - Categories: Jingle, Sweeper, Spot, Music, Station ID, News
  - Rotation rules: play N songs from genre X, then 1 jingle, repeat
- **Smart Playlists:** Rule-based queries (BPM range, energy level, year range, genre, mood tag)
  - SQL query builder UI for playlist rules
  - Preview: shows first 10 tracks matching rules
- **AI Rotation:** (future) acoustID fingerprinting via `fpcalc`, auto-detect BPM/key
- **Auto DJ:** Start the encoder automatically at scheduled time
  - `cron` or internal scheduler in C++ using steady_clock
- **Dead Air Detection:** Monitor silence longer than N seconds → skip track or play fallback

**Files:**
- `app/api/schedule.php` — clockwheel CRUD
- `schedule.php` — visual clockwheel editor
- C++ additions: internal scheduler thread, silence detector in DSP chain

---

### Phase L10 — Podcast & Archive Management (v1.9.0)
**Podcast RSS generation, recording archives — priority: LOW**

**Features:**
- **Podcast Recording:** Enable archive writer per slot (WAV + MP3 simultaneously)
  - Auto-split at configurable interval (e.g., 1 hour)
  - Metadata: episode title, description, cover art
- **Podcast RSS:** Generate iTunes-compatible RSS feed from archived episodes
  - `podcast_episodes` table: title, description, duration, file_path, published_at
  - RSS served at `/podcast/{slot}/feed.xml`
- **Archive Browser:** Web UI to browse, play, download, and publish archived recordings
- **Auto-Publish:** Optionally push to external podcast hosts (Anchor, Buzzsprout, etc.)

**Files:**
- `app/api/podcast.php` — episode CRUD + RSS generation
- `podcast.php` — archive browser UI

---

### Phase L11 — User Engagement & Social Integration (v2.0.0)
**Listener engagement, request system, social broadcast — priority: LOW**

**Features:**
- **Song Request System:** Listeners submit requests via web widget; DJ sees request queue
- **Shoutout / Dedication System:** Listener sends dedications shown in stream title
- **Discord/Slack Integration:** Emit now-playing events to webhook
- **Twitter/X Integration:** Auto-tweet now-playing when track changes
- **Listener Chat:** WebSocket-based live chat during broadcast
- **Station Website Widget:** Embeddable player + now-playing widget for external sites

---

## Database Growth Plan

| Database | Current Tables | Phase L6 Additions | Phase L7 Additions |
|----------|---------------|-------------------|-------------------|
| `mcaster1_encoder` | users, roles, user_sessions, encoder_configs, streaming_servers | — | — |
| `mcaster1_media` | tracks, playlists, playlist_tracks, player_queue, cover_art, track_categories, categories, media_library_paths | clock_hours, rotation_rules | — |
| `mcaster1_metrics` | listener_sessions, daily_stats | server_stat_history | geographic_stats, track_plays |

---

## Architecture Decisions (Record These)

1. **No PHP→C++ loopback:** Browser JS calls `/api/v1/...` directly. PHP never curls back to C++ on same port — thread pool deadlock.
2. **No exit()/die() in PHP:** uopz extension intercepts them. Always use `return` inside functions.
3. **Audio served via `audio.php`:** HTTP Range requests with 206 Partial Content. C++ fixed Content-Range duplicate (httplib apply_ranges + PHP both sending header).
4. **Session storage:** C++ in-memory (`mc1session` cookie, 30-day TTL) + MySQL PHP sessions (`mc1app_session` cookie, TTL from `users.session_ttl_override`).
5. **Multi-server stat polling:** Client-side polling (JS `setInterval`) — no PHP cron, no background PHP process. Keeps the architecture simple.
6. **FastCGI audio streaming:** Full file passed through FastCGI memory before range slicing. Acceptable for preview (5-20MB MP3s). Not suitable for large FLAC archival streaming — use direct C++ audio route for that in L9.
