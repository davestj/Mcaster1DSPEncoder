# Mcaster1DSPEncoder — Feature Overview

**Version:** 2.0.0
**Last Updated:** 2026-03-27

---

## Platform Support

| Platform | Build System | Audio Backend | Status |
|----------|-------------|---------------|--------|
| Linux (x86_64) | Autotools | PortAudio (ALSA/PulseAudio/JACK) | Active |
| Windows 10/11 | VS2022 | WASAPI / DirectSound | Maintained |
| macOS | Autotools | PortAudio (CoreAudio) | Planned |

---

## Core Features

### Multi-Format Audio Encoding
- MP3 (CBR via LAME)
- Ogg Vorbis (VBR)
- Opus (VBR, internal 48kHz resampling)
- FLAC (lossless archival)
- AAC-LC, HE-AAC v1, HE-AAC v2, AAC-ELD (via fdk-aac)

### Live Broadcast Streaming
- Icecast2 SOURCE protocol
- Shoutcast v1/v2 protocol
- Mcaster1DNAS native protocol
- ICY metadata (StreamTitle, StreamUrl)
- Auto-reconnect with exponential backoff
- Multi-slot simultaneous streaming (up to 12 slots)

### DSP Processing Chain
- **10-Band Parametric EQ** — RBJ biquad IIR filters (peaking, shelf)
- **AGC / Compressor** — Configurable threshold, ratio, attack, release
- **Hard Limiter** — Brick-wall peak limiting
- **Noise Gate** — Threshold-based gating with attack/release
- **Reverb** — Algorithmic reverb with room size, damping, wet/dry mix
- **Delay** — Tempo-synced or millisecond delay with feedback
- **De-Esser** — Sibilance reduction
- **Exciter** — Harmonic enhancement
- **Equal-Power Crossfader** — 9-curve types (linear, logarithmic, S-curve, etc.)
- **Sidechain Ducker** — PTT (push-to-talk) with spacebar trigger
- **Dead Air Detector** — Configurable silence threshold and fallback action

### Effects Rack (Modular)
- 8 unit types: EQ, Compressor, Gate, Limiter, De-Esser, Exciter, Reverb, Delay
- Drag-and-drop reordering
- Per-slot assignment: Global / Bypass / Custom chain
- Named presets per effect type

### JACK Audio Integration
- 12 virtual cables
- Port matrix for routing
- Automatic JACK client registration

---

## Web Administration

### Embedded HTTP/HTTPS Server
- cpp-httplib v0.18 with OpenSSL TLS
- Dual-port: HTTP (8330) + HTTPS (8344)
- Session-based auth with 64-byte hex tokens
- API token support via `X-API-Token` header

### PHP Web UI (via FastCGI)
- Dark navy/teal professional theme
- Dashboard with live encoder status cards
- Real-time progress bars (requestAnimationFrame at 60fps)
- Chart.js bandwidth and listener analytics
- Responsive sidebar navigation

### Pages
| Page | Description |
|------|-------------|
| Dashboard | Live encoder cards, progress bars, bandwidth chart |
| Encoders | Per-slot DSP controls, live stats, Sleep/Wake |
| Encoder Editor | 6-tab per-slot config (codec, stream, DSP, source, archive, advanced) |
| Media Library | Track browser, folder scan, metadata tags, artwork, categories |
| Media Player | Browser audio player with queue, drag-select, artwork display |
| MediaPlayerPro | Standalone popup WMP-style 3-column player |
| Playlists | DB-backed CRUD, 4-step generation wizard (8 algorithms) |
| Metrics | Chart.js listener analytics, session history, CSV export |
| Settings | Server info, encoder config table, DB admin, streaming servers |
| Profile | User profile, display name, email, password change |
| VoicTune | Voice analysis: oscilloscope, spectrum analyzer, pitch, meters, AI coaching |
| Mixer | Virtual mixer console with channel strips, faders, meters, 6 skins |
| Podcast Manager | Podcast show/episode management, archive scanner, RSS feeds |
| Recording Studio | One-click record from any slot, chapter markers, auto-split |
| Episode Editor | Browser waveform editor, EDL (cut/trim/fade/normalize), export |
| Podcast Analytics | Per-episode downloads, retention curves, platform breakdown, geo |
| Podcast Website | Auto-generated landing pages, episode list, embedded players, SEO |
| Song Requests | DJ request queue, approval workflow, dedication system |
| Request Widget | Public listener-facing request form (embeddable) |
| Now-Playing Widget | Embeddable player + now-playing for external sites |
| Remote Host | Remote recording host dashboard (manage guests, recording, chat) |
| Remote Guest | Remote recording guest view (audio, chat, hand-raise) |
| DAW | Multi-track DAW (timeline, clip editing, automation, mixing, export) |
| Forensic Audio | HQ spectrogram, 65536 FFT, noise subtraction, WSOLA, peak detection |
| Producer | DSP Producer (video capture, switcher, RTMP streaming, vodcast) |
| Monetization | Ad campaigns, dynamic ad insertion, CPM reports, sponsor management |
| Compliance | EBU R128 loudness compliance monitoring |
| Schedule | Clockwheel scheduler with category rotation |
| Crossfader | 9-curve crossfader controls |
| Effects Rack | Per-encoder and global effects rack management |
| Dual-Deck Player | Standalone A/B DJ mixer popup |
| JACK Routing | JACK audio port matrix for virtual cables |

