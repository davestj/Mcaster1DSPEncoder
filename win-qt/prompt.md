 I just pulled the latest changes from the macos-dev branch. The win-qt/ directory contains a new Qt 6 Windows port of our macOS Qt encoder GUI. I need to build it with Visual Studio 2022 /
   MSBuild.                                                                                                                                                                                   
                                                                                                                                                                                              
  ## What was done (on macOS)                                                                                                                                                                 
  - Copied all portable source from src/macos/ into win-qt/
  - Created win-qt/Mcaster1DSPEncoder_Qt.vcxproj (x64 only, v143 toolset, C++17)
  - Added it to Mcaster1DSPEncoder_Master.sln under "Qt Build" solution folder
  - Created 12 Windows platform stub files replacing 6 macOS .mm files:
    - platform_windows.h/cpp (Win32 notifications, tray — stubs)
    - audio_capture_windows.h/cpp (WASAPI Loopback — stub)
    - video/video_capture_windows.h/cpp (Media Foundation camera — stub)
    - video/video_encoder_windows.h/cpp (Media Foundation H.264 — stub)
    - video/screen_capture_windows.h/cpp (DXGI Desktop Duplication — stub)
    - video/video_file_source_windows.h/cpp (FFmpeg/MF decode — stub)
  - Applied #ifdef _WIN32 patches to: stream_client.cpp, dnas_stats.cpp, rtmp_client.cpp, stream_target_editor.cpp, sftp_uploader.cpp, app.cpp, about_dialog.cpp, main_window.cpp,
  audio_pipeline.h, live_video_studio.cpp

  ## Prerequisites to verify/install before building
  1. **Qt 6 for MSVC 2022 x64** — set QTDIR env var:
     setx QTDIR "C:\Qt\6.x.x\msvc2022_64"
  (Replace 6.x.x with your actual Qt version, e.g. 6.8.0 or 6.9.0)

  2. **vcpkg codec libraries (x64-windows)**:
     vcpkg install lame:x64-windows libvorbis:x64-windows opus:x64-windows opusenc:x64-windows flac:x64-windows fdk-aac:x64-windows portaudio:x64-windows libyaml:x64-windows
  openssl:x64-windows
  Expected at: C:\vcpkg\installed\x64-windows\

  3. **Verify Qt MOC is accessible**:
     "%QTDIR%\bin\moc.exe" --version

  ## Build command
  MSBuild win-qt\Mcaster1DSPEncoder_Qt.vcxproj /p:Configuration=Debug /p:Platform=x64 /m

  Or build the full solution:
  MSBuild Mcaster1DSPEncoder_Master.sln /p:Configuration=Debug /p:Platform=x64 /m

  ## What I need you to do
  1. First check that QTDIR is set and the vcpkg libs exist at the expected paths
  2. Try the MSBuild command above
  3. Fix any compile errors — the most likely issues will be:
     - Missing Qt or codec include/lib paths (adjust QTDIR or vcpkg paths in vcxproj)
     - MOC custom build rules not finding headers (path issues)
     - Any remaining macOS-isms we missed in the #ifdef patches
  4. Get it to compile clean (0 errors) — warnings are OK for now
  5. Once it builds, try running Mcaster1DSPEncoder_Qt.exe to verify the window launches

  The vcxproj expects these lib paths:
  - Qt: $(QTDIR)\include, $(QTDIR)\lib
  - vcpkg: C:\vcpkg\installed\x64-windows\include, C:\vcpkg\installed\x64-windows\lib

  Linked libraries: Qt6Core.lib, Qt6Widgets.lib, Qt6Gui.lib, libmp3lame.lib, vorbis.lib, vorbisenc.lib, ogg.lib, opus.lib, opusenc.lib, FLAC.lib, fdk-aac.lib, portaudio.lib, yaml.lib,
  libssl.lib, libcrypto.lib, ws2_32.lib

  Preprocessor defines: _CRT_SECURE_NO_WARNINGS, WIN32_LEAN_AND_MEAN, NOMINMAX, _WIN32_WINNT=0x0A00, HAVE_LAME, HAVE_VORBIS, HAVE_FLAC, HAVE_FDKAAC, HAVE_OPUS

  ---

