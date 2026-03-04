# Makefile.am — Mcaster1 DSP Encoder macOS Qt 6 GUI
#
# Pure Autotools build with custom Qt MOC/RCC rules.
# Requires: ./configure --enable-macos-gui
#
# Qt tools (moc, rcc) are detected by configure.ac and substituted here.

bin_PROGRAMS = mcaster1-encoder-gui

# ── Qt 6 flags (from pkg-config) ──────────────────────────────────────────
AM_CPPFLAGS = \
    $(QT6WIDGETS_CFLAGS) \
    $(QT6CORE_CFLAGS) \
    $(QT6GUI_CFLAGS) \
    $(LIBYAML_CFLAGS) \
    $(PORTAUDIO_CFLAGS) \
    -DHAVE_PORTAUDIO \
    -I$(srcdir) \
    -fPIC

# ── Phase M3: Codec library flags (conditional) ─────────────────────────
# IMPORTANT: Source files use #ifdef HAVE_LAME etc. but do NOT include config.h.
# These -D flags MUST be passed via AM_CPPFLAGS or the codecs silently fall back.
if HAVE_LAME
AM_CPPFLAGS += $(LAME_CFLAGS) -DHAVE_LAME
endif
if HAVE_VORBIS
AM_CPPFLAGS += $(VORBIS_CFLAGS) -DHAVE_VORBIS
endif
if HAVE_FLAC
AM_CPPFLAGS += $(FLAC_CFLAGS) -DHAVE_FLAC
endif
if HAVE_OPUS
AM_CPPFLAGS += $(OPUS_CFLAGS) -DHAVE_OPUS
endif
if HAVE_FDKAAC
AM_CPPFLAGS += $(FDKAAC_CFLAGS) -DHAVE_FDK_AAC
endif
if HAVE_MPG123
AM_CPPFLAGS += $(MPG123_CFLAGS) -DHAVE_MPG123
endif
if HAVE_AVFORMAT
AM_CPPFLAGS += $(AVFORMAT_CFLAGS) -DHAVE_AVFORMAT
endif
if HAVE_TAGLIB
AM_CPPFLAGS += $(TAGLIB_CFLAGS) -DHAVE_TAGLIB
endif

# MOC flags: same as AM_CPPFLAGS but without -fPIC (MOC misinterprets -f as force-include)
MOC_CPPFLAGS = \
    $(QT6WIDGETS_CFLAGS) \
    $(QT6CORE_CFLAGS) \
    $(QT6GUI_CFLAGS) \
    -I$(srcdir)

AM_CXXFLAGS = -std=c++17 -Wall -Wextra

# Obj-C++ flags for .mm files
AM_OBJCXXFLAGS = $(AM_CPPFLAGS) $(AM_CXXFLAGS) -fobjc-arc

mcaster1_encoder_gui_LDADD = \
    $(QT6WIDGETS_LIBS) \
    $(QT6CORE_LIBS) \
    $(QT6GUI_LIBS) \
    $(LIBYAML_LIBS) \
    $(PORTAUDIO_LIBS) \
    $(MACOS_FRAMEWORKS)

# ── Phase M3: Conditional codec library linking ──────────────────────────
if HAVE_LAME
mcaster1_encoder_gui_LDADD += $(LAME_LIBS)
endif
if HAVE_VORBIS
mcaster1_encoder_gui_LDADD += $(VORBIS_LIBS)
endif
if HAVE_FLAC
mcaster1_encoder_gui_LDADD += $(FLAC_LIBS)
endif
if HAVE_OPUS
mcaster1_encoder_gui_LDADD += $(OPUS_LIBS)
endif
if HAVE_FDKAAC
mcaster1_encoder_gui_LDADD += $(FDKAAC_LIBS)
endif
if HAVE_MPG123
mcaster1_encoder_gui_LDADD += $(MPG123_LIBS)
endif
if HAVE_AVFORMAT
mcaster1_encoder_gui_LDADD += $(AVFORMAT_LIBS)
endif
if HAVE_TAGLIB
mcaster1_encoder_gui_LDADD += $(TAGLIB_LIBS)
endif
if HAVE_LIBSSH2
AM_CPPFLAGS += $(LIBSSH2_CFLAGS) -DHAVE_LIBSSH2
mcaster1_encoder_gui_LDADD += $(LIBSSH2_LIBS)
endif
if HAVE_LIBCURL
AM_CPPFLAGS += $(LIBCURL_CFLAGS) -DHAVE_LIBCURL
mcaster1_encoder_gui_LDADD += $(LIBCURL_LIBS)
endif
if HAVE_VPX
AM_CPPFLAGS += $(VPX_CFLAGS) -DHAVE_VPX
mcaster1_encoder_gui_LDADD += $(VPX_LIBS)
endif

