# deploy-winamp.ps1 — Installs dsp_mcaster1.dll and runtime DLLs into Winamp
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
$winamp      = 'C:\Program Files (x86)\Winamp'
$plugins     = "$winamp\Plugins"
$release     = "$root\Winamp_Plugin\$Config"
$vcpkgBin    = 'C:\vcpkg\installed\x86-windows\bin'
$externalLib = "$root\external\lib"

Write-Host "=== Deploying to Winamp ($Config) ===" -ForegroundColor Cyan
Write-Host "Source : $release"
Write-Host "Target : $winamp"
Write-Host ""

if (-not (Test-Path $plugins)) {
    Write-Host "ERROR: Winamp Plugins folder not found: $plugins" -ForegroundColor Red
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
Write-Host "Copying runtime DLLs to Winamp root..." -ForegroundColor Cyan
foreach ($dll in $runtimeDlls) {
    $name = Split-Path $dll -Leaf
    if (Test-Path $dll) {
        Copy-Item $dll "$winamp\$name" -Force
        Write-Host "  OK  $name" -ForegroundColor Green
    } else {
        Write-Host "  MISSING  $dll" -ForegroundColor Red
    }
}

Write-Host ""
Write-Host "=== Deploy complete ===" -ForegroundColor Green
Write-Host "Start Winamp, then: Options > Preferences > Plugins > DSP/Effect" -ForegroundColor Yellow
Write-Host "Select 'Mcaster1 DSP Encoder'" -ForegroundColor Yellow
