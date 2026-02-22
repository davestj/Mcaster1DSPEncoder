# deploy-foobar.ps1 — Installs foo_mcaster1.dll and runtime DLLs into Foobar2000
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
$foobar      = 'C:\Program Files\foobar2000'
$components  = "$foobar\components"
$release     = "$root\foobar2000\$Config"
$vcpkgBin    = 'C:\vcpkg\installed\x86-windows\bin'
$externalLib = "$root\external\lib"

Write-Host "=== Deploying to Foobar2000 ($Config) ===" -ForegroundColor Cyan
Write-Host "Source : $release"
Write-Host "Target : $foobar"
Write-Host ""

if (-not (Test-Path $components)) {
    Write-Host "ERROR: Foobar2000 components folder not found: $components" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path "$release\foo_mcaster1.dll")) {
    Write-Host "ERROR: foo_mcaster1.dll not found in $release" -ForegroundColor Red
    Write-Host "Build the project first: .\build-plugins.ps1 -Plugin foobar -Config $Config" -ForegroundColor Yellow
    exit 1
}

Write-Host "Copying component to components\..." -ForegroundColor Cyan
Copy-Item "$release\foo_mcaster1.dll" "$components\foo_mcaster1.dll" -Force
Write-Host "  OK  foo_mcaster1.dll -> components\" -ForegroundColor Green

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
Write-Host "Copying runtime DLLs to Foobar2000 root..." -ForegroundColor Cyan
foreach ($dll in $runtimeDlls) {
    $name = Split-Path $dll -Leaf
    if (Test-Path $dll) {
        Copy-Item $dll "$foobar\$name" -Force
        Write-Host "  OK  $name" -ForegroundColor Green
    } else {
        Write-Host "  MISSING  $dll" -ForegroundColor Red
    }
}

Write-Host ""
Write-Host "=== Deploy complete ===" -ForegroundColor Green
Write-Host "Restart Foobar2000, then: File > Preferences > Playback > DSP Manager" -ForegroundColor Yellow
Write-Host "Add 'Mcaster1 DSP Encoder' to the active DSP chain" -ForegroundColor Yellow
