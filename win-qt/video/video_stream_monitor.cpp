/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * video/video_stream_monitor.cpp — AIR/CUE Video Stream Monitor dialog
 *
 * Two monitoring modes:
 *   AIR — Decodes the live stream from Icecast/DNAS server via QMediaPlayer
 *         + QVideoSink, showing what viewers actually see (round-trip QC).
 *   CUE — Displays raw pre-encoded program frames tapped from the studio's
 *         camera callback, showing full-quality uncompressed output.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "video_stream_monitor.h"
#include "camera_preview_widget.h"

#include <QAudioDevice>
#include <QAudioOutput>
#include <QComboBox>
#include <QDateTime>
#include <QElapsedTimer>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoFrame>
#include <QVideoSink>

#include <cstring>

namespace mc1 {

// ── Constructor / Destructor ─────────────────────────────────────────────

VideoStreamMonitor::VideoStreamMonitor(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Video Stream Monitor"));
    setMinimumSize(720, 520);
    resize(800, 580);

    buildUI();
    populateAudioOutputDevices();

    /* Stats refresh timer — 1 Hz */
    stats_timer_ = new QTimer(this);
    stats_timer_->setInterval(1000);
    connect(stats_timer_, &QTimer::timeout, this, &VideoStreamMonitor::onStatsRefresh);
    stats_timer_->start();

    last_fps_time_ = QDateTime::currentMSecsSinceEpoch();
}

VideoStreamMonitor::~VideoStreamMonitor()
{
    stopAirMonitor();
}

// ── Build UI ─────────────────────────────────────────────────────────────

void VideoStreamMonitor::buildUI()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 6, 8, 6);
    root->setSpacing(6);

    /* ── Title ── */
    auto *title = new QLabel(QStringLiteral("VIDEO STREAM MONITOR"));
    QFont tf = title->font();
    tf.setPointSize(13);
    tf.setBold(true);
    title->setFont(tf);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral("color: #00d4aa; padding: 2px;"));
    root->addWidget(title);

    /* ── Controls row 1: Mode + Stream target ── */
    auto *ctrl1 = new QHBoxLayout;
    ctrl1->setSpacing(8);

    ctrl1->addWidget(new QLabel(QStringLiteral("Mode:")));
    cmb_mode_ = new QComboBox;
    cmb_mode_->addItem(QStringLiteral("CUE — Pre-Encode Preview"),  0);
    cmb_mode_->addItem(QStringLiteral("AIR — Live Encoded Stream"), 1);
    cmb_mode_->setCurrentIndex(0);
    cmb_mode_->setMinimumWidth(200);
    connect(cmb_mode_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VideoStreamMonitor::onModeChanged);
    ctrl1->addWidget(cmb_mode_);

    ctrl1->addSpacing(12);

    ctrl1->addWidget(new QLabel(QStringLiteral("Stream:")));
    cmb_stream_ = new QComboBox;
    cmb_stream_->addItem(QStringLiteral("(No streams available)"), -1);
    cmb_stream_->setMinimumWidth(220);
    cmb_stream_->setEnabled(false); /* disabled in CUE mode */
    connect(cmb_stream_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VideoStreamMonitor::onStreamChanged);
    ctrl1->addWidget(cmb_stream_);

    ctrl1->addStretch();
    root->addLayout(ctrl1);

    /* ── Controls row 2: Audio output + Tune In ── */
    auto *ctrl2 = new QHBoxLayout;
    ctrl2->setSpacing(8);

    ctrl2->addWidget(new QLabel(QStringLiteral("Audio Output:")));
    cmb_audio_output_ = new QComboBox;
    cmb_audio_output_->setMinimumWidth(220);
    connect(cmb_audio_output_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VideoStreamMonitor::onAudioOutputChanged);
    ctrl2->addWidget(cmb_audio_output_);

    ctrl2->addSpacing(8);

    btn_tune_in_ = new QPushButton(QStringLiteral("TUNE IN"));
    btn_tune_in_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #0d9488; color: white; padding: 6px 18px; "
        "border-radius: 4px; font-weight: bold; font-size: 12px; }"
        "QPushButton:hover { background: #14b8a6; }"
        "QPushButton:disabled { background: #334155; color: #64748b; }"));
    btn_tune_in_->setEnabled(false); /* disabled in CUE mode */
    connect(btn_tune_in_, &QPushButton::clicked,
            this, &VideoStreamMonitor::onTuneIn);
    ctrl2->addWidget(btn_tune_in_);

    /* Mode badge (AIR = red, CUE = green) */
    lbl_mode_badge_ = new QLabel(QStringLiteral("  CUE  "));
    lbl_mode_badge_->setStyleSheet(QStringLiteral(
        "background: #00aa66; color: white; font-weight: bold; font-size: 12px; "
        "padding: 4px 12px; border-radius: 4px; letter-spacing: 2px;"));
    ctrl2->addWidget(lbl_mode_badge_);

    lbl_status_ = new QLabel(QStringLiteral("Monitoring pre-encode output"));
    lbl_status_->setStyleSheet(QStringLiteral(
        "color: #99aabb; font-size: 11px; padding: 0 8px;"));
    ctrl2->addWidget(lbl_status_);

    /* Feedback warning (hidden by default) */
    lbl_feedback_warn_ = new QLabel(
        QStringLiteral("\xe2\x9a\xa0 Audio output matches encoder input — feedback risk!"));
    lbl_feedback_warn_->setStyleSheet(QStringLiteral(
        "color: #ff4444; font-weight: bold; font-size: 10px;"));
    lbl_feedback_warn_->setVisible(false);
    ctrl2->addWidget(lbl_feedback_warn_);

    ctrl2->addStretch();
    root->addLayout(ctrl2);

    /* ── Video preview area ── */
    /* We create BOTH preview widgets and show/hide based on mode.
     * CUE preview receives pushCueFrame() calls from the studio.
     * AIR preview receives decoded frames from QMediaPlayer via QVideoSink. */

    auto *preview_area = new QGroupBox(QStringLiteral("Video Preview"));
    auto *preview_lay  = new QVBoxLayout(preview_area);
    preview_lay->setContentsMargins(4, 8, 4, 4);

    cue_preview_ = new CameraPreviewWidget;
    cue_preview_->setMinimumSize(640, 360);
    preview_lay->addWidget(cue_preview_);

    air_preview_ = new CameraPreviewWidget;
    air_preview_->setMinimumSize(640, 360);
    air_preview_->setVisible(false); /* hidden in CUE mode */
    preview_lay->addWidget(air_preview_);

    root->addWidget(preview_area, 1); /* stretch factor = 1 */

    /* ── Stats bar ── */
    auto *stats_bar = new QHBoxLayout;
    stats_bar->setSpacing(16);

    auto makeStatLabel = [](const QString &initial) {
        auto *lbl = new QLabel(initial);
        lbl->setStyleSheet(QStringLiteral(
            "color: #94a3b8; font-family: monospace; font-size: 11px; padding: 2px 4px;"));
        return lbl;
    };

    lbl_codec_      = makeStatLabel(QStringLiteral("Codec: —"));
    lbl_resolution_ = makeStatLabel(QStringLiteral("Resolution: —"));
    lbl_bitrate_    = makeStatLabel(QStringLiteral("Bitrate: —"));
    lbl_fps_        = makeStatLabel(QStringLiteral("FPS: —"));

    stats_bar->addWidget(lbl_codec_);
    stats_bar->addWidget(lbl_resolution_);
    stats_bar->addWidget(lbl_bitrate_);
    stats_bar->addWidget(lbl_fps_);
    stats_bar->addStretch();

    root->addLayout(stats_bar);

    /* ── QMediaPlayer setup for AIR mode ── */
    air_player_ = new QMediaPlayer(this);
    air_sink_   = new QVideoSink(this);
    air_audio_  = new QAudioOutput(this);

    air_player_->setVideoOutput(air_sink_);
    air_player_->setAudioOutput(air_audio_);

    connect(air_player_, &QMediaPlayer::playbackStateChanged,
            this, &VideoStreamMonitor::onPlayerStateChanged);
    connect(air_player_, &QMediaPlayer::errorOccurred,
            this, [this](QMediaPlayer::Error, const QString &msg) {
                onPlayerError();
            });
    connect(air_sink_, &QVideoSink::videoFrameChanged,
            this, &VideoStreamMonitor::onVideoFrameReady);
}

