$root = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder'
$files = @(
    'src\Mcaster1DSPEncoder.vcxproj',
    'src\mcaster1_winamp.vcxproj',
    'src\mcaster1_foobar.vcxproj',
    'src\libmcaster1dspencoder\libmcaster1dspencoder.vcxproj'
)

Write-Host '=== Remaining absolute C:\Users paths in main vcxproj files ===' -ForegroundColor Cyan
$found = $false
foreach ($f in $files) {
    $full = Join-Path $root $f
    $hits = Get-Content $full |
        Select-String 'C:\\Users' |
        Where-Object { $_ -notmatch '\.pdb|\.pch|user\.props|\.user|LocalAppData' }
    if ($hits) {
        Write-Host "  FOUND in $f :" -ForegroundColor Red
        $hits | ForEach-Object { Write-Host "    $_" }
        $found = $true
    } else {
        Write-Host "  OK: $f" -ForegroundColor Green
    }
}
if (-not $found) {
    Write-Host ''
    Write-Host 'No machine-specific absolute paths remain.' -ForegroundColor Green
}

Write-Host ''
Write-Host '=== vcpkg absolute paths (acceptable — vcpkg is machine-local) ===' -ForegroundColor Yellow
foreach ($f in $files) {
    $full = Join-Path $root $f
    $hits = Get-Content $full | Select-String 'vcpkg'
    if ($hits) {
        Write-Host "  $f :"
        $hits | ForEach-Object { Write-Host "    $_" }
    }
}
