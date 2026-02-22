# patch-vcxproj.ps1
# Updates OutDir, IntDir, portaudio/foobar SDK paths, and intermediate file paths
# in all 4 vcxproj files per the reorganization plan.
# Safe to re-run (replacements are idempotent).

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = Split-Path $MyInvocation.MyCommand.Path
$src  = Join-Path $root 'src'

function Patch-File {
    param(
        [string]    $Path,
        [string[][]]$Replacements
    )
    $content = [System.IO.File]::ReadAllText($Path)
    $count   = 0
    foreach ($r in $Replacements) {
        $old = $r[0]; $new = $r[1]
        if ($content.Contains($old)) {
            $content = $content.Replace($old, $new)
            $count++
            Write-Host "  OK: replaced '$old'" -ForegroundColor Green
        } else {
            Write-Host "  SKIP (not found): '$old'" -ForegroundColor Yellow
        }
    }
    [System.IO.File]::WriteAllText($Path, $content, [System.Text.Encoding]::UTF8)
    Write-Host "  -> wrote $Path  ($count replacements)`n"
}

# ===========================================================================
# 1. Mcaster1DSPEncoder.vcxproj
# ===========================================================================
Write-Host '=== Mcaster1DSPEncoder.vcxproj ===' -ForegroundColor Cyan
$f1 = Join-Path $src 'Mcaster1DSPEncoder.vcxproj'
Patch-File $f1 @(
    # OutDir / IntDir
    ,@('<OutDir Condition="''$(Configuration)|$(Platform)''==''Debug|Win32''">.\Debug\</OutDir>',
       '<OutDir Condition="''$(Configuration)|$(Platform)''==''Debug|Win32''">..\Debug\</OutDir>')
    ,@('<IntDir Condition="''$(Configuration)|$(Platform)''==''Debug|Win32''">.\Debug\</IntDir>',
       '<IntDir Condition="''$(Configuration)|$(Platform)''==''Debug|Win32''">..\build\Mcaster1DSPEncoder\Debug\</IntDir>')
    ,@('<OutDir Condition="''$(Configuration)|$(Platform)''==''Release|Win32''">.\Release\</OutDir>',
       '<OutDir Condition="''$(Configuration)|$(Platform)''==''Release|Win32''">..\Release\</OutDir>')
    ,@('<IntDir Condition="''$(Configuration)|$(Platform)''==''Release|Win32''">.\Release\</IntDir>',
       '<IntDir Condition="''$(Configuration)|$(Platform)''==''Release|Win32''">..\build\Mcaster1DSPEncoder\Release\</IntDir>')

    # portaudio include (same in Debug + Release; replace_all via two calls handled automatically)
    ,@('C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\portaudio_src\include',
       '../external/portaudio/src/include')

    # portaudio lib (Debug) + stale lib dirs
    ,@('C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\portaudio_built;C:\vcpkg\installed\x86-windows\lib;../external/lib;libmcaster1dspencoder/Debug;libmcaster1dspencoder_config/Debug;libtranscoder/Debug;icecast/win32/Debugicecast;%(AdditionalLibraryDirectories)',
       '../external/portaudio/built;C:\vcpkg\installed\x86-windows\lib;../external/lib;%(AdditionalLibraryDirectories)')

    # portaudio lib (Release)
    ,@('C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\portaudio_built;C:\vcpkg\installed\x86-windows\lib;../external/lib;%(AdditionalLibraryDirectories)',
       '../external/portaudio/built;C:\vcpkg\installed\x86-windows\lib;../external/lib;%(AdditionalLibraryDirectories)')

    # Intermediate file paths — Debug
    ,@('<TypeLibraryName>.\Debug/MCASTER1DSPENCODER.tlb</TypeLibraryName>',
       '<TypeLibraryName>$(IntDir)MCASTER1DSPENCODER.tlb</TypeLibraryName>')
    ,@('<PrecompiledHeaderOutputFile>.\Debug/MCASTER1DSPENCODER.pch</PrecompiledHeaderOutputFile>',
       '<PrecompiledHeaderOutputFile>$(IntDir)MCASTER1DSPENCODER.pch</PrecompiledHeaderOutputFile>')
    ,@('<AssemblerListingLocation>.\Debug/</AssemblerListingLocation>',
       '<AssemblerListingLocation>$(IntDir)</AssemblerListingLocation>')
    ,@('<ObjectFileName>.\Debug/</ObjectFileName>',
       '<ObjectFileName>$(IntDir)</ObjectFileName>')
    ,@('<ProgramDataBaseFileName>.\Debug/</ProgramDataBaseFileName>',
       '<ProgramDataBaseFileName>$(IntDir)</ProgramDataBaseFileName>')
    ,@('<ProgramDatabaseFile>.\Debug/MCASTER1DSPENCODER.pdb</ProgramDatabaseFile>',
       '<ProgramDatabaseFile>$(OutDir)MCASTER1DSPENCODER.pdb</ProgramDatabaseFile>')
    ,@('<OutputFile>.\Debug/MCASTER1DSPENCODER.bsc</OutputFile>',
       '<OutputFile>$(IntDir)MCASTER1DSPENCODER.bsc</OutputFile>')

    # Intermediate file paths — Release
    ,@('<TypeLibraryName>.\Release/MCASTER1DSPENCODER.tlb</TypeLibraryName>',
       '<TypeLibraryName>$(IntDir)MCASTER1DSPENCODER.tlb</TypeLibraryName>')
    ,@('<PrecompiledHeaderOutputFile>.\Release/MCASTER1DSPENCODER.pch</PrecompiledHeaderOutputFile>',
       '<PrecompiledHeaderOutputFile>$(IntDir)MCASTER1DSPENCODER.pch</PrecompiledHeaderOutputFile>')
    ,@('<AssemblerListingLocation>.\Release/</AssemblerListingLocation>',
       '<AssemblerListingLocation>$(IntDir)</AssemblerListingLocation>')
    ,@('<ObjectFileName>.\Release/</ObjectFileName>',
       '<ObjectFileName>$(IntDir)</ObjectFileName>')
    ,@('<ProgramDataBaseFileName>.\Release/</ProgramDataBaseFileName>',
       '<ProgramDataBaseFileName>$(IntDir)</ProgramDataBaseFileName>')
    ,@('<ProgramDatabaseFile>.\Release/MCASTER1DSPENCODER.pdb</ProgramDatabaseFile>',
       '<ProgramDatabaseFile>$(OutDir)MCASTER1DSPENCODER.pdb</ProgramDatabaseFile>')
    ,@('<OutputFile>.\Release/MCASTER1DSPENCODER.bsc</OutputFile>',
       '<OutputFile>$(IntDir)MCASTER1DSPENCODER.bsc</OutputFile>')
)

