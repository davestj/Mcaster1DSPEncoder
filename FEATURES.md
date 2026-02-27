# FEATURES.md — Mcaster1DSPEncoder Qt 6 vs Legacy MFC

**What's new in the Qt 6 rebuild vs the legacy MFC encoder.**

The Mcaster1DSPEncoder Qt 6 build (`win-qt/`) is a complete rewrite of the legacy
Windows MFC encoder (`src/`). This document catalogs every feature added, improved,
or carried forward — and what the legacy version lacked.

---

## Feature Comparison Matrix

| Category | Feature | Legacy MFC | Qt 6 (Win/Mac) |
|----------|---------|:----------:|:--------------:|
| **Audio Codecs** | MP3 (LAME) | CBR only | CBR (VBR/ABR planned) |
| | Ogg Vorbis | VBR quality | VBR quality |
| | Opus | Bitrate | Bitrate, 48kHz auto-resample |
| | FLAC | Level 5 fixed | Level 0-8 configurable |
| | AAC-LC (fdk-aac) | Bitrate | Bitrate, afterburner |
| | HE-AAC v1 (SBR) | Bitrate | Bitrate, 24-128 kbps |
| | HE-AAC v2 (PS) | Bitrate | Bitrate, stereo enforced |
| | AAC-ELD | Bitrate | Bitrate, low-delay |
| **DSP Processing** | 10-Band Parametric EQ | -- | RBJ biquad IIR, ±24dB, presets |
| | 31-Band Graphic EQ | -- | Per-band L/R, linked mode |
| | AGC / Compressor / Limiter | -- | Soft knee, gate, makeup gain |
| | Sonic Enhancer (BBE clone) | -- | LR4 crossover, 3-band phase align |
| | PTT Push-to-Talk Ducking | -- | Mic resampling, attack/release |
| | DBX 286s Voice Processor | -- | Gate, comp, de-ess, LF/HF enhance |
| | Equal-Power Crossfader | -- | cos/sin curves, configurable duration |
| **Audio Input** | PortAudio devices | Basic | Native SR detection, per-encoder override |
| | File playback (playlist) | -- | MP3/OGG/FLAC/Opus/AAC/WAV/AIFF via FFmpeg |
| | Playlist formats | -- | M3U, M3U8, PLS, XSPF, TXT |
| | WASAPI loopback (system audio) | -- | Capture desktop audio, no virtual cables |
| | PTT microphone input | -- | Independent device, sample rate conversion |
| **Video Pipeline** | Camera capture | -- | Media Foundation + DirectShow |
| | Screen capture | -- | DXGI Desktop Duplication (GPU) |
| | Video file playback | -- | MF Source Reader (MP4/MKV/AVI/WMV/MOV) |
| | H.264 encoding | -- | MF MFT (NVENC/QSV/AMF hw, sw fallback) |
| | VP8/VP9 encoding | -- | libvpx (configurable bitrate/fps) |
| | Video effects (13 types) | -- | Brightness, blur, chroma key, PiP, LUT, etc. |
| | Text/image overlays | -- | Lower thirds, news ticker, watermarks, SRT |
| **Video Transitions** | Cut | -- | Instant swap |
| | Crossfade | -- | sRGB gamma-correct blending (LUT) |
| | Fade to Black | -- | Gamma-correct A → black → B |
| | Dip to White | -- | Gamma-correct A → white → B |
| | Wipe (4 directions) | -- | 24px feathered edges |
| | Push (left/right) | -- | A slides out, B slides in |
| | Iris Circle | -- | Circular reveal from center |
| | Dissolve | -- | FNV-1a per-pixel hash pattern |
| **Video Streaming** | RTMP (YouTube/Twitch) | -- | Full handshake, AMF0, chunking |
| | FLV muxing | -- | Video+audio tag muxing |
| | WebM (VP8/VP9 + Vorbis) | -- | Matroska container, Icecast delivery |
| | HLS (MPEG-TS) | -- | Segment generation, M3U8 playlist |
| **Virtual Camera** | DirectShow output | -- | Shared memory BGRA → filter DLL |
| | Works with Zoom/OBS/Teams | -- | COM registration, auto-enumeration |
| **Live Video Studio** | 3-source preview panes | -- | Camera, screen, image, video file |
| | Program monitor | -- | Composited output with transitions |
| | ON-AIR indicator | -- | Flashing red pill, window title, tray notify |
| | Stream Monitor (AIR/CUE) | -- | Decode live stream or raw preview |
| **Streaming** | Icecast2 | Source client | Source client + admin stats |
| | Shoutcast v1/v2 | Source client | Source client |
| | Mcaster1 DNAS | Source client | Source + DNAS slot poller |
| | ICY 1.x metadata | StreamTitle push | StreamTitle push |
| | ICY 2.2 extended | 50+ header fields | 50+ header fields |
| | Auto-reconnect | Timer-based | Watchdog thread, exponential backoff |
| **Metadata** | Manual entry | Text field | Dialog with title/artist/album/artwork |
| | External file source | First line read | First line read, configurable interval |
| | External URL source | HTTP fetch | HTTP fetch, configurable interval |
| | Window title grab | FindWindow | -- (not ported, replaced by file/URL) |
| | Lock metadata | Checkbox | Checkbox |
| | Per-encoder metadata | Shared | Independent per-encoder YAML |
| **Podcast / RSS** | RSS 2.0 + iTunes | Basic generation | Full RSS with MIME detection |
| | SFTP upload | -- | libssh2 upload to web server |
| | WordPress REST API | -- | PowerPress/Podlove/SSP plugins |
| | Buzzsprout API | -- | Token-based upload |
| | Transistor.fm API | -- | Pre-signed URL upload |
| | Podbean OAuth 2.0 | -- | Three-step authenticated upload |
| **Archive / Recording** | WAV archive | Basic | Timestamped filenames |
| | MP3 archive | Basic | Simultaneous WAV + MP3 |
| | Archive directory | Browse button | Configurable path, auto-create |
| **Configuration** | Format | YAML (migrated from .cfg) | YAML (per-encoder + global + DSP) |
| | Per-encoder profiles | Slot-numbered files | Named YAML profiles |
| | DSP presets | -- | Per-effect YAML persistence |
| | Global settings | -- | Window geometry, theme, paths |
| | Save on exit | -- | aboutToQuit + closeEvent (all paths) |
| **Platform** | Windows (x86) | MFC dialogs | -- (legacy only) |
| | Windows (x64) | -- | Qt 6 Widgets, VS2022 |
| | macOS | -- | Qt 6 Widgets, Autotools |
| | Linux | -- | CLI + PHP web UI, Autotools |
| | Taskbar badge | -- | ITaskbarList3 overlay icon |
| | Toast notifications | -- | Shell_NotifyIcon balloon |
| | Single-instance | -- | QLocalServer/QLocalSocket |
| | System tray | Basic icon | Full context menu, minimize on close |
| **UI** | Framework | MFC dialogs (Win32) | Qt 6 Widgets (cross-platform) |
| | VU meter | FlexMeters GDI custom | QWidget custom paint |
| | Encoder list | SysListView32 | QListView + custom model |
| | Config tabs | 4 property pages | 6 tabs (Basic, YP, Advanced, ICY22, Podcast, Effects) |
| | Preferences dialog | -- | 2 tabs (General, Paths) |
| | Log viewer | -- | Scrollable filtered event log |
| | Preset browser | -- | Load/save/manage DSP presets |
| | EQ visualizer | -- | Frequency response curve widget |
| | Theme support | -- | Dark theme (QSS) |
| **Plugins** | Winamp DSP | dsp_mcaster1.dll | -- (legacy only) |
| | foobar2000 | foo_mcaster1.dll | -- (legacy only) |
| | RadioDJ DSP | dsp_mcaster1_radiodj.dll | -- (legacy only) |
| **Build System** | Compiler | VS2022 Win32 | VS2022 x64 + Qt MOC/RCC |
| | Build script | Solution file | build_winqt.ps1 (incremental) |
| | Installer | -- | NSIS (Mcaster1DSPEncoderQT_Setup) |
| | Code signing | -- | signtool + PFX cert |
| | Deploy script | -- | deploy_release.ps1 + windeployqt |

