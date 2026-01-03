# deploy-radioboss.ps1 — Installs dsp_mcaster1.dll (Winamp DSP) and runtime DLLs into RadioBoss
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
$radioboss   = 'C:\Program Files (x86)\RadioBoss'
$release     = "$root\Winamp_Plugin\$Config"
$vcpkgBin    = 'C:\vcpkg\installed\x86-windows\bin'
$externalLib = "$root\external\lib"

Write-Host "=== Deploying to RadioBoss ($Config) ===" -ForegroundColor Cyan
Write-Host "Source : $release"
Write-Host "Target : $radioboss"
Write-Host ""

if (-not (Test-Path $radioboss)) {
    Write-Host "ERROR: RadioBoss folder not found: $radioboss" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path "$release\dsp_mcaster1.dll")) {
    Write-Host "ERROR: dsp_mcaster1.dll not found in $release" -ForegroundColor Red
    Write-Host "Build the project first: .\build-plugins.ps1 -Plugin winamp -Config $Config" -ForegroundColor Yellow
    exit 1
}

# RadioBoss loads Winamp DSP plugins from its root directory
Write-Host "Copying DSP plugin to RadioBoss root..." -ForegroundColor Cyan
Copy-Item "$release\dsp_mcaster1.dll" "$radioboss\dsp_mcaster1.dll" -Force
Write-Host "  OK  dsp_mcaster1.dll -> RadioBoss root" -ForegroundColor Green

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
Write-Host "Copying runtime DLLs to RadioBoss root..." -ForegroundColor Cyan
foreach ($dll in $runtimeDlls) {
    $name = Split-Path $dll -Leaf
    if (Test-Path $dll) {
        Copy-Item $dll "$radioboss\$name" -Force
        Write-Host "  OK  $name" -ForegroundColor Green
    } else {
        Write-Host "  MISSING  $dll" -ForegroundColor Red
    }
}

Write-Host ""
Write-Host "=== Deploy complete ===" -ForegroundColor Green
Write-Host "In RadioBoss: Options > DSP/Effect Plugins > Add > select dsp_mcaster1.dll" -ForegroundColor Yellow