// ── Audio output device enumeration ──────────────────────────────────────

void VideoStreamMonitor::populateAudioOutputDevices()
{
    cmb_audio_output_->clear();

    const auto devices = QMediaDevices::audioOutputs();
    for (const auto &dev : devices) {
        QString label = dev.description();
        if (dev == QMediaDevices::defaultAudioOutput())
            label += QStringLiteral(" (default)");
        cmb_audio_output_->addItem(label, QVariant::fromValue(dev));
    }

    if (cmb_audio_output_->count() == 0) {
        cmb_audio_output_->addItem(QStringLiteral("(No audio output devices)"));
    }
}

// ── Stream list population ───────────────────────────────────────────────

void VideoStreamMonitor::setStreamList(
    const std::vector<VideoStreamMonitorInfo> &streams)
{
    stream_list_ = streams;

    cmb_stream_->blockSignals(true);
    cmb_stream_->clear();

    if (streams.empty()) {
        cmb_stream_->addItem(QStringLiteral("(No streams available)"), -1);
    } else {
        for (int i = 0; i < static_cast<int>(streams.size()); ++i) {
            cmb_stream_->addItem(streams[i].name, i);
        }
    }

    cmb_stream_->blockSignals(false);
    current_stream_idx_ = streams.empty() ? -1 : 0;
}

