dnl configure.ac — Mcaster1 DSP Encoder (Linux CLI + HTTP admin) v1.4.0
dnl
dnl ./autogen.sh  then  ./configure  then  make -j$(nproc)
dnl
dnl Options:
dnl   --with-webroot=DIR         Web UI directory (default: ${datadir}/mcaster1/web_ui)
dnl   --disable-portaudio        Skip PortAudio (HTTP-only test build)
dnl   --disable-encoder          Skip codec libs (HTTP-only test build)
dnl   --disable-medialib         Skip TagLib/avformat metadata libs

AC_PREREQ([2.69])
AC_INIT([mcaster1-encoder], [1.4.0], [support@mcaster1.com])
AM_INIT_AUTOMAKE([foreign subdir-objects -Wall])
AC_CONFIG_SRCDIR([src/linux/main_cli.cpp])
AC_CONFIG_HEADERS([config.h])

dnl ── Language / C++17 ────────────────────────────────────────────────────────
AC_PROG_CXX
AC_PROG_CC
AC_PROG_OBJCXX
AC_LANG([C++])
dnl AX_CXX_COMPILE_STDCXX is from autoconf-archive (ax_cxx_compile_stdcxx.m4)
AX_CXX_COMPILE_STDCXX([17], [noext], [mandatory])

dnl ── Basic OS / library checks ────────────────────────────────────────────────
AC_CHECK_HEADERS([unistd.h pthread.h])
AC_SEARCH_LIBS([pthread_create], [pthread], [],
    [AC_MSG_ERROR([pthreads not found. Install libpthread-dev])])

dnl ── Extend PKG_CONFIG_PATH so fdk-aac installed to /usr/local is found ───────
PKG_CONFIG_PATH="/usr/local/lib/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
export PKG_CONFIG_PATH

dnl ── OpenSSL (required for HTTPS and cert generation) ─────────────────────────
PKG_CHECK_MODULES([OPENSSL], [openssl >= 1.1.0],
    [AC_DEFINE([HAVE_OPENSSL], [1], [OpenSSL available])],
    [AC_MSG_ERROR([OpenSSL >= 1.1.0 required. Install: sudo apt install libssl-dev])])

dnl ── libyaml (required for YAML config parsing) ───────────────────────────────
have_libyaml=no
PKG_CHECK_MODULES([LIBYAML], [yaml-0.1],
    [have_libyaml=yes
     AC_DEFINE([HAVE_LIBYAML], [1], [libyaml available])],
    [AC_CHECK_HEADER([yaml.h],
        [AC_CHECK_LIB([yaml], [yaml_parser_initialize],
            [have_libyaml=yes
             LIBYAML_LIBS="-lyaml"
             LIBYAML_CFLAGS=""
             AC_SUBST([LIBYAML_LIBS])
             AC_SUBST([LIBYAML_CFLAGS])],
            [AC_MSG_ERROR([libyaml not found. Install: sudo apt install libyaml-dev])])],
        [AC_MSG_ERROR([yaml.h not found. Install: sudo apt install libyaml-dev])])])

dnl ── MariaDB / MySQL C client (required — encoder loads slots from DB) ─────────
have_mariadb=no
PKG_CHECK_MODULES([MARIADB], [libmariadb],
    [have_mariadb=yes
     AC_DEFINE([HAVE_MARIADB], [1], [MariaDB Connector/C available])],
    [PKG_CHECK_MODULES([MARIADB], [mariadb],
        [have_mariadb=yes
         AC_DEFINE([HAVE_MARIADB], [1], [MariaDB Connector/C available])],
        [AC_CHECK_HEADER([mysql/mysql.h],
            [AC_CHECK_LIB([mariadb], [mysql_init],
                [have_mariadb=yes
                 AC_DEFINE([HAVE_MARIADB], [1], [MariaDB Connector/C available])
                 MARIADB_LIBS="-lmariadb"
                 MARIADB_CFLAGS="-I/usr/include/mysql"
                 AC_SUBST([MARIADB_LIBS])
                 AC_SUBST([MARIADB_CFLAGS])],
                [AC_MSG_ERROR([libmariadb not found. Install: sudo apt install libmariadb-dev])])],
            [AC_MSG_ERROR([MariaDB headers not found. Install: sudo apt install libmariadb-dev])])])])

dnl ── PortAudio (optional — disable for HTTP-only test) ────────────────────────
AC_ARG_ENABLE([portaudio],
    [AS_HELP_STRING([--disable-portaudio], [Disable PortAudio (no audio device capture)])],
    [enable_portaudio=$enableval], [enable_portaudio=yes])

