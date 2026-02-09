/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * preview_audio_studio.cpp — Preview Audio Studio dialog
 *
 * Three modes of operation:
 *  1. Passthrough (global input)  – PortAudio input → ring buffer → PA output
 *  2. Slot eavesdrop              – pre-encode PCM tap → ring buffer → PA output
 *  3. Stream Monitor              – QMediaPlayer → live HTTP stream from server
 *                                   + IcyStreamReader (ICY1/ICY2.2 protocol monitor)
 *                                   + periodic Icecast2 JSON stats polling
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "preview_audio_studio.h"
#include "audio_pipeline.h"
#include "encoder_slot.h"

#include <QAudioDevice>
#include <QAudioOutput>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMediaDevices>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#ifdef HAVE_PORTAUDIO
#include <portaudio.h>
#endif

namespace mc1 {

/* ── Device category detection ─────────────────────────────────────────── */
static QString detectCategory(const QString &name)
{
    const QString lo = name.toLower();
    if (lo.contains(QStringLiteral("bluetooth")) || lo.contains(QStringLiteral("bt audio")) ||
        lo.contains(QStringLiteral("airpods"))   || lo.contains(QStringLiteral("headset"))  ||
        lo.contains(QStringLiteral("a2dp"))      || lo.contains(QStringLiteral("wireless")))
        return QStringLiteral("Bluetooth");
    if (lo.contains(QStringLiteral("usb"))       || lo.contains(QStringLiteral("focusrite")) ||
        lo.contains(QStringLiteral("scarlett"))  || lo.contains(QStringLiteral("behringer")) ||
        lo.contains(QStringLiteral("m-audio"))   || lo.contains(QStringLiteral("presonus"))  ||
        lo.contains(QStringLiteral("steinberg")) || lo.contains(QStringLiteral("audient"))   ||
        lo.contains(QStringLiteral("volt"))      || lo.contains(QStringLiteral("tascam"))    ||
        lo.contains(QStringLiteral("zoom h"))    || lo.contains(QStringLiteral("ua-"))       ||
        lo.contains(QStringLiteral("motu"))      || lo.contains(QStringLiteral("roland"))    ||
        lo.contains(QStringLiteral("native instruments"))                                    ||
        lo.contains(QStringLiteral("interface")) || lo.contains(QStringLiteral("audio box")))
        return QStringLiteral("USB Audio");
    return QStringLiteral("System");
}

/* ── Constructor ─────────────────────────────────────────────────────────── */
PreviewAudioStudio::PreviewAudioStudio(QWidget *parent)
    : QDialog(parent, Qt::Tool | Qt::WindowStaysOnTopHint)
{
    setWindowTitle(QStringLiteral("Preview Audio Studio"));
    setMinimumWidth(600);
    setMinimumHeight(680);

    auto *root = new QVBoxLayout(this);
    root->setSpacing(10);

    /* ═══ OUTPUT DEVICE ════════════════════════════════════════════════════ */
    auto *grp_out = new QGroupBox(
        QStringLiteral("Headphones, Speakers and USB Output Devices"));
    auto *out_lay = new QHBoxLayout(grp_out);
    out_lay->addWidget(new QLabel(QStringLiteral("Route to:")));
    cmb_output_ = new QComboBox;
    cmb_output_->setMinimumWidth(280);
    out_lay->addWidget(cmb_output_, 1);
    btn_scan_usb_ = new QPushButton(QStringLiteral("Scan USB"));
    btn_scan_usb_->setToolTip(QStringLiteral("Re-scan for USB audio interfaces"));
    out_lay->addWidget(btn_scan_usb_);
    root->addWidget(grp_out);

    /* ═══ CAPTURE LIVE MONITOR ══════════════════════════════════════════════ */
    auto *grp_pass = new QGroupBox(
        QStringLiteral("Capture Live Monitor  (Passthrough, route audio to Output device)"));
    auto *pass_lay = new QVBoxLayout(grp_pass);

    auto *src_row = new QHBoxLayout;
    src_row->addWidget(new QLabel(QStringLiteral("Source:")));
    cmb_input_source_ = new QComboBox;
    cmb_input_source_->setMinimumWidth(300);
    src_row->addWidget(cmb_input_source_, 1);
    pass_lay->addLayout(src_row);

    /* Input sample rate + channels — auto-update when source device changes */
    auto *fmt_row = new QHBoxLayout;
    fmt_row->addWidget(new QLabel(QStringLiteral("Sample Rate:")));
    cmb_input_sample_rate_ = new QComboBox;
    cmb_input_sample_rate_->addItem(QStringLiteral("22050 Hz"), 22050);
    cmb_input_sample_rate_->addItem(QStringLiteral("44100 Hz"), 44100);
    cmb_input_sample_rate_->addItem(QStringLiteral("48000 Hz"), 48000);
    cmb_input_sample_rate_->addItem(QStringLiteral("88200 Hz"), 88200);
    cmb_input_sample_rate_->addItem(QStringLiteral("96000 Hz"), 96000);
    cmb_input_sample_rate_->setCurrentIndex(1); /* 44100 Hz default */
    fmt_row->addWidget(cmb_input_sample_rate_);
    fmt_row->addSpacing(12);
    fmt_row->addWidget(new QLabel(QStringLiteral("Channels:")));
    cmb_input_channels_ = new QComboBox;
    cmb_input_channels_->addItem(QStringLiteral("Mono"),   1);
    cmb_input_channels_->addItem(QStringLiteral("Stereo"), 2);
    cmb_input_channels_->setCurrentIndex(1); /* Stereo default */
    fmt_row->addWidget(cmb_input_channels_);
    fmt_row->addStretch();
    pass_lay->addLayout(fmt_row);

    lbl_feedback_warn_ = new QLabel;
    lbl_feedback_warn_->setStyleSheet(QStringLiteral(
        "color: #ffaa00; font-weight: bold; padding: 4px; "
        "border: 1px solid #cc8800; border-radius: 4px; background: #332200;"));
    lbl_feedback_warn_->setWordWrap(true);
    lbl_feedback_warn_->hide();
    pass_lay->addWidget(lbl_feedback_warn_);

    auto *lvl_row = new QHBoxLayout;
    lvl_row->addWidget(new QLabel(QStringLiteral("Level:")));
    pb_level_ = new QProgressBar;
    pb_level_->setRange(0, 100);
    pb_level_->setValue(0);
    pb_level_->setTextVisible(false);
    pb_level_->setFixedHeight(14);
    pb_level_->setStyleSheet(QStringLiteral(
        "QProgressBar { border: 1px solid #334; background: #111; border-radius: 3px; }"
        "QProgressBar::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "  stop:0 #1a8a3a, stop:0.65 #aacc22, stop:1 #cc3322); border-radius: 2px; }"));
    lvl_row->addWidget(pb_level_, 1);
    pass_lay->addLayout(lvl_row);

    auto *pass_ctl = new QHBoxLayout;
    lbl_passthru_stat_ = new QLabel(QStringLiteral("Stopped"));
    lbl_passthru_stat_->setStyleSheet(QStringLiteral("color: #8899aa;"));
    pass_ctl->addWidget(lbl_passthru_stat_, 1);
    btn_start_stop_ = new QPushButton(QStringLiteral("Start Preview"));
    btn_start_stop_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #1a6a3a; color: white; padding: 5px 18px; "
        "border: 1px solid #2a8a4a; border-radius: 5px; font-weight: bold; }"
        "QPushButton:hover { background: #2a8a4a; }"));
    pass_ctl->addWidget(btn_start_stop_);
    pass_lay->addLayout(pass_ctl);
    root->addWidget(grp_pass);

    /* ═══ STREAM MONITOR ═══════════════════════════════════════════════════ */
    auto *grp_stream = new QGroupBox(QStringLiteral("Stream Monitor  \u2014  Tune In (ICY1/ICY2.2)"));
    auto *stream_lay = new QVBoxLayout(grp_stream);

    /* Stream selector row */
    auto *stream_sel = new QHBoxLayout;
    stream_sel->addWidget(new QLabel(QStringLiteral("Stream:")));
    cmb_stream_ = new QComboBox;
    cmb_stream_->setMinimumWidth(300);
    stream_sel->addWidget(cmb_stream_, 1);
    btn_tune_in_ = new QPushButton(QStringLiteral("Tune In"));
    btn_tune_in_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #1a3a7a; color: white; padding: 5px 16px; "
        "border: 1px solid #2a5aaa; border-radius: 5px; font-weight: bold; }"
        "QPushButton:hover { background: #2a5aaa; }"));
    stream_sel->addWidget(btn_tune_in_);
    stream_lay->addLayout(stream_sel);

    /* Status + mini player controls */
    auto *status_row = new QHBoxLayout;
    lbl_stream_status_ = new QLabel(QStringLiteral("Not connected"));
    lbl_stream_status_->setStyleSheet(QStringLiteral("color: #8899aa;"));
    status_row->addWidget(lbl_stream_status_, 1);

    btn_play_pause_ = new QPushButton(QStringLiteral("\u25b6"));   /* ▶ */
    btn_play_pause_->setFixedSize(30, 26);
    btn_play_pause_->setToolTip(QStringLiteral("Play / Pause stream"));
    btn_play_pause_->setEnabled(false);
    btn_play_pause_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #1a3a7a; color: white; border: 1px solid #2a5aaa; "
        "border-radius: 4px; font-size: 12px; } "
        "QPushButton:hover { background: #2a5aaa; } "
        "QPushButton:disabled { background: #222; color: #555; border-color: #333; }"));
    status_row->addWidget(btn_play_pause_);

    btn_stop_stream_ = new QPushButton(QStringLiteral("\u25a0")); /* ■ */
    btn_stop_stream_->setFixedSize(30, 26);
    btn_stop_stream_->setToolTip(QStringLiteral("Stop stream"));
    btn_stop_stream_->setEnabled(false);
    btn_stop_stream_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #6a1a1a; color: white; border: 1px solid #aa3322; "
        "border-radius: 4px; font-size: 12px; } "
        "QPushButton:hover { background: #aa3322; } "
        "QPushButton:disabled { background: #222; color: #555; border-color: #333; }"));
    status_row->addWidget(btn_stop_stream_);
    stream_lay->addLayout(status_row);

    /* Live metrics grid */
    auto *metrics_row1 = new QHBoxLayout;
    metrics_row1->addWidget(new QLabel(QStringLiteral("Listeners:")));
    lbl_listeners_ = new QLabel(QStringLiteral("\u2014"));
    lbl_listeners_->setStyleSheet(QStringLiteral("color: #66ccff; font-weight: bold;"));
    metrics_row1->addWidget(lbl_listeners_);
    metrics_row1->addSpacing(16);
    metrics_row1->addWidget(new QLabel(QStringLiteral("Bitrate:")));
    lbl_stream_bitrate_ = new QLabel(QStringLiteral("\u2014"));
    lbl_stream_bitrate_->setStyleSheet(QStringLiteral("color: #66ccff; font-weight: bold;"));
    metrics_row1->addWidget(lbl_stream_bitrate_);
    metrics_row1->addSpacing(16);
    metrics_row1->addWidget(new QLabel(QStringLiteral("Protocol:")));
    lbl_server_type_ = new QLabel(QStringLiteral("\u2014"));
    lbl_server_type_->setStyleSheet(QStringLiteral("color: #aabbcc;"));
    metrics_row1->addWidget(lbl_server_type_);
    metrics_row1->addStretch();
    stream_lay->addLayout(metrics_row1);

    auto *metrics_row2 = new QHBoxLayout;
    metrics_row2->addWidget(new QLabel(QStringLiteral("Now Playing:")));
    lbl_now_playing_ = new QLabel(QStringLiteral("\u2014 idle \u2014"));
    lbl_now_playing_->setStyleSheet(QStringLiteral("color: #ccddee; font-style: italic;"));
    lbl_now_playing_->setWordWrap(true);
    metrics_row2->addWidget(lbl_now_playing_, 1);
    stream_lay->addLayout(metrics_row2);

    root->addWidget(grp_stream);

    /* ═══ BOTTOM TAB WIDGET ═════════════════════════════════════════════════ */
    bottom_tabs_ = new QTabWidget;
    bottom_tabs_->setMinimumHeight(180);

    /* Tab 1: Protocol Log */
    log_view_ = new QPlainTextEdit;
    log_view_->setReadOnly(true);
    log_view_->setMaximumBlockCount(2000);
    log_view_->setFont(QFont(QStringLiteral("Consolas"), 8));
    log_view_->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background: #0d1117; color: #88ccaa; "
        "border: none; font-family: Consolas, 'Courier New', monospace; }"));
    log_view_->setPlaceholderText(QStringLiteral(
        "ICY protocol events and diagnostics will appear here when tuned in..."));
    bottom_tabs_->addTab(log_view_, QStringLiteral("Protocol Log"));

    /* Tab 2: ICY Metadata */
    icy_meta_view_ = new QPlainTextEdit;
    icy_meta_view_->setReadOnly(true);
    icy_meta_view_->setFont(QFont(QStringLiteral("Consolas"), 8));
    icy_meta_view_->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background: #0d1117; color: #aaccee; "
        "border: none; font-family: Consolas, 'Courier New', monospace; }"));
    icy_meta_view_->setPlaceholderText(QStringLiteral(
        "ICY1/ICY2.2 headers and stream metadata will appear here when tuned in..."));
    bottom_tabs_->addTab(icy_meta_view_, QStringLiteral("ICY Metadata"));

    root->addWidget(bottom_tabs_);

    /* ═══ SETUP ═════════════════════════════════════════════════════════════ */
    populateDevices();
    buildStreamCombo();

    /* Qt Multimedia for stream playback */
    nam_ = new QNetworkAccessManager(this);
    connect(nam_, &QNetworkAccessManager::finished,
            this, &PreviewAudioStudio::onStatsReply);

    stream_player_ = new QMediaPlayer(this);
    connect(stream_player_, &QMediaPlayer::playbackStateChanged,
            this, &PreviewAudioStudio::onPlayerStateChanged);
    connect(stream_player_, &QMediaPlayer::errorOccurred,
            this, [this](QMediaPlayer::Error, const QString &) { onPlayerError(); });
    connect(stream_player_, &QMediaPlayer::metaDataChanged,
            this, &PreviewAudioStudio::onMetadataChanged);

    /* IcyStreamReader — separate protocol monitor connection */
    icy_reader_ = new IcyStreamReader(this);
    connect(icy_reader_, &IcyStreamReader::connected,
            this, &PreviewAudioStudio::onIcyConnected);
    connect(icy_reader_, &IcyStreamReader::metaUpdated,
            this, &PreviewAudioStudio::onIcyMetaUpdated);
    connect(icy_reader_, &IcyStreamReader::protocolEvent,
            this, &PreviewAudioStudio::onIcyProtocolEvent);
    connect(icy_reader_, &IcyStreamReader::disconnected,
            this, &PreviewAudioStudio::onIcyDisconnected);
    connect(icy_reader_, &IcyStreamReader::streamError,
            this, &PreviewAudioStudio::onIcyError);

    /* Icecast2 JSON stats polling timer */
    stats_poll_timer_ = new QTimer(this);
    stats_poll_timer_->setInterval(8000);
    connect(stats_poll_timer_, &QTimer::timeout, this, &PreviewAudioStudio::onPollStats);

    /* Level meter refresh (80 ms) */
    auto *lvl_timer = new QTimer(this);
    connect(lvl_timer, &QTimer::timeout, this, [this]() {
        float pk  = peak_level_.exchange(0.0f, std::memory_order_relaxed);
        int   pct = static_cast<int>(std::min(1.0f, pk) * 100.0f);
        pb_level_->setValue(pct);
    });
    lvl_timer->start(80);

    /* Signal connections */
    connect(btn_start_stop_,   &QPushButton::clicked,
            this,              &PreviewAudioStudio::onStartStop);
    connect(cmb_input_source_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,              &PreviewAudioStudio::onInputSourceChanged);
    connect(cmb_output_,       QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,              &PreviewAudioStudio::onOutputDeviceChanged);
    connect(btn_scan_usb_,     &QPushButton::clicked,
            this,              &PreviewAudioStudio::onScanUsbDevices);
    connect(btn_tune_in_,      &QPushButton::clicked,
            this,              &PreviewAudioStudio::onTuneIn);
    connect(cmb_stream_,       QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,              &PreviewAudioStudio::onStreamComboChanged);

    /* Mini player controls */
    connect(btn_play_pause_, &QPushButton::clicked, this, [this]() {
        if (!stream_player_) return;
        if (stream_player_->playbackState() == QMediaPlayer::PlayingState)
            stream_player_->pause();
        else
            stream_player_->play();
    });
    connect(btn_stop_stream_, &QPushButton::clicked, this, [this]() {
        if (stream_tuned_in_) stopStream();
    });
}

