# Mcaster1DSPEncoder — Development Roadmap

**Version:** 2.0.0
**Last Updated:** 2026-03-27

---

## Current Release: v2.0.0 — ALL PHASES COMPLETE

v2.0.0 is a major release transforming Mcaster1 from a broadcast encoder into a full podcaster/broadcaster/video production studio. All 40+ development phases are complete, with quad-binary architecture, DSP Producer, multi-track DAW, forensic audio analysis, closed captions, monetization, and mode-based navigation.

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
| PC-1..PC-7 | Podcast studio (recording, editor, publishing, analytics, website, AI, remote) | **COMPLETE** |

### v1.9.0 — Video, DAW, Forensic, Captions, Monetization

| Phase | Description | Status |
|-------|-------------|--------|
| VP-1..VP-3 | DSP Producer daemon (video capture, switcher, RTMP, vodcast, overlays) | **COMPLETE** |
| DAW-1..DAW-3 | Multi-track DAW (timeline, clips, automation, effects, mixing, export, noise reduction, freeze) | **COMPLETE** |
| FA-1..FA-3 | Forensic audio (HQ spectrogram, 65536 FFT, WSOLA, peak detection, compare, reports, AI, goniometer) | **COMPLETE** |
| CC-1 | Closed captions (Whisper, SRT/VTT, live, burn-in, RSS) | **COMPLETE** |
| MON-1 | Monetization (DAI, campaigns, CPM, impressions, sponsors) | **COMPLETE** |
| NAV-1 | Mode-based navigation (5 modes), daemon health monitor | **COMPLETE** |
| MOBILE-1 | Mobile responsive UI, media picker component | **COMPLETE** |
| VIZ-1 | WebGL visualizations (spectrogram, 3D spectrum, globe, knobs, faders, cables) | **COMPLETE** |

---

## Future Releases

### v2.1.0 — Platform Expansion (Planned)

| Feature | Description | Priority |
|---------|-------------|----------|
| macOS native build | CoreAudio backend via PortAudio | Medium |
| Plugin SDK | Third-party DSP effect plugins | Medium |
| Multi-user collaboration | Concurrent web session editing | Low |
| Advanced VoicTune | De-breath, auto-tune, voice changer | Low |
| i18n localization | Multi-language web UI | Low |
| HLS/DASH streaming | Adaptive bitrate streaming output | Medium |
| SRT protocol | Secure Reliable Transport for low-latency | Medium |

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
| DAW timeline | Canvas 2D | Clip rendering, automation curves, zoom/scroll |
| Video encoding | FFmpeg subprocess | Transcode, RTMP push, thumbnail extraction |
| Forensic spectrogram | WebGL 2.0 | 65536-point FFT, real-time waterfall |
| Captions | Whisper (Ollama) | Speech-to-text, SRT/VTT output |
| Mode navigation | localStorage | 5 modes, sidebar filter, no reload |
