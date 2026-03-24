# PLANNING.md — Mcaster1DSPEncoder Future Roadmap

**Project:** Mcaster1DSPEncoder — Dual-platform broadcast DSP encoder
**Maintainer:** Dave St. John <davestj@gmail.com>
**Last Updated:** 2026-03-27

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
| L6 | v1.5.0 | Streaming Server Relay Monitor (multi-server stats) | COMPLETE |
| L7 | v1.5.1 | Listener Analytics + CSV export + GeoIP | COMPLETE |
| DJ-1..6 | v1.6.0 | 9-curve crossfader, effects rack, PTT, JACK, dual-deck, per-slot FX | COMPLETE |
| L8-SPLIT | v1.7.0 | Dual binary: admin + encoder (fault isolation) | COMPLETE |
| L9 | v1.7.0 | Clockwheel scheduler + dead air detection | COMPLETE |
| L-METRICS | v1.7.0 | System Health Dashboard (disk, codecs, FPM, SSL) | COMPLETE |
| L-MEDIA | v1.7.0 | Folder browser, scan progress, category types/weights | COMPLETE |
| **VT-1** | **v1.8.0** | **VoicTune daemon skeleton (HTTP, auth, FFT, pitch, meters, DB, WebSocket, USB hotplug, Ollama, coach)** | **COMPLETE** |

---

## v1.8.0 — VoicTune / Pedalboard / AI / Mixer (Active Development)

**Full plan:** `~/.claude/plans/quiet-juggling-stallman.md`

Four parallel tracks transforming Mcaster1 from broadcast encoder into full podcaster/broadcaster production studio.

### Architecture

```
PB-1 ──→ PB-2 ──→ PB-3 ────────────────────────┐
VT-1 ──→ VT-2 ──→ VT-3 ──→ VT-4 ───────────────┤
AI-1 ──→ AI-2 ──→ AI-3 ──→ AI-4 ────────────────┤
                                    MX-1 ──→ MX-2 ──→ MX-3
```

### Three Binaries

| Binary | Size | Ports | Purpose |
|--------|------|-------|---------|
| `mcaster1-dsp-encoder-admin` | 36MB | 8330/8344 | Web UI, FastCGI, auth, supervisor |
| `mcaster1-dsp-encoder` | 28MB | — | Audio pipeline, DSP, codecs, streaming |
| `mcaster1-voictune` | 18MB | 8350/8354/8355 | Voice analysis, coaching, AI |

### VoicTune Databases

| Database | Tables |
|----------|--------|
| `mcaster1_voictune` | sessions, voice_profiles, analysis_snapshots, ai_interactions |

### VoicTune Source Files (src/linux/voictune/)

| File | Purpose | Status |
|------|---------|--------|
| main_voictune.cpp | Entry point, CLI, signal handling, subsystem init | Real |
| vt_http_api.h/cpp | HTTP/HTTPS server, routes, session auth | Real (meters/spectrum stubs for VT-2) |
| vt_config.h/cpp | YAML config loader (libyaml) | Real |
| vt_logger.h | Logging singleton (voictune.log) | Real |
| vt_db.h/cpp | MariaDB client (mcaster1_voictune) | Real |
| vt_audio_capture.h/cpp | PortAudio mic capture wrapper | Real |
| vt_usb_monitor.h/cpp | USB/BT hotplug (inotify on /dev/snd/) | Real |
| vt_websocket.h/cpp | RFC 6455 WebSocket server (port 8355) | Real |
| vt_fft.h/cpp | FFT analysis (kiss_fft, spectral features) | Real |
| vt_meters.h/cpp | RMS, peak, LUFS metering | Real |
| vt_pitch.h/cpp | Pitch detection (autocorrelation, note mapping) | Real |
| vt_coach.h/cpp | Rule-based voice coaching engine | Real |
| vt_worker_pool.h/cpp | Thread pool for parallel FFT analysis | Real |
| ollama_client.h/cpp | Ollama REST API client | Real |
| ai_prompt_templates.h | System prompts for AI use cases | Real |
| vt_versions.h | Component version registry | Real |

### Phase Status