PreviewAudioStudio::~PreviewAudioStudio()
{
    stopPassthrough();
    stopStream();
}

/* ── Public API ─────────────────────────────────────────────────────────── */
void PreviewAudioStudio::setSlotList(const std::vector<PreviewAudioStudio::SlotInfo> &slot_entries)
{
    slot_list_ = slot_entries;
    buildInputCombo();
    buildStreamCombo();
}

/* ── Device enumeration ─────────────────────────────────────────────────── */
void PreviewAudioStudio::populateDevices()
{
#ifdef HAVE_PORTAUDIO
    if (Pa_Initialize() != paNoError) return;

    output_devs_.clear();
    input_devs_.clear();

    int count   = Pa_GetDeviceCount();
    int def_out = Pa_GetDefaultOutputDevice();
    int def_in  = Pa_GetDefaultInputDevice();

    for (int i = 0; i < count; ++i) {
        const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
        if (!info) continue;
        DeviceEntry e;
        e.pa_index    = i;
        e.name        = QString::fromUtf8(info->name ? info->name : "(unknown)");
        e.category    = detectCategory(e.name);
        e.in_ch       = info->maxInputChannels;
        e.out_ch      = info->maxOutputChannels;
        e.sample_rate = info->defaultSampleRate;
        e.is_input    = (e.in_ch  > 0);
        e.is_output   = (e.out_ch > 0);
        if (e.is_output) {
            DeviceEntry d = e;
            if (i == def_out) d.name += QStringLiteral(" (Default)");
            output_devs_.push_back(d);
        }
        if (e.is_input) {
            DeviceEntry d = e;
            if (i == def_in) d.name += QStringLiteral(" (Default)");
            input_devs_.push_back(d);
        }
    }
    Pa_Terminate();
#endif

    /* Also grab Qt audio device IDs for QAudioOutput routing */
    const auto qt_outputs = QMediaDevices::audioOutputs();
    for (auto &e : output_devs_) {
        QString lo = e.name.toLower().remove(QStringLiteral(" (default)")).trimmed();
        for (const auto &qd : qt_outputs) {
            if (qd.description().toLower().contains(lo) ||
                lo.contains(qd.description().toLower())) {
                e.qt_device_id = qd.id();
                break;
            }
        }
    }

    buildOutputCombo();
    buildInputCombo();
}

