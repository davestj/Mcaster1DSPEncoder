; =============================================================================
; Mcaster1 DSP Encoder — RadioBoss DSP Plugin Installer
;
; Pre-build:   .\generate_version.ps1
; Compile:     makensis mcaster1_radioboss_setup.nsi
; Output:      mcaster1_radioboss_<version>_setup.exe
;
; RadioBoss loads Winamp DSP plugins from its root directory.
; Plugin DLL:   $INSTDIR\dsp_mcaster1.dll
; Runtime DLLs: $INSTDIR\   (matches deploy-radioboss.ps1)
; =============================================================================

Unicode True
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "version.nsh"

; ---------------------------------------------------------------------------
; Branding
; ---------------------------------------------------------------------------
!define APP_NAME      "Mcaster1 DSP Encoder (RadioBoss Plugin)"
!define COMPANY_NAME  "Mcaster1"
!define PUBLISHER     "David St. John"
!define PRODUCT_URL   "https://mcaster1.com/encoder.php"
!define UNINST_KEY    "Software\Microsoft\Windows\CurrentVersion\Uninstall\Mcaster1DSP_RadioBoss"
!define UNINST_ROOT   "HKLM"

!define SRC_DIR       "Winamp_Plugin\Release"
!define ICON          "src\mcaster1.ico"
!define SPLASH        "mcaster1-dspencoder_splash_installer.bmp"
!define SIDEBAR       "mcaster1-dspencoder_sidebar.bmp"
!define HEADER        "mcaster1-authority-dspencoder_header-banner-.png"

; ---------------------------------------------------------------------------
; Installer metadata
; ---------------------------------------------------------------------------
Name              "${APP_NAME} ${VERSION}"
OutFile           "mcaster1_radioboss_${VERSION}_setup.exe"
InstallDir        "$PROGRAMFILES\RadioBoss"
InstallDirRegKey  ${UNINST_ROOT} "${UNINST_KEY}" "InstallLocation"
RequestExecutionLevel admin
ShowInstDetails   show
ShowUnInstDetails show

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
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

; ---------------------------------------------------------------------------
; Splash screen
; ---------------------------------------------------------------------------
Function .onInit
    InitPluginsDir
    File "/oname=$PLUGINSDIR\splash.bmp" "${SPLASH}"
    advsplash::show 2500 600 400 -1 "$PLUGINSDIR\splash"
    Pop $0
    Delete "$PLUGINSDIR\splash.bmp"
FunctionEnd

; ---------------------------------------------------------------------------
; Install section
; ---------------------------------------------------------------------------
Section "Mcaster1 RadioBoss Plugin" SecMain
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
    WriteRegStr   ${UNINST_ROOT} "${UNINST_KEY}" "UninstallString" "$INSTDIR\uninstall_mcaster1_radioboss.exe"
    WriteRegStr   ${UNINST_ROOT} "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr   ${UNINST_ROOT} "${UNINST_KEY}" "Publisher"       "${PUBLISHER}"
    WriteRegStr   ${UNINST_ROOT} "${UNINST_KEY}" "DisplayVersion"  "${VERSION}"
    WriteRegStr   ${UNINST_ROOT} "${UNINST_KEY}" "URLInfoAbout"    "${PRODUCT_URL}"
    WriteRegDWORD ${UNINST_ROOT} "${UNINST_KEY}" "NoModify"        1
    WriteRegDWORD ${UNINST_ROOT} "${UNINST_KEY}" "NoRepair"        1

    WriteUninstaller "$INSTDIR\uninstall_mcaster1_radioboss.exe"
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
    Delete "$INSTDIR\uninstall_mcaster1_radioboss.exe"

    DeleteRegKey ${UNINST_ROOT} "${UNINST_KEY}"
SectionEnd

VIProductVersion  "${VERSION_QUAD}"
VIAddVersionKey   "ProductName"     "${APP_NAME}"
VIAddVersionKey   "ProductVersion"  "${VERSION}"
VIAddVersionKey   "CompanyName"     "${COMPANY_NAME}"
VIAddVersionKey   "FileVersion"     "${VERSION}"
VIAddVersionKey   "FileDescription" "${APP_NAME} Installer"
VIAddVersionKey   "LegalCopyright"  "Copyright (C) 2024 ${PUBLISHER}"
