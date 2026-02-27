# Mcaster1 DSP Encoder

**Next Generation Broadcast DSP Encoder for Live Internet Radio & Video Streaming**

A professional broadcast encoder built with **Qt 6 Widgets** — featuring a 7-effect DSP chain,
8 audio codecs, hardware-accelerated video streaming with 12 broadcast transitions, virtual
camera output, and multi-server broadcasting to Icecast2, Shoutcast, Mcaster1DNAS, YouTube
Live, and Twitch.

**Product Page:** [mcaster1.com/encoder.php](https://mcaster1.com/encoder.php)
**Maintainer:** Dave St. John `<davestj@gmail.com>`
**License:** GPL v2 (inherited from EdCast lineage)
**Version:** 1.3.1-beta (Windows Qt)

---

## Key Features

### Professional DSP Effects Rack

7-effect broadcast processing chain — the legacy MFC encoder had zero audio processing:

| Effect | Description |
|--------|-------------|
| **10-Band Parametric EQ** | RBJ biquad IIR filters, ±24dB, frequency/Q/gain per band |
| **31-Band Graphic EQ** | Independent L/R channels, linked stereo mode |
| **AGC / Compressor / Limiter** | Soft-knee, noise gate, makeup gain, hard limiter ceiling |
| **Sonic Enhancer** | BBE Sonic Maximizer clone — LR4 crossover, phase alignment, saturation |
| **PTT Push-to-Talk Duck** | Mic input with automatic sample rate conversion |
| **DBX 286s Voice Processor** | Gate, compressor, de-esser, LF/HF enhancement |
| **Equal-Power Crossfader** | Smooth playlist transitions (cos/sin curves) |

### 8 Audio Codecs

| Codec | Library | Bitrate Range | Mode |
|-------|---------|---------------|------|
| MP3 | LAME 3.100 | 32–320 kbps | CBR |
| Ogg Vorbis | libvorbis | Quality 0–10 | VBR |
| Opus | libopusenc | 32–320 kbps | VBR (48kHz internal) |
| FLAC | libFLAC | Lossless | Level 0–8 |
| AAC-LC | fdk-aac | 64–320 kbps | CBR |
| HE-AAC v1 | fdk-aac | 24–128 kbps | SBR |
| HE-AAC v2 | fdk-aac | 16–64 kbps | SBR+PS (stereo only) |
| AAC-ELD | fdk-aac | 24–192 kbps | Low delay |

### Live Video Studio

Professional multi-source video switcher with broadcast-grade transitions:

- **4 video sources**: Camera (Media Foundation + DirectShow), DXGI screen capture,
  image files, video files (MP4/MKV/AVI/WMV/MOV)
- **12 transitions**: Cut, Crossfade, Fade to Black, Dip to White, 4 directional Wipes,
  2 Push transitions, Iris Circle, Dissolve — all with sRGB gamma-correct blending
  and 24px feathered edges
- **H.264 encoding**: Hardware-accelerated via NVENC, Intel QSV, or AMD AMF (software fallback)
- **VP8/VP9 encoding**: Open-source via libvpx
- **Stream to**: YouTube Live (RTMP), Twitch (RTMP), Icecast2 (WebM/FLV), HLS (MPEG-TS)
- **Virtual Camera**: DirectShow output — use your broadcast in Zoom, OBS, Teams, Discord
- **Stream Monitor**: AIR mode (decode live stream) + CUE mode (raw preview)
- **ON-AIR indicator**: Flashing red indicator with tray notification

### Multi-Server Broadcasting

Stream simultaneously to multiple servers with independent codec/bitrate per slot:

- **Mcaster1DNAS** — ICY 2.2 extended metadata (50+ headers, social, engagement, compliance)
- **Icecast 2.x** — Standard open-source streaming
- **Shoutcast v1/v2** — Legacy ICY protocol
- **YouTube Live / Twitch** — RTMP publishing (video + audio)
- **HLS** — HTTP Live Streaming (local segment generation)

### Podcast Publishing

Archive recording with multi-destination publishing:

- Simultaneous WAV + MP3 archival
- RSS 2.0 + iTunes podcast extension
- Publish to: SFTP, WordPress (REST API), Buzzsprout, Transistor.fm, Podbean (OAuth 2.0)

---

## Supported Platforms

| Platform | Source | Build | Branch | Version |
|----------|--------|-------|--------|---------|
| **Windows Qt** | `win-qt/` | VS2022 + Qt 6.9.3 | `winqt-dev` | **v1.3.1-beta** |
| **macOS** | `src/macos/` | Autotools + Qt 6 | `macos-dev` | v1.1.5 |
| **Linux** | `src/linux/` | Autotools + g++ | `linux-dev` | v1.4.5 |
| **Windows MFC** | `src/` | VS2022 .sln | `master` | v5.0 (legacy) |

All three Qt-era codebases are parallel — they share **no source code**.

---

## Quick Start (Windows)

### Install

Download and run `Mcaster1DSPEncoderQT_Setup_1.3.0.exe`. Installs to:
```
C:\Users\USERNAME\Mcaster1\Mcaster1DSPEncoder\
```

No admin rights required. Fully portable — all config files saved next to the exe.

### Build from Source

**Prerequisites:** VS2022 Professional, Qt 6.9.3 msvc2022_64, vcpkg

```powershell
# Install vcpkg dependencies
vcpkg install lame:x64-windows vorbis:x64-windows opus:x64-windows `
              opusenc:x64-windows flac:x64-windows fdk-aac:x64-windows `
              portaudio:x64-windows yaml:x64-windows openssl:x64-windows `
              libvpx:x64-windows

# Build (ALWAYS use build_winqt.ps1)
powershell.exe -NoProfile -File "win-qt\build_winqt.ps1"

# Build installer
powershell.exe -NoProfile -File "installer\build_installer.ps1"
```

**Output:** `build\win-qt\Debug\Mcaster1DSPEncoder_Qt.exe`

### Build (macOS)

```bash
bash install-deps.sh && ./autogen.sh && ./configure --enable-macos-gui && make -j$(sysctl -n hw.ncpu)
```

### Build (Linux)

```bash
bash install-deps.sh && ./autogen.sh && ./configure && make -j$(nproc)
```

---

## Mcaster1 Product Ecosystem

Mcaster1DSPEncoder is part of the **Mcaster1** open-source broadcast platform:

| Product | Description | URL |
|---------|-------------|-----|
| **Mcaster1 DSP Encoder** | This project — broadcast DSP encoder | [mcaster1.com/encoder.php](https://mcaster1.com/encoder.php) |
| **Mcaster1 DNAS** | Icecast fork with ICY 2.2, song history, video streaming | [mcaster1.com/mcaster1_dnas.php](https://mcaster1.com/mcaster1_dnas.php) |
| **Mcaster1 Studio** | Broadcast automation — 9 surfaces, 43+ modules | [mcaster1.com/mcaster1studio.php](https://mcaster1.com/mcaster1studio.php) |
| **Mcaster1AMP** | Desktop media player — dual A/B decks, video playback | [mcaster1.com/mcaster1amp.php](https://mcaster1.com/mcaster1amp.php) |
| **Mcaster1 AudioPipe** | Virtual audio routing — Qt6 3D patch bay | [mcaster1.com/audiopipes.php](https://mcaster1.com/audiopipes.php) |
| **Mcaster1 TagStack** | Content management — ICY 2.2 composer, media library | [mcaster1.com/tagstack.php](https://mcaster1.com/tagstack.php) |
| **Mcaster1 CastIt** | Statistics monitor for Shoutcast/Icecast2 | [mcaster1.com/mcaster1_castit.php](https://mcaster1.com/mcaster1_castit.php) |

**Typical broadcast workflow:**
TagStack (content prep) → DSP Encoder (streaming) → DNAS (server) → Studio (automation)

Use **Mcaster1AMP** as a video/radio player to monitor DSP Encoder video streams.

---

## Documentation

| Document | Description |
|----------|-------------|
| [FEATURES.md](FEATURES.md) | Complete feature comparison — Qt 6 vs Legacy MFC |
| [RELEASENOTES.md](RELEASENOTES.md) | v1.3.0 release notes, bundled deps, system requirements |
| [CHANGELOG.md](CHANGELOG.md) | Version history with per-release changes |
| [ROADMAP.md](ROADMAP.md) | Development roadmap and feature matrix |
| [PLANNING.md](PLANNING.md) | Future phase specifications |
| [CREDITS.md](CREDITS.md) | EdCast lineage, dependency credits |
| [FORKS.md](FORKS.md) | Fork rationale and codec patent status |
| [docs/index.html](docs/index.html) | HTML technical documentation |

---

## Project Structure

```
Mcaster1DSPEncoder/
├── win-qt/                         ← Windows Qt 6 source (active)
│   ├── build_winqt.ps1             ← Build script (ALWAYS use this)
│   ├── main_window.h/cpp           ← Main application window
│   ├── audio_pipeline.h/cpp        ← Multi-slot audio orchestrator
│   ├── encoder_slot.h/cpp          ← Per-slot state machine
│   ├── stream_client.h/cpp         ← Icecast2/Shoutcast/DNAS client
│   ├── dsp/                        ← DSP chain (EQ, AGC, PTT, DBX, etc.)
│   ├── video/                      ← Video pipeline
│   │   ├── live_video_studio.h/cpp ← 3-source switcher + transitions
│   │   ├── transition_engine.h/cpp ← 12 broadcast transitions (gamma-correct)
│   │   ├── rtmp_client.h/cpp       ← RTMP publishing
│   │   ├── video_encoder_windows.* ← MF H.264 (NVENC/QSV/AMF)
│   │   ├── screen_capture_windows.*← DXGI desktop capture
│   │   └── video_file_source_*     ← MF video file decoder
│   ├── virtual_camera/             ← DirectShow virtual camera DLL
│   ├── podcast/                    ← RSS, SFTP, WordPress, Buzzsprout
│   └── resources/                  ← Icons, QRC, theme
├── src/linux/                      ← Linux source (CLI + PHP web admin)
├── src/macos/                      ← macOS Qt 6 source
├── src/                            ← Legacy Windows MFC source
├── installer/                      ← NSIS installer scripts
├── docs/                           ← HTML documentation
├── VERSION.txt                     ← Current version (1.3.1-beta)
└── CLAUDE.md                       ← Project memory / build instructions
```

---

## Credits

See **[CREDITS.md](CREDITS.md)** — including Ed Zaleski's original EdCast/Oddcast/AltaCast
lineage, the history of casterclub.com and mediacast1.com, and all dependency library authors.

## License

**GPL v2** — inherited from the EdCast/AltaCast lineage.
Copyright (c) 2024-2026 Dave St. John. Original EdCast copyright (c) Ed Zaleski.