# ===========================================================================
# 2. mcaster1_winamp.vcxproj
# ===========================================================================
Write-Host '=== mcaster1_winamp.vcxproj ===' -ForegroundColor Cyan
$f2 = Join-Path $src 'mcaster1_winamp.vcxproj'
Patch-File $f2 @(
    # OutDir / IntDir
    ,@('<OutDir Condition="''$(Configuration)|$(Platform)''==''Release|Win32''">.\Release\</OutDir>',
       '<OutDir Condition="''$(Configuration)|$(Platform)''==''Release|Win32''">..\Winamp_Plugin\Release\</OutDir>')
    ,@('<IntDir Condition="''$(Configuration)|$(Platform)''==''Release|Win32''">.\Release\</IntDir>',
       '<IntDir Condition="''$(Configuration)|$(Platform)''==''Release|Win32''">..\build\Winamp_Plugin\Release\</IntDir>')
    ,@('<OutDir Condition="''$(Configuration)|$(Platform)''==''Debug|Win32''">.\Debug\</OutDir>',
       '<OutDir Condition="''$(Configuration)|$(Platform)''==''Debug|Win32''">..\Winamp_Plugin\Debug\</OutDir>')
    ,@('<IntDir Condition="''$(Configuration)|$(Platform)''==''Debug|Win32''">.\Debug\</IntDir>',
       '<IntDir Condition="''$(Configuration)|$(Platform)''==''Debug|Win32''">..\build\Winamp_Plugin\Debug\</IntDir>')

    # portaudio include (same string in both configs, Replace replaces all occurrences)
    ,@('C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\portaudio_src\include',
       '../external/portaudio/src/include')

    # portaudio lib (both configs have the same lib string)
    ,@('C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\portaudio_built;C:\vcpkg\installed\x86-windows\lib;../external/lib;%(AdditionalLibraryDirectories)',
       '../external/portaudio/built;C:\vcpkg\installed\x86-windows\lib;../external/lib;%(AdditionalLibraryDirectories)')

    # Intermediate file paths — Release
    ,@('<TypeLibraryName>.\Release/mcaster1_winamp.tlb</TypeLibraryName>',
       '<TypeLibraryName>$(IntDir)mcaster1_winamp.tlb</TypeLibraryName>')
    ,@('<PrecompiledHeaderOutputFile>.\Release/mcaster1_winamp.pch</PrecompiledHeaderOutputFile>',
       '<PrecompiledHeaderOutputFile>$(IntDir)mcaster1_winamp.pch</PrecompiledHeaderOutputFile>')
    ,@('<AssemblerListingLocation>.\Release/</AssemblerListingLocation>',
       '<AssemblerListingLocation>$(IntDir)</AssemblerListingLocation>')
    ,@('<ObjectFileName>.\Release/</ObjectFileName>',
       '<ObjectFileName>$(IntDir)</ObjectFileName>')
    ,@('<ProgramDataBaseFileName>.\Release/</ProgramDataBaseFileName>',
       '<ProgramDataBaseFileName>$(IntDir)</ProgramDataBaseFileName>')
    ,@('<ProgramDatabaseFile>.\Release/dsp_mcaster1.pdb</ProgramDatabaseFile>',
       '<ProgramDatabaseFile>$(OutDir)dsp_mcaster1.pdb</ProgramDatabaseFile>')
    ,@('<ImportLibrary>.\Release/dsp_mcaster1.lib</ImportLibrary>',
       '<ImportLibrary>$(IntDir)dsp_mcaster1.lib</ImportLibrary>')
    ,@('<OutputFile>.\Release/mcaster1_winamp.bsc</OutputFile>',
       '<OutputFile>$(IntDir)mcaster1_winamp.bsc</OutputFile>')

    # Intermediate file paths — Debug
    ,@('<TypeLibraryName>.\Debug/mcaster1_winamp.tlb</TypeLibraryName>',
       '<TypeLibraryName>$(IntDir)mcaster1_winamp.tlb</TypeLibraryName>')
    ,@('<PrecompiledHeaderOutputFile>.\Debug/mcaster1_winamp.pch</PrecompiledHeaderOutputFile>',
       '<PrecompiledHeaderOutputFile>$(IntDir)mcaster1_winamp.pch</PrecompiledHeaderOutputFile>')
    ,@('<AssemblerListingLocation>.\Debug/</AssemblerListingLocation>',
       '<AssemblerListingLocation>$(IntDir)</AssemblerListingLocation>')
    ,@('<ObjectFileName>.\Debug/</ObjectFileName>',
       '<ObjectFileName>$(IntDir)</ObjectFileName>')
    ,@('<ProgramDataBaseFileName>.\Debug/</ProgramDataBaseFileName>',
       '<ProgramDataBaseFileName>$(IntDir)</ProgramDataBaseFileName>')
    ,@('<ProgramDatabaseFile>.\Debug/dsp_mcaster1.pdb</ProgramDatabaseFile>',
       '<ProgramDatabaseFile>$(OutDir)dsp_mcaster1.pdb</ProgramDatabaseFile>')
    ,@('<ImportLibrary>.\Debug/dsp_mcaster1.lib</ImportLibrary>',
       '<ImportLibrary>$(IntDir)dsp_mcaster1.lib</ImportLibrary>')
    ,@('<OutputFile>.\Debug/mcaster1_winamp.bsc</OutputFile>',
       '<OutputFile>$(IntDir)mcaster1_winamp.bsc</OutputFile>')
)

