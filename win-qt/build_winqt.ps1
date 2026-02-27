# ============================================================
# Mcaster1DSPEncoder — Windows Qt 6 Build Script
# Usage:
#   powershell.exe -NoProfile -File win-qt\build_winqt.ps1
#   powershell.exe -NoProfile -File win-qt\build_winqt.ps1 -Config Release
# ============================================================

param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"
$QTDIR = "C:\Qt\6.9.3\msvc2022_64"
$MSBUILD = "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
$VCVARS = "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
$PROJ_APP  = "$PSScriptRoot\Mcaster1DSPEncoder_Qt.vcxproj"
$PROJ_VCAM = "$PSScriptRoot\virtual_camera\Mcaster1VirtualCam.vcxproj"
$EXEDIR = "$PSScriptRoot\build\win-qt\$Config"

Write-Host "=== Mcaster1 DSP Encoder Build ($Config) ===" -ForegroundColor Cyan

# Set up MSVC environment by calling vcvars64.bat and harvesting its env changes
$vcEnv = cmd.exe /c "`"$VCVARS`" >nul 2>&1 && set" 2>&1
foreach ($line in $vcEnv) {
    if ($line -match "^([^=]+)=(.+)$") {
        [System.Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], "Process")
    }
}
$env:QTDIR = $QTDIR
$env:PATH  = "$QTDIR\bin;" + $env:PATH

Write-Host "[Build] Building main encoder (MSBuild $Config)..."
& $MSBUILD $PROJ_APP /p:Configuration=$Config /p:Platform=x64 /p:QTDIR=$QTDIR /m /nologo /v:m
if ($LASTEXITCODE -ne 0) { Write-Host "[Build] Main app FAILED with error $LASTEXITCODE"; exit $LASTEXITCODE }
Write-Host "[Build] Main app SUCCESS"

Write-Host "[Build] Building virtual camera DLL (MSBuild $Config)..."
& $MSBUILD $PROJ_VCAM /p:Configuration=$Config /p:Platform=x64 /m /nologo /v:m
if ($LASTEXITCODE -ne 0) { Write-Host "[Build] VCam DLL FAILED with error $LASTEXITCODE"; exit $LASTEXITCODE }
Write-Host "[Build] VCam DLL SUCCESS"

# Deploy Qt DLLs — detect debug vs release by checking for the appropriate core DLL
$qtCheckDll = if ($Config -eq "Release") { "Qt6Core.dll" } else { "Qt6Cored.dll" }
$deployFlag = if ($Config -eq "Release") { "--release" } else { "--debug" }

if (-not (Test-Path "$EXEDIR\$qtCheckDll")) {
    Write-Host "[Deploy] Qt DLLs missing -- running windeployqt ($Config)..."
    & "$QTDIR\bin\windeployqt.exe" $deployFlag --no-translations "$EXEDIR\Mcaster1DSPEncoder_Qt.exe"
    if ($LASTEXITCODE -ne 0) { Write-Host "[Deploy] windeployqt FAILED with error $LASTEXITCODE"; exit $LASTEXITCODE }
    Write-Host "[Deploy] SUCCESS"
} else {
    Write-Host "[Deploy] Qt DLLs already present -- skipping windeployqt"
}

# Deploy vcpkg runtime DLLs if missing
$vcpkgBin = "C:\vcpkg\installed\x64-windows\bin"
$vcpkgDlls = @(
    "FLAC.dll", "fdk-aac.dll", "libmp3lame.dll", "ogg.dll",
    "opus.dll", "opusenc.dll", "portaudio.dll",
    "vorbis.dll", "vorbisenc.dll", "yaml.dll"
)
$missing = $false
foreach ($dll in $vcpkgDlls) {
    if (-not (Test-Path "$EXEDIR\$dll")) { $missing = $true; break }
}
if ($missing) {
    Write-Host "[Deploy] Copying vcpkg runtime DLLs..."
    foreach ($dll in $vcpkgDlls) {
        Copy-Item "$vcpkgBin\$dll" "$EXEDIR\" -Force -ErrorAction SilentlyContinue
    }
    Write-Host "[Deploy] vcpkg DLLs copied"
} else {
    Write-Host "[Deploy] vcpkg DLLs already present -- skipping"
}

# Copy docs to build output (for Help browser)
$docsSrc = Join-Path (Split-Path $PSScriptRoot) "docs"
$docsDst = Join-Path $EXEDIR "docs"
if ((Test-Path $docsSrc) -and (-not (Test-Path "$docsDst\index.html"))) {
    Write-Host "[Deploy] Copying docs/ to build output..."
    Copy-Item $docsSrc $docsDst -Recurse -Force
    Write-Host "[Deploy] docs/ copied"
}

# Sign the exe if signing keys are available
$signScript = "C:\Users\dstjohn\dev\00_mcaster1.com\SIGNING-KEYS\sign.ps1"
$outputExe  = "$EXEDIR\Mcaster1DSPEncoder_Qt.exe"
if ((Test-Path $signScript) -and (Test-Path $outputExe)) {
    Write-Host "[Sign] Signing exe with Mcaster1 code signing cert..."
    & $signScript $outputExe
    if ($LASTEXITCODE -eq 0) {
        Write-Host "[Sign] Signed successfully"
    } else {
        Write-Host "[Sign] Signing skipped or failed (non-fatal)" -ForegroundColor Yellow
    }
}

Write-Host ""
Write-Host "=== Build Complete ($Config) ===" -ForegroundColor Green
Write-Host "    Output: $EXEDIR\Mcaster1DSPEncoder_Qt.exe"
