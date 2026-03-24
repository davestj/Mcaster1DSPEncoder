# Mcaster1DSPEncoder — Feature Overview

**Version:** 1.8.0-beta.1
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
- **De-Esser** — Sibilance reduction (planned)
- **Exciter** — Harmonic enhancement (planned)
- **Equal-Power Crossfader** — 9-curve types (linear, logarithmic, S-curve, etc.)
- **Sidechain Ducker** — PTT (push-to-talk) with spacebar trigger
- **Dead Air Detector** — Configurable silence threshold and fallback action

### Effects Rack (Modular)
- 6 unit types: EQ, Compressor, Gate, Limiter, De-Esser, Exciter
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

## VoicTune — Voice Analysis & Coaching (NEW in v1.8.0)

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

### Voice Coaching (Rule-Based)
- Level monitoring (LUFS target tracking)
- Peak clipping detection
- Sibilance analysis (high-frequency energy)
- Proximity effect detection (low-frequency boom)
- Pitch drift warning
- Pacing analysis (dead air, continuous speech)

### Ollama AI Integration
- HTTP client for local Ollama instance
- Graceful degradation when Ollama is offline
- 7 AI prompt templates:
  - Voice coaching
  - EQ suggestion (JSON output)
  - Effects chain suggestion
  - Natural language command parsing
  - Audio troubleshooting
  - Content analysis / show notes
  - Mixer configuration

### Database
- `mcaster1_voictune.sessions` — Analysis session tracking
- `mcaster1_voictune.voice_profiles` — Per-user voice characteristics
- `mcaster1_voictune.analysis_snapshots` — Time-series FFT/pitch/meter data
- `mcaster1_voictune.ai_interactions` — AI conversation history

---

## Architecture

### Triple-Binary Design (v1.8.0+)

```
mcaster1-dsp-encoder-admin  (36MB) — Web UI, FastCGI, auth, supervisor
    └── fork/exec ──→ mcaster1-dsp-encoder  (28MB) — Audio, DSP, codecs, streaming
mcaster1-voictune           (18MB) — Voice analysis, coaching, AI (independent)
```

- Fault isolation: codec crash doesn't kill web UI
- Admin auto-restarts encoder within 5 seconds
- VoicTune runs independently with own lifecycle

### Authentication
- **Layer 1:** C++ in-memory sessions (`mc1session` cookie)
- **Layer 2:** PHP/MySQL sessions (`mc1app_session` cookie)
- **Auto-bridge:** `footer.php` transparently creates PHP session from C++ auth
- **Cross-daemon SSO:** VoicTune accepts encoder admin cookies

### Security
- GnuPG binary signing (RSA-4096)
- SHA256 checksums for all binaries
- HTTPS with configurable SSL cert/key
- HttpOnly, SameSite=Strict session cookies
- bcrypt password hashing (MySQL layer)

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
make -j$(nproc)          # Build all 3 binaries
bash scripts/sign-binaries.sh  # GPG sign binaries
```
