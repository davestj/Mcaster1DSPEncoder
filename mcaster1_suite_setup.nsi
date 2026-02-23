; =============================================================================
; Mcaster1 DSP Encoder — Suite Installer
;
; Lets the user choose any combination of:
;   - Standalone Encoder (MCASTER1DSPENCODER.exe → Program Files)
;   - Winamp DSP Plugin  (dsp_mcaster1.dll → Winamp\Plugins\)
;   - WACUP DSP Plugin   (dsp_mcaster1.dll → WACUP\Plugins\)
;   - foobar2000 Component (foo_mcaster1.dll → foobar2000\components\)
;   - RadioDJ v1 Plugin  (dsp_mcaster1.dll → RadioDJ root)
;   - RadioBoss Plugin   (dsp_mcaster1.dll → RadioBoss root)
;
; Pre-build:  .\generate_version.ps1
; Compile:    makensis mcaster1_suite_setup.nsi
; Output:     mcaster1_suite_<version>_setup.exe
;
; NOTE: Section !define symbols (${SEC_*}) are created at the point of each
; Section declaration.  All functions that call ${SectionIsSelected} etc.
; must therefore appear AFTER the Section blocks — placed below in this file.
; =============================================================================

Unicode True
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "nsDialogs.nsh"
!include "LogicLib.nsh"
!include "Sections.nsh"
!include "version.nsh"

; ---------------------------------------------------------------------------
; Branding
; ---------------------------------------------------------------------------
!define APP_NAME      "Mcaster1 DSP Encoder Suite"
!define COMPANY_NAME  "Mcaster1"
!define PUBLISHER     "David St. John"
!define PRODUCT_URL   "https://mcaster1.com/encoder.php"
!define UNINST_KEY    "Software\Microsoft\Windows\CurrentVersion\Uninstall\Mcaster1DSP_Suite"
!define UNINST_ROOT   "HKLM"
!define SUITE_REG     "Software\Mcaster1\Suite"

!define SRC_WIN       "Winamp_Plugin\Release"
!define SRC_FB        "foobar2000\Release"
!define SRC_SA        "Release"

!define ICON          "src\mcaster1.ico"
!define SPLASH        "mcaster1-dspencoder_splash_installer.bmp"
!define SIDEBAR       "mcaster1-dspencoder_sidebar.bmp"
!define HEADER        "mcaster1-authority-dspencoder_header-banner-.png"

; ---------------------------------------------------------------------------
; Installer metadata
; ---------------------------------------------------------------------------
Name              "${APP_NAME} ${VERSION}"
OutFile           "mcaster1_suite_${VERSION}_setup.exe"
InstallDir        "$PROGRAMFILES\Mcaster1\Mcaster1DSPEncoder"
RequestExecutionLevel admin
ShowInstDetails   show
ShowUnInstDetails show

; ---------------------------------------------------------------------------
; Per-component path variables
; ---------------------------------------------------------------------------
Var PathStandalone
Var PathWinamp
Var PathWACUP
Var PathFoobar
Var PathRadioDJ
Var PathRadioBoss

; DirRequest HWND handles for the plugin paths page
Var DirWinamp
Var DirWACUP
Var DirFoobar
Var DirRadioDJ
Var DirRadioBoss

; ---------------------------------------------------------------------------
; MUI2 appearance
; ---------------------------------------------------------------------------
!define MUI_ABORTWARNING
!define MUI_ICON                       "${ICON}"
!define MUI_UNICON                     "${ICON}"
!define MUI_WELCOMEFINISHPAGE_BITMAP   "${SIDEBAR}"
!define MUI_HEADER_IMAGE
!define MUI_HEADER_IMAGE_BITMAP        "${HEADER}"
!define MUI_HEADER_IMAGE_STRETCH
!define MUI_FINISHPAGE_LINK            "Visit mcaster1.com/encoder.php"
!define MUI_FINISHPAGE_LINK_LOCATION   "${PRODUCT_URL}"

