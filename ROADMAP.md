# Mcaster1DSPEncoder — Development Roadmap

**Maintainer:** Dave St. John <davestj@gmail.com>
**Repository:** https://github.com/davestj/Mcaster1DSPEncoder
**Last Updated:** 2026-02

---

## Platform Overview

This project maintains two parallel codebases targeting different platforms:

| Codebase | Version | Platform | Build System |
|----------|---------|----------|--------------|
| **Windows DSP Encoder** | v5.0 | Win32/x64, VS2022 | MSBuild (.vcxproj) |
| **Linux CLI + Web Admin** | v1.1.1 | Linux (Debian/Ubuntu) | autotools + g++ |

Windows phases 1–5 are complete. The Linux build is the active development track.
All Linux code lives under `src/linux/` — no Windows code is modified.

---

## Windows Build — Phase Status

| Phase | Name | Status |
|-------|------|--------|
| 1 | VS2022 Build Fix | **COMPLETE** |
| 2 | Rebrand AltaCast → Mcaster1DSPEncoder | **COMPLETE** |
| 2.5 | Project Reorganization (master .sln, SDK layout) | **COMPLETE** |
| 3 | Audio Backend (Opus, HE-AAC, PortAudio/ASIO) | **COMPLETE** |
| 4 | YAML Multi-Station Config | **COMPLETE** |
| 5 | ICY 2.2 Metadata + Podcast RSS | **IN PROGRESS** |
| 6 | Mcaster1DNAS Deep Integration | PLANNED |
| 7 | Mcaster1Castit (broadcast automation) | PLANNED |
| 8 | Analytics, Metrics & Platform Engagement | PLANNED |

---

## Linux CLI + Web Admin — Phase Status

| Phase | Version | Name | Status |
|-------|---------|------|--------|
| L1 | v1.0.0 | Infrastructure (platform.h, ICY 2.2 fields, build system) | **COMPLETE** |
| L2 | v1.1.1 | HTTP/HTTPS Admin Server + Web UI | **COMPLETE** |
| L3 | v1.2.0 | Audio Encoding + Streaming Engine | **IN PROGRESS** |
| L4 | v1.3.0 | DSP Chain + Pro Media Library + AI Playlist Generator | PLANNED |
| L5 | v1.4.0 | Mcaster1DNAS Integration + Live Dashboard | PLANNED |
| L6 | v1.5.0 | Analytics, Metrics, Listener Engagement | PLANNED |

---

## Linux Phase 3 — Audio Encoding + Streaming Engine `v1.2.0` `IN PROGRESS`

**Goal:** Wire the HTTP admin server to real audio — live device capture, playlist file
streaming, multi-format encoding, and live broadcast to Icecast2 / Mcaster1DNAS.

---

### 3.1 — Audio Input Abstraction

A unified `AudioSource` interface with two backends:

#### Live Device Capture (PortAudio)
| Backend | Library | Notes |
|---------|---------|-------|
| ALSA | `pa_alsa` | Native Linux kernel audio — works everywhere |
| PulseAudio | `pa_pulse` | User-space audio server — most desktop distros |
| PipeWire | via PulseAudio compat | Modern replacement — Fedora/Ubuntu 22+/Arch |
| JACK | `pa_jack` | Pro audio / low-latency — DAW integrations |
| Monitor (loopback) | PulseAudio monitor source | Capture what's playing — virtual cable equivalent |

- Device enumeration at startup → available in web UI dropdown
- Sample rates: 44100 / 48000 / 88200 / 96000 / 176400 / 192000 Hz
- Resample stage: high-rate capture → 44100/48000 encoder input (mc1_res_* already done)

#### Playlist File Streaming
- Decode engine: **libmpg123** (MP3) + **libavcodec** (OGG/FLAC/Opus/AAC/M4A/WAV)
- PCM float output → same pipeline as live capture
- Track advance: gapless or with crossfade (Phase 4)
- TagLib metadata on track load → auto-push ICY `StreamTitle=Artist - Title`