void PreviewAudioStudio::buildOutputCombo()
{
    cmb_output_->blockSignals(true);
    cmb_output_->clear();
    auto addGroup = [&](const QString &cat) {
        bool hdr = false;
        for (auto &d : output_devs_) {
            if (d.category != cat) continue;
            if (!hdr) {
                cmb_output_->addItem(
                    QStringLiteral("--- ") + cat + QStringLiteral(" ---"), -1);
                hdr = true;
            }
            cmb_output_->addItem(d.name, d.pa_index);
        }
    };
    addGroup(QStringLiteral("USB Audio"));
    addGroup(QStringLiteral("Bluetooth"));
    addGroup(QStringLiteral("System"));
    cmb_output_->blockSignals(false);
    for (int i = 0; i < cmb_output_->count(); ++i) {
        if (cmb_output_->itemText(i).contains(QStringLiteral("(Default)"))) {
            cmb_output_->setCurrentIndex(i); break;
        }
    }
    if (cmb_output_->currentIndex() <= 0 && cmb_output_->count() > 1)
        cmb_output_->setCurrentIndex(1);
    onOutputDeviceChanged(cmb_output_->currentIndex());
}

void PreviewAudioStudio::buildInputCombo()
{
    cmb_input_source_->blockSignals(true);
    cmb_input_source_->clear();
    cmb_input_source_->addItem(QStringLiteral("--- Global Input Devices ---"), -2);
    for (auto &d : input_devs_)
        cmb_input_source_->addItem(
            QStringLiteral("[") + d.category + QStringLiteral("] ") + d.name,
            d.pa_index);
    if (!slot_list_.empty()) {
        cmb_input_source_->addItem(
            QStringLiteral("--- Encoder Slot Eavesdrop ---"), -2);
        for (auto &s : slot_list_)
            cmb_input_source_->addItem(
                QString(QStringLiteral("Slot %1: %2 (pre-encode PCM)"))
                    .arg(s.slot_id).arg(s.name),
                -(s.slot_id + 100));
    }
    cmb_input_source_->blockSignals(false);
    for (int i = 0; i < cmb_input_source_->count(); ++i) {
        if (cmb_input_source_->itemText(i).contains(QStringLiteral("(Default)"))) {
            cmb_input_source_->setCurrentIndex(i); break;
        }
    }
    if (cmb_input_source_->currentIndex() <= 0 && cmb_input_source_->count() > 1)
        cmb_input_source_->setCurrentIndex(1);
}

