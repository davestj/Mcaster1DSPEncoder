# PLANNING.md — Mcaster1DSPEncoder Future Roadmap

**Project:** Mcaster1DSPEncoder — Dual-platform broadcast DSP encoder
**Maintainer:** Dave St. John <davestj@gmail.com>
**Last Updated:** 2026-03-27

This document captures all completed and planned phases. Reference it in CLAUDE.md for context.

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
| L5.5 | v1.4.5 | Standalone popup player, category UX overhaul, HTML attr quoting | COMPLETE |
| L6 | v1.5.0 | Streaming Server Relay Monitor (multi-server stats) | COMPLETE |
| L7 | v1.5.1 | Listener Analytics + CSV export + GeoIP | COMPLETE |
| DJ-1..6 | v1.6.0 | 9-curve crossfader, effects rack, PTT, JACK, dual-deck, per-slot FX | COMPLETE |
| L8-SPLIT | v1.7.0 | Dual binary: admin + encoder (fault isolation) | COMPLETE |
| L9 | v1.7.0 | Clockwheel scheduler + dead air detection | COMPLETE |
| L-METRICS | v1.7.0 | System Health Dashboard (disk, codecs, FPM, SSL) | COMPLETE |
| L-MEDIA | v1.7.0 | Folder browser, scan progress, category types/weights | COMPLETE |
| VT-1 | v1.8.0 | VoicTune daemon skeleton (HTTP, auth, FFT, pitch, meters, DB, WS, USB, Ollama) | COMPLETE |
| VT-2 | v1.8.0 | VoicTune audio capture pipeline + live analysis | COMPLETE |
| VT-3 | v1.8.0 | VoicTune web UI (oscilloscope, SA, pitch, meters) | COMPLETE |
| VT-4 | v1.8.0 | Voice coaching (rule-based + AI tips in browser) | COMPLETE |
| PB-1 | v1.8.0 | Pedalboard infrastructure + SVG broadcast pedals + drag/drop | COMPLETE |
| PB-2 | v1.8.0 | Cable routing + signal flow visualization (bezier SVG) | COMPLETE |
| PB-3 | v1.8.0 | Real-time meters + visual feedback on pedals | COMPLETE |
| AI-1 | v1.8.0 | Ollama AI coaching chat + EQ/chain suggestion endpoints | COMPLETE |
| AI-2 | v1.8.0 | NLP command parsing (natural language to API actions) | COMPLETE |
| AI-3 | v1.8.0 | Content analysis + show notes generation | COMPLETE |
| AI-4 | v1.8.0 | Smart playlist generation + audio troubleshooting | COMPLETE |
| MX-1 | v1.8.0 | Virtual mixer console (channel strips, faders, meters) | COMPLETE |
| MX-2 | v1.8.0 | Mixer skins (6 Mcaster1-branded styles) | COMPLETE |
| MX-3 | v1.8.0 | Custom user effect profiles + mixer presets | COMPLETE |
| L10 | v1.8.0 | Podcast archive management + iTunes RSS feed generation | COMPLETE |
| L11 | v1.8.0 | Song requests, dedications, webhooks, embeddable widget | COMPLETE |
| PC-1 | v1.8.0 | Recording studio (one-click record, chapter markers, auto-split) | COMPLETE |
| PC-2 | v1.8.0 | Episode editor (waveform, EDL, cut/trim/fade/normalize, export) | COMPLETE |
| PC-3 | v1.8.0 | Multi-platform publishing (RSS, Apple, Spotify, YouTube, etc.) | COMPLETE |
| PC-4 | v1.8.0 | Podcast analytics dashboard (downloads, retention, geo) | COMPLETE |
| PC-5 | v1.8.0 | Podcast website generator (landing pages, SEO, themes) | COMPLETE |
| PC-6 | v1.8.0 | AI podcast tools (transcription, show notes, chapter suggestions) | COMPLETE |
| PC-7 | v1.8.0 | Remote recording (WebRTC guests, per-track, chat, invite URL) | COMPLETE |
| VP-1 | v1.9.0 | DSP Producer daemon (video capture, RTMP streaming) | COMPLETE |
| VP-2 | v1.9.0 | Video switcher + multicam + overlays | COMPLETE |
| VP-3 | v1.9.0 | Vodcast support (video + audio encoding) | COMPLETE |
| DAW-1 | v1.9.0 | Multi-track DAW (timeline, clip editing, automation) | COMPLETE |
| DAW-2 | v1.9.0 | DAW effects chain + mixing + export | COMPLETE |
| DAW-3 | v1.9.0 | DAW noise reduction + freeze tracks | COMPLETE |
| FA-1 | v1.9.0 | Forensic audio (HQ spectrogram, 65536 FFT, WSOLA) | COMPLETE |
| FA-2 | v1.9.0 | Forensic compare, peak detection, reports, AI analysis | COMPLETE |
| FA-3 | v1.9.0 | Goniometer + noise subtraction + EBU R128 compliance | COMPLETE |
| CC-1 | v1.9.0 | Closed captions (Whisper, SRT/VTT, live, burn-in, RSS) | COMPLETE |
| MON-1 | v1.9.0 | Monetization (DAI, ad campaigns, CPM, impressions, sponsors) | COMPLETE |
| NAV-1 | v1.9.0 | Mode-based navigation (5 modes), daemon health monitor | COMPLETE |
| MOBILE-1 | v1.9.0 | Mobile responsive UI, media picker component | COMPLETE |
| VIZ-1 | v1.9.0 | WebGL visualizations (spectrogram, 3D spectrum, globe, knobs, faders, cables) | COMPLETE |

