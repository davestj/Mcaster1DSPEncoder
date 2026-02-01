/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * basic_settings_tab.cpp — Basic Settings tab (codec, server, bitrate)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "basic_settings_tab.h"
#include "encoder_presets.h"
#include "audio_source.h"
#include "audio_pipeline.h"
#include "edit_metadata_dialog.h"
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace mc1 {

BasicSettingsTab::BasicSettingsTab(EncoderConfig::EncoderType type, QWidget *parent)
    : QWidget(parent), encoder_type_(type)
{
    buildUI();
}

void BasicSettingsTab::buildUI()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    /* Wrap everything in a scroll area so long forms don't go off screen */
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget;
    auto *top = new QVBoxLayout(content);
    top->setContentsMargins(8, 8, 8, 8);

    /* ── Preset selector ──────────────────────────────────────── */
    auto *preset_grp = new QGroupBox(QStringLiteral("Encoder Preset"));
    auto *preset_lay = new QHBoxLayout(preset_grp);
    combo_preset_ = new QComboBox;
    combo_preset_->addItem(QStringLiteral("(Custom)"));
    for (const auto &name : allPresetNames())
        combo_preset_->addItem(name);
    preset_lay->addWidget(combo_preset_);
    top->addWidget(preset_grp);

    /* ── Audio Encoding ────────────────────────────────────────── */
    auto *codec_grp = new QGroupBox(QStringLiteral("Audio Encoding"));
    auto *codec_form = new QFormLayout(codec_grp);
    codec_form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    combo_codec_ = new QComboBox;
    if (encoder_type_ == EncoderConfig::EncoderType::TV_VIDEO) {
        /* TV/Video: only codecs suitable for muxing with video */
        combo_codec_->addItem(QStringLiteral("MP3"),        static_cast<int>(EncoderConfig::Codec::MP3));
        combo_codec_->addItem(QStringLiteral("Ogg Vorbis"), static_cast<int>(EncoderConfig::Codec::VORBIS));
        combo_codec_->addItem(QStringLiteral("Opus"),       static_cast<int>(EncoderConfig::Codec::OPUS));
        combo_codec_->addItem(QStringLiteral("AAC-LC"),     static_cast<int>(EncoderConfig::Codec::AAC_LC));
        combo_codec_->addItem(QStringLiteral("HE-AAC v1 (AAC+)"), static_cast<int>(EncoderConfig::Codec::AAC_HE));
    } else {
        /* Radio & Podcast: all audio codecs */
        combo_codec_->addItem(QStringLiteral("MP3"),        static_cast<int>(EncoderConfig::Codec::MP3));
        combo_codec_->addItem(QStringLiteral("Ogg Vorbis"), static_cast<int>(EncoderConfig::Codec::VORBIS));
        combo_codec_->addItem(QStringLiteral("Opus"),       static_cast<int>(EncoderConfig::Codec::OPUS));
        combo_codec_->addItem(QStringLiteral("FLAC"),       static_cast<int>(EncoderConfig::Codec::FLAC));
        combo_codec_->addItem(QStringLiteral("AAC-LC"),     static_cast<int>(EncoderConfig::Codec::AAC_LC));
        combo_codec_->addItem(QStringLiteral("HE-AAC v1 (AAC+)"),  static_cast<int>(EncoderConfig::Codec::AAC_HE));
        combo_codec_->addItem(QStringLiteral("HE-AAC v2 (AAC++)"), static_cast<int>(EncoderConfig::Codec::AAC_HE_V2));
        combo_codec_->addItem(QStringLiteral("AAC-ELD"),    static_cast<int>(EncoderConfig::Codec::AAC_ELD));
    }
    codec_form->addRow(QStringLiteral("Audio Codec:"), combo_codec_);

    lbl_bitrate_ = new QLabel(QStringLiteral("Bitrate (kbps):"));
    spin_bitrate_ = new QSpinBox;
    spin_bitrate_->setRange(16, 320);
    spin_bitrate_->setValue(128);
    spin_bitrate_->setSuffix(QStringLiteral(" kbps"));
    codec_form->addRow(lbl_bitrate_, spin_bitrate_);

    lbl_quality_ = new QLabel(QStringLiteral("Quality (0-10):"));
    spin_quality_ = new QSpinBox;
    spin_quality_->setRange(0, 10);
    spin_quality_->setValue(5);
    codec_form->addRow(lbl_quality_, spin_quality_);

    spin_sample_rate_ = new QSpinBox;
    spin_sample_rate_->setRange(8000, 96000);
    spin_sample_rate_->setValue(44100);
    spin_sample_rate_->setSuffix(QStringLiteral(" Hz"));
    codec_form->addRow(QStringLiteral("Sample Rate:"), spin_sample_rate_);

    combo_channels_ = new QComboBox;
    combo_channels_->addItem(QStringLiteral("Mono (1)"), 1);
    combo_channels_->addItem(QStringLiteral("Stereo (2)"), 2);
    combo_channels_->setCurrentIndex(1); /* stereo */
    codec_form->addRow(QStringLiteral("Channels:"), combo_channels_);

    lbl_encode_mode_ = new QLabel(QStringLiteral("Encode Mode:"));
    combo_encode_mode_ = new QComboBox;
    combo_encode_mode_->addItem(QStringLiteral("CBR"), static_cast<int>(EncoderConfig::EncodeMode::CBR));
    combo_encode_mode_->addItem(QStringLiteral("VBR"), static_cast<int>(EncoderConfig::EncodeMode::VBR));
    combo_encode_mode_->addItem(QStringLiteral("ABR"), static_cast<int>(EncoderConfig::EncodeMode::ABR));
    codec_form->addRow(lbl_encode_mode_, combo_encode_mode_);

    lbl_channel_mode_ = new QLabel(QStringLiteral("Channel Mode:"));
    combo_channel_mode_ = new QComboBox;
    combo_channel_mode_->addItem(QStringLiteral("Joint Stereo"), static_cast<int>(EncoderConfig::ChannelMode::JOINT));
    combo_channel_mode_->addItem(QStringLiteral("Stereo"),       static_cast<int>(EncoderConfig::ChannelMode::STEREO));
    combo_channel_mode_->addItem(QStringLiteral("Mono"),         static_cast<int>(EncoderConfig::ChannelMode::MONO));
    codec_form->addRow(lbl_channel_mode_, combo_channel_mode_);

    /* ── Video Encoding (TV/Video only) ── placed BEFORE audio for TV_VIDEO */
    video_group_ = new QGroupBox(QStringLiteral("Video Encoding"));
    auto *vid_form = new QFormLayout(video_group_);
    vid_form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    combo_video_codec_ = new QComboBox;
    combo_video_codec_->addItem(QStringLiteral("H.264"),  static_cast<int>(VideoConfig::VideoCodec::H264));
    combo_video_codec_->addItem(QStringLiteral("VP8"),    static_cast<int>(VideoConfig::VideoCodec::VP8));
    combo_video_codec_->addItem(QStringLiteral("VP9"),    static_cast<int>(VideoConfig::VideoCodec::VP9));
    combo_video_codec_->addItem(QStringLiteral("Theora"), static_cast<int>(VideoConfig::VideoCodec::THEORA));
    vid_form->addRow(QStringLiteral("Video Codec:"), combo_video_codec_);

    combo_video_resolution_ = new QComboBox;
    combo_video_resolution_->addItem(QStringLiteral("480p  (854x480)"),   QStringLiteral("854x480"));
    combo_video_resolution_->addItem(QStringLiteral("720p  (1280x720)"),  QStringLiteral("1280x720"));
    combo_video_resolution_->addItem(QStringLiteral("1080p (1920x1080)"), QStringLiteral("1920x1080"));
    combo_video_resolution_->setCurrentIndex(1); /* 720p */
    vid_form->addRow(QStringLiteral("Resolution:"), combo_video_resolution_);

    combo_video_fps_ = new QComboBox;
    combo_video_fps_->addItems({QStringLiteral("15"), QStringLiteral("24"),
                                QStringLiteral("25"), QStringLiteral("30"),
                                QStringLiteral("60")});
    combo_video_fps_->setCurrentIndex(3); /* 30 fps */
    vid_form->addRow(QStringLiteral("FPS:"), combo_video_fps_);

    spin_video_bitrate_ = new QSpinBox;
    spin_video_bitrate_->setRange(500, 20000);
    spin_video_bitrate_->setValue(2500);
    spin_video_bitrate_->setSuffix(QStringLiteral(" kbps"));
    spin_video_bitrate_->setSingleStep(500);
    vid_form->addRow(QStringLiteral("Video Bitrate:"), spin_video_bitrate_);

    video_group_->setVisible(encoder_type_ == EncoderConfig::EncoderType::TV_VIDEO);

    /* For TV/Video: video settings come FIRST, then audio */
    if (encoder_type_ == EncoderConfig::EncoderType::TV_VIDEO) {
        top->addWidget(video_group_);
        top->addWidget(codec_grp);
    } else {
        top->addWidget(codec_grp);
    }

    /* ── Audio Source (per-encoder device override) ────────────── */
    auto *src_grp = new QGroupBox(QStringLiteral("Audio Source"));
    auto *src_lay = new QVBoxLayout(src_grp);
    auto *src_form = new QFormLayout;

    auto *src_toggle = new QComboBox;
    src_toggle->addItem(QStringLiteral("Use Global Audio Source"), 0);
    src_toggle->addItem(QStringLiteral("Independent Audio Source"), 1);
    src_form->addRow(QStringLiteral("Source:"), src_toggle);

    lbl_device_override_ = new QLabel(QStringLiteral("Input Device:"));
    combo_device_override_ = new QComboBox;
    combo_device_override_->addItem(QStringLiteral("(Select a device...)"), -1);
    /* Enumerate PortAudio devices */
    auto devs = PortAudioSource::enumerate_devices();
    for (auto &d : devs)
        combo_device_override_->addItem(QString::fromStdString(d.name), d.index);
    /* If PortAudio returned nothing, try CoreAudio */
    if (devs.empty()) {
        auto ca_devs = enumerate_coreaudio_devices();
        for (auto &d : ca_devs) {
            if (d.input_channels > 0)
                combo_device_override_->addItem(QString::fromStdString(d.name),
                                                 static_cast<int>(d.device_id));
        }
    }
    src_form->addRow(lbl_device_override_, combo_device_override_);

    /* Initially hidden — shown when Independent is selected */
    lbl_device_override_->setVisible(false);
    combo_device_override_->setVisible(false);

    connect(src_toggle, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, src_toggle](int) {
        bool independent = (src_toggle->currentData().toInt() == 1);
        lbl_device_override_->setVisible(independent);
        combo_device_override_->setVisible(independent);
        if (independent && combo_device_override_->count() <= 1) {
            /* Re-enumerate if list was empty */
            auto fresh = PortAudioSource::enumerate_devices();
            for (auto &d : fresh)
                combo_device_override_->addItem(QString::fromStdString(d.name), d.index);
        }
    });
    src_lay->addLayout(src_form);
    top->addWidget(src_grp);

    /* ── Metadata Source (global vs per-encoder) ──────────────── */
    auto *meta_grp = new QGroupBox(QStringLiteral("Metadata Source"));
    auto *meta_lay = new QVBoxLayout(meta_grp);
    auto *meta_form = new QFormLayout;
    combo_metadata_source_ = new QComboBox;
    combo_metadata_source_->addItem(QStringLiteral("Use Global Metadata"), 0);
    combo_metadata_source_->addItem(QStringLiteral("Independent Metadata"), 1);
    meta_form->addRow(QStringLiteral("Metadata:"), combo_metadata_source_);
    meta_lay->addLayout(meta_form);

    btn_edit_metadata_ = new QPushButton(QStringLiteral("Edit Metadata..."));
    btn_edit_metadata_->setVisible(false);
    btn_edit_metadata_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #333355; color: #ccccdd; padding: 4px 12px; "
        "border-radius: 3px; }"
        "QPushButton:hover { background: #444466; }"));
    connect(btn_edit_metadata_, &QPushButton::clicked,
            this, &BasicSettingsTab::onEditMetadata);
    connect(combo_metadata_source_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BasicSettingsTab::onMetadataSourceChanged);
    meta_lay->addWidget(btn_edit_metadata_);
    top->addWidget(meta_grp);

    /* ── Server ───────────────────────────────────────────────── */
    auto *srv_grp = new QGroupBox(QStringLiteral("Server"));
    auto *srv_form = new QFormLayout(srv_grp);
    srv_form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    combo_server_type_ = new QComboBox;
    if (encoder_type_ == EncoderConfig::EncoderType::TV_VIDEO) {
        /* TV/Video: only servers that support video streaming */
        combo_server_type_->addItem(QStringLiteral("Icecast2"),        static_cast<int>(StreamTarget::Protocol::ICECAST2));
        combo_server_type_->addItem(QStringLiteral("Mcaster1 DNAS"),   static_cast<int>(StreamTarget::Protocol::MCASTER1_DNAS));
        combo_server_type_->addItem(QStringLiteral("YouTube Live"),    static_cast<int>(StreamTarget::Protocol::YOUTUBE));
        combo_server_type_->addItem(QStringLiteral("Twitch"),          static_cast<int>(StreamTarget::Protocol::TWITCH));
    } else {
        /* Radio & Podcast: all audio server types */
        combo_server_type_->addItem(QStringLiteral("Icecast2"),        static_cast<int>(StreamTarget::Protocol::ICECAST2));
        combo_server_type_->addItem(QStringLiteral("Shoutcast v1"),    static_cast<int>(StreamTarget::Protocol::SHOUTCAST_V1));
        combo_server_type_->addItem(QStringLiteral("Shoutcast v2"),    static_cast<int>(StreamTarget::Protocol::SHOUTCAST_V2));
        combo_server_type_->addItem(QStringLiteral("Mcaster1 DNAS"),   static_cast<int>(StreamTarget::Protocol::MCASTER1_DNAS));
        combo_server_type_->addItem(QStringLiteral("Live365"),         static_cast<int>(StreamTarget::Protocol::LIVE365));
        combo_server_type_->addItem(QStringLiteral("YouTube Live"),    static_cast<int>(StreamTarget::Protocol::YOUTUBE));
        combo_server_type_->addItem(QStringLiteral("Twitch"),          static_cast<int>(StreamTarget::Protocol::TWITCH));
    }
    srv_form->addRow(QStringLiteral("Server Type:"), combo_server_type_);

    edit_server_host_ = new QLineEdit;
    edit_server_host_->setPlaceholderText(QStringLiteral("hostname or IP"));
    srv_form->addRow(QStringLiteral("Host:"), edit_server_host_);

    spin_server_port_ = new QSpinBox;
    spin_server_port_->setRange(1, 65535);
    spin_server_port_->setValue(8000);
    srv_form->addRow(QStringLiteral("Port:"), spin_server_port_);

    lbl_username_ = new QLabel(QStringLiteral("Username:"));
    edit_username_ = new QLineEdit;
    edit_username_->setPlaceholderText(QStringLiteral("source"));
    edit_username_->setText(QStringLiteral("source"));
    srv_form->addRow(lbl_username_, edit_username_);

    lbl_password_ = new QLabel(QStringLiteral("Password:"));
    edit_password_ = new QLineEdit;
    edit_password_->setEchoMode(QLineEdit::Password);
    srv_form->addRow(lbl_password_, edit_password_);

    /* Shoutcast v2: Stream ID for multi-stream DNAS */
    lbl_stream_id_ = new QLabel(QStringLiteral("Stream ID:"));
    edit_stream_id_ = new QLineEdit;
    edit_stream_id_->setPlaceholderText(QStringLiteral("1"));
    edit_stream_id_->setText(QStringLiteral("1"));
    srv_form->addRow(lbl_stream_id_, edit_stream_id_);

    lbl_mountpoint_ = new QLabel(QStringLiteral("Mountpoint:"));
    edit_mountpoint_ = new QLineEdit;
    edit_mountpoint_->setPlaceholderText(QStringLiteral("/stream"));
    srv_form->addRow(lbl_mountpoint_, edit_mountpoint_);

    chk_auto_reconnect_ = new QCheckBox(QStringLiteral("Auto-reconnect"));
    chk_auto_reconnect_->setChecked(true);
    srv_form->addRow(QString(), chk_auto_reconnect_);

    spin_reconnect_sec_ = new QSpinBox;
    spin_reconnect_sec_->setRange(1, 300);
    spin_reconnect_sec_->setValue(5);
    spin_reconnect_sec_->setSuffix(QStringLiteral(" sec"));
    srv_form->addRow(QStringLiteral("Reconnect Interval:"), spin_reconnect_sec_);

    top->addWidget(srv_grp);
    top->addStretch();

    scroll->setWidget(content);
    outer->addWidget(scroll);

    /* ── Signals ──────────────────────────────────────────────── */
    connect(combo_codec_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BasicSettingsTab::onCodecChanged);
    connect(combo_preset_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BasicSettingsTab::onPresetChanged);
    connect(combo_encode_mode_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BasicSettingsTab::onEncodeModeChanged);
    connect(combo_server_type_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BasicSettingsTab::onServerTypeChanged);
    if (combo_video_codec_) {
        connect(combo_video_codec_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &BasicSettingsTab::onVideoCodecChanged);
    }

    updateFieldsForCodec();
    updateFieldsForServerType();
    if (encoder_type_ == EncoderConfig::EncoderType::TV_VIDEO)
        updateAudioCodecsForVideoCodec();
}