void PreviewAudioStudio::buildStreamCombo()
{
    cmb_stream_->blockSignals(true);
    cmb_stream_->clear();
    if (slot_list_.empty()) {
        cmb_stream_->addItem(QStringLiteral("(No active encoder slots)"), -1);
    } else {
        for (int i = 0; i < static_cast<int>(slot_list_.size()); ++i) {
            const auto &s = slot_list_[i];
            cmb_stream_->addItem(
                QString(QStringLiteral("Slot %1: %2  \u2014  %3"))
                    .arg(s.slot_id).arg(s.name).arg(s.listen_url),
                i);
        }
    }
    cmb_stream_->blockSignals(false);
    stream_combo_slot_idx_ = (cmb_stream_->count() > 0) ? 0 : -1;
}

/* ── Passthrough slots ──────────────────────────────────────────────────── */
void PreviewAudioStudio::onOutputDeviceChanged(int /*idx*/)
{
    int v = cmb_output_->currentData().toInt();
    out_dev_pa_idx_ = (v >= 0) ? v : -1;
    checkFeedbackRisk();
}

void PreviewAudioStudio::onInputSourceChanged(int /*idx*/)
{
    int v = cmb_input_source_->currentData().toInt();
    if (v >= 0) {
        in_dev_pa_idx_     = v;
        eavesdrop_slot_id_ = -1;

        /* Update SR/Ch combos for the selected PortAudio device */
        if (cmb_input_sample_rate_ && cmb_input_channels_) {
            double def_sr = 44100.0;
            int    max_ch = 2;
            for (const auto &d : input_devs_) {
                if (d.pa_index == v) {
                    def_sr = d.sample_rate;
                    max_ch = std::max(d.in_ch, 1);
                    break;
                }
            }
            /* Select nearest sample rate */
            {
                QSignalBlocker blk(cmb_input_sample_rate_);
                int target = static_cast<int>(def_sr);
                int best = -1, nearest_diff = INT_MAX;
                for (int i = 0; i < cmb_input_sample_rate_->count(); ++i) {
                    int diff = std::abs(cmb_input_sample_rate_->itemData(i).toInt() - target);
                    if (diff < nearest_diff) { nearest_diff = diff; best = i; }
                }
                if (best >= 0) cmb_input_sample_rate_->setCurrentIndex(best);
            }
            /* Update channels based on device capability */
            {
                QSignalBlocker blk(cmb_input_channels_);
                int prev_ch = cmb_input_channels_->currentData().toInt();
                cmb_input_channels_->clear();
                cmb_input_channels_->addItem(QStringLiteral("Mono"),   1);
                if (max_ch >= 2)
                    cmb_input_channels_->addItem(QStringLiteral("Stereo"), 2);
                int restore = -1;
                for (int i = 0; i < cmb_input_channels_->count(); ++i) {
                    if (cmb_input_channels_->itemData(i).toInt() == prev_ch) { restore = i; break; }
                }
                cmb_input_channels_->setCurrentIndex(restore >= 0 ? restore : cmb_input_channels_->count() - 1);
            }
        }
    } else if (v == -2) {
        in_dev_pa_idx_     = -1;
        eavesdrop_slot_id_ = -1;
    } else {
        in_dev_pa_idx_     = -1;
        eavesdrop_slot_id_ = -(v + 100);
    }
    checkFeedbackRisk();
}