; ---------------------------------------------------------------------------
; Pages
; ---------------------------------------------------------------------------
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE    "LICENSE"
!insertmacro MUI_PAGE_COMPONENTS

; Directory page for standalone — conditional (see StandaloneDirPre below)
!define MUI_PAGE_CUSTOMFUNCTION_PRE   StandaloneDirPre
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE StandaloneDirLeave
!insertmacro MUI_PAGE_DIRECTORY

; Plugin paths page — conditional (see PagePluginPaths below)
Page custom PagePluginPaths PagePluginPathsLeave

!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

; ---------------------------------------------------------------------------
; .onInit — splash + auto-detect all paths
; (placed before sections; does not reference ${SEC_*} defines)
; ---------------------------------------------------------------------------
Function .onInit
    InitPluginsDir
    File "/oname=$PLUGINSDIR\splash.bmp" "${SPLASH}"
    advsplash::show 2500 600 400 -1 "$PLUGINSDIR\splash"
    Pop $0
    Delete "$PLUGINSDIR\splash.bmp"

    StrCpy $PathStandalone "$PROGRAMFILES\Mcaster1\Mcaster1DSPEncoder"

    ; Winamp
    StrCpy $PathWinamp ""
    ReadRegStr $PathWinamp HKLM "SOFTWARE\WOW6432Node\Winamp" ""
    ${If} $PathWinamp == ""
        ReadRegStr $PathWinamp HKLM "SOFTWARE\Winamp" ""
    ${EndIf}
    ${If} $PathWinamp == ""
        StrCpy $PathWinamp "$PROGRAMFILES\Winamp"
    ${EndIf}

    ; WACUP
    StrCpy $PathWACUP ""
    ReadRegStr $PathWACUP HKLM "SOFTWARE\WOW6432Node\WACUP" "InstallPath"
    ${If} $PathWACUP == ""
        ReadRegStr $PathWACUP HKLM "SOFTWARE\WOW6432Node\WACUP" ""
    ${EndIf}
    ${If} $PathWACUP == ""
        ReadRegStr $PathWACUP HKLM "SOFTWARE\WACUP" "InstallPath"
    ${EndIf}
    ${If} $PathWACUP == ""
        ReadRegStr $PathWACUP HKCU "SOFTWARE\WACUP" "InstallPath"
    ${EndIf}
    ${If} $PathWACUP == ""
        StrCpy $PathWACUP "$PROGRAMFILES\WACUP"
    ${EndIf}

    ; foobar2000
    StrCpy $PathFoobar ""
    ReadRegStr $PathFoobar HKLM \
        "SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\foobar2000.exe" "Path"
    ${If} $PathFoobar == ""
        ReadRegStr $PathFoobar HKLM "SOFTWARE\WOW6432Node\foobar2000" "Location"
    ${EndIf}
    ${If} $PathFoobar == ""
        ReadRegStr $PathFoobar HKLM "SOFTWARE\foobar2000" "Location"
    ${EndIf}
    ${If} $PathFoobar == ""
        StrCpy $PathFoobar "$PROGRAMFILES\foobar2000"
    ${EndIf}

    ; RadioDJ v1
    StrCpy $PathRadioDJ ""
    ReadRegStr $PathRadioDJ HKLM "SOFTWARE\WOW6432Node\RadioDJ" "InstallPath"
    ${If} $PathRadioDJ == ""
        ReadRegStr $PathRadioDJ HKLM "SOFTWARE\RadioDJ" "InstallPath"
    ${EndIf}
    ${If} $PathRadioDJ == ""
        IfFileExists "C:\RadioDJ\RadioDJ.exe" 0 +2
            StrCpy $PathRadioDJ "C:\RadioDJ"
    ${EndIf}
    ${If} $PathRadioDJ == ""
        IfFileExists "C:\RadioDJv1\RadioDJ.exe" 0 +2
            StrCpy $PathRadioDJ "C:\RadioDJv1"
    ${EndIf}
    ${If} $PathRadioDJ == ""
        IfFileExists "$PROGRAMFILES\RadioDJ\RadioDJ.exe" 0 +2
            StrCpy $PathRadioDJ "$PROGRAMFILES\RadioDJ"
    ${EndIf}
    ${If} $PathRadioDJ == ""
        StrCpy $PathRadioDJ "C:\RadioDJ"
    ${EndIf}

    ; RadioBoss
    StrCpy $PathRadioBoss "$PROGRAMFILES\RadioBoss"
