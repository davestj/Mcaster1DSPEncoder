# deploy-wacup.ps1 — Installs dsp_mcaster1.dll and runtime DLLs into WACUP
# Self-elevates via UAC if not already running as Administrator
param(
    [ValidateSet("Release","Debug")]
    [string]$Config = "Release"
)

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "Not running as Administrator -- relaunching elevated (UAC prompt will appear)..." -ForegroundColor Yellow
    Start-Process powershell -Verb RunAs -ArgumentList "-ExecutionPolicy Bypass -File `"$PSCommandPath`" -Config $Config"
    exit
}

$root        = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder'
$wacup       = 'C:\Program Files\WACUP'
$plugins     = "$wacup\Plugins"
$release     = "$root\Winamp_Plugin\$Config"
$vcpkgBin    = 'C:\vcpkg\installed\x86-windows\bin'
$externalLib = "$root\external\lib"

Write-Host "=== Deploying to WACUP ($Config) ===" -ForegroundColor Cyan
Write-Host "Source : $release"
Write-Host "Target : $wacup"
Write-Host ""

if (-not (Test-Path $plugins)) {
    Write-Host "ERROR: WACUP Plugins folder not found: $plugins" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path "$release\dsp_mcaster1.dll")) {
    Write-Host "ERROR: dsp_mcaster1.dll not found in $release" -ForegroundColor Red
    Write-Host "Build the project first: .\build-plugins.ps1 -Plugin winamp -Config $Config" -ForegroundColor Yellow
    exit 1
}

Write-Host "Copying DSP plugin to Plugins\..." -ForegroundColor Cyan
Copy-Item "$release\dsp_mcaster1.dll" "$plugins\dsp_mcaster1.dll" -Force
Write-Host "  OK  dsp_mcaster1.dll -> Plugins\" -ForegroundColor Green

$runtimeDlls = @(
    "$vcpkgBin\fdk-aac.dll",
    "$vcpkgBin\libmp3lame.dll",
    "$vcpkgBin\opus.dll",
    "$vcpkgBin\opusenc.dll",
    "$externalLib\ogg.dll",
    "$externalLib\vorbis.dll",
    "$externalLib\vorbisenc.dll",
    "$externalLib\libFLAC.dll",
    "$externalLib\pthreadVSE.dll",
    "$externalLib\libcurl.dll",
    "$externalLib\iconv.dll"
)

Write-Host ""
Write-Host "Copying runtime DLLs to WACUP root..." -ForegroundColor Cyan
foreach ($dll in $runtimeDlls) {
    $name = Split-Path $dll -Leaf
    if (Test-Path $dll) {
        Copy-Item $dll "$wacup\$name" -Force
        Write-Host "  OK  $name" -ForegroundColor Green
    } else {
        Write-Host "  MISSING  $dll" -ForegroundColor Red
    }
}

Write-Host ""
Write-Host "=== Deploy complete ===" -ForegroundColor Green
Write-Host "In WACUP: Preferences > Plug-ins > DSP/Effect > select Mcaster1 DSP Encoder" -ForegroundColor Yellow
