; ============================================================
; Mcaster1DSPEncoder — NSIS Installer Script
; Installs to C:\Users\USERNAME\Mcaster1\Mcaster1DSPEncoder
; ============================================================

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"

; ── Product Info ──────────────────────────────────────────────
!define PRODUCT_NAME        "Mcaster1 DSP Encoder"
!define PRODUCT_SHORTNAME   "Mcaster1DSPEncoder"
!ifndef PRODUCT_VERSION
  !define PRODUCT_VERSION   "1.3.1-beta"
!endif
!define PRODUCT_PUBLISHER   "Mcaster1 Software"
!define PRODUCT_WEB_SITE    "https://mcaster1.com"
!define PRODUCT_EXE         "Mcaster1DSPEncoder_Qt.exe"
!define PRODUCT_UNINST_KEY  "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_SHORTNAME}"

; ── Source Directories (set by build_installer.ps1) ──────────
; These point to the staging directory created by build_installer.ps1
!ifndef STAGING_DIR
  !define STAGING_DIR "staging"
!endif

; ── Output ────────────────────────────────────────────────────
; Installer exe lands in the installer/ directory (project root)
Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
!ifdef OUTDIR
  OutFile "${OUTDIR}\Mcaster1DSPEncoderQT_Setup_${PRODUCT_VERSION}.exe"
!else
  OutFile "Mcaster1DSPEncoderQT_Setup_${PRODUCT_VERSION}.exe"
!endif
Unicode True
SetCompressor /SOLID lzma
SetCompressorDictSize 64
RequestExecutionLevel user  ; No UAC — installs to user profile

; Default install directory: C:\Users\USERNAME\Mcaster1\Mcaster1DSPEncoder
InstallDir "$PROFILE\Mcaster1\${PRODUCT_SHORTNAME}"

; ── MUI2 Configuration ───────────────────────────────────────
!define MUI_ICON "${STAGING_DIR}\app.ico"
!define MUI_UNICON "${STAGING_DIR}\app.ico"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "Welcome to ${PRODUCT_NAME} Setup"
!define MUI_WELCOMEPAGE_TEXT "This wizard will install ${PRODUCT_NAME} ${PRODUCT_VERSION} on your computer.$\r$\n$\r$\nA professional broadcast DSP encoder with:$\r$\n  - 8 audio codecs (MP3, Opus, Vorbis, FLAC, AAC)$\r$\n  - 7 DSP effects (EQ, AGC, Sonic Enhancer, PTT Duck)$\r$\n  - Live video studio with 12 broadcast transitions$\r$\n  - Virtual Camera output$\r$\n$\r$\nClick Next to continue."
!define MUI_FINISHPAGE_RUN "$INSTDIR\${PRODUCT_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Launch ${PRODUCT_NAME}"

