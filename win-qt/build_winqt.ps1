$ErrorActionPreference = "Stop"
$QTDIR = "C:\Qt\6.9.3\msvc2022_64"
$MSBUILD = "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
$VCVARS = "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
$PROJ_APP  = "$PSScriptRoot\Mcaster1DSPEncoder_Qt.vcxproj"
$PROJ_VCAM = "$PSScriptRoot\virtual_camera\Mcaster1VirtualCam.vcxproj"
$EXEDIR = "$PSScriptRoot\build\win-qt\Debug"

# Set up MSVC environment by calling vcvars64.bat and harvesting its env changes
$vcEnv = cmd.exe /c "`"$VCVARS`" >nul 2>&1 && set" 2>&1
foreach ($line in $vcEnv) {
    if ($line -match "^([^=]+)=(.+)$") {
        [System.Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], "Process")
    }
}
$env:QTDIR = $QTDIR
$env:PATH  = "$QTDIR\bin;" + $env:PATH

Write-Host "[Build] Building main encoder (MSBuild)..."
& $MSBUILD $PROJ_APP /p:Configuration=Debug /p:Platform=x64 /p:QTDIR=$QTDIR /m /nologo /v:m
if ($LASTEXITCODE -ne 0) { Write-Host "[Build] Main app FAILED with error $LASTEXITCODE"; exit $LASTEXITCODE }
Write-Host "[Build] Main app SUCCESS"

Write-Host "[Build] Building virtual camera DLL (MSBuild)..."
& $MSBUILD $PROJ_VCAM /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:m
if ($LASTEXITCODE -ne 0) { Write-Host "[Build] VCam DLL FAILED with error $LASTEXITCODE"; exit $LASTEXITCODE }
Write-Host "[Build] VCam DLL SUCCESS"

# Deploy Qt DLLs only if not already present (never delete existing DLLs or config files)
if (-not (Test-Path "$EXEDIR\Qt6Cored.dll")) {
    Write-Host "[Deploy] Qt DLLs missing -- running windeployqt..."
    & "$QTDIR\bin\windeployqt.exe" --debug --no-translations "$EXEDIR\Mcaster1DSPEncoder_Qt.exe"
    if ($LASTEXITCODE -ne 0) { Write-Host "[Deploy] windeployqt FAILED with error $LASTEXITCODE"; exit $LASTEXITCODE }
    Write-Host "[Deploy] SUCCESS"
} else {
    Write-Host "[Deploy] Qt DLLs already present -- skipping windeployqt"
}

# Deploy vcpkg runtime DLLs if missing
$vcpkgBin = "C:\vcpkg\installed\x64-windows\bin"
$vcpkgDlls = @(
    "FLAC.dll", "fdk-aac.dll", "libmp3lame.dll", "ogg.dll",
    "opus.dll", "opusenc.dll", "portaudio.dll",
    "vorbis.dll", "vorbisenc.dll", "yaml.dll"
)
$missing = $false
foreach ($dll in $vcpkgDlls) {
    if (-not (Test-Path "$EXEDIR\$dll")) { $missing = $true; break }
}
if ($missing) {
    Write-Host "[Deploy] Copying vcpkg runtime DLLs..."
    foreach ($dll in $vcpkgDlls) {
        Copy-Item "$vcpkgBin\$dll" "$EXEDIR\" -Force -ErrorAction SilentlyContinue
    }
    Write-Host "[Deploy] vcpkg DLLs copied"
} else {
    Write-Host "[Deploy] vcpkg DLLs already present -- skipping"
}
