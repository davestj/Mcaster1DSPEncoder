$root = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder'

# All projects that get linked into foo_mcaster1.dll must NOT define _DEBUG
# when using /MD (Release CRT), because MFC headers inject mfc140d.lib/msvcrtd.lib
# when they see _DEBUG + _AFXDLL — causing CRT mismatch with portaudio (which is /MD).
# Replace _DEBUG -> NDEBUG in ALL occurrences (Debug configs only have _DEBUG;
# Release configs already use NDEBUG, so this is safe).

$files = @(
    "$root\src\mcaster1_foobar.vcxproj",
    "$root\src\libmcaster1dspencoder\libmcaster1dspencoder.vcxproj",
    "$root\external\foobar2000\foobar2000\foobar2000_component_client\foobar2000_component_client.vcxproj",
    "$root\external\foobar2000\foobar2000\helpers\foobar2000_sdk_helpers.vcxproj",
    "$root\external\foobar2000\foobar2000\SDK\foobar2000_SDK.vcxproj",
    "$root\external\foobar2000\pfc\pfc.vcxproj"
)

foreach ($f in $files) {
    $content = [System.IO.File]::ReadAllText($f, [System.Text.Encoding]::UTF8)
    $count = ($content -split '_DEBUG').Count - 1
    # Replace _DEBUG; -> NDEBUG; (in define lists)
    $content = $content.Replace('_DEBUG;', 'NDEBUG;')
    # Also catch standalone _DEBUG (end of list without semicolon, unlikely but safe)
    $content = $content.Replace('>_DEBUG<', '>NDEBUG<')
    [System.IO.File]::WriteAllText($f, $content, [System.Text.Encoding]::UTF8)
    $fname = [System.IO.Path]::GetFileName($f)
    Write-Host ("  {0}: {1} _DEBUG occurrences replaced" -f $fname, $count)
}

Write-Host ""
Write-Host "Verify: grep remaining _DEBUG in patched files:"
foreach ($f in $files) {
    $content = [System.IO.File]::ReadAllText($f, [System.Text.Encoding]::UTF8)
    $remain = ($content -split '_DEBUG').Count - 1
    $fname = [System.IO.Path]::GetFileName($f)
    if ($remain -gt 0) {
        Write-Host ("  WARNING: {0} still has {1} _DEBUG occurrences" -f $fname, $remain) -ForegroundColor Yellow
    } else {
        Write-Host ("  OK: {0} - no _DEBUG remaining" -f $fname) -ForegroundColor Green
    }
}
