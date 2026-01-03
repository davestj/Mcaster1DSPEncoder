# build-foobar-verbose.ps1 — runs mcaster1_foobar Debug build and shows errors
$msbuild = 'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe'
$sln     = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\Mcaster1DSPEncoder_Master.sln'

$output = & $msbuild $sln /t:mcaster1_foobar /p:Configuration=Debug /p:Platform=Win32 /m /v:normal /nologo 2>&1

# Show all error/warning lines plus some context
$lines = $output -split "`n"
$errLines = @()
for ($i = 0; $i -lt $lines.Count; $i++) {
    $l = $lines[$i]
    if ($l -match 'error|FAILED|Cannot|Could not|missing|not found|LNK|C\d{4}' ) {
        $errLines += "LINE $($i+1): $l"
    }
}

if ($errLines.Count -gt 0) {
    Write-Host "=== Errors / Warnings ===" -ForegroundColor Red
    $errLines | Select-Object -First 80 | ForEach-Object { Write-Host $_ }
} else {
    Write-Host "No error lines captured — showing last 40 lines of output:" -ForegroundColor Yellow
    $lines | Select-Object -Last 40 | ForEach-Object { Write-Host $_ }
}

Write-Host ""
Write-Host "Exit code: $LASTEXITCODE"
