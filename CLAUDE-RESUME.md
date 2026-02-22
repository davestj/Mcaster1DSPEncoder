# Mcaster1DSPEncoder — Claude Session Resume File

**Last Updated:** 2026-02-21
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
| 2.5 | Project Reorganization (master .sln, per-project outputs, SDK layout) | **COMPLETE** (this session) |
| 3 | Audio Backend Modernization (Opus, HE-AAC, PortAudio) | **IN PROGRESS** — codecs integrated, pending E2E verification |
| 4 | YAML Multi-Station Config | PLANNED |
| 5 | ICY 2.1 Enhanced Metadata Protocol | PLANNED |
| 6 | Mcaster1DNAS Integration | PLANNED |
| 7 | Mcaster1Castit | PLANNED (next major project) |
| 8 | Analytics/Metrics/Platform Engagement | PLANNED |

---

## 4 Build Targets — All Building Clean (REORGANIZED)

| Target | vcxproj | Output |
|--------|---------|--------|
| Standalone encoder | `src\Mcaster1DSPEncoder.vcxproj` | `Release\Mcaster1DSPEncoder.exe` |
| Winamp/WACUP/RadioBoss DSP plugin | `src\mcaster1_winamp.vcxproj` | `Winamp_Plugin\Release\dsp_mcaster1.dll` |
| foobar2000 component | `src\mcaster1_foobar.vcxproj` | `foobar2000\Release\foo_mcaster1.dll` |
| Shared encoding engine (static lib) | `src\libmcaster1dspencoder\libmcaster1dspencoder.vcxproj` | `libmcaster1dspencoder\Release\libmcaster1dspencoder.lib` |

**Master Solution:** `Mcaster1DSPEncoder_Master.sln` (root) — open this, NOT any src\ .sln
- Default startup project: `Mcaster1DSPEncoder` (standalone EXE)
- All 4 targets visible in Solution Explorer
- Per-project build intermediate dir: `build\<target>\<config>\`

MSBuild: `C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe`
Scripts: `.\build-main.ps1` (uses master .sln), `.\build-plugins.ps1`, `.\build-portaudio.ps1`

---

## Directory Structure (CURRENT — post-reorganization)

```
Mcaster1DSPEncoder\               ← repo root
  Mcaster1DSPEncoder_Master.sln   ← Master solution — open this in VS2022
  src\                            ← all source files
    Mcaster1DSPEncoder.vcxproj
    mcaster1_winamp.vcxproj
    mcaster1_foobar.vcxproj
    libmcaster1dspencoder\
      libmcaster1dspencoder.vcxproj
  external\
    include\                      ← shared headers
    lib\                          ← pre-built runtime DLLs / import libs
    ASIOSDK\                      ← Steinberg ASIO SDK (moved from root)
    portaudio\
      src\                        ← PortAudio source (moved from root portaudio_src\)
      built\                      ← portaudio_static.lib (moved from root portaudio_built\)
      build_x86\                  ← cmake build tree (gitignored)
      build_x64\                  ← cmake build tree (gitignored)
    foobar2000\                   ← foobar2000 SDK (moved from root foobar2000\)
    ResizableLib\                 ← MFC resizable dialog framework
  Release\                        ← standalone EXE output (gitignored)
  Winamp_Plugin\Release\          ← Winamp DSP plugin output (gitignored)
  foobar2000\Release\             ← foobar2000 component output (gitignored)
  libmcaster1dspencoder\Release\  ← static lib output (gitignored)
  build\                          ← intermediate obj files (gitignored)
