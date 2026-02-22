# run-builds-debug.ps1
# Builds all 4 projects in Debug|Win32 using the master solution.
# Build order: lib -> standalone -> winamp -> foobar (foobar also builds its SDK deps)

$msbuild = 'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe'
$sln     = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\Mcaster1DSPEncoder_Master.sln'
$cfg     = 'Debug'
$plat    = 'Win32'

$results = [ordered]@{}

function Build-Project {
    param([string]$Label, [string]$Target)
    Write-Host ""
    Write-Host "================================================================" -ForegroundColor Cyan
    Write-Host "=== $Label : $cfg|$plat ===" -ForegroundColor Cyan
    Write-Host "================================================================" -ForegroundColor Cyan

    & $msbuild $sln /t:$Target /p:Configuration=$cfg /p:Platform=$plat /m /v:minimal /nologo
    $ok = ($LASTEXITCODE -eq 0)
    if ($ok) { Write-Host "RESULT: OK" -ForegroundColor Green }
    else      { Write-Host "RESULT: FAILED (exit $LASTEXITCODE)" -ForegroundColor Red }
    return $ok
}

$results['libmcaster1dspencoder'] = Build-Project 'libmcaster1dspencoder' 'libmcaster1dspencoder'
$results['Mcaster1DSPEncoder']    = Build-Project 'Mcaster1DSPEncoder (standalone)' 'Mcaster1DSPEncoder'
$results['mcaster1_winamp']       = Build-Project 'mcaster1_winamp' 'mcaster1_winamp'
$results['mcaster1_foobar']       = Build-Project 'mcaster1_foobar' 'mcaster1_foobar'

Write-Host ""
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "=== BUILD SUMMARY ($cfg|$plat) ===" -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan

$allOk = $true
foreach ($k in $results.Keys) {
    if ($results[$k]) { Write-Host "  OK      $k" -ForegroundColor Green }
    else              { Write-Host "  FAILED  $k" -ForegroundColor Red; $allOk = $false }
}

Write-Host ""

# Show what landed in each output dir
$dirs = @{
    'libmcaster1dspencoder' = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\libmcaster1dspencoder\Debug'
    'Standalone'            = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\Debug'
    'Winamp_Plugin'         = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\Winamp_Plugin\Debug'
    'foobar2000'            = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\foobar2000\Debug'
}
Write-Host "=== Output artifacts ===" -ForegroundColor Cyan
foreach ($label in $dirs.Keys) {
    $d = $dirs[$label]
    if (Test-Path $d) {
        $items = Get-ChildItem $d -File | Where-Object { $_.Extension -match '\.(exe|dll|lib|pdb)$' }
        Write-Host "  $label :"
        if ($items) { $items | ForEach-Object { Write-Host "    $($_.Name)  $([int]($_.Length/1KB)) KB" } }
        else         { Write-Host "    (empty)" -ForegroundColor Yellow }
    } else {
        Write-Host "  $label : dir not found ($d)" -ForegroundColor Yellow
    }
}

Write-Host ""
if ($allOk) { Write-Host "ALL BUILDS SUCCEEDED" -ForegroundColor Green }
else         { Write-Host "ONE OR MORE BUILDS FAILED" -ForegroundColor Red; exit 1 }