`--` = not available in that build

---

## New in Qt 6 — Feature Details

### DSP Effects Rack (7 Processors)

The legacy MFC encoder had **zero audio processing** — audio went straight from input to codec.
The Qt 6 build has a full broadcast-grade DSP chain processed in this order:

```
Input PCM → 10-Band EQ or 31-Band EQ → Sonic Enhancer → PTT Duck → AGC/Limiter → DBX Voice → Encoder
```

1. **10-Band Parametric EQ** — RBJ Audio EQ Cookbook biquad IIR filters. Peaking, low-shelf,
   high-shelf types. ±24 dB gain, configurable frequency and Q. Named presets (flat, classic_rock,
   country, modern_rock).

2. **31-Band Graphic EQ** — Full graphic EQ with independent L/R channel control. Linked stereo
   mode for mastering. Visual frequency response curve.

3. **AGC / Compressor / Limiter** — Soft-knee feedforward compressor with noise gate. Configurable
   threshold (dBFS), ratio (1:1 to ∞:1), attack (1-100ms), release (10-1000ms). Makeup gain stage
   and hard limiter ceiling (-1 dBFS default). Presets: default, speech, rock, jazz, classical.

4. **Sonic Enhancer** — BBE Sonic Maximizer clone. 3-band Linkwitz-Riley LR4 crossover at 150 Hz
   and 1.2 kHz. Phase alignment delay lines (low 2.5ms, mid 0.5ms). Lo Contour bass enhancement
   and Process presence boost with soft tanh saturation. Presets: bypass, subtle_warmth,
   broadcast_standard, rock_pop, voice_clarity, maximum_impact.

