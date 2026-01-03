# scan-altacast.ps1 — case-insensitive scan for any altacast variant in all source files

$src  = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\src'
$exts = @('.cpp','.h','.rc','.vcxproj','.sln','.filters')

$hits = @()
$allFiles = [System.IO.Directory]::GetFiles($src, '*', [System.IO.SearchOption]::AllDirectories)

foreach ($file in $allFiles) {
    $ext = [System.IO.Path]::GetExtension($file)
    if ($exts -notcontains $ext)      { continue }
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
    Write-Host "Found $($hits.Count) hit(s):" -ForegroundColor Yellow
    foreach ($h in $hits) {
        Write-Host "  $($h.File):$($h.Line)  $($h.Content)" -ForegroundColor Yellow
    }
} else {
    Write-Host "CLEAN - zero altacast references in any case/form" -ForegroundColor Green
}
