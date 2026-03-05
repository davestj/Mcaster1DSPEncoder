/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * basic_settings_tab.h — Basic Settings tab (codec, server, bitrate)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_BASIC_SETTINGS_TAB_H
#define MC1_BASIC_SETTINGS_TAB_H

#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QSpinBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>
#include "config_types.h"

namespace mc1 {

class BasicSettingsTab : public QWidget {
    Q_OBJECT

public:
    explicit BasicSettingsTab(EncoderConfig::EncoderType type = EncoderConfig::EncoderType::RADIO,
                              QWidget *parent = nullptr);

    void loadFromConfig(const EncoderConfig &cfg);
    void saveToConfig(EncoderConfig &cfg) const;

Q_SIGNALS:
    void codecChanged(EncoderConfig::Codec codec);

private Q_SLOTS:
    void onCodecChanged(int index);
    void onPresetChanged(int index);
    void onEncodeModeChanged(int index);
    void onServerTypeChanged(int index);
    void onVideoCodecChanged(int index);
    void onMetadataSourceChanged(int index);
    void onEditMetadata();
    void onDeviceOverrideChanged(int index);

private:
    void buildUI();
    void updateFieldsForCodec();
    void updateFieldsForServerType();
    void updateAudioCodecsForVideoCodec();
    void updateFieldsForEncoderType();

    EncoderConfig::EncoderType encoder_type_;

    /* Preset */
    QComboBox   *combo_preset_;

    /* Codec / encoding */
    QComboBox   *combo_codec_;
    QSpinBox    *spin_bitrate_;
    QSpinBox    *spin_quality_;
    QSpinBox    *spin_sample_rate_;
    QComboBox   *combo_channels_;
    QComboBox   *combo_encode_mode_;
    QComboBox   *combo_channel_mode_;

    /* Labels for dynamic show/hide */
    QLabel      *lbl_bitrate_;
    QLabel      *lbl_quality_;
    QLabel      *lbl_encode_mode_;
    QLabel      *lbl_channel_mode_;

    /* Server */
    QComboBox   *combo_server_type_;
    QLineEdit   *edit_server_host_;
    QSpinBox    *spin_server_port_;
    QLabel      *lbl_username_;
    QLineEdit   *edit_username_;
    QLabel      *lbl_password_;
    QLineEdit   *edit_password_;
    QLabel      *lbl_stream_id_;
    QLineEdit   *edit_stream_id_;
    QLabel      *lbl_mountpoint_;
    QLineEdit   *edit_mountpoint_;

    /* Reconnect */
    QSpinBox    *spin_reconnect_sec_;
    QCheckBox   *chk_auto_reconnect_;

    /* Video encoding (TV_VIDEO only) */
    QWidget     *video_group_       = nullptr;
    QComboBox   *combo_video_codec_ = nullptr;
    QComboBox   *combo_video_resolution_ = nullptr;
    QComboBox   *combo_video_fps_   = nullptr;
    QSpinBox    *spin_video_bitrate_ = nullptr;

    /* Per-encoder audio source */
    QComboBox   *combo_device_override_ = nullptr;
    QLabel      *lbl_device_override_   = nullptr;

    /* Per-encoder metadata */
    QComboBox   *combo_metadata_source_ = nullptr;
    QPushButton *btn_edit_metadata_     = nullptr;
    MetadataConfig per_encoder_metadata_;
};

} // namespace mc1

#endif // MC1_BASIC_SETTINGS_TAB_H
