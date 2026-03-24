# Mcaster1DSPEncoder v1.8.0-beta.1 Release Notes

**Release Date:** 2026-03-27
**Status:** Beta Preview
**Branch:** `linux-dev`
**Maintainer:** Dave St. John <davestj@gmail.com>

---

## Overview

v1.8.0-beta.1 is a major feature release that introduces VoicTune, a standalone voice analysis and coaching daemon. This release transforms Mcaster1 from a broadcast encoder into a full podcaster/broadcaster production studio.

The project has moved from a dual-binary (admin + encoder) to a **triple-binary architecture** with the addition of `mcaster1-voictune`.

---

## New Features

### VoicTune Daemon (`mcaster1-voictune`)

A new standalone C++ daemon for real-time voice analysis, pitch detection, spectrum analysis, and AI-powered voice coaching.

- **HTTP/HTTPS REST API** on ports 8350/8354 with session auth
- **WebSocket server** on port 8355 for browser mic audio streaming (RFC 6455)
- **FFT spectrum analysis** using vendored kiss_fft (BSD-3, header-only)
- **Musical pitch detection** — autocorrelation, A4=440Hz, note name + cents offset
- **Audio metering** — RMS, peak, LUFS (ITU-R BS.1770-4 approximation)
- **PortAudio mic capture** — device enumeration, start/stop, re-enumeration
- **USB/BT audio hotplug** — inotify on `/dev/snd/`, 500ms settle delay, auto re-enumeration
- **Rule-based voice coaching** — level, peak clipping, sibilance, proximity effect, pitch drift, pacing
- **Ollama AI integration** — REST client for LLM-powered coaching, EQ suggestions, NLP commands
- **AI prompt templates** — 7 system prompts for coaching, EQ, effects chain, NLP, troubleshooting, content, mixer
- **Thread pool** — configurable worker threads for parallel FFT analysis
- **MariaDB database** — `mcaster1_voictune` with 4 tables (sessions, voice_profiles, analysis_snapshots, ai_interactions)
- **YAML configuration** — mirrors encoder admin config pattern
- **systemd unit** — `mcaster1-voictune.service` with restart policy, resource limits
- **Logging** — singleton logger writing to `/var/log/mcaster1/voictune.log` and `voictune_error.log`

### Build System

- `configure.ac` bumped to v1.8.0-beta.1
- `--with-ollama-endpoint=URL` option (default: `http://127.0.0.1:11434`)
- `mcaster1-voictune` added to `bin_PROGRAMS` in Makefile.am
- 13 VoicTune source files in build system
- `kiss_fft_log.h` stub for vendored FFT library compatibility

### GnuPG Binary Signing

- New RSA-4096 key for binary code signing: `6C07628DF4D94C20`
- Package signing key: `A29A09463F34D8D5`
- Signing script at `scripts/sign-binaries.sh`
- Detached `.sig` files for all compiled binaries

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

```sql
CREATE TABLE sessions (id, session_name, user_id, started_at, ended_at, duration_sec, notes);
CREATE TABLE voice_profiles (id, user_id, profile_name, fundamental_hz, voice_type, avg_lufs, avg_rms_db, eq_preset_json, effects_chain_json, analysis_json);
CREATE TABLE analysis_snapshots (id, session_id, timestamp_ms, rms_db, peak_db, lufs, pitch_hz, note_name, cents_off, spectrum_json);
CREATE TABLE ai_interactions (id, user_id, context, prompt_text, response_text, model_used, latency_ms);
```

No changes to existing databases (`mcaster1_encoder`, `mcaster1_media`, `mcaster1_metrics`).

---

## API Endpoints (VoicTune)

| Method | Endpoint | Auth | Description |
|--------|----------|------|-------------|
| GET | `/api/v1/voictune/health` | No | Health check, version, uptime |
| POST | `/api/v1/voictune/auth/login` | No | Login, returns session cookie |
| POST | `/api/v1/voictune/auth/logout` | Yes | Logout |
| GET | `/api/v1/voictune/status` | Yes | Config, audio device, ports |
| GET | `/api/v1/voictune/devices` | Yes | PortAudio devices with USB/BT flags |
| GET | `/api/v1/voictune/meters` | Yes | RMS, peak, LUFS, pitch (live in VT-2) |
| GET | `/api/v1/voictune/spectrum` | Yes | FFT magnitude bins (live in VT-2) |
| GET | `/api/v1/ai/status` | Yes | Ollama availability, model list |

---

## Planned Phases (Post-Beta)

| Phase | Description |
|-------|-------------|
| VT-2 | Audio capture pipeline + live analysis wiring |
| VT-3 | VoicTune web UI (oscilloscope, spectrum analyzer, pitch display) |
| VT-4 | Voice coaching (rule-based + AI tips in browser) |
| PB-1..3 | Visual pedalboard (SVG pedals, cable routing, live meters) |
| AI-1..4 | Ollama AI (coaching chat, EQ/chain suggestions, NLP, content analysis) |
| MX-1..3 | Virtual mixer console (channel strips, skins, custom presets) |

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
src/linux/mcaster1-voictune --help
```

---

## Breaking Changes

- Version string in `/api/v1/status` changed from `1.2.0` to `1.8.0-beta.1`
- `SERVER_SOFTWARE` FastCGI param changed from `mcaster1-encoder/1.2.0` to `mcaster1-encoder/1.8.0-beta.1`

---

## Known Limitations (Beta)

- VoicTune `/meters` and `/spectrum` endpoints return placeholder data until VT-2 wires the audio pipeline
- WebSocket server instantiated but not started (deferred to VT-2)
- Ollama AI client connects but no chat/coaching API endpoints yet (deferred to AI-1)
- No web UI for VoicTune yet (deferred to VT-3)
