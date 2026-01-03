# restructure-p3.ps1 — Phases 3, 4, 5 (after directory move already done)

$newSrc = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\src'
$base   = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder'

function PatchFile($path, $old, $new) {
    $c = [System.IO.File]::ReadAllText($path)
    if ($c.Contains($old)) {
        [System.IO.File]::WriteAllText($path, $c.Replace($old, $new))
        return $true
    }
    return $false
}

# -----------------------------------------------------------------------
# PHASE 3a — libmcaster1dspencoder.vcxproj (one level deeper)
# ../../../ currently pointed to McasterDSPEncoder\, after move ../../ does that
# -----------------------------------------------------------------------
Write-Host ""
Write-Host "=== Phase 3a: libmcaster1dspencoder.vcxproj ===" -ForegroundColor Cyan
$libvcx = "$newSrc\libmcaster1dspencoder\libmcaster1dspencoder.vcxproj"
if (Test-Path $libvcx) {
    if (PatchFile $libvcx '../../../' '../../') {
        Write-Host "  Fixed 3-level to 2-level refs" -ForegroundColor Green
    } else {
        Write-Host "  No 3-level refs found (already fixed?)" -ForegroundColor DarkGray
    }
}

# -----------------------------------------------------------------------
# PHASE 3b — Top-level project files
# ../../ and ..\..\ need to drop one level (../../ -> ../, ..\..\ -> .\)
# -----------------------------------------------------------------------
Write-Host ""
Write-Host "=== Phase 3b: Top-level project files ===" -ForegroundColor Cyan

$extensions = @('.vcxproj', '.sln', '.filters')
$topFiles   = [System.IO.Directory]::GetFiles($newSrc) |
              Where-Object { $extensions -contains [System.IO.Path]::GetExtension($_) }

foreach ($file in $topFiles) {
    $name    = [System.IO.Path]::GetFileName($file)
    $changed = $false
    if (PatchFile $file '../../' '../')  { $changed = $true }
    if (PatchFile $file '..\..\' '..\') { $changed = $true }
    if ($changed) {
        Write-Host "  Fixed: $name" -ForegroundColor Green
    } else {
        Write-Host "  No change needed: $name" -ForegroundColor DarkGray
    }
}

# -----------------------------------------------------------------------
# PHASE 4 — Build / deploy scripts
# -----------------------------------------------------------------------
Write-Host ""
Write-Host "=== Phase 4: Build scripts ===" -ForegroundColor Cyan

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
        $name = [System.IO.Path]::GetFileName($s)
        $c1 = PatchFile $s 'AltaCast\src' 'src'
        $c2 = PatchFile $s 'AltaCast/src' 'src'
        if ($c1 -or $c2) {
            Write-Host "  Fixed: $name" -ForegroundColor Green
        } else {
            Write-Host "  No change needed: $name" -ForegroundColor DarkGray
        }
    }
}

# -----------------------------------------------------------------------
# PHASE 5 — Scan for remaining AltaCast references
# -----------------------------------------------------------------------
Write-Host ""
Write-Host "=== Phase 5: Scan for remaining AltaCast refs ===" -ForegroundColor Cyan

$exts = @('.cpp','.h','.rc','.vcxproj','.sln','.filters')
$hits = @()

$allFiles = [System.IO.Directory]::GetFiles($newSrc, '*', [System.IO.SearchOption]::AllDirectories)
foreach ($file in $allFiles) {
    if ($exts -notcontains [System.IO.Path]::GetExtension($file)) { continue }
    if ($file -match '\\foobar2000\\') { continue }
    if ($file -match '\\Release\\')    { continue }
    if ($file -match '\\Debug\\')      { continue }
    $lines = [System.IO.File]::ReadAllLines($file)
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -imatch 'altacast') {
            $hits += [PSCustomObject]@{
                File    = $file.Replace($newSrc + '\', '')
                Line    = $i + 1
                Content = $lines[$i].Trim()
            }
        }
    }
}

if ($hits.Count -gt 0) {
    Write-Host "  Remaining hits:" -ForegroundColor Yellow
    foreach ($h in $hits) {
        Write-Host "    $($h.File):$($h.Line)  $($h.Content)" -ForegroundColor Yellow
    }
} else {
    Write-Host "  CLEAN - no AltaCast references remain" -ForegroundColor Green
}

Write-Host ""
Write-Host "=== Done. New source root: $newSrc ===" -ForegroundColor Cyan
