param(
    [switch]$Clean,
    [switch]$Build,
    [ValidateSet("Release","Debug")]
    [string]$Config = "Debug",
    [ValidateSet("winamp","foobar","all")]
    [string]$Plugin = "all"
)

# Default: Clean + Build if neither flag is specified
if (-not $Clean -and -not $Build) {
    $Clean = $true
    $Build = $true
}

$msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
$sln     = "C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\Mcaster1DSPEncoder_Master.sln"

# Map friendly names to MSBuild project target names (dots/spaces replaced with underscores)
$allTargets = [ordered]@{
    "winamp" = "mcaster1_winamp"
    "foobar" = "mcaster1_foobar"
}

$selected = if ($Plugin -eq "all") { $allTargets.Keys } else { @($Plugin) }

$targets = @()
if ($Clean) { $targets += "Clean" }
if ($Build) { $targets += "Build" }

Write-Host "=== Mcaster1DSPEncoder Plugin Builds ===" -ForegroundColor Cyan
Write-Host "Solution : $sln"
Write-Host "Config   : $Config / Win32"
Write-Host "Plugins  : $($selected -join ', ')"
Write-Host ""

$failed = @()

foreach ($key in $selected) {
    $proj = $allTargets[$key]

    Write-Host "================================================================" -ForegroundColor Cyan
    Write-Host "=== $key ($proj) ===" -ForegroundColor Cyan
    Write-Host "================================================================" -ForegroundColor Cyan

    foreach ($t in $targets) {
        & $msbuild $sln /p:Configuration=$Config /p:Platform=Win32 /t:"$proj`:$t" /m /v:normal
        if ($LASTEXITCODE -ne 0) {
            Write-Host "=== FAILED: $key / $t (exit $LASTEXITCODE) ===" -ForegroundColor Red
            $failed += "$key/$t"
        } else {
            Write-Host "=== OK: $key / $t ===" -ForegroundColor Green
        }
    }
    Write-Host ""
}

Write-Host "================================================================" -ForegroundColor Cyan
if ($failed.Count -eq 0) {
    Write-Host "=== ALL PLUGIN BUILDS SUCCEEDED ===" -ForegroundColor Green
} else {
    Write-Host "=== FAILED: $($failed -join ', ') ===" -ForegroundColor Red
    exit 1
}
