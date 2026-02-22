# restructure.ps1
# 1. Fix remaining AltaCast display name strings in source files
# 2. Move AltaCast\src\ to src\
# 3. Update all relative paths in vcxproj / sln files
# 4. Update build/deploy scripts to new paths

$base   = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder'
$oldSrc = "$base\AltaCast\src"
$newSrc = "$base\src"

$ErrorActionPreference = 'Stop'

function ReplaceInFile($path, $old, $new) {
    $content = [System.IO.File]::ReadAllText($path)
    if ($content.Contains($old)) {
        [System.IO.File]::WriteAllText($path, $content.Replace($old, $new))
        return $true
    }
    return $false
}

# -----------------------------------------------------------------------
# PHASE 1 - Fix display names while files are still at old location
# -----------------------------------------------------------------------
Write-Host ""
Write-Host "=== Phase 1: Fix display name strings ===" -ForegroundColor Cyan

$fixes = @(
    @{ File="mcaster1_winamp.cpp"; Old='"AltaCast DSP",';              New='"Mcaster1 DSP Encoder",'; },
    @{ File="mcaster1_winamp.cpp"; Old='"AltaCast DSP"';               New='"Mcaster1 DSP Encoder"'; },
    @{ File="mcaster1_radiodj.cpp";Old='"AltaCast DSP",';              New='"Mcaster1 DSP for RadioDJ",'; },
    @{ File="mcaster1_radiodj.cpp";Old='"AltaCast for RadioDJ DSP"';   New='"Mcaster1 DSP for RadioDJ"'; },
    @{ File="mcaster1_foobar.cpp"; Old='"altacast\n"';                 New='"Mcaster1 DSP Encoder\n"'; },
    @{ File="MainWindow.cpp";      Old='"AltaCast Restore"';            New='"Mcaster1 DSP Encoder"'; },
    @{ File="FlexMeters.h";        Old='// AltaCastMeters.h:';          New='// FlexMeters.h:'; }
)

foreach ($f in $fixes) {
    $path = Join-Path $oldSrc $f.File
    if (Test-Path $path) {
        $changed = ReplaceInFile $path $f.Old $f.New
        if ($changed) {
            Write-Host "  Fixed: $($f.File)" -ForegroundColor Green
        } else {
            Write-Host "  No match (already done?): $($f.File)" -ForegroundColor DarkGray
        }
    } else {
        Write-Host "  NOT FOUND: $path" -ForegroundColor Red
    }
}

# -----------------------------------------------------------------------
# PHASE 2 - Move AltaCast\src\ to src\
# -----------------------------------------------------------------------
Write-Host ""
Write-Host "=== Phase 2: Move directory ===" -ForegroundColor Cyan

if (-not (Test-Path $newSrc)) {
    New-Item -ItemType Directory -Path $newSrc | Out-Null
    Write-Host "  Created $newSrc" -ForegroundColor Green
} else {
    Write-Host "  Target $newSrc already exists - merging" -ForegroundColor Yellow
}

$rc = Start-Process robocopy -ArgumentList "`"$oldSrc`" `"$newSrc`" /E /MOVE /NFL /NDL /NJH /NJS" -Wait -PassThru -NoNewWindow
if ($rc.ExitCode -gt 7) {
    Write-Host "  robocopy exit $($rc.ExitCode) - check manually" -ForegroundColor Red
} else {
    Write-Host "  Move complete (robocopy exit $($rc.ExitCode))" -ForegroundColor Green
}

if (Test-Path "$base\AltaCast") {
    Remove-Item "$base\AltaCast" -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "  Removed $base\AltaCast" -ForegroundColor Green
}

# -----------------------------------------------------------------------
# PHASE 3 - Update relative paths in vcxproj / sln / filters files
# -----------------------------------------------------------------------
Write-Host ""
Write-Host "=== Phase 3: Fix relative paths in project files ===" -ForegroundColor Cyan

