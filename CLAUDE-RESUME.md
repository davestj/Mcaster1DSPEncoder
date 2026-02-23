# Mcaster1DSPEncoder — Claude Session Resume File

**Last Updated:** 2026-02-22
**Working Directory:** `C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\`
**Companion Project:** `C:\Users\dstjohn\dev\00_mcaster1.com\mcaster1dnas\`

---

## Project Identity

- **Name:** Mcaster1DSPEncoder
- **Fork of:** EdCast / AltaCast DSP encoder by Ed Zaleski
- **Maintainer:** Dave St. John <davestj@gmail.com>
- **GitHub:** https://github.com/davestj/Mcaster1DSPEncoder
- **License:** GPL v2 (inherited)
- **Toolset:** Visual Studio 2022, v143, Win32 (x64 planned)

---

## Phase Completion Status

| Phase | Name | Status |
|-------|------|--------|
| 1 | VS2022 Build Fix | **COMPLETE** |
| 2 | Rebrand AltaCast → Mcaster1DSPEncoder | **COMPLETE** |
| 2.5 | Project Reorganization (master .sln, per-project outputs, SDK layout) | **COMPLETE** |
| 3 | Audio Backend Modernization (Opus, HE-AAC, PortAudio) | **COMPLETE** — all codecs integrated, volume slider fixed |
| 4 | YAML Multi-Station Config | **COMPLETE** — config_yaml.cpp fully integrated across all targets |
| 5 | Podcast Feature Set & ICY 2.2 Metadata | **IN PROGRESS** — Podcast tab, RSS gen, ICY 2.2 tab, header injection |
| 6 | Mcaster1DNAS Integration | PLANNED |
| 7 | Mcaster1Castit | PLANNED (next major project) |
| 8 | Analytics/Metrics/Platform Engagement | PLANNED |

---

## 4 Build Targets — All Building Clean

| Target | vcxproj | Output |
|--------|---------|--------|
| Standalone encoder | `src\Mcaster1DSPEncoder.vcxproj` | `Release\MCASTER1DSPENCODER.exe` |
| Winamp/WACUP/RadioBoss DSP plugin | `src\mcaster1_winamp.vcxproj` | `Winamp_Plugin\Release\dsp_mcaster1.dll` |
| foobar2000 component | `src\mcaster1_foobar.vcxproj` | `foobar2000\Release\foo_mcaster1.dll` |
| Shared encoding engine (static lib) | `src\libmcaster1dspencoder\libmcaster1dspencoder.vcxproj` | `libmcaster1dspencoder\Release\libmcaster1dspencoder.lib` |

**Master Solution:** `Mcaster1DSPEncoder_Master.sln` (root) — open this, NOT any src\ .sln
**MSBuild:** `C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe`

### Build & Deploy Scripts

```powershell
# Full clean + build all targets + stage DLLs
.\build-all.ps1 -Config Release

# Build standalone only
.\build-main.ps1 -Config Release

# Build plugins only
.\build-plugins.ps1 -Config Release

# Deploy (each self-elevates via UAC)
.\deploy-winamp.ps1    -Config Release   # C:\Program Files (x86)\Winamp\
.\deploy-wacup.ps1     -Config Release   # C:\Program Files (x86)\WACUP\  ← x86 build
.\deploy-radioboss.ps1 -Config Release   # C:\Program Files (x86)\RadioBoss\
.\deploy-radiodj.ps1   -Config Release   # C:\RadioDJv2\
.\deploy-foobar.ps1    -Config Release   # C:\Program Files\foobar2000\
```

**Note on WACUP:** WACUP x64 is at `C:\Program Files\WACUP\` but our build is Win32/x86.
x86 WACUP is installed at `C:\Program Files (x86)\WACUP\` — deploy-wacup.ps1 already points there.
When we do x64 build, change deploy-wacup.ps1 back to `C:\Program Files\WACUP\`.

---

## Fixes Applied This Session (2026-02-21)

### 1. YAML Config — Full Transition from .cfg (COMPLETE)

**Problem:** `MCASTER1DSPENCODER_0.cfg` was still being created on app init despite transitioning to YAML.

**Root causes:**
- `MainWindow.cpp LoadConfigs()` called `readConfigFile(&gMain)` directly, bypassing YAML
- `readConfigFile()` internally calls `writeConfigFile()` unconditionally at line 676 — creates .cfg even if none existed
- All 3 plugin files (`mcaster1_winamp.cpp`, `mcaster1_foobar.cpp`, `mcaster1_radiodj.cpp`) still called `readConfigFile(g)` with no YAML path

**Fixes:**
| File | Change |
|------|--------|
| `src/MainWindow.cpp` `LoadConfigs()` | Replaced bare `readConfigFile(&gMain)` with YAML-first block |
| `src/Mcaster1DSPEncoder.cpp` `mcaster1_init()` | Changed `readConfigFile(g)` → `readConfigFile(g, 1)` (readOnly, no .cfg recreation) |
| `src/mcaster1_winamp.cpp` | Added `#include "config_yaml.h"`, YAML-first init block |
| `src/mcaster1_foobar.cpp` | Added `#include "config_yaml.h"`, YAML-first init block |
| `src/mcaster1_radiodj.cpp` | Added `#include "config_yaml.h"`, YAML-first init block |
| `src/mcaster1_winamp.vcxproj` | Added `config_yaml.cpp` + `config_yaml.h` to build |
| `src/mcaster1_foobar.vcxproj` | Added `config_yaml.cpp` + `config_yaml.h` to build |
| `src/mcaster1_radiodj.vcxproj` | Added `config_yaml.cpp` + `config_yaml.h` to build |