### Media Library
- Folder browser with recursive scanning
- ID3/Vorbis/FLAC metadata extraction (TagLib)
- Cover art extraction and caching
- Broadcast category system (Music, Jingle, Sweeper, Spot, Station ID, News)
- Category weights for playlist generation
- Play count tracking

### Playlist Generation Wizard
- 8 algorithms: Random, Weighted Random, Genre Balanced, Artist Spread, BPM Flow, Energy Curve, Decade Mix, Category Rotation
- Preview before generation
- Load-to-slot direct integration

---

## Streaming Server Monitor

- Multi-server management (Icecast2, Shoutcast v1/v2, Steamcast, Mcaster1DNAS)
- Per-server live stats: listeners, max listeners, bitrate, stream title, uptime
- Color-coded status badges (Online/Offline/Unknown)
- Slot-to-mount assignment
- Client-side polling (30s interval)

---

## Listener Analytics

- Real-time listener count graphs (Chart.js)
- Session tracking (connect/disconnect, duration, bytes)
- Top tracks played
- CSV export of sessions and daily stats
- Date range filtering

---

## System Health Dashboard

- CPU load, memory usage, swap
- Disk space for audio root, archive, logs
- Installed codec detection
- PHP-FPM pool status
- SSL certificate expiry monitoring
- Process status and uptime

---

## Clockwheel Scheduler

- Hour-by-hour rotation scheduling
- Category-based rotation rules
- Dead air detection with configurable threshold
- Automatic track skip on silence

---

## VoicTune — Voice Analysis & Coaching

### Standalone Daemon
- Independent binary: `mcaster1-voictune` (18MB)
- HTTP/HTTPS API on ports 8350/8354
- WebSocket server on port 8355 for browser mic
- Own systemd unit, YAML config, MariaDB database

### Audio Analysis
- **FFT Spectrum Analysis** — kiss_fft, Hann window, parabolic peak interpolation
- **Pitch Detection** — Autocorrelation, A4=440Hz, note + octave + cents
- **Audio Metering** — RMS (dB), peak (dB), LUFS (ITU-R BS.1770-4)
- **Spectral Features** — Centroid, high/low frequency energy ratios

### Device Support
- PortAudio device enumeration (ALSA, PulseAudio, JACK)
- USB audio hotplug detection (inotify on /dev/snd/)
- Bluetooth audio detection (PulseAudio subscription)
- Browser mic via WebSocket (getUserMedia + AudioWorklet)
- Tri-mode: Server mic, Browser mic, Hybrid

### Voice Coaching
- Rule-based level monitoring (LUFS target tracking)
- Peak clipping detection
- Sibilance analysis (high-frequency energy)
- Proximity effect detection (low-frequency boom)
- Pitch drift warning
- Pacing analysis (dead air, continuous speech)
- AI-powered coaching tips via Ollama

### Ollama AI Integration
- HTTP client for local Ollama instance
- Graceful degradation when Ollama is offline
- 7 AI prompt templates: voice coaching, EQ suggestion, effects chain, NLP command, troubleshooting, content analysis, mixer config

---

## Visual Pedalboard

- SVG broadcast-themed pedal faceplates
- Drag-and-drop pedal placement on canvas
- Bezier SVG cable routing between pedals
- Real-time meters on each pedal (Canvas 2D)
- Signal flow visualization
- Save/load pedalboard layouts (DB-backed)

