param(
    [switch]$Clean,
    [switch]$Build,
    [ValidateSet("Release","Debug")]
    [string]$Config = "Debug"
)

# Default: Clean + Build if neither flag is specified
if (-not $Clean -and -not $Build) {
    $Clean = $true
    $Build = $true
}

$msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
$sln     = "C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\Mcaster1DSPEncoder_Master.sln"

$targets = @()
if ($Clean) { $targets += "Clean" }
if ($Build) { $targets += "Build" }
$targetArg = $targets -join ","

Write-Host "=== Mcaster1DSPEncoder Standalone ===" -ForegroundColor Cyan
Write-Host "Solution : $sln"
Write-Host "Config   : $Config / Win32"
Write-Host "Targets  : $targetArg"
Write-Host ""

& $msbuild $sln /p:Configuration=$Config /p:Platform=Win32 /t:$targetArg /m /v:normal

Write-Host ""
if ($LASTEXITCODE -eq 0) {
    Write-Host "=== BUILD SUCCEEDED ===" -ForegroundColor Green
} else {
    Write-Host "=== BUILD FAILED (exit $LASTEXITCODE) ===" -ForegroundColor Red
    exit $LASTEXITCODE
}