---

## v2.0.0 — CURRENT RELEASE (2026-03-27)

All 40+ phases complete. Mcaster1 is a full podcaster/broadcaster/video production studio.

### Four Binaries

| Binary | Size | Ports | Purpose |
|--------|------|-------|---------|
| `mcaster1-dsp-encoder-admin` | 46MB | 8330/8344 | Web UI, FastCGI, auth, supervisor |
| `mcaster1-dsp-encoder` | 29MB | --- | Audio pipeline, DSP, codecs, streaming |
| `mcaster1-voictune` | 21MB | 8350/8354/8355 | Voice analysis, coaching, AI |
| `mcaster1-producer` | 9.1MB | 8360/8364 | Video encoding, multi-track mixdown, forensic FFT |

### Key Decisions

- Graphics: Canvas 2D primary + WebGL 2.0 for mixer/spectrogram + CSS 3D for knobs. No Three.js.
- FFT: kiss_fft vendored (BSD-3, header-only)
- AI: Ollama client (cpp-httplib to localhost:11434), graceful degradation if offline
- Mixer skins: 6 Mcaster1-branded styles, NO actual brand names
- WebSocket: Raw RFC 6455 implementation (no external lib)
- USB hotplug: inotify on /dev/snd/ + 500ms settle delay + PortAudio re-enumeration
- Captions: Whisper via Ollama or external API, SRT/VTT output
- Video: FFmpeg subprocess for encode/transcode, RTMP push via librtmp
- DAW: Browser-based Canvas 2D timeline, server-side FFmpeg for export
- Mode navigation: 5 UI modes (Broadcast, Podcast, Producer, Forensic, DAW) via localStorage

---

## v2.1.0 — Planned

### Potential Features
- macOS native build (CoreAudio backend via PortAudio)
- Plugin SDK for third-party DSP effects
- Multi-user collaborative editing (concurrent web sessions)
- Advanced VoicTune: real-time vocal effects (de-breath, auto-tune, voice changer)
- Multi-language UI localization (i18n)
- Streaming to additional platforms (Twitch, YouTube Live, Facebook Live)

---

## Database Growth Plan

| Database | Tables |
|----------|--------|
| `mcaster1_encoder` | users, roles, user_sessions, encoder_configs, streaming_servers, pedalboard_layouts, mixer_configs, mixer_custom_units, webhook_configs, ad_campaigns, ad_schedule, ad_impressions, sponsor_configs |
| `mcaster1_media` | tracks, playlists, playlist_tracks, player_queue, cover_art, track_categories, categories, media_library_paths, podcast_shows, podcast_episodes, episode_markers, publish_targets, publish_queue, podcast_downloads, song_requests, dedications, remote_sessions, remote_participants, remote_chat, daw_projects, daw_tracks, daw_clips, daw_automation, captions, caption_segments |
| `mcaster1_metrics` | listener_sessions, daily_stats, ad_impressions_log |
| `mcaster1_voictune` | sessions, voice_profiles, analysis_snapshots, ai_interactions |
| `mcaster1_producer` | jobs, job_results, forensic_reports |

---

## Architecture Decisions (Record These)

1. **No PHP-to-C++ loopback:** Browser JS calls `/api/v1/...` directly. PHP never curls back to C++ on same port — thread pool deadlock.
2. **No exit()/die() in PHP:** uopz extension intercepts them. Always use `return` inside functions.
3. **Audio served via `audio.php`:** HTTP Range requests with 206 Partial Content.
4. **Session storage:** C++ in-memory (`mc1session` cookie, 30-day TTL) + MySQL PHP sessions (`mc1app_session` cookie).
5. **Multi-server stat polling:** Client-side polling (JS `setInterval`) — no PHP cron, no background PHP process.
6. **VoicTune is a separate daemon:** Independent binary on ports 8350/8354/8355. Own systemd unit, YAML config, database. Shares auth cookies with encoder admin for cross-daemon SSO.
7. **kiss_fft vendored:** BSD-3 header-only FFT library in `src/linux/external/include/`.
8. **WebSocket raw implementation:** No external lib — RFC 6455 in `vt_websocket.cpp`.
9. **USB hotplug via inotify:** Monitor `/dev/snd/` for ALSA device nodes. 500ms settle delay before PortAudio re-enumeration.
10. **Cookie SameSite=Lax:** Not Strict — Strict breaks cross-port navigation (8330 to 8344).
11. **Conditional Secure flag:** Only set on HTTPS connections to avoid cookie drops on HTTP port.
12. **cpp-httplib route limit:** Use individual route registrations, not catch-all dispatchers.
13. **Producer is a separate daemon:** Independent binary on ports 8360/8364. Video/audio/FFT worker threads offloaded from admin.
14. **Mode-based navigation:** 5 UI modes filter sidebar visibility. State persisted in localStorage.
15. **Captions via Whisper:** Ollama or external Whisper API, SRT/VTT output, optional burn-in via FFmpeg.
16. **DAW is browser-only timeline:** Canvas 2D timeline rendering, server-side FFmpeg for final export/mixdown.
17. **Forensic spectrogram:** WebGL HQ spectrogram with up to 65536-point FFT, WSOLA time stretch.