FunctionEnd

; ===========================================================================
; SECTIONS  (${SEC_*} defines are created here — functions below can use them)
; ===========================================================================

Section "Standalone Encoder" SEC_STANDALONE
    SetOutPath "$PathStandalone"
    File "${SRC_SA}\MCASTER1DSPENCODER.exe"
    File "${SRC_SA}\fdk-aac.dll"
    File "${SRC_SA}\iconv.dll"
    File "${SRC_SA}\libcurl.dll"
    File "${SRC_SA}\libFLAC.dll"
    File "${SRC_SA}\libmp3lame.DLL"
    File "${SRC_SA}\ogg.dll"
    File "${SRC_SA}\opus.dll"
    File "${SRC_SA}\opusenc.dll"
    File "${SRC_SA}\pthreadVSE.dll"
    File "${SRC_SA}\vorbis.dll"
    File "${SRC_SA}\vorbisenc.dll"
    File "${SRC_SA}\yaml.dll"
    WriteRegStr HKLM "${SUITE_REG}" "StandalonePath" "$PathStandalone"
SectionEnd

SectionGroup /e "DSP Plugins" SecPluginsGroup

Section "Winamp Plugin" SEC_WINAMP
    CreateDirectory "$PathWinamp\Plugins"
    SetOutPath "$PathWinamp\Plugins"
    File "${SRC_WIN}\dsp_mcaster1.dll"
    SetOutPath "$PathWinamp"
    File "${SRC_WIN}\fdk-aac.dll"
    File "${SRC_WIN}\iconv.dll"
    File "${SRC_WIN}\libcurl.dll"
    File "${SRC_WIN}\libFLAC.dll"
    File "${SRC_WIN}\libmp3lame.DLL"
    File "${SRC_WIN}\ogg.dll"
    File "${SRC_WIN}\opus.dll"
    File "${SRC_WIN}\opusenc.dll"
    File "${SRC_WIN}\pthreadVSE.dll"
    File "${SRC_WIN}\vorbis.dll"
    File "${SRC_WIN}\vorbisenc.dll"
    File "${SRC_WIN}\yaml.dll"
    WriteRegStr HKLM "${SUITE_REG}" "WinampPath" "$PathWinamp"
SectionEnd

Section "WACUP Plugin" SEC_WACUP
    CreateDirectory "$PathWACUP\Plugins"
    SetOutPath "$PathWACUP\Plugins"
    File "${SRC_WIN}\dsp_mcaster1.dll"
    SetOutPath "$PathWACUP"
    File "${SRC_WIN}\fdk-aac.dll"
    File "${SRC_WIN}\iconv.dll"
    File "${SRC_WIN}\libcurl.dll"
    File "${SRC_WIN}\libFLAC.dll"
    File "${SRC_WIN}\libmp3lame.DLL"
    File "${SRC_WIN}\ogg.dll"
    File "${SRC_WIN}\opus.dll"
    File "${SRC_WIN}\opusenc.dll"
    File "${SRC_WIN}\pthreadVSE.dll"
    File "${SRC_WIN}\vorbis.dll"
    File "${SRC_WIN}\vorbisenc.dll"
    File "${SRC_WIN}\yaml.dll"
    WriteRegStr HKLM "${SUITE_REG}" "WACUPPath" "$PathWACUP"
SectionEnd

