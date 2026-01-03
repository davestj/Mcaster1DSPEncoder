$msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
$sln = "C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\AltaCast\src\Mcaster1DSPEncoder.sln"

Write-Host "=== Building Mcaster1DSPEncoder.sln (Release/Win32) ===" -ForegroundColor Cyan
Write-Host ""

& $msbuild $sln /p:Configuration=Release /p:Platform=Win32 /m /v:minimal

Write-Host ""
Write-Host "=== Build complete. Exit code: $LASTEXITCODE ===" -ForegroundColor Cyan
