$releaseDir = "C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\AltaCast\src\Release"
$foobarRelease = "C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\AltaCast\Release"

Write-Host "=== Artifacts in src\Release ===" -ForegroundColor Cyan
Get-ChildItem $releaseDir | Where-Object { $_.Extension -in @(".exe",".dll") } |
    Select-Object Name, @{N='KB';E={[int]($_.Length/1KB)}}, LastWriteTime |
    Format-Table -AutoSize

Write-Host "=== Artifacts in AltaCast\Release (foobar) ===" -ForegroundColor Cyan
if (Test-Path $foobarRelease) {
    Get-ChildItem $foobarRelease | Where-Object { $_.Extension -in @(".exe",".dll") } |
        Select-Object Name, @{N='KB';E={[int]($_.Length/1KB)}}, LastWriteTime |
        Format-Table -AutoSize
} else {
    Write-Host "  (not found)"
}