Section "foobar2000 Component" SEC_FOOBAR
    CreateDirectory "$PathFoobar\components"
    SetOutPath "$PathFoobar\components"
    File "${SRC_FB}\foo_mcaster1.dll"
    SetOutPath "$PathFoobar"
    File "${SRC_FB}\fdk-aac.dll"
    File "${SRC_FB}\iconv.dll"
    File "${SRC_FB}\libcurl.dll"
    File "${SRC_FB}\libFLAC.dll"
    File "${SRC_FB}\libmp3lame.DLL"
    File "${SRC_FB}\ogg.dll"
    File "${SRC_FB}\opus.dll"
    File "${SRC_FB}\opusenc.dll"
    File "${SRC_FB}\pthreadVSE.dll"
    File "${SRC_FB}\vorbis.dll"
    File "${SRC_FB}\vorbisenc.dll"
    File "${SRC_FB}\yaml.dll"
    WriteRegStr HKLM "${SUITE_REG}" "FoobarPath" "$PathFoobar"
SectionEnd

Section "RadioDJ v1 Plugin" SEC_RADIODJ
    SetOutPath "$PathRadioDJ"
    File "${SRC_WIN}\dsp_mcaster1.dll"
    File "${SRC_WIN}\fdk-aac.dll"
    File "${SRC_WIN}\iconv.dll"
    File "${SRC_WIN}\libcurl.dll"
    File "${SRC_WIN}\libFLAC.dll"
    File "${SRC_WIN}\libmp3lame.DLL"
    File "${SRC_WIN}\ogg.dll"
    File "${SRC_WIN}\opus.dll"
    File "${SRC_WIN}\opusenc.dll"
    File "${SRC_WIN}\pthreadVSE.dll"
    File "${SRC_WIN}\vorbis.dll"
    File "${SRC_WIN}\vorbisenc.dll"
    File "${SRC_WIN}\yaml.dll"
    WriteRegStr HKLM "${SUITE_REG}" "RadioDJPath" "$PathRadioDJ"
SectionEnd

Section "RadioBoss Plugin" SEC_RADIOBOSS
    SetOutPath "$PathRadioBoss"
    File "${SRC_WIN}\dsp_mcaster1.dll"
    File "${SRC_WIN}\fdk-aac.dll"
    File "${SRC_WIN}\iconv.dll"
    File "${SRC_WIN}\libcurl.dll"
    File "${SRC_WIN}\libFLAC.dll"
    File "${SRC_WIN}\libmp3lame.DLL"
    File "${SRC_WIN}\ogg.dll"
    File "${SRC_WIN}\opus.dll"
    File "${SRC_WIN}\opusenc.dll"
    File "${SRC_WIN}\pthreadVSE.dll"
    File "${SRC_WIN}\vorbis.dll"
    File "${SRC_WIN}\vorbisenc.dll"
    File "${SRC_WIN}\yaml.dll"
    WriteRegStr HKLM "${SUITE_REG}" "RadioBossPath" "$PathRadioBoss"
SectionEnd

SectionGroupEnd

; Hidden section — always runs last; writes the suite uninstaller
Section "-SuiteFinalize"
    CreateDirectory "$PROGRAMFILES\Mcaster1"
    WriteUninstaller "$PROGRAMFILES\Mcaster1\uninstall_mcaster1_suite.exe"

    WriteRegStr   ${UNINST_ROOT} "${UNINST_KEY}" "DisplayName"     "${APP_NAME} ${VERSION}"
    WriteRegStr   ${UNINST_ROOT} "${UNINST_KEY}" "UninstallString" \
        "$PROGRAMFILES\Mcaster1\uninstall_mcaster1_suite.exe"
    WriteRegStr   ${UNINST_ROOT} "${UNINST_KEY}" "InstallLocation" "$PROGRAMFILES\Mcaster1"
    WriteRegStr   ${UNINST_ROOT} "${UNINST_KEY}" "Publisher"       "${PUBLISHER}"
    WriteRegStr   ${UNINST_ROOT} "${UNINST_KEY}" "DisplayVersion"  "${VERSION}"
    WriteRegStr   ${UNINST_ROOT} "${UNINST_KEY}" "URLInfoAbout"    "${PRODUCT_URL}"
    WriteRegDWORD ${UNINST_ROOT} "${UNINST_KEY}" "NoModify"        1
    WriteRegDWORD ${UNINST_ROOT} "${UNINST_KEY}" "NoRepair"        1