### 3.2 — Playlist Parser

Support all four formats used in broadcasting:

| Format | Extension | Notes |
|--------|-----------|-------|
| Extended M3U | `.m3u`, `.m3u8` | `#EXTINF` duration + title, relative/absolute paths |
| Winamp PLS | `.pls` | `File1=`, `Title1=`, `Length1=` |
| XSPF | `.xspf` | XML Shareable Playlist Format, W3C |
| Plain paths | `.txt` | One absolute file path per line |

- HTTP(S) remote URLs supported in playlist entries (stream relay)
- Shuffle, repeat-all, repeat-one modes
- Hot-reload: reload playlist without restarting the encoder slot

### 3.3 — Multi-Format Encoding Pipeline

Per-slot encoding state machine (`EncoderSlot`):

```
AudioSource (PCM float 32-bit)
      ↓
 [Volume Scaling]               ← g_recVolumeFactor, same as Windows
      ↓
 [Resample if needed]           ← mc1_res_* already ported
      ↓
 [Codec Encoder]                ← one of the below
      ↓
 [Icecast/DNAS SOURCE client]
      ↓
 [Archive Writer]               ← optional simultaneous WAV/MP3 file
```

| Codec | Library | Modes |
|-------|---------|-------|
| MP3 | `libmp3lame` | CBR (32–320), VBR (V0–V9), ABR |
| Ogg Vorbis | `libvorbis` + `libogg` | Quality -1–10, bitrate managed |
| Opus | `libopusenc` | 6–512 kbps, OPUS_APPLICATION_AUDIO/RESTRICTED_LOWDELAY |
| FLAC | `libFLAC` | Lossless, compression 0–8 |
| AAC-LC / HE-AAC v1 / v2 | `libfdk-aac` | ADTS + ADIF containers |

### 3.4 — Icecast / Mcaster1DNAS SOURCE Client

Full broadcast protocol stack:

| Protocol | Use |
|----------|-----|
| **Icecast2 HTTP PUT** | Modern — HTTP PUT `/mount`, `Authorization: Basic`, ICY headers |
| **Shoutcast v1 ICY SOURCE** | Legacy — `SOURCE /mount HTTP/1.0`, password-only |
| **Shoutcast v2 ULTRAVOX** | Extended — for Shoutcast DNAS v2+ |

Features:
- ICY 2.2 extended headers injected at SOURCE connect time
- Inline `StreamTitle=` metadata blocks at `Icy-MetaInt` byte boundaries
- Auto-reconnect watchdog: configurable retry interval (default 5s), exponential backoff
- Connection health: bytes sent, connection duration, listener count from stats endpoint
- Multiple server targets per encoder slot (simulcast to DNAS + Icecast simultaneously)

### 3.5 — Audio Archive

- Simultaneous WAV/MP3 archive output alongside the live stream
- Filename templating: `{station}_{YYYY-MM-DD}_{HH-MM-SS}.{ext}`
- Podcast RSS generated on archive close (`podcast_rss_gen.cpp` already ported)
- ICY 2.2 metadata embedded in RSS `<item>` on generation

### 3.6 — Web Admin API — Encoder Control

New endpoints added on top of the Phase L2 base:

```
POST /api/v1/encoders/{slot}/start          start encoding + streaming
POST /api/v1/encoders/{slot}/stop           stop gracefully
POST /api/v1/encoders/{slot}/restart        reconnect without full stop
GET  /api/v1/encoders/{slot}/stats          listeners, bitrate, uptime, ICY info
PUT  /api/v1/metadata                       push StreamTitle= to all live slots
PUT  /api/v1/volume                         apply gain in audio pipeline
GET  /api/v1/devices                        list PortAudio input devices
GET  /api/v1/playlist                       current playlist + position
POST /api/v1/playlist/skip                  advance to next track
POST /api/v1/playlist/load                  load playlist file by path
```

Dashboard updates:
- Live VU meter (via WebSocket or SSE polling)
- Start/stop buttons per encoder slot
- Track progress bar for file streaming
- Real listener count from server stats API