void BasicSettingsTab::onCodecChanged(int /*index*/)
{
    updateFieldsForCodec();
    auto codec = static_cast<EncoderConfig::Codec>(combo_codec_->currentData().toInt());
    emit codecChanged(codec);
    combo_preset_->setCurrentIndex(0); /* switch to (Custom) */
}

void BasicSettingsTab::onPresetChanged(int index)
{
    if (index <= 0) return; /* (Custom) */
    QString name = combo_preset_->currentText();
    const EncoderPreset *p = findPreset(name);
    if (!p) return;

    /* Apply preset to UI controls */
    for (int i = 0; i < combo_codec_->count(); ++i) {
        if (combo_codec_->itemData(i).toInt() == static_cast<int>(p->codec)) {
            combo_codec_->setCurrentIndex(i);
            break;
        }
    }
    spin_bitrate_->setValue(p->bitrate_kbps);
    spin_quality_->setValue(p->quality);
    spin_sample_rate_->setValue(p->sample_rate);

    for (int i = 0; i < combo_channels_->count(); ++i) {
        if (combo_channels_->itemData(i).toInt() == p->channels) {
            combo_channels_->setCurrentIndex(i);
            break;
        }
    }
    for (int i = 0; i < combo_encode_mode_->count(); ++i) {
        if (combo_encode_mode_->itemData(i).toInt() == static_cast<int>(p->encode_mode)) {
            combo_encode_mode_->setCurrentIndex(i);
            break;
        }
    }
    for (int i = 0; i < combo_channel_mode_->count(); ++i) {
        if (combo_channel_mode_->itemData(i).toInt() == static_cast<int>(p->channel_mode)) {
            combo_channel_mode_->setCurrentIndex(i);
            break;
        }
    }
    updateFieldsForCodec();
}