SectionEnd

; ---------------------------------------------------------------------------
; Section descriptions — must appear after section declarations
; ---------------------------------------------------------------------------
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SEC_STANDALONE} \
        "Standalone encoder application.$\nInstalls to $PROGRAMFILES\Mcaster1\Mcaster1DSPEncoder."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecPluginsGroup} \
        "DSP plugin components for third-party audio player applications."
    !insertmacro MUI_DESCRIPTION_TEXT ${SEC_WINAMP} \
        "Winamp DSP plugin.$\nRequires Winamp 2.x / 5.x to be installed."
    !insertmacro MUI_DESCRIPTION_TEXT ${SEC_WACUP} \
        "WACUP DSP plugin (x86).$\nRequires WACUP to be installed."
    !insertmacro MUI_DESCRIPTION_TEXT ${SEC_FOOBAR} \
        "foobar2000 component (foo_mcaster1.dll).$\nRequires foobar2000 to be installed."
    !insertmacro MUI_DESCRIPTION_TEXT ${SEC_RADIODJ} \
        "RadioDJ v1 DSP plugin.$\nTargets RadioDJ v1 ONLY — not compatible with v2."
    !insertmacro MUI_DESCRIPTION_TEXT ${SEC_RADIOBOSS} \
        "RadioBoss DSP plugin.$\nRequires RadioBoss to be installed."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ===========================================================================
; FUNCTIONS that reference ${SEC_*} — placed after sections so defines exist
; ===========================================================================

; ---------------------------------------------------------------------------
; Standalone directory page — skip if not selected
; ---------------------------------------------------------------------------
Function StandaloneDirPre
    ${IfNot} ${SectionIsSelected} ${SEC_STANDALONE}
        Abort
    ${EndIf}
    StrCpy $INSTDIR $PathStandalone
FunctionEnd

Function StandaloneDirLeave
    StrCpy $PathStandalone $INSTDIR
FunctionEnd