**Config file naming convention:**
- Encoder 0 (first): `MCASTER1DSPENCODER_0.yaml`
- Encoder 1 (second): `MCASTER1DSPENCODER_1.yaml`
- Encoder N: `MCASTER1DSPENCODER_N.yaml`
- Migration: old `.cfg` → renamed to `.cfg.bak` on first YAML run
- No `.cfg` files should ever be created going forward

### 2. Volume Slider for Live Recording — FIXED

**Problem:** When selecting a sound device for live streaming, volume pegged at 100% with slider having no effect. Slider appeared all the way to the left (position 0) but audio was at full.

**Root causes:**
- `m_RecVolume` initialized to 0 in constructor — slider started at minimum
- `OnHScroll()` read slider position but only stored it "for UI state only" (commented-out stub)
- No volume factor was ever applied to audio samples anywhere in the pipeline
- `handleAllOutput()` passed raw PortAudio float32 samples to encoders with no scaling

**Fix — 4 changes to `src/MainWindow.cpp`:**

```cpp
// 1. New global (~line 40)
volatile float g_recVolumeFactor = 1.0f;   // 0.0=silent, 1.0=unity

// 2. OnInitDialog — slider starts at full volume
m_RecVolumeCtrl.SetRange(0, 100);
m_RecVolumeCtrl.SetPos(100);
m_RecVolume = 100;
g_recVolumeFactor = 1.0f;

// 3. OnHScroll — compute factor from slider position
g_recVolumeFactor = (float)m_RecVolume / 100.0f;

// 4. handleAllOutput — scale samples before peak detection + encoding
if (g_recVolumeFactor < 0.9999f) {
    int total = nsamples * nchannels;
    for (int i = 0; i < total; i++)
        samples[i] *= g_recVolumeFactor;
}
```

Scaling happens before VU meter peak calculation AND before `handle_output()` — so meters, all codecs (MP3/AAC/Opus), and encoded output all reflect the adjusted level.

### 3. Build Scripts Updated

| Script | Change |
|--------|--------|
| `build-all.ps1` | NEW — full clean+build all 4 targets + DLL staging + deploy hint |
| `build-main.ps1` | Now auto-runs `copy-dlls.ps1` after successful build |
| `deploy-winamp.ps1` | Added `yaml.dll` to runtime DLL list |
| `deploy-wacup.ps1` | Added `yaml.dll`; fixed path to `C:\Program Files (x86)\WACUP\` |
| `deploy-radioboss.ps1` | Added `yaml.dll` |
| `deploy-radiodj.ps1` | Added `yaml.dll` |
| `deploy-foobar.ps1` | Added `yaml.dll` |

### 4. Confirmed Working

- Standalone EXE: **working** (tested by user)
- Winamp plugin: **working** (deployed, tested by user)
- WACUP x86 plugin: **working** (deployed to `C:\Program Files (x86)\WACUP\`)
- MP3, AAC, Opus streaming: **working**
- Volume slider: **fixed** (previously broken, now functional)

### 5. Windows Smart App Control

User's machine was in **Evaluation mode (1)** — blocking unsigned dev binaries.
To disable permanently (one-way, requires admin PowerShell + reboot):
```powershell
Set-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\CI\Policy" `
    -Name "VerifiedAndReputablePolicyState" -Value 0 -Type DWord