void BasicSettingsTab::onEncodeModeChanged(int /*index*/)
{
    updateFieldsForCodec();
}

void BasicSettingsTab::updateFieldsForCodec()
{
    auto codec = static_cast<EncoderConfig::Codec>(combo_codec_->currentData().toInt());
    auto mode  = static_cast<EncoderConfig::EncodeMode>(combo_encode_mode_->currentData().toInt());

    bool show_bitrate    = true;
    bool show_quality    = true;
    bool show_encode_mode = true;
    bool show_channel_mode = true;

    switch (codec) {
        case EncoderConfig::Codec::MP3:
            show_quality = (mode == EncoderConfig::EncodeMode::VBR);
            show_bitrate = (mode != EncoderConfig::EncodeMode::VBR);
            show_channel_mode = true;
            spin_bitrate_->setRange(32, 320);
            spin_quality_->setRange(0, 9);
            break;

        case EncoderConfig::Codec::VORBIS:
            show_bitrate = false;
            show_quality = true;
            show_encode_mode = false;
            show_channel_mode = false;
            spin_quality_->setRange(0, 10);
            break;

        case EncoderConfig::Codec::OPUS:
            show_bitrate = true;
            show_quality = false;
            show_encode_mode = false;
            show_channel_mode = false;
            spin_bitrate_->setRange(32, 320);
            break;

        case EncoderConfig::Codec::FLAC:
            show_bitrate = false;
            show_quality = true;
            show_encode_mode = false;
            show_channel_mode = false;
            spin_quality_->setRange(0, 8);
            lbl_quality_->setText(QStringLiteral("Compression (0-8):"));
            break;

        case EncoderConfig::Codec::AAC_LC:
            show_bitrate = true;
            show_quality = false;
            show_encode_mode = false;
            show_channel_mode = false;
            spin_bitrate_->setRange(64, 320);
            break;

        case EncoderConfig::Codec::AAC_HE:
            show_bitrate = true;
            show_quality = false;
            show_encode_mode = false;
            show_channel_mode = false;
            spin_bitrate_->setRange(24, 128);
            break;

        case EncoderConfig::Codec::AAC_HE_V2:
            show_bitrate = true;
            show_quality = false;
            show_encode_mode = false;
            show_channel_mode = false;
            spin_bitrate_->setRange(16, 64);
            /* HE-AAC v2 MUST be stereo */
            combo_channels_->setCurrentIndex(1);
            combo_channels_->setEnabled(false);
            break;

        case EncoderConfig::Codec::AAC_ELD:
            show_bitrate = true;
            show_quality = false;
            show_encode_mode = false;
            show_channel_mode = false;
            spin_bitrate_->setRange(24, 192);
            break;
    }

    /* Re-enable channels if not AAC HE v2 */
    if (codec != EncoderConfig::Codec::AAC_HE_V2)
        combo_channels_->setEnabled(true);

    /* Update quality label */
    if (codec != EncoderConfig::Codec::FLAC)
        lbl_quality_->setText(QStringLiteral("Quality (0-10):"));

    lbl_bitrate_->setVisible(show_bitrate);
    spin_bitrate_->setVisible(show_bitrate);
    lbl_quality_->setVisible(show_quality);
    spin_quality_->setVisible(show_quality);
    lbl_encode_mode_->setVisible(show_encode_mode);
    combo_encode_mode_->setVisible(show_encode_mode);
    lbl_channel_mode_->setVisible(show_channel_mode);
    combo_channel_mode_->setVisible(show_channel_mode);
}