; ---------------------------------------------------------------------------
; Plugin paths page — skip if no plugin is selected
; ---------------------------------------------------------------------------
Function PagePluginPaths
    ${IfNot} ${SectionIsSelected} ${SEC_WINAMP}
    ${AndIfNot} ${SectionIsSelected} ${SEC_WACUP}
    ${AndIfNot} ${SectionIsSelected} ${SEC_FOOBAR}
    ${AndIfNot} ${SectionIsSelected} ${SEC_RADIODJ}
    ${AndIfNot} ${SectionIsSelected} ${SEC_RADIOBOSS}
        Abort
    ${EndIf}

    !insertmacro MUI_HEADER_TEXT "Plugin Install Paths" \
        "Verify the installation folder for each selected plugin."

    nsDialogs::Create 1018
    Pop $0

    StrCpy $R1 5   ; running Y accumulator (dialog units)

    ${If} ${SectionIsSelected} ${SEC_WINAMP}
        ${NSD_CreateLabel} 0 $R1 100% 10u \
            "Winamp  (dsp_mcaster1.dll will be placed in Plugins\ sub-folder):"
        IntOp $R1 $R1 + 12
        ${NSD_CreateDirRequest} 0 $R1 82% 14u "$PathWinamp"
        Pop $DirWinamp
        ${NSD_CreateBrowseButton} 84% $R1 16% 14u "Browse..."
        Pop $0
        ${NSD_OnClick} $0 OnBrowseWinamp
        IntOp $R1 $R1 + 22
    ${Else}
        StrCpy $DirWinamp ""
    ${EndIf}

    ${If} ${SectionIsSelected} ${SEC_WACUP}
        ${NSD_CreateLabel} 0 $R1 100% 10u \
            "WACUP  (dsp_mcaster1.dll will be placed in Plugins\ sub-folder):"
        IntOp $R1 $R1 + 12
        ${NSD_CreateDirRequest} 0 $R1 82% 14u "$PathWACUP"
        Pop $DirWACUP
        ${NSD_CreateBrowseButton} 84% $R1 16% 14u "Browse..."
        Pop $0
        ${NSD_OnClick} $0 OnBrowseWACUP
        IntOp $R1 $R1 + 22
    ${Else}
        StrCpy $DirWACUP ""
    ${EndIf}

    ${If} ${SectionIsSelected} ${SEC_FOOBAR}
        ${NSD_CreateLabel} 0 $R1 100% 10u \
            "foobar2000  (foo_mcaster1.dll will be placed in components\ sub-folder):"
        IntOp $R1 $R1 + 12
        ${NSD_CreateDirRequest} 0 $R1 82% 14u "$PathFoobar"
        Pop $DirFoobar
        ${NSD_CreateBrowseButton} 84% $R1 16% 14u "Browse..."
        Pop $0
        ${NSD_OnClick} $0 OnBrowseFoobar
        IntOp $R1 $R1 + 22
    ${Else}
        StrCpy $DirFoobar ""
    ${EndIf}

    ${If} ${SectionIsSelected} ${SEC_RADIODJ}
        ${NSD_CreateLabel} 0 $R1 100% 10u \
            "RadioDJ v1  (all files go to the RadioDJ root folder):"
        IntOp $R1 $R1 + 12
        ${NSD_CreateDirRequest} 0 $R1 82% 14u "$PathRadioDJ"
        Pop $DirRadioDJ
        ${NSD_CreateBrowseButton} 84% $R1 16% 14u "Browse..."
        Pop $0
        ${NSD_OnClick} $0 OnBrowseRadioDJ
        IntOp $R1 $R1 + 22
    ${Else}
        StrCpy $DirRadioDJ ""
    ${EndIf}

    ${If} ${SectionIsSelected} ${SEC_RADIOBOSS}
        ${NSD_CreateLabel} 0 $R1 100% 10u \
            "RadioBoss  (all files go to the RadioBoss root folder):"
        IntOp $R1 $R1 + 12
        ${NSD_CreateDirRequest} 0 $R1 82% 14u "$PathRadioBoss"
        Pop $DirRadioBoss
        ${NSD_CreateBrowseButton} 84% $R1 16% 14u "Browse..."
        Pop $0
        ${NSD_OnClick} $0 OnBrowseRadioBoss
        IntOp $R1 $R1 + 22
    ${Else}
        StrCpy $DirRadioBoss ""
    ${EndIf}

    nsDialogs::Show
FunctionEnd

Function PagePluginPathsLeave
    ${If} ${SectionIsSelected} ${SEC_WINAMP}
        ${NSD_GetText} $DirWinamp $PathWinamp
        ${If} $PathWinamp == ""
            MessageBox MB_OK|MB_ICONEXCLAMATION "Please enter a valid Winamp install path."
            Abort
        ${EndIf}
    ${EndIf}
    ${If} ${SectionIsSelected} ${SEC_WACUP}
        ${NSD_GetText} $DirWACUP $PathWACUP
        ${If} $PathWACUP == ""
            MessageBox MB_OK|MB_ICONEXCLAMATION "Please enter a valid WACUP install path."
            Abort
        ${EndIf}
    ${EndIf}
    ${If} ${SectionIsSelected} ${SEC_FOOBAR}
        ${NSD_GetText} $DirFoobar $PathFoobar
        ${If} $PathFoobar == ""
            MessageBox MB_OK|MB_ICONEXCLAMATION "Please enter a valid foobar2000 install path."
            Abort
        ${EndIf}
    ${EndIf}
    ${If} ${SectionIsSelected} ${SEC_RADIODJ}
        ${NSD_GetText} $DirRadioDJ $PathRadioDJ
        ${If} $PathRadioDJ == ""
            MessageBox MB_OK|MB_ICONEXCLAMATION "Please enter a valid RadioDJ v1 install path."
            Abort
        ${EndIf}
    ${EndIf}
    ${If} ${SectionIsSelected} ${SEC_RADIOBOSS}
        ${NSD_GetText} $DirRadioBoss $PathRadioBoss
        ${If} $PathRadioBoss == ""
            MessageBox MB_OK|MB_ICONEXCLAMATION "Please enter a valid RadioBoss install path."
            Abort
        ${EndIf}
    ${EndIf}
