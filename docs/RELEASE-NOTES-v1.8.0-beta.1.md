# Mcaster1DSPEncoder v1.8.0-beta.1 Release Notes

**Release Date:** 2026-03-27
**Status:** Beta Preview (all phases complete)
**Branch:** `linux-dev`
**Maintainer:** Dave St. John <davestj@gmail.com>

---

## Overview

v1.8.0-beta.1 is a major feature release that transforms Mcaster1 from a broadcast encoder into a full podcaster/broadcaster production studio. All 23+ development phases are complete, adding VoicTune voice analysis, visual pedalboard, virtual mixer, Ollama AI integration, complete podcast studio with recording/editing/publishing, remote recording, song requests, webhooks, and more.

The project now uses a **triple-binary architecture** with the addition of `mcaster1-voictune`.

---

## New Features

### VoicTune Daemon (VT-1..VT-4)
- Standalone C++ daemon (`mcaster1-voictune`, 18MB) for real-time voice analysis
- HTTP/HTTPS API on ports 8350/8354, WebSocket on port 8355
- FFT spectrum analysis (kiss_fft), musical pitch detection, RMS/peak/LUFS metering
- PortAudio mic capture with USB/BT hotplug detection (inotify)
- Rule-based voice coaching (level, clipping, sibilance, proximity, pitch drift, pacing)
- Web UI with oscilloscope, spectrum analyzer, pitch display, and meters
- AI-powered coaching tips via Ollama
- MariaDB database (`mcaster1_voictune`) with 4 tables

### Visual Pedalboard (PB-1..PB-3)
- SVG broadcast-themed pedal faceplates with drag-and-drop placement
- Bezier SVG cable routing for signal flow visualization
- Real-time Canvas 2D meters on each pedal
- Save/load pedalboard layouts (DB-backed)

### Ollama AI Integration (AI-1..AI-4)
- Coaching chat and EQ/effects chain suggestion endpoints
- Natural language command parsing (text to API actions)
- Content analysis and AI-generated show notes
- Smart playlist generation and audio troubleshooting
- 7 prompt templates with graceful offline degradation

### Virtual Mixer Console (MX-1..MX-3)
- Channel strips with faders, pan, mute, solo (WebGL 2.0)
- 6 Mcaster1-branded mixer skins
- Custom user effect profiles per channel
- Save/load mixer configurations (DB-backed)

### Podcast Archive & RSS (L10)
- Podcast show and episode CRUD management
- iTunes-compatible RSS feed generation (`/podcast/{show_id}/feed.xml`)
- Archive directory scanner for importing existing recordings
- Episode metadata editing (title, description, season, episode number, tags)

### Song Requests & Webhooks (L11)
- Public web widget for listener song requests
- DJ queue with approve/reject workflow
- Dedication system (to/from messages)
- Webhook dispatch for now-playing events (Discord, Slack, custom)
- HMAC-signed webhook payloads with delivery logs
- Embeddable now-playing widget for external websites

### Recording Studio (PC-1)
- One-click record from any encoder slot
- Live recording timer with animated indicator
- Chapter marker system (M key shortcut)
- Auto-split at configurable intervals
- Pre-roll / post-roll audio file selection
- Multi-format output (MP3, WAV, OGG, Opus, FLAC, AAC)
- C++ recording API (`/api/v1/recording/*`)

### Episode Editor (PC-2)
- Browser-based waveform editor (Canvas 2D + Web Audio API)
- Non-destructive editing via Edit Decision List (EDL)
- Operations: Cut, Trim, Fade In/Out, Silence, Normalize
- 50-level Undo/Redo stack
- Chapter marker editor with drag-to-reorder
- Server-side multi-format export via FFmpeg

### Multi-Platform Publishing (PC-3)
- Publish targets per show (RSS, Apple, Spotify, YouTube, Podbean, Buzzsprout, custom)
- One-click or scheduled publishing with queue tracking
- YouTube video generation (cover art + audio via ffmpeg)
- Social media cross-posting via webhooks

### Podcast Analytics (PC-4)
- Per-episode download tracking
- Listener retention curves
- Platform breakdown (Apple vs Spotify vs RSS)
- Growth trends and geographic breakdown

### Podcast Website Generator (PC-5)
- Auto-generated podcast landing pages
- Episode list with embedded audio players
- Show notes with clickable chapter timestamps
- Subscribe buttons, SEO optimization, customizable themes

### AI Podcast Tools (PC-6)
- Auto-transcription via Whisper or Ollama
- AI-generated show notes from transcript
- Chapter suggestions from content analysis
- SEO title/description suggestions
- Filler word detection

### Remote Recording (PC-7)
- WebRTC-based remote guest recording
- Separate tracks per participant
- Built-in chat and hand-raise system
- Guest invite via URL (no account needed)

### DSP Enhancements
- **Reverb** — Algorithmic reverb with room size, damping, wet/dry mix
- **Delay** — Tempo-synced or millisecond delay with feedback
- Effects rack expanded to 8 unit types (added Reverb, Delay)

