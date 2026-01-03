$msbuild = 'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe'
$sln     = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\Mcaster1DSPEncoder_Master.sln'
$log     = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\foobar-build.log'

Write-Host '=== Clean ===' -ForegroundColor Cyan
& $msbuild $sln /t:mcaster1_foobar:Clean /p:Configuration=Debug /p:Platform=Win32 /v:minimal /nologo

Write-Host '=== Build ===' -ForegroundColor Cyan
& $msbuild $sln /t:mcaster1_foobar /p:Configuration=Debug /p:Platform=Win32 /v:normal /nologo 2>&1 |
    Out-File $log -Encoding utf8

$code = $LASTEXITCODE
Write-Host "Exit code: $code"

Get-Content $log | Select-String -Pattern ' error | warning LNK|FAILED|LNK[0-9]' | ForEach-Object { Write-Host $_ }