FunctionEnd

; ---------------------------------------------------------------------------
; Browse button handlers
; ---------------------------------------------------------------------------
Function OnBrowseWinamp
    ${NSD_GetText} $DirWinamp $0
    nsDialogs::SelectFolderDialog "Select Winamp install folder" "$0"
    Pop $0
    ${IfNot} $0 == "error"
        ${NSD_SetText} $DirWinamp $0
    ${EndIf}
FunctionEnd

Function OnBrowseWACUP
    ${NSD_GetText} $DirWACUP $0
    nsDialogs::SelectFolderDialog "Select WACUP install folder" "$0"
    Pop $0
    ${IfNot} $0 == "error"
        ${NSD_SetText} $DirWACUP $0
    ${EndIf}
FunctionEnd

Function OnBrowseFoobar
    ${NSD_GetText} $DirFoobar $0
    nsDialogs::SelectFolderDialog "Select foobar2000 install folder" "$0"
    Pop $0
    ${IfNot} $0 == "error"
        ${NSD_SetText} $DirFoobar $0
    ${EndIf}
FunctionEnd

Function OnBrowseRadioDJ
    ${NSD_GetText} $DirRadioDJ $0
    nsDialogs::SelectFolderDialog "Select RadioDJ v1 install folder" "$0"
    Pop $0
    ${IfNot} $0 == "error"
        ${NSD_SetText} $DirRadioDJ $0
    ${EndIf}
FunctionEnd

Function OnBrowseRadioBoss
    ${NSD_GetText} $DirRadioBoss $0
    nsDialogs::SelectFolderDialog "Select RadioBoss install folder" "$0"
    Pop $0
    ${IfNot} $0 == "error"
        ${NSD_SetText} $DirRadioBoss $0
    ${EndIf}
FunctionEnd

