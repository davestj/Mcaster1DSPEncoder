param(
    [ValidateSet("Release","Debug")]
    [string]$Config = "Debug"
)

# copy-dlls.ps1
# Copies all runtime DLLs from external\lib into each project's output directory
# so that the installers (makensis) and direct-run from the build dir both work.
#
# Run this after a successful build.

$root   = "C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder"
$extLib = "$root\external\lib"

# Per-project output directories (relative to $root)
$outDirs = [ordered]@{
    "Standalone"  = "$root\$Config"
    "Winamp"      = "$root\Winamp_Plugin\$Config"
    "foobar2000"  = "$root\foobar2000\$Config"
}

# Runtime DLLs provided by external\lib
$dlls = @(
    "ogg.dll",
    "vorbis.dll",
    "vorbisenc.dll",
    "libFLAC.dll",
    "pthreadVSE.dll",
    "libcurl.dll",
    "iconv.dll"
)

Write-Host "=== Copying external DLLs ($Config) ===" -ForegroundColor Cyan
Write-Host "Source: $extLib"
Write-Host ""

foreach ($label in $outDirs.Keys) {
    $dest = $outDirs[$label]
    Write-Host "--- $label -> $dest" -ForegroundColor Yellow
    if (-not (Test-Path $dest)) {
        Write-Host "  Output dir does not exist yet (build first): $dest" -ForegroundColor Red
        continue
    }
    foreach ($dll in $dlls) {
        $src = Join-Path $extLib $dll
        $dst = Join-Path $dest  $dll
        if (Test-Path $src) {
            Copy-Item $src $dst -Force
            Write-Host "  COPIED: $dll" -ForegroundColor Green
        } else {
            Write-Host "  MISSING in external\lib: $dll" -ForegroundColor Red
        }
    }
    Write-Host ""
}

Write-Host "Done." -ForegroundColor Cyan
