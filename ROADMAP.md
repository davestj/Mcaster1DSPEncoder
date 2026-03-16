# Mcaster1DSPEncoder — Development Roadmap

**Maintainer:** Dave St. John <davestj@gmail.com>
**Repository:** https://github.com/davestj/Mcaster1DSPEncoder
**Last Updated:** 2026-03-07

---

## Platform Overview

This project maintains three parallel codebases targeting different platforms.
All share the same feature set but NO source code is shared — Windows ports from macOS, macOS ports from Linux.

| Codebase | Current Version | Platform | Build System | UI |
|----------|----------------|----------|--------------|-----|
| **Windows Qt** | v1.3.0 (W1.3) | Win x64, VS2022 | MSBuild + Qt 6.9.3 msvc2022_64 | Qt 6 Widgets |
| **macOS** | v1.1.5 (M11.5) | macOS, Qt 6 | Autotools + Qt 6 | Qt 6 Widgets |
| **Linux CLI + Web Admin** | v1.4.5 (L5.5) | Linux (Debian/Ubuntu) | Autotools + g++ | PHP Web UI |
| **Windows MFC (legacy)** | v5.0 | Win32/x64, VS2022 | MSBuild (.sln) | MFC dialogs |

The **Windows Qt** and **macOS** builds are the active GUI development tracks.
The **Linux CLI + Web Admin** is the active server/web development track.
The **Windows MFC** build is maintained as a legacy standalone encoder + DSP plugin suite.

---

## Windows Qt Build — Phase Status (branch: `winqt-dev`)

| Phase | Version | Name | Status |
|-------|---------|------|--------|
| W0.1–W1.1 | v1.1.5 | Qt 6 Port Foundation (main window, encoder list, VU, DSP chain, streaming, YAML config, presets, log viewer, video pipeline, ICY2.2 tab) | **COMPLETE** |
| W1.2 | v1.2.0 | Preview Audio Studio, ICY1/ICY2.2 Protocol Parser, Live Stream Monitor | **COMPLETE** |
| W1.2.5 | v1.2.5 | PTT Resampling Fix, Event Logging, Sleep→IDLE Fix, Metadata Persistence, Save-on-Exit | **COMPLETE** |
| **W1.3** | **v1.3.0** | **Video Capture (MF+DirectShow), Live Video Studio, Virtual Camera, VP8/VP9, ON-AIR Indicator, Video Stream Monitor (AIR/CUE), DNAS Slot Poller** | **IN PROGRESS** |
| W1.4 | v1.4.0 | WASAPI Loopback VU Metering, Stream Health Dashboard, Bitrate Graph, NSIS Installer | PLANNED |

---

## macOS Build — Phase Status (branch: `macos-dev`)

| Phase | Version | Name | Status |
|-------|---------|------|--------|
| M1 | v0.1.0 | Main Window Shell, Menus, Tray, VU Meter, Encoder List | **COMPLETE** |
| M2 | v0.2.0 | Config Dialogs (4 Tabs), YAML Config, Encoder Presets | **COMPLETE** |
| M3 | v0.3.0 | Audio Engine: DSP, Codecs, PortAudio, ScreenCaptureKit | **COMPLETE** |
| M4 | v0.4.0 | Streaming Client, Server Protocols, Metadata Push | **COMPLETE** |
| M5 | v0.5.0 | Polish: Presets UI, Preferences, Log Viewer, EQ Visualizer | **COMPLETE** |
| M6 | v0.6.0 | .app Bundle, 31-Band EQ, Sonic Enhancer, Video Pipeline | **COMPLETE** |
| M6.5 | v0.6.5 | Video Effects (13), Overlays, SRT, Broadcast Pipeline | **COMPLETE** |
| M7 | v0.7.0 | Radio/Podcast/TV-Video Categories, RSS, SFTP, Studio Dialog | **COMPLETE** |
| M8 | v0.8.0 | PTT Ducking, DBX Voice, REST Publishing, Stream Target Editor | **COMPLETE** |
| M8.5 | v0.8.5 | DSP Effects Rack, Effects Rack Tab, Per-Encoder DSP Controls | **COMPLETE** |
| M9 | v0.9.0 | Video Streaming: RTMP, HLS, WebM, FLV, Transitions | **COMPLETE** |
| M10 | v1.0.0 | Integration, VP8/VP9, Entitlements, DMG, Notifications | **COMPLETE** |
| M11 | v1.1.0 | Global DSP Rack Persistence, Per-Unit YAML, Save Settings | **COMPLETE** |
| M11.5 | v1.1.5 | DSP Persistence Bug Fixes, 10-Band EQ Sync, Model Vector Fixes | **COMPLETE** |

---

## Linux CLI + Web Admin — Phase Status (branch: `linux-dev`)