void BasicSettingsTab::onServerTypeChanged(int /*index*/)
{
    updateFieldsForServerType();
}

void BasicSettingsTab::updateFieldsForServerType()
{
    auto proto = static_cast<StreamTarget::Protocol>(combo_server_type_->currentData().toInt());

    bool show_username   = true;
    bool show_password   = true;
    bool show_stream_id  = false;
    bool show_mountpoint = true;

    switch (proto) {
        case StreamTarget::Protocol::ICECAST2:
            edit_username_->setPlaceholderText(QStringLiteral("source"));
            lbl_password_->setText(QStringLiteral("Password:"));
            break;

        case StreamTarget::Protocol::MCASTER1_DNAS:
            edit_username_->setPlaceholderText(QStringLiteral("source"));
            lbl_password_->setText(QStringLiteral("Source Password:"));
            break;

        case StreamTarget::Protocol::LIVE365:
            edit_username_->setPlaceholderText(QStringLiteral("member ID"));
            lbl_password_->setText(QStringLiteral("Password:"));
            break;

        case StreamTarget::Protocol::SHOUTCAST_V1:
            show_username = false;
            show_mountpoint = false;
            lbl_password_->setText(QStringLiteral("Password:"));
            break;

        case StreamTarget::Protocol::SHOUTCAST_V2:
            show_username = false;
            show_stream_id = true;
            show_mountpoint = false;
            lbl_password_->setText(QStringLiteral("DJ Password:"));
            break;

        case StreamTarget::Protocol::YOUTUBE:
            show_username = false;
            show_mountpoint = false;
            lbl_password_->setText(QStringLiteral("Stream Key:"));
            edit_server_host_->setText(QStringLiteral("a.rtmp.youtube.com"));
            spin_server_port_->setValue(1935);
            break;

        case StreamTarget::Protocol::TWITCH:
            show_username = false;
            show_mountpoint = false;
            lbl_password_->setText(QStringLiteral("Stream Key:"));
            edit_server_host_->setText(QStringLiteral("live.twitch.tv"));
            spin_server_port_->setValue(1935);
            break;
    }

    lbl_username_->setVisible(show_username);
    edit_username_->setVisible(show_username);
    lbl_password_->setVisible(show_password);
    edit_password_->setVisible(show_password);
    lbl_stream_id_->setVisible(show_stream_id);
    edit_stream_id_->setVisible(show_stream_id);
    lbl_mountpoint_->setVisible(show_mountpoint);
    edit_mountpoint_->setVisible(show_mountpoint);
}