```

---

## Key Fixes Applied (This Session 2026-02-21)

### 1. VS2022 "Load Failed" for Mcaster1DSPEncoder — FIXED
**Root cause:** `src\Mcaster1DSPEncoder.vcxproj` had duplicate `<ClInclude>` entries:
```xml
<ClInclude Include="mcaster1dspencoder.h" />   <!-- lowercase -->
<ClInclude Include="MCASTER1DSPENCODER.h" />   <!-- UPPERCASE -->
```
VS2022 IDE does case-insensitive duplicate detection and refuses to load. MSBuild ignores it silently.
**Fix:** Merged to single `<ClInclude Include="Mcaster1DSPEncoder.h" />` in both `.vcxproj` and `.vcxproj.filters`.
**Diagnosed with:** `devenv.exe /build Mcaster1DSPEncoder_Master.sln /out devenv-build.log`
Error message was: `Cannot load project with duplicated project items: MCASTER1DSPENCODER.h is included as 'ClInclude' and as 'ClInclude' item types.`

### 2. libmcaster1dspencoder project recovery — FIXED
User accidentally deleted `src\libmcaster1dspencoder\` (entire folder).
Recovered from Windows Recycle Bin via PowerShell `Shell.Application.Namespace(10)` + `InvokeVerb("undelete")`.
All source files restored: `libmcaster1dspencoder.vcxproj`, `libmcaster1dspencoder.cpp`, `Socket.cpp`, `cbuffer.c`, `resample.c`.

### 3. VU Meter resize — FIXED
**Problem:** FlexMeters bar graphs did not resize when user resized the main dialog.
The outer Picture Control (IDC_METER) was anchored via ResizableLib (`TOP_LEFT, BOTTOM_RIGHT`)
so its frame resized, but the DIB double-buffer inside was not recreated.

**Fix A — GDI leak in `src\FlexMeters.cpp Initialize_Step3()`:**
Every resize call created new `memDC`/`memBM` without deleting old ones (GDI handle exhaustion).
Added cleanup before recreation:
```cpp
if (memDC) {
    if (oldBM) { ::SelectObject(memDC, oldBM); oldBM = NULL; }
    DeleteDC(memDC);  memDC = NULL;
}
if (memBM) { DeleteObject(memBM); memBM = NULL; }
buffer = NULL;  intbuffer = NULL;
```

**Fix B — missing immediate redraw in `src\MainWindow.cpp OnSize()`:**
After `Initialize_Step3()`, the timer (50ms) was the only trigger to redraw — stale bars showed at old size.
Added immediate `RenderMeters()` call:
```cpp
HWND hMeter = GetDlgItem(IDC_METER)->m_hWnd;
HDC  hDC    = ::GetDC(hMeter);
if (hDC) {
    flexmeters.RenderMeters(hDC);
    ::ReleaseDC(hMeter, hDC);
}
```

### 4. Master Solution startup project set
`Mcaster1DSPEncoder_Master.sln` GlobalSection(ExtensibilityGlobals) now contains:
`StartupProject = {F2EB751E-9592-4064-9432-617FB1C45D25}` (Mcaster1DSPEncoder EXE).
If VS still shows another project bold, right-click Mcaster1DSPEncoder → Set as Startup Project once.

### 5. Old per-project .sln files deleted
`src\Mcaster1DSPEncoder.sln`, `src\mcaster1_winamp.sln`, `src\mcaster1_foobar.sln`, `src\mcaster1_radiodj.sln` — all deleted.
Only `Mcaster1DSPEncoder_Master.sln` at root should be used.

---

## Key Architectural Decisions

### ResizableLib Integration
- All dialog classes: `CDialog` → `CResizableDialog`
- ResizableLib at `external\ResizableLib\` compiled into all targets
- `AddAnchor(IDC_METER, TOP_LEFT, BOTTOM_RIGHT)` in MainWindow makes meter frame resize
- FlexMeters recalculates geometry in `OnSize()` via `Initialize_Step3()`

### Dialog Styling
- `mcaster1dspencoder.rc`: `WS_THICKFRAME` (resizable) on main dialog
- Font: `9, "Segoe UI", 400, 0, 0x1` throughout
- `DS_3DLOOK` removed; `WS_EX_COMPOSITED` NOT used (RC compiler doesn't support it)

### CRT Runtime
- All 4 targets: `/MD` (MultiThreadedDLL) — never mix with `/MT`
- foobar Debug also uses `/MDd` (explicitly, not default) for CRT consistency

### PortAudio
- Built from source at `external\portaudio\src\` with `external\ASIOSDK\`
- Static lib at `external\portaudio\built\`
- Script: `.\build-portaudio.ps1`
- Known debt: `Pa_Terminate()` not called at app exit — should be added to `CleanUp()`

---

## vcpkg Packages (C:\vcpkg)

Required for x86-windows:
```powershell
vcpkg install opus:x86-windows libopusenc:x86-windows mp3lame:x86-windows `
              libflac:x86-windows libogg:x86-windows libvorbis:x86-windows `
              fdk-aac[he-aac]:x86-windows libxml2:x86-windows `
              libiconv:x86-windows curl:x86-windows libyaml:x86-windows
```

---

## Deploy Scripts

```powershell
.\deploy-winamp.ps1    -Config Release   # → C:\Program Files (x86)\Winamp\
.\deploy-wacup.ps1     -Config Release   # → C:\Program Files\WACUP\
.\deploy-radioboss.ps1 -Config Release   # → C:\Program Files (x86)\RadioBoss\
.\deploy-radiodj.ps1   -Config Release   # → C:\RadioDJv2\
.\deploy-foobar.ps1    -Config Release   # → C:\Program Files\foobar2000\
```

Each script self-elevates via UAC, validates build exists, copies plugin DLL + all dependency DLLs.

---

## RadioDJ Notes

- RadioDJ v2 has NO DSP plugin support — dropped in v2.0.0.x
- RadioDJ v2 workaround: use standalone EXE + virtual audio cable
- `deploy-radiodj.ps1` deploys standalone EXE + DLLs to `C:\RadioDJv2\`
- RadioDJ v2 has REST API for now-playing metadata (can poll separately in future)

---

## Git Status

**Not yet initialized as a git repo.** User will run `git init` themselves.

Suggested initial commit message:
```
git commit -m "$(cat <<'EOF'
Initial commit: Mcaster1DSPEncoder — modernized DSP encoder suite for Windows