# libmcaster1dspencoder.vcxproj is one level deeper (src\libmcaster1dspencoder\)
# ../../../ currently pointed to McasterDSPEncoder\ - after move ../../ does the same
$libvcx = "$newSrc\libmcaster1dspencoder\libmcaster1dspencoder.vcxproj"
if (Test-Path $libvcx) {
    $c = ReplaceInFile $libvcx '../../../' '../../'
    if ($c) {
        Write-Host "  Fixed (3-level to 2-level): libmcaster1dspencoder.vcxproj" -ForegroundColor Green
    }
}

# Top-level vcxproj / sln / filters:
# ../../ (forward slash) currently pointed to McasterDSPEncoder\ from AltaCast\src\
# After move, from src\ that same target is only one level up: ../
# Same logic for backslash variant ..\..\ used in ProjectReference and .sln lines
$topFiles = Get-ChildItem $newSrc -MaxDepth 1 -File |
    Where-Object { $_.Extension -in @('.vcxproj','.sln','.filters') }

foreach ($file in $topFiles) {
    $changed = $false
    if (ReplaceInFile $file.FullName '../../' '../')    { $changed = $true }
    if (ReplaceInFile $file.FullName '..\..\' '..\')   { $changed = $true }
    if ($changed) {
        Write-Host "  Fixed paths: $($file.Name)" -ForegroundColor Green
    } else {
        Write-Host "  No path changes needed: $($file.Name)" -ForegroundColor DarkGray
    }
}

# -----------------------------------------------------------------------
# PHASE 4 - Update build/deploy scripts that hardcode AltaCast\src path
# -----------------------------------------------------------------------
Write-Host ""
Write-Host "=== Phase 4: Update build scripts ===" -ForegroundColor Cyan

$scripts = @(
    "$base\build-main.ps1",
    "$base\build-plugins.ps1",
    "$base\fix-plugin-pa.ps1",
    "$base\fix-plugin-includes.ps1",
    "$base\fix-libnames.ps1",
    "$base\deploy-winamp.ps1"
)

foreach ($s in $scripts) {
    if (Test-Path $s) {
        $c1 = ReplaceInFile $s 'AltaCast\src' 'src'
        $c2 = ReplaceInFile $s 'AltaCast/src' 'src'
        if ($c1 -or $c2) {
            Write-Host "  Fixed: $(Split-Path $s -Leaf)" -ForegroundColor Green
        } else {
            Write-Host "  No change needed: $(Split-Path $s -Leaf)" -ForegroundColor DarkGray
        }
    }
}

# -----------------------------------------------------------------------
# PHASE 5 - Scan for remaining AltaCast references
# -----------------------------------------------------------------------
Write-Host ""
Write-Host "=== Phase 5: Scan for remaining AltaCast references ===" -ForegroundColor Cyan

$remaining = Get-ChildItem $newSrc -Recurse -File |
    Where-Object {
        $_.Extension -in @('.cpp','.h','.rc','.vcxproj','.sln','.filters') -and
        $_.FullName -notmatch '\\foobar2000\\' -and
        $_.FullName -notmatch '\\Release\\' -and
        $_.FullName -notmatch '\\Debug\\'
    } |
    ForEach-Object {
        $path = $_.FullName
        $lines = [System.IO.File]::ReadAllLines($path)
        for ($i = 0; $i -lt $lines.Count; $i++) {
            if ($lines[$i] -imatch 'altacast') {
                [PSCustomObject]@{
                    File    = $path.Replace($newSrc + '\','')
                    Line    = $i + 1
                    Content = $lines[$i].Trim()
                }
            }
        }
    }

if ($remaining) {
    Write-Host "  Remaining AltaCast hits:" -ForegroundColor Yellow
    $remaining | ForEach-Object {
        Write-Host "    $($_.File):$($_.Line)  $($_.Content)" -ForegroundColor Yellow
    }
} else {
    Write-Host "  CLEAN - no AltaCast references remain" -ForegroundColor Green
}

Write-Host ""
Write-Host "=== Restructure complete ===" -ForegroundColor Cyan
Write-Host "New source root: $newSrc"
