dnl configure.ac — Mcaster1 DSP Encoder (Linux CLI + HTTP admin) v5.0
dnl
dnl ./autogen.sh  then  ./configure  then  make
dnl
dnl Options:
dnl   --with-webroot=DIR     Web UI directory (default: ${datadir}/mcaster1/web_ui)
dnl   --disable-portaudio    Skip PortAudio (HTTP-only test build)
dnl   --disable-encoder      Skip codec libs (HTTP-only test build)

AC_PREREQ([2.69])
AC_INIT([mcaster1-encoder], [5.0], [support@mcaster1.com])
AM_INIT_AUTOMAKE([foreign subdir-objects -Wall])
AC_CONFIG_SRCDIR([src/linux/main_cli.cpp])
AC_CONFIG_HEADERS([config.h])

dnl ── Language ────────────────────────────────────────────────────────────────
AC_PROG_CXX
AC_PROG_CC
AC_LANG([C++])
AX_CXX_COMPILE_STDVER([17], [mandatory])

dnl ── Basic OS / library checks ────────────────────────────────────────────────
AC_CHECK_HEADERS([unistd.h pthread.h])
AC_SEARCH_LIBS([pthread_create], [pthread], [],
    [AC_MSG_ERROR([pthreads not found. Install libpthread-dev])])

dnl ── OpenSSL (required for HTTPS and cert generation) ─────────────────────────
PKG_CHECK_MODULES([OPENSSL], [openssl >= 1.1.0],
    [AC_DEFINE([HAVE_OPENSSL], [1], [OpenSSL available])],
    [AC_MSG_ERROR([OpenSSL >= 1.1.0 required. Install: sudo apt install libssl-dev])])

dnl ── libyaml (required for YAML config parsing) ───────────────────────────────
PKG_CHECK_MODULES([LIBYAML], [yaml-0.1],
    [AC_DEFINE([HAVE_LIBYAML], [1], [libyaml available])],
    [
        dnl Fallback: try manual header+lib check
        AC_CHECK_HEADER([yaml.h], [],
            [AC_MSG_ERROR([libyaml not found. Install: sudo apt install libyaml-dev])])
        AC_CHECK_LIB([yaml], [yaml_parser_initialize], [LIBYAML_LIBS="-lyaml"],
            [AC_MSG_ERROR([libyaml not found. Install: sudo apt install libyaml-dev])])
        LIBYAML_CFLAGS=""
        AC_SUBST([LIBYAML_CFLAGS])
        AC_SUBST([LIBYAML_LIBS])
    ])

dnl ── PortAudio (optional — disable for HTTP-only test) ────────────────────────
AC_ARG_ENABLE([portaudio],
    [AS_HELP_STRING([--disable-portaudio], [Disable PortAudio (HTTP-only test build)])],
    [enable_portaudio=$enableval], [enable_portaudio=yes])

AM_CONDITIONAL([HAVE_PORTAUDIO], [false])
AS_IF([test "x$enable_portaudio" = "xyes"], [
    PKG_CHECK_MODULES([PORTAUDIO], [portaudio-2.0],
        [AM_CONDITIONAL([HAVE_PORTAUDIO], [true])
         AC_DEFINE([HAVE_PORTAUDIO], [1], [PortAudio available])],
        [AC_MSG_WARN([PortAudio not found — audio input disabled. Install: sudo apt install libportaudio2 portaudio19-dev])])
])

dnl ── Codec libraries (optional — disable for HTTP-only test) ──────────────────
AC_ARG_ENABLE([encoder],
    [AS_HELP_STRING([--disable-encoder], [Disable codec libs (HTTP-only test build)])],
    [enable_encoder=$enableval], [enable_encoder=yes])

AM_CONDITIONAL([BUILD_ENCODER], [test "x$enable_encoder" = "xyes"])

