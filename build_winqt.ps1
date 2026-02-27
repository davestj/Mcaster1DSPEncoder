$ErrorActionPreference = "Continue"

$QTDIR   = "C:\Qt\6.9.3\msvc2022_64"
$MSBUILD = "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
$PROJ    = "C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\win-qt\Mcaster1DSPEncoder_Qt.vcxproj"
$SLNDIR  = "C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\"
$OUTDIR  = "C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\build\win-qt"
$WDEPLOY = "C:\Qt\6.9.3\msvc2022_64\bin\windeployqt.exe"

Write-Host "QTDIR = $QTDIR"
Write-Host ""

$configs = @("Debug", "Release")

foreach ($cfg in $configs) {
    Write-Host "=== Building $cfg|x64 ==="
    & $MSBUILD $PROJ /p:Configuration=$cfg /p:Platform=x64 /p:QTDIR=$QTDIR /p:SolutionDir=$SLNDIR /nologo /m
    if ($LASTEXITCODE -ne 0) {
        Write-Host "BUILD FAILED for $cfg"
        exit $LASTEXITCODE
    }

    $exe = "$OUTDIR\$cfg\Mcaster1DSPEncoder_Qt.exe"
    Write-Host "Deploying Qt DLLs ($cfg)..."
    & $WDEPLOY $exe 2>$null | Out-Null
    Write-Host "Built: $exe"
    Write-Host ""
}

Write-Host "=== All builds succeeded ==="
