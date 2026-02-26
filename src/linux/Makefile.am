# src/linux/Makefile.am — mcaster1-encoder binary (Phase 4 / v1.4.0)
#
# HTTP-only test build:  ./configure --disable-encoder --disable-portaudio
# Full encoder build:    ./configure
#
# Build from project root:
#   ./autogen.sh
#   ./configure [--prefix=/usr/local] [--with-webroot=/usr/share/mcaster1/web_ui]
#   make -j$(nproc)

bin_PROGRAMS = mcaster1-encoder

# ── Include paths ──────────────────────────────────────────────────────────────
# src/linux/external/include — Linux-only cross-platform vendor headers:
#   httplib.h        (cpp-httplib v0.18, header-only HTTP server)
#   nlohmann/json.hpp (nlohmann/json v3.11, header-only JSON)
# These are the only two headers not available as system packages.
# Windows-specific vendor headers live in external/ at the project root — not used here.
AM_CPPFLAGS = \
    -I$(srcdir)/external/include \
    -I$(top_srcdir)/src \
    -I$(top_srcdir)/src/linux \
    -I$(top_srcdir)/src/libmcaster1dspencoder \
    -DCPPHTTPLIB_OPENSSL_SUPPORT \
    $(OPENSSL_CFLAGS) \
    $(LIBYAML_CFLAGS) \
    $(MARIADB_CFLAGS)

AM_CXXFLAGS = -std=c++17 -Wall -Wno-unused-parameter -O2
AM_LDFLAGS  = -rdynamic

# ── Core sources (always compiled) ────────────────────────────────────────────
mcaster1_encoder_SOURCES = \
    main_cli.cpp \
    http_api.cpp \
    fastcgi_client.cpp \
    audio_pipeline.cpp \
    encoder_slot.cpp \
    audio_source.cpp \
    file_source.cpp \
    playlist_parser.cpp \
    stream_client.cpp \
    archive_writer.cpp \
    config_loader.cpp \
    mc1_db.cpp \
    system_health.cpp \
    server_monitors.cpp \
    dsp/eq.cpp \
    dsp/agc.cpp \
    dsp/crossfader.cpp \
    dsp/dsp_chain.cpp

# ── Required libraries ────────────────────────────────────────────────────────
mcaster1_encoder_LDADD = \
    $(OPENSSL_LIBS) \
    $(LIBYAML_LIBS) \
    $(MARIADB_LIBS) \
    -lpthread \
    -lcrypt \
    -lm

# ── PortAudio (optional) ──────────────────────────────────────────────────────
# Source files use #ifdef HAVE_PORTAUDIO (not config.h), so we pass -D explicitly.
if HAVE_PORTAUDIO
AM_CPPFLAGS += $(PORTAUDIO_CFLAGS) -DHAVE_PORTAUDIO
mcaster1_encoder_LDADD += $(PORTAUDIO_LIBS)
endif

# ── Per-codec (optional) ──────────────────────────────────────────────────────
# Source files test #ifdef HAVE_LAME / HAVE_VORBIS etc. directly.
# config.h has these defines but sources don't include config.h, so pass -D flags.
if HAVE_LAME
AM_CPPFLAGS += $(LAME_CFLAGS) -DHAVE_LAME
mcaster1_encoder_LDADD += $(LAME_LIBS)
endif

if HAVE_VORBIS
AM_CPPFLAGS += $(VORBIS_CFLAGS) -DHAVE_VORBIS
mcaster1_encoder_LDADD += $(VORBIS_LIBS)
endif

if HAVE_FLAC
AM_CPPFLAGS += $(FLAC_CFLAGS) -DHAVE_FLAC
mcaster1_encoder_LDADD += $(FLAC_LIBS)
endif

if HAVE_OPUS
AM_CPPFLAGS += $(OPUS_CFLAGS) -DHAVE_OPUS
mcaster1_encoder_LDADD += $(OPUS_LIBS)
endif

if HAVE_FDKAAC
AM_CPPFLAGS += $(FDKAAC_CFLAGS) -DHAVE_FDK_AAC
mcaster1_encoder_LDADD += $(FDKAAC_LIBS)
endif

# ── Media / metadata libs (optional) ──────────────────────────────────────────
if HAVE_MPG123
AM_CPPFLAGS += $(MPG123_CFLAGS) -DHAVE_MPG123
mcaster1_encoder_LDADD += $(MPG123_LIBS)
endif

if HAVE_AVFORMAT
AM_CPPFLAGS += $(AVFORMAT_CFLAGS) -DHAVE_AVFORMAT
mcaster1_encoder_LDADD += $(AVFORMAT_LIBS)
endif

if HAVE_TAGLIB
AM_CPPFLAGS += $(TAGLIB_CFLAGS) -DHAVE_TAGLIB
mcaster1_encoder_LDADD += $(TAGLIB_LIBS)
endif

# ── Web UI install ─────────────────────────────────────────────────────────────
# $(WEBROOT) is the top-level web UI directory (default: ${datadir}/mcaster1/web_ui)
# Files at web_ui/*.{html,css,js,php} → $(WEBROOT)/
# Files at web_ui/app/api/*.php       → $(WEBROOT)/app/api/
# Files at web_ui/app/inc/*.php       → $(WEBROOT)/app/inc/
webuidir = $(WEBROOT)
dist_webuiDATA = \
    web_ui/login.html \
    web_ui/index.html \
    web_ui/style.css \
    web_ui/app.js \
    web_ui/dashboard.php \
    web_ui/encoders.php \
    web_ui/media.php \
    web_ui/metrics.php \
    web_ui/playlists.php \
    web_ui/settings.php

appapi_webuidir = $(WEBROOT)/app/api
dist_appapi_webuiDATA = \
    web_ui/app/api/tracks.php \
    web_ui/app/api/metrics.php \
    web_ui/app/api/playlists.php \
    web_ui/app/api/encoders.php \
    web_ui/app/api/auth.php

appinc_webuidir = $(WEBROOT)/app/inc
dist_appinc_webuiDATA = \
    web_ui/app/inc/header.php \
    web_ui/app/inc/footer.php \
    web_ui/app/inc/auth.php \
    web_ui/app/inc/user_auth.php \
    web_ui/app/inc/db.php \
    web_ui/app/inc/logger.php \
    web_ui/app/inc/mc1_config.php \
    web_ui/app/inc/traits.db.class.php

# ── Extra dist (build scripts + legacy) ───────────────────────────────────────
EXTRA_DIST = \
    make_phase4.sh \
    make_phase3.sh \
    make_phase2.sh \
    CLAUDE.md