AS_IF([test "x$enable_encoder" = "xyes"], [
    dnl MP3 — LAME
    PKG_CHECK_MODULES([LAME], [lame], [],
        [AC_CHECK_LIB([mp3lame], [lame_init],
            [LAME_LIBS="-lmp3lame"; LAME_CFLAGS=""],
            [AC_MSG_WARN([LAME not found. Install: sudo apt install libmp3lame-dev])])])

    dnl Ogg / Vorbis
    PKG_CHECK_MODULES([VORBIS], [vorbisenc ogg], [],
        [AC_MSG_WARN([libvorbis not found. Install: sudo apt install libvorbis-dev libogg-dev])])

    dnl FLAC
    PKG_CHECK_MODULES([FLAC], [flac], [],
        [AC_MSG_WARN([FLAC not found. Install: sudo apt install libflac-dev])])

    dnl Opus
    PKG_CHECK_MODULES([OPUS], [opus], [],
        [AC_MSG_WARN([Opus not found. Install: sudo apt install libopus-dev])])

    dnl fdk-aac (optional)
    PKG_CHECK_MODULES([FDKAAC], [fdk-aac], [],
        [AC_MSG_WARN([fdk-aac not found (optional). Install: sudo apt install libfdk-aac-dev])])
])

dnl ── Audio metadata / media library (optional — for playlist mgmt + ID3/tags) ─
dnl    id3lib    — ID3v1/v2 tag reading/writing for MP3
dnl    taglib    — Cross-format tag library (MP3/OGG/FLAC/OPUS/AAC/M4A) — preferred
dnl    libvorbis — Ogg comment metadata (already checked above for encoding)
dnl    libavformat (FFmpeg) — universal audio/video metadata + duration probing

AC_ARG_ENABLE([medialib],
    [AS_HELP_STRING([--disable-medialib], [Disable audio metadata libs (playlist manager uses filename only)])],
    [enable_medialib=$enableval], [enable_medialib=yes])

AM_CONDITIONAL([HAVE_TAGLIB], [false])
AM_CONDITIONAL([HAVE_ID3LIB], [false])
AM_CONDITIONAL([HAVE_AVFORMAT], [false])

AS_IF([test "x$enable_medialib" = "xyes"], [

    dnl TagLib — preferred: handles MP3/OGG/FLAC/Opus/AAC/M4A/WavPack
    PKG_CHECK_MODULES([TAGLIB], [taglib >= 1.11],
        [AM_CONDITIONAL([HAVE_TAGLIB], [true])
         AC_DEFINE([HAVE_TAGLIB], [1], [TagLib available])],
        [AC_MSG_WARN([TagLib not found (optional). Install: sudo apt install libtag1-dev])])

    dnl id3lib — ID3v1/v2 for MP3 (fallback if no TagLib)
    AC_CHECK_LIB([id3], [ID3Tag_New],
        [AM_CONDITIONAL([HAVE_ID3LIB], [true])
         AC_DEFINE([HAVE_ID3LIB], [1], [id3lib available])
         ID3LIB_LIBS="-lid3"
         AC_SUBST([ID3LIB_LIBS])],
        [AC_MSG_WARN([id3lib not found (optional). Install: sudo apt install libid3-dev])])

    dnl libavformat (FFmpeg) — audio duration probing for playlist timing
    PKG_CHECK_MODULES([AVFORMAT], [libavformat libavcodec libavutil],
        [AM_CONDITIONAL([HAVE_AVFORMAT], [true])
         AC_DEFINE([HAVE_AVFORMAT], [1], [FFmpeg libavformat available])],
        [AC_MSG_WARN([FFmpeg libavformat not found (optional). Install: sudo apt install libavformat-dev])])
])

dnl ── Web UI install directory ─────────────────────────────────────────────────
AC_ARG_WITH([webroot],
    [AS_HELP_STRING([--with-webroot=DIR],
        [Web UI directory (default: DATADIR/mcaster1/web_ui)])],
    [WEBROOT="$withval"],
    [WEBROOT='${datadir}/mcaster1/web_ui'])
AC_SUBST([WEBROOT])

dnl ── External (vendor) header include path ────────────────────────────────────
VENDOR_CPPFLAGS="-I\$(top_srcdir)/external/include"
AC_SUBST([VENDOR_CPPFLAGS])

dnl ── Output ───────────────────────────────────────────────────────────────────
AC_CONFIG_FILES([
    Makefile
    src/linux/Makefile
])
AC_OUTPUT

echo ""
echo "── Mcaster1 DSP Encoder v5.0 configuration ────────────────────────────────"
echo "   Prefix      : $(eval echo ${prefix})"
echo "   Web UI dir  : $(eval echo ${WEBROOT})"
echo "   OpenSSL     : yes"
echo "   PortAudio   : ${enable_portaudio}"
echo "   Encoder     : ${enable_encoder}"
echo "   Media Lib   : ${enable_medialib} (TagLib/id3lib/libavformat)"
echo ""
echo "   Run: make -j\$(nproc)"
echo ""