; ---------------------------------------------------------------------------
; Uninstall — reads installed paths from SUITE_REG and removes per-component
; ---------------------------------------------------------------------------
Section "Uninstall"
    ; Standalone
    ReadRegStr $0 HKLM "${SUITE_REG}" "StandalonePath"
    ${If} $0 != ""
        Delete "$0\MCASTER1DSPENCODER.exe"
        Delete "$0\fdk-aac.dll"
        Delete "$0\iconv.dll"
        Delete "$0\libcurl.dll"
        Delete "$0\libFLAC.dll"
        Delete "$0\libmp3lame.DLL"
        Delete "$0\ogg.dll"
        Delete "$0\opus.dll"
        Delete "$0\opusenc.dll"
        Delete "$0\pthreadVSE.dll"
        Delete "$0\vorbis.dll"
        Delete "$0\vorbisenc.dll"
        Delete "$0\yaml.dll"
        RMDir  "$0"
        RMDir  "$PROGRAMFILES\Mcaster1"
    ${EndIf}

    ; Winamp
    ReadRegStr $0 HKLM "${SUITE_REG}" "WinampPath"
    ${If} $0 != ""
        Delete "$0\Plugins\dsp_mcaster1.dll"
        Delete "$0\fdk-aac.dll"
        Delete "$0\iconv.dll"
        Delete "$0\libcurl.dll"
        Delete "$0\libFLAC.dll"
        Delete "$0\libmp3lame.DLL"
        Delete "$0\ogg.dll"
        Delete "$0\opus.dll"
        Delete "$0\opusenc.dll"
        Delete "$0\pthreadVSE.dll"
        Delete "$0\vorbis.dll"
        Delete "$0\vorbisenc.dll"
        Delete "$0\yaml.dll"
    ${EndIf}

    ; WACUP
    ReadRegStr $0 HKLM "${SUITE_REG}" "WACUPPath"
    ${If} $0 != ""
        Delete "$0\Plugins\dsp_mcaster1.dll"
        Delete "$0\fdk-aac.dll"
        Delete "$0\iconv.dll"
        Delete "$0\libcurl.dll"
        Delete "$0\libFLAC.dll"
        Delete "$0\libmp3lame.DLL"
        Delete "$0\ogg.dll"
        Delete "$0\opus.dll"
        Delete "$0\opusenc.dll"
        Delete "$0\pthreadVSE.dll"
        Delete "$0\vorbis.dll"
        Delete "$0\vorbisenc.dll"
        Delete "$0\yaml.dll"
    ${EndIf}

    ; foobar2000
    ReadRegStr $0 HKLM "${SUITE_REG}" "FoobarPath"
    ${If} $0 != ""
        Delete "$0\components\foo_mcaster1.dll"
        Delete "$0\fdk-aac.dll"
        Delete "$0\iconv.dll"
        Delete "$0\libcurl.dll"
        Delete "$0\libFLAC.dll"
        Delete "$0\libmp3lame.DLL"
        Delete "$0\ogg.dll"
        Delete "$0\opus.dll"
        Delete "$0\opusenc.dll"
        Delete "$0\pthreadVSE.dll"
        Delete "$0\vorbis.dll"
        Delete "$0\vorbisenc.dll"
        Delete "$0\yaml.dll"
    ${EndIf}

    ; RadioDJ
    ReadRegStr $0 HKLM "${SUITE_REG}" "RadioDJPath"
    ${If} $0 != ""
        Delete "$0\dsp_mcaster1.dll"
        Delete "$0\fdk-aac.dll"
        Delete "$0\iconv.dll"
        Delete "$0\libcurl.dll"
        Delete "$0\libFLAC.dll"
        Delete "$0\libmp3lame.DLL"
        Delete "$0\ogg.dll"
        Delete "$0\opus.dll"
        Delete "$0\opusenc.dll"
        Delete "$0\pthreadVSE.dll"
        Delete "$0\vorbis.dll"
        Delete "$0\vorbisenc.dll"
        Delete "$0\yaml.dll"
    ${EndIf}

    ; RadioBoss
    ReadRegStr $0 HKLM "${SUITE_REG}" "RadioBossPath"
    ${If} $0 != ""
        Delete "$0\dsp_mcaster1.dll"
        Delete "$0\fdk-aac.dll"
        Delete "$0\iconv.dll"
        Delete "$0\libcurl.dll"
        Delete "$0\libFLAC.dll"
        Delete "$0\libmp3lame.DLL"
        Delete "$0\ogg.dll"
        Delete "$0\opus.dll"
        Delete "$0\opusenc.dll"
        Delete "$0\pthreadVSE.dll"
        Delete "$0\vorbis.dll"
        Delete "$0\vorbisenc.dll"
        Delete "$0\yaml.dll"
    ${EndIf}

    DeleteRegKey HKLM "${SUITE_REG}"
    DeleteRegKey HKLM "Software\Mcaster1"
    DeleteRegKey ${UNINST_ROOT} "${UNINST_KEY}"

    Delete "$PROGRAMFILES\Mcaster1\uninstall_mcaster1_suite.exe"
    RMDir  "$PROGRAMFILES\Mcaster1"
SectionEnd

; ---------------------------------------------------------------------------
; Version stamp embedded in setup.exe
; ---------------------------------------------------------------------------
VIProductVersion  "${VERSION_QUAD}"
VIAddVersionKey   "ProductName"     "${APP_NAME}"
VIAddVersionKey   "ProductVersion"  "${VERSION}"
VIAddVersionKey   "CompanyName"     "${COMPANY_NAME}"
VIAddVersionKey   "FileVersion"     "${VERSION}"
VIAddVersionKey   "FileDescription" "${APP_NAME} Installer"
VIAddVersionKey   "LegalCopyright"  "Copyright (C) 2024 ${PUBLISHER}"
