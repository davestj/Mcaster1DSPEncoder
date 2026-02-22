; mcaster1dspencoder_standalone.nsi
; NSIS installer for Mcaster1 DSP Encoder — Standalone application
;
; Prerequisites before running makensis:
;   1. Build Release configuration (Mcaster1DSPEncoder.sln)
;   2. Copy vcpkg DLLs into Release\:
;        fdk-aac.dll, libmp3lame.dll, opus.dll, opusenc.dll
;      (the external\lib DLLs below are referenced directly)
;
; Build:  makensis mcaster1dspencoder_standalone.nsi

;---------------------------------------------------------------------------
; General
;---------------------------------------------------------------------------
Name "Mcaster1 DSP Encoder"
OutFile "mcaster1dspencoder_standalone_setup.exe"
InstallDir "$PROGRAMFILES\Mcaster1DSPEncoder"
InstallDirRegKey HKLM "Software\Mcaster1DSPEncoder" "InstallDir"
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show

LicenseText "Mcaster1 DSP Encoder is released under the GNU Public License"
LicenseData "..\COPYING"

UninstallText "This will uninstall Mcaster1 DSP Encoder Standalone. Click Next to continue."
DirText "Please select the installation directory:"

;---------------------------------------------------------------------------
; Main install section
;---------------------------------------------------------------------------
Section "Mcaster1 DSP Encoder (required)" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"

  ; Main executable
  File "..\Release\Mcaster1DSPEncoder.exe"

  ; Codec DLLs (vcpkg — copy to Release\ at repo root before running makensis)
  File "..\Release\fdk-aac.dll"
  File "..\Release\libmp3lame.dll"
  File "..\Release\opus.dll"
  File "..\Release\opusenc.dll"

  ; Codec DLLs (external\lib)
  File "..\external\lib\ogg.dll"
  File "..\external\lib\vorbis.dll"
  File "..\external\lib\vorbisenc.dll"
  File "..\external\lib\libFLAC.dll"
  File "..\external\lib\pthreadVSE.dll"
  File "..\external\lib\libcurl.dll"
  File "..\external\lib\iconv.dll"

  ; Registry entries
  WriteRegStr HKLM "Software\Mcaster1DSPEncoder" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Mcaster1DSPEncoder" \
              "DisplayName" "Mcaster1 DSP Encoder"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Mcaster1DSPEncoder" \
              "UninstallString" "$INSTDIR\mcaster1dspencoder-uninst.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Mcaster1DSPEncoder" \
              "Publisher" "Mcaster1"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Mcaster1DSPEncoder" \
              "URLInfoAbout" "https://github.com/davestj/Mcaster1DSPEncoder"

  WriteUninstaller "$INSTDIR\mcaster1dspencoder-uninst.exe"
SectionEnd

;---------------------------------------------------------------------------
Section "Create Start Menu shortcuts" SecStartMenu
  SectionIn 1
  CreateDirectory "$SMPROGRAMS\Mcaster1 DSP Encoder"
  CreateShortCut "$SMPROGRAMS\Mcaster1 DSP Encoder\Mcaster1 DSP Encoder.lnk" \
                 "$INSTDIR\Mcaster1DSPEncoder.exe"
  CreateShortCut "$SMPROGRAMS\Mcaster1 DSP Encoder\Uninstall.lnk" \
                 "$INSTDIR\mcaster1dspencoder-uninst.exe"
SectionEnd

;---------------------------------------------------------------------------
Section "Create Desktop Shortcut" SecDesktop
  SectionIn 1
  CreateShortCut "$DESKTOP\Mcaster1 DSP Encoder.lnk" "$INSTDIR\Mcaster1DSPEncoder.exe"
SectionEnd

;---------------------------------------------------------------------------
; Uninstall
;---------------------------------------------------------------------------
Section "Uninstall"
  Delete "$INSTDIR\Mcaster1DSPEncoder.exe"
  Delete "$INSTDIR\fdk-aac.dll"
  Delete "$INSTDIR\libmp3lame.dll"
  Delete "$INSTDIR\opus.dll"
  Delete "$INSTDIR\opusenc.dll"
  Delete "$INSTDIR\ogg.dll"
  Delete "$INSTDIR\vorbis.dll"
  Delete "$INSTDIR\vorbisenc.dll"
  Delete "$INSTDIR\libFLAC.dll"
  Delete "$INSTDIR\pthreadVSE.dll"
  Delete "$INSTDIR\libcurl.dll"
  Delete "$INSTDIR\iconv.dll"
  Delete "$INSTDIR\mcaster1dspencoder-uninst.exe"

  Delete "$SMPROGRAMS\Mcaster1 DSP Encoder\Mcaster1 DSP Encoder.lnk"
  Delete "$SMPROGRAMS\Mcaster1 DSP Encoder\Uninstall.lnk"
  RMDir  "$SMPROGRAMS\Mcaster1 DSP Encoder"
  Delete "$DESKTOP\Mcaster1 DSP Encoder.lnk"

  ; Remove user config/log files (optional — comment out to preserve)
  ; Delete "$INSTDIR\mcaster1dspencoder*.cfg"
  ; Delete "$INSTDIR\mcaster1dspencoder*.log"

  RMDir  "$INSTDIR"

  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Mcaster1DSPEncoder"
  DeleteRegKey HKLM "Software\Mcaster1DSPEncoder"

  MessageBox MB_OK "Mcaster1 DSP Encoder has been removed."
SectionEnd
; eof