# ===========================================================================
# 3. mcaster1_foobar.vcxproj
# ===========================================================================
Write-Host '=== mcaster1_foobar.vcxproj ===' -ForegroundColor Cyan
$f3 = Join-Path $src 'mcaster1_foobar.vcxproj'
Patch-File $f3 @(
    # OutDir / IntDir
    ,@('<OutDir Condition="''$(Configuration)|$(Platform)''==''Debug|Win32''">.\../Debug\</OutDir>',
       '<OutDir Condition="''$(Configuration)|$(Platform)''==''Debug|Win32''">..\foobar2000\Debug\</OutDir>')
    ,@('<IntDir Condition="''$(Configuration)|$(Platform)''==''Debug|Win32''">.\Debug\</IntDir>',
       '<IntDir Condition="''$(Configuration)|$(Platform)''==''Debug|Win32''">..\build\foobar2000\Debug\</IntDir>')
    ,@('<OutDir Condition="''$(Configuration)|$(Platform)''==''Release|Win32''">.\../Release\</OutDir>',
       '<OutDir Condition="''$(Configuration)|$(Platform)''==''Release|Win32''">..\foobar2000\Release\</OutDir>')
    ,@('<IntDir Condition="''$(Configuration)|$(Platform)''==''Release|Win32''">.\Release\</IntDir>',
       '<IntDir Condition="''$(Configuration)|$(Platform)''==''Release|Win32''">..\build\foobar2000\Release\</IntDir>')

    # portaudio include (same in both configs)
    ,@('C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\portaudio_src\include',
       '../external/portaudio/src/include')

    # foobar SDK include (same in both configs)
    ,@('../foobar2000/foobar2000/SDK',
       '../external/foobar2000/foobar2000/SDK')

    # portaudio + foobar lib (same in both configs)
    ,@('C:\Users\dstjohn\dev\00_mcaster1.com\Mcaster1DSPEncoder\portaudio_built;C:\vcpkg\installed\x86-windows\lib;../foobar2000/foobar2000/shared;../external/lib;%(AdditionalLibraryDirectories)',
       '../external/portaudio/built;C:\vcpkg\installed\x86-windows\lib;../external/foobar2000/foobar2000/shared;../external/lib;%(AdditionalLibraryDirectories)')

    # ProjectReferences — foobar SDK
    ,@('..\foobar2000\foobar2000\foobar2000_component_client\foobar2000_component_client.vcxproj',
       '..\external\foobar2000\foobar2000\foobar2000_component_client\foobar2000_component_client.vcxproj')
    ,@('..\foobar2000\foobar2000\helpers\foobar2000_sdk_helpers.vcxproj',
       '..\external\foobar2000\foobar2000\helpers\foobar2000_sdk_helpers.vcxproj')
    ,@('..\foobar2000\foobar2000\SDK\foobar2000_SDK.vcxproj',
       '..\external\foobar2000\foobar2000\SDK\foobar2000_SDK.vcxproj')
    ,@('..\foobar2000\pfc\pfc.vcxproj',
       '..\external\foobar2000\pfc\pfc.vcxproj')

    # Intermediate + output file paths — Debug
    ,@('<TypeLibraryName>.\Debug/mcaster1_foobar.tlb</TypeLibraryName>',
       '<TypeLibraryName>$(IntDir)mcaster1_foobar.tlb</TypeLibraryName>')
    ,@('<PrecompiledHeaderOutputFile>.\Debug/mcaster1_foobar.pch</PrecompiledHeaderOutputFile>',
       '<PrecompiledHeaderOutputFile>$(IntDir)mcaster1_foobar.pch</PrecompiledHeaderOutputFile>')
    ,@('<AssemblerListingLocation>.\Debug/</AssemblerListingLocation>',
       '<AssemblerListingLocation>$(IntDir)</AssemblerListingLocation>')
    ,@('<ObjectFileName>.\Debug/</ObjectFileName>',
       '<ObjectFileName>$(IntDir)</ObjectFileName>')
    ,@('<ProgramDataBaseFileName>.\Debug/</ProgramDataBaseFileName>',
       '<ProgramDataBaseFileName>$(IntDir)</ProgramDataBaseFileName>')
    ,@('<OutputFile>Debug/foo_mcaster1.dll</OutputFile>',
       '<OutputFile>$(OutDir)foo_mcaster1.dll</OutputFile>')
    ,@('<ProgramDatabaseFile>.\Debug/foo_mcaster1.pdb</ProgramDatabaseFile>',
       '<ProgramDatabaseFile>$(OutDir)foo_mcaster1.pdb</ProgramDatabaseFile>')
    ,@('<ImportLibrary>.\Debug/foo_mcaster1.lib</ImportLibrary>',
       '<ImportLibrary>$(IntDir)foo_mcaster1.lib</ImportLibrary>')
    ,@('<OutputFile>.\Debug/mcaster1_foobar.bsc</OutputFile>',
       '<OutputFile>$(IntDir)mcaster1_foobar.bsc</OutputFile>')

    # Intermediate + output file paths — Release
    ,@('<TypeLibraryName>.\../Release/mcaster1_foobar.tlb</TypeLibraryName>',
       '<TypeLibraryName>$(IntDir)mcaster1_foobar.tlb</TypeLibraryName>')
    ,@('<PrecompiledHeaderOutputFile>.\Release/mcaster1_foobar.pch</PrecompiledHeaderOutputFile>',
       '<PrecompiledHeaderOutputFile>$(IntDir)mcaster1_foobar.pch</PrecompiledHeaderOutputFile>')
    ,@('<AssemblerListingLocation>.\Release/</AssemblerListingLocation>',
       '<AssemblerListingLocation>$(IntDir)</AssemblerListingLocation>')
    ,@('<ObjectFileName>.\Release/</ObjectFileName>',
       '<ObjectFileName>$(IntDir)</ObjectFileName>')
    ,@('<ProgramDataBaseFileName>.\Release/</ProgramDataBaseFileName>',
       '<ProgramDataBaseFileName>$(IntDir)</ProgramDataBaseFileName>')
    ,@('<OutputFile>Release/foo_mcaster1.dll</OutputFile>',
       '<OutputFile>$(OutDir)foo_mcaster1.dll</OutputFile>')
    ,@('<ProgramDatabaseFile>.\../Release/foo_mcaster1.pdb</ProgramDatabaseFile>',
       '<ProgramDatabaseFile>$(OutDir)foo_mcaster1.pdb</ProgramDatabaseFile>')
    ,@('<ImportLibrary>.\../Release/foo_mcaster1.lib</ImportLibrary>',
       '<ImportLibrary>$(IntDir)foo_mcaster1.lib</ImportLibrary>')
    ,@('<OutputFile>.\../Release/mcaster1_foobar.bsc</OutputFile>',
       '<OutputFile>$(IntDir)mcaster1_foobar.bsc</OutputFile>')
)

