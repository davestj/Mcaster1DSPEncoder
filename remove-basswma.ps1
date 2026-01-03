$srcDir = "C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\AltaCast\src"
$files = Get-ChildItem $srcDir -Recurse -Filter "*.vcxproj"

foreach ($f in $files) {
    $content = [System.IO.File]::ReadAllText($f.FullName)
    if ($content -match "basswma") {
        $new = $content -replace "basswma\.lib;", ""
        [System.IO.File]::WriteAllText($f.FullName, $new)
        Write-Host "CLEANED: $($f.Name)" -ForegroundColor Green
    }
}
Write-Host "Done."
