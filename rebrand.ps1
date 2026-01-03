# rebrand.ps1 — Phase 2: AltaCast → Mcaster1DSPEncoder content replacement pass
# Run from: C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\
# Does NOT rename files — run rename.ps1 after this completes.

$srcRoot = "C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\AltaCast\src"

Write-Host "=== Phase 2 Rebrand: Content Replacement Pass ===" -ForegroundColor Cyan
Write-Host "Source root: $srcRoot"
Write-Host ""

# Collect all files to process — source, project, solution, resource files
# Exclude: build output dirs, foobar2000 SDK, legacy/backup files, binary dirs
$files = Get-ChildItem -Path $srcRoot -Recurse -File |
    Where-Object {
        $_.Extension -in @('.cpp','.h','.rc','.vcxproj','.sln','.filters','.nsi') -and
        $_.FullName -notmatch '\\Release\\|\\Debug\\|\\x64\\|\\Win32\\' -and
        $_.FullName -notmatch '\\foobar2000\\' -and
        $_.FullName -notmatch '\\external\\' -and
        $_.FullName -notmatch '\\Interop\\' -and
        $_.FullName -notmatch '\\\.tmp\\' -and
        $_.Name    -notmatch '\.old$|\.original$'
    }

Write-Host "Files to process: $($files.Count)" -ForegroundColor Yellow
Write-Host ""

$changedCount = 0

foreach ($file in $files) {
    # Read as raw string preserving line endings
    $content = [System.IO.File]::ReadAllText($file.FullName)
    $original = $content

    # ── Class / struct / function names (most specific first) ──────────────────
    $content = $content -replace 'CaltacastStandaloneApp',       'CMcaster1DSPEncoderApp'
    $content = $content -replace 'altacastGlobals',               'mcaster1Globals'
    $content = $content -replace 'altacast_play_callback_ui',     'mcaster1_play_callback_ui'
    $content = $content -replace 'initquit_altacast',             'initquit_mcaster1'
    $content = $content -replace 'altacast_init',                 'mcaster1_init'

    # ── Preprocessor defines ───────────────────────────────────────────────────
    $content = $content -replace 'FRONT_END_ALTACAST_PLUGIN',     'FRONT_END_MCASTER1_PLUGIN'
    $content = $content -replace 'AFX_ALTACASTSTANDALONE_H__',    'AFX_MCASTER1DSPENCODER_H__'
    $content = $content -replace 'IDD_ALTACAST_CONFIG',           'IDD_MCASTER1_CONFIG'
    $content = $content -replace 'ALTACAST_WINAMP_EXPORTS',       'MCASTER1_WINAMP_EXPORTS'
    $content = $content -replace 'ALTACAST_RADIODJ_EXPORTS',      'MCASTER1_RADIODJ_EXPORTS'
    $content = $content -replace 'ALTACAST_PLUGIN',               'MCASTER1_PLUGIN'
    $content = $content -replace 'ALTACASTSTANDALONE',            'MCASTER1DSPENCODER'
    $content = $content -replace 'altacastSTANDALONE',            'MCASTER1DSPENCODER'
    $content = $content -replace '__DSP_ALTACAST_H',              '__DSP_MCASTER1_H'
    $content = $content -replace 'IDD_ALTACAST',                  'IDD_MCASTER1'
    $content = $content -replace 'APP_NAME=altacast_foobar',      'APP_NAME=mcaster1_foobar'
    $content = $content -replace 'APP_NAME=altacast_radiodj',     'APP_NAME=mcaster1_radiodj'
    $content = $content -replace 'APP_NAME=altacast_winamp',      'APP_NAME=mcaster1_winamp'

    # ── Compound identifiers (specific → general) ──────────────────────────────
    $content = $content -replace 'libaltacast',                   'libmcaster1dspencoder'
    $content = $content -replace 'dsp_altacastv2',                'dsp_mcaster1v2'
    $content = $content -replace 'dsp_altacast',                  'dsp_mcaster1'
    $content = $content -replace 'foo_altacast',                  'foo_mcaster1'
    $content = $content -replace 'altacast_winamp',               'mcaster1_winamp'
    $content = $content -replace 'altacast_foobar',               'mcaster1_foobar'
    $content = $content -replace 'altacast_radiodj',              'mcaster1_radiodj'
    $content = $content -replace 'altacastStandalone',            'Mcaster1DSPEncoder'
    $content = $content -replace 'altacaststandalone',            'mcaster1dspencoder'

    # ── File-extension-anchored patterns ──────────────────────────────────────
    $content = $content -replace 'altacast\.log',                 'mcaster1dspencoder.log'
    $content = $content -replace 'altacast\.chm',                 'mcaster1dspencoder.chm'
    $content = $content -replace 'altacast_aacp\.ini',            'mcaster1_aacp.ini'
    $content = $content -replace 'altacast\.rc',                  'mcaster1dspencoder.rc'
    $content = $content -replace 'altacast\.h',                   'mcaster1dspencoder.h'

    # ── String literals and attribution ───────────────────────────────────────
    $content = $content -replace 'Written by admin@altacast\.com','Maintained by davestj@gmail.com'
    $content = $content -replace 'admin@altacast\.com',           'davestj@gmail.com'
    $content = $content -replace 'http://www\.altacast\.com',     'https://github.com/davestj/Mcaster1DSPEncoder'
    $content = $content -replace '"ENCODEDBY","altacast"',        '"ENCODEDBY","mcaster1dspencoder"'
    $content = $content -replace '(gAppName,\s*)"altacast"',      '$1"mcaster1dspencoder"'
    $content = $content -replace '"AltaCast 1\.1"',               '"Mcaster1 DSP Encoder"'
    $content = $content -replace '"AltaCast Standalone"',         '"Mcaster1 DSP Encoder"'
    $content = $content -replace '"AltaCast"',                    '"Mcaster1 DSP Encoder"'

    # ── Final generic quoted-string pass ──────────────────────────────────────
    # Catches remaining "altacast" string literals (log prefixes, gAppName, tray, session)
    $content = $content -replace '"altacast"',                    '"mcaster1dspencoder"'

    if ($content -ne $original) {
        [System.IO.File]::WriteAllText($file.FullName, $content)
        Write-Host "  UPDATED: $($file.FullName.Replace($srcRoot,''))" -ForegroundColor Green
        $changedCount++
    }
}

Write-Host ""
Write-Host "=== Content replacement complete: $changedCount files updated ===" -ForegroundColor Cyan
Write-Host "Next step: run rename.ps1 to rename files and directories."
