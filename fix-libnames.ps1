$srcDir = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\src'
$files = Get-ChildItem $srcDir -Recurse -Filter '*.vcxproj'
foreach ($f in $files) {
    $content = [System.IO.File]::ReadAllText($f.FullName)
    if ($content -match 'mp3lame\.lib') {
        $new = $content -replace 'mp3lame\.lib', 'libmp3lame.lib'
        [System.IO.File]::WriteAllText($f.FullName, $new)
        Write-Host "Fixed mp3lame -> libmp3lame: $($f.Name)" -ForegroundColor Green
    }
}
Write-Host 'Done.'
