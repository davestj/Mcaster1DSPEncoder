# deploy_release.ps1
# Copies all required DLL dependencies into the Release build directory.
# Run this after every Release build (or once after initial build).
# Usage:  powershell -ExecutionPolicy Bypass -File deploy_release.ps1

param(
    [string]$Config    = "Release",
    [string]$QtDir     = "C:\Qt\6.9.3\msvc2022_64",
    [string]$VcpkgDir  = "C:\vcpkg\installed\x64-windows"
)

$ErrorActionPreference = "Stop"

$BuildDir = "$PSScriptRoot\build\win-qt\$Config"
$Exe      = "$BuildDir\Mcaster1DSPEncoder_Qt.exe"

if (-not (Test-Path $Exe)) {
    Write-Error "Build output not found: $Exe`nRun a $Config build first."
    exit 1
}

Write-Host "`n=== Deploying $Config build to:`n    $BuildDir`n" -ForegroundColor Cyan

# ── 1. windeployqt — copies Qt6*.dll + all needed plugins ─────────────────
$wdqt = "$QtDir\bin\windeployqt.exe"
if (-not (Test-Path $wdqt)) {
    Write-Error "windeployqt not found at: $wdqt"
    exit 1
}

Write-Host "Running windeployqt..." -ForegroundColor Yellow
$wdArgs = @(
    "--$($Config.ToLower())"   # --release or --debug
    "--no-translations"
    "--no-system-d3d-compiler"
    "--multimedia"
    "--network"
    $Exe
)
# Redirect stderr to stdout so dxcompiler/dxil warnings don't trigger PS error handling
& $wdqt @wdArgs 2>&1 | Where-Object { $_ -notmatch 'dxcompiler|dxil' } | Write-Host
# windeployqt exits 0 even on warnings; only fail on genuine errors (exit >= 2)
if ($LASTEXITCODE -ge 2) {
    Write-Error "windeployqt failed (exit $LASTEXITCODE)"
    exit 1
}
Write-Host "windeployqt done." -ForegroundColor Green

# ── 2. vcpkg runtime DLLs ─────────────────────────────────────────────────
#    windeployqt does not know about vcpkg; copy them explicitly.
$vcpkgBin = "$VcpkgDir\bin"
$vcpkgDlls = @(
    "portaudio.dll",
    "libmp3lame.dll",
    "ogg.dll",
    "vorbis.dll",
    "vorbisenc.dll",
    "opus.dll",
    "opusenc.dll",
    "FLAC.dll",
    "yaml.dll",
    "libcrypto-3-x64.dll",
    "libssl-3-x64.dll"
)

Write-Host "`nCopying vcpkg DLLs..." -ForegroundColor Yellow
foreach ($dll in $vcpkgDlls) {
    $src = "$vcpkgBin\$dll"
    if (Test-Path $src) {
        Copy-Item $src $BuildDir -Force
        Write-Host "  + $dll"
    } else {
        # Try case-insensitive search for DLLs with varying capitalisation
        $found = Get-ChildItem $vcpkgBin -Filter $dll -ErrorAction SilentlyContinue |
                 Select-Object -First 1
        if ($found) {
            Copy-Item $found.FullName $BuildDir -Force
            Write-Host "  + $($found.Name)"
        } else {
            Write-Warning "  ! Not found: $dll (skipping)"
        }
    }
}

# ── 3. MSVC runtime (if not already present) ──────────────────────────────
#    vcredist DLLs are usually installed system-wide, but include them for
#    portability.  Grab from the VS2022 redist path.
$vsRedist = "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Redist\MSVC"
if (Test-Path $vsRedist) {
    $latestRedist = Get-ChildItem $vsRedist -Directory |
                    Sort-Object Name -Descending | Select-Object -First 1
    if ($latestRedist) {
        $crtDir = "$($latestRedist.FullName)\x64\Microsoft.VC143.CRT"
        if (Test-Path $crtDir) {
            Write-Host "`nCopying MSVC CRT DLLs from $crtDir..." -ForegroundColor Yellow
            foreach ($dll in @("msvcp140.dll","vcruntime140.dll","vcruntime140_1.dll")) {
                $src = "$crtDir\$dll"
                if (Test-Path $src) {
                    Copy-Item $src $BuildDir -Force
                    Write-Host "  + $dll"
                }
            }
        }
    }
}

# ── 4. Final summary ──────────────────────────────────────────────────────
Write-Host "`n=== Deploy complete ===" -ForegroundColor Green
Write-Host "Build directory contents:"
Get-ChildItem $BuildDir -File | Sort-Object Name |
    Format-Table Name, @{L="Size KB";E={[math]::Round($_.Length/1KB,1)}} -AutoSize