### 3.7 — New Source Files

```
src/linux/
  audio_source.h/cpp        AudioSource interface + PortAudio backend
  file_source.h/cpp         File streaming source (libmpg123 + libavcodec)
  playlist_parser.h/cpp     M3U / PLS / XSPF / TXT parser
  encoder_slot.h/cpp        Per-slot state machine (idle/connecting/live/error)
  stream_client.h/cpp       Icecast2 / Shoutcast SOURCE client
  archive_writer.h/cpp      WAV + MP3 simultaneous archive output
  audio_pipeline.h/cpp      Master pipeline: source → DSP → encoders → archive
```

### 3.8 — Phase 3 Dependencies

```bash
sudo apt install \
  libportaudio2 portaudio19-dev \
  libmpg123-dev \
  libavformat-dev libavcodec-dev libavutil-dev libswresample-dev \
  libmp3lame-dev \
  libvorbis-dev libogg-dev \
  libopus-dev libopusenc-dev \
  libflac-dev \
  libfdk-aac-dev \
  libtag1-dev
```

---

## Linux Phase 4 — DSP Chain + Pro Media Library + AI Playlist `v1.3.0`

**Goal:** Transform the encoder from a stream relay tool into a full broadcast automation
platform — professional DSP processing, a media library à la SAM Broadcaster / RadioBoss,
and an AI-driven playlist engine ("Pulse") that selects tracks based on context, mood,
news, themes, and listener engagement.

---

### 4A — DSP Processing Chain

Inserted between the audio source and encoder pool. All DSP operates on
PCM float32 stereo at 44100 or 48000 Hz.

```
[AudioSource: PCM float32]
         ↓
┌────────────────────────────────────────────┐
│              DSP Chain                     │
│                                            │
│   [10-band Parametric EQ]                 │
│         ↓                                  │
│   [AGC / Compressor / Limiter]            │
│         ↓                                  │
│   [Crossfader]  ←── next track pre-roll   │
└────────────────────────────────────────────┘
         ↓
[Encoder Pool: MP3 / Opus / OGG / FLAC / AAC]
```

#### 4A.1 — Parametric EQ

- **10 bands**: low shelf, 8 parametric mid bands, high shelf
- **Per band**: center frequency (20 Hz – 20 kHz), gain (±24 dB), Q factor (0.1 – 10.0)
- **Algorithm**: RBJ Audio EQ Cookbook biquad IIR filters — pure C++, no library needed
- **Pre-gain compensation**: auto-attenuate input to prevent inter-stage clipping
- **Bypass**: per-band enable/disable toggle
- **Presets** (stored in YAML):
  - Flat (bypass all)
  - Spoken Word (boost 200–3kHz, cut 80Hz, cut 10kHz)
  - Vocal Presence (+3dB 2–5kHz)
  - Bass Boost (+6dB 60–100Hz)
  - Treble Air (+4dB 10–16kHz)
  - Broadcast Standard (gentle smile curve)
  - Rock / Club / Classical / Late Night
- **Web UI**: interactive EQ curve graph (SVG canvas), per-band sliders

#### 4A.2 — AGC / Compressor / Limiter

Three-stage loudness chain:

| Stage | Function |
|-------|----------|
| **AGC** | Long-term level normalization — target loudness tracking |
| **Compressor** | Dynamic range reduction — punchy, consistent output |
| **Limiter** | Hard ceiling — prevent clipping before encoder |

Parameters:
- **Threshold**: -60 – 0 dBFS (compressor knee point)
- **Ratio**: 1:1 – ∞:1 (1:1 = bypass, ∞:1 = limiter)
- **Attack**: 1 ms – 500 ms (how fast gain reduces on loud signal)
- **Release**: 10 ms – 5000 ms (how fast gain recovers)
- **Makeup gain**: 0 – +24 dB (auto or manual)
- **Target loudness**: -23 LUFS (EBU R128) / -16 LUFS (streaming) / -14 LUFS (podcasts)
- **Lookahead**: 5–20 ms (prevents transient clipping)
- **Limiter ceiling**: -1.0 dBFS (hard clip prevention before encoder)

