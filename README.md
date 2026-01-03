# Mcaster1DSPEncoder

**Modern, open-source, multi-format audio streaming DSP encoder for live internet radio.**

A maintained, modernized fork of the [EdCast/Oddsock DSP encoder](https://www.oddsock.org/)
by **Ed Zaleski** — rebuilt for Visual Studio 2022, extended with Opus and HE-AAC encoding,
PortAudio device capture (WASAPI loopback + ASIO), and full integration with the
**Mcaster1DNAS** streaming server ecosystem.

**Maintainer:** Dave St. John `<davestj@gmail.com>`
**Repository:** https://github.com/davestj/Mcaster1DSPEncoder
**License:** GPL v2 (inherited from EdCast lineage — see [LICENSE](LICENSE))
**Original Author:** Ed Zaleski — see [CREDITS.md](CREDITS.md) for the full story

---

## What It Does

Mcaster1DSPEncoder captures live audio and streams it to Shoutcast and Icecast servers
in multiple formats simultaneously. It ships as a standalone encoder application and
as plugin DLLs for popular broadcast software.

### Standalone Encoder Application
`Mcaster1DSPEncoder.exe` — captures from any Windows audio device (soundcard, USB
interface, virtual audio cable, WASAPI loopback) and encodes to one or more streams.
Ideal for use with RadioDJ v2, VirtualDJ, SAM Broadcaster, or any software that outputs
to a Windows audio device.

### DSP Plugins — Intercept Audio Directly From Your Broadcast Software
| Plugin | File | Host Application |
|--------|------|-----------------|
| Winamp DSP | `dsp_mcaster1.dll` | Winamp 5.x, WACUP, RadioBoss, RadioDJ v2 |
| foobar2000 | `foo_mcaster1.dll` | foobar2000 1.6+ |

> **RadioDJ v2 users:** RadioDJ v2 uses the Winamp DSP plugin format.
> Use `dsp_mcaster1.dll` via RadioDJ's DSP plugin loader, or use `Mcaster1DSPEncoder.exe`
> in standalone mode with a virtual audio cable.

---

## Supported Formats

| Format | Library | License | Notes |
|--------|---------|---------|-------|
| **MP3 — LAME built-in** | LAME 3.100 | LGPL | CBR / VBR (V0–V9) / ABR. Patents expired April 2017 |
| **MP3 — ACM** | Windows `msacm32` | System | CBR, runtime detection of installed ACM codec |
| **Opus** | libopusenc 0.3 | BSD | Modern, royalty-free, recommended for new streams |
| **Ogg Vorbis** | libvorbis 1.3.7 | BSD | Patent-free, widely supported |
| **FLAC** | libFLAC 1.5.0 | BSD | Lossless, Icecast-compatible |
| **AAC-LC** | fdk-aac 2.0.3 | BSD-like | Broad player compatibility |
| **HE-AAC v1 (AAC+)** | fdk-aac | BSD-like | Low-bitrate streaming (32–96 kbps) |
| **HE-AAC v2 (AAC++)** | fdk-aac | BSD-like | Very low bitrate (16–48 kbps, SBR+PS) |
| ~~WMA~~ | ~~basswma~~ | — | Removed — Windows Media Services is EOL |

> **On AAC/HE-AAC patents:** AAC-LC baseline patents expire ~2028; HE-AAC v2 ~2026–2027.
> Via Licensing clarifies that *content distribution does not require a license*.
> See [FORKS.md](FORKS.md) for full detail.

---

## Supported Servers

- Icecast 2.x (recommended — Mcaster1DNAS is our actively maintained Icecast fork)
- SHOUTcast DNAS v1 (legacy ICY protocol)
- SHOUTcast DNAS v2
- Any ICY/HTTP-compatible streaming server

---

## What Changed From AltaCast

| Area | AltaCast | Mcaster1DSPEncoder |
|------|----------|--------------------|
| Compiler | VS2008 (v90) | VS2022 (v143) |
| Platform | Win32 only | Win32 |
| Windows target | XP (`0x0501`) | Windows 7+ (`0x0601`) |
| MP3 | BladeMP3EncDLL (obsolete) | Native LAME — CBR / VBR / ABR |
| Opus | Not supported | libopusenc — royalty-free |
| HE-AAC | libfaac (LC only) | fdk-aac — LC / HE-v1 / HE-v2 |
| WMA | basswma.dll (proprietary) | Removed |
| Audio capture | bass.dll (capture only) | PortAudio — WASAPI loopback / ASIO / MME / DS |
| Dialog UI | Fixed-size, MS Sans Serif | Resizable (ResizableLib), Segoe UI |
| VU Meters | Fixed-size FlexMeters | FlexMeters + resize-aware (scales with dialog) |
| Solution | Multiple per-project .sln files | Single master .sln — all 4 targets |
| SDK layout | Root-level clutter | Organized under `external/` |
| Build outputs | All mixed in `src\Release\` | Per-project: `Release\`, `Winamp_Plugin\`, `foobar2000\` |
| Deployment | Manual DLL copy | PowerShell deploy scripts per host |
| Maintenance | Unmaintained since ~2015 | Actively maintained |

See [FORKS.md](FORKS.md) for the complete fork rationale including codec patent status.

---

## Project Structure

```
Mcaster1DSPEncoder_Master.sln   ← Open this in VS2022 — all 4 targets
src\
  Mcaster1DSPEncoder.vcxproj        — Standalone EXE (default startup project)
  mcaster1_winamp.vcxproj           — Winamp/WACUP/RadioBoss DSP plugin DLL
  mcaster1_foobar.vcxproj           — foobar2000 component DLL
  libmcaster1dspencoder\
    libmcaster1dspencoder.vcxproj   — Shared encoding engine (static lib)
external\
  include\                          — Shared headers
  lib\                              — Pre-built runtime DLLs and import libs
  ASIOSDK\                          — Steinberg ASIO SDK
  portaudio\src\                    — PortAudio source
  portaudio\built\                  — PortAudio compiled libs (after build-portaudio.ps1)
  foobar2000\                       — foobar2000 SDK
  ResizableLib\                     — MFC ResizableLib (resizable dialog support)
Release\                        ← Standalone EXE output
Winamp_Plugin\Release\          ← dsp_mcaster1.dll output
foobar2000\Release\             ← foo_mcaster1.dll output
libmcaster1dspencoder\Release\  ← shared static lib output
build\                          ← Intermediate object files (gitignored)
```

---

## Building From Source

### Prerequisites

- **Windows 10 or later**
- **Visual Studio 2022** with Desktop C++ workload (v143 toolset, Windows 10 SDK, MFC)
- **vcpkg** at `C:\vcpkg` with the following packages:

```powershell
vcpkg install opus:x86-windows libopusenc:x86-windows mp3lame:x86-windows `
              libflac:x86-windows libogg:x86-windows libvorbis:x86-windows `
              fdk-aac[he-aac]:x86-windows libxml2:x86-windows `
              libiconv:x86-windows curl:x86-windows
```

- **PortAudio** built from source with Steinberg ASIO SDK (run `.\build-portaudio.ps1` first)
- **PowerShell 5+**

### Quick Build (All Targets)

```powershell
# From the repo root:
.\build-portaudio.ps1  # Build PortAudio with ASIO (first time only)
.\build-main.ps1       # Standalone encoder → Release\Mcaster1DSPEncoder.exe
.\build-plugins.ps1    # DSP plugins → Winamp_Plugin\Release\ and foobar2000\Release\
```

### Visual Studio 2022 (Recommended for Development)

Open `Mcaster1DSPEncoder_Master.sln` — all four projects load in Solution Explorer.
`Mcaster1DSPEncoder` (standalone EXE) is the default startup project.

Right-click any project → **Set as Startup Project** to switch build targets.

### Manual MSBuild

```powershell
# All targets via master solution:
msbuild Mcaster1DSPEncoder_Master.sln /p:Configuration=Release /p:Platform=Win32 /m

# Individual targets:
msbuild Mcaster1DSPEncoder_Master.sln /t:Mcaster1DSPEncoder  /p:Configuration=Release /p:Platform=Win32 /m
msbuild Mcaster1DSPEncoder_Master.sln /t:mcaster1_winamp     /p:Configuration=Release /p:Platform=Win32 /m
msbuild Mcaster1DSPEncoder_Master.sln /t:mcaster1_foobar     /p:Configuration=Release /p:Platform=Win32 /m
```

### Build Outputs

```
Release\
  Mcaster1DSPEncoder.exe          — Standalone encoder

Winamp_Plugin\Release\
  dsp_mcaster1.dll                — Winamp / WACUP / RadioBoss DSP plugin

foobar2000\Release\
  foo_mcaster1.dll                — foobar2000 component

libmcaster1dspencoder\Release\
  libmcaster1dspencoder.lib       — Shared encoding engine (linked into above)

(Runtime DLLs from vcpkg and external/lib — copy via deploy scripts)
```

---

## Deployment

Use the included PowerShell deploy scripts to copy the built plugin and all required
runtime DLLs to your host application's directory:

```powershell
.\deploy-winamp.ps1    -Config Release   # → C:\Program Files (x86)\Winamp\
.\deploy-wacup.ps1     -Config Release   # → C:\Program Files\WACUP\
.\deploy-radioboss.ps1 -Config Release   # → C:\Program Files (x86)\RadioBoss\
.\deploy-radiodj.ps1   -Config Release   # → C:\RadioDJv2\
.\deploy-foobar.ps1    -Config Release   # → C:\Program Files\foobar2000\
```

Each script self-elevates via UAC, validates the build exists, and copies the plugin DLL
plus all dependency DLLs in one step. Use `-Config Debug` for debug builds.

---

## Installation

### Standalone Encoder (`Mcaster1DSPEncoder.exe`)

Copy `Release\Mcaster1DSPEncoder.exe` and all DLLs from `external\lib\` and the vcpkg
bin directory to a folder of your choice and run `Mcaster1DSPEncoder.exe`.

### Winamp / WACUP DSP Plugin

Run `.\deploy-winamp.ps1` or `.\deploy-wacup.ps1`, then:
- **Winamp:** Options → Preferences → Plug-ins → DSP/Effect → Mcaster1 DSP Encoder
- **WACUP:** Preferences → Plug-ins → DSP/Effect → Mcaster1 DSP Encoder

### foobar2000 Component

Run `.\deploy-foobar.ps1`, restart foobar2000, then:
File → Preferences → Playback → DSP Manager → Add → Mcaster1 DSP Encoder

### RadioBoss

Run `.\deploy-radioboss.ps1`, then:
Options → DSP/Effect Plugins → Add → select `dsp_mcaster1.dll`

---

## Configuration

### Encoder Connection (per encoder slot)

Each encoder slot has its own config tab in the UI:

| Setting | Description |
|---------|-------------|
| Server Type | Icecast2 / SHOUTcast v1 / SHOUTcast v2 |
| Server Host / Port | Stream server address |
| Password | Source/stream password |
| Mount Point | Icecast mount (e.g. `/live`) |
| Format | MP3 / Opus / Vorbis / FLAC / AAC / HE-AAC |
| Bitrate | Format-dependent (CBR / VBR / quality) |
| ICY Name / Genre / URL | Stream metadata for directory listings |

### Config Files

`MCASTER1DSPENCODER_1.cfg`, `_2.cfg`, ... — one file per active encoder slot (auto-generated at runtime, not committed to repo).

---

## Audio Device Capture (Standalone Mode)

The standalone encoder uses **PortAudio** with full Windows audio backend support:

| Backend | Use Case |
|---------|----------|
| **WASAPI Loopback** | Capture what's playing from any application (no virtual cable needed) |
| **WASAPI Exclusive** | Low-latency dedicated capture device |
| **ASIO** | Professional audio interfaces (Steinberg, UA, Focusrite, RME, MOTU) |
| **DirectSound** | Broad device compatibility |
| **MME** | Legacy and broad hardware support |
| **WDM-KS** | Kernel streaming — lowest latency on older hardware |

Supported capture sample rates: 44100 / 48000 / 88200 / 96000 / 176400 / 192000 Hz.
When capture rate differs from encoder rate, the built-in resample stage handles conversion
automatically (e.g. 192kHz ASIO capture → 48kHz Opus encode).

---

## Mcaster1 Ecosystem

Mcaster1DSPEncoder is part of the **Mcaster1** open-source internet radio platform:

| Project | Description | Status |
|---------|-------------|--------|
| **Mcaster1DNAS** | Maintained fork of Icecast 2.4 with ICY 2.1, song history API, and track history pages | Active |
| **Mcaster1DSPEncoder** | This project — encoder suite for Windows | Active |
| **Mcaster1Castit** | Modernized broadcast scheduler (VC6 → VS2022 upgrade of original Castit) | Planned |

---

## Roadmap

See **[ROADMAP.md](ROADMAP.md)** for the full development roadmap. Summary of completed and planned work:

### Completed
- [x] VS2022 upgrade (v90 → v143 toolset)
- [x] Master solution `Mcaster1DSPEncoder_Master.sln` with all 4 targets
- [x] Per-project output directories (no more mixed `src\Release\`)
- [x] External SDKs organized under `external\`
- [x] Opus encoding (libopusenc)
- [x] HE-AAC v1/v2 (fdk-aac)
- [x] PortAudio integration (WASAPI loopback / ASIO / MME / DS)
- [x] ResizableLib — fully resizable main dialog
- [x] VU meter resize fix — FlexMeters bars now scale correctly with dialog resize
- [x] PowerShell deploy scripts for all 5 host applications
- [x] Debug/Release both build cleanly (CRT-consistent across all linked projects)
- [x] WMA removed (basswma dependency eliminated)

### In Progress
- [ ] Phase 3 — Audio Backend Modernization (PortAudio WASAPI loopback refinement)
- [ ] YAML multi-station configuration

### Planned
- [ ] Phase 4 — YAML Multi-Station Configuration
- [ ] Phase 5 — ICY 2.1 Enhanced Metadata Protocol
- [ ] Phase 6 — Mcaster1DNAS Deep Integration
- [ ] Phase 7 — Mcaster1Castit
- [ ] Phase 8 — Analytics, Metrics & Platform Engagement

---

## Credits

See **[CREDITS.md](CREDITS.md)** for the full story — including:
- Ed Zaleski's mentorship and the EdCast / Oddcast / AltaCast lineage
- The history of casterclub.com, mediacast1.com, and oddsock.org
- All dependency library authors and licenses

See **[FORKS.md](FORKS.md)** for the detailed fork rationale and codec patent status.

See **[AUTHORS](AUTHORS)** for the author list.

---

## License

**GPL v2** — inherited from the EdCast/AltaCast lineage.

Original EdCast/AltaCast copyright © Ed Zaleski.
Fork and modifications copyright © 2024–2026 Dave St. John.

See [LICENSE](LICENSE) for details.

---

## Contributing

Pull requests, issues, and discussion are welcome on GitHub.
Please read [FORKS.md](FORKS.md) to understand the project's goals before contributing.