void PreviewAudioStudio::onScanUsbDevices()
{
    bool was = passthrough_running_;
    if (was) stopPassthrough();
    populateDevices();
    int usb = 0;
    for (auto &d : output_devs_) if (d.category == QStringLiteral("USB Audio")) ++usb;
    lbl_passthru_stat_->setText(
        QString(QStringLiteral("USB scan: %1 interface(s) found")).arg(usb));
    if (usb == 0)
        QMessageBox::information(this, QStringLiteral("USB Audio Scan"),
            QStringLiteral("No USB audio interfaces detected.\n"
                           "Connect your USB device and try again."));
    else
        QMessageBox::information(this, QStringLiteral("USB Audio Scan"),
            QString(QStringLiteral("Found %1 USB audio interface(s).\n"
                                   "USB devices are listed first in the Output dropdown."))
                .arg(usb));
}

void PreviewAudioStudio::checkFeedbackRisk()
{
    lbl_feedback_warn_->hide();
    if (eavesdrop_slot_id_ >= 0) return;
    if (out_dev_pa_idx_ < 0 || in_dev_pa_idx_ < 0) return;
    if (out_dev_pa_idx_ == in_dev_pa_idx_) {
        lbl_feedback_warn_->setText(QStringLiteral(
            "\u26a0  WARNING: Same device for input and output \u2014 high feedback risk! "
            "Use headphones as output device."));
        lbl_feedback_warn_->show();
        return;
    }
    auto getName = [&](int pa_idx, const std::vector<DeviceEntry> &list) -> QString {
        for (auto &d : list)
            if (d.pa_index == pa_idx)
                return d.name.toLower().remove(QStringLiteral(" (default)")).trimmed();
        return {};
    };
    QString on = getName(out_dev_pa_idx_, output_devs_);
    QString in = getName(in_dev_pa_idx_,  input_devs_);
    int common = 0;
    int limit  = std::min(on.length(), in.length());
    for (int i = 0; i < limit; ++i)
        if (on[i] == in[i]) ++common; else break;
    if (common >= 10) {
        lbl_feedback_warn_->setText(QStringLiteral(
            "\u26a0  CAUTION: Input and Output may be the same physical device. "
            "Use headphones to avoid feedback."));
        lbl_feedback_warn_->show();
    }
}

void PreviewAudioStudio::onStartStop()
{
    if (passthrough_running_) stopPassthrough();
    else                      startPassthrough();
}

/* ── Passthrough engine ─────────────────────────────────────────────────── */
void PreviewAudioStudio::startPassthrough()
{
#ifdef HAVE_PORTAUDIO
    if (passthrough_running_) return;
    if (out_dev_pa_idx_ < 0) {
        lbl_passthru_stat_->setText(QStringLiteral("Select an output device first"));
        return;
    }
    if (in_dev_pa_idx_ < 0 && eavesdrop_slot_id_ < 0) {
        lbl_passthru_stat_->setText(QStringLiteral("Select an input source first"));
        return;
    }
    ring_head_.store(0, std::memory_order_relaxed);
    ring_tail_.store(0, std::memory_order_relaxed);

    if (Pa_Initialize() != paNoError) {
        lbl_passthru_stat_->setText(QStringLiteral("PortAudio init failed"));
        return;
    }
    const PaDeviceInfo *out_info = Pa_GetDeviceInfo(out_dev_pa_idx_);
    double sr = out_info ? out_info->defaultSampleRate : 44100.0;

    /* Output stream */
    {
        PaStreamParameters op{};
        op.device                    = out_dev_pa_idx_;
        op.channelCount              = 2;
        op.sampleFormat              = paFloat32;
        op.suggestedLatency          = out_info ? out_info->defaultLowOutputLatency : 0.02;
        op.hostApiSpecificStreamInfo = nullptr;
        PaError err = Pa_OpenStream(&pa_output_, nullptr, &op,
                                    sr, 256, paClipOff, &paOutputCb, this);
        if (err != paNoError) {
            lbl_passthru_stat_->setText(
                QStringLiteral("Output: ") + Pa_GetErrorText(err));
            Pa_Terminate(); return;
        }
        Pa_StartStream(pa_output_);
    }
    /* Input stream */
    if (in_dev_pa_idx_ >= 0) {
        const PaDeviceInfo *in_info = Pa_GetDeviceInfo(in_dev_pa_idx_);
        /* Use user-selected channels; clamp to device capability */
        int desired_ch = cmb_input_channels_ ? cmb_input_channels_->currentData().toInt() : 2;
        int dev_max_ch = in_info ? std::max(in_info->maxInputChannels, 1) : 1;
        in_channels_ = std::min(desired_ch, dev_max_ch);
        if (in_channels_ < 1) in_channels_ = 1;
        PaStreamParameters ip{};
        ip.device                    = in_dev_pa_idx_;
        ip.channelCount              = in_channels_;
        ip.sampleFormat              = paFloat32;
        ip.suggestedLatency          = in_info ? in_info->defaultLowInputLatency : 0.02;
        ip.hostApiSpecificStreamInfo = nullptr;
        PaError err = Pa_OpenStream(&pa_input_, &ip, nullptr,
                                    sr, 256, paClipOff, &paInputCb, this);
        if (err == paNoError) Pa_StartStream(pa_input_);
    }
    /* Slot eavesdrop */
    if (eavesdrop_slot_id_ >= 0 && g_pipeline) {
        int sid = eavesdrop_slot_id_;
        g_pipeline->for_each_slot([this, sid](int id, EncoderSlot &slot) {
            if (id != sid) return;
            slot.set_pcm_tap([this](const float *pcm, size_t frames, int ch, int) {
                pushStereo(pcm, static_cast<int>(frames), ch);
            });
        });
    }
    passthrough_running_ = true;
    lbl_passthru_stat_->setText(QStringLiteral("Preview active \u2014 monitoring..."));
    btn_start_stop_->setText(QStringLiteral("Stop Preview"));
    btn_start_stop_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #8a2a1a; color: white; padding: 5px 18px; "
        "border: 1px solid #cc3322; border-radius: 5px; font-weight: bold; }"
        "QPushButton:hover { background: #aa3a2a; }"));
#else
    lbl_passthru_stat_->setText(QStringLiteral("PortAudio not available"));
#endif
}