Library: **libebur128** (BSD-2-Clause) for integrated LUFS measurement.

Web UI: Gain reduction meter, loudness histogram, LUFS readout.

#### 4A.3 — Crossfader

Professional track-to-track transition engine:

| Setting | Range | Default |
|---------|-------|---------|
| Fade duration | 0 – 15 seconds | 3 s |
| Curve type | Linear / Equal-power / S-curve | Equal-power |
| Trigger | Auto (track end) / Silence detect / Manual | Auto |
| Pre-roll buffer | 2 – 10 seconds | 5 s |
| Intro protection | 0 – 30 seconds | 8 s |

Features:
- **Equal-power crossfade**: constant perceived loudness through transition
  (`gain_out = cos(t × π/2)`, `gain_in = sin(t × π/2)`)
- **Silence detection**: auto-trigger crossfade on trailing silence (configurable threshold)
- **Pre-roll**: next track decoded and buffered before current track ends — zero-gap
- **Intro protection**: don't crossfade over a track's vocal intro (configurable per-track)
- **BPM-aware mode**: extend/compress crossfade to align beat grids (requires Phase 4B BPM data)
- **Live assist override**: operator triggers crossfade from web UI at any time
- **Cue point markers**: per-track `cue_in` / `cue_out` timestamps stored in media library

Web UI: visual crossfade waveform preview, timeline with cue points.

---

### 4B — Pro Media Library Manager

SQLite-backed track database with TagLib/aubio metadata extraction.
Web UI resembles SAM Broadcaster / RadioBoss track browser.

#### Database Schema (SQLite)

```sql
tracks:
  id, file_path, title, artist, album, genre, year, track_number,
  bpm, musical_key, energy_level (0.0–1.0), mood_tag,
  duration_ms, bitrate, samplerate, channels, filesize,
  intro_duration_ms, outro_duration_ms, cue_in_ms, cue_out_ms,
  date_added, date_modified, date_last_scanned,
  rating (1–5), play_count, last_played_at,
  cover_art_hash, is_missing, is_jingle, is_sweeper, is_spot,
  isrc, musicbrainz_id, custom_tags_json

play_history:
  id, track_id, played_at, slot, duration_played_ms,
  listeners_at_play, crossfade_in, crossfade_out, skipped

playlists:
  id, name, type (static/smart/ai/clock_hour), description,
  created_at, modified_at, rule_json, track_count

playlist_tracks:
  id, playlist_id, track_id, position, added_at, weight

station_rotation_rules:
  id, name, artist_separation, song_separation_hours,
  genre_ratios_json, jingle_every_n, spot_on_hour, spot_on_half

clock_hours:
  id, hour (0–23), day_of_week (0–6), segment_json
```

#### Library Features

| Feature | Implementation |
|---------|---------------|
| **Directory scan** | Recursive, inotify watch for live changes |
| **Metadata extraction** | TagLib — ID3v1/v2 (MP3), Ogg comments, FLAC, Opus, M4A |
| **Duration + bitrate** | libavformat probe |
| **BPM detection** | aubio tempo analysis at scan time |
| **Key detection** | aubio Krumhansl-Schmuckler key profile |
| **Energy level** | RMS loudness computed at scan time (0.0 = silence, 1.0 = max) |
| **Mood tag** | Derived: BPM + key + energy → happy/sad/energetic/calm/romantic/angry |
| **Cover art** | Extract from tag → thumbnail cache (JPEG, 200×200px) |
| **Duplicate detection** | MD5 file hash + metadata fingerprint comparison |
| **Missing file detection** | Flagged at scan time, shown in UI |
| **Intro/outro silence** | Scan first/last 15s for silence onset → auto-set cue points |
| **Mass tag edit** | Multi-select rows → apply field values to selection |
| **Export** | Generate M3U/PLS/XSPF/TXT from any filtered view |
| **Import** | Bulk import from existing playlist files |