# ── Source files ──────────────────────────────────────────────────────────
mcaster1_encoder_gui_SOURCES = \
    main.cpp \
    app.cpp \
    main_window.cpp \
    encoder_list_model.cpp \
    vu_meter_widget.cpp \
    about_dialog.cpp \
    config_dialog.cpp \
    basic_settings_tab.cpp \
    yp_settings_tab.cpp \
    advanced_settings_tab.cpp \
    icy22_settings_tab.cpp \
    edit_metadata_dialog.cpp \
    agc_settings_dialog.cpp \
    preferences_dialog.cpp \
    log_viewer_dialog.cpp \
    preset_browser_dialog.cpp \
    eq_curve_widget.cpp \
    encoder_presets.cpp \
    dsp_presets.cpp \
    config_loader.cpp \
    platform_macos.mm \
    eq_graphic_dialog.cpp \
    sonic_enhancer_dialog.cpp \
    dbx_voice_dialog.cpp \
    dsp/eq.cpp \
    dsp/eq31.cpp \
    dsp/agc.cpp \
    dsp/crossfader.cpp \
    dsp/dsp_chain.cpp \
    dsp/sonic_enhancer.cpp \
    dsp/ptt_duck.cpp \
    dsp/dbx_voice.cpp \
    audio_source.cpp \
    audio_capture_macos.mm \
    audio_pipeline.cpp \
    encoder_slot.cpp \
    file_source.cpp \
    playlist_parser.cpp \
    archive_writer.cpp \
    codec_validator.cpp \
    stream_client.cpp \
    dnas_stats.cpp \
    dnas_slot_poller.cpp \
    input_level_monitor.cpp \
    video/video_capture_macos.mm \
    video/video_encoder.mm \
    video/flv_muxer.cpp \
    video/camera_preview_widget.cpp \
    video/camera_preview_window.cpp \
    video/video_effects.cpp \
    video/overlay_renderer.cpp \
    video/broadcast_monitor_window.cpp \
    video_effects_panel.cpp \
    overlay_settings_dialog.cpp \
    profile_manager.cpp \
    encoder_type_chooser.cpp \
    podcast_settings_tab.cpp \
    video/live_video_studio.cpp \
    video/stream_target_editor.cpp \
    video/transition_engine.cpp \
    video/screen_capture_source.mm \
    video/image_source.cpp \
    video/video_file_source.mm \
    podcast/rss_generator.cpp \
    podcast/sftp_uploader.cpp \
    podcast/podcast_publisher.cpp \
    podcast/http_client.cpp \
    dsp_effects_rack.cpp \
    effects_rack_tab.cpp \
    global_config_manager.cpp \
    video/video_stream_pipeline.cpp \
    video/amf0.cpp \
    video/rtmp_client.cpp \
    video/webm_muxer.cpp \
    video/hls_segmenter.cpp

if HAVE_VPX
mcaster1_encoder_gui_SOURCES += video/vpx_encoder.cpp
endif

