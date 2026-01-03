$msbuild = 'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe'
$sln     = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\Mcaster1DSPEncoder_Master.sln'
$log     = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\foobar-build.log'

& $msbuild $sln /t:mcaster1_foobar /p:Configuration=Debug /p:Platform=Win32 /v:normal /nologo 2>&1 |
    Out-File $log -Encoding utf8

$code = $LASTEXITCODE
Write-Host "Exit code: $code"
Write-Host "Log: $log"

# Print just the error lines
Get-Content $log | Select-String -Pattern ' error |FAILED|Cannot open|LNK[0-9]|: error' | ForEach-Object { Write-Host $_ }