// ── CUE frame push (thread-safe) ────────────────────────────────────────

void VideoStreamMonitor::pushCueFrame(const uint8_t *bgra, int width,
                                       int height, int stride)
{
    if (current_mode_ != Mode::CUE) return;
    if (!cue_preview_ || !isVisible()) return;

    cue_preview_->pushFrame(bgra, width, height, stride);

    /* Count frames for FPS calculation */
    ++frame_count_;
}

// ── Mode switching ───────────────────────────────────────────────────────

void VideoStreamMonitor::onModeChanged(int index)
{
    Mode new_mode = (index == 1) ? Mode::AIR : Mode::CUE;
    if (new_mode == current_mode_) return;

    /* Tear down current mode */
    if (current_mode_ == Mode::AIR)
        stopAirMonitor();

    current_mode_ = new_mode;

    if (new_mode == Mode::CUE) {
        /* CUE mode: show raw preview, hide AIR preview */
        cue_preview_->setVisible(true);
        air_preview_->setVisible(false);
        cmb_stream_->setEnabled(false);
        btn_tune_in_->setEnabled(false);

        lbl_mode_badge_->setText(QStringLiteral("  CUE  "));
        lbl_mode_badge_->setStyleSheet(QStringLiteral(
            "background: #00aa66; color: white; font-weight: bold; font-size: 12px; "
            "padding: 4px 12px; border-radius: 4px; letter-spacing: 2px;"));
        lbl_status_->setText(QStringLiteral("Monitoring pre-encode output"));
    } else {
        /* AIR mode: show AIR preview, hide CUE preview */
        cue_preview_->setVisible(false);
        air_preview_->setVisible(true);
        cmb_stream_->setEnabled(true);
        btn_tune_in_->setEnabled(!stream_list_.empty());

        lbl_mode_badge_->setText(QStringLiteral("  AIR  "));
        lbl_mode_badge_->setStyleSheet(QStringLiteral(
            "background: #cc0000; color: white; font-weight: bold; font-size: 12px; "
            "padding: 4px 12px; border-radius: 4px; letter-spacing: 2px;"));
        lbl_status_->setText(QStringLiteral("Select a stream target and tune in"));
    }

    /* Reset FPS counter */
    frame_count_ = 0;
    current_fps_ = 0.0;
    last_fps_time_ = QDateTime::currentMSecsSinceEpoch();
}

