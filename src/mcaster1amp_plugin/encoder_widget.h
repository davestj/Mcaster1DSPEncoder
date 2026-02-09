/*
 * Mcaster1 DSP Encoder — Mcaster1AMP Plugin
 * encoder_widget.h — Qt configuration UI for the AMP encoder plugin
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QWidget>
#include <QString>

class QLineEdit;
class QSpinBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTimer;

// Forward declarations for control API (defined in dsp_mcaster1encoder.cpp)
struct EncoderConfig;
void        mc1enc_start_streaming();
void        mc1enc_stop_streaming();
bool        mc1enc_is_connected();
int         mc1enc_state();
uint64_t    mc1enc_bytes_sent();
void        mc1enc_update_config(const EncoderConfig& cfg);
EncoderConfig mc1enc_get_config();

namespace mc1enc {

class EncoderWidget : public QWidget {
    Q_OBJECT

public:
    explicit EncoderWidget(QWidget* parent = nullptr);
    ~EncoderWidget() override = default;

    QSize sizeHint()        const override { return {560, 480}; }
    QSize minimumSizeHint() const override { return {440, 380}; }

private slots:
    void onConnectClicked();
    void onRefreshStatus();
    void onApplyConfig();

private:
    void loadConfigToUi();
    EncoderConfig configFromUi() const;

    // Server settings
    QLineEdit*   host_edit_     = nullptr;
    QSpinBox*    port_spin_     = nullptr;
    QLineEdit*   mount_edit_    = nullptr;
    QLineEdit*   user_edit_     = nullptr;
    QLineEdit*   pass_edit_     = nullptr;

    // Encoding settings
    QComboBox*   codec_combo_   = nullptr;
    QComboBox*   bitrate_combo_ = nullptr;

    // Station info
    QLineEdit*   name_edit_     = nullptr;
    QLineEdit*   desc_edit_     = nullptr;
    QLineEdit*   genre_edit_    = nullptr;
    QLineEdit*   url_edit_      = nullptr;

    // Controls
    QPushButton* connect_btn_   = nullptr;
    QLabel*      status_label_  = nullptr;
    QLabel*      bytes_label_   = nullptr;
    QLabel*      dot_label_     = nullptr;

    // Status refresh timer
    QTimer*      status_timer_  = nullptr;
};

} // namespace mc1enc
