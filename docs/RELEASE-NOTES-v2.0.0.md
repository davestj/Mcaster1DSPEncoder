# Mcaster1DSPEncoder v2.0.0 Release Notes

**Release Date:** 2026-03-27
**Status:** Stable Release
**Branch:** `linux-dev`
**Maintainer:** Dave St. John <davestj@gmail.com>

---

## Overview

v2.0.0 is a major release that transforms Mcaster1 from a broadcast encoder and podcast studio into a complete audio/video production platform. This release adds a fourth daemon (Producer), multi-track DAW, forensic audio analysis, closed captions, monetization, mode-based navigation, and mobile responsive UI.

The project now uses a **quad-binary architecture** with the addition of `mcaster1-producer`.

---

## New Features

### DSP Producer Daemon (VP-1..VP-3)
- Standalone C++ daemon (`mcaster1-producer`, 9.1MB) for CPU-intensive workloads
- HTTP/HTTPS API on ports 8360/8364
- Video source capture and multi-camera switcher with transitions
- RTMP push streaming (YouTube Live, Twitch, Facebook Live)
- Vodcast support (simultaneous video + audio encoding)
- Overlay management (text, images, logos)
- Thumbnail extraction via FFmpeg subprocess
- Own systemd unit, YAML config, and worker pool

### Multi-Track DAW (DAW-1..DAW-3)
- Browser-based Canvas 2D timeline editor
- Multi-track clip placement with drag-and-drop
- Automation lanes (volume, pan, effects parameters)
- Per-track effects chain with bypass
- Real-time mixing with level metering
- Server-side export via FFmpeg (MP3, WAV, FLAC, AAC)
- Noise reduction processing (spectral subtraction)
- Freeze tracks (render-in-place for CPU savings)
- Waveform rendering with zoom and scroll
- Web UI: `/daw.php`

### Forensic Audio Analysis (FA-1..FA-3)
- HQ spectrogram with WebGL rendering (up to 65536-point FFT)
- Spectral noise subtraction with noise profile capture
- WSOLA time stretch (pitch-preserving)
- Automatic peak and transient detection
- Side-by-side audio file comparison
- PDF/HTML forensic analysis report generation
- AI-powered audio content analysis via Ollama
- Goniometer (Lissajous stereo field visualization)
- EBU R128 loudness compliance (integrated, momentary, short-term)
- Web UI: `/forensic.php`

### Closed Captions (CC-1)
- Speech-to-text via Whisper (Ollama or external API)
- SRT and VTT subtitle format export
- Live caption generation during broadcast
- Burn-in captions to video via FFmpeg
- Caption tracks linked to podcast RSS feeds
- Web UI controls in producer and podcast pages

### Monetization (MON-1)
- Dynamic Ad Insertion (DAI) with automated placement
- Campaign management with date ranges and targeting
- CPM (cost-per-mille) impression tracking and reporting
- Per-listener ad delivery logging
- Sponsor configuration and rotation scheduling
- Web UI: `/monetization.php`

### Mode-Based Navigation (NAV-1)
- 5 UI modes: Broadcast, Podcast, Producer, Forensic, DAW
- Sidebar filters visible pages based on active mode
- Mode state persisted in `localStorage['mc1_mode']`
- Mode switching without page reload (CSS visibility toggle)

### Daemon Health Monitor
- Real-time status of all 4 daemons displayed in dashboard
- PHP helper (`daemon_monitor.php`) polls each daemon's `/health` endpoint
- Dashboard widget with online/offline badges and uptime display
- Auto-refresh on 30-second interval
- Health API: `/app/api/health.php`

### Mobile Responsive UI (MOBILE-1)
- `responsive.css` with tablet and phone breakpoints
- `mobile-nav.js` for hamburger menu and swipe navigation
- Touch-friendly fader and knob controls
- Collapsed sidebar on small screens

### Media Picker Component
- Reusable PHP component (`media_picker.php`)
- Folder browser with breadcrumb navigation
- Search and filter by format, genre, category
- Used across recording, DAW, and producer pages

### Video Playback Support
- WebGL video preview in producer page
- Video source selection and switching
- RTMP stream management API

### WebGL Visualizations (VIZ-1)
- HQ spectrogram with 65536-point FFT waterfall
- 3D spectrum analyzer
- Geographic listener globe
- Realistic metallic knobs and faders with lighting
- SVG bezier cable routing with glow effects
- Animated dashboard bandwidth and listener graphs

---

## Binaries

| Binary | Size | Ports | Purpose |
|--------|------|-------|---------|
| `mcaster1-dsp-encoder-admin` | 46 MB | 8330/8344 | Web UI, FastCGI, auth, process supervisor |
| `mcaster1-dsp-encoder` | 29 MB | --- | Audio pipeline, DSP, codecs, streaming |
| `mcaster1-voictune` | 21 MB | 8350/8354/8355 | Voice analysis, coaching, Ollama AI |
| `mcaster1-producer` | 9.1 MB | 8360/8364 | Video encoding, DAW mixdown, forensic FFT |

