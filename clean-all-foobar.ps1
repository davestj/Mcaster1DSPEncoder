$root = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder'
$msbuild = 'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe'
$sln     = "$root\Mcaster1DSPEncoder_Master.sln"

Write-Host '=== Removing all intermediate and output build dirs ===' -ForegroundColor Cyan

$dirsToRemove = @(
    "$root\build\foobar2000",
    "$root\build\lib",
    "$root\foobar2000\Debug",
    "$root\libmcaster1dspencoder\Debug"
)

# Also remove per-SDK project Debug output dirs (they output to their own vcxproj-relative Debug\)
$sdkDirs = @(
    "$root\external\foobar2000\foobar2000\foobar2000_component_client\Debug",
    "$root\external\foobar2000\foobar2000\helpers\Debug",
    "$root\external\foobar2000\foobar2000\SDK\Debug",
    "$root\external\foobar2000\pfc\Debug",
    # These dirs show up in the compile /Fd flags
    "$root\Debug"   # pfc and sdk_helpers pdb go here
)

foreach ($d in $dirsToRemove + $sdkDirs) {
    if (Test-Path $d) {
        Remove-Item $d -Recurse -Force
        Write-Host "  Removed: $d"
    } else {
        Write-Host "  Already absent: $d"
    }
}

Write-Host ""
Write-Host "=== MSBuild Clean (all 4 foobar-linked projects) ===" -ForegroundColor Cyan

$targets = 'pfc:Clean;foobar2000_SDK:Clean;foobar2000_sdk_helpers:Clean;foobar2000_component_client:Clean;libmcaster1dspencoder:Clean;mcaster1_foobar:Clean'
& $msbuild $sln /t:$targets /p:Configuration=Debug /p:Platform=Win32 /v:minimal /nologo

Write-Host ""
Write-Host "Done. Run rebuild-foobar.ps1 next." -ForegroundColor Green
