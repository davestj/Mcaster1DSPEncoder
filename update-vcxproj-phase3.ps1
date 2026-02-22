$srcDir  = "C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\AltaCast\src"
$vcpkgX86 = "C:\vcpkg\installed\x86-windows"
$paBuilt   = "C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\portaudio_built"
$paInclude = "C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\portaudio_src\include"

# Files that need include path + preprocessor updates
$allVcxproj = Get-ChildItem $srcDir -Recurse -Filter "*.vcxproj"

foreach ($f in $allVcxproj) {
    $content = [System.IO.File]::ReadAllText($f.FullName)
    $orig    = $content
    $changed = $false

    # ── 1. Add vcpkg x86-windows include path to AdditionalIncludeDirectories ──
    # Pattern: ../../../external/include;  →  ../../../external/include;$(VcpkgX86)\include;
    $vcpkgIncTag = "$vcpkgX86\include;"
    if ($content -notmatch [regex]::Escape($vcpkgIncTag)) {
        $content = $content -replace '(\.\./){2,3}external/include;', "`$0$vcpkgIncTag"
        $changed = $true
    }

    # ── 2. Swap preprocessor defines ─────────────────────────────────────────
    # Remove HAVE_FAAC and HAVE_AACP, add HAVE_FDKAAC and HAVE_OPUS
    if ($content -match 'HAVE_FAAC' -or $content -match 'HAVE_AACP') {
        $content = $content -replace 'HAVE_FAAC;', ''
        $content = $content -replace 'HAVE_AACP;', ''

        # Add new defines after HAVE_FLAC (or HAVE_LAME if FLAC not present)
        if ($content -match 'HAVE_FLAC;' -and $content -notmatch 'HAVE_FDKAAC') {
            $content = $content -replace 'HAVE_FLAC;', 'HAVE_FLAC;HAVE_FDKAAC;HAVE_OPUS;'
        } elseif ($content -match 'HAVE_LAME;' -and $content -notmatch 'HAVE_FDKAAC') {
            $content = $content -replace 'HAVE_LAME;', 'HAVE_LAME;HAVE_FDKAAC;HAVE_OPUS;'
        }
        $changed = $true
    }

    if ($changed) {
        [System.IO.File]::WriteAllText($f.FullName, $content)
        Write-Host "UPDATED: $($f.Name)" -ForegroundColor Green
    } else {
        Write-Host "SKIP: $($f.Name)" -ForegroundColor DarkGray
    }
}

# ── 3. Mcaster1DSPEncoder.vcxproj — swap linker deps ─────────────────────────
$exeProj = Join-Path $srcDir "Mcaster1DSPEncoder.vcxproj"
$c = [System.IO.File]::ReadAllText($exeProj)
$o = $c

# Remove old codec libs, add new ones (keep vorbis, ogg, libFLAC, pthreadVSE etc)
$c = $c -replace 'libfaac\.lib;', ''
$c = $c -replace 'bass\.lib;',    ''

# Add new libs before ws2_32.lib (if not already there)
if ($c -notmatch 'mp3lame\.lib') {
    $c = $c -replace 'ws2_32\.lib;', "mp3lame.lib;fdk-aac.lib;opus.lib;opusenc.lib;msacm32.lib;portaudio_static_x86.lib;ws2_32.lib;"
}

# Add portaudio lib dir and vcpkg lib dir
if ($c -notmatch 'portaudio_built') {
    $c = $c -replace '(<AdditionalLibraryDirectories>)', "`$1$paBuilt;$vcpkgX86\lib;"
}

if ($c -ne $o) {
    [System.IO.File]::WriteAllText($exeProj, $c)
    Write-Host "LINKER UPDATED: Mcaster1DSPEncoder.vcxproj" -ForegroundColor Cyan
}

# ── 4. Plugin vcxproj — remove old codec libs, add new codec libs ─────────────
$pluginProjs = @(
    (Join-Path $srcDir "mcaster1_winamp.vcxproj"),
    (Join-Path $srcDir "mcaster1_radiodj.vcxproj"),
    (Join-Path $srcDir "mcaster1_foobar.vcxproj")
)

foreach ($pf in $pluginProjs) {
    $c = [System.IO.File]::ReadAllText($pf)
    $o = $c

    $c = $c -replace 'libfaac\.lib;', ''
    $c = $c -replace 'bass\.lib;',    ''
    # libmp3lame.lib may already be there (foobar had it) — add if missing
    if ($c -notmatch 'mp3lame\.lib') {
        $c = $c -replace 'ws2_32\.lib;', "mp3lame.lib;fdk-aac.lib;opus.lib;opusenc.lib;msacm32.lib;ws2_32.lib;"
    }
    if ($c -notmatch "$vcpkgX86\lib") {
        $c = $c -replace '(<AdditionalLibraryDirectories>)', "`$1$vcpkgX86\lib;"
    }

    if ($c -ne $o) {
        [System.IO.File]::WriteAllText($pf, $c)
        Write-Host "LINKER UPDATED: $(Split-Path $pf -Leaf)" -ForegroundColor Cyan
    }
}

Write-Host ""
Write-Host "=== vcxproj updates complete ===" -ForegroundColor Cyan