// ── Stream target selection ──────────────────────────────────────────────

void VideoStreamMonitor::onStreamChanged(int index)
{
    if (index < 0) return;
    int stream_idx = cmb_stream_->itemData(index).toInt();
    current_stream_idx_ = stream_idx;
}

// ── Tune In / Disconnect ─────────────────────────────────────────────────

void VideoStreamMonitor::onTuneIn()
{
    if (air_connected_) {
        stopAirMonitor();
        return;
    }

    if (current_stream_idx_ < 0 ||
        current_stream_idx_ >= static_cast<int>(stream_list_.size()))
        return;

    startAirMonitor();
}

void VideoStreamMonitor::startAirMonitor()
{
    if (current_stream_idx_ < 0 ||
        current_stream_idx_ >= static_cast<int>(stream_list_.size()))
        return;

    const auto &info = stream_list_[current_stream_idx_];

    fprintf(stderr, "[VideoStreamMonitor] Tuning in to AIR stream: %s\n",
            info.listen_url.toUtf8().constData());

    /* Set the source URL and play */
    air_player_->setSource(QUrl(info.listen_url));
    air_player_->play();

    air_connected_ = true;
    btn_tune_in_->setText(QStringLiteral("DISCONNECT"));
    btn_tune_in_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #aa2233; color: white; padding: 6px 18px; "
        "border-radius: 4px; font-weight: bold; font-size: 12px; }"
        "QPushButton:hover { background: #cc3344; }"));
    lbl_status_->setText(QStringLiteral("Connecting to %1...").arg(info.listen_url));

    /* Update stats for this stream */
    lbl_bitrate_->setText(
        QString("Bitrate: %1 kbps").arg(info.video_bitrate_kbps));

    /* Reset FPS counter */
    frame_count_ = 0;
    current_fps_ = 0.0;
    last_fps_time_ = QDateTime::currentMSecsSinceEpoch();
}

void VideoStreamMonitor::stopAirMonitor()
{
    if (air_player_) {
        air_player_->stop();
        air_player_->setSource(QUrl());
    }

    air_connected_ = false;
    btn_tune_in_->setText(QStringLiteral("TUNE IN"));
    btn_tune_in_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #0d9488; color: white; padding: 6px 18px; "
        "border-radius: 4px; font-weight: bold; font-size: 12px; }"
        "QPushButton:hover { background: #14b8a6; }"
        "QPushButton:disabled { background: #334155; color: #64748b; }"));
    lbl_status_->setText(QStringLiteral("Disconnected"));

    lbl_codec_->setText(QStringLiteral("Codec: —"));
    lbl_resolution_->setText(QStringLiteral("Resolution: —"));
    lbl_fps_->setText(QStringLiteral("FPS: —"));
}

// ── Audio output device change ───────────────────────────────────────────

void VideoStreamMonitor::onAudioOutputChanged(int index)
{
    if (index < 0 || !air_audio_) return;

    QVariant var = cmb_audio_output_->itemData(index);
    if (var.canConvert<QAudioDevice>()) {
        QAudioDevice dev = var.value<QAudioDevice>();
        air_audio_->setDevice(dev);
        fprintf(stderr, "[VideoStreamMonitor] Audio output: %s\n",
                dev.description().toUtf8().constData());
    }
}

// ── QMediaPlayer callbacks ───────────────────────────────────────────────

