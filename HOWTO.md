# Mcaster1 DSP Encoder — Build & Deploy How-To

---

## Prerequisites

### PowerShell Execution Policy (one-time setup)

Windows blocks unsigned scripts by default. Run this **once** in a normal PowerShell window:

```powershell
Set-ExecutionPolicy RemoteSigned -Scope CurrentUser
```

Answer `Y` when prompted. Applies to your user account only, persists across sessions.

> If you prefer not to change the policy, bypass it per-script:
> `powershell -ExecutionPolicy Bypass -File .\build-main.ps1`

---

### Tools Required

| Tool | Version | Notes |
|------|---------|-------|
| Visual Studio 2022 | 17.x | Professional or Community; **Desktop development with C++** workload required |
| MSBuild | 17.x | Included with VS2022 — no separate install |
| vcpkg | any | Install at `C:\vcpkg`; use the `x86-windows` triplet |
| NSIS | 3.x | Nullsoft Scriptable Install System — only needed for building installers |
| PowerShell | 5.1+ | Built into Windows 10/11 |

### vcpkg Packages (x86-windows)

```powershell
vcpkg install curl:x86-windows libmp3lame:x86-windows opus:x86-windows opusenc:x86-windows fdk-aac:x86-windows
```

> **Note:** `ogg`, `vorbis`, `vorbisenc`, `libFLAC`, `pthreadVSE`, `iconv`, `libcurl`, and `bass`
> ship pre-built in `external\lib\`. Do not reinstall them via vcpkg.

---

### Project Root

```
C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\
```

All PowerShell scripts are in the project root. Run them from there.

---

## Step 1 — Find MSBuild (one-time check)

Confirm MSBuild is where the build scripts expect it:

```powershell
.\find-msbuild.ps1
```

Expected output:
```
Found: C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe
```

---

## Step 2 — Build the Standalone App

**Script:** `build-main.ps1`
**Output:** `src\Release\Mcaster1DSPEncoder.exe`

### Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `-Clean` | switch | — | Run the Clean target |
| `-Build` | switch | — | Run the Build target |
| `-Config` | string | `Release` | `Release` or `Debug` |

> **Default behaviour:** when neither `-Clean` nor `-Build` is passed, both run (Clean + Build).

### Examples

```powershell
# Clean + Build (default)
.\build-main.ps1

# Clean only
.\build-main.ps1 -Clean

# Incremental build (no clean)
.\build-main.ps1 -Build

# Debug build
.\build-main.ps1 -Config Debug

# Clean then Debug build
.\build-main.ps1 -Clean -Build -Config Debug
```

### Manual MSBuild equivalent

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" `
    src\Mcaster1DSPEncoder.sln `
    /p:Configuration=Release /p:Platform=Win32 /t:Clean,Build /m /v:normal
```

---

## Step 3 — Build the Plugins

**Script:** `build-plugins.ps1`

| Plugin | Solution | Output DLL | Targets |
|--------|----------|------------|---------|
| Winamp DSP | `src\mcaster1_winamp.sln` | `src\Release\dsp_mcaster1.dll` | Winamp, WACUP, RadioBoss |
| Foobar2000 | `src\mcaster1_foobar.sln` | `src\Release\foo_mcaster1.dll` | Foobar2000 |
| RadioDJ v2 | `src\mcaster1_radiodj.sln` | `src\Release\dsp_radiodj.dll` | RadioDJ v2 |

> **RadioBoss** uses the Winamp DSP plugin interface — deploy `dsp_mcaster1.dll`. No separate build needed.

### Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `-Clean` | switch | — | Run the Clean target |
| `-Build` | switch | — | Run the Build target |
| `-Config` | string | `Release` | `Release` or `Debug` |
| `-Plugin` | string | `all` | `all`, `winamp`, `foobar`, or `radiodj` |

> **Default behaviour:** when neither `-Clean` nor `-Build` is passed, both run (Clean + Build).

