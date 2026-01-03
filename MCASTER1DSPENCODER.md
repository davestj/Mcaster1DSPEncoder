# Mcaster1DSPEncoder — Technical Reference

**Version:** 0.3.x (Phase 3 in progress)
**Maintainer:** Dave St. John <davestj@gmail.com>
**Toolset:** Visual Studio 2022, v143, Win32 / x64
**Last Updated:** 2026-02

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Component Tree](#component-tree)
3. [Plugin Architecture](#plugin-architecture)
4. [Codec Pipeline](#codec-pipeline)
5. [Audio Device Enumeration — PortAudio](#audio-device-enumeration)
6. [Configuration System](#configuration-system)
7. [Build System](#build-system)
8. [Dependency Matrix](#dependency-matrix)
9. [Project File Map](#project-file-map)
10. [Known Limitations & Technical Debt](#known-limitations--technical-debt)

---

## Architecture Overview

Mcaster1DSPEncoder is a Windows MFC application that encodes live PCM audio to one or
more streaming formats simultaneously and transmits each stream to an Icecast or
SHOUTcast server over HTTP/ICY.

The architecture has three major layers:

```
┌─────────────────────────────────────────────────────────────┐
│                    HOST AUDIO SOURCES                        │
│  Winamp plugin │ foobar2000 component │ RadioDJ DSP plugin  │
│                  Standalone: PortAudio device capture        │
└───────────────────────────┬─────────────────────────────────┘
                            │ PCM float samples
                            ▼
┌─────────────────────────────────────────────────────────────┐
│              MFC APPLICATION LAYER (MainWindow.cpp)          │
│  CMainWindow — UI, device selection, encoder management      │
│  CConfig / CBasicSettings / CYPSettings / CAdvancedSettings │
│  CEditMetadata — manual title override                       │
│  CSystemTray — system tray integration                       │
│  CFlexMeters — VU meter display                              │
└───────────────────────────┬─────────────────────────────────┘
                            │ handleAllOutput(float*, nsamples, nch, rate)
                            ▼
┌─────────────────────────────────────────────────────────────┐
│        libmcaster1dspencoder — core streaming engine         │
│  mcaster1Globals[MAX_ENCODERS] — per-slot state              │
│  mcaster1_init() / mcaster1_encode() / mcaster1_quit()       │
│  Encoder threads (pthreadVSE) — one per active encoder slot  │
│  Format encoders: MP3(LAME) │ MP3(ACM) │ Opus │ Vorbis      │
│                   FLAC │ AAC-LC │ HE-AACv1 │ HE-AACv2       │
│  ICY metadata writer — StreamTitle inline block injection    │
│  HTTP/ICY stream socket — libcurl or raw Winsock             │
│  YP directory submission — libxml2 / libcurl                 │
│  Resample stage — libmcaster1dspencoder_resample.h           │
└─────────────────────────────────────────────────────────────┘
```

---

## Component Tree

```
Mcaster1DSPEncoder\
├── src\                              — All compilable source
│   ├── Mcaster1DSPEncoder.sln        — Master solution (standalone EXE)
│   ├── Mcaster1DSPEncoder.vcxproj    — Standalone encoder project
│   ├── Mcaster1DSPEncoder.cpp/.h     — MFC application entry (WinMain, AfxWinInit)
│   ├── MainWindow.cpp/.h             — Main dialog (CMainWindow : CResizableDialog)
│   ├── Config.cpp/.h                 — Config dialog shell with tab control
│   ├── BasicSettings.cpp/.h          — Tab: server/encoder basic settings
│   ├── YPSettings.cpp/.h             — Tab: ICY metadata / YP directory
│   ├── AdvancedSettings.cpp/.h       — Tab: log, archive, reconnect settings
│   ├── EditMetadata.cpp/.h           — Manual metadata edit dialog
│   ├── FindWindow.cpp/.h             — Window-class metadata finder
│   ├── About.cpp/.h                  — About dialog
│   ├── SystemTray.cpp/.h             — Windows system tray icon
│   ├── FlexMeters.cpp/.h             — VU meter widget (GDI)
│   ├── Dummy.cpp/.h                  — Null audio source for plugin-less mode
│   ├── mcaster1dspencoder.rc         — All dialog resources
│   ├── resource.h                    — Resource ID definitions
│   ├── StdAfx.h                      — Precompiled header root
│   │
│   ├── libmcaster1dspencoder\        — Core streaming engine (static lib)
│   │   ├── libmcaster1dspencoder.vcxproj
│   │   ├── libmcaster1dspencoder.cpp — Main encoder engine: init, encode, quit
│   │   ├── libmcaster1dspencoder.h   — mcaster1Globals struct, public API
│   │   ├── libmcaster1dspencoder_socket.h  — Socket abstraction (Winsock)
│   │   └── libmcaster1dspencoder_resample.h — PCM resampler (rate conversion)
│   │
│   ├── mcaster1_winamp.cpp/.h        — Winamp DSP plugin (winampDSPModule interface)
│   ├── mcaster1_winamp.sln/.vcxproj
│   ├── mcaster1_foobar.cpp           — foobar2000 component (foobar2000 SDK)
│   ├── mcaster1_foobar.sln/.vcxproj
│   ├── mcaster1_radiodj.cpp/.h       — RadioDJ v1.x DSP plugin (Winamp DSP format)
│   ├── mcaster1_radiodj.sln/.vcxproj
│   │
│   └── mcaster1dspencoder.h          — Shared plugin/app header (encoder slot access)
│
├── external\                         — Third-party libraries
│   ├── ResizableLib\                 — Resizable MFC dialog framework (Paolo Messina)
│   ├── lame\                         — LAME MP3 source + headers
│   ├── include\                      — External headers (libvorbis, libogg, etc.)
│   └── lib\                          — Pre-built import libs (Win32)
│
├── foobar2000\                       — foobar2000 SDK (unmodified)
│   ├── foobar2000\SDK\
│   └── pfc\
│
├── ASIOSDK\                          — Steinberg ASIO SDK (for PortAudio ASIO backend)
│
├── portaudio_src\                    — PortAudio source (cloned from GitHub)
├── portaudio_build_x86\             — PortAudio CMake build (x86 + ASIO)
├── portaudio_built\                  — Built portaudio_static.lib outputs
│
├── README.md                         — Project README
├── ROADMAP.md                        — Development roadmap
├── CREDITS.md                        — Attribution and history
├── FORKS.md                          — Fork rationale and codec patent status
├── AUTHORS                           — Author list
├── LICENSE                           — GPL v2
├── MCASTER1DSPENCODER.md            — This file (technical reference)
├── REFACTOR-REPORT.md               — Phase-by-phase refactor log
├── REFACTOR-REPORT.html             — HTML version of refactor report
│
├── build-main.ps1                    — Build standalone encoder
├── build-plugins.ps1                 — Build all 3 plugins
├── build-portaudio.ps1               — Build PortAudio with ASIO
└── copy-dlls.ps1                     — Copy runtime DLLs to Release\
```

---

## Plugin Architecture

### Winamp DSP Plugin (`dsp_mcaster1.dll`)

Winamp loads DSP plugins that export `winampDSPGetHeader2()` returning a
`winampDSPHeader` struct. The plugin receives audio as 16-bit PCM stereo from Winamp's
`modify_samples()` callback, converts to float, and calls `handleAllOutput()`.

```
winampDSPGetHeader2() → winampDSPHeader → winampDSPModule
  .init()         — creates CMainWindow, initializes encoder
  .config()       — opens configuration dialog
  .modify_samples() — called per audio frame by Winamp
  .quit()         — shuts down encoder, destroys window
```

### foobar2000 Component (`foo_mcaster1.dll`)

Uses the foobar2000 SDK component framework. The component registers:
- `initquit_mcaster1` — lifecycle callbacks (startup/shutdown)
- `dsp_mcaster1` — DSP processor that receives `audio_chunk` objects
- `mcaster1_play_callback_ui` — now-playing metadata callbacks

The foobar2000 SDK (unmodified) is bundled in `foobar2000/`. Key SDK files that
required fixes for VS2022 compatibility are documented in `REFACTOR-REPORT.md §Phase 1`.

### RadioDJ v1.x Plugin (`dsp_mcaster1_radiodj.dll`)

Same Winamp DSP format as the Winamp plugin — RadioDJ v1.x had native Winamp DSP
plugin support. This plugin does **not** work with RadioDJ v2.x, which dropped
third-party plugin support entirely.

### Shared Dialog Code

All 4 build targets (exe + 3 DLLs) compile the full set of MFC dialog source files
(`MainWindow.cpp`, `Config.cpp`, `BasicSettings.cpp`, etc.) and the full ResizableLib
set. The standalone exe creates the main window directly; plugins create it as a modeless
dialog hosted by the plugin entry point.

---

## Codec Pipeline

### PCM Entry Point

All audio reaches the core engine via:

```c
int handleAllOutput(float *samples, int nsamples, int nchannels, int in_samplerate);
```

This fans the float PCM out to all active encoder threads via the
`mcaster1Globals *g[MAX_ENCODERS]` array.

### Per-Encoder Thread

Each active encoder slot runs in its own `pthreadVSE` thread. The thread:
1. Receives float PCM samples
2. Resamples if `in_samplerate != encoder_samplerate`
3. Encodes to the selected format
4. Writes encoded frames to the network socket (HTTP/ICY PUT or SHOUTcast SOURCE)
5. Injects ICY `StreamTitle=` metadata at `Icy-MetaInt` byte boundaries

### Format Encoders

| Format | Init | Encode | Cleanup |
|--------|------|--------|---------|
| MP3 LAME | `lame_init()`, `lame_set_*()`, `lame_init_params()` | `lame_encode_buffer_ieee_float()` | `lame_close()` |
| MP3 ACM | `acmStreamOpen()` | `acmStreamConvert()` | `acmStreamClose()` |
| Opus | `ope_encoder_create_callbacks()` | `ope_encoder_write()` | `ope_encoder_drain(); ope_encoder_destroy()` |
| Vorbis | `vorbis_encode_init_vbr()` | `vorbis_analysis_wrote()` | `vorbis_dsp_clear()` |
| FLAC | `FLAC__stream_encoder_new()` | `FLAC__stream_encoder_process_interleaved()` | `FLAC__stream_encoder_delete()` |
| AAC-LC | `aacEncOpen(); AACENC_AOT=2` | `aacEncEncode()` | `aacEncClose()` |
| HE-AACv1 | `aacEncOpen(); AACENC_AOT=5` | `aacEncEncode()` | `aacEncClose()` |
| HE-AACv2 | `aacEncOpen(); AACENC_AOT=29` | `aacEncEncode()` | `aacEncClose()` |

---

## Audio Device Enumeration

### PortAudio Integration

`Mcaster1DSPEncoder.exe` uses PortAudio (statically linked, ASIO-enabled) for device
enumeration and capture. The standalone encoder does NOT use PortAudio — plugins receive
audio from their host application.

**Enumeration:**
```c
Pa_Initialize();
int n = Pa_GetDeviceCount();
for (int i = 0; i < n; i++) {
    const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
    const PaHostApiInfo *api = Pa_GetHostApiInfo(info->hostApi);
    // api->type: paWASAPI / paDirectSound / paMME / paWDMKS / paASIO
    if (info->maxInputChannels > 0) { /* show in device list */ }
}
```

**Supported sample rates probing:**
```c
double rates[] = {44100, 48000, 88200, 96000, 176400, 192000};
for (double r : rates) {
    PaError e = Pa_IsFormatSupported(&inParams, NULL, r);
    if (e == paFormatIsSupported) { /* enable in UI dropdown */ }
}
```

**WASAPI Loopback** — to capture what's playing on any output device:
```c
PaWasapiStreamInfo wasapiInfo = {0};
wasapiInfo.flags = paWinWasapiExclusive;  // or shared
// Use a render device index with loopback flag via PaWasapi_GetDeviceDefaultFormat
```

**ASIO** — PortAudio surfaces ASIO devices in the same `Pa_GetDeviceInfo()` call.
No special handling needed in the device list — ASIO devices appear alongside WASAPI/MME.

### Build Note — PortAudio with ASIO

PortAudio must be built from source with the Steinberg ASIO SDK:
```powershell
cmake -S portaudio_src -B portaudio_build_x86 -A Win32 `
    -DPA_USE_ASIO=ON -DASIO_SDK_DIR="$PSScriptRoot\ASIOSDK" `
    -DPA_BUILD_STATIC=ON -DPA_BUILD_SHARED=OFF
cmake --build portaudio_build_x86 --config Release
# → portaudio_build_x86\Release\portaudio_static.lib
```

The Steinberg ASIO SDK is a free download from steinberg.net. Its license prohibits
redistribution of SDK source but permits shipping compiled binaries — which is exactly
what we do (static link into the EXE, no ASIO SDK files distributed).

---

## Configuration System

### Current: Key=Value Files

Config is managed by `libmcaster1dspencoder.cpp` using a flat `key=value` file per
encoder slot. File naming: `{configBaseName}_{n}.cfg` where `n` is the encoder slot
number (1-indexed).

Key functions:
```c
void mcaster1_load_config(mcaster1Globals *g, int encoderNumber);
void mcaster1_save_config(mcaster1Globals *g, int encoderNumber);
```

**Settings per slot (selected):**

| Key | Type | Description |
|-----|------|-------------|
| `ServerType` | int | 1=Icecast2, 2=SHOUTcast v1, 3=SHOUTcast v2 |
| `ServerIP` | string | Hostname or IP |
| `ServerPort` | int | Port number |
| `ServerPassword` | string | Source password |
| `MountPoint` | string | Icecast mount (e.g. `/live`) |
| `BitRate` | int | kbps |
| `Channels` | int | 1=mono, 2=stereo |
| `SampleRate` | int | 44100 / 48000 |
| `EncoderType` | int | Format selector |
| `ICYName` | string | Stream name |
| `ICYGenre` | string | Stream genre |
| `ICYURL` | string | Station website URL |
| `ICYDescription` | string | Stream description |
| `ICYPublic` | bool | List on YP directories |

### Phase 4: YAML Config

See [ROADMAP.md](ROADMAP.md) §Phase 4 for the full YAML schema.

---

## Build System

### Project Files

| Solution | Produces | Platform |
|----------|----------|----------|
| `src\Mcaster1DSPEncoder.sln` | `Mcaster1DSPEncoder.exe` | Win32 |
| `src\mcaster1_winamp.sln` | `dsp_mcaster1.dll` | Win32 |
| `src\mcaster1_foobar.sln` | `foo_mcaster1.dll` | Win32 |
| `src\mcaster1_radiodj.sln` | `dsp_mcaster1_radiodj.dll` | Win32 |

### Key MSBuild Configuration (all projects)

- **Toolset:** v143 (VS2022)
- **Windows SDK:** 10.0 (latest)
- **CRT:** `/MD` (MultiThreadedDLL) — all components must match
- **`_WIN32_WINNT`:** `0x0601` (Windows 7 minimum, Windows 10 target)
- **vcpkg includes:** `$(VCPKG_ROOT)\installed\x86-windows\include`
- **vcpkg libs:** `$(VCPKG_ROOT)\installed\x86-windows\lib`
- **ResizableLib:** `../external/ResizableLib` — compiled with `NotUsing` PCH and `/wd4005`

### PowerShell Build Scripts

| Script | Purpose |
|--------|---------|
| `build-main.ps1` | Build `Mcaster1DSPEncoder.sln` |
| `build-plugins.ps1` | Build all 3 plugin solutions in sequence |
| `build-portaudio.ps1` | Build PortAudio static lib with ASIO SDK |
| `copy-dlls.ps1` | Copy vcpkg runtime DLLs to `src\Release\` |
| `deploy-winamp.ps1` | Copy plugin to Winamp Plugins dir |
| `deploy-radiodj.ps1` | Copy plugin to RadioDJ dir |

---

## Dependency Matrix

| Library | Version | License | Link | Phase |
|---------|---------|---------|------|-------|
| LAME mp3 | 3.100 | LGPL v2 | Dynamic (`libmp3lame.dll`) | 2 |
| libopus | 1.5.2 | BSD 3-Clause | Dynamic | 3 |
| libopusenc | 0.3 | BSD 3-Clause | Dynamic | 3 |
| libvorbis | 1.3.7 | BSD 3-Clause | Dynamic | existing |
| libogg | 1.3.6 | BSD 3-Clause | Dynamic | existing |
| libFLAC | 1.5.0 | BSD 3-Clause | Dynamic | existing |
| fdk-aac | 2.0.3 | BSD-like | Dynamic | 3 |
| libcurl | latest | MIT/curl | Dynamic | existing |
| libxml2 | latest | MIT | Dynamic | existing |
| libiconv | latest | LGPL | Dynamic | existing |
| pthreads-win32 | latest | LGPL | Dynamic (`pthreadVSE.dll`) | existing |
| PortAudio | 19.7.0 | MIT | **Static** (baked into EXE only) | 3 |
| libyaml | 0.2.5 | MIT | Static | 4 |
| ResizableLib | latest | Artistic | Source (compiled in) | 2 |
| MFC | VS2022 | MS EULA | System (`mfc140u.dll`) | — |

---

## Project File Map

### Source Files and Their Roles

| File | Class / Role |
|------|--------------|
| `Mcaster1DSPEncoder.cpp` | `CMcaster1DSPEncoderApp : CWinApp` — MFC app entry |
| `MainWindow.cpp` | `CMainWindow : CResizableDialog` — main UI |
| `Config.cpp` | `CConfig : CResizableDialog` — config shell + tab control |
| `BasicSettings.cpp` | `CBasicSettings : CResizableDialog` — server/encoder tab |
| `YPSettings.cpp` | `CYPSettings : CResizableDialog` — ICY metadata tab |
| `AdvancedSettings.cpp` | `CAdvancedSettings : CResizableDialog` — advanced tab |
| `EditMetadata.cpp` | `CEditMetadata : CResizableDialog` — manual metadata edit |
| `FindWindow.cpp` | `CFindWindowDlg : CResizableDialog` — window class finder |
| `About.cpp` | `CAbout : CDialog` — about box |
| `SystemTray.cpp` | `CSystemTray` — system tray icon, WM_TRAY_NOTIFY handling |
| `FlexMeters.cpp` | `CFlexMeters` — GDI VU meter |
| `Dummy.cpp` | Null audio source for standalone mode |
| `log.cpp` | Logging subsystem |
| `mcaster1_winamp.cpp` | Winamp DSP plugin entry, `winampDSPGetHeader2` export |
| `mcaster1_foobar.cpp` | foobar2000 SDK component registration and callbacks |
| `mcaster1_radiodj.cpp` | RadioDJ v1.x DSP plugin (Winamp DSP format) |
| `libmcaster1dspencoder/libmcaster1dspencoder.cpp` | Core streaming engine |

---

## Known Limitations & Technical Debt

| Item | Description | Phase to Fix |
|------|-------------|-------------|
| `Pa_Terminate()` not called at exit | PortAudio initialized in `OnInitDialog()` but `CleanUp()` doesn't call `Pa_Terminate()` — potential driver state leak on abnormal exit | Phase 3 |
| RadioDJ v2 incompatible | RadioDJ v2 dropped all plugin support — `dsp_mcaster1_radiodj.dll` works only with RadioDJ v1.x. Use standalone + virtual audio cable for RadioDJ v2. | Won't fix (upstream) |
| Max 10 encoder slots | `MAX_ENCODERS 10` hardcoded in `MainWindow.h`. YAML config (Phase 4) will increase to 32. | Phase 4 |
| Legacy key=value config | No station grouping, duplicate ICY metadata, opaque numbered files. | Phase 4 |
| ICY metadata limited to plain text | No album art, no structured metadata fields. | Phase 5 |
| LAME ACM path — CBR only | The ACM interface is inherently CBR; VBR requires the LAME direct API path. This is by design, not a bug. | — |
| HE-AAC v2 patent caveat | HE-AAC v2 (SBR+PS) patents held by Dolby/Philips expire ~2026–2027. Commercial encoder distributors may have Via Licensing obligations. Content distribution (streaming to listeners) does not require a license. | — |
| x64 builds not yet packaged | Codebase targets Win32 only in current release. x64 configuration planned. | Phase 3 followup |

---

*For the development roadmap see [ROADMAP.md](ROADMAP.md).*
*For fork rationale and codec patent details see [FORKS.md](FORKS.md).*
*For attribution and project history see [CREDITS.md](CREDITS.md).*
