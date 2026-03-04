/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/broadcast_monitor_window.h — Frameless popout broadcast monitor window
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_BROADCAST_MONITOR_WINDOW_H
#define MC1_BROADCAST_MONITOR_WINDOW_H

#include <QWidget>
#include <QPoint>
#include <QElapsedTimer>
#include "camera_preview_widget.h"

class QLabel;
class QPushButton;
class QTimer;

namespace mc1 {

class BroadcastMonitorWindow : public QWidget {
    Q_OBJECT

public:
    explicit BroadcastMonitorWindow(QWidget *parent = nullptr);

    CameraPreviewWidget *previewWidget() { return preview_; }

    bool isLive()   const { return is_live_; }
    bool isDryRun() const { return is_dry_run_; }

    /* Phase M9: Update live stats from VideoStreamPipeline */
    void updateStats(double bitrate_kbps, double fps, double uptime_sec);

signals:
    void windowHidden();
    void goLiveRequested();    /* user clicked GO LIVE   */
    void stopRequested();      /* user clicked STOP      */
    void dryRunToggled(bool active);

protected:
    void paintEvent(QPaintEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private slots:
    void onGoLive();
    void onStop();
    void onDryRun();
    void updateDuration();
    void updateOnAirBlink();

private:
    CameraPreviewWidget *preview_      = nullptr;
    QPushButton         *btn_close_    = nullptr;
    QPushButton         *btn_go_live_  = nullptr;
    QPushButton         *btn_stop_     = nullptr;
    QPushButton         *btn_dry_run_  = nullptr;
    QLabel              *lbl_status_   = nullptr;    /* "DRY RUN" / "LIVE ON-AIR" */
    QLabel              *lbl_duration_ = nullptr;    /* "00:00:00"                */
    QLabel              *lbl_bitrate_  = nullptr;    /* "2500 kbps"               */
    QLabel              *lbl_fps_      = nullptr;    /* "30 fps"                  */
    QLabel              *lbl_dot_      = nullptr;    /* status indicator dot      */
    QTimer              *duration_timer_ = nullptr;
    QTimer              *blink_timer_    = nullptr;
    QElapsedTimer        elapsed_;

    bool is_live_     = false;
    bool is_dry_run_  = false;
    bool blink_state_ = false;

    /* Dragging support */
    bool   dragging_ = false;
    QPoint drag_start_;
};

} // namespace mc1

#endif // MC1_BROADCAST_MONITOR_WINDOW_H
