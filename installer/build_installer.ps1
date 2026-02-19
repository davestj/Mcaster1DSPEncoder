# ============================================================
# Mcaster1DSPEncoder — Installer Build Script
# Stages all files and runs NSIS to produce the .exe installer
# ============================================================
#
# Usage:
#   powershell.exe -NoProfile -File installer\build_installer.ps1
#   powershell.exe -NoProfile -File installer\build_installer.ps1 -Config Release
#
# Prerequisites:
#   - NSIS installed (https://nsis.sourceforge.io/)
#   - Successful build via build_winqt.ps1
#   - docs/index.html in project root
#
# Output:
#   installer\Mcaster1DSPEncoderQT_Setup_1.3.0.exe
# ============================================================

param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"

$ProjectRoot   = Split-Path -Parent $PSScriptRoot
$InstallerDir  = $PSScriptRoot
$BuildDir      = Join-Path $ProjectRoot "win-qt\build\win-qt\$Config"
$StagingDir    = Join-Path $InstallerDir "staging"
$VcpkgBin      = "C:\vcpkg\installed\x64-windows\bin"
$SigningKeys   = "C:\Users\dstjohn\dev\00_mcaster1.com\SIGNING-KEYS"
$Version       = "1.3.0"

# Locate NSIS
$NsisPaths = @(
    "C:\Program Files (x86)\NSIS\makensis.exe",
    "C:\Program Files\NSIS\makensis.exe",
    "$env:LOCALAPPDATA\NSIS\makensis.exe"
)
$MakeNsis = $null
foreach ($p in $NsisPaths) {
    if (Test-Path $p) { $MakeNsis = $p; break }
}
if (-not $MakeNsis) {
    Write-Host "[ERROR] NSIS not found. Install from https://nsis.sourceforge.io/" -ForegroundColor Red
    Write-Host "        Searched: $($NsisPaths -join ', ')"
    exit 1
}
Write-Host "[NSIS] Found: $MakeNsis" -ForegroundColor Cyan

# Verify build output exists
if (-not (Test-Path "$BuildDir\Mcaster1DSPEncoder_Qt.exe")) {
    Write-Host "[ERROR] Build output not found at $BuildDir" -ForegroundColor Red
    Write-Host "        Run: powershell.exe -NoProfile -File win-qt\build_winqt.ps1" -ForegroundColor Yellow
    exit 1
}
Write-Host "[Stage] Source: $BuildDir" -ForegroundColor Cyan

# ── Clean and create staging directory ────────────────────────
if (Test-Path $StagingDir) { Remove-Item -Recurse -Force $StagingDir }
New-Item -ItemType Directory -Path $StagingDir -Force | Out-Null
Write-Host "[Stage] Staging directory: $StagingDir"

# ── Copy main executable ─────────────────────────────────────
Copy-Item "$BuildDir\Mcaster1DSPEncoder_Qt.exe" $StagingDir

# ── Copy VCam DLL ────────────────────────────────────────────
if (Test-Path "$BuildDir\Mcaster1VirtualCam.dll") {
    Copy-Item "$BuildDir\Mcaster1VirtualCam.dll" $StagingDir
    Write-Host "[Stage] VCam DLL included"
}

# ── Copy all DLLs from build dir ─────────────────────────────
Get-ChildItem "$BuildDir\*.dll" | ForEach-Object {
    Copy-Item $_.FullName $StagingDir -Force
}
# Also pick up case-sensitive .DLL
Get-ChildItem "$BuildDir\*.DLL" | ForEach-Object {
    Copy-Item $_.FullName $StagingDir -Force
}
Write-Host "[Stage] DLLs copied"

# ── Ensure vcpkg DLLs are present ────────────────────────────
$vcpkgDlls = @("FLAC.dll", "fdk-aac.dll", "libmp3lame.dll", "ogg.dll",
                "opus.dll", "opusenc.dll", "portaudio.dll",
                "vorbis.dll", "vorbisenc.dll", "yaml.dll")