# ── Headers (installed: none, just tracked for dist) ──────────────────────
noinst_HEADERS = \
    app.h \
    main_window.h \
    encoder_list_model.h \
    vu_meter_widget.h \
    about_dialog.h \
    config_dialog.h \
    basic_settings_tab.h \
    yp_settings_tab.h \
    advanced_settings_tab.h \
    icy22_settings_tab.h \
    edit_metadata_dialog.h \
    agc_settings_dialog.h \
    preferences_dialog.h \
    log_viewer_dialog.h \
    preset_browser_dialog.h \
    eq_curve_widget.h \
    encoder_presets.h \
    dsp_presets.h \
    config_loader.h \
    config_types.h \
    platform_macos.h \
    eq_graphic_dialog.h \
    sonic_enhancer_dialog.h \
    dbx_voice_dialog.h \
    dsp/eq.h \
    dsp/eq31.h \
    dsp/agc.h \
    dsp/crossfader.h \
    dsp/dsp_chain.h \
    dsp/sonic_enhancer.h \
    dsp/ptt_duck.h \
    dsp/dbx_voice.h \
    audio_source.h \
    audio_capture_macos.h \
    audio_pipeline.h \
    encoder_slot.h \
    file_source.h \
    playlist_parser.h \
    archive_writer.h \
    codec_validator.h \
    stream_client.h \
    dnas_stats.h \
    dnas_slot_poller.h \
    input_level_monitor.h \
    video/video_source.h \
    video/video_capture_macos.h \
    video/video_encoder.h \
    video/flv_muxer.h \
    video/camera_preview_widget.h \
    video/camera_preview_window.h \
    video/video_effects.h \
    video/overlay_renderer.h \
    video/broadcast_monitor_window.h \
    video_effects_panel.h \
    overlay_settings_dialog.h \
    profile_manager.h \
    encoder_type_chooser.h \
    podcast_settings_tab.h \
    video/live_video_studio.h \
    video/stream_target_editor.h \
    video/transition_engine.h \
    video/screen_capture_source.h \
    video/image_source.h \
    video/video_file_source.h \
    podcast/rss_generator.h \
    podcast/sftp_uploader.h \
    podcast/podcast_publisher.h \
    podcast/http_client.h \
    dsp_effects_rack.h \
    effects_rack_tab.h \
    global_config_manager.h \
    video/video_stream_pipeline.h \
    video/amf0.h \
    video/rtmp_client.h \
    video/webm_muxer.h \
    video/hls_segmenter.h \
    video/vpx_encoder.h

# Headers that need MOC processing (subset of noinst_HEADERS)
MOC_INPUTS = \
    app.h \
    main_window.h \
    encoder_list_model.h \
    vu_meter_widget.h \
    about_dialog.h \
    config_dialog.h \
    basic_settings_tab.h \
    yp_settings_tab.h \
    advanced_settings_tab.h \
    icy22_settings_tab.h \
    edit_metadata_dialog.h \
    agc_settings_dialog.h \
    preferences_dialog.h \
    log_viewer_dialog.h \
    preset_browser_dialog.h \
    eq_curve_widget.h \
    eq_graphic_dialog.h \
    sonic_enhancer_dialog.h \
    video/camera_preview_widget.h \
    video/camera_preview_window.h \
    video/broadcast_monitor_window.h \
    video_effects_panel.h \
    overlay_settings_dialog.h \
    encoder_type_chooser.h \
    podcast_settings_tab.h \
    video/live_video_studio.h \
    dbx_voice_dialog.h \
    video/stream_target_editor.h \
    dsp_effects_rack.h \
    effects_rack_tab.h

# Generated MOC sources
MOC_GENERATED = \
    moc_app.cpp \
    moc_main_window.cpp \
    moc_encoder_list_model.cpp \
    moc_vu_meter_widget.cpp \
    moc_about_dialog.cpp \
    moc_config_dialog.cpp \
    moc_basic_settings_tab.cpp \
    moc_yp_settings_tab.cpp \
    moc_advanced_settings_tab.cpp \
    moc_icy22_settings_tab.cpp \
    moc_edit_metadata_dialog.cpp \
    moc_agc_settings_dialog.cpp \
    moc_preferences_dialog.cpp \
    moc_log_viewer_dialog.cpp \
    moc_preset_browser_dialog.cpp \
    moc_eq_curve_widget.cpp \
    moc_eq_graphic_dialog.cpp \
    moc_sonic_enhancer_dialog.cpp \
    moc_camera_preview_widget.cpp \
    moc_camera_preview_window.cpp \
    moc_broadcast_monitor_window.cpp \
    moc_video_effects_panel.cpp \
    moc_overlay_settings_dialog.cpp \
    moc_encoder_type_chooser.cpp \
    moc_podcast_settings_tab.cpp \
    moc_live_video_studio.cpp \
    moc_dbx_voice_dialog.cpp \
    moc_stream_target_editor.cpp \
    moc_dsp_effects_rack.cpp \
    moc_effects_rack_tab.cpp

# Generated RCC source
RCC_GENERATED = qrc_mcaster1.cpp

# Add generated files to sources
nodist_mcaster1_encoder_gui_SOURCES = $(MOC_GENERATED) $(RCC_GENERATED)

# Ensure generated files are built before compilation
BUILT_SOURCES = $(MOC_GENERATED) $(RCC_GENERATED)

