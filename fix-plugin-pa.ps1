$srcDir = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\src'
$paInc  = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\portaudio_src\include'
$paLib  = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\portaudio_built'
$plugins = @('mcaster1_winamp.vcxproj','mcaster1_radiodj.vcxproj','mcaster1_foobar.vcxproj')

foreach ($proj in $plugins) {
    $path    = Join-Path $srcDir $proj
    $content = [System.IO.File]::ReadAllText($path)
    $orig    = $content
    $changed = $false

    # 1. Add portaudio include path if missing
    if ($content -notmatch [regex]::Escape($paInc)) {
        $content = $content -replace [regex]::Escape('../../external/include;C:\vcpkg\installed\x86-windows\include;'),
                                     "../../external/include;C:\vcpkg\installed\x86-windows\include;$paInc;"
        $changed = $true
    }

    # 2. Add portaudio lib dir if missing
    if ($content -notmatch [regex]::Escape($paLib)) {
        $content = $content -replace '(<AdditionalLibraryDirectories>)', "`$1$paLib;"
        $changed = $true
    }

    # 3. Add portaudio_static_x86.lib to AdditionalDependencies if missing
    if ($content -notmatch 'portaudio_static_x86\.lib') {
        $content = $content -replace 'libmcaster1dspencoder\.lib;', 'portaudio_static_x86.lib;libmcaster1dspencoder.lib;'
        $changed = $true
    }

    if ($changed) {
        [System.IO.File]::WriteAllText($path, $content)
        Write-Host "Fixed: $proj" -ForegroundColor Green
    } else {
        Write-Host "No changes needed: $proj" -ForegroundColor DarkGray
    }
}
Write-Host 'Done.'
