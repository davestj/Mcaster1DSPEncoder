$msbuild = 'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe'
$sln     = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\Mcaster1DSPEncoder_Master.sln'
$cfg     = 'Release'
$plat    = 'Win32'

$projects = @(
    @{ Name = 'libmcaster1dspencoder'; Target = 'libmcaster1dspencoder'; Out = 'libmcaster1dspencoder\Release\libmcaster1dspencoder.lib' },
    @{ Name = 'Mcaster1DSPEncoder';    Target = 'Mcaster1DSPEncoder';    Out = 'Release\Mcaster1DSPEncoder.exe' },
    @{ Name = 'mcaster1_winamp';       Target = 'mcaster1_winamp';       Out = 'Winamp_Plugin\Release\dsp_mcaster1.dll' },
    @{ Name = 'mcaster1_foobar';       Target = 'mcaster1_foobar';       Out = 'foobar2000\Release\foo_mcaster1.dll' }
)

$root = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder'
$results = [ordered]@{}

foreach ($proj in $projects) {
    Write-Host ""
    Write-Host "================================================================" -ForegroundColor Cyan
    Write-Host "=== $($proj.Name) : $cfg|$plat ===" -ForegroundColor Cyan
    Write-Host "================================================================" -ForegroundColor Cyan

    & $msbuild $sln /t:$($proj.Target) /p:Configuration=$cfg /p:Platform=$plat /m /v:minimal /nologo
    $ok = ($LASTEXITCODE -eq 0)
    $artifact = Join-Path $root $proj.Out
    $exists = Test-Path $artifact

    if ($ok -and $exists) {
        $size = [math]::Round((Get-Item $artifact).Length / 1KB, 1)
        Write-Host "  OK  $($proj.Out) ($size KB)" -ForegroundColor Green
        $results[$proj.Name] = 'PASS'
    } else {
        Write-Host "  FAILED  exit=$LASTEXITCODE  artifact_exists=$exists" -ForegroundColor Red
        $results[$proj.Name] = 'FAIL'
    }
}

Write-Host ""
Write-Host "================================================================" -ForegroundColor Yellow
Write-Host "=== BUILD SUMMARY ($cfg|$plat) ===" -ForegroundColor Yellow
Write-Host "================================================================" -ForegroundColor Yellow
foreach ($k in $results.Keys) {
    $color = if ($results[$k] -eq 'PASS') { 'Green' } else { 'Red' }
    Write-Host ("  {0,-12} {1}" -f $results[$k], $k) -ForegroundColor $color
}

Write-Host ""
Write-Host "=== Release Output Artifacts ===" -ForegroundColor Cyan
$outDirs = [ordered]@{
    'libmcaster1dspencoder\Release' = '*.lib'
    'Release'                       = '*.exe'
    'Winamp_Plugin\Release'         = '*.dll'
    'foobar2000\Release'            = '*.dll'
}
foreach ($d in $outDirs.Keys) {
    $full = Join-Path $root $d
    Write-Host "  --- $d ---"
    if (Test-Path $full) {
        Get-ChildItem $full -File | Where-Object { $_.Extension -match '\.(exe|dll|lib|pdb)$' } |
            ForEach-Object { Write-Host ("    {0,-35} {1,8} KB" -f $_.Name, [math]::Round($_.Length/1KB,1)) }
    } else {
        Write-Host "    (directory not found)" -ForegroundColor Yellow
    }
}