void BasicSettingsTab::loadFromConfig(const EncoderConfig &cfg)
{
    /* Codec */
    for (int i = 0; i < combo_codec_->count(); ++i) {
        if (combo_codec_->itemData(i).toInt() == static_cast<int>(cfg.codec)) {
            combo_codec_->setCurrentIndex(i);
            break;
        }
    }
    spin_bitrate_->setValue(cfg.bitrate_kbps);
    spin_quality_->setValue(cfg.quality);
    spin_sample_rate_->setValue(cfg.sample_rate);

    for (int i = 0; i < combo_channels_->count(); ++i) {
        if (combo_channels_->itemData(i).toInt() == cfg.channels) {
            combo_channels_->setCurrentIndex(i);
            break;
        }
    }
    for (int i = 0; i < combo_encode_mode_->count(); ++i) {
        if (combo_encode_mode_->itemData(i).toInt() == static_cast<int>(cfg.encode_mode)) {
            combo_encode_mode_->setCurrentIndex(i);
            break;
        }
    }
    for (int i = 0; i < combo_channel_mode_->count(); ++i) {
        if (combo_channel_mode_->itemData(i).toInt() == static_cast<int>(cfg.channel_mode)) {
            combo_channel_mode_->setCurrentIndex(i);
            break;
        }
    }

    /* Video config */
    if (video_group_->isVisible()) {
        for (int i = 0; i < combo_video_codec_->count(); ++i) {
            if (combo_video_codec_->itemData(i).toInt() == static_cast<int>(cfg.video.video_codec)) {
                combo_video_codec_->setCurrentIndex(i);
                break;
            }
        }
        QString res = QString("%1x%2").arg(cfg.video.width).arg(cfg.video.height);
        int ri = combo_video_resolution_->findData(res);
        if (ri >= 0) combo_video_resolution_->setCurrentIndex(ri);

        int fi = combo_video_fps_->findText(QString::number(cfg.video.fps));
        if (fi >= 0) combo_video_fps_->setCurrentIndex(fi);

        spin_video_bitrate_->setValue(cfg.video.bitrate_kbps);
    }

    /* Per-encoder audio device */
    if (cfg.per_encoder_device_index >= 0) {
        /* Set the toggle to Independent and show the device combo */
        int di = combo_device_override_->findData(cfg.per_encoder_device_index);
        if (di >= 0) {
            combo_device_override_->setCurrentIndex(di);
            lbl_device_override_->setVisible(true);
            combo_device_override_->setVisible(true);
        }
    }

    /* Per-encoder metadata */
    per_encoder_metadata_ = cfg.per_encoder_metadata;
    combo_metadata_source_->setCurrentIndex(cfg.use_global_metadata ? 0 : 1);
    btn_edit_metadata_->setVisible(!cfg.use_global_metadata);

    /* Server */
    for (int i = 0; i < combo_server_type_->count(); ++i) {
        if (combo_server_type_->itemData(i).toInt() == static_cast<int>(cfg.stream_target.protocol)) {
            combo_server_type_->setCurrentIndex(i);
            break;
        }
    }
    edit_server_host_->setText(QString::fromStdString(cfg.stream_target.host));
    spin_server_port_->setValue(cfg.stream_target.port);
    edit_username_->setText(QString::fromStdString(cfg.stream_target.username));
    edit_password_->setText(QString::fromStdString(cfg.stream_target.password));
    edit_mountpoint_->setText(QString::fromStdString(cfg.stream_target.mount));
    chk_auto_reconnect_->setChecked(cfg.auto_reconnect);
    spin_reconnect_sec_->setValue(cfg.reconnect_interval_sec);

    updateFieldsForCodec();
    updateFieldsForServerType();
}

