; =============================================================================
; Mcaster1 DSP Encoder — RadioDJ v1 DSP Plugin Installer
;
; Pre-build:   .\generate_version.ps1
; Compile:     makensis mcaster1_radiodj_setup.nsi
; Output:      mcaster1_radiodj_<version>_setup.exe
;
; Targets RadioDJ v1 ONLY.  RadioDJ loads Winamp DSP plugins from its root.
; Plugin DLL:   $INSTDIR\dsp_mcaster1.dll
; Runtime DLLs: $INSTDIR\
; =============================================================================

Unicode True
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "nsDialogs.nsh"
!include "LogicLib.nsh"
!include "version.nsh"

; ---------------------------------------------------------------------------
; Branding
; ---------------------------------------------------------------------------
!define APP_NAME      "Mcaster1 DSP Encoder (RadioDJ v1 Plugin)"
!define COMPANY_NAME  "Mcaster1"
!define PUBLISHER     "David St. John"
!define PRODUCT_URL   "https://mcaster1.com/encoder.php"
!define UNINST_KEY    "Software\Microsoft\Windows\CurrentVersion\Uninstall\Mcaster1DSP_RadioDJ"
!define UNINST_ROOT   "HKLM"

!define SRC_DIR       "Winamp_Plugin\Release"
!define ICON          "src\mcaster1.ico"
!define SPLASH        "mcaster1-dspencoder_splash_installer.bmp"
!define SIDEBAR       "mcaster1-dspencoder_sidebar.bmp"
!define HEADER        "mcaster1-authority-dspencoder_header-banner-.png"

; RadioDJ v1 common install paths (checked in order)
!define RADIODJ_DEFAULT "C:\RadioDJ"

; ---------------------------------------------------------------------------
; Installer metadata
; ---------------------------------------------------------------------------
Name              "${APP_NAME} ${VERSION}"
OutFile           "mcaster1_radiodj_${VERSION}_setup.exe"
InstallDir        "${RADIODJ_DEFAULT}"
RequestExecutionLevel admin
ShowInstDetails   show
ShowUnInstDetails show

Var DetectedPath
Var DirCtrl

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
Page custom PageDetectDir PageDetectDirLeave
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

; ---------------------------------------------------------------------------
; Splash + RadioDJ v1 path detection
; RadioDJ v1 does not always register in HKLM; probe common locations.
; ---------------------------------------------------------------------------
Function .onInit
    InitPluginsDir
    File "/oname=$PLUGINSDIR\splash.bmp" "${SPLASH}"
    advsplash::show 2500 600 400 -1 "$PLUGINSDIR\splash"
    Pop $0
    Delete "$PLUGINSDIR\splash.bmp"

    ; Try registry first
    StrCpy $DetectedPath ""
    ReadRegStr $DetectedPath HKLM "SOFTWARE\WOW6432Node\RadioDJ" "InstallPath"
    ${If} $DetectedPath == ""
        ReadRegStr $DetectedPath HKLM "SOFTWARE\WOW6432Node\RadioDJ" ""
    ${EndIf}
    ${If} $DetectedPath == ""
        ReadRegStr $DetectedPath HKLM "SOFTWARE\RadioDJ" "InstallPath"
    ${EndIf}

    ; Fall back to probing well-known paths
    ${If} $DetectedPath == ""
        IfFileExists "C:\RadioDJ\RadioDJ.exe"           0 +2
            StrCpy $DetectedPath "C:\RadioDJ"
    ${EndIf}
    ${If} $DetectedPath == ""
        IfFileExists "C:\RadioDJv1\RadioDJ.exe"         0 +2
            StrCpy $DetectedPath "C:\RadioDJv1"
    ${EndIf}
    ${If} $DetectedPath == ""
        IfFileExists "$PROGRAMFILES\RadioDJ\RadioDJ.exe" 0 +2
            StrCpy $DetectedPath "$PROGRAMFILES\RadioDJ"
    ${EndIf}
    ${If} $DetectedPath == ""
        StrCpy $DetectedPath "${RADIODJ_DEFAULT}"
    ${EndIf}
    StrCpy $INSTDIR $DetectedPath
FunctionEnd

; ---------------------------------------------------------------------------
; Custom detect page
; ---------------------------------------------------------------------------
Function PageDetectDir
    !insertmacro MUI_HEADER_TEXT "RadioDJ v1 Install Location" \
        "Confirm the path to your RadioDJ v1 installation."

    nsDialogs::Create 1018
    Pop $0

    ${NSD_CreateLabel} 0 0 100% 48u \
        "RadioDJ v1 was detected at the path shown below.$\r$\nVerify it is correct, or Browse to select a different folder.$\r$\n$\r$\nIMPORTANT: This installer targets RadioDJ v1 only.$\r$\ndsp_mcaster1.dll and all runtime DLLs are placed in the RadioDJ root folder."
    Pop $0

    ${NSD_CreateDirRequest} 0 55u 83% 14u "$INSTDIR"
    Pop $DirCtrl

    ${NSD_CreateBrowseButton} 85% 54u 15% 16u "Browse..."
    Pop $0
    ${NSD_OnClick} $0 OnBrowseDir

    nsDialogs::Show