| Phase | Version | Name | Status |
|-------|---------|------|--------|
| L1 | v1.0.0 | Infrastructure (platform.h, ICY 2.2 fields, build system) | **COMPLETE** |
| L2 | v1.1.1 | HTTP/HTTPS Admin Server + Web UI | **COMPLETE** |
| L3 | v1.2.0 | Audio Encoding + Streaming Engine | **COMPLETE** |
| L4 | v1.3.0 | DSP Chain (EQ/AGC/Crossfade) + ICY2 + DNAS Stats | **COMPLETE** |
| L5 | v1.4.0 | PHP Frontend Overhaul + Logging + Autotools | **COMPLETE** |
| L5.1 | v1.4.1 | Media Library (folder browser, track tags, playlists) | **COMPLETE** |
| L5.2 | v1.4.2 | Browser Media Player (queue, audio streaming) | **COMPLETE** |
| L5.3 | v1.4.3 | Session Fix, Content-Range Fix | **COMPLETE** |
| L5.4 | v1.4.4 | User Profile, Broadcast Roles, Session TTL | **COMPLETE** |
| L5.5 | v1.4.5 | Standalone Popup Player, Category UX Overhaul | **COMPLETE** |
| L6 | v1.5.0 | Streaming Server Relay Monitor (multi-DNAS/Icecast/Shoutcast) | PLANNED |
| L7 | v1.6.0 | Listener Analytics & Metrics Dashboard | PLANNED |
| L8 | v1.7.0 | System Health Monitoring (CPU/memory/network) | PLANNED |
| L9 | v1.8.0 | Advanced Automation & Clockwheel Scheduling | PLANNED |
| L10 | v1.9.0 | Podcast & Archive Management | PLANNED |
| L11 | v2.0.0 | User Engagement & Social Integration | PLANNED |

---

## Windows MFC (Legacy) — Phase Status (branch: `master`)

| Phase | Name | Status |
|-------|------|--------|
| 1 | VS2022 Build Fix | **COMPLETE** |
| 2 | Rebrand AltaCast → Mcaster1DSPEncoder | **COMPLETE** |
| 2.5 | Project Reorganization (master .sln, SDK layout) | **COMPLETE** |
| 3 | Audio Backend (Opus, HE-AAC, PortAudio/ASIO) | **COMPLETE** |
| 4 | YAML Multi-Station Config | **COMPLETE** |
| 5 | ICY 2.2 Metadata + Podcast RSS | **COMPLETE** |

---

## Feature Matrix — Current Capabilities

| Feature | Windows Qt | macOS | Linux | Win MFC |
|---------|-----------|-------|-------|---------|
| Multi-format audio (MP3/Vorbis/Opus/FLAC/AAC) | Yes | Yes | Yes | Yes |
| 10-band parametric EQ | Yes | Yes | Yes | No |
| 31-band graphic EQ | Yes | Yes | No | No |
| AGC / Compressor / Limiter | Yes | Yes | Yes | No |
| Sonic Enhancer | Yes | Yes | No | No |
| PTT ducking with mic input | Yes | Yes | No | No |
| DBX Voice processor | Yes | Yes | No | No |
| DSP Effects Rack (per-encoder) | Yes | Yes | No | No |
| ICY 2.2 extended metadata | Yes | Yes | Yes | Partial |
| Video streaming (RTMP/HLS/WebM/FLV) | Yes | Yes | No | No |
| Live Video Studio (3-source switcher) | Yes | Yes | No | No |
| VP8/VP9 video encoding | Yes | Yes | No | No |
| Virtual Camera output | Yes | Yes | No | No |
| Video Stream Monitor (AIR/CUE) | Yes | No | No | No |
| Podcast RSS generation | Yes | Yes | No | No |
| SFTP publishing | No | Yes | No | No |
| Preview Audio Studio | Yes | Yes | No | No |
| YAML profile persistence | Yes | Yes | No | No |
| PHP web admin UI | No | No | Yes | No |
| Winamp/foobar DSP plugin | No | No | No | Yes |

---

## Release Version Plan

### Windows Qt

| Tag | Phase | Description |
|-----|-------|-------------|
| `v1.1.5` | W0.1–W1.1 | Qt 6 port foundation — full feature parity with macOS M11.5 |
| `v1.2.0` | W1.2 | Preview Audio Studio, ICY protocol parser |
| `v1.2.5` | W1.2.5 | PTT fix, event logging, metadata persistence |
| `v1.3.0` | W1.3 | Video capture, Live Video Studio, Virtual Camera, VP8/VP9, Video Stream Monitor |
| `v1.4.0` | W1.4 | WASAPI VU metering, stream health dashboard, NSIS installer |

### macOS

| Tag | Phase | Description |
|-----|-------|-------------|
| `v1.0.0` | M10 | First release — VP8/VP9, DMG, code signing, notifications |
| `v1.1.0` | M11 | DSP rack persistence, per-unit YAML |
| `v1.1.5` | M11.5 | Bug fixes, EQ sync, model vector fixes |

### Linux CLI + Web Admin

| Tag | Phase | Description |
|-----|-------|-------------|
| `v1.0.0` | L1 | Infrastructure, build system, ICY 2.2 structs |
| `v1.1.1` | L2 | HTTP/HTTPS admin server + web UI |
| `v1.2.0` | L3 | PortAudio capture + file streaming + encoding + Icecast client |
| `v1.3.0` | L4 | DSP (EQ/AGC/crossfade) + ICY2 + DNAS stats |
| `v1.4.0` | L5 | PHP frontend, logging, autotools |
| `v1.4.5` | L5.5 | Media player, category system, playlist generator |
| `v1.5.0` | L6 | Streaming server relay monitor |
| `v1.6.0` | L7 | Listener analytics & metrics |

---

## Contributing

Issues, pull requests, and discussion are open on GitHub.
See `FORKS.md` for the rationale behind this project.
See `CREDITS.md` for attribution and project history.