void BasicSettingsTab::saveToConfig(EncoderConfig &cfg) const
{
    cfg.codec        = static_cast<EncoderConfig::Codec>(combo_codec_->currentData().toInt());
    cfg.bitrate_kbps = spin_bitrate_->value();
    cfg.quality      = spin_quality_->value();
    cfg.sample_rate  = spin_sample_rate_->value();
    cfg.channels     = combo_channels_->currentData().toInt();
    cfg.encode_mode  = static_cast<EncoderConfig::EncodeMode>(combo_encode_mode_->currentData().toInt());
    cfg.channel_mode = static_cast<EncoderConfig::ChannelMode>(combo_channel_mode_->currentData().toInt());

    /* Video config */
    if (video_group_->isVisible()) {
        cfg.video.enabled = true;
        cfg.video.video_codec = static_cast<VideoConfig::VideoCodec>(
            combo_video_codec_->currentData().toInt());
        QString res = combo_video_resolution_->currentData().toString();
        auto parts = res.split('x');
        if (parts.size() == 2) {
            cfg.video.width  = parts[0].toInt();
            cfg.video.height = parts[1].toInt();
        }
        cfg.video.fps          = combo_video_fps_->currentText().toInt();
        cfg.video.bitrate_kbps = spin_video_bitrate_->value();
    }

    /* Per-encoder audio device */
    cfg.per_encoder_device_index = combo_device_override_->currentData().toInt();

    /* Per-encoder metadata */
    cfg.use_global_metadata = (combo_metadata_source_->currentIndex() == 0);
    cfg.per_encoder_metadata = per_encoder_metadata_;

    cfg.stream_target.protocol = static_cast<StreamTarget::Protocol>(combo_server_type_->currentData().toInt());
    cfg.stream_target.host     = edit_server_host_->text().toStdString();
    cfg.stream_target.port     = static_cast<uint16_t>(spin_server_port_->value());
    cfg.stream_target.username = edit_username_->text().toStdString();
    cfg.stream_target.password = edit_password_->text().toStdString();
    cfg.stream_target.mount    = edit_mountpoint_->text().toStdString();
    cfg.auto_reconnect         = chk_auto_reconnect_->isChecked();
    cfg.reconnect_interval_sec = spin_reconnect_sec_->value();
}