FunctionEnd

Function PageDetectDirLeave
    ${NSD_GetText} $DirCtrl $INSTDIR
    ${If} $INSTDIR == ""
        MessageBox MB_OK|MB_ICONEXCLAMATION "Please select a valid RadioDJ v1 install directory."
        Abort
    ${EndIf}
FunctionEnd

Function OnBrowseDir
    ${NSD_GetText} $DirCtrl $0
    nsDialogs::SelectFolderDialog "Select RadioDJ v1 install folder" "$0"
    Pop $0
    ${IfNot} $0 == "error"
        ${NSD_SetText} $DirCtrl $0
    ${EndIf}
FunctionEnd

; ---------------------------------------------------------------------------
; Install section — plugin + DLLs all go to RadioDJ root
; ---------------------------------------------------------------------------
Section "Mcaster1 RadioDJ Plugin" SecMain
    SectionIn RO

    SetOutPath "$INSTDIR"
    File "${SRC_DIR}\dsp_mcaster1.dll"
    File "${SRC_DIR}\fdk-aac.dll"
    File "${SRC_DIR}\iconv.dll"
    File "${SRC_DIR}\libcurl.dll"
    File "${SRC_DIR}\libFLAC.dll"
    File "${SRC_DIR}\libmp3lame.DLL"
    File "${SRC_DIR}\ogg.dll"
    File "${SRC_DIR}\opus.dll"
    File "${SRC_DIR}\opusenc.dll"
    File "${SRC_DIR}\pthreadVSE.dll"
    File "${SRC_DIR}\vorbis.dll"
    File "${SRC_DIR}\vorbisenc.dll"
    File "${SRC_DIR}\yaml.dll"

    WriteRegStr   ${UNINST_ROOT} "${UNINST_KEY}" "DisplayName"     "${APP_NAME} ${VERSION}"
    WriteRegStr   ${UNINST_ROOT} "${UNINST_KEY}" "UninstallString" "$INSTDIR\uninstall_mcaster1_radiodj.exe"
    WriteRegStr   ${UNINST_ROOT} "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr   ${UNINST_ROOT} "${UNINST_KEY}" "Publisher"       "${PUBLISHER}"
    WriteRegStr   ${UNINST_ROOT} "${UNINST_KEY}" "DisplayVersion"  "${VERSION}"
    WriteRegStr   ${UNINST_ROOT} "${UNINST_KEY}" "URLInfoAbout"    "${PRODUCT_URL}"
    WriteRegDWORD ${UNINST_ROOT} "${UNINST_KEY}" "NoModify"        1
    WriteRegDWORD ${UNINST_ROOT} "${UNINST_KEY}" "NoRepair"        1

    WriteUninstaller "$INSTDIR\uninstall_mcaster1_radiodj.exe"
SectionEnd

; ---------------------------------------------------------------------------
; Uninstall
; ---------------------------------------------------------------------------
Section "Uninstall"
    Delete "$INSTDIR\dsp_mcaster1.dll"
    Delete "$INSTDIR\fdk-aac.dll"
    Delete "$INSTDIR\iconv.dll"
    Delete "$INSTDIR\libcurl.dll"
    Delete "$INSTDIR\libFLAC.dll"
    Delete "$INSTDIR\libmp3lame.DLL"
    Delete "$INSTDIR\ogg.dll"
    Delete "$INSTDIR\opus.dll"
    Delete "$INSTDIR\opusenc.dll"
    Delete "$INSTDIR\pthreadVSE.dll"
    Delete "$INSTDIR\vorbis.dll"
    Delete "$INSTDIR\vorbisenc.dll"
    Delete "$INSTDIR\yaml.dll"
    Delete "$INSTDIR\uninstall_mcaster1_radiodj.exe"

    DeleteRegKey ${UNINST_ROOT} "${UNINST_KEY}"
SectionEnd

VIProductVersion  "${VERSION_QUAD}"
VIAddVersionKey   "ProductName"     "${APP_NAME}"
VIAddVersionKey   "ProductVersion"  "${VERSION}"
VIAddVersionKey   "CompanyName"     "${COMPANY_NAME}"
VIAddVersionKey   "FileVersion"     "${VERSION}"
VIAddVersionKey   "FileDescription" "${APP_NAME} Installer"
VIAddVersionKey   "LegalCopyright"  "Copyright (C) 2024 ${PUBLISHER}"
