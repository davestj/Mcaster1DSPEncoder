# src/linux/Makefile.am — mcaster1-encoder binary (Phase 3)
#
# HTTP-only test build:  ./configure --disable-encoder --disable-portaudio
# Full encoder build:    ./configure

bin_PROGRAMS = mcaster1-encoder

# ── Include paths ─────────────────────────────────────────────────────────────
AM_CPPFLAGS = \
    $(VENDOR_CPPFLAGS) \
    -I$(top_srcdir)/src \
    -I$(top_srcdir)/src/linux \
    -I$(top_srcdir)/src/libmcaster1dspencoder \
    -DCPPHTTPLIB_OPENSSL_SUPPORT \
    $(OPENSSL_CFLAGS) \
    $(LIBYAML_CFLAGS)

AM_CXXFLAGS = -std=c++17 -Wall -Wno-unused-parameter -O2

# ── Core HTTP + CLI sources (always compiled) ─────────────────────────────────
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
    config_stub.cpp \
    $(top_srcdir)/src/podcast_rss_gen.cpp

# ── Full encoder sources (when --disable-encoder is NOT set) ─────────────────
if BUILD_ENCODER
mcaster1_encoder_SOURCES += \
    $(top_srcdir)/src/libmcaster1dspencoder/libmcaster1dspencoder.cpp \
    $(top_srcdir)/src/libmcaster1dspencoder/Socket.cpp \
    $(top_srcdir)/src/log.cpp
endif

# ── Libraries ─────────────────────────────────────────────────────────────────
mcaster1_encoder_LDADD = \
    $(OPENSSL_LIBS) \
    $(LIBYAML_LIBS) \
    -lpthread

if HAVE_PORTAUDIO
AM_CPPFLAGS += $(PORTAUDIO_CFLAGS) -DHAVE_PORTAUDIO
mcaster1_encoder_LDADD += $(PORTAUDIO_LIBS)
endif

if BUILD_ENCODER
AM_CPPFLAGS += -DHAVE_LAME -DHAVE_VORBIS -DHAVE_OPUS -DHAVE_FLAC
mcaster1_encoder_LDADD += \
    $(LAME_LIBS) \
    $(VORBIS_LIBS) \
    $(FLAC_LIBS) \
    $(OPUS_LIBS) \
    $(FDKAAC_LIBS)
endif

if HAVE_AVFORMAT
AM_CPPFLAGS += $(AVFORMAT_CFLAGS) -DHAVE_AVFORMAT
mcaster1_encoder_LDADD += $(AVFORMAT_LIBS)
endif

if HAVE_TAGLIB
AM_CPPFLAGS += $(TAGLIB_CFLAGS) -DHAVE_TAGLIB
mcaster1_encoder_LDADD += $(TAGLIB_LIBS)
endif

# ── libmpg123 (MP3 file decode) ───────────────────────────────────────────────
# Detected via pkg-config in configure.ac when HAVE_MPG123 is set
if HAVE_MPG123
AM_CPPFLAGS += $(MPG123_CFLAGS) -DHAVE_MPG123
mcaster1_encoder_LDADD += $(MPG123_LIBS)
endif

# ── Web UI install ────────────────────────────────────────────────────────────
webuidir = $(WEBROOT)
nobase_webuiDATA = \
    web_ui/index.html \
    web_ui/login.html \
    web_ui/style.css \
    web_ui/app.js

EXTRA_DIST = \
    make_http_test.sh \
    make_phase3.sh \
    $(nobase_webuiDATA)
