# src/linux/Makefile.am — Mcaster1DSPEncoder dual-binary build (v1.5.0)
#
# Two binaries:
#   mcaster1-dsp-encoder-admin  — Web UI, API, auth, supervisor (always-up)
#   mcaster1-dsp-encoder        — Audio pipeline, DSP, codecs, streaming (restartable)
#
# Build from project root:
#   ./autogen.sh
#   ./configure
#   make -j$(nproc)

bin_PROGRAMS = mcaster1-dsp-encoder-admin mcaster1-dsp-encoder mcaster1-voictune

# ── Include paths ──────────────────────────────────────────────────────────────
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

# ══════════════════════════════════════════════════════════════════════════════
# ADMIN BINARY — Web UI, FastCGI, auth, supervisor, system health
# ══════════════════════════════════════════════════════════════════════════════

mcaster1_dsp_encoder_admin_CPPFLAGS = $(AM_CPPFLAGS) -DMC1_ADMIN_BUILD
mcaster1_dsp_encoder_admin_SOURCES = \
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
    process_supervisor.cpp \
    ipc_proxy.cpp \
    clockwheel_scheduler.cpp \
    dsp/eq.cpp \
    dsp/agc.cpp \
    dsp/crossfader.cpp \
    dsp/dj_crossfader.cpp \
    dsp/dsp_chain.cpp \
    dsp/effects_rack.cpp \
    dsp/sidechain_ducker.cpp \
    dsp/dead_air_detector.cpp

# Admin needs these shared DSP headers for static curve data / effects type listing
# but does NOT need the audio processing source files.

mcaster1_dsp_encoder_admin_LDADD = \
    $(OPENSSL_LIBS) \
    $(LIBYAML_LIBS) \
    $(MARIADB_LIBS) \
    -lpthread \
    -lcrypt \
    -lm

# Admin links all codec/audio libs too (shares source files with encoder for symbols).
# Audio code never executes in admin — g_pipeline is null. Fault isolation comes from
# the ProcessSupervisor running the encoder as a separate child process.
if HAVE_JACK
mcaster1_dsp_encoder_admin_SOURCES += jack_manager.cpp
mcaster1_dsp_encoder_admin_LDADD += $(JACK_LIBS)
endif

if HAVE_PORTAUDIO
mcaster1_dsp_encoder_admin_LDADD += $(PORTAUDIO_LIBS)
endif

if HAVE_LAME
mcaster1_dsp_encoder_admin_LDADD += $(LAME_LIBS)
endif

if HAVE_VORBIS
mcaster1_dsp_encoder_admin_LDADD += $(VORBIS_LIBS)
endif

if HAVE_FLAC
mcaster1_dsp_encoder_admin_LDADD += $(FLAC_LIBS)
endif

if HAVE_OPUS
mcaster1_dsp_encoder_admin_LDADD += $(OPUS_LIBS)
endif

if HAVE_FDKAAC
mcaster1_dsp_encoder_admin_LDADD += $(FDKAAC_LIBS)
endif

if HAVE_MPG123
mcaster1_dsp_encoder_admin_LDADD += $(MPG123_LIBS)
endif

if HAVE_AVFORMAT
mcaster1_dsp_encoder_admin_LDADD += $(AVFORMAT_LIBS)
endif

if HAVE_TAGLIB
mcaster1_dsp_encoder_admin_LDADD += $(TAGLIB_LIBS)
endif

# ══════════════════════════════════════════════════════════════════════════════
# ENCODER BINARY — Audio pipeline, DSP, codecs, streaming (restartable worker)
# ══════════════════════════════════════════════════════════════════════════════

mcaster1_dsp_encoder_SOURCES = \
    main_encoder.cpp \
    encoder_ipc_api.cpp \
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
    dsp/dj_crossfader.cpp \
    dsp/dsp_chain.cpp \
    dsp/effects_rack.cpp \
    dsp/sidechain_ducker.cpp \
    dsp/dead_air_detector.cpp \
    clockwheel_scheduler.cpp

