# Mcaster1DSPEncoder — Refactor & Development Roadmap

**Fork of:** EdCast/Oddsock DSP encoder by Ed Zaleski
**Maintainer:** Dave St. John <davestj@gmail.com>
**Repository:** https://github.com/davestj/Mcaster1DSPEncoder
**Phase 1 Status:** COMPLETE — All 4 build targets compiling and running under VS2022

---

## Table of Contents

1. [Project Overview](#project-overview)
2. [Phase 1 — VS2022 Build Fix (COMPLETE)](#phase-1)
3. [Phase 2 — Rebrand & Refactor](#phase-2)
   - [2.1 Naming Conventions](#21-naming-conventions)
   - [2.2 Deep Naming Audit — All 150+ Instances](#22-deep-naming-audit)
   - [2.3 File & Directory Renames](#23-file--directory-renames)
   - [2.4 Credits & Attribution](#24-credits--attribution)
   - [2.5 MP3 — Replace BladeMP3 with Native LAME](#25-mp3--replace-blademp3-with-native-lame)
   - [2.6 Platform Migration: Win32 → x64](#26-platform-migration-win32--x64)
   - [2.7 Build System Cleanup](#27-build-system-cleanup)
   - [2.8 Phase 2 Deliverables](#28-phase-2-deliverables)
4. [Phase 3 — Audio Backend Modernization](#phase-3)
5. [Phase 4 — YAML Multi-Station Configuration](#phase-4)
6. [Dependency Matrix](#dependency-matrix)
7. [Git Repository Setup](#git-repository-setup)

---

## Project Overview

Mcaster1DSPEncoder is an open-source, multi-format audio streaming DSP encoder suite — a
maintained, modernized fork of the EdCast/Oddsock DSP encoder lineage. It ships as a plugin for Winamp, foobar2000, and
RadioDJ, and as a self-contained encoder application targeting Shoutcast and Icecast servers.

**Original Author:** Ed Zaleski
**Fork Maintainer:** Dave St. John <davestj@gmail.com>
**License:** GPL (inherited from AltaCast)

---

## Phase 1 — VS2022 Build Fix (COMPLETE)

All issues blocking compilation under Visual Studio 2022 (v143 toolset, Windows 10 SDK)
were resolved. All 4 build targets compile cleanly and run without crashes.

| Target | Output | Status |
|--------|--------|--------|
| Core encoder library | `libmcaster1dspencoder.lib` | DONE |
| Winamp DSP plugin | `dsp_mcaster1.dll` | DONE |
| RadioDJ DSP plugin | `dsp_mcaster1_radiodj.dll` | DONE |
| foobar2000 component | `foo_mcaster1.dll` | DONE |
| Encoder application | `Mcaster1DSPEncoder.exe` | DONE |

**Fixes applied:**
- `foobar2000_SDK` / `foobar2000_sdk_helpers`: Updated `_WIN32_WINNT=0x501` → `0x0601` to resolve `_REASON_CONTEXT` C2011 struct redefinition against Win10 SDK
- `pfc/alloc.h`: Fixed `alloc_fixed::alloc::move_from()` calling `pfc::copy_array_t()` on a type lacking `has_owned_items` — replaced with element-by-element copy loop
- `foobar2000/SDK/guids.cpp`: Overrode `FOOGUIDDECL` (`__declspec(selectany)`) with empty define so `class_guid` symbols become regular externals visible to `lib.exe`, resolving mass LNK2001
- All vcxproj files: Unified CRT/MFC runtime to consistent `/MD` (MultiThreadedDLL), eliminating `STATUS_STACK_BUFFER_OVERRUN` (0xC0000409) crash from mixed `/MT`+`/MD` build

---

## Phase 2 — Rebrand & Refactor (AltaCast → Mcaster1DSPEncoder)

**Goal:** Complete renaming of every AltaCast identity in the codebase. No new features.
"Standalone" is dropped from all names — the application is self-contained by definition.

---

### 2.1 Naming Conventions

| Old Name | New Name | Notes |
|----------|----------|-------|
| `altacastStandalone.exe` | `Mcaster1DSPEncoder.exe` | Drop "Standalone" — implied |
| `altacastStandalone.vcxproj` | `Mcaster1DSPEncoder.vcxproj` | |
| `libaltacast/` (directory) | `libmcaster1dspencoder/` | Rename folder |
| `libaltacast.lib` | `libmcaster1dspencoder.lib` | |
| `libaltacast.vcxproj` | `libmcaster1dspencoder.vcxproj` | |
| `libaltacast.cpp` / `.h` | `libmcaster1dspencoder.cpp` / `.h` | |
| `libaltacast_socket.*` | `libmcaster1dspencoder_socket.*` | |
| `libaltacast_resample.*` | `libmcaster1dspencoder_resample.*` | |
| `dsp_altacast.dll` | `dsp_mcaster1.dll` | Winamp plugin |
| `altacast_winamp.vcxproj` | `mcaster1_winamp.vcxproj` | |
| `altacast_winamp.cpp/.h` | `mcaster1_winamp.cpp/.h` | |
| `dsp_radiodj.dll` | `dsp_mcaster1_radiodj.dll` | RadioDJ plugin |
| `altacast_radiodj.vcxproj` | `mcaster1_radiodj.vcxproj` | |
| `altacast_radiodj.cpp/.h` | `mcaster1_radiodj.cpp/.h` | |
| `foo_altacast.dll` | `foo_mcaster1.dll` | foobar2000 component |
| `altacast_foobar.vcxproj` | `mcaster1_foobar.vcxproj` | |
| `altacast_foobar.cpp` | `mcaster1_foobar.cpp` | |
| `altacast.h` | `mcaster1dspencoder.h` | Shared plugin header |
| `altacast.rc` | `mcaster1dspencoder.rc` | Shared resource file |
| `altacastStandalone.sln` | `Mcaster1DSPEncoder.sln` | |
| `altacastStandalone.h/.cpp` | `Mcaster1DSPEncoder.h/.cpp` | |
| `altacaststandalone_N.cfg` | `mcaster1dspencoder.cfg` | DONE |
| `APP_NAME=altacast_*` | `APP_NAME=mcaster1_*` | Preprocessor defines |

---

### 2.2 Deep Naming Audit

Complete inventory of all 150+ instances requiring change, organized by category.

#### Category 1 — Class Names

| Current | New | File |
|---------|-----|------|
| `CaltacastStandaloneApp` | `CMcaster1DSPEncoderApp` | `altacastStandalone.h:22` |
| `dsp_altacast` (class) | `dsp_mcaster1` | `altacast_foobar.cpp:153,253` |
| `initquit_altacast` | `initquit_mcaster1` | `altacast_foobar.cpp:331` |
| `altacast_play_callback_ui` | `mcaster1_play_callback_ui` | `altacast_foobar.cpp:356` |
| `altacastGlobals` (struct) | `mcaster1Globals` | `libaltacast/libaltacast.h` (all refs) |

#### Category 2 — Function Names

| Current | New | File(s) |
|---------|-----|---------|
| `altacast_init(altacastGlobals *g)` | `mcaster1_init(mcaster1Globals *g)` | `MainWindow.cpp:28,401,954,1005,1274,1360` |
| `altacast_init(altacastGlobals *g)` | `mcaster1_init(mcaster1Globals *g)` | `altacastStandalone.cpp:60` |
| `altacast_init(altacastGlobals *g)` | `mcaster1_init(mcaster1Globals *g)` | `altacast_radiodj.cpp:110` |
| `altacast_init(altacastGlobals *g)` | `mcaster1_init(mcaster1Globals *g)` | `altacast_foobar.cpp:68` |
| `altacast_init(altacastGlobals *g)` | `mcaster1_init(mcaster1Globals *g)` | `altacast_winamp.cpp:110` |

#### Category 3 — Preprocessor Defines & Header Guards

| Current | New | File |
|---------|-----|------|
| `#ifndef __DSP_ALTACAST_H` | `#ifndef __DSP_MCASTER1_H` | `libaltacast.h:1` |
| `#define __DSP_ALTACAST_H` | `#define __DSP_MCASTER1_H` | `libaltacast.h:2` |
| `FRONT_END_ALTACAST_PLUGIN 1` | `FRONT_END_MCASTER1_PLUGIN 1` | `libaltacast.h:110` |
| `AFX_ALTACASTSTANDALONE_H__*` | `AFX_MCASTER1DSPENCODER_H__*` | `altacastStandalone.h:4,5,50` |
| `#ifndef ALTACASTSTANDALONE` | `#ifndef MCASTER1DSPENCODER` | `EditMetadata.cpp:191` |
| `#ifdef ALTACASTSTANDALONE` | `#ifdef MCASTER1DSPENCODER` | `MainWindow.cpp:978,1061,1247` |
| `#ifndef ALTACASTSTANDALONE` | `#ifndef MCASTER1DSPENCODER` | `MainWindow.cpp:1482,1681` |
| `#ifdef altacastSTANDALONE` | `#ifdef MCASTER1DSPENCODER` | `libaltacast.cpp:3090` |
| `IDD_ALTACAST_CONFIG` | `IDD_MCASTER1_CONFIG` | `resource.h:7` |
| `IDD_ALTACAST 301` | `IDD_MCASTER1 301` | `resource.h:30` |
| `IDD = IDD_ALTACAST` | `IDD = IDD_MCASTER1` | `MainWindow.h:56` |
| `ALTACAST_PLUGIN` | `MCASTER1_PLUGIN` | `altacast_foobar.vcxproj:76,124` |
| `ALTACAST_PLUGIN` | `MCASTER1_PLUGIN` | `altacast_radiodj.vcxproj:82,134` |
| `ALTACAST_WINAMP_EXPORTS` | `MCASTER1_WINAMP_EXPORTS` | `altacast_winamp.vcxproj:82,131` |
| `ALTACAST_RADIODJ_EXPORTS` | `MCASTER1_RADIODJ_EXPORTS` | `altacast_radiodj.vcxproj:82,134` |
| `ALTACASTSTANDALONE` | `MCASTER1DSPENCODER` | `altacastStandalone.vcxproj:79,91,127` |
| `APP_NAME=altacast_foobar` | `APP_NAME=mcaster1_foobar` | `altacast_foobar.vcxproj:76,124` |
| `APP_NAME=altacast_radiodj` | `APP_NAME=mcaster1_radiodj` | `altacast_radiodj.vcxproj:82,134` |
| `APP_NAME=altacast_winamp` | `APP_NAME=mcaster1_winamp` | `altacast_winamp.vcxproj:82,131` |

#### Category 4 — String Literals & UI Text

| Current String | New String | File |
|----------------|------------|------|
| `"AltaCast"` (dialog caption) | `"Mcaster1 DSP Encoder"` | `altacast.rc:35` |
| `"AltaCast 1.1"` (about text) | `"Mcaster1 DSP Encoder"` | `altacast.rc:189` |
| `"AltaCast Standalone"` (caption) | `"Mcaster1 DSP Encoder"` | `altacast.rc:197` |
| `"altacast\n"` (foobar info) | `"mcaster1dspencoder\n"` | `altacast_foobar.cpp:36` |
| `"Written by admin@altacast.com\n"` | `"Maintained by davestj@gmail.com\n"` | `altacast_foobar.cpp:37` |
| `"dsp_altacastv2"` (cfg key) | `"dsp_mcaster1v2"` | `altacast_foobar.cpp:40` |
| `"altacast_foo"` (log prefix) | `"mcaster1_foo"` | `altacast_foobar.cpp:104` |
| `"altacast V3"` (foobar version str) | `"mcaster1dspencoder V3"` | `altacast_foobar.cpp:282` |
| `"dsp_altacast"` (log prefix) | `"dsp_mcaster1"` | `altacast_radiodj.cpp:20` |
| `"altacaststandalone"` (log prefix) | `"mcaster1dspencoder"` | `altacastStandalone.cpp:19` |
| `LoadConfigs(currentDir, "altacaststandalone")` | `LoadConfigs(currentDir, "mcaster1dspencoder")` | `altacastStandalone.cpp:168` |
| `"dsp_altacast"` (log prefix) | `"dsp_mcaster1"` | `altacast_winamp.cpp:20` |
| `LoadConfigs(".", "altacaststandalone")` | `LoadConfigs(".", "mcaster1dspencoder")` | `Dummy.cpp:98` |
| `CInternetSession session("altacast")` | `CInternetSession session("mcaster1dspencoder")` | `MainWindow.cpp:193` |
| `"altacast"` (tray notification msg) | `"mcaster1dspencoder"` | `MainWindow.cpp:639` |
| `"altacast.chm"` (help file) | `"mcaster1dspencoder.chm"` | `MainWindow.cpp:1543` |
| `"altacast.log"` (default log name) | `"mcaster1dspencoder.log"` | `libaltacast.cpp:127` |
| `"altacast"` (defaultConfigName) | `"mcaster1dspencoder"` | `libaltacast.cpp:600,670,694` |
| `"altacast_aacp.ini"` | `"mcaster1_aacp.ini"` | `libaltacast.cpp:2090` |
| `"ENCODEDBY","altacast"` (FLAC tag) | `"ENCODEDBY","mcaster1dspencoder"` | `libaltacast.cpp:2415` |
| `strcpy(g->gAppName, "altacast")` | `strcpy(g->gAppName, "mcaster1dspencoder")` | `libaltacast.cpp:2798,3253` |
| `"http://www.altacast.com"` (default URL) | `"https://github.com/davestj/Mcaster1DSPEncoder"` | `libaltacast.cpp:2834` |

#### Category 5 — Resource IDs & Dialog References

| Current | New | File |
|---------|-----|------|
| `IDD_ALTACAST` (dialog resource) | `IDD_MCASTER1` | `altacast.rc:32` |
| `IDD_ALTACAST_CONFIG` | `IDD_MCASTER1_CONFIG` | `resource.h:7` |
| `mainWindow->Create((UINT)IDD_ALTACAST,...)` | `mainWindow->Create((UINT)IDD_MCASTER1,...)` | `altacast_foobar.cpp:131` |
| `mainWindow->Create((UINT)IDD_ALTACAST,...)` | `mainWindow->Create((UINT)IDD_MCASTER1,...)` | `altacast_radiodj.cpp:246` |
| `mainWindow->Create((UINT)IDD_ALTACAST,...)` | `mainWindow->Create((UINT)IDD_MCASTER1,...)` | `altacast_winamp.cpp:246` |
| `mainWindow->Create((UINT)IDD_ALTACAST,...)` | `mainWindow->Create((UINT)IDD_MCASTER1,...)` | `Dummy.cpp:108` |

#### Category 6 — DLL/EXE Output Names in vcxproj

| Current | New | File |
|---------|-----|------|
| `<TargetName>dsp_altacast</TargetName>` | `<TargetName>dsp_mcaster1</TargetName>` | `altacast_winamp.vcxproj:47` |
| `<TargetName>foo_altacast</TargetName>` | `<TargetName>foo_mcaster1</TargetName>` | `altacast_foobar.vcxproj:47` |
| `$(OutDir)dsp_altacast.dll` | `$(OutDir)dsp_mcaster1.dll` | `altacast_winamp.vcxproj:101,148` |
| `dsp_altacast.pdb/.lib/.bsc` | `dsp_mcaster1.pdb/.lib/.bsc` | `altacast_winamp.vcxproj` |
| `foo_altacast.dll/.pdb/.lib` | `foo_mcaster1.dll/.pdb/.lib` | `altacast_foobar.vcxproj:93,103,141` |
| `altacast_radiodj` pdb/lib/bsc | `mcaster1_radiodj` pdb/lib/bsc | `altacast_radiodj.vcxproj` |
| `$(OutDir)altacastStandalone.exe` | `$(OutDir)Mcaster1DSPEncoder.exe` | `altacastStandalone.vcxproj:96,144` |
| `libaltacast.lib/.pch/.bsc` | `libmcaster1dspencoder.lib/.pch/.bsc` | `libaltacast.vcxproj:76,81,105,110` |

#### Category 7 — Solution Files (.sln)

| Current | New | File |
|---------|-----|------|
| Project name `"altacastStandalone"` | `"Mcaster1DSPEncoder"` | `altacastStandalone.sln:6` |
| Project name `"libaltacast"` | `"libmcaster1dspencoder"` | all `.sln` files |
| Project name `"altacast_winamp"` | `"mcaster1_winamp"` | `altacast_winamp.sln` |
| Project name `"altacast_foobar"` | `"mcaster1_foobar"` | `altacast_foobar.sln` |
| Project name `"altacast_radiodj"` | `"mcaster1_radiodj"` | `altacast_radiodj.sln` |

#### Category 8 — vcxproj Internal Paths & Namespaces

| Current | New | Files |
|---------|-----|-------|
| `<RootNamespace>altacastStandalone</RootNamespace>` | `<RootNamespace>mcaster1dspencoder</RootNamespace>` | `altacastStandalone.vcxproj:19` |
| `<RootNamespace>altacast_*</RootNamespace>` | `<RootNamespace>mcaster1_*</RootNamespace>` | all plugin vcxproj:19 |
| `<RootNamespace>libaltacast</RootNamespace>` | `<RootNamespace>libmcaster1dspencoder</RootNamespace>` | `libaltacast.vcxproj:15` |
| `libaltacast;libaltacast_config` (include dirs) | `libmcaster1dspencoder;libmcaster1dspencoder_config` | all vcxproj |
| `libaltacast/Debug` (lib dirs) | `libmcaster1dspencoder/Debug` | all vcxproj |
| `.\Debug/altacast_*.pch/.tlb/.bsc` | `.\Debug/mcaster1_*.pch/.tlb/.bsc` | all vcxproj |
| `ProjectReference Include="libaltacast\libaltacast.vcxproj"` | `ProjectReference Include="libmcaster1dspencoder\libmcaster1dspencoder.vcxproj"` | all consumer vcxproj |
| `Include="altacast.h"` / `Include="altacast.rc"` | `Include="mcaster1dspencoder.h"` / `.rc"` | all vcxproj |
| `Include="altacast_winamp.cpp"` | `Include="mcaster1_winamp.cpp"` | `mcaster1_winamp.vcxproj` |
| `Include="altacast_foobar.cpp"` | `Include="mcaster1_foobar.cpp"` | `mcaster1_foobar.vcxproj` |
| `Include="altacast_radiodj.cpp"` | `Include="mcaster1_radiodj.cpp"` | `mcaster1_radiodj.vcxproj` |
| `Include="libaltacast.cpp/h"` | `Include="libmcaster1dspencoder.cpp/h"` | `libmcaster1dspencoder.vcxproj` |
| `Include="libaltacast_socket.h"` | `Include="libmcaster1dspencoder_socket.h"` | `libmcaster1dspencoder.vcxproj` |
| `Include="libaltacast_resample.h"` | `Include="libmcaster1dspencoder_resample.h"` | `libmcaster1dspencoder.vcxproj` |

#### Category 9 — InnoSetup Installer

All references in `*.iss` installer script:
- App name, app publisher URL, output base filename
- `Source:` file paths for `dsp_altacast.dll`, `foo_altacast.dll`, `altacastStandalone.exe`
- `[Registry]` keys if any use `AltaCast` as a key name

---

### 2.3 File & Directory Renames

Ordered sequence to avoid broken references mid-rename:

```
Step 1 — Rename source files inside libaltacast/:
  libaltacast.cpp         → libmcaster1dspencoder.cpp
  libaltacast.h           → libmcaster1dspencoder.h
  libaltacast_socket.cpp  → libmcaster1dspencoder_socket.cpp
  libaltacast_socket.h    → libmcaster1dspencoder_socket.h
  libaltacast_resample.h  → libmcaster1dspencoder_resample.h
  libaltacast.vcxproj     → libmcaster1dspencoder.vcxproj

Step 2 — Rename libaltacast/ folder:
  src/libaltacast/        → src/libmcaster1dspencoder/

Step 3 — Rename plugin source files (in their respective folders):
  altacast_winamp.cpp     → mcaster1_winamp.cpp
  altacast_winamp.h       → mcaster1_winamp.h
  altacast_foobar.cpp     → mcaster1_foobar.cpp
  altacast_radiodj.cpp    → mcaster1_radiodj.cpp
  altacast_radiodj.h      → mcaster1_radiodj.h

Step 4 — Rename shared files:
  altacast.h              → mcaster1dspencoder.h
  altacast.rc             → mcaster1dspencoder.rc
  altacastStandalone.h    → Mcaster1DSPEncoder.h
  altacastStandalone.cpp  → Mcaster1DSPEncoder.cpp

Step 5 — Rename project files:
  altacastStandalone.vcxproj  → Mcaster1DSPEncoder.vcxproj
  altacast_winamp.vcxproj     → mcaster1_winamp.vcxproj
  altacast_foobar.vcxproj     → mcaster1_foobar.vcxproj
  altacast_radiodj.vcxproj    → mcaster1_radiodj.vcxproj

Step 6 — Rename solution files:
  altacastStandalone.sln  → Mcaster1DSPEncoder.sln
  altacast_winamp.sln     → mcaster1_winamp.sln
  altacast_foobar.sln     → mcaster1_foobar.sln
  altacast_radiodj.sln    → mcaster1_radiodj.sln
```

---

### 2.4 Credits & Attribution

In all About dialogs, splash screens, installer text, and source file headers:

```
Mcaster1 DSP Encoder
Maintainer: Dave St. John <davestj@gmail.com>
https://github.com/davestj/Mcaster1DSPEncoder

Based on EdCast/Oddsock DSP encoder by Ed Zaleski
Original project: https://www.oddsock.org/
```

All source file copyright comment blocks — ADD, do not replace original:
```cpp
// Mcaster1DSPEncoder — fork of EdCast/Oddsock DSP encoder
// Maintainer: Dave St. John <davestj@gmail.com>
// Original Author: Ed Zaleski
```

---

### 2.5 MP3 — Replace BladeMP3 with Native LAME

On Windows, AltaCast uses `BladeMP3EncDLL.h` — an obsolete Windows-only DLL interface.
All US MP3 patents expired April 23, 2017. LAME (LGPL) is now freely distributable.
LAME 3.100 source is in `external/lame/`; also available via vcpkg `mp3lame:x86-windows`.

**Steps:**
1. Upgrade `external/lame/vc_solution/vc9_libmp3lame_dll.vcproj` to v143, build `libmp3lame.dll`
2. Replace `#ifdef WIN32` BladeMP3 guards with `#ifdef HAVE_LAME` using standard `lame.h` API
3. Remove `BladeMP3EncDLL.h` from `external/include/`
4. Distribute `libmp3lame.dll` with all targets

---

### 2.6 Dual-Platform Build: x86 (Win32) and x64

**Goal:** Ship both 32-bit and 64-bit builds to maximize Windows compatibility.
x64 for modern systems (Windows 10/11), x86 for legacy environments still running
32-bit Windows or 32-bit host applications (older Winamp builds, 32-bit RadioDJ, etc.).

**Current state:** All targets are Win32 only. vcpkg at `C:\vcpkg` has x64-windows packages
installed. x86-windows triplets need to be added.

#### Step 1 — Install x86-windows vcpkg triplets

```powershell
cd C:\vcpkg
.\vcpkg install `
  opus:x86-windows `
  libopusenc:x86-windows `
  mp3lame:x86-windows `
  libyaml:x86-windows `
  libflac:x86-windows `
  libogg:x86-windows `
  libvorbis:x86-windows `
  fdk-aac[he-aac]:x86-windows `
  libxml2:x86-windows `
  libiconv:x86-windows `
  curl:x86-windows
```

x64-windows packages are already installed. Both triplets will coexist in vcpkg without conflict.

#### Step 2 — Add x64 configurations to all vcxproj files

Each project currently has `Win32|Debug` and `Win32|Release` configurations. Add:
- `x64|Debug`
- `x64|Release`

Key changes per config group:
- `<Platform>Win32</Platform>` → `<Platform>x64</Platform>`
- Linker: `<TargetMachine>MachineX86</TargetMachine>` → `<TargetMachine>MachineX64</TargetMachine>`
- vcpkg integration paths: `$(VCPKG_ROOT)\installed\x64-windows\` for x64 configs,
  `$(VCPKG_ROOT)\installed\x86-windows\` for Win32 configs
- Output dirs: `$(OutDir)` already varies by config — ensure `x64\Release\` and `Win32\Release\`
  are distinct so builds don't overwrite each other

#### Step 3 — vcxproj include/lib path pattern (per-config)

```xml
<!-- Win32 Release -->
<AdditionalIncludeDirectories>$(VCPKG_ROOT)\installed\x86-windows\include;...</AdditionalIncludeDirectories>
<AdditionalLibraryDirectories>$(VCPKG_ROOT)\installed\x86-windows\lib;...</AdditionalLibraryDirectories>

<!-- x64 Release -->
<AdditionalIncludeDirectories>$(VCPKG_ROOT)\installed\x64-windows\include;...</AdditionalIncludeDirectories>
<AdditionalLibraryDirectories>$(VCPKG_ROOT)\installed\x64-windows\lib;...</AdditionalLibraryDirectories>
```

Use an MSBuild property to keep this DRY:
```xml
<PropertyGroup>
  <VcpkgTriplet Condition="'$(Platform)'=='Win32'">x86-windows</VcpkgTriplet>
  <VcpkgTriplet Condition="'$(Platform)'=='x64'">x64-windows</VcpkgTriplet>
  <VcpkgInclude>$(VCPKG_ROOT)\installed\$(VcpkgTriplet)\include</VcpkgInclude>
  <VcpkgLib>$(VCPKG_ROOT)\installed\$(VcpkgTriplet)\lib</VcpkgLib>
</PropertyGroup>
```

#### Step 4 — Release artifact layout

```
dist/
  Win32/
    Mcaster1DSPEncoder.exe
    dsp_mcaster1.dll
    dsp_mcaster1_radiodj.dll
    foo_mcaster1.dll
    libmp3lame.dll
    opus.dll  opusenc.dll  fdk-aac.dll  ...
  x64/
    Mcaster1DSPEncoder.exe
    dsp_mcaster1.dll
    dsp_mcaster1_radiodj.dll
    foo_mcaster1.dll
    libmp3lame.dll
    opus.dll  opusenc.dll  fdk-aac.dll  ...
```

#### Compatibility Notes

| Target | x86 Build | x64 Build |
|--------|-----------|-----------|
| `Mcaster1DSPEncoder.exe` | Win7+ (32-bit) | Win10+ (64-bit) |
| `dsp_mcaster1.dll` (Winamp) | Winamp 5.x 32-bit | Winamp 5.9+ 64-bit |
| `foo_mcaster1.dll` (foobar2000) | foobar2000 32-bit | foobar2000 64-bit |
| `dsp_mcaster1_radiodj.dll` | RadioDJ (32-bit) | RadioDJ 64-bit future |

> **Note:** DSP plugins must match the bitness of the host application. Ship both so users
> can install whichever matches their Winamp/foobar2000/RadioDJ installation.

---

### 2.7 Build System Cleanup

- Update `build_all.ps1`: build both `Win32` and `x64` for all targets, copy runtime DLLs
  from correct vcpkg triplet per platform, populate `dist/Win32/` and `dist/x64/`
- Remove `build.ps1` and `build_winamp.ps1` (consolidated into `build_all.ps1`)
- Add `.gitignore` covering: `Release/`, `Debug/`, `x64/`, `Win32/`, `dist/`, `*.user`,
  `*.suo`, `*.aps`, `*.pch`, `*.obj`, `*.ilk`, `*.exp`, `ipch/`, `.vs/`, `*.log`,
  `*.tlog`, `*.lastbuildstate`
- Initialize fresh git repository in project root

---

### 2.8 Phase 2 Deliverables

- [ ] All source files, folders, and projects renamed per Section 2.3
- [ ] All 150+ string/symbol instances replaced per Section 2.2 deep audit
- [ ] All 4 targets compile clean (x86 and x64) with zero AltaCast references remaining
- [ ] x86-windows vcpkg triplets installed for all required packages
- [ ] x64 configurations added to all vcxproj files alongside existing Win32 configs
- [ ] VcpkgTriplet MSBuild property wires correct include/lib paths per platform
- [ ] About dialogs show correct attribution (Ed Zaleski original, davestj maintainer)
- [ ] `Mcaster1DSPEncoder.exe` launches and encodes — both x86 and x64 builds
- [ ] `dsp_mcaster1.dll` loads in Winamp — both x86 and x64 builds
- [ ] `foo_mcaster1.dll` loads in foobar2000 — both x86 and x64 builds
- [ ] `dsp_mcaster1_radiodj.dll` loads in RadioDJ — both x86 and x64 builds
- [ ] Native LAME MP3 via `libmp3lame.dll` working, BladeMP3 removed
- [ ] `dist/Win32/` and `dist/x64/` populated with correct runtime DLLs
- [ ] `build_all.ps1` builds both platforms in one pass
- [ ] `README.md` updated
- [ ] `.gitignore` in place
- [ ] Initial git commit tagged `v0.2.0`

---

## Phase 3 — Audio Backend Modernization

**Goal:** Remove WMA (end-of-life). Add dual MP3 encoder paths (LAME built-in with full VBR
+ LAME ACM for system-installed codecs). Add Opus and AAC/AAC+/AAC++ (HE-AAC via fdk-aac).
Replace bass.dll with statically-linked PortAudio for wide device support. Achieve full
format parity with RadioBOSS: MP3 (×2), AAC-LC, AAC+, AAC++, Ogg Vorbis, Opus, FLAC.

### vcpkg Packages Available (x64-windows at C:\vcpkg)

| Package | Version | Purpose |
|---------|---------|---------|
| `opus:x64-windows` | 1.5.2 | Core Opus codec |
| `libopusenc:x64-windows` | 0.3 | High-level Opus encoding API |
| `opusfile:x64-windows` | 0.12 | Opus stream decoding |
| `mp3lame:x64-windows` | 3.100 | LAME MP3 built-in encoder |
| `portaudio` (custom build) | 19.7.0 | Device capture — WASAPI/DS/MME/WDM-KS/ASIO (**built from source with Steinberg SDK**) |
| `libyaml:x64-windows` | 0.2.5 | YAML config (Phase 4) |
| `libflac:x64-windows` | 1.5.0 | Updated FLAC |
| `libogg:x64-windows` | 1.3.6 | Updated Ogg |
| `libvorbis:x64-windows` | 1.3.7 | Updated Vorbis |
| `fdk-aac:x64-windows` | 2.0.3 | AAC / AAC+ / HE-AAC v2 (**install required**) |

All available after x64 migration in Phase 2.

**Install required packages before Phase 3:**
```powershell
cd C:\vcpkg
.\vcpkg install fdk-aac[he-aac]:x64-windows fdk-aac[he-aac]:x86-windows
.\vcpkg install portaudio:x64-windows portaudio:x86-windows
```
The `[he-aac]` feature enables HE-AAC (AAC+) and HE-AAC v2 (AAC++) profiles alongside
standard AAC-LC. Note: HE-AAC patents are licensed through the Via Licensing AAC patent
pool — verify your distribution license before shipping commercially.

### 3.1 Remove basswma.dll / WMA

- Remove `basswma.dll/.lib` from `external/lib/`
- Remove `basswma.h` from `external/include/`
- Remove all WMA encoder code paths from `libmcaster1dspencoder.cpp`
- Remove WMA UI controls from all plugin config dialogs
- Remove `basswma.lib` from all vcxproj linker inputs

### 3.2 Dual MP3 Encoder Paths

Two MP3 encoder types will be presented in the format dropdown, giving users choice based
on their installed software and workflow preferences.

#### Path A — LAME Built-In (direct `lame.h` API)

Ships with `libmp3lame.dll` bundled in the release. Always available — no user action
required. Exposes the full LAME parameter set including CBR, ABR, and VBR modes.

**CBR mode:**
```c
lame_set_VBR(gfp, vbr_off);
lame_set_brate(gfp, 128);          // fixed bitrate: 64, 96, 128, 192, 256, 320
```

**VBR mode (quality-based, V0–V9):**
```c
lame_set_VBR(gfp, vbr_mtrh);      // LAME's best VBR algorithm
lame_set_VBR_q(gfp, 2);           // V0=best quality, V9=smallest file
lame_set_VBR_min_bitrate_kbps(gfp, 64);
lame_set_VBR_max_bitrate_kbps(gfp, 320);
```

**ABR mode (average bitrate):**
```c
lame_set_VBR(gfp, vbr_abr);
lame_set_VBR_mean_bitrate_kbps(gfp, 192);
lame_set_VBR_min_bitrate_kbps(gfp, 128);
lame_set_VBR_max_bitrate_kbps(gfp, 320);
```

**Config UI for LAME built-in:**
- Mode dropdown: `CBR` / `VBR (V0–V9)` / `ABR`
- CBR: bitrate selector (64 / 96 / 128 / 160 / 192 / 256 / 320)
- VBR: quality slider V0–V9, min/max bitrate clamps
- ABR: target bitrate, min/max clamps
- Quality: `lame_set_quality()` 0–9 (encode speed vs quality tradeoff)
- Joint stereo / stereo / mono selector

#### Path B — LAME ACM (Windows Audio Compression Manager)

Uses the Windows ACM API (`msacm32.lib` — standard Windows SDK, no extra DLL to ship).
Requires the user to have an ACM-compatible MP3 codec installed — LAME ACM is the most
common (free, widely installed in radio/broadcast environments). Also works with
Fraunhofer MP3 ACM or any other registered ACM MP3 driver.

At startup the encoder enumerates all ACM codecs reporting `WAVE_FORMAT_MPEGLAYER3` and
populates the device list. If none are found the option is greyed out with a tooltip:
`"Requires LAME ACM or compatible MP3 ACM codec to be installed"`.

```c
#include <msacm.h>
// Enumerate installed ACM MP3 codecs at runtime
acmDriverEnum(acmDriverEnumCallback, 0, 0);

// Open an ACM encode stream (PCM → MP3)
MPEGLAYER3WAVEFORMAT wfxDst = {0};
wfxDst.wfx.wFormatTag      = WAVE_FORMAT_MPEGLAYER3;
wfxDst.wfx.nChannels        = 2;
wfxDst.wfx.nSamplesPerSec   = 44100;
wfxDst.wfx.nAvgBytesPerSec  = 128000 / 8;   // CBR 128 kbps
wfxDst.wfx.nBlockAlign      = 1;
wfxDst.wfx.cbSize            = MPEGLAYER3_WFX_EXTRA_BYTES;
wfxDst.wID                  = MPEGLAYER3_ID_MPEG;
wfxDst.fdwFlags             = MPEGLAYER3_FLAG_PADDING_OFF;
wfxDst.nBlockSize           = 144 * 128 * 1000 / 44100;
wfxDst.nFramesPerBlock      = 1;
wfxDst.nCodecDelay          = 1393;

acmStreamOpen(&hStream, NULL, &wfxPCM, (WAVEFORMATEX*)&wfxDst, NULL, 0, 0, 0);
acmStreamConvert(hStream, &acmHdr, ACM_STREAMCONVERTF_BLOCKALIGN);
acmStreamClose(hStream, 0);
// No extra DLL to ship — msacm32.lib is a standard Windows SDK library
```

**Config UI for LAME ACM:**
- Bitrate selector: populated from what the installed ACM codec reports (typically
  32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 kbps CBR)
- Samplerate: 44100 / 48000 / 32000 (from codec's reported format list)
- Channels: stereo / mono
- No VBR — ACM interface is CBR only

**Encoder type labels in UI:**
- `MP3 — LAME (built-in)` — CBR / VBR / ABR, always available
- `MP3 — ACM` — CBR only, requires ACM codec installed (greyed out if none detected)

---

### 3.3 Add Opus Encoder

vcpkg provides `opus` + `libopusenc` ready to link. Integration via `libopusenc` API:

```c
OggOpusComments *comments = ope_comments_create();
ope_comments_add(comments, "ENCODER", "mcaster1dspencoder");
OggOpusEnc *enc = ope_encoder_create_callbacks(
    &callbacks, userdata, comments, 48000, channels, 0, &error);
ope_encoder_ctl(enc, OPUS_SET_BITRATE(bitrate * 1000));
ope_encoder_ctl(enc, OPUS_SET_COMPLEXITY(10));
ope_encoder_write(enc, pcm, samples_per_channel);
ope_encoder_drain(enc);
ope_encoder_destroy(enc);
ope_comments_destroy(comments);
```

MIME: `audio/ogg; codecs=opus`. Mount point extension: `.opus`.

### 3.4 Add AAC and AAC+ (HE-AAC) via fdk-aac

Replace the legacy `libfaac` dependency with `fdk-aac`, which supports AAC-LC, HE-AAC
(AAC+), and HE-AAC v2 (AAC++/eAAC+). This achieves full RadioBOSS format parity.

**Format matrix:**

| Format | Profile | Typical Bitrate | Best For |
|--------|---------|----------------|----------|
| AAC-LC | Low Complexity | 96–256 kbps | High quality, broad compatibility |
| AAC+ (HE-AAC v1) | High Efficiency | 32–96 kbps | Low-bitrate streaming |
| AAC++ (HE-AAC v2) | High Efficiency v2 | 16–48 kbps | Very low bitrate (SBR + PS) |

**Integration via fdk-aac C API:**

```c
#include <fdk-aac/aacenc_lib.h>

HANDLE_AACENCODER hEncoder;
aacEncOpen(&hEncoder, 0, channels);

// AAC-LC: AOT_AAC_LC (2)
// HE-AAC v1 (AAC+): AOT_SBR (5)
// HE-AAC v2 (AAC++): AOT_PS (29)
aacEncoder_SetParam(hEncoder, AACENC_AOT, AOT_SBR);   // AAC+ profile
aacEncoder_SetParam(hEncoder, AACENC_SAMPLERATE, 44100);
aacEncoder_SetParam(hEncoder, AACENC_CHANNELMODE, MODE_2);
aacEncoder_SetParam(hEncoder, AACENC_BITRATE, 64000);
aacEncoder_SetParam(hEncoder, AACENC_TRANSMUX, TT_MP4_ADTS); // ADTS for streaming

aacEncEncode(hEncoder, &inBufDesc, &outBufDesc, &inArgs, &outArgs);
aacEncClose(&hEncoder);
```

**MIME types:**
- AAC-LC: `audio/aac` (ADTS stream)
- AAC+: `audio/aac` (ADTS with SBR)
- AAC++: `audio/aac` (ADTS with SBR+PS)

**Config UI additions:**
- Format dropdown: `AAC-LC` / `AAC+ (HE-AACv1)` / `AAC++ (HE-AACv2)`
- Bitrate guidance per profile (auto-limit to sensible ranges)
- Samplerate: 44100 / 48000 (HE-AAC internally uses SBR to double apparent rate)

**Recommended bitrates (RadioBOSS parity):**
- AAC-LC: 96, 128, 192, 256 kbps
- AAC+: 32, 40, 48, 64 kbps
- AAC++: 16, 24, 32 kbps

### 3.5 Replace bass.dll with PortAudio (statically linked, bundled)

**Why bass.dll is being replaced:**
The current `bass.dll` (v2.4) device enumeration is incomplete — it surfaces only capture
devices (microphone inputs) and misses the full Windows audio device graph. Playback
devices, WASAPI loopback devices (for capturing what's playing), and aggregate devices are
not visible, which makes `Mcaster1DSPEncoder.exe` unusable for the primary broadcast
use-case of capturing a virtual audio cable or soundcard output.

**PortAudio v19** enumerates the full device set across all available Windows audio backends,
including ASIO for professional hardware:

| Backend | Devices Exposed | Max Sample Rate | Notes |
|---------|----------------|-----------------|-------|
| WASAPI | All render + capture devices, loopback on render devices | Hardware limit (192kHz+) | Best for modern systems |
| DirectSound | All DirectSound-compatible devices | Up to 192kHz | Broadest legacy compatibility |
| MME | Windows Multimedia devices | Up to 192kHz | Widest hardware support, older cards |
| WDM-KS | Kernel-streaming direct to driver | Up to 192kHz+ | Low latency, bypasses mixer |
| ASIO | Pro audio interfaces (RME, MOTU, Focusrite, UA, etc.) | 192kHz / 384kHz / 768kHz | Lowest latency, direct hardware path |

#### ASIO Support

ASIO (Audio Stream Input/Output) is the professional standard for low-latency direct
hardware access. PortAudio supports it natively via `pa_asio.h`, but it requires the
Steinberg ASIO SDK at build time. The SDK is a free download from Steinberg; its license
prohibits redistribution of the SDK source but permits distribution of compiled binaries.

**One-time ASIO build setup:**
```powershell
# 1. Download Steinberg ASIO SDK 2.3 from:
#    https://www.steinberg.net/asiosdk
#    Extract to: C:\ASIOSDK2.3\

# 2. Build PortAudio from source with ASIO + static enabled
git clone https://github.com/PortAudio/portaudio.git portaudio_src
cmake -S portaudio_src -B portaudio_build_x86 `
    -A Win32 `
    -DPA_USE_ASIO=ON `
    -DASIO_SDK_DIR="C:\ASIOSDK2.3" `
    -DPA_BUILD_STATIC=ON `
    -DPA_BUILD_SHARED=OFF
cmake --build portaudio_build_x86 --config Release

# Result: portaudio_build_x86\Release\portaudio_static.lib  (x86 with ASIO)
# Repeat with -A x64 for 64-bit
```

At runtime, ASIO devices appear in the same `Pa_GetDeviceInfo()` enumeration alongside
WASAPI/MME/DS devices. No special UI path — the user just picks their ASIO interface from
the device dropdown. The `PaAsio_GetInputChannelName()` API can be used to show per-channel
names as reported by the ASIO driver (useful for multi-channel interfaces).

#### 192kHz and High Sample Rate Handling

PortAudio exposes whatever rates the driver supports. Pro soundcards and ASIO interfaces
commonly support 44100, 48000, 88200, 96000, 176400, and 192000 Hz. Some professional
ASIO interfaces (RME HDSPe, Merging Hapi) support 352800 / 384000 Hz.

Since no streaming format transmits above 48000 Hz, a resample stage is required when
the capture rate exceeds the encoder input rate:

```
[192kHz capture via PortAudio] → [libmcaster1dspencoder_resample] → [44100/48000 encoder input]
```

The existing `libmcaster1dspencoder_resample.h` (based on the Ogg resampler in the
codebase) handles this. The resample stage is bypassed when capture rate == encoder rate.

**Supported capture sample rates (UI dropdown in `Mcaster1DSPEncoder.exe`):**
`44100` / `48000` / `88200` / `96000` / `176400` / `192000` Hz

Encoder output is always 44100 or 48000 Hz (per-format recommendation shown in UI).

**Deployment — statically linked, zero extra DLL:**
PortAudio (with ASIO) is built as a static library and linked directly into
`Mcaster1DSPEncoder.exe`. No `portaudio.dll` is shipped or required — fully baked in.
Users with ASIO hardware simply have their ASIO driver installed (which they already do
to use the hardware with any other application).

**Integration:**
```c
Pa_Initialize();

// Enumerate all devices — WASAPI, MME, DS, WDM-KS, and ASIO
int numDevices = Pa_GetDeviceCount();
for (int i = 0; i < numDevices; i++) {
    const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
    const PaHostApiInfo *api = Pa_GetHostApiInfo(info->hostApi);
    // api->type: paWASAPI, paDirectSound, paMME, paWDMKS, paASIO
    // info->maxInputChannels > 0  → usable as capture source
    // info->defaultSampleRate     → driver's preferred rate
}

// Query supported rates for selected device (show in UI)
double testRates[] = {44100, 48000, 88200, 96000, 176400, 192000};
for (int r = 0; r < 6; r++) {
    PaError err = Pa_IsFormatSupported(&inputParams, NULL, testRates[r]);
    if (err == paFormatIsSupported) { /* add to UI dropdown */ }
}

// Open stream at selected rate — PortAudio handles the driver negotiation
Pa_OpenStream(&stream, &inputParams, NULL, captureRate, 512, paNoFlag, audioCallback, ud);
Pa_StartStream(stream);
// Resample captureRate → encoderRate in audioCallback if rates differ
```

**Changes to build:**
- Remove `bass.lib` from `Mcaster1DSPEncoder.vcxproj` linker inputs
- Remove `bass.dll` from `Release/` and `dist/` copy steps
- Remove `bass.lib` from Winamp / foobar2000 / RadioDJ plugin vcxproj linker deps
  (plugins intercept audio from the host app — they never need device capture)
- Add `portaudio_static_x86.lib` (Win32 + ASIO) / `portaudio_static_x64.lib` (x64 + ASIO)
  to `Mcaster1DSPEncoder.vcxproj` only
- Add `winmm.lib`, `ole32.lib`, `uuid.lib` to linker (PortAudio static dependencies)

### 3.6 Format & Device Parity Summary

**Encoder formats:**

| Format | Library | Type | Status After Phase 3 |
|--------|---------|------|---------------------|
| MP3 — LAME built-in | `libmp3lame.dll` | CBR / VBR / ABR | ✓ New in Phase 3 (full VBR) |
| MP3 — ACM | Windows `msacm32.lib` | CBR only | ✓ New in Phase 3 (runtime detect) |
| AAC-LC | fdk-aac | CBR | ✓ New in Phase 3 |
| AAC+ (HE-AACv1) | fdk-aac `[he-aac]` | CBR | ✓ New in Phase 3 |
| AAC++ (HE-AACv2) | fdk-aac `[he-aac]` | CBR | ✓ New in Phase 3 |
| Ogg Vorbis | libvorbis | CBR / VBR | ✓ Existing (updated) |
| Opus | libopusenc | CBR / VBR | ✓ New in Phase 3 |
| FLAC | libFLAC | Lossless | ✓ Existing (updated) |
| WMA | basswma.dll | — | ✗ Removed (EOL) |

**Device capture (Mcaster1DSPEncoder.exe):**

| Backend | Type | Max Rate | Status After Phase 3 |
|---------|------|----------|---------------------|
| WASAPI (incl. loopback) | Software | 192kHz+ | ✓ PortAudio baked in |
| DirectSound | Software | 192kHz | ✓ PortAudio baked in |
| MME | Software | 192kHz | ✓ PortAudio baked in |
| WDM-KS | Software / kernel | 192kHz+ | ✓ PortAudio baked in |
| ASIO | Pro hardware | 192kHz / 384kHz+ | ✓ PortAudio + Steinberg SDK |
| bass.dll | Legacy | Limited | ✗ Removed |

### 3.7 Phase 3 Deliverables

**Audio Removal:**
- [ ] `basswma.dll` removed, WMA code paths deleted from `libmcaster1dspencoder.cpp`
- [ ] `bass.dll` / `bass.lib` removed from `Mcaster1DSPEncoder.vcxproj` and all plugin vcxproj files

**MP3:**
- [ ] LAME built-in MP3 encoder integrated — CBR, VBR (V0–V9), ABR modes in all 4 targets
- [ ] ACM MP3 encoder path integrated — runtime ACM codec enumeration, CBR, in all 4 targets
- [ ] ACM path gracefully greyed out when no `WAVE_FORMAT_MPEGLAYER3` ACM codec is detected

**AAC:**
- [ ] Steinberg ASIO SDK downloaded, PortAudio built from source with ASIO + static enabled (x86 + x64)
- [ ] `fdk-aac[he-aac]` installed via vcpkg (x86 + x64) and linked
- [ ] AAC-LC encoder integrated in all 4 targets
- [ ] AAC+ (HE-AAC v1) encoder integrated in all 4 targets
- [ ] AAC++ (HE-AAC v2) encoder integrated in all 4 targets
- [ ] Legacy `libfaac` removed (replaced by fdk-aac)

**Opus:**
- [ ] Opus encoder integrated in all 4 targets via `libopusenc`
- [ ] Opus Ogg stream verified in Icecast and VLC

**Verification:**
- [ ] AAC ADTS stream verified in Icecast and compatible players
- [ ] MP3 VBR stream verified in Icecast and media players
- [ ] All format streams verified on both x86 and x64 builds

**Device Capture:**
- [ ] PortAudio statically linked into `Mcaster1DSPEncoder.exe` — no separate DLL shipped
- [ ] ASIO backend included (built with Steinberg SDK) — zero user install needed beyond ASIO driver
- [ ] Full device list verified: WASAPI loopback, ASIO interfaces, playback + capture devices all visible
- [ ] Capture sample rate UI: 44100 / 48000 / 88200 / 96000 / 176400 / 192000 Hz selectable
- [ ] Rates reported as unsupported by selected device auto-greyed in UI
- [ ] Resample stage active when capture rate ≠ encoder rate (via `libmcaster1dspencoder_resample`)
- [ ] 192kHz ASIO capture → 48000 Hz Opus encode verified end-to-end

**UI:**
- [ ] Format dropdown updated in all 4 plugin config dialogs (all new encoder types present)
- [ ] Device + sample rate selector in `Mcaster1DSPEncoder.exe` shows full device graph

**Build & Dist:**
- [ ] `winmm.lib`, `ole32.lib`, `uuid.lib` added to `Mcaster1DSPEncoder.vcxproj` linker (PortAudio deps)
- [ ] `build-all-rebrand.ps1` updated — copies `libmp3lame.dll`, `fdk-aac.dll`, `opus.dll`, `opusenc.dll`
- [ ] `bass.dll` removed from copy step in build script
- [ ] All 4 targets build clean, zero errors on x86 and x64

**Phase 3 Hardware Test Matrix (available test devices):**

| Device | Interface | Backend | Target Test |
|--------|-----------|---------|------------|
| Steinberg USB audio interface(s) | USB | ASIO (Yamaha/Steinberg driver) | ASIO enumeration, 44100 / 96000 / 192000 capture |
| Universal Audio (UAudio) USB | USB | ASIO (UA driver) | ASIO enumeration, low-latency 192kHz capture |
| ASUS Xonar soundcard | PCIe | WASAPI / WDM-KS / ASIO (Xonar ASIO) | 192kHz WASAPI + ASIO, loopback capture |
| Offbrand 192kHz USB soundcard(s) | USB | WASAPI / MME | 192kHz WASAPI capture, resample-to-48kHz verify |

All devices will be tested for:
- Correct appearance in `Mcaster1DSPEncoder.exe` device dropdown
- Stable capture at 44100, 48000, 96000, and 192000 Hz where supported
- Clean resample from 192kHz → 48kHz before encode (Opus / AAC verification)
- ASIO channel name display for multi-channel interfaces
- End-to-end stream to local Icecast test instance at each sample rate

---

## Phase 4 — YAML Multi-Station Configuration

**Goal:** Replace legacy flat `key=value` config with structured YAML supporting multiple
simultaneous stations, per-station encoder chains, ICY/YP metadata, and save-to-WAV.

Note: `libyaml:x64-windows 0.2.5` is already installed in vcpkg — ready to integrate.

### 4.1 Current Config Problems

**How the numbered files actually work (from source code):**

The encoder maintains an array `altacastGlobals *g[MAX_ENCODERS]` (max 10 slots, defined in
`MainWindow.h:43`). On startup, each active slot is assigned `encoderNumber = i + 1`
(1-indexed). Config filenames are then constructed in `libaltacast.cpp` as:

```c
sprintf(configFile, "%s_%d.cfg", g->gConfigFileName, g->encoderNumber);
// → mcaster1dspencoder_1.cfg, mcaster1dspencoder_2.cfg, ...
```

So each numbered file represents **one encoder connection slot** — not one station. If you
want to stream MP3 and Ogg simultaneously to the same server you need two slots (`_1.cfg`
and `_2.cfg`) each pointing at the same server but with different format settings.

**Problems with this approach:**

- **No station grouping concept** — the config system has no notion of a "station". Encoder
  slots that logically belong together (same server, same ICY metadata, different formats)
  have no way to express that relationship in config.
- **Duplicated metadata** — ICY name, genre, URL, password must be copy-pasted into every
  numbered file for encoders targeting the same server.
- **Opaque numbering** — `_1.cfg` vs `_2.cfg` carries no semantic meaning; figuring out which
  slot goes where requires reading every file.
- **Hard cap of 10 slots** (`MAX_ENCODERS 10`) — increasing requires a recompile.
- **Flat key namespace** — no grouping or structure within each file; no inline comments
  describing what the encoder is for.
- **No archive configuration** — no built-in save-to-WAV settings per encoder slot.

### 4.2 New YAML Config Format

File: `mcaster1dspencoder.yaml`

```yaml
global:
  log_level: info
  log_file: mcaster1dspencoder.log
  reconnect_delay: 5
  reconnect_on_drop: true

stations:
  - id: 1
    name: "My Rock Station"
    enabled: true
    icy:
      name: "My Rock Station"
      genre: "Rock"
      url: "https://mystation.example.com"
      description: "The best rock on the internet"
      pub: true
    server:
      type: icecast2
      host: stream.example.com
      port: 8000
      password: "hackme"
      mount: /rock
    encoders:
      - format: mp3
        bitrate: 128
        samplerate: 44100
        channels: 2
        quality: 5
        vbr: false
      - format: ogg
        bitrate: 96
        samplerate: 44100
        channels: 2
        quality: 0.6
      - format: opus
        bitrate: 128
        samplerate: 48000
        channels: 2
        complexity: 10
        signal: audio
      - format: aac
        profile: lc          # lc | he-aacv1 | he-aacv2
        bitrate: 128
        samplerate: 44100
        channels: 2
    archive:
      enabled: false
      path: "./recordings/"
      filename_pattern: "{station_id}_{date}_{time}.wav"
      max_size_mb: 2048

  - id: 2
    name: "My Jazz Stream"
    enabled: false
    icy:
      name: "Smooth Jazz 24/7"
      genre: "Jazz"
      url: "https://jazz.example.com"
      pub: false
    server:
      type: shoutcast2
      host: shout2.example.com
      port: 8002
      password: "jazzpass"
      mount: /jazz
    encoders:
      - format: mp3
        bitrate: 192
        samplerate: 44100
        channels: 2
        quality: 2
        vbr: true
        vbr_min: 96
        vbr_max: 320
    archive:
      enabled: true
      path: "D:/archive/jazz/"
      filename_pattern: "jazz_{date}.wav"
      max_size_mb: 4096
```

### 4.3 YAML Library — libyaml (already in vcpkg)

New module: `libmcaster1dspencoder/config_yaml.cpp` + `config_yaml.h`

```c
int mcaster1_load_yaml_config(const char *path, mcaster1_config_t *cfg);
int mcaster1_get_station_count(const mcaster1_config_t *cfg);
int mcaster1_get_station(const mcaster1_config_t *cfg, int idx, mcaster1_station_t *st);
int mcaster1_save_yaml_config(const char *path, const mcaster1_config_t *cfg);
```

Legacy `key=value` reader retained as fallback. Format auto-detected by file extension.

### 4.4 Multi-Station Architecture

`MAX_ENCODERS` increased from 10 → 32. Each YAML station encoder entry maps to one
`mcaster1Globals` slot. Audio input is fanned out to all active encoder threads from
a single capture/input callback using the existing `pthreadVSE` threading model.

### 4.5 Phase 4 Deliverables

- [ ] `libyaml` linked from vcpkg (x64-windows, static)
- [ ] `config_yaml.cpp` / `config_yaml.h` module in `libmcaster1dspencoder`
- [ ] `mcaster1dspencoder.yaml` sample config with 2 stations
- [ ] YAML loader replaces legacy cfg loader (legacy kept as fallback)
- [ ] `MAX_ENCODERS` increased 10 → 32
- [ ] Multi-station verified: 2 stations, 2+ encoders each, simultaneous
- [ ] Per-station ICY metadata verified on Icecast and Shoutcast
- [ ] Save-to-WAV archive functional per station
- [ ] Station list UI in `Mcaster1DSPEncoder.exe`
- [ ] Station selector in all plugin config dialogs

---

## Dependency Matrix

| Dependency | Purpose | License | Distribute | Phase | vcpkg |
|------------|---------|---------|-----------|-------|-------|
| `libmp3lame.dll` | MP3 encoding | LGPL v2 | Yes (patents expired 2017) | 2 | `mp3lame:x64` |
| `vorbis.dll` / `vorbisenc.dll` | Ogg Vorbis | BSD | Yes | existing | `libvorbis:x64` |
| `ogg.dll` | Ogg container | BSD | Yes | existing | `libogg:x64` |
| `libFLAC.dll` | FLAC encoding | BSD | Yes | existing | `libflac:x64` |
| `fdk-aac.dll` | AAC-LC / AAC+ / AAC++ encoding | BSD-2 (patent license for HE-AAC) | Yes | 3 | `fdk-aac:x64[he-aac]` |
| `libcurl.dll` | HTTP / streaming | MIT/curl | Yes | existing | `curl:x64` |
| `pthreadVSE.dll` | Threading | LGPL | Yes | existing | `pthreads:x64` |
| `opus.dll` | Opus core codec | BSD | Yes | 3 | `opus:x64` ✓ |
| `opusenc.dll` | Opus encoding API | BSD | Yes | 3 | `libopusenc:x64` ✓ |
| PortAudio | Device capture (WASAPI/DS/MME/WDM-KS) | MIT | **Baked in (static link)** | 3 | `portaudio:x86/x64-windows-static` |
| `libyaml` | YAML config | MIT | Yes (static) | 4 | `libyaml:x64` ✓ |
| `bass.dll` | Audio capture | Proprietary | Open-source exception | remove Phase 3 | — |
| `basswma.dll` | WMA encoding | Proprietary | **REMOVE** | 3 | — |
| `libxml2.dll` | XML / YP | MIT | Yes | existing | `libxml2:x64` |
| `iconv.dll` | Encoding conv. | LGPL | Yes | existing | `libiconv:x64` |

---

## Git Repository Setup

```bash
git init
git remote add origin https://github.com/davestj/Mcaster1DSPEncoder.git
git add .
git commit -m "feat: Phase 2 complete — rebranded to Mcaster1DSPEncoder, x64 migration, native LAME"
git tag v0.2.0
git push -u origin main
```

**Release tags:**
- `v0.1.0` — Phase 1 complete (VS2022 build fixes, original AltaCast naming)
- `v0.2.0` — Phase 2 complete (rebrand, x64, native LAME)
- `v0.3.0` — Phase 3 complete (Opus, WMA removed, PortAudio)
- `v0.4.0` — Phase 4 complete (YAML multi-station config)

---

## Phase Summary

| Phase | Name | Status | Key Outputs |
|-------|------|--------|-------------|
| 1 | VS2022 Build Fix | **DONE** | All 4 targets compile and run |
| 2 | Rebrand & Refactor | **DONE** | Full rename, ResizableLib UI, Segoe UI styling, native LAME, all 4 targets clean |
| 3 | Audio Modernization | **IN PROGRESS** | Dual MP3 (LAME + ACM), Opus, AAC/AAC+/AAC++, WMA removed, PortAudio — pending E2E verification |
| 4 | YAML Multi-Station | PLANNED | YAML config, multi-station, per-station encoders, archive |
| 5 | ICY 2.1 Enhanced Metadata | PLANNED | JSON metadata blocks, album art, track IDs, DNAS history API linkage |
| 6 | Mcaster1DNAS Integration | PLANNED | Auto-discovery, shared config, live health feedback, song history push |
| 7 | Mcaster1Castit | PLANNED | Castit VC6 → VS2022 upgrade — libxml2/libcurl/libmariadb via vcpkg |
| 8 | Analytics & Engagement | PLANNED | Listener metrics, platform engagement via ICY 2.1 signals |

See [ROADMAP.md](ROADMAP.md) for full detail on Phases 5–8.
