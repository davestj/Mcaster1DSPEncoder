# Mcaster1DSPEncoder — Development Roadmap

**Version:** 1.8.0-beta.1
**Last Updated:** 2026-03-27

---

## Current Release: v1.8.0-beta.1 — ALL PHASES COMPLETE

v1.8.0-beta.1 is a major release transforming Mcaster1 from a broadcast encoder into a full podcaster/broadcaster production studio. All 23+ development phases are complete.

---

## Completed Phases

### v1.0.0 - v1.7.0 (Foundation)

| Phase | Description | Status |
|-------|-------------|--------|
| L1 | Infrastructure, build system, platform abstraction | **COMPLETE** |
| L2 | HTTP/HTTPS admin server + login + web UI shell | **COMPLETE** |
| L3 | Audio encoding + streaming + FastCGI + PHP | **COMPLETE** |
| L4 | DSP chain (EQ/AGC/crossfade) + ICY2 + DNAS stats | **COMPLETE** |
| L5-L5.5 | PHP frontend, media library, player, profiles, categories | **COMPLETE** |
| L6 | Streaming Server Relay Monitor | **COMPLETE** |
| L7 | Listener Analytics + CSV export + GeoIP | **COMPLETE** |
| DJ-1..6 | Crossfader, effects rack, PTT, JACK, dual-deck, per-slot FX | **COMPLETE** |
| L8-SPLIT | Dual binary: admin + encoder (fault isolation) | **COMPLETE** |
| L9 | Clockwheel scheduler + dead air detection | **COMPLETE** |
| L-METRICS | System Health Dashboard | **COMPLETE** |
| L-MEDIA | Folder browser, scan progress, category types/weights | **COMPLETE** |

### v1.8.0 — Production Studio

| Phase | Description | Status |
|-------|-------------|--------|
| VT-1..VT-4 | VoicTune daemon (FFT, pitch, meters, coaching, web UI) | **COMPLETE** |
| PB-1..PB-3 | Visual pedalboard (SVG pedals, cable routing, live meters) | **COMPLETE** |
| AI-1..AI-4 | Ollama AI (coaching, EQ/chain, NLP, content analysis, smart playlists) | **COMPLETE** |
| MX-1..MX-3 | Virtual mixer console (channel strips, 6 skins, custom presets) | **COMPLETE** |
| L10 | Podcast archive management + iTunes RSS feed generation | **COMPLETE** |
| L11 | Song requests, dedications, webhooks, embeddable widget | **COMPLETE** |
| PC-1 | Recording studio (one-click record, chapter markers, auto-split) | **COMPLETE** |
| PC-2 | Episode editor (waveform, EDL, cut/trim/fade/normalize, export) | **COMPLETE** |
| PC-3 | Multi-platform publishing (RSS, Apple, Spotify, YouTube, etc.) | **COMPLETE** |
| PC-4 | Podcast analytics dashboard (downloads, retention, geo) | **COMPLETE** |
| PC-5 | Podcast website generator (landing pages, SEO, themes) | **COMPLETE** |
| PC-6 | AI podcast tools (transcription, show notes, chapter suggestions) | **COMPLETE** |
| PC-7 | Remote recording (WebRTC guests, per-track, chat, invite URL) | **COMPLETE** |

---

## Future Releases

### v1.9.0 — Platform Expansion (Planned)

| Feature | Description | Priority |
|---------|-------------|----------|
| macOS native build | CoreAudio backend via PortAudio | Medium |
| Mobile-responsive UI | Web UI overhaul for tablet/phone | Medium |
| WebRTC browser streaming | Direct browser-to-encoder audio | Low |
| Plugin SDK | Third-party DSP effect plugins | Low |
| Multi-user collaboration | Concurrent web session editing | Low |
| Loudness compliance | EBU R128 / ATSC A/85 auto-correction | Medium |
| Advanced VoicTune | De-breath, auto-tune, voice changer | Low |
| Podcast monetization | Dynamic ad insertion, sponsor management | Low |
| i18n localization | Multi-language web UI | Low |
| Multi-platform streaming | Twitch, YouTube Live, Facebook Live | Medium |

---

## Technology Decisions

| Component | Technology | Rationale |
|-----------|-----------|-----------|
| Pedalboard surface | SVG + Canvas 2D | Bezier cables (SVG), meter overlays (Canvas) |
| Pedal faceplates | SVG (generated) | Crisp at any zoom, broadcast aesthetic |
| Simple knobs | CSS 3D transforms | GPU-accelerated, < 10 per pedal |
| Realistic 3D knobs | WebGL shader | Metallic texture + lighting for mixer |
| VU meters | Canvas 2D | 60fps updates, pre-allocated gradients |
| Spectrum analyzer | Canvas 2D / WebGL | Bar graph (Canvas), waterfall (WebGL) |
| Mixer faders | WebGL 2.0 (raw) | Realistic fader caps + channel strip lighting |
| FFT engine | kiss_fft (vendored) | BSD-3, header-only, ~800 LOC |
| AI engine | Ollama (local) | Privacy-first, graceful offline degradation |
| WebSocket | Raw RFC 6455 | No external lib dependency |
| Episode editor | Canvas 2D + Web Audio API | Waveform rendering, EDL operations |
| Remote recording | WebRTC | Per-participant tracks, low latency |