foreach ($dll in $vcpkgDlls) {
    if (-not (Test-Path "$StagingDir\$dll")) {
        if (Test-Path "$VcpkgBin\$dll") {
            Copy-Item "$VcpkgBin\$dll" $StagingDir -Force
            Write-Host "[Stage] vcpkg: $dll"
        } else {
            Write-Host "[WARN] Missing vcpkg DLL: $dll" -ForegroundColor Yellow
        }
    }
}

# ── Copy Qt plugin directories ───────────────────────────────
$qtPluginDirs = @("platforms", "styles", "iconengines", "imageformats",
                  "multimedia", "networkinformation", "generic", "tls", "translations")
foreach ($dir in $qtPluginDirs) {
    $src = Join-Path $BuildDir $dir
    if (Test-Path $src) {
        $dst = Join-Path $StagingDir $dir
        Copy-Item $src $dst -Recurse -Force
    }
}
Write-Host "[Stage] Qt plugins copied"

# ── Copy app icon ────────────────────────────────────────────
$iconSrc = Join-Path $ProjectRoot "win-qt\resources\Mcaster1DSPEncoder_Qt.ico"
if (Test-Path $iconSrc) {
    Copy-Item $iconSrc "$StagingDir\app.ico"
} else {
    # Create a placeholder — NSIS requires an icon
    Write-Host "[WARN] App icon not found, using default" -ForegroundColor Yellow
}

# ── Copy documentation ───────────────────────────────────────
$docsDir = Join-Path $StagingDir "docs"
New-Item -ItemType Directory -Path $docsDir -Force | Out-Null

# HTML docs + CSS
$docsSrc = Join-Path $ProjectRoot "docs\index.html"
if (Test-Path $docsSrc) {
    Copy-Item $docsSrc $docsDir
    Write-Host "[Stage] docs/index.html"
}
$cssSrc = Join-Path $ProjectRoot "docs\style.css"
if (Test-Path $cssSrc) {
    Copy-Item $cssSrc $docsDir
    Write-Host "[Stage] docs/style.css"
}

# Screenshots
$screenshotsSrc = Join-Path $ProjectRoot "docs\screenshots"
if (Test-Path $screenshotsSrc) {
    $screenshotsDst = Join-Path $docsDir "screenshots"
    Copy-Item $screenshotsSrc $screenshotsDst -Recurse -Force
    $count = (Get-ChildItem "$screenshotsDst\*.png" -ErrorAction SilentlyContinue).Count
    Write-Host "[Stage] docs/screenshots/ ($count images)"
}

# Project documentation (Markdown files)
$mdFiles = @("README.md", "RELEASENOTES.md", "FEATURES.md", "CHANGELOG.md",
             "ROADMAP.md", "CREDITS.md", "FORKS.md", "LICENSE")
foreach ($md in $mdFiles) {
    $mdSrc = Join-Path $ProjectRoot $md
    if (Test-Path $mdSrc) {
        Copy-Item $mdSrc $docsDir
        Write-Host "[Stage] docs/$md"
    }
}
Write-Host "[Stage] Documentation bundled"

# ── Ensure FFmpeg DLLs from build dir ────────────────────────
$ffmpegDlls = @("avcodec-*.dll", "avformat-*.dll", "avutil-*.dll",
                "swresample-*.dll", "swscale-*.dll")
foreach ($pattern in $ffmpegDlls) {
    Get-ChildItem "$BuildDir\$pattern" -ErrorAction SilentlyContinue | ForEach-Object {
        if (-not (Test-Path "$StagingDir\$($_.Name)")) {
            Copy-Item $_.FullName $StagingDir -Force
            Write-Host "[Stage] FFmpeg: $($_.Name)"
        }
    }
}