void BasicSettingsTab::onVideoCodecChanged(int /*index*/)
{
    updateAudioCodecsForVideoCodec();
    combo_preset_->setCurrentIndex(0); /* switch to (Custom) */
}

void BasicSettingsTab::onMetadataSourceChanged(int index)
{
    btn_edit_metadata_->setVisible(index == 1);
}

void BasicSettingsTab::onEditMetadata()
{
    EditMetadataDialog dlg(per_encoder_metadata_, this);
    dlg.exec();
}

void BasicSettingsTab::onDeviceOverrideChanged(int /*index*/)
{
    /* Selection change handled; saveToConfig reads currentData() */
}

void BasicSettingsTab::updateFieldsForEncoderType()
{
    /* Placeholder for future encoder-type-specific UI adjustments */
}

void BasicSettingsTab::updateAudioCodecsForVideoCodec()
{
    if (!combo_video_codec_ || !combo_codec_) return;

    auto vc = static_cast<VideoConfig::VideoCodec>(combo_video_codec_->currentData().toInt());

    /* Remember current audio codec selection so we can restore if still valid */
    int prev_codec = combo_codec_->currentData().toInt();

    QSignalBlocker blk(combo_codec_);
    combo_codec_->clear();

    switch (vc) {
        case VideoConfig::VideoCodec::H264:
            /* H.264 containers (FLV/MPEG-TS/MP4) support wide audio range */
            combo_codec_->addItem(QStringLiteral("MP3"),              static_cast<int>(EncoderConfig::Codec::MP3));
            combo_codec_->addItem(QStringLiteral("Ogg Vorbis"),       static_cast<int>(EncoderConfig::Codec::VORBIS));
            combo_codec_->addItem(QStringLiteral("Opus"),             static_cast<int>(EncoderConfig::Codec::OPUS));
            combo_codec_->addItem(QStringLiteral("AAC-LC"),           static_cast<int>(EncoderConfig::Codec::AAC_LC));
            combo_codec_->addItem(QStringLiteral("HE-AAC v1 (AAC+)"),static_cast<int>(EncoderConfig::Codec::AAC_HE));
            break;

        case VideoConfig::VideoCodec::VP8:
        case VideoConfig::VideoCodec::VP9:
            /* WebM container: only Vorbis or Opus */
            combo_codec_->addItem(QStringLiteral("Ogg Vorbis"), static_cast<int>(EncoderConfig::Codec::VORBIS));
            combo_codec_->addItem(QStringLiteral("Opus"),       static_cast<int>(EncoderConfig::Codec::OPUS));
            break;

        case VideoConfig::VideoCodec::THEORA:
            /* Ogg container: only Vorbis or Opus */
            combo_codec_->addItem(QStringLiteral("Ogg Vorbis"), static_cast<int>(EncoderConfig::Codec::VORBIS));
            combo_codec_->addItem(QStringLiteral("Opus"),       static_cast<int>(EncoderConfig::Codec::OPUS));
            break;
    }

    /* Try to restore previous selection */
    bool restored = false;
    for (int i = 0; i < combo_codec_->count(); ++i) {
        if (combo_codec_->itemData(i).toInt() == prev_codec) {
            combo_codec_->setCurrentIndex(i);
            restored = true;
            break;
        }
    }
    if (!restored)
        combo_codec_->setCurrentIndex(0);

    /* Update dependent fields for the (possibly changed) codec */
    updateFieldsForCodec();
}

} // namespace mc1