void VideoStreamMonitor::onPlayerStateChanged()
{
    if (!air_player_) return;

    switch (air_player_->playbackState()) {
    case QMediaPlayer::PlayingState:
        lbl_status_->setText(QStringLiteral("AIR — Streaming live"));
        lbl_status_->setStyleSheet(QStringLiteral(
            "color: #00dd66; font-size: 11px; font-weight: bold; padding: 0 8px;"));
        break;
    case QMediaPlayer::StoppedState:
        if (air_connected_) {
            lbl_status_->setText(QStringLiteral("Stream ended or failed"));
            lbl_status_->setStyleSheet(QStringLiteral(
                "color: #ff6644; font-size: 11px; padding: 0 8px;"));
        }
        break;
    case QMediaPlayer::PausedState:
        lbl_status_->setText(QStringLiteral("Paused"));
        lbl_status_->setStyleSheet(QStringLiteral(
            "color: #ffaa00; font-size: 11px; padding: 0 8px;"));
        break;
    }
}

void VideoStreamMonitor::onPlayerError()
{
    if (!air_player_) return;
    QString err = air_player_->errorString();
    fprintf(stderr, "[VideoStreamMonitor] Player error: %s\n",
            err.toUtf8().constData());
    lbl_status_->setText(QStringLiteral("Error: %1").arg(err));
    lbl_status_->setStyleSheet(QStringLiteral(
        "color: #ff4444; font-size: 11px; padding: 0 8px;"));
}

// ── QVideoSink frame callback (AIR mode decoded frames) ──────────────────

void VideoStreamMonitor::onVideoFrameReady()
{
    if (!air_sink_ || !air_preview_ || current_mode_ != Mode::AIR) return;

    QVideoFrame frame = air_sink_->videoFrame();
    if (!frame.isValid()) return;

    /* Map the frame for CPU access */
    if (!frame.map(QVideoFrame::ReadOnly)) return;

    /* Convert to QImage (BGRA/RGB32) */
    QImage img = frame.toImage();
    frame.unmap();

    if (img.isNull()) return;

    /* Convert to Format_RGB32 (BGRA on little-endian) if needed */
    if (img.format() != QImage::Format_RGB32 &&
        img.format() != QImage::Format_ARGB32) {
        img = img.convertToFormat(QImage::Format_RGB32);
    }

    /* Push to the AIR preview widget */
    air_preview_->pushFrame(img.constBits(), img.width(), img.height(),
                            static_cast<int>(img.bytesPerLine()));

    /* Count frames for FPS */
    ++frame_count_;

    /* Update resolution stat on first frame */
    static int last_w = 0, last_h = 0;
    if (img.width() != last_w || img.height() != last_h) {
        last_w = img.width();
        last_h = img.height();
        lbl_resolution_->setText(
            QString("Resolution: %1x%2").arg(last_w).arg(last_h));
    }
}

// ── Stats refresh (1 Hz) ─────────────────────────────────────────────────

void VideoStreamMonitor::onStatsRefresh()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 elapsed = now - last_fps_time_;

    if (elapsed > 0) {
        current_fps_ = frame_count_ * 1000.0 / elapsed;
        frame_count_ = 0;
        last_fps_time_ = now;
    }

    if (current_mode_ == Mode::CUE) {
        lbl_codec_->setText(QStringLiteral("Codec: Raw BGRA"));
        lbl_bitrate_->setText(QStringLiteral("Bitrate: —"));
    }

    if (current_fps_ > 0.1) {
        lbl_fps_->setText(QString("FPS: %1").arg(current_fps_, 0, 'f', 1));
    } else {
        lbl_fps_->setText(QStringLiteral("FPS: —"));
    }
}

// ── Update stats display (called externally with pipeline stats) ─────────

void VideoStreamMonitor::updateStatsDisplay()
{
    /* Placeholder for future pipeline stats integration */
}

} // namespace mc1