```
Long-term solution: EV code-signing cert (Sectigo, DigiCert, etc.) for public releases.

---

## Phase 5 Progress (2026-02-22)

### What Was Implemented

| File | Status | Notes |
|------|--------|-------|
| `src/resource.h` | **DONE** | Added IDC_ 1110–1185, IDD_PROPPAGE_LARGE3=304 |
| `src/libmcaster1dspencoder/libmcaster1dspencoder.h` | **DONE** | +11 podcast fields, +~55 gICY22* fields |
| `src/mcaster1dspencoder.rc` | **DONE** | Reworked podcast tab (IDD_PROPPAGE_LARGE2), added ICY 2.2 tab (IDD_PROPPAGE_LARGE3) |
| `src/AdvancedSettings.h/.cpp` | **DONE** | Podcast DDX, Browse button, EnableDisablePodcast, OnRSSUseYP |
| `src/ICY22Settings.h/.cpp` | **NEW** | 4th config tab — all 55 ICY 2.2 fields + UUID auto-gen |
| `src/Config.h/.cpp` | **DONE** | 4th tab wired, GlobalsToDialog/DialogToGlobals extended |
| `src/config_yaml.cpp` | **DONE** | 75+ new YAML keys for podcast + ICY 2.2 fields |
| `src/podcast_rss_gen.h/.cpp` | **NEW** | RSS 2.0 + iTunes NS generator (pure C, fprintf) |
| `src/libmcaster1dspencoder/libmcaster1dspencoder.cpp` | **DONE** | Path capture in openArchiveFile, RSS call in closeArchiveFile, appendICY22Headers(), Icecast2 integration |
| `src/MainWindow.cpp` | **DONE** | writeMainConfig in OnClose + ProcessConfigDone, Pa_Terminate in CleanUp |
| `src/Mcaster1DSPEncoder.vcxproj` | **DONE** | Added ICY22Settings.cpp/.h, podcast_rss_gen.cpp/.h |
| `src/mcaster1_winamp.vcxproj` | **DONE** | Same additions |
| `src/mcaster1_foobar.vcxproj` | **DONE** | Same additions |

### What Remains for Phase 5

- **Build verification** — `.\build-all.ps1 -Config Release` on all 4 targets
- **E2E test** — Connect to Mcaster1DNAS, verify ICY 2.2 headers in server log; stream, record, check .rss file
- **REFACTOR-REPORT.html** — Phases 3/4 → COMPLETE badges, Phase 5 → IN PROGRESS sections

---

## Next Session Priorities

1. **Build Phase 5** — Run `.\build-all.ps1 -Config Release`; fix any compilation errors
2. **E2E test podcast RSS** — Enable archive, stream, disconnect, verify `.rss` file alongside audio file
3. **E2E test ICY 2.2 headers** — Connect to Mcaster1DNAS, check server log for `icy-metadata-version: 2.2` and all metadata fields
4. **x64 build** — Add x64 platform configuration to all 4 targets; update deploy-wacup.ps1 back to `C:\Program Files\WACUP\`
5. **Phase 6: Mcaster1DNAS Integration** — DNAS API, listener stats, song history feed in the encoder
6. **Code signing cert** — For public release builds (EV cert from Sectigo/DigiCert)
7. **Mcaster1Castit** — Next major project: VC6 → VS2022 upgrade of original Castit tool

---

## Key Architecture Notes

### YAML Config System (Phase 4 — COMPLETE)
- `src/config_yaml.cpp` + `src/config_yaml.h` — libyaml-based read/write
- All 4 build targets compile `config_yaml.cpp`
- YAML-first init in ALL entry points: `mcaster1_init()`, `LoadConfigs()`, plugin inits
- Migration path: INI → YAML on first run, `.cfg` renamed to `.cfg.bak`
- `yaml.dll` (vcpkg x86-windows) required at runtime — included in all deploy scripts

### PortAudio Volume Pipeline
```
Device → paRecordCallback (const float*) → handleAllOutput (float*)
           ↓                                      ↓
      raw samples                    [1] apply g_recVolumeFactor
                                     [2] VU meter peak detection
                                     [3] handle_output() → all encoders
