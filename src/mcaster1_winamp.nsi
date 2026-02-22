; mcaster1_winamp.nsi
; NSIS installer for Mcaster1 DSP Encoder — Winamp / WACUP / RadioBoss DSP plugin
;
; Installs dsp_mcaster1.dll into the Winamp Plugins\ folder and places all
; required runtime DLLs into the Winamp root directory.
;
; The same dsp_mcaster1.dll works for Winamp, WACUP, and RadioBoss.
; Run the appropriate installer for each host application.
;
; Prerequisites before running makensis:
;   1. Build Release configuration (mcaster1_winamp.sln)
;   2. Copy vcpkg DLLs into Release\:
;        fdk-aac.dll, libmp3lame.dll, opus.dll, opusenc.dll
;
; Build:  makensis mcaster1_winamp.nsi

;---------------------------------------------------------------------------
; General
;---------------------------------------------------------------------------
Name "Mcaster1 DSP Encoder for Winamp"
OutFile "mcaster1_winamp_setup.exe"
InstallDir "$PROGRAMFILES\Winamp"
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show

LicenseText "Mcaster1 DSP Encoder is released under the GNU Public License"
LicenseData "..\COPYING"

UninstallText "This will uninstall the Mcaster1 DSP Encoder Winamp plugin. Click Next to continue."
DirText "Please select your Winamp installation directory:"

;---------------------------------------------------------------------------
; DSP plugin
;---------------------------------------------------------------------------
Section "Mcaster1 DSP plugin for Winamp (required)" SecPlugin
  SectionIn RO
  SetOutPath "$INSTDIR\Plugins"
  File "..\Winamp_Plugin\Release\dsp_mcaster1.dll"

  ; Runtime DLLs go into the Winamp root (same dir as winamp.exe)
  SetOutPath "$INSTDIR"

  ; Codec DLLs (vcpkg — copy to Winamp_Plugin\Release\ before running makensis)
  File "..\Winamp_Plugin\Release\fdk-aac.dll"
  File "..\Winamp_Plugin\Release\libmp3lame.dll"
  File "..\Winamp_Plugin\Release\opus.dll"
  File "..\Winamp_Plugin\Release\opusenc.dll"

  ; Codec DLLs (external\lib)
  File "..\external\lib\ogg.dll"
  File "..\external\lib\vorbis.dll"
  File "..\external\lib\vorbisenc.dll"
  File "..\external\lib\libFLAC.dll"
  File "..\external\lib\pthreadVSE.dll"
  File "..\external\lib\libcurl.dll"
  File "..\external\lib\iconv.dll"

  WriteUninstaller "$INSTDIR\mcaster1-winamp-uninst.exe"
SectionEnd

;---------------------------------------------------------------------------
Section "Create Start Menu shortcut" SecStartMenu
  SectionIn 1
  CreateDirectory "$SMPROGRAMS\Mcaster1 DSP Encoder"
  CreateShortCut "$SMPROGRAMS\Mcaster1 DSP Encoder\Uninstall Winamp Plugin.lnk" \
                 "$INSTDIR\mcaster1-winamp-uninst.exe"
SectionEnd

;---------------------------------------------------------------------------
; Uninstall
;---------------------------------------------------------------------------
Section "Uninstall"
  Delete "$INSTDIR\Plugins\dsp_mcaster1.dll"
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
  Delete "$INSTDIR\mcaster1-winamp-uninst.exe"

  Delete "$SMPROGRAMS\Mcaster1 DSP Encoder\Uninstall Winamp Plugin.lnk"
  RMDir  "$SMPROGRAMS\Mcaster1 DSP Encoder"

  MessageBox MB_OK "Mcaster1 DSP Encoder Winamp plugin has been removed."
SectionEnd
; eof
