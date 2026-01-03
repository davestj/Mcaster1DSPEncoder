# Mcaster1DSPEncoder — Development Roadmap

**Maintainer:** Dave St. John <davestj@gmail.com>
**Repository:** https://github.com/davestj/Mcaster1DSPEncoder
**Last Updated:** 2026-02

---

## Current Status at a Glance

| Phase | Name | Status |
|-------|------|--------|
| 1 | VS2022 Build Fix | **COMPLETE** |
| 2 | Rebrand & Refactor | **COMPLETE** |
| 3 | Audio Backend Modernization | **IN PROGRESS** |
| 4 | YAML Multi-Station Configuration | PLANNED |
| 5 | ICY 2.1 Enhanced Metadata Protocol | PLANNED |
| 6 | Mcaster1DNAS Integration | PLANNED |
| 7 | Mcaster1Castit Integration | PLANNED |
| 8 | Analytics, Metrics & Platform Engagement | PLANNED |

---

## Phase 1 — VS2022 Build Fix `COMPLETE`

**Goal:** Get all 4 build targets compiling and running under Visual Studio 2022.

All issues blocking compilation under VS2022 (v143 toolset, Windows 10 SDK) were resolved.

**Completed:**
- foobar2000 SDK: `_WIN32_WINNT` updated `0x0501` → `0x0601` — resolved `_REASON_CONTEXT` C2011
- `pfc/alloc.h`: Fixed `alloc_fixed::alloc::move_from()` for types without `has_owned_items`
- `foobar2000/SDK/guids.cpp`: Fixed `FOOGUIDDECL` / `__declspec(selectany)` LNK2001 mass errors
- All vcxproj: Unified CRT to consistent `/MD` — eliminated 0xC0000409 crash on Windows 10+

**Outputs:** All 4 targets building clean — `Mcaster1DSPEncoder.exe`, `dsp_mcaster1.dll`,
`dsp_mcaster1_radiodj.dll`, `foo_mcaster1.dll`

---

## Phase 2 — Rebrand & Refactor `COMPLETE`

**Goal:** Complete rename of every AltaCast identity. No new features.

**Completed:**
- 150+ string/symbol instances renamed: AltaCast → Mcaster1DSPEncoder throughout
- All source files, folders, vcxproj, sln files renamed
- Native LAME MP3 (`lame.h` API) replaces obsolete `BladeMP3EncDLL.h`
- ResizableLib integrated — all dialogs resizable with anchor-based layout
- Modern dialog styling: Segoe UI 9pt font, `WS_THICKFRAME`, flat look
- Full attribution to Ed Zaleski preserved in About dialog and source headers

**Outputs:** Clean build with Mcaster1DSPEncoder identity throughout

---

## Phase 3 — Audio Backend Modernization `IN PROGRESS`

**Goal:** Remove EOL dependencies. Add Opus, HE-AAC, dual MP3 paths. Replace bass.dll
with PortAudio for full device coverage including WASAPI loopback and ASIO.

### Codec Work
- [x] LAME MP3 — native C API, CBR / VBR (V0–V9) / ABR
- [x] LAME ACM — runtime Windows ACM codec enumeration
- [x] Opus — `libopusenc` integration, OGG container
- [x] AAC-LC / HE-AAC v1 / HE-AAC v2 — `fdk-aac` replaces legacy `libfaac`
- [x] WMA removed — `basswma.dll` dependency eliminated
- [x] Ogg Vorbis updated — `libvorbis 1.3.7`
- [x] FLAC updated — `libFLAC 1.5.0`

### Device Capture
- [x] PortAudio statically linked — WASAPI / DirectSound / MME / WDM-KS / ASIO
- [x] WASAPI loopback — capture what's playing (virtual audio cable use case)
- [x] ASIO — professional hardware, built with Steinberg ASIO SDK
- [x] Sample rate UI: 44100 / 48000 / 88200 / 96000 / 176400 / 192000 Hz
- [x] Resample stage: high-rate capture → 44100/48000 encoder input

### Pending Verification
- [ ] Opus stream end-to-end test — Icecast + VLC
- [ ] AAC ADTS stream end-to-end test — Icecast + compatible players
- [ ] 192kHz ASIO capture → 48kHz Opus encode verified
- [ ] All format streams verified on both Win32 and x64 builds

---

## Phase 4 — YAML Multi-Station Configuration `PLANNED`

**Goal:** Replace legacy flat `key=value` config with structured YAML supporting multiple
simultaneous stations, per-station encoder chains, and archive settings.