void PreviewAudioStudio::stopPassthrough()
{
#ifdef HAVE_PORTAUDIO
    if (!passthrough_running_) return;
    passthrough_running_ = false;
    if (eavesdrop_slot_id_ >= 0 && g_pipeline) {
        int sid = eavesdrop_slot_id_;
        g_pipeline->for_each_slot([sid](int id, EncoderSlot &slot) {
            if (id == sid) slot.clear_pcm_tap();
        });
    }
    if (pa_input_)  { Pa_StopStream(pa_input_);  Pa_CloseStream(pa_input_);  pa_input_  = nullptr; }
    if (pa_output_) { Pa_StopStream(pa_output_); Pa_CloseStream(pa_output_); pa_output_ = nullptr; }
    Pa_Terminate();
    lbl_passthru_stat_->setText(QStringLiteral("Stopped"));
    btn_start_stop_->setText(QStringLiteral("Start Preview"));
    btn_start_stop_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #1a6a3a; color: white; padding: 5px 18px; "
        "border: 1px solid #2a8a4a; border-radius: 5px; font-weight: bold; }"
        "QPushButton:hover { background: #2a8a4a; }"));
    pb_level_->setValue(0);
#endif
}

/* ── Stream Monitor ─────────────────────────────────────────────────────── */
void PreviewAudioStudio::onStreamComboChanged(int idx)
{
    stream_combo_slot_idx_ = (idx >= 0) ? cmb_stream_->itemData(idx).toInt() : -1;
}

void PreviewAudioStudio::onTuneIn()
{
    if (stream_tuned_in_) { stopStream(); return; }
    if (stream_combo_slot_idx_ < 0 ||
        stream_combo_slot_idx_ >= static_cast<int>(slot_list_.size())) {
        lbl_stream_status_->setText(QStringLiteral("No stream selected"));
        return;
    }
    startStream();
}

void PreviewAudioStudio::startStream()
{
    const SlotInfo &s = slot_list_[static_cast<size_t>(stream_combo_slot_idx_)];
    if (s.listen_url.isEmpty()) {
        lbl_stream_status_->setText(QStringLiteral("No stream URL for this slot"));
        return;
    }

    /* Clear previous log and metadata */
    log_view_->clear();
    icy_meta_view_->clear();
    logEvent(QString(QStringLiteral("=== Tuning in to: %1 ===")).arg(s.listen_url));

    /* Route QAudioOutput to the selected output device */
    QAudioDevice chosen_dev = QMediaDevices::defaultAudioOutput();
    if (out_dev_pa_idx_ >= 0) {
        for (auto &e : output_devs_) {
            if (e.pa_index != out_dev_pa_idx_) continue;
            QString base = e.name.toLower().remove(QStringLiteral(" (default)")).trimmed();
            for (const auto &qd : QMediaDevices::audioOutputs()) {
                if (qd.description().toLower().contains(base) ||
                    base.contains(qd.description().toLower())) {
                    chosen_dev = qd;
                    break;
                }
            }
            break;
        }
    }

    if (stream_audio_out_) {
        delete stream_audio_out_;
        stream_audio_out_ = nullptr;
    }
    stream_audio_out_ = new QAudioOutput(chosen_dev, this);
    stream_audio_out_->setVolume(1.0f);
    stream_player_->setAudioOutput(stream_audio_out_);
    stream_player_->setSource(QUrl(s.listen_url));
    stream_player_->play();

    /* Start ICY protocol monitor (independent TCP connection, no audio decode) */
    icy_reader_->connectToUrl(s.listen_url);

    stream_tuned_in_ = true;
    lbl_stream_status_->setText(
        QStringLiteral("Connecting to ") + s.listen_url + QStringLiteral("..."));
    btn_tune_in_->setText(QStringLiteral("Disconnect"));
    btn_tune_in_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #8a2a1a; color: white; padding: 5px 16px; "
        "border: 1px solid #cc3322; border-radius: 5px; font-weight: bold; }"
        "QPushButton:hover { background: #aa3a2a; }"));
    btn_play_pause_->setEnabled(true);
    btn_stop_stream_->setEnabled(true);

    /* Start Icecast2 JSON stats polling immediately */
    onPollStats();
    stats_poll_timer_->start();
}

void PreviewAudioStudio::stopStream()
{
    stats_poll_timer_->stop();
    icy_reader_->disconnectFromStream();
    if (stream_player_) {
        stream_player_->stop();
        stream_player_->setSource(QUrl());
    }
    stream_tuned_in_ = false;
    lbl_stream_status_->setText(QStringLiteral("Not connected"));
    btn_tune_in_->setText(QStringLiteral("Tune In"));
    btn_tune_in_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #1a3a7a; color: white; padding: 5px 16px; "
        "border: 1px solid #2a5aaa; border-radius: 5px; font-weight: bold; }"
        "QPushButton:hover { background: #2a5aaa; }"));
    btn_play_pause_->setEnabled(false);
    btn_stop_stream_->setEnabled(false);
    lbl_listeners_->setText(QStringLiteral("\u2014"));
    lbl_now_playing_->setText(QStringLiteral("\u2014 idle \u2014"));
    lbl_stream_bitrate_->setText(QStringLiteral("\u2014"));
    lbl_server_type_->setText(QStringLiteral("\u2014"));
    logEvent(QStringLiteral("=== Stream stopped ==="));
}

/* ── QMediaPlayer state ─────────────────────────────────────────────────── */
void PreviewAudioStudio::onPlayerStateChanged()
{
    if (!stream_player_) return;
    switch (stream_player_->playbackState()) {
        case QMediaPlayer::PlayingState:
            lbl_stream_status_->setText(QStringLiteral("\u25cf  Tuned in \u2014 streaming"));
            lbl_stream_status_->setStyleSheet(QStringLiteral("color: #44ee88;"));
            btn_play_pause_->setText(QStringLiteral("\u23f8")); /* ⏸ */
            break;
        case QMediaPlayer::PausedState:
            lbl_stream_status_->setText(QStringLiteral("\u23f8  Paused"));
            lbl_stream_status_->setStyleSheet(QStringLiteral("color: #aabb66;"));
            btn_play_pause_->setText(QStringLiteral("\u25b6")); /* ▶ */
            break;
        case QMediaPlayer::StoppedState:
            if (stream_tuned_in_)
                lbl_stream_status_->setText(QStringLiteral("Buffering..."));
            lbl_stream_status_->setStyleSheet(QStringLiteral("color: #8899aa;"));
            btn_play_pause_->setText(QStringLiteral("\u25b6"));
            break;
    }
}

