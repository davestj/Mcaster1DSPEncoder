/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * video/video_stream_monitor.h — AIR/CUE Video Stream Monitor dialog
 *
 * Broadcast-grade stream monitoring with two modes:
 *   AIR — Decode the live encoded stream from the server (round-trip QC)
 *   CUE — View raw pre-encoded program output at full quality
 *
 * Follows the same pattern as PreviewAudioStudio for audio monitoring.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_VIDEO_STREAM_MONITOR_H
#define MC1_VIDEO_STREAM_MONITOR_H

#include <QDialog>
#include <QAudioDevice>
#include <vector>
#include <string>

class QAudioOutput;
class QComboBox;
class QLabel;
class QMediaPlayer;
class QPushButton;
class QTimer;
class QVideoSink;

namespace mc1 {

class CameraPreviewWidget;

struct VideoStreamMonitorInfo {
    QString name;           /* "Icecast2 — host:port/mount" */
    QString listen_url;     /* http://host:port/mount */
    QString content_type;   /* "video/webm", "video/x-flv" */
    int     video_bitrate_kbps = 2500;
    int     audio_bitrate_kbps = 128;
};

class VideoStreamMonitor : public QDialog {
    Q_OBJECT

public:
    explicit VideoStreamMonitor(QWidget *parent = nullptr);
    ~VideoStreamMonitor() override;

    /* Populate AIR mode stream target dropdown */
    void setStreamList(const std::vector<VideoStreamMonitorInfo> &streams);

    /* CUE mode: push raw pre-encoded BGRA program frames (thread-safe).
     * Called from the studio's camera callback on a capture thread. */
    void pushCueFrame(const uint8_t *bgra, int width, int height, int stride);

signals:
    void openStudioRequested();

private slots:
    void onModeChanged(int index);
    void onStreamChanged(int index);
    void onTuneIn();
    void onAudioOutputChanged(int index);
    void onPlayerStateChanged();
    void onPlayerError();
    void onVideoFrameReady();
    void onStatsRefresh();

private:
    void buildUI();
    void populateAudioOutputDevices();
    void startAirMonitor();
    void stopAirMonitor();
    void updateStatsDisplay();

    /* Monitoring mode */
    enum class Mode { AIR, CUE };
    Mode current_mode_ = Mode::CUE;

    /* Stream list for AIR mode */
    std::vector<VideoStreamMonitorInfo> stream_list_;
    int  current_stream_idx_ = -1;
    bool air_connected_      = false;

    /* ── CUE mode ── */
    CameraPreviewWidget *cue_preview_ = nullptr;

    /* ── AIR mode ── */
    CameraPreviewWidget *air_preview_ = nullptr;
    QMediaPlayer        *air_player_  = nullptr;
    QAudioOutput        *air_audio_   = nullptr;
    QVideoSink          *air_sink_    = nullptr;

    /* ── Common UI ── */
    QComboBox   *cmb_mode_         = nullptr;
    QComboBox   *cmb_stream_       = nullptr;
    QComboBox   *cmb_audio_output_ = nullptr;
    QPushButton *btn_tune_in_      = nullptr;
    QLabel      *lbl_mode_badge_   = nullptr;
    QLabel      *lbl_status_       = nullptr;
    QLabel      *lbl_codec_        = nullptr;
    QLabel      *lbl_resolution_   = nullptr;
    QLabel      *lbl_bitrate_      = nullptr;
    QLabel      *lbl_fps_          = nullptr;
    QLabel      *lbl_feedback_warn_ = nullptr;

    /* Stats tracking */
    QTimer      *stats_timer_      = nullptr;
    int          frame_count_      = 0;
    qint64       last_fps_time_    = 0;
    double       current_fps_      = 0.0;
};

} // namespace mc1

#endif // MC1_VIDEO_STREAM_MONITOR_H
