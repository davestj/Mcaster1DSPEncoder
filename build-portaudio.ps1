$root    = "C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder"
$paSrc   = "$root\external\portaudio\src"
$asioSdk = "$root\external\ASIOSDK"
$outDir  = "$root\external\portaudio\built"
$cmake   = "cmake"

$configs = @(
    @{ Arch = "Win32"; BuildDir = "$root\external\portaudio\build_x86"; OutLib = "portaudio_static_x86.lib" },
    @{ Arch = "x64";   BuildDir = "$root\external\portaudio\build_x64"; OutLib = "portaudio_static_x64.lib" }
)

New-Item -ItemType Directory -Force $outDir | Out-Null

foreach ($cfg in $configs) {
    $arch     = $cfg.Arch
    $buildDir = $cfg.BuildDir
    $outLib   = $cfg.OutLib

    Write-Host ""
    Write-Host "=== Configuring PortAudio for $arch ===" -ForegroundColor Cyan

    New-Item -ItemType Directory -Force $buildDir | Out-Null

    & $cmake -S $paSrc -B $buildDir `
        -A $arch `
        -DBUILD_SHARED_LIBS=OFF `
        -DPA_USE_ASIO=ON `
        -DASIO_SDK_DIR="$asioSdk" `
        -DPA_USE_DS=ON `
        -DPA_USE_WMME=ON `
        -DPA_USE_WASAPI=ON `
        -DPA_USE_WDMKS=ON 2>&1

    if ($LASTEXITCODE -ne 0) {
        Write-Host "CMake configure FAILED for $arch" -ForegroundColor Red
        continue
    }

    Write-Host ""
    Write-Host "=== Building PortAudio Release/$arch ===" -ForegroundColor Cyan
    & $cmake --build $buildDir --config Release 2>&1

    if ($LASTEXITCODE -ne 0) {
        Write-Host "Build FAILED for $arch" -ForegroundColor Red
        continue
    }

    # Find the output lib
    $lib = Get-ChildItem $buildDir -Recurse -Filter "portaudio_static.lib" |
           Where-Object { $_.FullName -match "Release" } |
           Select-Object -First 1

    if (-not $lib) {
        # Some CMake versions name it differently
        $lib = Get-ChildItem $buildDir -Recurse -Filter "*.lib" |
               Where-Object { $_.FullName -match "Release" -and $_.Name -notmatch "\.pdb" } |
               Select-Object -First 1
    }

    if ($lib) {
        $dest = Join-Path $outDir $outLib
        Copy-Item $lib.FullName $dest -Force
        Write-Host "  OUTPUT: $dest" -ForegroundColor Green
    } else {
        Write-Host "  Could not find output lib for $arch" -ForegroundColor Red
        Get-ChildItem $buildDir -Recurse -Filter "*.lib" | ForEach-Object { Write-Host "    Found: $($_.FullName)" }
    }
}

Write-Host ""
Write-Host "=== PortAudio build complete ===" -ForegroundColor Cyan
Write-Host "Outputs in: $outDir"
Get-ChildItem $outDir | Format-Table Name, @{N='KB';E={[int]($_.Length/1KB)}} -AutoSize
