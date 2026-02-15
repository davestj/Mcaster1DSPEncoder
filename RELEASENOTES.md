# Release Notes — Mcaster1DSPEncoder Qt v1.3.0

**Release Date:** March 2026
**Branch:** `winqt-dev`
**Platform:** Windows 10+ (x64)
**Installer:** `Mcaster1DSPEncoderQT_Setup_1.3.0.exe`
**Install Path:** `C:\Users\USERNAME\Mcaster1\Mcaster1DSPEncoder\`

---

## Highlights

This release completes the video pipeline, implements all Windows platform subsystems, and
delivers broadcast-grade video transitions with professional quality blending.

### Broadcast Video Transitions (12 Types)

The transition engine has been completely rewritten with professional broadcast techniques:

- **sRGB gamma-correct blending** — eliminates the grainy midpoints caused by naive linear
  interpolation in sRGB space. Uses a pre-computed 256-entry lookup table for fast linearization.
- **24px feathered wipe edges** — smooth gradient zone instead of harsh 1-pixel boundaries.
- **Cubic Hermite easing** — natural acceleration/deceleration on all timed transitions.
- **New transitions**: Dip to White, Wipe Up/Down, Push Left/Right, Iris Circle, Dissolve.

### Windows Platform Subsystems (5 Implementations)

All scaffolded Windows API stubs are now fully implemented:

| Subsystem | Windows API | Purpose |
|-----------|-------------|---------|
| WASAPI Loopback | IAudioClient + IAudioCaptureClient | Capture system audio output |
| DXGI Screen Capture | IDXGIOutputDuplication | GPU-accelerated desktop capture |
| MF H.264 Encoder | IMFTransform (MFT) | Hardware-accelerated video encoding |
| MF Video File Decoder | IMFSourceReader | Decode MP4/MKV/AVI/WMV/MOV files |
| Platform Integration | ITaskbarList3 + Shell_NotifyIcon | Taskbar badge + toast notifications |

### NSIS Installer

First official installer for the Qt build. User-space install (no admin required), LZMA solid
compression, config preservation on upgrade, Start Menu + Desktop shortcuts.

---

## What's New in v1.3.0

### Video Pipeline
- Camera capture via Media Foundation + DirectShow enumeration
- DXGI Desktop Duplication screen capture (GPU-accelerated, all monitors)
- Media Foundation H.264 encoder with NVENC/QSV/AMF hardware acceleration (software fallback)
- Media Foundation video file decoder (MP4, MKV, AVI, WMV, MOV)
- VP8/VP9 encoding via libvpx
- Virtual Camera DirectShow DLL (shared memory BGRA output)
- Live Video Studio: 3-source preview switcher + program monitor
- Video Stream Monitor: AIR (decode live stream) + CUE (raw preview)
- ON-AIR flashing indicator with tray notification

### Video Transitions (TransitionEngine Rewrite)
- 12 transition types (was 5): Cut, Crossfade, Fade to Black, Dip to White, Wipe Left/Right/Up/Down, Push Left/Right, Iris Circle, Dissolve
- sRGB gamma-correct crossfade blending via 256-entry LUT
- 24px feathered wipe edges (eliminates harsh lines)
- Cubic Hermite smoothstep easing (3t²−2t³)
- FNV-1a hash dissolve pattern
- Duration slider extended to 0.2s–5.0s

### Audio
- WASAPI loopback system audio capture (no virtual audio cables required)
- PTT mic sample rate conversion (linear interpolation when rates differ)

### Platform Integration
- ITaskbarList3 taskbar overlay badge (red circle + encoder count)
- Shell_NotifyIconW balloon notifications
- COM initialization + DPI awareness
- Renamed `macos_init()` → `windows_init()`

### Streaming
- DNAS slot poller with admin credentials (separate from source creds)
- SSL auto-detection from port (443, 8443, 9443, 8243)
- Preview Audio Studio with ICY protocol parser
- ICY stream reader for live stream monitoring

### Configuration & Persistence
- Per-encoder metadata always saved (survives global/per-encoder toggle)
- Save-on-exit via both `aboutToQuit` and `closeEvent` (covers all exit paths)
- Live Video Studio YAML persistence (codec, transition, targets)
- Event logging system (DEBUG/INFO/WARN/ERROR/CONNECT/AUTH/ICY_META)

### Installer & Distribution
- NSIS installer: `Mcaster1DSPEncoderQT_Setup_1.3.0.exe`
- User-space install: `C:\Users\USERNAME\Mcaster1\Mcaster1DSPEncoder\`
- All Qt DLLs, vcpkg codec DLLs, Qt plugins bundled
- Default config files (zeroed out for fresh install)
- Config preservation on upgrade (existing YAMLs not overwritten)
- Start Menu + Desktop shortcuts
- Add/Remove Programs registration
- Code signing integration (signtool + PFX)

### Build System
- Added system libraries: d3d11.lib, dxgi.lib, mfplat.lib, mfreadwrite.lib, mfuuid.lib, mf.lib
- PostBuildEvent: cert copy + exe signing (Release config)
- `build_winqt.ps1` signing step (non-fatal if keys missing)
- Updated `.gitignore`: ASIO SDK, .claude/, .bat scripts, installer artifacts, signing keys

---

## Portable Application

Mcaster1DSPEncoder Qt is a **fully portable application**. All configuration files are stored
next to the executable — nothing is written to AppData, LOCALAPPDATA, or the Windows registry
(except Add/Remove Programs entries created by the installer).

**Config files saved next to exe:**
- `mcaster1dspencoder_global.yaml` — global settings (window, theme, devices)
- `radio_encoder_00.yaml` (etc.) — per-encoder profiles
- `dsp_effect_agc.yaml` — AGC/compressor settings
- `dsp_effect_eq10.yaml` — 10-band EQ settings
- `dsp_effect_eq31.yaml` — 31-band EQ settings
- `dsp_effect_sonic_enhancer.yaml` — Sonic Enhancer settings
- `dsp_effect_ptt_duck.yaml` — PTT ducking settings
- `dsp_effect_dbx_voice.yaml` — DBX Voice processor settings
- `live_video_studio.yaml` — Live Video Studio settings

You can copy the entire install folder to a USB drive and run from there.

---

## Dependencies Bundled

All runtime dependencies are included in the installer:

### Qt 6.9.3 Framework
- Qt6Core, Qt6Gui, Qt6Widgets, Qt6Multimedia, Qt6Network, Qt6Svg, Qt6Pdf
- Platform plugin: `platforms/qwindows.dll`
- Style plugin: `styles/qwindowsvistastyle.dll`
- Image formats: `imageformats/` (JPEG, PNG, SVG, ICO, etc.)
- Multimedia: `multimedia/` (Windows Media Foundation backend)
- TLS: `tls/` (OpenSSL backend for HTTPS)
- Network: `networkinformation/` (Windows network info)

### Audio Codec Libraries (vcpkg)
- `libmp3lame.dll` — LAME MP3 encoder
- `vorbis.dll` + `vorbisenc.dll` + `ogg.dll` — Ogg Vorbis encoder
- `opus.dll` + `opusenc.dll` — Opus encoder
- `FLAC.dll` — FLAC lossless encoder
- `fdk-aac.dll` — Fraunhofer AAC encoder (LC/HE/HEv2/ELD)
- `portaudio.dll` — PortAudio audio I/O
- `yaml.dll` — libyaml configuration parser

### Media & Graphics
- `D3Dcompiler_47.dll` — Direct3D shader compiler
- `opengl32sw.dll` — Software OpenGL fallback
- `avcodec-61.dll` + `avformat-61.dll` + `avutil-59.dll` — FFmpeg (file decode)
- `swresample-5.dll` + `swscale-8.dll` — FFmpeg (resample/scale)

### Security
- `libcrypto-3-x64.dll` + `libssl-3-x64.dll` — OpenSSL 3.x (TLS for HTTPS streaming)

### Virtual Camera
- `Mcaster1VirtualCam.dll` — DirectShow virtual camera filter (optional registration)

---

## System Requirements

- **OS:** Windows 10 (64-bit) or later
- **RAM:** 4 GB minimum, 8 GB recommended
- **Disk:** ~200 MB for application + dependencies
- **GPU:** Any DirectX 11 capable GPU (for DXGI screen capture and MF H.264 encoding)
- **Audio:** Any Windows audio device (WASAPI)
- **Network:** Internet connection for streaming (Icecast2/Shoutcast/DNAS/RTMP)

---

## Known Issues

- VCam DLL link may fail if `Mcaster1VirtualCam.dll` is registered and locked by the system.
  Unregister (`regsvr32 /u`) before rebuilding.
- `vpx.lib` triggers LTCG and MSVCRT linker warnings (harmless, pre-existing from vcpkg static link).
- Hardware H.264 encoders may require NV12 input format; RGB32 fallback to software encoder.