have_portaudio=no
AS_IF([test "x$enable_portaudio" = "xyes"], [
    PKG_CHECK_MODULES([PORTAUDIO], [portaudio-2.0],
        [have_portaudio=yes
         AC_DEFINE([HAVE_PORTAUDIO], [1], [PortAudio available])],
        [AC_MSG_WARN([PortAudio not found — audio device capture disabled. Install: sudo apt install portaudio19-dev])])
])
AM_CONDITIONAL([HAVE_PORTAUDIO], [test "x$have_portaudio" = "xyes"])

dnl ── Codec libraries (optional — disable for HTTP-only test) ──────────────────
AC_ARG_ENABLE([encoder],
    [AS_HELP_STRING([--disable-encoder], [Disable codec libs (HTTP-only test build)])],
    [enable_encoder=$enableval], [enable_encoder=yes])

AM_CONDITIONAL([BUILD_ENCODER], [test "x$enable_encoder" = "xyes"])

dnl Per-codec detection — each gets its own AM_CONDITIONAL so Makefile.am
dnl can add only the CFLAGS/LDADD for codecs that were actually found.
have_lame=no
have_vorbis=no
have_flac=no
have_opus=no
have_fdkaac=no

AS_IF([test "x$enable_encoder" = "xyes"], [

    dnl ── LAME (MP3 encoding) ──────────────────────────────────────────────────
    PKG_CHECK_MODULES([LAME], [lame],
        [have_lame=yes
         AC_DEFINE([HAVE_LAME], [1], [LAME MP3 encoder available])],
        [AC_CHECK_HEADER([lame/lame.h],
            [AC_CHECK_LIB([mp3lame], [lame_init],
                [have_lame=yes
                 AC_DEFINE([HAVE_LAME], [1], [LAME MP3 encoder available])
                 LAME_LIBS="-lmp3lame"
                 LAME_CFLAGS=""
                 AC_SUBST([LAME_LIBS])
                 AC_SUBST([LAME_CFLAGS])],
                [AC_MSG_WARN([LAME not found. Install: sudo apt install libmp3lame-dev])])],
            [AC_MSG_WARN([lame/lame.h not found. Install: sudo apt install libmp3lame-dev])])])

    dnl ── Ogg + Vorbis (Ogg/Vorbis encoding) ──────────────────────────────────
    PKG_CHECK_MODULES([VORBIS], [vorbisenc ogg vorbis],
        [have_vorbis=yes
         AC_DEFINE([HAVE_VORBIS], [1], [libvorbis available])],
        [AC_MSG_WARN([libvorbisenc/libogg not found. Install: sudo apt install libvorbis-dev libogg-dev])])

    dnl ── FLAC ─────────────────────────────────────────────────────────────────
    PKG_CHECK_MODULES([FLAC], [flac],
        [have_flac=yes
         AC_DEFINE([HAVE_FLAC], [1], [FLAC encoder available])],
        [AC_MSG_WARN([libflac not found. Install: sudo apt install libflac-dev])])

    dnl ── Opus (libopusenc for Ogg/Opus container) ──────────────────────────────
    PKG_CHECK_MODULES([OPUS], [libopusenc],
        [have_opus=yes
         AC_DEFINE([HAVE_OPUS], [1], [libopusenc available])],
        [PKG_CHECK_MODULES([OPUS], [opus],
            [have_opus=yes
             AC_DEFINE([HAVE_OPUS], [1], [libopus available])],
            [AC_MSG_WARN([libopusenc/libopus not found. Install: sudo apt install libopus-dev libopusenc-dev])])])

    dnl ── fdk-aac (AAC-LC / HE-AAC v1+v2 / AAC-ELD) ──────────────────────────
    PKG_CHECK_MODULES([FDKAAC], [fdk-aac],
        [have_fdkaac=yes
         AC_DEFINE([HAVE_FDK_AAC], [1], [fdk-aac available])],
        [AC_CHECK_HEADER([fdk-aac/aacenc_lib.h],
            [AC_CHECK_LIB([fdk-aac], [aacEncOpen],
                [have_fdkaac=yes
                 AC_DEFINE([HAVE_FDK_AAC], [1], [fdk-aac available])
                 FDKAAC_LIBS="-lfdk-aac"
                 FDKAAC_CFLAGS="-I/usr/local/include"
                 AC_SUBST([FDKAAC_LIBS])
                 AC_SUBST([FDKAAC_CFLAGS])],
                [AC_MSG_WARN([fdk-aac not found. Build from: github.com/mstorsjo/fdk-aac])])],
            [AC_MSG_WARN([fdk-aac headers not found (AAC encoding disabled). Build from source.])])])
])

AM_CONDITIONAL([HAVE_LAME],   [test "x$have_lame"   = "xyes"])
AM_CONDITIONAL([HAVE_VORBIS], [test "x$have_vorbis" = "xyes"])
AM_CONDITIONAL([HAVE_FLAC],   [test "x$have_flac"   = "xyes"])
AM_CONDITIONAL([HAVE_OPUS],   [test "x$have_opus"   = "xyes"])
AM_CONDITIONAL([HAVE_FDKAAC], [test "x$have_fdkaac" = "xyes"])