void PreviewAudioStudio::onPlayerError()
{
    if (!stream_player_) return;
    const QString err = stream_player_->errorString();
    lbl_stream_status_->setText(QStringLiteral("Stream error: ") + err);
    lbl_stream_status_->setStyleSheet(QStringLiteral("color: #ee4444;"));
    logEvent(QStringLiteral("[Player] ERROR: ") + err);
}

void PreviewAudioStudio::onMetadataChanged()
{
    if (!stream_player_) return;
    const auto &meta = stream_player_->metaData();
    QString title = meta.value(QMediaMetaData::Title).toString();
    if (!title.isEmpty()) {
        lbl_now_playing_->setText(title);
        /* Also log it — QMediaPlayer may get ICY title via HTTP redirects */
        logEvent(QStringLiteral("[Player] QMediaMetaData title: ") + title);
    }
}

/* ── IcyStreamReader callbacks ──────────────────────────────────────────── */
void PreviewAudioStudio::onIcyConnected(const mc1::IcyStreamReader::IcyHeaders &hdrs)
{
    logEvent(QStringLiteral("[ICY] Headers received \u2014 ICY2.2=") +
             (hdrs.is_icy2 ? QStringLiteral("yes") : QStringLiteral("no")));

    /* Update server type label */
    QString srv = hdrs.server.isEmpty() ? hdrs.icy_version : hdrs.server;
    if (!srv.isEmpty())
        lbl_server_type_->setText(srv);

    /* Update bitrate from ICY header if stats polling hasn't filled it yet */
    if (hdrs.bitrate_kbps > 0)
        lbl_stream_bitrate_->setText(
            QString::number(hdrs.bitrate_kbps) + QStringLiteral(" kbps"));

    /* Populate ICY Metadata tab */
    populateIcyMetadataTab(hdrs);

    /* Switch to ICY Metadata tab so user sees the headers */
    bottom_tabs_->setCurrentWidget(icy_meta_view_);
}

void PreviewAudioStudio::onIcyMetaUpdated(const QString &title,
                                           const QString &stream_url,
                                           const QString &raw_meta)
{
    Q_UNUSED(stream_url)
    if (!title.isEmpty())
        lbl_now_playing_->setText(title);

    /* Append to ICY Metadata tab as a live update */
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    icy_meta_view_->appendPlainText(
        QStringLiteral("\n[%1] StreamTitle Update:\n  %2").arg(ts).arg(raw_meta));
}

void PreviewAudioStudio::onIcyProtocolEvent(const QString &line)
{
    logEvent(line);
}

void PreviewAudioStudio::onIcyDisconnected()
{
    logEvent(QStringLiteral("[ICY] Protocol monitor disconnected"));
}

void PreviewAudioStudio::onIcyError(const QString &msg)
{
    logEvent(QStringLiteral("[ICY] ERROR: ") + msg);
    lbl_stream_status_->setText(QStringLiteral("ICY error: ") + msg);
    lbl_stream_status_->setStyleSheet(QStringLiteral("color: #ee4444;"));
}

/* ── ICY Metadata tab population ────────────────────────────────────────── */
void PreviewAudioStudio::populateIcyMetadataTab(const IcyStreamReader::IcyHeaders &h)
{
    QStringList lines;
    const QString sep = QStringLiteral("─────────────────────────────────");

    lines << QStringLiteral("ICY1/ICY2.2 Stream Headers") << sep;
    if (!h.icy_version.isEmpty())    lines << QStringLiteral("Version     : ") + h.icy_version;
    if (!h.icy_name.isEmpty())       lines << QStringLiteral("Name        : ") + h.icy_name;
    if (!h.icy_genre.isEmpty())      lines << QStringLiteral("Genre       : ") + h.icy_genre;
    if (!h.content_type.isEmpty())   lines << QStringLiteral("Content-Type: ") + h.content_type;
    if (!h.server.isEmpty())         lines << QStringLiteral("Server      : ") + h.server;
    if (h.bitrate_kbps > 0)         lines << QStringLiteral("Bitrate     : ") + QString::number(h.bitrate_kbps) + QStringLiteral(" kbps");
    if (h.metaint > 0)              lines << QStringLiteral("MetaInt     : ") + QString::number(h.metaint) + QStringLiteral(" bytes");
    if (!h.icy_url.isEmpty())        lines << QStringLiteral("URL         : ") + h.icy_url;
    if (!h.icy_description.isEmpty())lines << QStringLiteral("Description : ") + h.icy_description;
    if (!h.icy_audio_info.isEmpty()) lines << QStringLiteral("Audio Info  : ") + h.icy_audio_info;
    if (h.icy_pub >= 0)             lines << QStringLiteral("Public      : ") + (h.icy_pub ? QStringLiteral("yes") : QStringLiteral("no"));

    if (h.is_icy2) {
        lines << QString() << QStringLiteral("ICY2.2 Extended Fields") << sep;
        if (!h.icy2_language.isEmpty())  lines << QStringLiteral("Language    : ") + h.icy2_language;
        if (!h.icy2_country.isEmpty())   lines << QStringLiteral("Country     : ") + h.icy2_country;
        if (!h.icy2_timezone.isEmpty())  lines << QStringLiteral("Timezone    : ") + h.icy2_timezone;
        if (!h.icy2_email.isEmpty())     lines << QStringLiteral("Email       : ") + h.icy2_email;
        if (!h.icy2_twitter.isEmpty())   lines << QStringLiteral("Twitter     : ") + h.icy2_twitter;
        if (!h.icy2_facebook.isEmpty())  lines << QStringLiteral("Facebook    : ") + h.icy2_facebook;
        if (!h.icy2_instagram.isEmpty()) lines << QStringLiteral("Instagram   : ") + h.icy2_instagram;
        if (!h.icy2_logo.isEmpty())      lines << QStringLiteral("Logo        : ") + h.icy2_logo;
        if (!h.icy2_irc.isEmpty())       lines << QStringLiteral("IRC         : ") + h.icy2_irc;
        if (!h.icy2_aim.isEmpty())       lines << QStringLiteral("AIM         : ") + h.icy2_aim;
        if (!h.icy2_icq.isEmpty())       lines << QStringLiteral("ICQ         : ") + h.icy2_icq;
    }

    lines << QString() << QStringLiteral("All Raw Headers") << sep;
    for (auto it = h.all.constBegin(); it != h.all.constEnd(); ++it)
        lines << it.key() + QStringLiteral(": ") + it.value();

    icy_meta_view_->setPlainText(lines.join(QStringLiteral("\n")));
}