### Examples

```powershell
# Clean + Build all plugins (default)
.\build-plugins.ps1

# Build all plugins, no clean (incremental)
.\build-plugins.ps1 -Build

# Clean only, all plugins
.\build-plugins.ps1 -Clean

# Build the Winamp plugin only
.\build-plugins.ps1 -Plugin winamp

# Incremental build of Foobar2000 plugin only
.\build-plugins.ps1 -Plugin foobar -Build

# Full clean + Debug build of RadioDJ plugin only
.\build-plugins.ps1 -Plugin radiodj -Clean -Build -Config Debug

# Debug build of all plugins
.\build-plugins.ps1 -Config Debug
```

---

## Step 4 — Copy Runtime DLLs to Release\

Stage codec DLLs alongside the output binaries before deploying or packaging:

```powershell
.\copy-dlls.ps1
```

| DLL | Source | Purpose |
|-----|--------|---------|
| `ogg.dll` | `external\lib\` | Ogg container format |
| `vorbis.dll` | `external\lib\` | Vorbis decoder |
| `vorbisenc.dll` | `external\lib\` | Vorbis encoder |
| `libFLAC.dll` | `external\lib\` | FLAC encoder/decoder |
| `pthreadVSE.dll` | `external\lib\` | POSIX threads for Windows |
| `libcurl.dll` | `external\lib\` | HTTP/HTTPS streaming transport |
| `iconv.dll` | `external\lib\` | Character encoding conversion |
| `bass.dll` | `external\lib\` | BASS audio library |
| `libmp3lame.dll` | `vcpkg x86-windows\bin\` | MP3 encoder (LAME) |
| `fdk-aac.dll` | `vcpkg x86-windows\bin\` | AAC/HE-AAC encoder (FDK) |
| `opus.dll` | `vcpkg x86-windows\bin\` | Opus decoder |
| `opusenc.dll` | `vcpkg x86-windows\bin\` | Opus encoder |

---

## Step 5 — Deploy to Host Applications

> **Run deploy scripts as Administrator.**
> Right-click PowerShell → Run as Administrator, then execute the script.

### Winamp

Default path: `C:\Program Files (x86)\Winamp`

```powershell
.\deploy-winamp.ps1
```

- `dsp_mcaster1.dll` → `Plugins\`
- All runtime DLLs → Winamp root

**Activate:** Options → Preferences → Plugins → DSP/Effect → **Mcaster1 DSP Encoder**

---

### WACUP

Default path: `C:\Program Files\WACUP`

```powershell
.\deploy-wacup.ps1
```

- `dsp_mcaster1.dll` → `Plugins\`
- All runtime DLLs → WACUP root

**Activate:** Preferences → Plug-ins → DSP/Effect → **Mcaster1 DSP Encoder**

---

### Foobar2000

Default path: `C:\Program Files\foobar2000`

```powershell
.\deploy-foobar.ps1
```

- `foo_mcaster1.dll` → `components\`
- All runtime DLLs → Foobar2000 root

**Activate:** File → Preferences → Playback → DSP Manager → add **Mcaster1 DSP Encoder**

---

### RadioDJ v2

Default path: `C:\RadioDJv2`

```powershell
.\deploy-radiodj.ps1
```

- `dsp_radiodj.dll` → RadioDJ root (same folder as `RadioDJ.exe`)
- All runtime DLLs → RadioDJ root

**Activate:** Settings → Sound → DSP Plugin → browse to `dsp_radiodj.dll`

---

### RadioBoss

Default path: `C:\Program Files (x86)\RadioBoss`

```powershell
.\deploy-radioboss.ps1
```

- `dsp_mcaster1.dll` → RadioBoss `Plugins\` (Winamp DSP interface)
- All runtime DLLs → RadioBoss root

**Activate:** Options → DSP/Effect Plugins → Add → select `dsp_mcaster1.dll`

---

## Step 6 — Check Dependencies (diagnostic)

Verify all required DLLs are resolvable for the standalone exe:

```powershell
.\check-deps.ps1
```

Uses `dumpbin /DEPENDENTS` from the VS2022 toolchain. Any DLL listed that is not present in the application folder will cause a startup crash.

---

## Step 7 — Build NSIS Installers

Complete Steps 2–4 first. NSIS scripts are in `src\`. Run from a **regular** command prompt (not elevated).

> **Note:** The `.nsi` scripts are being migrated from legacy Edcast/AltaCast naming to Mcaster1DSPEncoder as part of Phase 2. See `ROADMAP.md` for status.

| Installer | Command | Output |
|-----------|---------|--------|
| Standalone | `"C:\Program Files (x86)\NSIS\makensis.exe" src\mcaster1_standalone.nsi` | `src\Mcaster1DSPEncoder-Setup.exe` |
| Winamp | `"C:\Program Files (x86)\NSIS\makensis.exe" src\mcaster1_winamp.nsi` | `src\Mcaster1DSPEncoder-Winamp-Setup.exe` |
| Foobar2000 | `"C:\Program Files (x86)\NSIS\makensis.exe" src\mcaster1_foobar.nsi` | `src\Mcaster1DSPEncoder-Foobar2000-Setup.exe` |
| RadioDJ | `"C:\Program Files (x86)\NSIS\makensis.exe" src\mcaster1_radiodj.nsi` | `src\Mcaster1DSPEncoder-RadioDJ-Setup.exe` |

---

## Quick Reference — All Scripts

| Script | Purpose | Admin? |
|--------|---------|--------|
| `find-msbuild.ps1` | Locate MSBuild.exe | No |
| `build-main.ps1 [-Clean] [-Build] [-Config Release\|Debug]` | Build standalone exe | No |
| `build-plugins.ps1 [-Clean] [-Build] [-Config Release\|Debug] [-Plugin all\|winamp\|foobar\|radiodj]` | Build plugin DLLs | No |
| `copy-dlls.ps1` | Stage codec DLLs into `src\Release\` | No |
| `deploy-winamp.ps1` | Deploy to Winamp | **Yes** |
| `deploy-wacup.ps1` | Deploy to WACUP | **Yes** |
| `deploy-foobar.ps1` | Deploy to Foobar2000 | **Yes** |
| `deploy-radiodj.ps1` | Deploy to RadioDJ v2 | **Yes** |
| `deploy-radioboss.ps1` | Deploy to RadioBoss | **Yes** |
| `check-deps.ps1` | Verify DLL dependencies via dumpbin | No |

---

## Troubleshooting

**Script blocked — "running scripts is disabled"**
Run `Set-ExecutionPolicy RemoteSigned -Scope CurrentUser` once in PowerShell, then retry.

**MSBuild not found**
Run `.\find-msbuild.ps1`. If empty, open the VS Installer and confirm the **Desktop development with C++** workload is installed.

**Build sits silently with no output**
This was a bug in the old script — scripts now use `/v:normal` and stream output directly. Pull the latest version.

**Missing DLL at runtime (startup crash)**
Run `.\check-deps.ps1` and compare against the DLL table in Step 4. Copy any missing DLL from `external\lib\` or `C:\vcpkg\installed\x86-windows\bin\` into the application folder.

**Plugin not visible in Winamp / WACUP**
`dsp_mcaster1.dll` must be in the `Plugins\` subfolder. All runtime DLLs go in the **root** (same level as `winamp.exe`). Restart Winamp after copying.

**Plugin not visible in Foobar2000**
`foo_mcaster1.dll` must be in `components\`. Foobar2000 scans components only on startup — restart it after deploying.

**NSIS: file not found**
Run `.\copy-dlls.ps1` first so all DLLs exist in `src\Release\` before invoking `makensis.exe`.

**Deploy script: Access Denied**
Deploy scripts write into Program Files — must be run as Administrator.
