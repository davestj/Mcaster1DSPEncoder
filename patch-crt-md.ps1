$root = 'C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder'

# All projects that get linked into foo_mcaster1.dll must use /MD (Release CRT)
# even in Debug, because foobar2000.exe is a Release-CRT host.
# Replace MultiThreadedDebugDLL -> MultiThreadedDLL in these projects.

$files = @(
    "$root\src\libmcaster1dspencoder\libmcaster1dspencoder.vcxproj",
    "$root\external\foobar2000\foobar2000\foobar2000_component_client\foobar2000_component_client.vcxproj",
    "$root\external\foobar2000\foobar2000\helpers\foobar2000_sdk_helpers.vcxproj",
    "$root\external\foobar2000\foobar2000\SDK\foobar2000_SDK.vcxproj",
    "$root\external\foobar2000\pfc\pfc.vcxproj"
)

foreach ($f in $files) {
    $content = [System.IO.File]::ReadAllText($f, [System.Text.Encoding]::UTF8)
    $before = ($content -split 'MultiThreadedDebugDLL').Count - 1
    $content = $content.Replace('MultiThreadedDebugDLL', 'MultiThreadedDLL')
    [System.IO.File]::WriteAllText($f, $content, [System.Text.Encoding]::UTF8)
    Write-Host ("{0}: {1} replacements" -f [System.IO.Path]::GetFileName($f), $before)
}

Write-Host ""
Write-Host "Done. Also removing BasicRuntimeChecks from libmcaster1dspencoder (incompatible with /MD)..."
# Already removed in a previous edit, but double-check
$libf = "$root\src\libmcaster1dspencoder\libmcaster1dspencoder.vcxproj"
$content = [System.IO.File]::ReadAllText($libf, [System.Text.Encoding]::UTF8)
if ($content -match 'BasicRuntimeChecks') {
    $content = $content -replace '<BasicRuntimeChecks>[^<]*</BasicRuntimeChecks>\s*', ''
    [System.IO.File]::WriteAllText($libf, $content, [System.Text.Encoding]::UTF8)
    Write-Host "  Removed BasicRuntimeChecks from libmcaster1dspencoder.vcxproj"
} else {
    Write-Host "  BasicRuntimeChecks already absent - OK"
}