; ── Pages ─────────────────────────────────────────────────────
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${STAGING_DIR}\LICENSE.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; ── Install Section ───────────────────────────────────────────
Section "Main Application" SecMain
  SectionIn RO  ; Required — cannot be deselected

  SetOutPath "$INSTDIR"

  ; ── Main executable ──
  File "${STAGING_DIR}\${PRODUCT_EXE}"

  ; ── Virtual Camera DLL ──
  File /nonfatal "${STAGING_DIR}\Mcaster1VirtualCam.dll"

  ; ── Qt Framework DLLs (Debug "d" suffix OR Release — only one set present) ──
  File /nonfatal "${STAGING_DIR}\Qt6Cored.dll"
  File /nonfatal "${STAGING_DIR}\Qt6Guid.dll"
  File /nonfatal "${STAGING_DIR}\Qt6Widgetsd.dll"
  File /nonfatal "${STAGING_DIR}\Qt6Multimediad.dll"
  File /nonfatal "${STAGING_DIR}\Qt6Networkd.dll"
  File /nonfatal "${STAGING_DIR}\Qt6Svgd.dll"
  File /nonfatal "${STAGING_DIR}\Qt6Pdfd.dll"
  File /nonfatal "${STAGING_DIR}\Qt6Core.dll"
  File /nonfatal "${STAGING_DIR}\Qt6Gui.dll"
  File /nonfatal "${STAGING_DIR}\Qt6Widgets.dll"
  File /nonfatal "${STAGING_DIR}\Qt6Multimedia.dll"
  File /nonfatal "${STAGING_DIR}\Qt6Network.dll"
  File /nonfatal "${STAGING_DIR}\Qt6Svg.dll"
  File /nonfatal "${STAGING_DIR}\Qt6Pdf.dll"

  ; ── Direct3D / OpenGL ──
  File /nonfatal "${STAGING_DIR}\D3Dcompiler_47.dll"
  File /nonfatal "${STAGING_DIR}\opengl32sw.dll"

  ; ── FFmpeg / AV DLLs ──
  File /nonfatal "${STAGING_DIR}\avcodec-61.dll"
  File /nonfatal "${STAGING_DIR}\avformat-61.dll"
  File /nonfatal "${STAGING_DIR}\avutil-59.dll"
  File /nonfatal "${STAGING_DIR}\swresample-5.dll"
  File /nonfatal "${STAGING_DIR}\swscale-8.dll"

  ; ── Codec DLLs (vcpkg) ──
  File /nonfatal "${STAGING_DIR}\FLAC.dll"
  File /nonfatal "${STAGING_DIR}\fdk-aac.dll"
  File /nonfatal "${STAGING_DIR}\libmp3lame.dll"
  File /nonfatal "${STAGING_DIR}\libmp3lame.DLL"
  File /nonfatal "${STAGING_DIR}\ogg.dll"
  File /nonfatal "${STAGING_DIR}\opus.dll"
  File /nonfatal "${STAGING_DIR}\opusenc.dll"
  File /nonfatal "${STAGING_DIR}\portaudio.dll"
  File /nonfatal "${STAGING_DIR}\vorbis.dll"
  File /nonfatal "${STAGING_DIR}\vorbisenc.dll"
  File /nonfatal "${STAGING_DIR}\yaml.dll"

  ; ── SSL/TLS ──
  File /nonfatal "${STAGING_DIR}\libcrypto-3-x64.dll"
  File /nonfatal "${STAGING_DIR}\libssl-3-x64.dll"

  ; ── Qt Platform Plugins ──
  SetOutPath "$INSTDIR\platforms"
  File /nonfatal "${STAGING_DIR}\platforms\*.dll"

  SetOutPath "$INSTDIR\styles"
  File /nonfatal "${STAGING_DIR}\styles\*.dll"

  SetOutPath "$INSTDIR\iconengines"
  File /nonfatal "${STAGING_DIR}\iconengines\*.dll"

  SetOutPath "$INSTDIR\imageformats"
  File /nonfatal "${STAGING_DIR}\imageformats\*.dll"

  SetOutPath "$INSTDIR\multimedia"
  File /nonfatal "${STAGING_DIR}\multimedia\*.dll"

  SetOutPath "$INSTDIR\networkinformation"
  File /nonfatal "${STAGING_DIR}\networkinformation\*.dll"

  SetOutPath "$INSTDIR\generic"
  File /nonfatal "${STAGING_DIR}\generic\*.dll"

  SetOutPath "$INSTDIR\tls"
  File /nonfatal "${STAGING_DIR}\tls\*.dll"

  SetOutPath "$INSTDIR\translations"
  File /nonfatal "${STAGING_DIR}\translations\*.qm"

  ; ── Default Config Files (fresh install — zeroed out) ──
  SetOutPath "$INSTDIR"
  ; Only write configs if they don't already exist (preserve user settings on upgrade)
  IfFileExists "$INSTDIR\mcaster1dspencoder_global.yaml" +2
    File "${STAGING_DIR}\config\mcaster1dspencoder_global.yaml"
  IfFileExists "$INSTDIR\radio_encoder_00.yaml" +2
    File "${STAGING_DIR}\config\radio_encoder_00.yaml"
  IfFileExists "$INSTDIR\dsp_effect_agc.yaml" +2
    File "${STAGING_DIR}\config\dsp_effect_agc.yaml"
  IfFileExists "$INSTDIR\dsp_effect_eq10.yaml" +2
    File "${STAGING_DIR}\config\dsp_effect_eq10.yaml"
  IfFileExists "$INSTDIR\dsp_effect_eq31.yaml" +2
    File "${STAGING_DIR}\config\dsp_effect_eq31.yaml"
  IfFileExists "$INSTDIR\dsp_effect_sonic_enhancer.yaml" +2
    File "${STAGING_DIR}\config\dsp_effect_sonic_enhancer.yaml"
  IfFileExists "$INSTDIR\dsp_effect_ptt_duck.yaml" +2
    File "${STAGING_DIR}\config\dsp_effect_ptt_duck.yaml"
  IfFileExists "$INSTDIR\dsp_effect_dbx_voice.yaml" +2
    File "${STAGING_DIR}\config\dsp_effect_dbx_voice.yaml"

  ; ── Documentation ──
  SetOutPath "$INSTDIR\docs"
  File "${STAGING_DIR}\docs\index.html"
  File /nonfatal "${STAGING_DIR}\docs\style.css"
  File /nonfatal "${STAGING_DIR}\docs\README.md"
  File /nonfatal "${STAGING_DIR}\docs\RELEASENOTES.md"
  File /nonfatal "${STAGING_DIR}\docs\FEATURES.md"
  File /nonfatal "${STAGING_DIR}\docs\CHANGELOG.md"
  File /nonfatal "${STAGING_DIR}\docs\ROADMAP.md"
  File /nonfatal "${STAGING_DIR}\docs\CREDITS.md"
  File /nonfatal "${STAGING_DIR}\docs\FORKS.md"
  File /nonfatal "${STAGING_DIR}\docs\LICENSE"

  ; ── Documentation Screenshots ──
  SetOutPath "$INSTDIR\docs\screenshots"
  File /nonfatal "${STAGING_DIR}\docs\screenshots\*.png"

  ; ── Code Signing Certificate (if present) ──
  SetOutPath "$INSTDIR\certs"
  File /nonfatal "${STAGING_DIR}\certs\Mcaster1CodeSigning.cer"

  ; ── Create Uninstaller ──
  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; ── Start Menu Shortcuts ──
  CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
  CreateShortCut  "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk" \
                  "$INSTDIR\${PRODUCT_EXE}" "" "$INSTDIR\${PRODUCT_EXE}" 0
  CreateShortCut  "$SMPROGRAMS\${PRODUCT_NAME}\Documentation.lnk" \
                  "$INSTDIR\docs\index.html"
  CreateShortCut  "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall.lnk" \
                  "$INSTDIR\Uninstall.exe"

  ; ── Desktop Shortcut ──
  CreateShortCut "$DESKTOP\${PRODUCT_NAME}.lnk" \
                 "$INSTDIR\${PRODUCT_EXE}" "" "$INSTDIR\${PRODUCT_EXE}" 0

  ; ── Registry (for Add/Remove Programs) ──
  WriteRegStr HKCU "${PRODUCT_UNINST_KEY}" "DisplayName"     "${PRODUCT_NAME}"
  WriteRegStr HKCU "${PRODUCT_UNINST_KEY}" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
  WriteRegStr HKCU "${PRODUCT_UNINST_KEY}" "DisplayIcon"     "$INSTDIR\${PRODUCT_EXE}"
  WriteRegStr HKCU "${PRODUCT_UNINST_KEY}" "Publisher"        "${PRODUCT_PUBLISHER}"
  WriteRegStr HKCU "${PRODUCT_UNINST_KEY}" "URLInfoAbout"     "${PRODUCT_WEB_SITE}"
  WriteRegStr HKCU "${PRODUCT_UNINST_KEY}" "DisplayVersion"   "${PRODUCT_VERSION}"
  WriteRegStr HKCU "${PRODUCT_UNINST_KEY}" "InstallLocation"  "$INSTDIR"
  WriteRegDWORD HKCU "${PRODUCT_UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${PRODUCT_UNINST_KEY}" "NoRepair" 1

  ; Calculate installed size
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKCU "${PRODUCT_UNINST_KEY}" "EstimatedSize" $0

