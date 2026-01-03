$exe = "C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\AltaCast\src\Release\MCASTER1DSPENCODER.exe"

# Find dumpbin
$dumpbin = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\MSVC" -Recurse -Filter "dumpbin.exe" |
    Where-Object { $_.FullName -match "Hostx86\\x86" } | Select-Object -First 1 -ExpandProperty FullName

if (-not $dumpbin) {
    $dumpbin = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\MSVC" -Recurse -Filter "dumpbin.exe" |
        Select-Object -First 1 -ExpandProperty FullName
}

Write-Host "Using dumpbin: $dumpbin"
Write-Host ""
Write-Host "=== Dependencies of MCASTER1DSPENCODER.exe ===" -ForegroundColor Cyan
& $dumpbin /DEPENDENTS $exe | Where-Object { $_ -match "\.dll" }