#### Web UI — Library Browser

- Sortable/filterable grid: Title, Artist, Album, Genre, Year, BPM, Duration, Energy, Mood, Rating, Plays
- Cover art thumbnail column
- Click row → detail panel: full metadata, waveform preview, cue point editor
- Drag-to-queue: drag tracks from library directly into Pulse queue
- Smart filter: "energy > 0.7 AND genre = rock AND NOT played_in_last_4h"
- Batch operations: rate, tag, set cue points, exclude from rotation
- Library statistics: total tracks, total duration, by-genre breakdown, missing files

---

### 4C — Pulse: AI Playlist Generator

**"Pulse"** — the context-aware, edge-algorithm playlist engine.
Selects the right track at the right moment based on multi-signal context.

#### Architecture

```
╔══════════════════════════════════════════════════════════════╗
║                    Context Engine                            ║
║                                                              ║
║  Time/Season ──┐                                             ║
║  Mood Preset ──┤                                             ║
║  Daily Theme ──┤──► Context Vector (weighted signals)        ║
║  News Sentiment┤                                             ║
║  Blog/Post NLP ┤                                             ║
║  Weather API ──┤                                             ║
║  Engagement ───┘                                             ║
╚════════════════════╤═════════════════════════════════════════╝
                     ↓
╔════════════════════╧═════════════════════════════════════════╗
║                   Track Scorer                               ║
║                                                              ║
║  score = Σ wᵢ × featureᵢ                                   ║
║                                                              ║
║  • energy_match     (context energy vs track energy)         ║
║  • mood_match       (context mood vs track mood tag)         ║
║  • bpm_fit          (target BPM range match)                 ║
║  • genre_quota      (genre ratio enforcement)                ║
║  • artist_sep       (-∞ if artist played < N tracks ago)     ║
║  • song_sep         (-∞ if song played < M hours ago)        ║
║  • recency_penalty  (prefer less-recently played)            ║
║  • rating_boost     (operator 1–5 star rating)               ║
║  • engagement_boost (ICY 2.2 listener retention signal)      ║
╚════════════════════╤═════════════════════════════════════════╝
                     ↓
           Top-N candidates → Queue (5–20 tracks ahead)
                     ↓
     Crossfader → Encoder Pool → Live Stream
```

#### Context Signals

| Signal | Source | Notes |
|--------|--------|-------|
| **Time of day** | System clock | Morning: +energy; Drive time: +energy; Evening: -energy; Late night: -energy |
| **Day of week** | System clock | Weekend = +energy; Monday morning = gentle ramp |
| **Season** | System clock | Winter holidays override, summer upbeat |
| **Mood preset** | Web UI selector | Party / Chill / Drive Time / Late Night / Morning Show / Worship / Oldies / News Break |
| **Daily theme** | Web UI text field | Operator types "today we're talking about spring gardening" → NLP → calm/organic tracks |
| **News sentiment** | RSS feed parser | Serious breaking news → lower energy, avoid upbeat pop; positive news → boost energy |
| **Blog/post text** | URL poll or webhook | Station's daily blog post text → NLP topic + sentiment extraction |
| **Weather** | OpenWeatherMap API | Rainy/overcast → mellower; sunny → upbeat |
| **Listener engagement** | ICY 2.2 signals | Which tracks retained listeners → boost their score |

#### Mood ↔ Music Feature Mapping

| Mood | Energy | BPM Range | Preferred Keys | Genres |
|------|--------|-----------|---------------|--------|
| Energetic / Party | 0.8–1.0 | 120–180 | Major | Rock, Pop, Dance |
| Drive Time | 0.6–0.85 | 100–140 | Major | Rock, Country, Pop |
| Morning Show | 0.5–0.75 | 90–130 | Major | Pop, Light Rock, Country |
| Chill / Late Night | 0.2–0.5 | 60–100 | Minor/Major | Soft Rock, Acoustic, Jazz |
| Worship | 0.3–0.7 | 70–110 | Major | Christian, Gospel |
| Oldies Hour | 0.4–0.7 | 100–140 | Major | Classic Rock, 60s/70s/80s |
| News Break | 0.1–0.3 | 60–90 | Minor | Instrumental, Acoustic |
| Serious / Grief | 0.1–0.3 | 50–80 | Minor | Slow Ballads, Classical |