---

## Virtual Mixer Console

- Channel strips with faders, pan, mute, solo
- WebGL 2.0 rendered fader caps and meters
- 6 Mcaster1-branded mixer skins
- Master bus with metering
- Custom user effect profiles per channel
- Save/load mixer configurations (DB-backed)

---

## Podcast Studio

### Recording (PC-1)
- One-click record from any encoder slot
- Live recording timer with animated indicator
- Chapter marker system (keyboard shortcut: M key)
- Auto-split at configurable intervals
- Pre-roll / post-roll audio file selection
- Format selection (MP3, WAV, OGG, Opus, FLAC, AAC)

### Episode Editor (PC-2)
- Browser-based waveform editor (Canvas 2D + Web Audio API)
- Non-destructive editing via Edit Decision List (EDL)
- Operations: Cut, Trim, Fade In/Out, Silence, Normalize
- Undo/Redo stack (50 levels)
- Chapter marker editor with drag-to-reorder
- Multi-format export via FFmpeg

### Multi-Platform Publishing (PC-3)
- Publish targets per show (RSS, Apple, Spotify, YouTube, Podbean, Buzzsprout, custom)
- One-click or scheduled publishing
- Publish queue with status tracking
- YouTube video generation (cover art + audio via ffmpeg)

### Podcast Analytics (PC-4)
- Per-episode download tracking
- Listener retention curves
- Platform breakdown (Apple vs Spotify vs RSS)
- Growth trends (subscribers, downloads/week)
- Geographic breakdown

### Website Generator (PC-5)
- Auto-generated podcast landing pages
- Episode list with embedded players
- Show notes with clickable chapter timestamps
- Subscribe buttons (Apple, Spotify, RSS)
- SEO-optimized episode pages
- Customizable themes

### AI Podcast Tools (PC-6)
- Auto-transcription via Whisper or Ollama
- AI-generated show notes from transcript
- Chapter suggestions from content analysis
- SEO title/description suggestions
- Filler word detection

### Remote Recording (PC-7)
- WebRTC-based remote guest recording
- Separate tracks per participant
- Built-in chat and hand-raise
- Guest invite via URL (no account needed)

---

## User Engagement & Social (L11)

### Song Request System
- Public web widget for listener requests
- DJ queue with approve/reject workflow
- Dedication system with to/from messages
- Auto-load approved requests to encoder slot

### Webhooks
- Now-playing event dispatch (Discord, Slack, custom)
- Configurable webhook targets per event type
- Secret-based HMAC verification
- Delivery logs and retry

### Embeddable Widget
- Now-playing display for external websites
- Embedded audio player
- Station branding customization

---

## DSP Producer (Video)

### Standalone Daemon
- Independent binary: `mcaster1-producer` (9.1MB)
- HTTP/HTTPS API on ports 8360/8364
- Video/audio/FFT worker thread pools

### Video Features
- Video source capture and management
- Multi-camera switcher with transitions
- RTMP push streaming (YouTube Live, Twitch, etc.)
- Vodcast support (video + audio simultaneous encoding)
- Overlay management (text, images, logos)
- Thumbnail extraction via FFmpeg

---

## Multi-Track DAW

- Browser-based Canvas 2D timeline editor
- Multi-track clip placement with drag-and-drop
- Automation lanes (volume, pan, effects parameters)
- Per-track effects chain with bypass
- Real-time mixing with level metering
- Server-side export via FFmpeg (MP3, WAV, FLAC, AAC)
- Noise reduction processing
- Freeze tracks (render-in-place for CPU savings)
- Waveform rendering with zoom and scroll

---

## Forensic Audio Analysis

- **HQ Spectrogram** — WebGL rendering with up to 65536-point FFT
- **Noise Subtraction** — Spectral noise profile and removal
- **WSOLA Time Stretch** — Time-domain pitch-preserving stretch
- **Peak Detection** — Automatic transient and peak finding
- **Compare Mode** — Side-by-side analysis of two audio files
- **Report Generation** — PDF/HTML forensic analysis reports
- **AI Analysis** — Ollama-powered audio content analysis
- **Goniometer** — Stereo field visualization (Lissajous)
- **EBU R128 Compliance** — Integrated, momentary, and short-term loudness

---

## Closed Captions