# ===========================================================================
# 4. libmcaster1dspencoder.vcxproj
# ===========================================================================
Write-Host '=== libmcaster1dspencoder.vcxproj ===' -ForegroundColor Cyan
$f4 = Join-Path $src 'libmcaster1dspencoder\libmcaster1dspencoder.vcxproj'
Patch-File $f4 @(
    # OutDir / IntDir
    ,@('<OutDir Condition="''$(Configuration)|$(Platform)''==''Release|Win32''">.\Release\</OutDir>',
       '<OutDir Condition="''$(Configuration)|$(Platform)''==''Release|Win32''">..\..\lib\Release\</OutDir>')
    ,@('<IntDir Condition="''$(Configuration)|$(Platform)''==''Release|Win32''">.\Release\</IntDir>',
       '<IntDir Condition="''$(Configuration)|$(Platform)''==''Release|Win32''">..\..\build\lib\Release\</IntDir>')
    ,@('<OutDir Condition="''$(Configuration)|$(Platform)''==''Debug|Win32''">.\Debug\</OutDir>',
       '<OutDir Condition="''$(Configuration)|$(Platform)''==''Debug|Win32''">..\..\lib\Debug\</OutDir>')
    ,@('<IntDir Condition="''$(Configuration)|$(Platform)''==''Debug|Win32''">.\Debug\</IntDir>',
       '<IntDir Condition="''$(Configuration)|$(Platform)''==''Debug|Win32''">..\..\build\lib\Debug\</IntDir>')

    # Remove duplicate include path in Release config
    ,@('C:\vcpkg\installed\x86-windows\include;../../external/include;../../external/include;%(AdditionalIncludeDirectories)',
       'C:\vcpkg\installed\x86-windows\include;../../external/include;%(AdditionalIncludeDirectories)')

    # Lib output files
    ,@('<OutputFile>.\Release\libmcaster1dspencoder.lib</OutputFile>',
       '<OutputFile>$(OutDir)libmcaster1dspencoder.lib</OutputFile>')
    ,@('<OutputFile>.\Debug\libmcaster1dspencoder.lib</OutputFile>',
       '<OutputFile>$(OutDir)libmcaster1dspencoder.lib</OutputFile>')

    # Intermediate file paths — Release
    ,@('<PrecompiledHeaderOutputFile>.\Release/libmcaster1dspencoder.pch</PrecompiledHeaderOutputFile>',
       '<PrecompiledHeaderOutputFile>$(IntDir)libmcaster1dspencoder.pch</PrecompiledHeaderOutputFile>')
    ,@('<AssemblerListingLocation>.\Release/</AssemblerListingLocation>',
       '<AssemblerListingLocation>$(IntDir)</AssemblerListingLocation>')
    ,@('<ObjectFileName>.\Release/</ObjectFileName>',
       '<ObjectFileName>$(IntDir)</ObjectFileName>')
    ,@('<ProgramDataBaseFileName>.\Release/</ProgramDataBaseFileName>',
       '<ProgramDataBaseFileName>$(IntDir)</ProgramDataBaseFileName>')
    ,@('<OutputFile>.\Release/libmcaster1dspencoder.bsc</OutputFile>',
       '<OutputFile>$(IntDir)libmcaster1dspencoder.bsc</OutputFile>')

    # Intermediate file paths — Debug
    ,@('<PrecompiledHeaderOutputFile>.\Debug/libmcaster1dspencoder.pch</PrecompiledHeaderOutputFile>',
       '<PrecompiledHeaderOutputFile>$(IntDir)libmcaster1dspencoder.pch</PrecompiledHeaderOutputFile>')
    ,@('<AssemblerListingLocation>.\Debug/</AssemblerListingLocation>',
       '<AssemblerListingLocation>$(IntDir)</AssemblerListingLocation>')
    ,@('<ObjectFileName>.\Debug/</ObjectFileName>',
       '<ObjectFileName>$(IntDir)</ObjectFileName>')
    ,@('<ProgramDataBaseFileName>.\Debug/</ProgramDataBaseFileName>',
       '<ProgramDataBaseFileName>$(IntDir)</ProgramDataBaseFileName>')
    ,@('<OutputFile>.\Debug/libmcaster1dspencoder.bsc</OutputFile>',
       '<OutputFile>$(IntDir)libmcaster1dspencoder.bsc</OutputFile>')
)

Write-Host 'All vcxproj files patched successfully.' -ForegroundColor Green