#### Rotation Rules

- **Artist separation**: no same artist within N tracks (default: 3 tracks, configurable)
- **Song separation**: same song not repeated within M hours (default: 4 hours)
- **Genre ratios**: e.g., 60% Christian / 30% Country / 10% Pop — enforced per clock hour
- **Jingles / sweepers**: inserted every N tracks automatically
- **Traffic spots**: on the hour + half-hour (configurable)
- **Clock hour templates**: define segment-by-segment for each hour of the day (Sunday 8am = worship; Friday 5pm = drive time)
- **Intro hour rule**: first track of each clock hour = high energy, strong opener, full song (not recently played)

#### LLM Integration (Optional)

Two backend options, operator's choice:

| Backend | Model | Cost | Quality |
|---------|-------|------|---------|
| **Ollama (local)** | llama3.2, mistral, gemma2 | Free — runs on same server | Good |
| **Claude API** | claude-haiku-4-5 (fast/cheap) or claude-sonnet-4-6 | Per-token | Excellent |

Use cases:
- **Natural language playlist**: "Create a 2-hour Sunday morning gospel show with 3 worship sets"
- **Theme analysis**: LLM reads today's blog post → extracts musical theme recommendations
- **News sentiment**: LLM classifies news headlines → high/low energy context signal
- **Smart rule generation**: "Play only tracks from the 90s on Friday afternoons" → auto-generates rule_json

LLM receives: condensed track catalog (title/artist/genre/bpm/mood/energy), context vector, rotation rules → returns ordered track ID list with brief reasoning.

#### Web UI — Pulse Dashboard

- **Now Playing**: cover art, title, artist, energy bar, BPM, key, mood tag
- **Up Next**: 5-track queue preview with drag-to-reorder
- **Context Panel**: active mood preset, today's theme, news sentiment indicator, weather, time slot
- **Manual Override**: operator drags tracks from library into queue, Pulse fills remaining slots
- **Skip**: instant crossfade to next track
- **History**: last 50 tracks played — listener count at each change, rating prompt
- **Pulse Settings**: signal weights sliders, rotation rules config, genre ratio pie chart, LLM enable/disable

---

### 4D — Phase 4 New Source Files

```
src/linux/
  dsp/
    eq.h/cpp                10-band biquad IIR parametric EQ
    agc.h/cpp               AGC / compressor / limiter chain
    crossfader.h/cpp        Track crossfade engine with pre-roll
    dsp_chain.h/cpp         Master DSP pipeline orchestrator

  medialib/
    database.h/cpp          SQLite schema + CRUD + migrations
    scanner.h/cpp           Directory scanner + inotify watcher
    tag_extractor.h/cpp     TagLib metadata + libavformat probe
    bpm_analyzer.h/cpp      aubio BPM + musical key detection
    energy_analyzer.h/cpp   RMS energy level + silence detection
    cover_art.h/cpp         Cover art extract + thumbnail cache
    mood_classifier.h/cpp   BPM + key + energy → mood tag

  pulse/
    context_engine.h/cpp    Multi-signal context aggregator
    track_scorer.h/cpp      Candidate scoring (weighted features)
    rotation_enforcer.h/cpp Artist/song separation + quota rules
    clock_scheduler.h/cpp   Clock hour templates + segment rules
    news_feed.h/cpp         RSS fetch + headline sentiment
    llm_client.h/cpp        Ollama + Claude API integration
    weather_client.h/cpp    OpenWeatherMap API (optional)

  web_ui/
    medialib.html/js        Media library browser
    pulse.html/js           AI playlist generator + context UI
    dsp.html/js             EQ / AGC / crossfade controls
```