dnl ── Media / metadata libs ────────────────────────────────────────────────────
AC_ARG_ENABLE([medialib],
    [AS_HELP_STRING([--disable-medialib], [Disable audio metadata libs (filename-only playlist)])],
    [enable_medialib=$enableval], [enable_medialib=yes])

have_taglib=no
have_mpg123=no
have_avformat=no

AS_IF([test "x$enable_medialib" = "xyes"], [

    dnl ── TagLib (cross-format tag reading: MP3/OGG/FLAC/Opus/AAC/M4A) ─────────
    PKG_CHECK_MODULES([TAGLIB], [taglib >= 1.11],
        [have_taglib=yes
         AC_DEFINE([HAVE_TAGLIB], [1], [TagLib available])],
        [AC_MSG_WARN([TagLib >= 1.11 not found (optional). Install: sudo apt install libtag1-dev])])

    dnl ── libmpg123 (fast MP3 file decode) ─────────────────────────────────────
    PKG_CHECK_MODULES([MPG123], [libmpg123],
        [have_mpg123=yes
         AC_DEFINE([HAVE_MPG123], [1], [libmpg123 available])],
        [AC_MSG_WARN([libmpg123 not found (optional). Install: sudo apt install libmpg123-dev])])

    dnl ── FFmpeg: libavformat + libavcodec + libavutil + libswresample ──────────
    dnl    Used for decoding any audio format FileSource doesn't handle via mpg123.
    PKG_CHECK_MODULES([AVFORMAT],
        [libavformat libavcodec libavutil libswresample],
        [have_avformat=yes
         AC_DEFINE([HAVE_AVFORMAT], [1], [FFmpeg libavformat available])],
        [AC_MSG_WARN([FFmpeg not found (optional). Install: sudo apt install libavformat-dev libswresample-dev])])
])

AM_CONDITIONAL([HAVE_TAGLIB],   [test "x$have_taglib"   = "xyes"])
AM_CONDITIONAL([HAVE_MPG123],   [test "x$have_mpg123"   = "xyes"])
AM_CONDITIONAL([HAVE_AVFORMAT], [test "x$have_avformat" = "xyes"])

dnl ── macOS Qt 6 GUI (optional) ───────────────────────────────────────────
AC_ARG_ENABLE([macos-gui],
    [AS_HELP_STRING([--enable-macos-gui], [Build macOS Qt 6 GUI application])],
    [enable_macos_gui=$enableval], [enable_macos_gui=no])

have_qt6=no
AS_IF([test "x$enable_macos_gui" = "xyes"], [
    PKG_CHECK_MODULES([QT6WIDGETS], [Qt6Widgets >= 6.5],
        [have_qt6=yes],
        [AC_MSG_ERROR([Qt 6 Widgets >= 6.5 not found. Install: brew install qt])])
    PKG_CHECK_MODULES([QT6CORE], [Qt6Core >= 6.5])
    PKG_CHECK_MODULES([QT6GUI],  [Qt6Gui  >= 6.5])

    dnl Locate moc and rcc from Qt installation
    dnl Try host_bins first (standard), then libexecdir (Homebrew), then prefix fallback
    QT6_HOST_BINS=`$PKG_CONFIG --variable=host_bins Qt6Core 2>/dev/null`
    if test -z "$QT6_HOST_BINS" || test ! -x "$QT6_HOST_BINS/moc"; then
        QT6_HOST_BINS=`$PKG_CONFIG --variable=libexecdir Qt6Core 2>/dev/null`
    fi
    if test -z "$QT6_HOST_BINS" || test ! -x "$QT6_HOST_BINS/moc"; then
        QT6_HOST_BINS=`$PKG_CONFIG --variable=prefix Qt6Core`/share/qt/libexec
    fi
    QT6_MOC="${QT6_HOST_BINS}/moc"
    QT6_RCC="${QT6_HOST_BINS}/rcc"
    if test ! -x "$QT6_MOC"; then
        AC_MSG_ERROR([Qt6 moc not found at $QT6_MOC. Check your Qt installation.])
    fi
    AC_SUBST([QT6_MOC])
    AC_SUBST([QT6_RCC])
    AC_SUBST([QT6WIDGETS_CFLAGS])
    AC_SUBST([QT6WIDGETS_LIBS])
    AC_SUBST([QT6CORE_CFLAGS])
    AC_SUBST([QT6CORE_LIBS])
    AC_SUBST([QT6GUI_CFLAGS])
    AC_SUBST([QT6GUI_LIBS])

    dnl macOS system frameworks for audio/video capture
    MACOS_FRAMEWORKS="-framework CoreAudio -framework AudioToolbox -framework AVFoundation -framework CoreMedia -framework CoreVideo -framework VideoToolbox -framework ScreenCaptureKit -framework Security -framework Cocoa -framework UserNotifications"
    AC_SUBST([MACOS_FRAMEWORKS])
    AC_DEFINE([MC1_MACOS_GUI], [1], [Building macOS Qt 6 GUI])
])
AM_CONDITIONAL([ENABLE_MACOS_GUI], [test "x$enable_macos_gui" = "xyes"])