5. **PTT Push-to-Talk Ducking** — Attenuates main audio when PTT mic is active. Independent
   PortAudio mic stream with automatic sample rate conversion (linear interpolation when mic SR
   differs from encoder SR). Configurable duck amount, attack/release timing.

6. **DBX 286s Voice Processor** — 5-section broadcast voice channel strip: expander/gate,
   compressor, de-esser (sidechain bandpass), LF enhancer (bass shelf), HF detail (presence/air).

7. **Equal-Power Crossfader** — Smooth track transitions using cos/sin curves (constant perceived
   loudness). Configurable duration. Triggered automatically at playlist track boundaries.

### Video Pipeline (Entirely New)

The legacy MFC encoder was **audio-only**. The Qt 6 build adds a complete video streaming pipeline:

- **4 video sources**: Camera (MF/DirectShow), screen capture (DXGI), image files, video files (MF)
- **2 video encoders**: H.264 (Media Foundation hardware-accelerated), VP8/VP9 (libvpx)
- **4 container formats**: FLV (RTMP), WebM (Icecast), HLS (MPEG-TS), MKV (archive)
- **12 broadcast transitions**: Cut, Crossfade, Fade to Black, Dip to White, 4 directional Wipes,
  2 Push transitions, Iris Circle, Dissolve — all with sRGB gamma-correct blending and feathered edges
- **13 video effects**: Brightness/contrast, blur, sharpen, chroma key, PiP, LUT, and more
- **Overlay system**: Text overlays, image watermarks, lower thirds, news ticker, SRT subtitles
- **Live Video Studio**: 3-source switcher with program monitor, transition controls, stream targets
- **Virtual Camera**: DirectShow filter DLL via shared memory — works with Zoom, OBS, Teams, Discord
- **Video Stream Monitor**: AIR mode (decode live stream for QC) + CUE mode (raw preview)

### Podcast Publishing (Entirely New)

The legacy MFC encoder had basic RSS generation alongside archived audio. The Qt 6 build adds
multi-destination publishing:

- RSS 2.0 + iTunes podcast extension with full episode metadata
- SFTP upload to web servers
- WordPress REST API (PowerPress, Seriously Simple Podcasting, Podlove)
- Buzzsprout API (token-based)
- Transistor.fm API (pre-signed URL)
- Podbean OAuth 2.0 (three-step authenticated)

### Platform Integration (Entirely New)

- **Taskbar badge**: ITaskbarList3 overlay icon showing live encoder count (red circle + white text)
- **Toast notifications**: Shell_NotifyIcon balloon tips for encoder state changes
- **Single-instance enforcement**: QLocalServer/QLocalSocket prevents duplicate launches
- **DPI awareness**: Per-monitor DPI scaling (Windows 10+)
- **System tray**: Full context menu with show/hide, quit, status display

### NSIS Installer (Entirely New)

- User-space install to `C:\Users\USERNAME\Mcaster1\Mcaster1DSPEncoder` (no UAC)
- LZMA solid compression
- Qt DLLs, vcpkg codec DLLs, Qt plugins all bundled
- Default config files (zeroed out for fresh install, preserved on upgrade)
- Start Menu + Desktop shortcuts
- Add/Remove Programs registration
- Code signing integration (signtool + PFX)
- Clean uninstaller with config preservation prompt

---

## Legacy MFC — Features Retained

These features from the legacy MFC build are **still available** in the Qt 6 build:

- All 8 audio codecs (MP3, Vorbis, Opus, FLAC, AAC-LC, HE-AAC v1/v2, AAC-ELD)
- Multi-slot simultaneous encoding (10 slots)
- Icecast2 / Shoutcast / Mcaster1 DNAS streaming
- ICY 1.x and ICY 2.2 extended metadata (50+ headers)
- Manual, file-based, and URL-based metadata sources
- YellowPages (YP) directory listing metadata
- Archive recording (WAV + MP3)
- System tray minimize/restore
- YAML configuration with per-encoder profiles
- Podcast RSS 2.0 generation

## Legacy MFC — Features Deprecated

These features exist **only in the legacy MFC build** and are not ported to Qt 6:

- **DSP plugins**: Winamp (`dsp_mcaster1.dll`), foobar2000 (`foo_mcaster1.dll`),
  RadioDJ (`dsp_mcaster1_radiodj.dll`) — these hook into media player audio streams.
  The Qt 6 build captures audio directly via PortAudio or file playback instead.

- **Window title metadata grabbing**: FindWindow-based window title parsing for metadata.
  Replaced by more reliable file-based and URL-based metadata sources.

- **Embedded help browser**: WebBrowser ActiveX control with ICY 2.2 spec, encoder guide,
  and roadmap tabs. Replaced by `docs/index.html` and online documentation.

- **Win32/x86 builds**: The Qt 6 build is x64-only. Legacy MFC was Win32 (32-bit).

---

## Version History Summary

| Version | Build | Milestone |
|---------|-------|-----------|
| v5.0 | MFC (legacy) | Last MFC release — 8 codecs, ICY 2.2, multi-slot, YAML config |
| v0.1–v0.5 | macOS Qt | Port foundation — main window, config, DSP, streaming |
| v0.6–v0.8 | macOS Qt | Video pipeline, podcast, PTT, DBX Voice, effects rack |
| v0.9–v1.0 | macOS Qt | RTMP/HLS/WebM/FLV, VP8/VP9, DMG distribution |
| v1.1–v1.1.5 | macOS Qt | DSP persistence, per-unit YAML, bug fixes |
| v0.1–v1.1.5 | Windows Qt | Full port from macOS — all features carried forward |
| v1.2–v1.2.5 | Windows Qt | Preview Audio Studio, ICY parser, PTT resampling, metadata persistence |
| **v1.3.0** | **Windows Qt** | **Video capture (MF/DXGI), Live Video Studio, 12 transitions, Virtual Camera, WASAPI loopback, H.264 encoder, NSIS installer** |