Complete reorganization and modernization from AltaCast/EdCast lineage:
- VS2022 master solution (Mcaster1DSPEncoder_Master.sln) with all 4 targets
- Standalone EXE, Winamp DSP plugin, foobar2000 component, shared static lib
- Per-project output dirs: Release/, Winamp_Plugin/, foobar2000/, libmcaster1dspencoder/
- External SDKs organized under external/: ASIOSDK, portaudio, foobar2000 SDK
- Modern codec support: MP3/LAME, Opus, HE-AAC/fdk-aac, Ogg/Vorbis, FLAC
- PortAudio: WASAPI loopback/exclusive, ASIO, DirectSound, MME, WDM-KS
- All 4 targets build clean Debug + Release (CRT-consistent /MD across all)
- VU meter resize fix: FlexMeters GDI cleanup + immediate redraw on dialog resize
- PowerShell deploy scripts for Winamp, WACUP, RadioBoss, RadioDJ, foobar2000
- WMA/basswma dependency removed

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

## Codec Patent Quick Reference

| Codec | Status | Notes |
|-------|--------|-------|
| MP3 | **Patent-free since April 2017** | Last US patent expired April 16, 2017 |
| Opus | **Royalty-free** | All patents licensed royalty-free |
| Ogg Vorbis | **Patent-free** | Xiph.Org confirmed |
| FLAC | **Patent-free** | No known patents |
| AAC-LC | Via Licensing pool (~2028) | Content distribution: no license needed |
| HE-AAC v2 | Dolby/Philips patents (~2026–2027) | SBR+PS patents |

---

## Next Session Priorities

1. **Test VU meter resize** — Run the app, resize the dialog, confirm bars scale correctly at all sizes
2. **Phase 3 E2E verification** — Opus stream to Icecast + VLC; HE-AAC ADTS to Icecast; verify all codecs connect and stream
3. **`Pa_Terminate()` debt** — Add `Pa_Terminate()` to `CMainWindow::CleanUp()` before shutdown
4. **git init + first commit** — User runs `git init`, then use commit message above
5. **Phase 4 start** — YAML config: `libyaml` already in vcpkg, begin `config_yaml.cpp`
6. **New icons** — User mentioned wanting new icons (deferred)
7. **Mcaster1Castit** — Next project after Phase 4: VC6 → VS2022 upgrade

---

## Source Layout (Key Files)

```
src\
  Mcaster1DSPEncoder.vcxproj      — standalone EXE project
  mcaster1_winamp.vcxproj         — Winamp DSP plugin
  mcaster1_foobar.vcxproj         — foobar2000 component
  MainWindow.cpp / .h             — CMainWindow : CResizableDialog
  FlexMeters.cpp / .h             — VU meter renderer (DIB double-buffer)
  Config.cpp / .h                 — CConfig + tab page shell
  BasicSettings.cpp / .h          — Server/encoder settings tab
  YPSettings.cpp / .h             — ICY metadata tab
  AdvancedSettings.cpp / .h       — Advanced settings tab
  EditMetadata.cpp / .h           — Manual metadata dialog
  FindWindow.cpp / .h             — Window class finder dialog
  mcaster1dspencoder.rc           — All dialog resources
  StdAfx.h                        — PCH root
  libmcaster1dspencoder\          — Core streaming engine (static lib)
    libmcaster1dspencoder.cpp/.h  — mcaster1Globals[], mcaster1_init/encode/quit
    Socket.cpp                    — Winsock abstraction
    cbuffer.c                     — Circular buffer
    resample.c                    — PCM rate converter
  mcaster1_winamp.cpp/.h          — Winamp DSP plugin entry points
  mcaster1_foobar.cpp             — foobar2000 component
external\
  ResizableLib\                   — Resizable dialog framework (Paolo Messina)
  ASIOSDK\                        — Steinberg ASIO SDK
  portaudio\src\                  — PortAudio source
  portaudio\built\                — portaudio_static.lib
  foobar2000\                     — foobar2000 SDK (unmodified)
  include\                        — shared headers (opus, lame, fdk-aac, etc.)
  lib\                            — pre-built runtime DLLs
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
- ICY 2.1 protocol integration planned (Phase 5 for both projects)
- See `CLAUDE-RESUME.md` in that directory for its session state

---

## Doc Files in Repo

- `README.md` — project overview, build instructions, deployment, roadmap
- `CREDITS.md` — Ed Zaleski history, casterclub.com, oddsock.org, dependency credits
- `FORKS.md` — fork rationale, codec patent status, what changed from AltaCast
- `ROADMAP.md` — 8-phase roadmap
- `AUTHORS` — author list
- `LICENSE` — GPL v2
- `CLAUDE-RESUME.md` — this file (session resume state)
