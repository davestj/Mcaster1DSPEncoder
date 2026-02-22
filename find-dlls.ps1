$root = "C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder"
$needed = @("BASS.dll","vorbis.dll","ogg.dll","libFLAC.dll","pthreadVSE.dll","libfaac.dll","mfc140.dll","VCRUNTIME140.dll")

Write-Host "=== Searching for dependency DLLs in project tree ===" -ForegroundColor Cyan
Write-Host ""

foreach ($dll in $needed) {
    $found = Get-ChildItem $root -Recurse -Filter $dll -ErrorAction SilentlyContinue |
             Where-Object { $_.FullName -notmatch '\\Release\\|\\Debug\\' } |
             Select-Object -ExpandProperty FullName
    if ($found) {
        foreach ($f in $found) { Write-Host "  FOUND: $f" -ForegroundColor Green }
    } else {
        # also check Release dirs
        $found2 = Get-ChildItem $root -Recurse -Filter $dll -ErrorAction SilentlyContinue |
                  Select-Object -ExpandProperty FullName
        if ($found2) {
            foreach ($f in $found2) { Write-Host "  FOUND (in build dir): $f" -ForegroundColor Yellow }
        } else {
            Write-Host "  MISSING: $dll" -ForegroundColor Red
        }
    }
}

Write-Host ""
Write-Host "=== Checking system (Windows\System32) ===" -ForegroundColor Cyan
$sysRoot = "C:\Windows\System32"
foreach ($dll in @("mfc140.dll","VCRUNTIME140.dll")) {
    $p = Join-Path $sysRoot $dll
    if (Test-Path $p) { Write-Host "  SYSTEM: $p" -ForegroundColor Green }
}

# Also check VS redist
Write-Host ""
Write-Host "=== Checking VS2022 redist ===" -ForegroundColor Cyan
$redist = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Redist\MSVC" -Recurse -Filter "mfc140.dll" -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match "x86" } | Select-Object -First 1 -ExpandProperty FullName
if ($redist) { Write-Host "  $redist" -ForegroundColor Green } else { Write-Host "  Not found in VS redist" }
