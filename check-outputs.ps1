$root = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder'

$dirs = @(
    'foobar2000\Debug',
    'foobar2000\Release',
    'libmcaster1dspencoder\Debug',
    'libmcaster1dspencoder\Release',
    'Winamp_Plugin\Debug',
    'Winamp_Plugin\Release',
    'Debug',
    'Release'
)

foreach ($d in $dirs) {
    $full = Join-Path $root $d
    Write-Host "--- $d ---" -ForegroundColor Cyan
    if (Test-Path $full) {
        Get-ChildItem $full | Select-Object Name, @{N='KB';E={[math]::Round($_.Length/1KB,1)}} | Format-Table -AutoSize
    } else {
        Write-Host "  (directory not found)"
    }
}
