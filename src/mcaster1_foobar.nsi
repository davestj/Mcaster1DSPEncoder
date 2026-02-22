; mcaster1_foobar.nsi
; NSIS installer for Mcaster1 DSP Encoder — foobar2000 component
;
; Installs foo_mcaster1.dll into the foobar2000 components\ folder and
; places all required runtime DLLs into the foobar2000 root directory.
;
; Prerequisites before running makensis:
;   1. Build Release configuration (mcaster1_foobar.sln)
;   2. Copy vcpkg DLLs into Release\:
;        fdk-aac.dll, libmp3lame.dll, opus.dll, opusenc.dll
;
; Build:  makensis mcaster1_foobar.nsi

;---------------------------------------------------------------------------
; General
;---------------------------------------------------------------------------
Name "Mcaster1 DSP Encoder for foobar2000"
OutFile "mcaster1_foobar_setup.exe"
InstallDir "$PROGRAMFILES\foobar2000"
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show

LicenseText "Mcaster1 DSP Encoder is released under the GNU Public License"
LicenseData "..\COPYING"

UninstallText "This will uninstall the Mcaster1 DSP Encoder foobar2000 component. Click Next to continue."
DirText "Please select your foobar2000 installation directory:"

;---------------------------------------------------------------------------
; foobar2000 component
;---------------------------------------------------------------------------
Section "Mcaster1 component for foobar2000 (required)" SecComponent
  SectionIn RO
  SetOutPath "$INSTDIR\components"
  File "..\foobar2000\Release\foo_mcaster1.dll"

  ; Runtime DLLs go into the foobar2000 root (same dir as foobar2000.exe)
  SetOutPath "$INSTDIR"

  ; Codec DLLs (vcpkg — copy to foobar2000\Release\ before running makensis)
  File "..\foobar2000\Release\fdk-aac.dll"
  File "..\foobar2000\Release\libmp3lame.dll"
  File "..\foobar2000\Release\opus.dll"
  File "..\foobar2000\Release\opusenc.dll"

  ; Codec DLLs (external\lib)
  File "..\external\lib\ogg.dll"
  File "..\external\lib\vorbis.dll"
  File "..\external\lib\vorbisenc.dll"
  File "..\external\lib\libFLAC.dll"
  File "..\external\lib\pthreadVSE.dll"
  File "..\external\lib\libcurl.dll"
  File "..\external\lib\iconv.dll"

  WriteUninstaller "$INSTDIR\mcaster1-foobar-uninst.exe"
SectionEnd

;---------------------------------------------------------------------------
Section "Create Start Menu shortcut" SecStartMenu
  SectionIn 1
  CreateDirectory "$SMPROGRAMS\Mcaster1 DSP Encoder"
  CreateShortCut "$SMPROGRAMS\Mcaster1 DSP Encoder\Uninstall foobar2000 Component.lnk" \
                 "$INSTDIR\mcaster1-foobar-uninst.exe"
SectionEnd

;---------------------------------------------------------------------------
; Uninstall
;---------------------------------------------------------------------------
Section "Uninstall"
  Delete "$INSTDIR\components\foo_mcaster1.dll"
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
  Delete "$INSTDIR\mcaster1-foobar-uninst.exe"

  Delete "$SMPROGRAMS\Mcaster1 DSP Encoder\Uninstall foobar2000 Component.lnk"
  RMDir  "$SMPROGRAMS\Mcaster1 DSP Encoder"

  MessageBox MB_OK "Mcaster1 DSP Encoder foobar2000 component has been removed."
SectionEnd
; eof