`libyaml 0.2.5` is already installed in vcpkg — ready to integrate.

**Planned:**
- YAML loader: `config_yaml.cpp` / `config_yaml.h` in `libmcaster1dspencoder`
- Schema: `global` settings + `stations[]` array with nested `icy`, `server`,
  `encoders[]`, and `archive` blocks
- `MAX_ENCODERS` increased 10 → 32
- Multi-station UI: station list in `Mcaster1DSPEncoder.exe`
- Legacy `key=value` retained as auto-detected fallback
- Per-station ICY metadata — no more copy-paste across numbered files
- Save-to-WAV archive per station with filename pattern templating

**Sample config shape:**
```yaml
global:
  log_level: info
  reconnect_delay: 5
stations:
  - id: 1
    name: "My Rock Station"
    server: { type: icecast2, host: stream.example.com, port: 8000 }
    encoders:
      - { format: mp3, bitrate: 128 }
      - { format: opus, bitrate: 128 }
```

---

## Phase 5 — ICY 2.1 Enhanced Metadata Protocol `PLANNED`

**Goal:** Implement and publish the ICY 2.1 metadata extension on both encoder
(Mcaster1DSPEncoder) and server (Mcaster1DNAS) sides, enabling richer stream metadata
beyond what the legacy `StreamTitle=` inline metadata block supports.

### Background

The legacy ICY metadata protocol sends a plain text `StreamTitle=Artist - Title;` string
embedded in the audio stream at fixed intervals (`Icy-MetaInt` byte boundaries). This
works for basic now-playing display but has no support for:

- Album art / cover images
- Track duration and playback position
- ISRC / MusicBrainz / Spotify identifiers for service lookup
- Ratings, listener engagement signals
- Multi-language metadata
- Structured JSON payloads

ICY 2.1 extends this with:

### ICY 2.1 Feature Set (Planned)

| Feature | Description |
|---------|-------------|
| **JSON metadata blocks** | Full JSON payload option alongside legacy `StreamTitle=` for backward compatibility |
| **Album art embedding** | Base64-encoded thumbnail in metadata block, or HTTP URL reference for larger art |
| **Track identifiers** | ISRC, MusicBrainz Track ID, Spotify URI fields for automated service lookup |
| **Duration + position** | Track length and elapsed time for player progress bars |
| **Station branding** | Logo URL, station color scheme, social handles |
| **Engagement signals** | Like/reaction metadata for listener platform integration |
| **History API linkage** | `history_url` field pointing to the Mcaster1DNAS song history API endpoint |
| **Backward compatibility** | ICY 2.1 blocks include legacy `StreamTitle=` so all existing players continue working |

### Integration Points

- **Mcaster1DSPEncoder** sends ICY 2.1 metadata to Mcaster1DNAS on `PUT /metadata`
- **Mcaster1DNAS** stores the structured metadata in its in-memory ring buffer
  (the song history API introduced in v2.5.1-rc1)
- **Mcaster1DNAS** relays ICY 2.1 blocks to ICY 2.1-aware listeners; falls back to
  legacy `StreamTitle=` for legacy players
- **Track History pages** in Mcaster1DNAS already consume the ring buffer — ICY 2.1
  adds richer data to those pages (cover art, streaming service links, etc.)

---

## Phase 6 — Mcaster1DNAS Integration `PLANNED`

**Goal:** First-class integration between Mcaster1DSPEncoder and the Mcaster1DNAS
streaming server — making them a matched pair that works better together than either
does with generic third-party tools.

**Planned integrations:**

- **Auto-discovery** — Mcaster1DSPEncoder detects local Mcaster1DNAS instances on the
  LAN and offers them in a server picker (mDNS / simple UDP broadcast)
- **Shared YAML config** — encoder and DNAS share a common `mcaster1.yaml` with
  station definitions, eliminating duplicate configuration
- **Live stream health** — DNAS reports listener count, bitrate stability, and buffer
  state back to the encoder UI in real time
- **Song history push** — encoder pushes structured now-playing data directly to the
  DNAS ring buffer API on each track change (no polling delay)
- **Admin API auth** — encoder uses the same API key as the DNAS admin panel; no
  separate credential management
- **Relay/fallback coordination** — encoder can trigger a DNAS relay switch if the
  live source drops, and resume when the source reconnects

---

## Phase 7 — Mcaster1Castit `PLANNED`

