# Mcaster1DSPEncoder

**Professional, open-source, triple-platform broadcast DSP encoder for live internet radio and video streaming.**

A next-generation broadcast encoder built with **Qt 6 Widgets** for Windows and macOS, and a
**PHP web admin UI** for Linux headless servers. Features a full DSP effects rack, multi-format
audio/video encoding, live streaming to Icecast2/Shoutcast/Mcaster1DNAS, and a broadcast-grade
Live Video Studio with virtual camera output.

Originally a modernized fork of the [EdCast/Oddsock DSP encoder](https://www.oddsock.org/)
by **Ed Zaleski** — now rebuilt from the ground up as a professional broadcast platform.

**Maintainer:** Dave St. John `<davestj@gmail.com>`
**Repository:** https://github.com/davestj/Mcaster1DSPEncoder
**License:** GPL v2 (inherited from EdCast lineage — see [LICENSE](LICENSE))
**Original Author:** Ed Zaleski — see [CREDITS.md](CREDITS.md) for the full story

---

## What It Does

Mcaster1DSPEncoder captures live audio and video, applies professional DSP processing,
encodes to multiple formats, and streams to broadcast servers — all from a single application.

### Qt 6 Desktop Application (Windows + macOS)

Full-featured broadcast encoder with native Qt 6 Widgets GUI:

- **Multi-slot encoding**: Run multiple simultaneous encoder instances to different servers/mounts
- **DSP Effects Rack**: 10-band parametric EQ, 31-band graphic EQ, AGC/compressor/limiter,
  Sonic Enhancer, PTT ducking with mic input, DBX Voice processor, equal-power crossfader
- **Audio formats**: MP3 (LAME), Ogg Vorbis, Opus, FLAC, AAC-LC, HE-AAC v1/v2, AAC-ELD
- **Video streaming**: RTMP (YouTube/Twitch), HLS, WebM (Icecast), FLV with VP8/VP9 encoding
- **Live Video Studio**: 3-source preview switcher + program monitor, cut/crossfade/wipe transitions,
  image/video file sources, audio device capture
- **Virtual Camera**: DirectShow output (Windows) / ScreenCaptureKit (macOS) for use in Zoom, OBS, etc.
- **Video Stream Monitor**: AIR mode (decode live encoded stream for QC) + CUE mode (raw pre-encode preview)
- **Preview Audio Studio**: Tune into your own live streams for quality monitoring
- **ICY 2.2**: Full extended metadata protocol support for Mcaster1DNAS
- **Podcast/RSS**: Archive recording with RSS feed generation
- **YAML profiles**: Per-encoder configuration persistence, DSP effect presets

### Linux Headless Server (CLI + Web Admin)

Server-grade encoder with embedded HTTP/HTTPS admin server and PHP web UI:

- All audio encoding features (MP3/Vorbis/Opus/FLAC/AAC)
- DSP chain (10-band EQ, AGC/limiter, crossfader)
- PHP web UI: dashboard, encoder controls, media library, playlist management
- Two-layer auth: C++ in-memory sessions + MySQL PHP sessions
- FastCGI bridge to PHP-FPM
- systemd service deployment

### Legacy Windows Standalone + DSP Plugins

The original MFC-based encoder (maintained for compatibility):

| Plugin | File | Host Application |
|--------|------|-----------------|
| Standalone EXE | `Mcaster1DSPEncoder.exe` | Direct audio device capture |
| Winamp DSP | `dsp_mcaster1.dll` | Winamp 5.x, WACUP, RadioBoss, RadioDJ v2 |
| foobar2000 | `foo_mcaster1.dll` | foobar2000 1.6+ |

---

## Supported Audio Formats

| Format | Library | License | Notes |
|--------|---------|---------|-------|
| **MP3** | LAME 3.100 | LGPL | CBR / VBR / ABR. Patents expired April 2017 |
| **Opus** | libopusenc | BSD | Modern, royalty-free, recommended for new streams |
| **Ogg Vorbis** | libvorbis 1.3.7 | BSD | Patent-free, widely supported |
| **FLAC** | libFLAC 1.5.0 | BSD | Lossless, Icecast-compatible |
| **AAC-LC** | fdk-aac 2.0.3 | BSD-like | Broad player compatibility |
| **HE-AAC v1 (AAC+)** | fdk-aac | BSD-like | Low-bitrate streaming (32–96 kbps) |
| **HE-AAC v2 (AAC++)** | fdk-aac | BSD-like | Very low bitrate (16–48 kbps, SBR+PS) |
| **AAC-ELD** | fdk-aac | BSD-like | Enhanced low delay (real-time comms) |

## Supported Video Formats

| Format | Container | Use Case |
|--------|-----------|----------|
| **VP8** | WebM | Icecast2 video streaming |
| **VP9** | WebM | Icecast2 video streaming (higher quality) |
| **H.264** | FLV, MPEG-TS | RTMP (YouTube Live, Twitch) |
| **HLS** | MPEG-TS segments | HTTP Live Streaming (local or CDN) |

## Supported Servers

- **Mcaster1DNAS** — our maintained Icecast fork with ICY 2.2, song history, track pages
- **Icecast 2.x** — standard open-source streaming server
- **SHOUTcast DNAS v1/v2** — legacy ICY protocol support
- **YouTube Live / Twitch** — RTMP publishing (video)
- **HLS (Local)** — segment-based HTTP live streaming

---

## Platform Builds

| Platform | Source | Build | Branch | Current Version |
|----------|--------|-------|--------|----------------|
| **Windows Qt** | `win-qt/` | VS2022 + Qt 6.9.3 msvc2022_64 | `winqt-dev` | v1.3.0 |
| **macOS** | `src/macos/` | Autotools + Qt 6 | `macos-dev` | v1.1.5 |
| **Linux** | `src/linux/` | Autotools + g++ | `linux-dev` | v1.4.5 |
| **Windows MFC** | `src/` | VS2022 .sln | `master` | v5.0 (legacy) |

All three Qt-era codebases are parallel — they share **no source code**. Windows ports from macOS, macOS ports from Linux.

---

## Building From Source

### Windows Qt (Recommended for Desktop Development)

**Prerequisites:**
- Windows 10 or later (x64)
- Visual Studio 2022 Professional with Desktop C++ workload
- Qt 6.9.3 msvc2022_64 at `C:\Qt\6.9.3\msvc2022_64`
- vcpkg at `C:\vcpkg` with packages:

```powershell
vcpkg install lame:x64-windows vorbis:x64-windows opus:x64-windows `
              opusenc:x64-windows flac:x64-windows fdk-aac:x64-windows `
              portaudio:x64-windows yaml:x64-windows openssl:x64-windows `
              libvpx:x64-windows
```

**Build:**
```powershell
# ALWAYS use build_winqt.ps1 — NEVER use .bat scripts or /t:Clean
powershell.exe -NoProfile -File "win-qt\build_winqt.ps1"
```

**Output:** `build\win-qt\Debug\Mcaster1DSPEncoder_Qt.exe`

The build script handles MSVC environment setup, MSBuild, windeployqt (first run only),
vcpkg DLL copying, and Virtual Camera DLL compilation.

### macOS

```bash
bash install-deps.sh          # Install Homebrew dependencies
./autogen.sh                  # Bootstrap autotools
./configure --enable-macos-gui
make -j$(sysctl -n hw.ncpu)
make codesign                 # Optional: ad-hoc code signing
make dmg                      # Optional: create DMG installer
```

### Linux

```bash
bash install-deps.sh    # Auto-detects OS (apt/dnf/yum/zypper/pacman)
./autogen.sh            # Bootstrap autotools
./configure             # Detect available codecs
make -j$(nproc)         # Build
```

**Run:**
```bash
nohup ./build/mcaster1-encoder \
  --config src/linux/config/mcaster1_rock_yolo.yaml \
  > /tmp/mc1enc.log 2>&1 & disown $!
```

### Legacy Windows MFC

Open `Mcaster1DSPEncoder_Master.sln` in VS2022. All 4 targets (standalone, Winamp, foobar2000, shared lib) build from the master solution.

---

## Project Structure

```
Mcaster1DSPEncoder/
├── win-qt/                     ← Windows Qt 6 source (active)
│   ├── Mcaster1DSPEncoder_Qt.vcxproj
│   ├── build_winqt.ps1         ← Build script (ALWAYS use this)
│   ├── main_window.h/cpp       ← Main application window
│   ├── audio_pipeline.h/cpp    ← Multi-slot audio orchestrator
│   ├── encoder_slot.h/cpp      ← Per-slot state machine
│   ├── stream_client.h/cpp     ← Icecast2/Shoutcast/DNAS SOURCE client
│   ├── dsp/                    ← DSP effects (EQ, AGC, PTT, DBX, etc.)
│   ├── video/                  ← Video pipeline (capture, studio, streaming)
│   │   ├── live_video_studio.h/cpp
│   │   ├── video_stream_monitor.h/cpp
│   │   ├── rtmp_client.h/cpp
│   │   └── video_capture_windows.h/cpp
│   ├── virtual_camera/         ← DirectShow virtual camera DLL
│   └── resources/              ← QSS themes, icons, .qrc
├── src/
│   ├── linux/                  ← Linux source (CLI + web admin)
│   │   ├── http_api.cpp        ← HTTP/HTTPS server + routes
│   │   ├── web_ui/             ← PHP frontend
│   │   └── dsp/                ← DSP chain (EQ, AGC, crossfader)
│   ├── macos/                  ← macOS Qt 6 source
│   └── libmcaster1dspencoder/  ← Legacy shared structs
├── external/                   ← Legacy Windows vendor headers
├── CLAUDE.md                   ← Project memory / instructions
├── PLANNING.md                 ← Future phase roadmap
├── ROADMAP.md                  ← Development roadmap + feature matrix
├── CHANGELOG.md                ← Version history
├── configure.ac                ← Autotools config (Linux + macOS)
└── docs/index.html             ← HTML technical documentation
```

---

## Mcaster1 Ecosystem

Mcaster1DSPEncoder is part of the **Mcaster1** open-source internet radio/video platform:

| Project | Description | Status |
|---------|-------------|--------|
| **Mcaster1DNAS** | Maintained fork of Icecast 2.4 with ICY 2.2, song history API, video WebM streaming | Active |
| **Mcaster1DSPEncoder** | This project — triple-platform broadcast encoder | Active |
| **Mcaster1Castit** | Modernized broadcast scheduler (planned) | Planned |

---

## Roadmap

See **[ROADMAP.md](ROADMAP.md)** for the full development roadmap and feature matrix.
See **[PLANNING.md](PLANNING.md)** for detailed future phase specifications.

---

## Credits

See **[CREDITS.md](CREDITS.md)** for the full story — including:
- Ed Zaleski's mentorship and the EdCast / Oddcast / AltaCast lineage
- The history of casterclub.com, mediacast1.com, and oddsock.org
- All dependency library authors and licenses

See **[FORKS.md](FORKS.md)** for the detailed fork rationale and codec patent status.

---

## License

**GPL v2** — inherited from the EdCast/AltaCast lineage.

Original EdCast/AltaCast copyright (c) Ed Zaleski.
Fork and modifications copyright (c) 2024-2026 Dave St. John.

See [LICENSE](LICENSE) for details.

---

## Contributing

Pull requests, issues, and discussion are welcome on GitHub.
Please read [FORKS.md](FORKS.md) to understand the project's goals before contributing.