# ── Ensure SSL DLLs ─────────────────────────────────────────
$sslDlls = @("libcrypto-3-x64.dll", "libssl-3-x64.dll")
foreach ($dll in $sslDlls) {
    if (-not (Test-Path "$StagingDir\$dll")) {
        $src = "$BuildDir\$dll"
        if (-not (Test-Path $src)) { $src = "$VcpkgBin\$dll" }
        if (Test-Path $src) {
            Copy-Item $src $StagingDir -Force
            Write-Host "[Stage] SSL: $dll"
        }
    }
}

# ── Copy code signing certificate (if present) ──────────────
if (Test-Path "$SigningKeys\Mcaster1CodeSigning.cer") {
    $certsDir = Join-Path $StagingDir "certs"
    New-Item -ItemType Directory -Path $certsDir -Force | Out-Null
    Copy-Item "$SigningKeys\Mcaster1CodeSigning.cer" $certsDir
    Write-Host "[Stage] Code signing certificate included"
}

# ── Create fresh default config files ────────────────────────
$configDir = Join-Path $StagingDir "config"
New-Item -ItemType Directory -Path $configDir -Force | Out-Null

# Global config — zeroed out for new install
@"
# Mcaster1 DSP Encoder — Global Settings
# Generated by installer — edit freely

global:
  audio_device_index: -1
  audio_device_uid: ""
  video_device_index: 0
  window_x: 100
  window_y: 100
  window_width: 1280
  window_height: 800
  window_maximized: false
  theme: 0
  log_level: 4
  log_dir: ""
  dnas_poll_sec: 15
  auto_start: false
  tray_on_close: true
  playlist_dir: ""
  archive_dir: ""
  global_metadata:
    source: "manual"
    lock_metadata: false
    manual_text: ""
    append_string: ""
    meta_url: ""
    meta_file: ""
    update_interval_sec: 5
  ptt_mic_device_name: ""
  ptt_mic_sample_rate: 44100
"@ | Set-Content "$configDir\mcaster1dspencoder_global.yaml" -Encoding UTF8

# Default encoder profile — zeroed out
@"
# Mcaster1 Encoder Profile
# Generated by installer — edit freely

encoder:
  name: "Encoder 1"
  type: "radio"
  slot_id: 1
  codec: "mp3"
  bitrate_kbps: 128
  quality: 5
  sample_rate: 44100
  channels: 2
  encode_mode: "cbr"
  channel_mode: "joint"
  shuffle: false
  repeat_all: true
  auto_start: false
  auto_reconnect: true
  input_type: "device"
  device_index: -1
  per_encoder_device_index: -1
  use_global_metadata: true

per_encoder_metadata:
  source: "manual"
  lock_metadata: false
  manual_text: ""
  append_string: ""
  meta_url: ""
  meta_file: ""
  update_interval_sec: 5

dsp:
  eq_enabled: false
  agc_enabled: false
  crossfade_enabled: true
  crossfade_duration: 3
  eq_preset: "flat"
  sonic_enabled: false
  eq_mode: 0

server:
  server_type: "icecast2"
  host: ""
  port: 8000
  mount: "/stream"
  username: "source"
  password: ""
  station_name: ""
  station_genre: ""
  station_url: ""
  public_listing: false
"@ | Set-Content "$configDir\radio_encoder_00.yaml" -Encoding UTF8

# DSP effect defaults — all disabled/flat
@"
# AGC/Compressor — Default (disabled)
agc:
  enabled: false
  threshold_db: -18.0
  ratio: 4.0
  attack_ms: 10.0
  release_ms: 100.0
  makeup_gain_db: 0.0
  knee_db: 6.0
  gate_threshold_db: -60.0
  limiter_ceiling_db: -1.0
"@ | Set-Content "$configDir\dsp_effect_agc.yaml" -Encoding UTF8

@"
# 10-Band Parametric EQ — Default (flat)
eq10:
  enabled: false
  preset: "flat"
"@ | Set-Content "$configDir\dsp_effect_eq10.yaml" -Encoding UTF8

@"
# 31-Band Graphic EQ — Default (flat)
eq31:
  enabled: false
  preset: "flat"