**Goal:** Resurrect and modernize **Castit** — the original live broadcast scheduling
and automation tool built for casterclub.com in 2001 with Ed Zaleski's guidance.
Castit was written against VC6 and will be upgraded to Visual Studio 2022 as a new
first-class member of the Mcaster1 ecosystem.

### Castit Upgrade Plan

| Component | Old | New |
|-----------|-----|-----|
| Compiler | VC6 | VS2022 (v143 toolset) |
| XML processing | bundled libxml2 (VC6) | `libxml2:x86-windows` via vcpkg |
| HTTP client | custom / early libcurl | `libcurl:x86-windows` via vcpkg (modern TLS/HTTP2) |
| Database | MySQL (direct client) | **TBD:** `libmariadb` (preferred — open-source aligned) or `libmysql` via vcpkg |
| UI framework | MFC VC6 dialogs | MFC VS2022 + ResizableLib (matching Mcaster1DSPEncoder) |
| Configuration | legacy flat config | YAML via `libyaml` (matching Mcaster1DSPEncoder) |
| Encoding integration | Castit-internal | Delegates to Mcaster1DSPEncoder encoder engine |

### Mcaster1Castit Feature Goals

- Playlist scheduling: time-based and event-triggered playback
- Live source injection: connect a live source mid-schedule
- Integration with Mcaster1DNAS for stream status and listener stats
- Integration with Mcaster1DSPEncoder for encoding and stream health
- ICY 2.1 metadata injection from the playlist schedule (track changes push metadata)

> **Database choice:** MariaDB client (`libmariadb`) is preferred over the Oracle MySQL
> client — it is a true open-source drop-in replacement with no Oracle license constraints,
> active community development, and the same wire protocol. This will be revisited before
> implementation begins.

---

## Phase 8 — Analytics, Metrics & Platform Engagement `PLANNED`

**Goal:** Leverage the ICY 2.1 protocol and the Mcaster1DNAS song history ring buffer
to build a statistical metrics and analytics layer that gives station operators
actionable listener engagement data — without requiring third-party analytics services.

### Metrics Platform Architecture

```
[Listeners] ──ICY 2.1 engagement signals──► [Mcaster1DNAS]
                                                    │
                                          [Analytics Ring Buffer]
                                                    │
                                   ┌────────────────┼─────────────────┐
                                   ▼                ▼                 ▼
                          [Track Play Stats]  [Listener Peaks]  [Format Stats]
                                   │
                          [Mcaster1Castit Scheduler]
                                   │
                         [Optimize playlist rotation
                          based on engagement data]
```

### Planned Metrics

| Metric | Source | Use |
|--------|--------|-----|
| Track play count | DNAS ring buffer | Most-played tracks, rotation analysis |
| Listener count at track change | DNAS stats API | Measure which tracks retain listeners |
| Format popularity | DNAS mount stats | Which codec/bitrate combinations listeners prefer |
| Peak hours | DNAS listener timeseries | Schedule high-value content at peak times |
| Tune-in / tune-out | ICY 2.1 engagement signal | Identify tracks that cause listeners to leave |
| Music service click-through | Track History page | Which tracks generate service lookup (Spotify/Apple) |

### Platform Engagement via ICY 2.1

The ICY 2.1 metadata block includes a `history_url` field pointing to the DNAS track
history API. Listener-side players that support ICY 2.1 can display:

- A clickable "now playing" card with album art and service links
- A scrollable track history for the current session
- A "add to playlist" action via Spotify / Apple Music deep link

This creates a direct engagement path from the audio stream to music service platforms,
measurable through the `history_url` click analytics on the DNAS side.

---

## Release Version Plan

| Tag | Phase | Description |
|-----|-------|-------------|
| `v0.1.0` | Phase 1 | VS2022 build fixes — all 4 targets compile |
| `v0.2.0` | Phase 2 | Full rebrand, native LAME, ResizableLib UI |
| `v0.3.0` | Phase 3 | Opus, HE-AAC, PortAudio/ASIO, WMA removed |
| `v0.4.0` | Phase 4 | YAML multi-station config, 32 encoder slots |
| `v0.5.0` | Phase 5 | ICY 2.1 metadata protocol |
| `v0.6.0` | Phase 6 | Mcaster1DNAS deep integration |
| `v1.0.0` | Phase 7 | Mcaster1Castit + full Mcaster1 platform |
| `v1.1.0` | Phase 8 | Analytics, metrics, platform engagement |

---

## Contributing

Issues, pull requests, and discussion are open on GitHub.
See `FORKS.md` for the rationale behind this project.
See `CREDITS.md` for attribution and project history.
