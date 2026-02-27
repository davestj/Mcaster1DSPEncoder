/*
 * Mcaster1 DSP Encoder — Mcaster1AMP Plugin
 * encoder_widget.cpp — Qt configuration UI implementation
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "encoder_widget.h"

// Must include the config struct definition
struct EncoderConfig {
    std::string host          = "localhost";
    uint16_t    port          = 8000;
    std::string mount         = "/stream";
    std::string username      = "source";
    std::string password      = "";
    std::string station_name  = "Mcaster1AMP Stream";
    std::string description   = "Powered by Mcaster1 DSP Encoder";
    std::string genre         = "Various";
    std::string url           = "https://mcaster1.com";
    int         bitrate       = 128;
    int         sample_rate   = 44100;
    int         channels      = 2;
};

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QFont>
#include <QString>

namespace mc1enc {

// ── Mcaster1 dark theme colors ──────────────────────────────────────────────
static const char* kStyleSheet = R"(
    QWidget {
        background: #0D1526;
        color: #C8D8E8;
        font-family: "Segoe UI", "SF Pro Display", sans-serif;
        font-size: 11px;
    }
    QGroupBox {
        border: 1px solid #1A2A40;
        border-radius: 4px;
        margin-top: 12px;
        padding-top: 16px;
        font-weight: bold;
        color: #0EA5E9;
    }
    QGroupBox::title {
        subcontrol-origin: margin;
        subcontrol-position: top left;
        padding: 2px 8px;
    }
    QLineEdit, QSpinBox, QComboBox {
        background: #131F3A;
        border: 1px solid #1A2A40;
        border-radius: 3px;
        padding: 4px 6px;
        color: #E0E8F0;
        selection-background-color: #0EA5E9;
    }
    QLineEdit:focus, QSpinBox:focus, QComboBox:focus {
        border-color: #0EA5E9;
    }
    QPushButton {
        background: #0EA5E9;
        color: #FFFFFF;
        border: none;
        border-radius: 4px;
        padding: 8px 20px;
        font-weight: bold;
        font-size: 12px;
    }
    QPushButton:hover {
        background: #38BDF8;
    }
    QPushButton:pressed {
        background: #0284C7;
    }
    QPushButton#disconnect {
        background: #DC2626;
    }
    QPushButton#disconnect:hover {
        background: #EF4444;
    }
    QLabel#header {
        font-size: 14px;
        font-weight: bold;
        color: #0EA5E9;
        padding: 4px 0;
    }
    QLabel#status {
        font-size: 11px;
        padding: 4px 8px;
        border-radius: 3px;
    }
)";

EncoderWidget::EncoderWidget(QWidget* parent)
    : QWidget(parent)
{
    setStyleSheet(QString::fromLatin1(kStyleSheet));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 8, 12, 8);
    root->setSpacing(6);

    // ── Header ──────────────────────────────────────────────────────────────
    auto* header = new QLabel(QStringLiteral("MC1 DSP ENCODER"), this);
    header->setObjectName(QStringLiteral("header"));
    header->setAlignment(Qt::AlignCenter);
    root->addWidget(header);

    auto* subtitle = new QLabel(
        QStringLiteral("Stream Mcaster1AMP playback to Icecast2 / Shoutcast"), this);
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setStyleSheet(QStringLiteral("color: #5A789A; font-size: 10px;"));
    root->addWidget(subtitle);

    // ── Server Settings ─────────────────────────────────────────────────────
    auto* server_grp = new QGroupBox(QStringLiteral("Server Connection"), this);
    auto* sg = new QGridLayout(server_grp);
    sg->setSpacing(4);

    sg->addWidget(new QLabel(QStringLiteral("Host:"), this), 0, 0);
    host_edit_ = new QLineEdit(this);
    host_edit_->setPlaceholderText(QStringLiteral("e.g. radio.example.com"));
    sg->addWidget(host_edit_, 0, 1);

    sg->addWidget(new QLabel(QStringLiteral("Port:"), this), 0, 2);
    port_spin_ = new QSpinBox(this);
    port_spin_->setRange(1, 65535);
    port_spin_->setValue(8000);
    port_spin_->setFixedWidth(80);
    sg->addWidget(port_spin_, 0, 3);

    sg->addWidget(new QLabel(QStringLiteral("Mount:"), this), 1, 0);
    mount_edit_ = new QLineEdit(this);
    mount_edit_->setPlaceholderText(QStringLiteral("/stream"));
    sg->addWidget(mount_edit_, 1, 1);

    sg->addWidget(new QLabel(QStringLiteral("Username:"), this), 2, 0);
    user_edit_ = new QLineEdit(this);
    user_edit_->setText(QStringLiteral("source"));
    sg->addWidget(user_edit_, 2, 1);

    sg->addWidget(new QLabel(QStringLiteral("Password:"), this), 2, 2);
    pass_edit_ = new QLineEdit(this);
    pass_edit_->setEchoMode(QLineEdit::Password);
    sg->addWidget(pass_edit_, 2, 3);

    root->addWidget(server_grp);

    // ── Encoding Settings ───────────────────────────────────────────────────
    auto* enc_grp = new QGroupBox(QStringLiteral("Encoding"), this);
    auto* eg = new QHBoxLayout(enc_grp);
    eg->setSpacing(8);

    eg->addWidget(new QLabel(QStringLiteral("Codec:"), this));
    codec_combo_ = new QComboBox(this);
    codec_combo_->addItems({
        QStringLiteral("MP3 (LAME)"),
    });
    codec_combo_->setToolTip(QStringLiteral("Additional codecs coming soon (Vorbis, Opus, AAC)"));
    eg->addWidget(codec_combo_);

    eg->addWidget(new QLabel(QStringLiteral("Bitrate:"), this));
    bitrate_combo_ = new QComboBox(this);
    bitrate_combo_->addItems({
        QStringLiteral("64"),  QStringLiteral("96"),  QStringLiteral("128"),
        QStringLiteral("160"), QStringLiteral("192"), QStringLiteral("224"),
        QStringLiteral("256"), QStringLiteral("320"),
    });
    bitrate_combo_->setCurrentText(QStringLiteral("128"));
    eg->addWidget(bitrate_combo_);
    eg->addWidget(new QLabel(QStringLiteral("kbps"), this));

    eg->addStretch();
    root->addWidget(enc_grp);

    // ── Station Info ────────────────────────────────────────────────────────
    auto* info_grp = new QGroupBox(QStringLiteral("Station Info (ICY metadata)"), this);
    auto* ig = new QGridLayout(info_grp);
    ig->setSpacing(4);

    ig->addWidget(new QLabel(QStringLiteral("Name:"), this), 0, 0);
    name_edit_ = new QLineEdit(this);
    name_edit_->setPlaceholderText(QStringLiteral("My Radio Station"));
    ig->addWidget(name_edit_, 0, 1);

    ig->addWidget(new QLabel(QStringLiteral("Description:"), this), 1, 0);
    desc_edit_ = new QLineEdit(this);
    ig->addWidget(desc_edit_, 1, 1);

    ig->addWidget(new QLabel(QStringLiteral("Genre:"), this), 2, 0);
    genre_edit_ = new QLineEdit(this);
    ig->addWidget(genre_edit_, 2, 1);

    ig->addWidget(new QLabel(QStringLiteral("URL:"), this), 3, 0);
    url_edit_ = new QLineEdit(this);
    url_edit_->setPlaceholderText(QStringLiteral("https://mystation.com"));
    ig->addWidget(url_edit_, 3, 1);

    root->addWidget(info_grp);

    // ── Status Bar ──────────────────────────────────────────────────────────
    auto* status_row = new QHBoxLayout;
    status_row->setSpacing(8);

    dot_label_ = new QLabel(QStringLiteral("\u25CF"), this);  // ●
    dot_label_->setStyleSheet(QStringLiteral("color: #555; font-size: 14px;"));
    dot_label_->setFixedWidth(18);
    status_row->addWidget(dot_label_);

    status_label_ = new QLabel(QStringLiteral("Idle"), this);
    status_label_->setObjectName(QStringLiteral("status"));
    status_row->addWidget(status_label_);

    status_row->addStretch();

    bytes_label_ = new QLabel(QStringLiteral("0 bytes"), this);
    bytes_label_->setStyleSheet(QStringLiteral("color: #5A789A; font-size: 10px;"));
    status_row->addWidget(bytes_label_);

    root->addLayout(status_row);

    // ── Connect Button ──────────────────────────────────────────────────────
    auto* btn_row = new QHBoxLayout;
    btn_row->addStretch();

    connect_btn_ = new QPushButton(QStringLiteral("Connect"), this);
    connect_btn_->setFixedWidth(160);
    connect(connect_btn_, &QPushButton::clicked,
            this, &EncoderWidget::onConnectClicked);
    btn_row->addWidget(connect_btn_);

    btn_row->addStretch();
    root->addLayout(btn_row);

    root->addStretch();

    // ── Status refresh timer ────────────────────────────────────────────────
    status_timer_ = new QTimer(this);
    status_timer_->setInterval(500);
    connect(status_timer_, &QTimer::timeout,
            this, &EncoderWidget::onRefreshStatus);
    status_timer_->start();

    // Load current config into UI
    loadConfigToUi();
}

void EncoderWidget::loadConfigToUi()
{
    EncoderConfig cfg = mc1enc_get_config();

    host_edit_->setText(QString::fromStdString(cfg.host));
    port_spin_->setValue(cfg.port);
    mount_edit_->setText(QString::fromStdString(cfg.mount));
    user_edit_->setText(QString::fromStdString(cfg.username));
    pass_edit_->setText(QString::fromStdString(cfg.password));
    name_edit_->setText(QString::fromStdString(cfg.station_name));
    desc_edit_->setText(QString::fromStdString(cfg.description));
    genre_edit_->setText(QString::fromStdString(cfg.genre));
    url_edit_->setText(QString::fromStdString(cfg.url));
    bitrate_combo_->setCurrentText(QString::number(cfg.bitrate));
}

EncoderConfig EncoderWidget::configFromUi() const
{
    EncoderConfig cfg;
    cfg.host         = host_edit_->text().toStdString();
    cfg.port         = static_cast<uint16_t>(port_spin_->value());
    cfg.mount        = mount_edit_->text().toStdString();
    cfg.username     = user_edit_->text().toStdString();
    cfg.password     = pass_edit_->text().toStdString();
    cfg.station_name = name_edit_->text().toStdString();
    cfg.description  = desc_edit_->text().toStdString();
    cfg.genre        = genre_edit_->text().toStdString();
    cfg.url          = url_edit_->text().toStdString();
    cfg.bitrate      = bitrate_combo_->currentText().toInt();
    return cfg;
}

void EncoderWidget::onConnectClicked()
{
    if (mc1enc_is_connected()) {
        mc1enc_stop_streaming();
    } else {
        onApplyConfig();
        mc1enc_start_streaming();
    }
}

void EncoderWidget::onApplyConfig()
{
    mc1enc_update_config(configFromUi());
}

void EncoderWidget::onRefreshStatus()
{
    int state = mc1enc_state();
    bool connected = mc1enc_is_connected();
    uint64_t bytes = mc1enc_bytes_sent();

    // Status dot color
    const char* dot_color = "#555";     // idle = gray
    const char* status_text = "Idle";

    switch (state) {
    case 0:  // idle
        dot_color = "#555";
        status_text = "Idle";
        break;
    case 1:  // connecting
        dot_color = "#F59E0B";  // amber
        status_text = "Connecting...";
        break;
    case 2:  // live
        dot_color = "#22C55E";  // green
        status_text = "LIVE — Streaming";
        break;
    case 3:  // error
        dot_color = "#EF4444";  // red
        status_text = "Error — will retry";
        break;
    }

    dot_label_->setStyleSheet(
        QStringLiteral("color: %1; font-size: 14px;").arg(QLatin1String(dot_color)));
    status_label_->setText(QLatin1String(status_text));

    // Format bytes sent
    QString bytes_str;
    if (bytes < 1024)
        bytes_str = QStringLiteral("%1 B").arg(bytes);
    else if (bytes < 1024 * 1024)
        bytes_str = QStringLiteral("%1 KB").arg(bytes / 1024);
    else if (bytes < 1024ULL * 1024 * 1024)
        bytes_str = QStringLiteral("%1 MB").arg(bytes / (1024 * 1024));
    else
        bytes_str = QStringLiteral("%1 GB").arg(
            static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
    bytes_label_->setText(bytes_str);

    // Toggle button text and style
    if (connected) {
        connect_btn_->setText(QStringLiteral("Disconnect"));
        connect_btn_->setObjectName(QStringLiteral("disconnect"));
    } else {
        connect_btn_->setText(QStringLiteral("Connect"));
        connect_btn_->setObjectName(QString());
    }
    // Force style re-evaluation after objectName change
    connect_btn_->setStyleSheet(connect_btn_->styleSheet());

    // Disable fields while connected
    bool editable = !connected && state != 1;
    host_edit_->setEnabled(editable);
    port_spin_->setEnabled(editable);
    mount_edit_->setEnabled(editable);
    user_edit_->setEnabled(editable);
    pass_edit_->setEnabled(editable);
    codec_combo_->setEnabled(editable);
    bitrate_combo_->setEnabled(editable);
}

} // namespace mc1enc
