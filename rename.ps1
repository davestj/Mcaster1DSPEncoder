# rename.ps1 — Phase 2: File and directory renames
# Run AFTER rebrand.ps1 (content replacements must be done first)
# Run from: C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\

$src = "C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\AltaCast\src"

Write-Host "=== Phase 2 Rebrand: File Rename Pass ===" -ForegroundColor Cyan
Write-Host ""

function Ren($base, $old, $new) {
    $oldPath = Join-Path $base $old
    $newName = Split-Path $new -Leaf
    if (Test-Path $oldPath) {
        Rename-Item -Path $oldPath -NewName $newName -Force
        Write-Host "  RENAMED: $old  →  $newName" -ForegroundColor Green
    } else {
        Write-Host "  SKIP (not found): $old" -ForegroundColor DarkGray
    }
}

# ── Step 1: Files inside libaltacast\ (before renaming the directory) ─────────
Write-Host "Step 1: libaltacast\ source files" -ForegroundColor Yellow
$lib = Join-Path $src "libaltacast"
Ren $lib "libaltacast.cpp"          "libmcaster1dspencoder.cpp"
Ren $lib "libaltacast.h"            "libmcaster1dspencoder.h"
Ren $lib "libaltacast_socket.h"     "libmcaster1dspencoder_socket.h"
Ren $lib "libaltacast_resample.h"   "libmcaster1dspencoder_resample.h"
Ren $lib "libaltacast.vcxproj"      "libmcaster1dspencoder.vcxproj"

# ── Step 2: Rename the libaltacast\ directory ─────────────────────────────────
Write-Host ""
Write-Host "Step 2: rename libaltacast\ directory" -ForegroundColor Yellow
Ren $src "libaltacast" "libmcaster1dspencoder"

# ── Step 3: Plugin source files ────────────────────────────────────────────────
Write-Host ""
Write-Host "Step 3: plugin source files" -ForegroundColor Yellow
Ren $src "altacast_winamp.cpp"      "mcaster1_winamp.cpp"
Ren $src "altacast_winamp.h"        "mcaster1_winamp.h"
Ren $src "altacast_foobar.cpp"      "mcaster1_foobar.cpp"
Ren $src "altacast_radiodj.cpp"     "mcaster1_radiodj.cpp"
Ren $src "altacast_radiodj.h"       "mcaster1_radiodj.h"

# ── Step 4: Shared files ───────────────────────────────────────────────────────
Write-Host ""
Write-Host "Step 4: shared header and resource files" -ForegroundColor Yellow
Ren $src "altacast.h"               "mcaster1dspencoder.h"
Ren $src "altacast.rc"              "mcaster1dspencoder.rc"
Ren $src "altacastStandalone.h"     "Mcaster1DSPEncoder.h"
Ren $src "altacastStandalone.cpp"   "Mcaster1DSPEncoder.cpp"

# ── Step 5: vcxproj and .filters files ────────────────────────────────────────
Write-Host ""
Write-Host "Step 5: vcxproj and filters files" -ForegroundColor Yellow
Ren $src "altacastStandalone.vcxproj"           "Mcaster1DSPEncoder.vcxproj"
Ren $src "altacastStandalone.vcxproj.filters"   "Mcaster1DSPEncoder.vcxproj.filters"
Ren $src "altacast_winamp.vcxproj"              "mcaster1_winamp.vcxproj"
Ren $src "altacast_winamp.vcxproj.filters"      "mcaster1_winamp.vcxproj.filters"
Ren $src "altacast_foobar.vcxproj"              "mcaster1_foobar.vcxproj"
Ren $src "altacast_radiodj.vcxproj"             "mcaster1_radiodj.vcxproj"
Ren $src "altacast_radiodj.vcxproj.filters"     "mcaster1_radiodj.vcxproj.filters"

# ── Step 6: Solution files (last — reference vcxproj by path) ─────────────────
Write-Host ""
Write-Host "Step 6: solution files" -ForegroundColor Yellow
Ren $src "altacastStandalone.sln"   "Mcaster1DSPEncoder.sln"
Ren $src "altacast_winamp.sln"      "mcaster1_winamp.sln"
Ren $src "altacast_foobar.sln"      "mcaster1_foobar.sln"
Ren $src "altacast_radiodj.sln"     "mcaster1_radiodj.sln"

Write-Host ""
Write-Host "=== File rename pass complete ===" -ForegroundColor Cyan
Write-Host "Next step: verify.ps1 then build."