# ── MOC rules ──────────────────────────────────────────────────────────────
# Qt Meta-Object Compiler: generates moc_*.cpp from *.h containing Q_OBJECT
# Using explicit rules instead of pattern rules for portability.
moc_app.cpp: $(srcdir)/app.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

moc_main_window.cpp: $(srcdir)/main_window.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

moc_encoder_list_model.cpp: $(srcdir)/encoder_list_model.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

moc_vu_meter_widget.cpp: $(srcdir)/vu_meter_widget.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

moc_about_dialog.cpp: $(srcdir)/about_dialog.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

moc_config_dialog.cpp: $(srcdir)/config_dialog.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

moc_basic_settings_tab.cpp: $(srcdir)/basic_settings_tab.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

moc_yp_settings_tab.cpp: $(srcdir)/yp_settings_tab.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

moc_advanced_settings_tab.cpp: $(srcdir)/advanced_settings_tab.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

moc_icy22_settings_tab.cpp: $(srcdir)/icy22_settings_tab.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

moc_edit_metadata_dialog.cpp: $(srcdir)/edit_metadata_dialog.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

moc_agc_settings_dialog.cpp: $(srcdir)/agc_settings_dialog.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

moc_preferences_dialog.cpp: $(srcdir)/preferences_dialog.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

moc_log_viewer_dialog.cpp: $(srcdir)/log_viewer_dialog.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

moc_preset_browser_dialog.cpp: $(srcdir)/preset_browser_dialog.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

moc_eq_curve_widget.cpp: $(srcdir)/eq_curve_widget.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

moc_eq_graphic_dialog.cpp: $(srcdir)/eq_graphic_dialog.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

moc_sonic_enhancer_dialog.cpp: $(srcdir)/sonic_enhancer_dialog.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

moc_camera_preview_widget.cpp: $(srcdir)/video/camera_preview_widget.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

moc_camera_preview_window.cpp: $(srcdir)/video/camera_preview_window.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

moc_broadcast_monitor_window.cpp: $(srcdir)/video/broadcast_monitor_window.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

moc_video_effects_panel.cpp: $(srcdir)/video_effects_panel.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

moc_overlay_settings_dialog.cpp: $(srcdir)/overlay_settings_dialog.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

moc_encoder_type_chooser.cpp: $(srcdir)/encoder_type_chooser.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

moc_podcast_settings_tab.cpp: $(srcdir)/podcast_settings_tab.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

moc_live_video_studio.cpp: $(srcdir)/video/live_video_studio.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

moc_dbx_voice_dialog.cpp: $(srcdir)/dbx_voice_dialog.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

moc_stream_target_editor.cpp: $(srcdir)/video/stream_target_editor.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

moc_dsp_effects_rack.cpp: $(srcdir)/dsp_effects_rack.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

moc_effects_rack_tab.cpp: $(srcdir)/effects_rack_tab.h
	$(QT6_MOC) $(MOC_CPPFLAGS) $< -o $@

# ── RCC rule ─────────────────────────────────────────────────────────────
# Qt Resource Compiler: compiles .qrc + embedded files into C++ source
qrc_mcaster1.cpp: $(srcdir)/resources/mcaster1.qrc
	$(QT6_RCC) --name mcaster1 $< -o $@

