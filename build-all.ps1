# build-all.ps1 - Full clean+build of all Mcaster1DSPEncoder targets
# Builds: libmcaster1dspencoder, standalone EXE, Winamp plugin, foobar2000 plugin
# Then stages all runtime DLLs (including yaml.dll) into each output directory.
#
# Usage:
#   .\build-all.ps1                         # Release (default)
#   .\build-all.ps1 -Config Debug
#   .\build-all.ps1 -Config Release -NoCopy # skip DLL staging
param(
    [ValidateSet("Release","Debug")]
    [string]$Config = "Release",
    [switch]$NoCopy
)

$root     = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder'
$msbuild  = 'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe'
$sln      = "$root\Mcaster1DSPEncoder_Master.sln"
$platform = 'Win32'

$buildTargets = @(
    [ordered]@{ Name = 'libmcaster1dspencoder'; Out = 'libmcaster1dspencoder\{0}\libmcaster1dspencoder.lib' },
    [ordered]@{ Name = 'Mcaster1DSPEncoder';    Out = '{0}\MCASTER1DSPENCODER.exe' },
    [ordered]@{ Name = 'mcaster1_winamp';       Out = 'Winamp_Plugin\{0}\dsp_mcaster1.dll' },
    [ordered]@{ Name = 'mcaster1_foobar';       Out = 'foobar2000\{0}\foo_mcaster1.dll' }
)

Write-Host ""
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "=== Mcaster1DSPEncoder Full Build: $Config|$platform ===" -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "Solution : $sln"
Write-Host "Config   : $Config / $platform"
Write-Host ""

# -- Step 1: Clean all --------------------------------------------------------
Write-Host "--- Cleaning all targets..." -ForegroundColor Yellow
& $msbuild $sln /p:Configuration=$Config /p:Platform=$platform /t:Clean /m /v:minimal /nologo
if ($LASTEXITCODE -ne 0) {
    Write-Host "Clean failed (exit $LASTEXITCODE)" -ForegroundColor Red
    exit $LASTEXITCODE
}
Write-Host "Clean complete." -ForegroundColor Green
Write-Host ""

# -- Step 2: Build each target in dependency order ----------------------------
$results = [ordered]@{}

foreach ($tgt in $buildTargets) {
    $label  = $tgt.Name
    $outRel = ($tgt.Out -f $Config)
    $artifact = Join-Path $root $outRel

    Write-Host "================================================================" -ForegroundColor Cyan
    Write-Host "=== Building: $label ===" -ForegroundColor Cyan
    Write-Host "================================================================" -ForegroundColor Cyan

    & $msbuild $sln /t:$label /p:Configuration=$Config /p:Platform=$platform /m /v:minimal /nologo
    $exitOk  = ($LASTEXITCODE -eq 0)
    $exists  = Test-Path $artifact

    if ($exitOk -and $exists) {
        $kb = [math]::Round((Get-Item $artifact).Length / 1KB, 1)
        Write-Host "  PASS  $outRel  ($($kb) KB)" -ForegroundColor Green
        $results[$label] = 'PASS'
    } else {
        Write-Host "  FAIL  exit=$LASTEXITCODE  artifact_exists=$exists" -ForegroundColor Red
        $results[$label] = 'FAIL'
    }
    Write-Host ""
}

# -- Step 3: Stage runtime DLLs -----------------------------------------------
if (-not $NoCopy) {
    Write-Host "================================================================" -ForegroundColor Cyan
    Write-Host "=== Staging runtime DLLs: $Config ===" -ForegroundColor Cyan
    Write-Host "================================================================" -ForegroundColor Cyan
    & "$root\copy-dlls.ps1" -Config $Config
    Write-Host ""
}

# -- Step 4: Summary ----------------------------------------------------------
Write-Host "================================================================" -ForegroundColor Yellow
Write-Host "=== BUILD SUMMARY: $Config|$platform ===" -ForegroundColor Yellow
Write-Host "================================================================" -ForegroundColor Yellow

$anyFail = $false
foreach ($k in $results.Keys) {
    $pass = ($results[$k] -eq 'PASS')
    if (-not $pass) { $anyFail = $true }
    $color = if ($pass) { 'Green' } else { 'Red' }
    Write-Host ("  {0,-6} {1}" -f $results[$k], $k) -ForegroundColor $color
}

Write-Host ""
Write-Host "=== Artifacts ===" -ForegroundColor Cyan
$artifactDirs = [ordered]@{
    'MCASTER1DSPENCODER.exe'          = "$root\$Config"
    'dsp_mcaster1.dll'                = "$root\Winamp_Plugin\$Config"
    'foo_mcaster1.dll'                = "$root\foobar2000\$Config"
    'libmcaster1dspencoder.lib'       = "$root\libmcaster1dspencoder\$Config"
}
foreach ($name in $artifactDirs.Keys) {
    $dir  = $artifactDirs[$name]
    $file = Join-Path $dir $name
    if (Test-Path $file) {
        $kb = [math]::Round((Get-Item $file).Length / 1KB, 1)
        Write-Host ("  OK   {0,-40} $($kb) KB" -f $name) -ForegroundColor Green
    } else {
        Write-Host ("  MISS {0}" -f $name) -ForegroundColor Red
    }
}

Write-Host ""
if ($anyFail) {
    Write-Host "=== OVERALL: FAILED ===" -ForegroundColor Red
    exit 1
} else {
    Write-Host "=== OVERALL: ALL TARGETS SUCCEEDED ===" -ForegroundColor Green
    Write-Host ""
    Write-Host "To deploy, run:" -ForegroundColor Cyan
    Write-Host "  .\deploy-winamp.ps1    -Config $Config"
    Write-Host "  .\deploy-wacup.ps1     -Config $Config"
    Write-Host "  .\deploy-radioboss.ps1 -Config $Config"
    Write-Host "  .\deploy-radiodj.ps1   -Config $Config"
    Write-Host "  .\deploy-foobar.ps1    -Config $Config"
}