---

## Database Changes

### New Database: `mcaster1_producer`
- `jobs` -- Worker job queue (video, audio, FFT)
- `job_results` -- Completed job output references
- `forensic_reports` -- Forensic analysis report metadata

### New Tables in `mcaster1_encoder`
- `ad_campaigns` -- Ad campaign metadata and scheduling
- `ad_schedule` -- Per-slot ad insertion schedule
- `ad_impressions` -- Per-listener impression records
- `sponsor_configs` -- Sponsor rotation configuration

### New Tables in `mcaster1_media`
- `daw_projects` -- DAW project metadata
- `daw_tracks` -- Per-project track definitions
- `daw_clips` -- Audio clips placed on tracks
- `daw_automation` -- Automation lane points
- `captions` -- Caption job metadata
- `caption_segments` -- Individual caption segments (timestamp, text)

### New Tables in `mcaster1_metrics`
- `ad_impressions_log` -- Historical ad impression data

---

## New Web UI Pages

| Page | URL | Description |
|------|-----|-------------|
| DAW | `/daw.php` | Multi-track timeline editor with mixing and export |
| Forensic Audio | `/forensic.php` | HQ spectrogram, analysis, compare, reports |
| Producer | `/producer.php` | Video capture, switcher, RTMP streaming |
| Monetization | `/monetization.php` | Ad campaigns, DAI, sponsor management |
| Compliance | `/compliance.php` | EBU R128 loudness monitoring |
| Schedule | `/schedule.php` | Clockwheel scheduler UI |

---

## New API Endpoints

### PHP APIs
- `POST /app/api/daw.php` -- DAW project/clip/automation CRUD, mix, export, freeze, noise reduce
- `POST /app/api/forensic.php` -- Forensic analysis jobs, spectrogram, peak detection, compare, reports
- `POST /app/api/producer.php` -- Video source management, switcher, overlay, stream control
- `POST /app/api/ads.php` -- Campaign CRUD, ad scheduling, impression tracking, CPM reports
- `POST /app/api/captions.php` -- Caption generation, SRT/VTT download, live captions, burn-in
- `POST /app/api/health.php` -- Multi-daemon health status check
- `POST /app/api/rtmp.php` -- RTMP stream management
- `POST /app/api/schedule.php` -- Clockwheel schedule CRUD
- `POST /app/api/upload.php` -- Multipart file upload handler

### Producer API (ports 8360/8364)
- `GET /api/v1/producer/health` -- Health check (no auth)
- `POST /api/v1/producer/auth/login` -- Login
- `GET /api/v1/producer/status` -- Worker queue depths, active jobs

---

## New JavaScript Modules

| File | Purpose |
|------|---------|
| `daw-engine.js` | DAW timeline engine, clip management, automation |
| `daw-waveform.js` | DAW waveform rendering (Canvas 2D) |
| `forensic-analyzer.js` | Forensic spectrogram and analysis tools |
| `video-producer.js` | Video capture, switcher, RTMP controls |
| `captions-engine.js` | Whisper caption generation and display |
| `mobile-nav.js` | Mobile responsive hamburger menu |
| `webgl-spectrogram-hq.js` | WebGL HQ spectrogram (65536 FFT) |
| `webgl-video.js` | WebGL video preview rendering |
| `webgl-dashboard.js` | WebGL dashboard visualizations |

---

## Breaking Changes

- Version string in `/api/v1/status` changed from `1.8.0-beta.1` to `2.0.0`
- `SERVER_SOFTWARE` FastCGI param changed to `mcaster1-encoder/2.0.0`
- VoicTune `/api/v1/voictune/health` version changed to `2.0.0`
- Producer daemon added as 4th binary (new ports 8360/8364)
- Sidebar navigation restructured with mode-based filtering

---

## Upgrade Notes

1. Install the new `mcaster1-producer` systemd service:
   ```bash
   sudo cp install/mcaster1-producer.service /etc/systemd/system/
   sudo systemctl daemon-reload
   sudo systemctl enable mcaster1-producer
   sudo systemctl start mcaster1-producer
   ```

2. Run database migrations for new tables (daw_*, captions*, ad_*, forensic_reports)

3. Rebuild all binaries:
   ```bash
   bash autogen.sh
   ./configure
   make -j$(nproc)
   ```

4. Restart all services:
   ```bash
   sudo systemctl restart mcaster1-dsp-encoder
   sudo systemctl restart mcaster1-voictune
   sudo systemctl restart mcaster1-producer
   ```
