# patch-vcxproj-2.ps1
# Fix libmcaster1dspencoder output path and add lib search dirs to consuming projects.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = Split-Path $MyInvocation.MyCommand.Path
$src  = Join-Path $root 'src'

function Patch-File {
    param([string]$Path, [string[][]]$Replacements)
    $content = [System.IO.File]::ReadAllText($Path)
    $count   = 0
    foreach ($r in $Replacements) {
        $old = $r[0]; $new = $r[1]
        if ($content.Contains($old)) {
            $content = $content.Replace($old, $new)
            $count++
            Write-Host "  OK: $($old.Substring(0,[Math]::Min(80,$old.Length)).Trim())" -ForegroundColor Green
        } else {
            Write-Host "  SKIP (not found): $($old.Substring(0,[Math]::Min(80,$old.Length)).Trim())" -ForegroundColor Yellow
        }
    }
    [System.IO.File]::WriteAllText($Path, $content, [System.Text.Encoding]::UTF8)
    Write-Host "  -> wrote $Path  ($count replacements)`n"
}

# ===========================================================================
# 1. libmcaster1dspencoder.vcxproj — fix OutDir to libmcaster1dspencoder\
# ===========================================================================
Write-Host '=== libmcaster1dspencoder.vcxproj ===' -ForegroundColor Cyan
$f4 = Join-Path $src 'libmcaster1dspencoder\libmcaster1dspencoder.vcxproj'
Patch-File $f4 @(
    ,@('<OutDir Condition="''$(Configuration)|$(Platform)''==''Release|Win32''">..\..\lib\Release\</OutDir>',
       '<OutDir Condition="''$(Configuration)|$(Platform)''==''Release|Win32''">..\..\libmcaster1dspencoder\Release\</OutDir>')
    ,@('<OutDir Condition="''$(Configuration)|$(Platform)''==''Debug|Win32''">..\..\lib\Debug\</OutDir>',
       '<OutDir Condition="''$(Configuration)|$(Platform)''==''Debug|Win32''">..\..\libmcaster1dspencoder\Debug\</OutDir>')
)

# ===========================================================================
# Helper: the libdir strings for consuming projects (relative to src\)
# Debug adds ../libmcaster1dspencoder/Debug;
# Release adds ../libmcaster1dspencoder/Release;
# Discriminator: IgnoreSpecificDefaultLibraries differs between Debug/Release
# ===========================================================================