### 4E — New REST API Endpoints

```
# Media Library
GET    /api/v1/library/stats              totals, by-genre counts, missing files
POST   /api/v1/library/scan               trigger rescan of configured directories
GET    /api/v1/library/tracks             paginated: ?page=1&limit=50&genre=rock&sort=bpm
GET    /api/v1/library/tracks/{id}        full track detail + play history
PUT    /api/v1/library/tracks/{id}        update metadata, rating, cue points
DELETE /api/v1/library/tracks/{id}        remove from library (not from disk)
GET    /api/v1/library/search             full-text search: ?q=artist:queen

# Playlists
GET    /api/v1/playlists                  list all playlists
POST   /api/v1/playlists                  create static/smart/ai playlist
GET    /api/v1/playlists/{id}             playlist + tracks
PUT    /api/v1/playlists/{id}             update rules or track order
DELETE /api/v1/playlists/{id}

# Pulse AI Engine
GET    /api/v1/pulse/queue                current play queue (5–20 tracks ahead)
PUT    /api/v1/pulse/queue                reorder / insert / remove from queue
POST   /api/v1/pulse/skip                 crossfade to next track immediately
GET    /api/v1/pulse/context              current context vector
PUT    /api/v1/pulse/context              set mood preset, theme text, weather override
POST   /api/v1/pulse/generate             generate N-track playlist from context
POST   /api/v1/pulse/nlq                  natural language query → playlist

# DSP Chain
GET    /api/v1/dsp/eq                     current EQ band settings
PUT    /api/v1/dsp/eq                     update bands / load preset
GET    /api/v1/dsp/agc                    AGC / compressor parameters
PUT    /api/v1/dsp/agc                    update AGC settings
GET    /api/v1/dsp/crossfade              crossfade settings
PUT    /api/v1/dsp/crossfade              update crossfade settings
GET    /api/v1/dsp/meters                 real-time loudness/gain-reduction readings (SSE)
```

### 4F — Phase 4 Dependencies

```bash
sudo apt install \
  libsqlite3-dev \
  libtag1-dev \
  libebur128-dev \
  libaubio-dev \
  libavformat-dev libavcodec-dev libavutil-dev libswresample-dev \
  libcurl4-openssl-dev \
  libjpeg-dev

# Optional: local LLM backend
# curl -fsSL https://ollama.com/install.sh | sh
# ollama pull llama3.2
```

---

## Release Version Plan

### Windows

| Tag | Phase | Description |
|-----|-------|-------------|
| `v0.1.0` | 1 | VS2022 build fixes |
| `v0.2.0` | 2 | Full rebrand, native LAME |
| `v0.3.0` | 3 | Opus, HE-AAC, PortAudio/ASIO |
| `v0.4.0` | 4 | YAML multi-station config |
| `v0.5.0` | 5 | ICY 2.2 metadata + Podcast RSS |
| `v0.6.0` | 6 | Mcaster1DNAS deep integration |
| `v1.0.0` | 7 | Mcaster1Castit platform |
| `v1.1.0` | 8 | Analytics + engagement |

### Linux CLI + Web Admin

| Tag | Phase | Description |
|-----|-------|-------------|
| `v1.0.0` | L1 | Infrastructure, build system, ICY 2.2 structs |
| `v1.1.1` | L2 | HTTP/HTTPS admin server + web UI — **current** |
| `v1.2.0` | L3 | PortAudio capture + file streaming + encoding + Icecast client |
| `v1.3.0` | L4 | DSP (EQ/AGC/crossfade) + Media Library + Pulse AI playlist |
| `v1.4.0` | L5 | Mcaster1DNAS deep integration |
| `v1.5.0` | L6 | Analytics, listener metrics, engagement platform |

---

## Contributing

Issues, pull requests, and discussion are open on GitHub.
See `FORKS.md` for the rationale behind this project.
See `CREDITS.md` for attribution and project history.