"@ | Set-Content "$configDir\dsp_effect_eq31.yaml" -Encoding UTF8

@"
# Sonic Enhancer — Default (disabled)
sonic_enhancer:
  enabled: false
  lo_contour: 0.0
  process: 0.0
  output_gain: 0.0
"@ | Set-Content "$configDir\dsp_effect_sonic_enhancer.yaml" -Encoding UTF8

@"
# PTT Duck — Default (disabled)
ptt_duck:
  enabled: false
  threshold_db: -30.0
  duck_amount_db: -18.0
  attack_ms: 50.0
  release_ms: 500.0
"@ | Set-Content "$configDir\dsp_effect_ptt_duck.yaml" -Encoding UTF8

@"
# DBX 286s Voice Processor — Default (disabled)
dbx_voice:
  enabled: false
  compressor_threshold_db: -10.0
  compressor_ratio: 3.0
  de_esser_frequency: 6000
  de_esser_threshold_db: -20.0
  enhancer_detail: 0.0
  enhancer_warmth: 0.0
  gate_threshold_db: -60.0
  output_gain_db: 0.0
"@ | Set-Content "$configDir\dsp_effect_dbx_voice.yaml" -Encoding UTF8

Write-Host "[Stage] Default config files created"

# ── Create LICENSE.txt ───────────────────────────────────────
@"
Mcaster1 DSP Encoder
Copyright (c) 2026 David St. John <davestj@gmail.com>

Licensed under the GNU General Public License v2.0 or later (GPL-2.0-or-later).

This software uses the following open-source libraries:
  - Qt 6 (LGPL v3)
  - PortAudio (MIT)
  - LAME (LGPL v2)
  - libvorbis/libogg (BSD)
  - Opus/libopusenc (BSD)
  - FLAC (BSD)
  - fdk-aac (Fraunhofer FDK AAC, custom license)
  - libyaml (MIT)
  - libvpx (BSD)
  - OpenSSL (Apache 2.0)
  - FFmpeg (LGPL v2.1)

For full license texts, see:
  https://github.com/user/Mcaster1DSPEncoder/blob/master/LICENSE
"@ | Set-Content "$StagingDir\LICENSE.txt" -Encoding UTF8

Write-Host "[Stage] Staging complete" -ForegroundColor Green

# ── Run NSIS ─────────────────────────────────────────────────
Write-Host ""
Write-Host "[NSIS] Compiling installer..." -ForegroundColor Cyan

$nsiScript = Join-Path $InstallerDir "mcaster1_installer.nsi"

& $MakeNsis /DSTAGING_DIR="$StagingDir" /DOUTDIR="$InstallerDir" /NOCD "$nsiScript"

if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] NSIS compilation failed with exit code $LASTEXITCODE" -ForegroundColor Red
    exit $LASTEXITCODE
}

# Verify output
$outputExe = Join-Path $InstallerDir "Mcaster1DSPEncoderQT_Setup_$Version.exe"
if (Test-Path $outputExe) {
    Write-Host ""
    Write-Host "[SUCCESS] Installer built: $outputExe" -ForegroundColor Green
    $size = (Get-Item $outputExe).Length / 1MB
    Write-Host "          Size: $([math]::Round($size, 1)) MB"
} else {
    Write-Host "[ERROR] Installer exe not found at $outputExe" -ForegroundColor Red
    exit 1
}

# ── Sign the installer (if signing keys available) ───────────
$signScript = "$SigningKeys\sign.ps1"
if ((Test-Path $outputExe) -and (Test-Path $signScript)) {
    Write-Host "[Sign] Signing installer..." -ForegroundColor Cyan
    & $signScript $outputExe
    if ($LASTEXITCODE -eq 0) {
        Write-Host "[Sign] Installer signed successfully" -ForegroundColor Green
    } else {
        Write-Host "[Sign] Signing failed (non-fatal)" -ForegroundColor Yellow
    }
}

Write-Host ""
Write-Host "Done. Installer ready for distribution." -ForegroundColor Green
