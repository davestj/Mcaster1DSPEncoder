$msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
$src = "C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\AltaCast\src"

$solutions = @(
    "Mcaster1DSPEncoder.sln",
    "mcaster1_winamp.sln",
    "mcaster1_foobar.sln",
    "mcaster1_radiodj.sln"
)

$results = @()

foreach ($sln in $solutions) {
    $slnPath = Join-Path $src $sln
    if (-not (Test-Path $slnPath)) {
        Write-Host "SKIP (not found): $sln" -ForegroundColor DarkGray
        $results += [PSCustomObject]@{ Solution=$sln; Result="NOT FOUND" }
        continue
    }
    Write-Host ""
    Write-Host "=== Building: $sln ===" -ForegroundColor Cyan
    & $msbuild $slnPath /p:Configuration=Release /p:Platform=Win32 /m /v:minimal 2>&1
    $code = $LASTEXITCODE
    $status = if ($code -eq 0) { "SUCCESS" } else { "FAILED (exit $code)" }
    $results += [PSCustomObject]@{ Solution=$sln; Result=$status }
    Write-Host "--- $sln : $status ---" -ForegroundColor $(if ($code -eq 0) { "Green" } else { "Red" })
}

Write-Host ""
Write-Host "=== BUILD SUMMARY ===" -ForegroundColor Cyan
foreach ($r in $results) {
    $color = if ($r.Result -eq "SUCCESS") { "Green" } elseif ($r.Result -eq "NOT FOUND") { "DarkGray" } else { "Red" }
    Write-Host "  $($r.Solution.PadRight(35)) $($r.Result)" -ForegroundColor $color
}

# ── Copy dependency DLLs to Release ──────────────────────────────────────────
$anySuccess = $results | Where-Object { $_.Result -eq "SUCCESS" }
if ($anySuccess) {
    Write-Host ""
    Write-Host "=== Copying dependency DLLs to Release\ ===" -ForegroundColor Cyan
    $extLib  = "C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\external\lib"
    $release = Join-Path $src "Release"
    $dlls = @("bass.dll","vorbis.dll","ogg.dll","libFLAC.dll","pthreadVSE.dll","libfaac.dll")
    foreach ($dll in $dlls) {
        $srcDll = Join-Path $extLib $dll
        $dstDll = Join-Path $release $dll
        if (Test-Path $srcDll) {
            Copy-Item $srcDll $dstDll -Force
            Write-Host "  COPIED: $dll" -ForegroundColor Green
        } else {
            Write-Host "  MISSING: $dll" -ForegroundColor Yellow
        }
    }
}
