$srcDir = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\src'
$paInc  = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\portaudio_src\include'
$plugins = @('mcaster1_winamp.vcxproj','mcaster1_radiodj.vcxproj','mcaster1_foobar.vcxproj')

foreach ($proj in $plugins) {
    $path = Join-Path $srcDir $proj
    $content = [System.IO.File]::ReadAllText($path)
    $old = '../;../../external/include;C:\vcpkg\installed\x86-windows\include;libmcaster1dspencoder;libmcaster1dspencoder_config;libtranscoder;FlexMeter;%(AdditionalIncludeDirectories)'
    $new = "../;../../external/include;C:\vcpkg\installed\x86-windows\include;$paInc;libmcaster1dspencoder;libmcaster1dspencoder_config;libtranscoder;FlexMeter;%(AdditionalIncludeDirectories)"
    if ($content -match [regex]::Escape($old)) {
        $content = $content.Replace($old, $new)
        [System.IO.File]::WriteAllText($path, $content)
        Write-Host "Fixed: $proj" -ForegroundColor Green
    } else {
        Write-Host "No match: $proj" -ForegroundColor Yellow
    }
}