/* ── Protocol log helper ─────────────────────────────────────────────────── */
void PreviewAudioStudio::logEvent(const QString &line)
{
    log_view_->appendPlainText(line);
    /* Auto-scroll to bottom */
    QTextCursor c = log_view_->textCursor();
    c.movePosition(QTextCursor::End);
    log_view_->setTextCursor(c);
}

/* ── Stats polling (Icecast2 JSON) ─────────────────────────────────────── */
void PreviewAudioStudio::onPollStats()
{
    if (!stream_tuned_in_ || stream_combo_slot_idx_ < 0) return;
    const auto &s = slot_list_[static_cast<size_t>(stream_combo_slot_idx_)];
    if (s.stats_url.isEmpty()) return;
    QNetworkRequest req(QUrl(s.stats_url));
    req.setRawHeader("User-Agent", "Mcaster1-Preview/1.0");
    nam_->get(req);
}

void PreviewAudioStudio::onStatsReply(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) return;

    const QByteArray data = reply->readAll();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) return;

    const QJsonObject root = doc.object();
    const QJsonObject icestats = root[QStringLiteral("icestats")].toObject();
    if (icestats.isEmpty()) return;

    int     listeners   = 0;
    int     bitrate     = 0;
    QString title;
    QString server_type = icestats[QStringLiteral("server_id")].toString();

    auto parseSource = [&](const QJsonObject &src) {
        if (stream_combo_slot_idx_ >= 0 &&
            stream_combo_slot_idx_ < static_cast<int>(slot_list_.size())) {
            QString mount = slot_list_[static_cast<size_t>(stream_combo_slot_idx_)]
                            .listen_url.section(QLatin1Char('/'), 3);
            QString src_mount = src[QStringLiteral("listenurl")].toString().section(QLatin1Char('/'), 3);
            if (!src_mount.isEmpty() && !mount.isEmpty() && src_mount != mount)
                return;
        }
        listeners += src[QStringLiteral("listeners")].toInt();
        if (src[QStringLiteral("bitrate")].toInt() > 0)
            bitrate = src[QStringLiteral("bitrate")].toInt();
        QString t = src[QStringLiteral("title")].toString();
        if (!t.isEmpty()) title = t;
    };

    const QJsonValue sources = icestats[QStringLiteral("source")];
    if (sources.isArray()) {
        for (const QJsonValue &v : sources.toArray())
            if (v.isObject()) parseSource(v.toObject());
    } else if (sources.isObject()) {
        parseSource(sources.toObject());
    }

    updateMetricsDisplay(title, listeners, bitrate, server_type);
}

void PreviewAudioStudio::updateMetricsDisplay(const QString &title, int listeners,
                                               int bitrate_kbps, const QString &server_type)
{
    lbl_listeners_->setText(
        listeners >= 0 ? QString::number(listeners) : QStringLiteral("\u2014"));
    if (bitrate_kbps > 0)
        lbl_stream_bitrate_->setText(
            QString::number(bitrate_kbps) + QStringLiteral(" kbps"));
    if (!server_type.isEmpty())
        lbl_server_type_->setText(server_type);
    if (!title.isEmpty())
        lbl_now_playing_->setText(title);
}

/* ── Ring buffer ─────────────────────────────────────────────────────────── */
void PreviewAudioStudio::rbPush(const float *data, int n)
{
    int h = ring_head_.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
        ring_buf_[(h + i) & (RING_CAP - 1)] = data[i];
    ring_head_.store((h + n) & (RING_CAP - 1), std::memory_order_release);
}

int PreviewAudioStudio::rbPop(float *out, int n)
{
    int t = ring_tail_.load(std::memory_order_relaxed);
    int h = ring_head_.load(std::memory_order_acquire);
    int avail = (h - t + RING_CAP) & (RING_CAP - 1);
    int count = std::min(avail, n);
    for (int i = 0; i < count; ++i)
        out[i] = ring_buf_[(t + i) & (RING_CAP - 1)];
    if (count < n)
        std::memset(out + count, 0, static_cast<size_t>(n - count) * sizeof(float));
    ring_tail_.store((t + count) & (RING_CAP - 1), std::memory_order_release);
    return count;
}

void PreviewAudioStudio::pushStereo(const float *pcm, int frames, int src_channels)
{
    if (src_channels == 2) {
        rbPush(pcm, frames * 2);
    } else {
        std::vector<float> tmp(static_cast<size_t>(frames) * 2);
        for (int i = 0; i < frames; ++i) {
            float s = (src_channels >= 1) ? pcm[i * src_channels] : 0.0f;
            tmp[static_cast<size_t>(i) * 2]     = s;
            tmp[static_cast<size_t>(i) * 2 + 1] = s;
        }
        rbPush(tmp.data(), frames * 2);
    }
}

/* ── PortAudio callbacks ─────────────────────────────────────────────────── */
#ifdef HAVE_PORTAUDIO
int PreviewAudioStudio::paInputCb(const void *input, void * /*output*/,
                                   unsigned long frames,
                                   const PaStreamCallbackTimeInfo *,
                                   PaStreamCallbackFlags, void *ud)
{
    auto *self = static_cast<PreviewAudioStudio *>(ud);
    if (!self->passthrough_running_ || !input) return paContinue;
    const float *in_f = static_cast<const float *>(input);
    /* Measure input peak for VU meter */
    float pk = 0.0f;
    int n = static_cast<int>(frames) * self->in_channels_;
    for (int i = 0; i < n; ++i) {
        float a = std::fabs(in_f[i]);
        if (a > pk) pk = a;
    }
    float cur = self->peak_level_.load(std::memory_order_relaxed);
    while (pk > cur &&
           !self->peak_level_.compare_exchange_weak(cur, pk,
               std::memory_order_release, std::memory_order_relaxed)) {}
    self->pushStereo(in_f, static_cast<int>(frames), self->in_channels_);
    return paContinue;
}

int PreviewAudioStudio::paOutputCb(const void * /*input*/, void *output,
                                    unsigned long frames,
                                    const PaStreamCallbackTimeInfo *,
                                    PaStreamCallbackFlags, void *ud)
{
    auto *self = static_cast<PreviewAudioStudio *>(ud);
    float *out = static_cast<float *>(output);
    self->rbPop(out, static_cast<int>(frames) * 2);
    return paContinue;
}
#endif

} // namespace mc1
