# restructure-p6.ps1 — Fix all remaining altacast references in source files

$src = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\src'

function PatchFile($path, $old, $new) {
    $c = [System.IO.File]::ReadAllText($path)
    if ($c.Contains($old)) {
        [System.IO.File]::WriteAllText($path, $c.Replace($old, $new))
        return $true
    }
    return $false
}

function PatchAll($relFiles, $old, $new) {
    $anyChanged = $false
    foreach ($rel in $relFiles) {
        $path = Join-Path $src $rel
        if (Test-Path $path) {
            if (PatchFile $path $old $new) {
                Write-Host "  Fixed '$old' in $rel" -ForegroundColor Green
                $anyChanged = $true
            }
        }
    }
    return $anyChanged
}

Write-Host ""
Write-Host "=== Fix string literals (user-visible / stream data) ===" -ForegroundColor Cyan

# ENCODEDBY metadata tag embedded in OGG Vorbis streams
PatchAll @('libmcaster1dspencoder\libmcaster1dspencoder.cpp') `
    'ENCODEDBY=altacast' 'ENCODEDBY=mcaster1dspencoder'

# foobar2000 log file name prefix
PatchAll @('mcaster1_foobar.cpp') `
    '"altacast_foo"' '"mcaster1_foo"'

# foobar2000 version/about string
PatchAll @('mcaster1_foobar.cpp') `
    '"altacast V3"' '"Mcaster1 DSP Encoder V3"'

# Config default for SourceURL (Linux FIFO path default)
PatchAll @('libmcaster1dspencoder\libmcaster1dspencoder.cpp') `
    '"/tmp/altacastFIFO"' '"/tmp/mcaster1FIFO"'

# Config description comment strings
PatchAll @('libmcaster1dspencoder\libmcaster1dspencoder.cpp') `
    '"Indicator which tells altacast to grab metadata from a defined window class"' `
    '"Indicator which tells mcaster1dspencoder to grab metadata from a defined window class"'

Write-Host ""
Write-Host "=== Fix internal C++ identifiers ===" -ForegroundColor Cyan

# --- Function / variable renames (applied to all relevant files) ---

# Thread function names and the thread handle variable
$mainFiles = @('MainWindow.cpp', 'MainWindow.h',
               'mcaster1_winamp.cpp', 'mcaster1_radiodj.cpp', 'mcaster1_foobar.cpp')

PatchAll $mainFiles 'startSpecificaltacastThread' 'startSpecificMcaster1Thread'
PatchAll $mainFiles 'startaltacastThread'          'startMcaster1Thread'
PatchAll $mainFiles 'altacastThread'               'mcaster1Thread'

# Main init/stop/start methods
PatchAll $mainFiles 'initializealtacast'           'initializeMcaster1'
PatchAll $mainFiles 'stopaltacast'                 'stopMcaster1'
PatchAll $mainFiles 'startaltacast'                'startMcaster1'

# Winamp/RadioDJ DSP module init/quit callbacks and HWND
$pluginFiles = @('mcaster1_winamp.cpp', 'mcaster1_radiodj.cpp')
PatchAll $pluginFiles 'initaltacast'               'initMcaster1'
PatchAll $pluginFiles 'quitaltacast'               'quitMcaster1'
PatchAll $pluginFiles 'altacastHWND'               'mcaster1HWND'

Write-Host ""
Write-Host "=== Fix remaining comments ===" -ForegroundColor Cyan
PatchAll @('Mcaster1DSPEncoder.cpp') `
    '".\\altacast_standalone"' '".\\mcaster1dspencoder"'
PatchAll @('mcaster1_foobar.cpp') `
    'altacast_show_on_startup' 'mcaster1_show_on_startup'
PatchAll @('libmcaster1dspencoder\libmcaster1dspencoder.cpp') `
    'altacast will reconnect' 'mcaster1dspencoder will reconnect'

Write-Host ""
Write-Host "=== Final scan for remaining altacast refs ===" -ForegroundColor Cyan

$exts = @('.cpp','.h','.rc','.vcxproj','.sln','.filters')
$hits = @()
$allFiles = [System.IO.Directory]::GetFiles($src, '*', [System.IO.SearchOption]::AllDirectories)
foreach ($file in $allFiles) {
    if ($exts -notcontains [System.IO.Path]::GetExtension($file)) { continue }
    if ($file -match '\\foobar2000\\') { continue }
    if ($file -match '\\Release\\')    { continue }
    if ($file -match '\\Debug\\')      { continue }
    $lines = [System.IO.File]::ReadAllLines($file)
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -imatch 'altacast') {
            $hits += [PSCustomObject]@{
                File    = $file.Replace($src + '\', '')
                Line    = $i + 1
                Content = $lines[$i].Trim()
            }
        }
    }
}

if ($hits.Count -gt 0) {
    Write-Host "  Remaining:" -ForegroundColor Yellow
    foreach ($h in $hits) {
        Write-Host "    $($h.File):$($h.Line)  $($h.Content)" -ForegroundColor Yellow
    }
} else {
    Write-Host "  CLEAN - zero AltaCast references remain" -ForegroundColor Green
}
Write-Host ""
Write-Host "Done." -ForegroundColor Cyan