# ── Extra dist ───────────────────────────────────────────────────────────
EXTRA_DIST = \
    resources/mcaster1.qrc \
    resources/icons/app-icon.svg \
    resources/icons/live-on.svg \
    resources/icons/live-off.svg \
    resources/icons/connect.svg \
    resources/icons/disconnect.svg \
    resources/icons/add-encoder.svg \
    resources/icons/settings.svg \
    resources/icons/tray-icon.svg \
    resources/icons/video-camera.svg \
    resources/icons/microphone.svg \
    resources/icons/equalizer.svg \
    resources/icons/sonic.svg \
    resources/icons/play.svg \
    resources/icons/stop.svg \
    resources/icons/log-viewer.svg \
    resources/icons/presets.svg \
    resources/icons/metadata.svg \
    resources/icons/volume.svg \
    resources/icons/agc.svg \
    resources/icons/edit.svg \
    resources/icons/delete.svg \
    resources/icons/refresh.svg \
    resources/icons/about.svg \
    resources/icons/quit.svg \
    resources/icons/theme.svg \
    resources/icons/broadcast.svg \
    resources/icons/dsp.svg \
    resources/icons/vu-meter.svg \
    resources/icons/on-air.svg \
    resources/icons/dry-run.svg \
    resources/icons/go-live.svg \
    resources/icons/stop-broadcast.svg \
    resources/icons/effects.svg \
    resources/icons/overlay.svg \
    resources/icons/subtitles.svg \
    resources/icons/lower-third.svg \
    resources/icons/editor.svg \
    resources/icons/monitor.svg \
    resources/icons/radio.svg \
    resources/icons/podcast.svg \
    resources/icons/tv-video.svg \
    resources/icons/radio-card.svg \
    resources/icons/radio-card-hover.svg \
    resources/icons/podcast-card.svg \
    resources/icons/podcast-card-hover.svg \
    resources/icons/tv-video-card.svg \
    resources/icons/tv-video-card-hover.svg \
    resources/icons/rss.svg \
    resources/icons/sftp.svg \
    resources/icons/studio.svg \
    resources/icons/banner-broadcast.svg \
    resources/test/example.srt \
    resources/images/dsp-encoder.png \
    resources/styles/dark_theme.qss \
    resources/Mcaster1DSPEncoder.icns \
    scripts/make-icns.sh \
    scripts/make-dmg.sh \
    Info.plist \
    Mcaster1DSPEncoder.entitlements

# ── Clean generated files ────────────────────────────────────────────────
CLEANFILES = $(MOC_GENERATED) $(RCC_GENERATED)

# ── .app bundle target ──────────────────────────────────────────────────
# Usage: make app-bundle (after 'make' builds the bare binary)
BUNDLE      = Mcaster1DSPEncoder.app
BUNDLE_BIN  = $(BUNDLE)/Contents/MacOS
BUNDLE_RES  = $(BUNDLE)/Contents/Resources

.PHONY: app-bundle clean-bundle

app-bundle: mcaster1-encoder-gui
	@echo "=== Building $(BUNDLE) ==="
	mkdir -p "$(BUNDLE_BIN)" "$(BUNDLE_RES)"
	cp mcaster1-encoder-gui "$(BUNDLE_BIN)/mcaster1-encoder-gui"
	cp "$(srcdir)/Info.plist" "$(BUNDLE)/Contents/Info.plist"
	@if [ -f "$(srcdir)/resources/Mcaster1DSPEncoder.icns" ]; then \
	    cp "$(srcdir)/resources/Mcaster1DSPEncoder.icns" "$(BUNDLE_RES)/Mcaster1DSPEncoder.icns"; \
	    echo "  Installed .icns icon"; \
	else \
	    echo "  WARNING: No .icns file — run: bash scripts/make-icns.sh"; \
	fi
	codesign --force --deep --sign - "$(BUNDLE)"
	@echo "=== $(BUNDLE) ready (ad-hoc signed) ==="
	@echo "  Launch: open $(BUNDLE)"

clean-bundle:
	rm -rf "$(BUNDLE)"

# ── Code-signing target ──────────────────────────────────────────────────
# Usage: make codesign CODE_SIGN_IDENTITY="-"  (ad-hoc by default)
CODE_SIGN_IDENTITY ?= -

.PHONY: codesign

codesign: app-bundle
	@echo "=== Code-signing $(BUNDLE) ==="
	codesign --force --options runtime \
	    --entitlements "$(srcdir)/Mcaster1DSPEncoder.entitlements" \
	    --sign "$(CODE_SIGN_IDENTITY)" \
	    "$(BUNDLE)"
	@echo "=== Signed with identity: $(CODE_SIGN_IDENTITY) ==="
	codesign -dvvv "$(BUNDLE)" 2>&1 | head -5

# ── DMG creation target ──────────────────────────────────────────────────
# Usage: make dmg  (creates Mcaster1DSPEncoder-<version>.dmg)
VERSION ?= 1.0.0

.PHONY: dmg

dmg: app-bundle
	bash "$(srcdir)/scripts/make-dmg.sh" "$(BUNDLE)" "$(VERSION)"

# ── Obj-C++ suffix rule ──────────────────────────────────────────────────
SUFFIXES = .mm
.mm.o:
	$(OBJCXX) $(AM_OBJCXXFLAGS) $(OBJCXXFLAGS) -c -o $@ $<