# ===========================================================================
# 2. Mcaster1DSPEncoder.vcxproj
# ===========================================================================
Write-Host '=== Mcaster1DSPEncoder.vcxproj ===' -ForegroundColor Cyan
$f1 = Join-Path $src 'Mcaster1DSPEncoder.vcxproj'
Patch-File $f1 @(
    # Debug link section — LIBCMTD.lib is unique to Debug
    ,@('<AdditionalLibraryDirectories>../external/portaudio/built;C:\vcpkg\installed\x86-windows\lib;../external/lib;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
      <IgnoreSpecificDefaultLibraries>LIBCMTD.lib;%(IgnoreSpecificDefaultLibraries)</IgnoreSpecificDefaultLibraries>',
       '<AdditionalLibraryDirectories>../libmcaster1dspencoder/Debug;../external/portaudio/built;C:\vcpkg\installed\x86-windows\lib;../external/lib;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
      <IgnoreSpecificDefaultLibraries>LIBCMTD.lib;%(IgnoreSpecificDefaultLibraries)</IgnoreSpecificDefaultLibraries>')

    # Release link section — LIBCMT.lib (no D) is unique to Release
    ,@('<AdditionalLibraryDirectories>../external/portaudio/built;C:\vcpkg\installed\x86-windows\lib;../external/lib;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
      <IgnoreSpecificDefaultLibraries>LIBCMT.lib;%(IgnoreSpecificDefaultLibraries)</IgnoreSpecificDefaultLibraries>',
       '<AdditionalLibraryDirectories>../libmcaster1dspencoder/Release;../external/portaudio/built;C:\vcpkg\installed\x86-windows\lib;../external/lib;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
      <IgnoreSpecificDefaultLibraries>LIBCMT.lib;%(IgnoreSpecificDefaultLibraries)</IgnoreSpecificDefaultLibraries>')
)

# ===========================================================================
# 3. mcaster1_winamp.vcxproj
# ===========================================================================
Write-Host '=== mcaster1_winamp.vcxproj ===' -ForegroundColor Cyan
$f2 = Join-Path $src 'mcaster1_winamp.vcxproj'
Patch-File $f2 @(
    # Debug link section — libcmtd is unique to Debug
    ,@('<AdditionalLibraryDirectories>../external/portaudio/built;C:\vcpkg\installed\x86-windows\lib;../external/lib;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
      <IgnoreSpecificDefaultLibraries>libcmtd;%(IgnoreSpecificDefaultLibraries)</IgnoreSpecificDefaultLibraries>',
       '<AdditionalLibraryDirectories>../libmcaster1dspencoder/Debug;../external/portaudio/built;C:\vcpkg\installed\x86-windows\lib;../external/lib;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
      <IgnoreSpecificDefaultLibraries>libcmtd;%(IgnoreSpecificDefaultLibraries)</IgnoreSpecificDefaultLibraries>')

    # Release link section — libcmt (no d) is unique to Release
    ,@('<AdditionalLibraryDirectories>../external/portaudio/built;C:\vcpkg\installed\x86-windows\lib;../external/lib;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
      <IgnoreSpecificDefaultLibraries>libcmt;%(IgnoreSpecificDefaultLibraries)</IgnoreSpecificDefaultLibraries>',
       '<AdditionalLibraryDirectories>../libmcaster1dspencoder/Release;../external/portaudio/built;C:\vcpkg\installed\x86-windows\lib;../external/lib;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
      <IgnoreSpecificDefaultLibraries>libcmt;%(IgnoreSpecificDefaultLibraries)</IgnoreSpecificDefaultLibraries>')
)

# ===========================================================================
# 4. mcaster1_foobar.vcxproj
# ===========================================================================
Write-Host '=== mcaster1_foobar.vcxproj ===' -ForegroundColor Cyan
$f3 = Join-Path $src 'mcaster1_foobar.vcxproj'
Patch-File $f3 @(
    # Debug link section — GenerateDebugInformation is unique to Debug
    ,@('<AdditionalLibraryDirectories>../external/portaudio/built;C:\vcpkg\installed\x86-windows\lib;../external/foobar2000/foobar2000/shared;../external/lib;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
      <IgnoreSpecificDefaultLibraries>%(IgnoreSpecificDefaultLibraries)</IgnoreSpecificDefaultLibraries>
      <DelayLoadDLLs>%(DelayLoadDLLs)</DelayLoadDLLs>
      <GenerateDebugInformation>true</GenerateDebugInformation>',
       '<AdditionalLibraryDirectories>../libmcaster1dspencoder/Debug;../external/portaudio/built;C:\vcpkg\installed\x86-windows\lib;../external/foobar2000/foobar2000/shared;../external/lib;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
      <IgnoreSpecificDefaultLibraries>%(IgnoreSpecificDefaultLibraries)</IgnoreSpecificDefaultLibraries>
      <DelayLoadDLLs>%(DelayLoadDLLs)</DelayLoadDLLs>
      <GenerateDebugInformation>true</GenerateDebugInformation>')

    # Release link section — ProgramDatabaseFile follows DelayLoad (no GenerateDebugInformation)
    ,@('<AdditionalLibraryDirectories>../external/portaudio/built;C:\vcpkg\installed\x86-windows\lib;../external/foobar2000/foobar2000/shared;../external/lib;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
      <IgnoreSpecificDefaultLibraries>%(IgnoreSpecificDefaultLibraries)</IgnoreSpecificDefaultLibraries>
      <DelayLoadDLLs>%(DelayLoadDLLs)</DelayLoadDLLs>
      <ProgramDatabaseFile>$(OutDir)foo_mcaster1.pdb</ProgramDatabaseFile>',
       '<AdditionalLibraryDirectories>../libmcaster1dspencoder/Release;../external/portaudio/built;C:\vcpkg\installed\x86-windows\lib;../external/foobar2000/foobar2000/shared;../external/lib;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
      <IgnoreSpecificDefaultLibraries>%(IgnoreSpecificDefaultLibraries)</IgnoreSpecificDefaultLibraries>
      <DelayLoadDLLs>%(DelayLoadDLLs)</DelayLoadDLLs>
      <ProgramDatabaseFile>$(OutDir)foo_mcaster1.pdb</ProgramDatabaseFile>')
)

# ===========================================================================
# Create output directories at repo root
# ===========================================================================
$dirs = @(
    'libmcaster1dspencoder\Release',
    'libmcaster1dspencoder\Debug',
    'Winamp_Plugin\Release',
    'Winamp_Plugin\Debug',
    'foobar2000\Release',
    'foobar2000\Debug',
    'Release',
    'Debug',
    'build'
)
Write-Host '=== Creating output directories ===' -ForegroundColor Cyan
foreach ($d in $dirs) {
    $full = Join-Path $root $d
    if (-not (Test-Path $full)) {
        New-Item -ItemType Directory -Path $full -Force | Out-Null
        Write-Host "  Created: $d" -ForegroundColor Green
    } else {
        Write-Host "  Exists:  $d" -ForegroundColor DarkGray
    }
}

Write-Host 'Patch 2 complete.' -ForegroundColor Green