mcaster1_dsp_encoder_LDADD = \
    $(OPENSSL_LIBS) \
    $(LIBYAML_LIBS) \
    $(MARIADB_LIBS) \
    -lpthread \
    -lcrypt \
    -lm

# ── PortAudio (optional — encoder only) ──────────────────────────────────────
if HAVE_PORTAUDIO
mcaster1_dsp_encoder_LDADD += $(PORTAUDIO_LIBS)
endif

# ── JACK (encoder also needs it — audio_pipeline.h includes jack_manager.h) ──
if HAVE_JACK
mcaster1_dsp_encoder_SOURCES += jack_manager.cpp
mcaster1_dsp_encoder_LDADD += $(JACK_LIBS)
endif

# ── Per-codec (optional — encoder only) ──────────────────────────────────────
if HAVE_LAME
AM_CPPFLAGS += $(LAME_CFLAGS) -DHAVE_LAME
mcaster1_dsp_encoder_LDADD += $(LAME_LIBS)
endif

if HAVE_VORBIS
AM_CPPFLAGS += $(VORBIS_CFLAGS) -DHAVE_VORBIS
mcaster1_dsp_encoder_LDADD += $(VORBIS_LIBS)
endif

if HAVE_FLAC
AM_CPPFLAGS += $(FLAC_CFLAGS) -DHAVE_FLAC
mcaster1_dsp_encoder_LDADD += $(FLAC_LIBS)
endif

if HAVE_OPUS
AM_CPPFLAGS += $(OPUS_CFLAGS) -DHAVE_OPUS
mcaster1_dsp_encoder_LDADD += $(OPUS_LIBS)
endif

if HAVE_FDKAAC
AM_CPPFLAGS += $(FDKAAC_CFLAGS) -DHAVE_FDK_AAC
mcaster1_dsp_encoder_LDADD += $(FDKAAC_LIBS)
endif

# ── Media / metadata libs (optional — encoder only) ──────────────────────────
if HAVE_MPG123
AM_CPPFLAGS += $(MPG123_CFLAGS) -DHAVE_MPG123
mcaster1_dsp_encoder_LDADD += $(MPG123_LIBS)
endif

if HAVE_AVFORMAT
AM_CPPFLAGS += $(AVFORMAT_CFLAGS) -DHAVE_AVFORMAT
mcaster1_dsp_encoder_LDADD += $(AVFORMAT_LIBS)
endif

if HAVE_TAGLIB
AM_CPPFLAGS += $(TAGLIB_CFLAGS) -DHAVE_TAGLIB
mcaster1_dsp_encoder_LDADD += $(TAGLIB_LIBS)
endif

# ── Web UI install ─────────────────────────────────────────────────────────────
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
    web_ui/settings.php \
    web_ui/crossfader.php \
    web_ui/effects-rack.php \
    web_ui/jack.php \
    web_ui/dualdeck-player.php

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

# ══════════════════════════════════════════════════════════════════════════════
# VOICTUNE BINARY — Voice Analysis & Coaching Daemon (Phase VT)
# ══════════════════════════════════════════════════════════════════════════════

mcaster1_voictune_CPPFLAGS = $(AM_CPPFLAGS) -DMC1_VOICTUNE_BUILD
mcaster1_voictune_SOURCES = \
    voictune/main_voictune.cpp \
    voictune/vt_http_api.cpp \
    voictune/vt_config.cpp

mcaster1_voictune_LDADD = \
    $(OPENSSL_LIBS) \
    $(LIBYAML_LIBS) \
    $(MARIADB_LIBS) \
    -lpthread \
    -lcrypt \
    -lm

# VoicTune uses PortAudio for mic capture (optional — can run browser-mic-only mode)
if HAVE_PORTAUDIO
mcaster1_voictune_LDADD += $(PORTAUDIO_LIBS)
endif

# VoicTune uses JACK for advanced audio routing (optional)
if HAVE_JACK
mcaster1_voictune_LDADD += $(JACK_LIBS)
endif

# ── Extra dist ─────────────────────────────────────────────────────────────────
EXTRA_DIST = \
    make_phase4.sh \
    make_phase3.sh \
    make_phase2.sh \
    CLAUDE.md