```

### ResizableLib Integration
- All dialogs: `CDialog` → `CResizableDialog`
- `AddAnchor(IDC_METER, TOP_LEFT, BOTTOM_RIGHT)` in MainWindow
- `AddAnchor(IDC_RECVOLUME, TOP_LEFT, TOP_RIGHT)` — slider stretches with window

### CRT Runtime
- All 4 targets: `/MD` (MultiThreadedDLL) — never mix with `/MT`

### PortAudio Build
- Static lib at `external\portaudio\built\portaudio_static.lib`
- Script: `.\build-portaudio.ps1`
- Known debt: `Pa_Terminate()` not called at app exit

---

## vcpkg Packages (C:\vcpkg, x86-windows)

```powershell
vcpkg install opus:x86-windows libopusenc:x86-windows mp3lame:x86-windows `
              libflac:x86-windows libogg:x86-windows libvorbis:x86-windows `
              fdk-aac[he-aac]:x86-windows libxml2:x86-windows `
              libiconv:x86-windows curl:x86-windows libyaml:x86-windows
```

Runtime DLLs deployed to host app directories:
`fdk-aac.dll`, `libmp3lame.dll`, `opus.dll`, `opusenc.dll`, `yaml.dll` (from vcpkg bin)
`ogg.dll`, `vorbis.dll`, `vorbisenc.dll`, `libFLAC.dll`, `pthreadVSE.dll`, `libcurl.dll`, `iconv.dll` (from external\lib)

---

## Directory Structure (Current)

```
Mcaster1DSPEncoder\               ← repo root
  Mcaster1DSPEncoder_Master.sln   ← Master solution — open this in VS2022
  build-all.ps1                   ← NEW: full clean+build all targets
  build-main.ps1                  ← standalone EXE (now auto-stages DLLs)
  build-plugins.ps1               ← Winamp + foobar2000 plugins
  build-portaudio.ps1             ← PortAudio static lib (CMake)
  deploy-winamp.ps1               ← → C:\Program Files (x86)\Winamp\
  deploy-wacup.ps1                ← → C:\Program Files (x86)\WACUP\ (x86!)
  deploy-radioboss.ps1            ← → C:\Program Files (x86)\RadioBoss\
  deploy-radiodj.ps1              ← → C:\RadioDJv2\
  deploy-foobar.ps1               ← → C:\Program Files\foobar2000\
  src\
    Mcaster1DSPEncoder.vcxproj
    mcaster1_winamp.vcxproj
    mcaster1_foobar.vcxproj
    config_yaml.cpp / .h          ← YAML config engine (Phase 4)
    MainWindow.cpp / .h           ← volume slider fix, YAML init
    FlexMeters.cpp / .h           ← GDI leak fixed, immediate redraw
    Config.cpp / .h               ← resize flicker fixed
    libmcaster1dspencoder\
      libmcaster1dspencoder.vcxproj
      libmcaster1dspencoder.cpp/.h
  external\
    include\, lib\, ASIOSDK\
    portaudio\src\, portaudio\built\
    foobar2000\, ResizableLib\
```

---

## Personal History / Project Context

- **Dave St. John** began working with **Ed Zaleski** in 2001 on **Castit** for **casterclub.com**
- Ed mentored Dave in C/C++ and MFC/Windows programming
- Dave sponsored hosting for **oddsock.org** (Ed's project site) in exchange for guidance
- Dave sold **mediacast1.com** to **Spacial Audio** (later acquired by **Triton Digital**)
- Dave kept **casterclub.com** as internet radio history
- EdCast VC6 source was the starting point for this entire codebase
- **Mcaster1Castit** is the next project — upgrading the original Castit tool to VS2022
- **Castit DB choice:** MariaDB preferred over MySQL (open-source, no Oracle constraints)

---

## Companion Project: Mcaster1DNAS

`C:\Users\dstjohn\dev\00_mcaster1.com\mcaster1dnas\`
- Active branch: `windows-dev`
- Maintained fork of Icecast 2.4
- Recent: song history API (in-memory ring buffer), Track History pages with music service icons
- ICY 2.2 protocol integration — Phase 5 encoder sends `icy-metadata-version: 2.2` + full header set to DNAS on Icecast2 SOURCE connect
- See `CLAUDE-RESUME.md` in that directory for its session state