SectionEnd

; ── Uninstall Section ─────────────────────────────────────────
Section "Uninstall"

  ; ── Remove application files ──
  Delete "$INSTDIR\${PRODUCT_EXE}"
  Delete "$INSTDIR\Mcaster1VirtualCam.dll"
  Delete "$INSTDIR\Uninstall.exe"

  ; DLLs
  Delete "$INSTDIR\*.dll"
  Delete "$INSTDIR\*.DLL"

  ; Qt plugins
  RMDir /r "$INSTDIR\platforms"
  RMDir /r "$INSTDIR\styles"
  RMDir /r "$INSTDIR\iconengines"
  RMDir /r "$INSTDIR\imageformats"
  RMDir /r "$INSTDIR\multimedia"
  RMDir /r "$INSTDIR\networkinformation"
  RMDir /r "$INSTDIR\generic"
  RMDir /r "$INSTDIR\tls"
  RMDir /r "$INSTDIR\translations"

  ; Docs and certs
  RMDir /r "$INSTDIR\docs"
  RMDir /r "$INSTDIR\certs"

  ; Config files — ask user
  MessageBox MB_YESNO "Remove your encoder configuration files?$\r$\n(Select No to keep your settings for future installs)" IDYES RemoveConfigs IDNO SkipConfigs
  RemoveConfigs:
    Delete "$INSTDIR\*.yaml"
    Delete "$INSTDIR\*.yml"
  SkipConfigs:

  ; PDB (if present from debug install)
  Delete "$INSTDIR\*.pdb"

  ; Try to remove install directory (only if empty or configs removed)
  RMDir "$INSTDIR"
  ; Try to remove parent Mcaster1 directory if empty
  RMDir "$INSTDIR\.."

  ; ── Remove shortcuts ──
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\Documentation.lnk"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall.lnk"
  RMDir  "$SMPROGRAMS\${PRODUCT_NAME}"
  Delete "$DESKTOP\${PRODUCT_NAME}.lnk"

  ; ── Remove registry entries ──
  DeleteRegKey HKCU "${PRODUCT_UNINST_KEY}"

SectionEnd