- **Whisper Integration** — Speech-to-text via Ollama or external Whisper API
- **SRT/VTT Export** — Standard subtitle format output
- **Live Captions** — Real-time caption generation during broadcast
- **Burn-In** — Hardcode captions into video via FFmpeg
- **RSS Integration** — Caption tracks linked to podcast RSS feeds

---

## Monetization

- **Dynamic Ad Insertion (DAI)** — Automated ad placement in streams
- **Campaign Management** — Create/manage ad campaigns with date ranges
- **CPM Reporting** — Cost-per-mille impression tracking
- **Impression Logging** — Per-listener ad delivery tracking
- **Sponsor Management** — Sponsor configurations and rotation

---

## Mode-Based Navigation

Five UI modes that filter the sidebar to show relevant pages:
- **Broadcast** — Encoders, DSP, streaming, analytics
- **Podcast** — Recording, editing, publishing, RSS
- **Producer** — Video capture, switcher, RTMP, overlays
- **Forensic** — Spectrogram, analysis, compare, reports
- **DAW** — Multi-track timeline, mixing, export

---

## Daemon Health Monitor

- Real-time status of all 4 daemons (admin, encoder, voictune, producer)
- PHP helper (`daemon_monitor.php`) polls each daemon's `/health` endpoint
- Dashboard widget shows online/offline status with uptime
- Auto-refresh on 30-second interval

---

## Media Picker Component

- Reusable PHP component (`media_picker.php`) for selecting audio/video files
- Folder browser with breadcrumb navigation
- Search and filter by format, genre, category
- Used across recording, DAW, and producer pages

---

## Mobile Responsive UI

- `responsive.css` with breakpoints for tablet and phone
- `mobile-nav.js` for hamburger menu and swipe navigation
- Touch-friendly controls for faders and knobs
- Collapsed sidebar on small screens

---

## WebGL Visualizations

- **HQ Spectrogram** — 65536-point FFT, time-frequency waterfall
- **3D Spectrum** — Real-time 3D frequency visualization
- **Globe** — Geographic listener distribution
- **Knobs & Faders** — Realistic metallic texture with lighting
- **Cable Routing** — SVG bezier cables with glow effects
- **Dashboard** — Animated bandwidth and listener graphs

---

## Architecture

### Quad-Binary Design (v2.0.0)

```
mcaster1-dsp-encoder-admin  (46MB) — Web UI, FastCGI, auth, supervisor
    +-- fork/exec --> mcaster1-dsp-encoder  (29MB) — Audio, DSP, codecs, streaming
mcaster1-voictune           (21MB) — Voice analysis, coaching, AI (independent)
mcaster1-producer            (9.1MB) — Video, DAW mixdown, forensic FFT (independent)
```

- Fault isolation: codec crash doesn't kill web UI
- Admin auto-restarts encoder within 5 seconds
- VoicTune runs independently with own lifecycle
- Producer handles CPU-intensive video/audio/FFT jobs

### Authentication
- **Layer 1:** C++ in-memory sessions (`mc1session` cookie)
- **Layer 2:** PHP/MySQL sessions (`mc1app_session` cookie)
- **Auto-bridge:** `footer.php` transparently creates PHP session from C++ auth
- **Cross-daemon SSO:** VoicTune accepts encoder admin cookies

### Security
- GnuPG binary signing (RSA-4096)
- SHA256 checksums for all binaries
- HTTPS with configurable SSL cert/key
- HttpOnly, SameSite=Lax session cookies
- bcrypt password hashing (MySQL layer)
- Conditional Secure flag (HTTPS only)

---

## Build Requirements

### Linux Dependencies
```
Build: g++, autotools, autoconf-archive, pkg-config
Audio: portaudio19-dev, libjack-dev
Codecs: libmp3lame-dev, libvorbis-dev, libflac-dev, libopus-dev, libopusenc-dev, fdk-aac
Media: libtag1-dev, libmpg123-dev, libavformat-dev, libswresample-dev
Server: libssl-dev, libyaml-dev, libmariadb-dev
PHP: php8.2-fpm
```

### Quick Start
```bash
bash install-deps.sh     # Auto-detect OS, install everything
bash autogen.sh          # Bootstrap autotools
./configure              # Detect codecs
make -j$(nproc)          # Build all 4 binaries
bash scripts/sign-binaries.sh  # GPG sign binaries
```