dnl ── libvpx (optional — VP8/VP9 video encoding) ────────────────────────────
have_vpx=no
PKG_CHECK_MODULES([VPX], [vpx],
    [have_vpx=yes
     AC_DEFINE([HAVE_VPX], [1], [libvpx available for VP8/VP9 encoding])],
    [AC_MSG_NOTICE([libvpx not found — VP8/VP9 video encoding will be disabled])])
AM_CONDITIONAL([HAVE_VPX], [test "x$have_vpx" = "xyes"])

dnl ── libssh2 (optional — SFTP podcast upload) ──────────────────────────────
have_libssh2=no
PKG_CHECK_MODULES([LIBSSH2], [libssh2],
    [have_libssh2=yes
     AC_DEFINE([HAVE_LIBSSH2], [1], [libssh2 available for SFTP])],
    [AC_MSG_NOTICE([libssh2 not found — SFTP podcast upload will be disabled])])
AM_CONDITIONAL([HAVE_LIBSSH2], [test "x$have_libssh2" = "xyes"])

dnl ── libcurl (optional — HTTP REST API for podcast publishing) ─────────────
have_libcurl=no
PKG_CHECK_MODULES([LIBCURL], [libcurl],
    [have_libcurl=yes
     AC_DEFINE([HAVE_LIBCURL], [1], [libcurl available for HTTP REST APIs])],
    [AC_MSG_NOTICE([libcurl not found — REST API podcast publishing will be disabled])])
AM_CONDITIONAL([HAVE_LIBCURL], [test "x$have_libcurl" = "xyes"])

dnl ── Web UI install directory ─────────────────────────────────────────────────
AC_ARG_WITH([webroot],
    [AS_HELP_STRING([--with-webroot=DIR],
        [Web UI directory (default: DATADIR/mcaster1/web_ui)])],
    [WEBROOT="$withval"],
    [WEBROOT='${datadir}/mcaster1/web_ui'])
AC_SUBST([WEBROOT])

dnl ── Linux vendor headers: src/linux/external/include ─────────────────────────
dnl    httplib.h + nlohmann/json.hpp live there (header-only, not in system repos)
dnl    Windows vendor headers live in external/ at project root — not referenced here

dnl ── Output ───────────────────────────────────────────────────────────────────
AC_CONFIG_FILES([
    Makefile
    src/linux/Makefile
])
if test "x$enable_macos_gui" = "xyes"; then
    AC_CONFIG_FILES([src/macos/Makefile])
fi
AC_OUTPUT

echo ""
echo "── Mcaster1 DSP Encoder v1.4.0 configuration ──────────────────────────────"
echo "   Prefix      : $(eval echo ${prefix})"
echo "   Web UI dir  : $(eval echo ${WEBROOT})"
echo "   OpenSSL     : yes"
echo "   MariaDB     : ${have_mariadb}"
echo "   PortAudio   : ${have_portaudio}"
echo "   Encoder     : ${enable_encoder}"
echo "     LAME      : ${have_lame}"
echo "     Vorbis    : ${have_vorbis}"
echo "     FLAC      : ${have_flac}"
echo "     Opus      : ${have_opus}"
echo "     FDK-AAC   : ${have_fdkaac}"
echo "   Media libs  : ${enable_medialib}"
echo "     TagLib    : ${have_taglib}"
echo "     mpg123    : ${have_mpg123}"
echo "     FFmpeg    : ${have_avformat}"
echo ""
echo "   macOS GUI  : ${enable_macos_gui}"
if test "x$enable_macos_gui" = "xyes"; then
echo "     Qt 6     : ${have_qt6}"
echo "     MOC      : ${QT6_MOC}"
echo "     RCC      : ${QT6_RCC}"
fi
echo "   libvpx     : ${have_vpx} (VP8/VP9 video encoding)"
echo "   libssh2    : ${have_libssh2} (SFTP podcast upload)"
echo "   libcurl    : ${have_libcurl} (REST API podcast publishing)"
echo ""
echo "   Run: make -j\$(nproc)"
echo "   Binary: src/linux/mcaster1-encoder  (or \$(prefix)/bin after make install)"
if test "x$enable_macos_gui" = "xyes"; then
echo "   macOS GUI: src/macos/mcaster1-encoder-gui"
fi
echo ""