### Security Patches
- Cookie SameSite changed from Strict to Lax (fixes cross-port navigation)
- Conditional Secure flag (only on HTTPS, prevents HTTP login loops)
- Improved session cleanup on competing instance detection

### Build System
- `configure.ac` bumped to v1.8.0-beta.1
- `--with-ollama-endpoint=URL` option (default: `http://127.0.0.1:11434`)
- `mcaster1-voictune` added to `bin_PROGRAMS` in Makefile.am
- 13 VoicTune source files in build system
- GnuPG binary signing (RSA-4096 key: `6C07628DF4D94C20`)

---

## Binaries

| Binary | Size | Purpose |
|--------|------|---------|
| `mcaster1-dsp-encoder-admin` | 36 MB | Web UI, FastCGI, auth, process supervisor |
| `mcaster1-dsp-encoder` | 28 MB | Audio pipeline, DSP, codecs, streaming |
| `mcaster1-voictune` | 18 MB | Voice analysis, coaching, Ollama AI |

---

## Database Changes

### New Database: `mcaster1_voictune`
- `sessions` — Analysis session tracking
- `voice_profiles` — Per-user voice characteristics
- `analysis_snapshots` — Time-series FFT/pitch/meter data
- `ai_interactions` — AI conversation history

### New Tables in `mcaster1_encoder`
- `pedalboard_layouts` — Saved pedalboard configurations
- `mixer_configs` — Mixer console configurations
- `mixer_custom_units` — Custom effect units per mixer
- `webhook_configs` — Webhook targets and event config

### New Tables in `mcaster1_media`
- `podcast_shows` — Podcast show metadata
- `podcast_episodes` — Episode metadata, file paths, publish state
- `episode_markers` — Chapter markers (chapter, note, highlight, ad_break)
- `publish_targets` — Per-show publish platform configs
- `publish_queue` — Publish job queue with status tracking
- `podcast_downloads` — Episode download tracking
- `song_requests` — Listener song requests with status
- `dedications` — Request dedications (to/from)
- `remote_sessions` — Remote recording sessions
- `remote_participants` — Per-session participants with track files
- `remote_chat` — In-session chat messages

---

## New Web UI Pages

| Page | URL | Description |
|------|-----|-------------|
| VoicTune | `/voictune.php` | Voice analysis with oscilloscope, spectrum, pitch, meters |
| Mixer | `/mixer.php` | Virtual mixer console with 6 skins |
| Podcast Manager | `/podcast.php` | Show/episode CRUD, archive scan, RSS |
| Recording Studio | `/recording.php` | Live recording with chapter markers |
| Episode Editor | `/episode-editor.php` | Waveform editor with EDL operations |
| Podcast Analytics | `/podcast-analytics.php` | Download tracking, retention, geo |
| Podcast Website | `/podcast-site.php` | Landing page generator |
| Song Requests | `/requests.php` | DJ request queue and approval |
| Request Widget | `/request-widget.php` | Public listener request form |
| Now-Playing Widget | `/widget.php` | Embeddable player widget |
| Remote Host | `/remote-host.php` | Remote recording host dashboard |
| Remote Guest | `/remote-guest.php` | Guest recording view |

---

## New API Endpoints

### C++ Recording API
- `POST /api/v1/recording/start` — Start recording on a slot
- `POST /api/v1/recording/stop` — Stop recording
- `POST /api/v1/recording/marker` — Add chapter marker
- `GET /api/v1/recording/status` — Recording state for all slots
- `POST /api/v1/recording/split` — Split current recording

### PHP APIs
- `POST /app/api/podcast.php` — Podcast CRUD, RSS, publishing (30+ actions)
- `POST /app/api/requests.php` — Song request/dedication management
- `POST /app/api/webhooks.php` — Webhook CRUD, test, logs
- `POST /app/api/remote.php` — Remote recording session management

### VoicTune API (ports 8350/8354)
- `GET /api/v1/voictune/health` — Health check (no auth)
- `POST /api/v1/voictune/auth/login` — Login
- `GET /api/v1/voictune/status` — Config, device, ports
- `GET /api/v1/voictune/devices` — PortAudio devices
- `GET /api/v1/voictune/meters` — Live RMS, peak, LUFS, pitch
- `GET /api/v1/voictune/spectrum` — Live FFT magnitude bins
- `GET /api/v1/ai/status` — Ollama availability

---

## Breaking Changes

- Version string in `/api/v1/status` changed from `1.2.0` to `1.8.0-beta.1`
- `SERVER_SOFTWARE` FastCGI param changed to `mcaster1-encoder/1.8.0-beta.1`
- Session cookie SameSite changed from `Strict` to `Lax`

---

## Build Instructions

```bash
# Install dependencies (first time)
bash install-deps.sh

# Bootstrap + configure + build
bash autogen.sh
./configure
make -j$(nproc)

# Verify
src/linux/mcaster1-voictune --version
src/linux/mcaster1-dsp-encoder-admin --version
```
