$vcpkg = "C:\vcpkg\vcpkg.exe"

Write-Host "=== Phase 3 vcpkg installs ===" -ForegroundColor Cyan
Write-Host ""

$packages = @(
    "fdk-aac[core,he-aac]:x86-windows",
    "fdk-aac[core,he-aac]:x64-windows",
    "mp3lame:x86-windows",
    "mp3lame:x64-windows",
    "opus:x86-windows",
    "opus:x64-windows",
    "libopusenc:x86-windows",
    "libopusenc:x64-windows"
)

foreach ($pkg in $packages) {
    Write-Host "Installing: $pkg" -ForegroundColor Yellow
    & $vcpkg install $pkg
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  OK: $pkg" -ForegroundColor Green
    } else {
        Write-Host "  FAILED: $pkg" -ForegroundColor Red
    }
    Write-Host ""
}

Write-Host "=== Installed x86-windows packages ===" -ForegroundColor Cyan
& $vcpkg list | Where-Object { $_ -match "x86-windows" }
Write-Host ""
Write-Host "=== Installed x64-windows packages ===" -ForegroundColor Cyan
& $vcpkg list | Where-Object { $_ -match "x64-windows" }