| Phase | Description | Status | Notes |
|-------|-------------|--------|-------|
| VT-1 | Daemon skeleton + all source files | **COMPLETE** | 17 files, 18MB binary, DB provisioned |
| VT-2 | Audio capture pipeline + live analysis | NEXT | Wire PortAudio→FFT→meters→pitch→coach loop |
| VT-3 | VoicTune web UI (oscilloscope, SA, pitch) | PLANNED | Canvas 2D visualizations |
| VT-4 | Voice coaching (rule-based + AI tips) | PLANNED | |
| PB-1 | Pedalboard infrastructure + SVG pedals | NEXT | Can parallel with VT-2 |
| PB-2 | Cable routing + signal flow | PLANNED | |
| PB-3 | Real-time meters + visual feedback | PLANNED | |
| AI-1 | Ollama AI endpoints (coaching, EQ, chain) | NEXT | Can parallel with VT-2 |
| AI-2 | NLP command parsing | PLANNED | |
| AI-3 | Content analysis + show notes | PLANNED | |
| AI-4 | Smart playlists + troubleshooting | PLANNED | |
| MX-1 | Virtual mixer console | PLANNED | After PB-2 + VT-2 |
| MX-2 | Mixer skins (6 Mcaster1-branded) | PLANNED | |
| MX-3 | Custom user effect profiles | PLANNED | |

### Key Decisions

- Graphics: Canvas 2D primary + WebGL 2.0 for mixer + CSS 3D for knobs. No Three.js.
- FFT: kiss_fft vendored (BSD-3, header-only). Needs kiss_fft_log.h stub.
- AI: Ollama client (cpp-httplib to localhost:11434), graceful degradation if offline
- Mixer skins: 6 Mcaster1-branded styles, NO actual brand names
- WebSocket: Raw RFC 6455 implementation (no external lib)
- USB hotplug: inotify on /dev/snd/ + 500ms settle delay + PortAudio re-enumeration

---

## Planned Phases (Post v1.8.0)

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

| Database | Current Tables | v1.8.0 Additions |
|----------|---------------|-----------------|
| `mcaster1_encoder` | users, roles, user_sessions, encoder_configs, streaming_servers | pedalboard_layouts, mixer_configs, mixer_custom_units |
| `mcaster1_media` | tracks, playlists, playlist_tracks, player_queue, cover_art, track_categories, categories, media_library_paths | — |
| `mcaster1_metrics` | listener_sessions, daily_stats | — |
| `mcaster1_voictune` | sessions, voice_profiles, analysis_snapshots, ai_interactions | (created in VT-1) |

---

## Architecture Decisions (Record These)

1. **No PHP→C++ loopback:** Browser JS calls `/api/v1/...` directly. PHP never curls back to C++ on same port — thread pool deadlock.
2. **No exit()/die() in PHP:** uopz extension intercepts them. Always use `return` inside functions.
3. **Audio served via `audio.php`:** HTTP Range requests with 206 Partial Content. C++ fixed Content-Range duplicate (httplib apply_ranges + PHP both sending header).
4. **Session storage:** C++ in-memory (`mc1session` cookie, 30-day TTL) + MySQL PHP sessions (`mc1app_session` cookie, TTL from `users.session_ttl_override`).
5. **Multi-server stat polling:** Client-side polling (JS `setInterval`) — no PHP cron, no background PHP process. Keeps the architecture simple.
6. **FastCGI audio streaming:** Full file passed through FastCGI memory before range slicing. Acceptable for preview (5-20MB MP3s). Not suitable for large FLAC archival streaming — use direct C++ audio route for that in L9.
7. **VoicTune is a separate daemon:** Independent binary on ports 8350/8354/8355. Own systemd unit, own YAML config, own database. Shares auth cookies with encoder admin for cross-daemon SSO.
8. **kiss_fft vendored:** BSD-3 header-only FFT library in `src/linux/external/include/`. Needs `kiss_fft_log.h` stub (created). No external FFT dependency.
9. **WebSocket raw implementation:** No external lib — RFC 6455 handshake + frame parsing in `vt_websocket.cpp` using OpenSSL for SHA1/Base64.
10. **USB hotplug via inotify:** Monitor `/dev/snd/` for ALSA device nodes. 500ms settle delay before PortAudio re-enumeration. BT via PulseAudio subscription (optional, `HAVE_PULSE`).
