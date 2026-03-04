/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/broadcast_monitor_window.cpp — Frameless popout broadcast monitor window
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "broadcast_monitor_window.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace mc1 {

/* ------------------------------------------------------------------ */
/*  Construction                                                       */
/* ------------------------------------------------------------------ */

BroadcastMonitorWindow::BroadcastMonitorWindow(QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::Tool)
{
    setAttribute(Qt::WA_TranslucentBackground, false);
    setMinimumSize(500, 420);
    resize(720, 560);
    setWindowTitle(QStringLiteral("Broadcast Monitor"));

    /* Root layout --------------------------------------------------- */
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(2, 2, 2, 2);
    root->setSpacing(0);

    /* ============================================================== */
    /*  Title bar  (28 px)                                             */
    /* ============================================================== */
    auto *title_bar = new QWidget;
    title_bar->setFixedHeight(28);
    title_bar->setStyleSheet(QStringLiteral(
        "background: #1a2332; border-top-left-radius: 6px;"
        " border-top-right-radius: 6px;"));

    auto *title_lay = new QHBoxLayout(title_bar);
    title_lay->setContentsMargins(8, 2, 4, 2);

    /* Status indicator dot */
    lbl_dot_ = new QLabel(QStringLiteral("\u25CF")); /* Unicode filled circle */
    lbl_dot_->setFixedWidth(14);
    lbl_dot_->setStyleSheet(QStringLiteral(
        "color: #666; font-size: 10px;"));
    title_lay->addWidget(lbl_dot_);

    /* Title label */
    auto *lbl_title = new QLabel(QStringLiteral("BROADCAST MONITOR"));
    lbl_title->setStyleSheet(QStringLiteral(
        "color: #7a8ba0; font-size: 11px; font-weight: 600;"
        " letter-spacing: 1px;"));
    title_lay->addWidget(lbl_title);
    title_lay->addStretch();

    /* Close button */
    btn_close_ = new QPushButton(QStringLiteral("\u2715")); /* Unicode X */
    btn_close_->setFixedSize(22, 22);
    btn_close_->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; color: #7a8ba0;"
        " border: none; font-size: 14px; font-weight: bold; }"
        "QPushButton:hover { color: #ff5252; }"));
    connect(btn_close_, &QPushButton::clicked, this, &QWidget::hide);
    title_lay->addWidget(btn_close_);

    root->addWidget(title_bar);

    /* ============================================================== */
    /*  Preview widget  (fills middle)                                 */
    /* ============================================================== */
    preview_ = new CameraPreviewWidget;
    root->addWidget(preview_, 1);

    /* ============================================================== */
    /*  Control bar  (~90 px)                                          */
    /* ============================================================== */
    auto *ctrl_bar = new QWidget;
    ctrl_bar->setStyleSheet(QStringLiteral(
        "background: #1a2332; border-bottom-left-radius: 6px;"
        " border-bottom-right-radius: 6px;"));

    auto *ctrl_root = new QVBoxLayout(ctrl_bar);
    ctrl_root->setContentsMargins(12, 8, 12, 8);
    ctrl_root->setSpacing(6);

    /* --- Button row --- */
    auto *btn_row = new QHBoxLayout;
    btn_row->setSpacing(8);

    btn_go_live_ = new QPushButton(QStringLiteral("\u25CF  GO LIVE"));
    btn_go_live_->setObjectName(QStringLiteral("goLiveBtn"));
    btn_go_live_->setStyleSheet(QStringLiteral(
        "QPushButton#goLiveBtn {"
        "  background: #ff3333; color: white; font-weight: bold;"
        "  border: none; border-radius: 4px; padding: 8px 16px;"
        "  font-size: 13px;"
        "}"
        "QPushButton#goLiveBtn:hover { background: #ff5555; }"
        "QPushButton#goLiveBtn:disabled { background: #333; color: #666; }"));
    connect(btn_go_live_, &QPushButton::clicked, this, &BroadcastMonitorWindow::onGoLive);
    btn_row->addWidget(btn_go_live_);

    btn_stop_ = new QPushButton(QStringLiteral("\u25A0  STOP"));
    btn_stop_->setObjectName(QStringLiteral("stopBtn"));
    btn_stop_->setEnabled(false);
    btn_stop_->setStyleSheet(QStringLiteral(
        "QPushButton#stopBtn {"
        "  background: #2a3a4e; color: #ff3333; font-weight: bold;"
        "  border: 1px solid #3a4a5e; border-radius: 4px; padding: 8px 16px;"
        "  font-size: 13px;"
        "}"
        "QPushButton#stopBtn:hover { background: #3a4a5e; }"
        "QPushButton#stopBtn:disabled {"
        "  background: #1a2a3e; color: #444; border-color: #2a3a4e;"
        "}"));
    connect(btn_stop_, &QPushButton::clicked, this, &BroadcastMonitorWindow::onStop);
    btn_row->addWidget(btn_stop_);

    btn_dry_run_ = new QPushButton(QStringLiteral("\u25B6  DRY RUN"));
    btn_dry_run_->setObjectName(QStringLiteral("dryRunBtn"));
    btn_dry_run_->setCheckable(true);
    btn_dry_run_->setStyleSheet(QStringLiteral(
        "QPushButton#dryRunBtn {"
        "  background: transparent; color: #00d4aa; font-weight: bold;"
        "  border: 1px solid #2a3a4e; border-radius: 4px; padding: 8px 16px;"
        "  font-size: 13px;"
        "}"
        "QPushButton#dryRunBtn:checked {"
        "  border-color: #00d4aa; background: rgba(0,212,170,0.1);"
        "}"
        "QPushButton#dryRunBtn:hover { border-color: #00d4aa; }"));
    connect(btn_dry_run_, &QPushButton::clicked, this, &BroadcastMonitorWindow::onDryRun);
    btn_row->addWidget(btn_dry_run_);

    btn_row->addStretch();
    ctrl_root->addLayout(btn_row);

    /* --- Status info grid --- */
    auto *info_grid = new QGridLayout;
    info_grid->setContentsMargins(0, 2, 0, 0);
    info_grid->setHorizontalSpacing(6);
    info_grid->setVerticalSpacing(2);

    auto make_label = [](const QString &text) -> QLabel * {
        auto *l = new QLabel(text);
        l->setStyleSheet(QStringLiteral("color: #556677; font-size: 11px;"));
        return l;
    };

    auto make_value = [](const QString &text) -> QLabel * {
        auto *l = new QLabel(text);
        l->setStyleSheet(QStringLiteral("color: #7a8ba0; font-size: 11px;"));
        return l;
    };

    /* Row 0 */
    info_grid->addWidget(make_label(QStringLiteral("Status:")),   0, 0);
    lbl_status_ = make_value(QStringLiteral("IDLE"));
    info_grid->addWidget(lbl_status_, 0, 1);

    info_grid->addWidget(make_label(QStringLiteral("Duration:")), 0, 2);
    lbl_duration_ = make_value(QStringLiteral("00:00:00"));
    info_grid->addWidget(lbl_duration_, 0, 3);

    /* Row 1 */
    info_grid->addWidget(make_label(QStringLiteral("Bitrate:")),  1, 0);
    lbl_bitrate_ = make_value(QStringLiteral("\u2014 kbps"));   /* em dash */
    info_grid->addWidget(lbl_bitrate_, 1, 1);

    info_grid->addWidget(make_label(QStringLiteral("FPS:")),      1, 2);
    lbl_fps_ = make_value(QStringLiteral("\u2014"));
    info_grid->addWidget(lbl_fps_, 1, 3);

    /* Stretch the value columns */
    info_grid->setColumnStretch(1, 1);
    info_grid->setColumnStretch(3, 1);

    ctrl_root->addLayout(info_grid);
    root->addWidget(ctrl_bar);

    /* ============================================================== */
    /*  Timers                                                         */
    /* ============================================================== */
    duration_timer_ = new QTimer(this);
    duration_timer_->setInterval(1000);
    connect(duration_timer_, &QTimer::timeout,
            this, &BroadcastMonitorWindow::updateDuration);

    blink_timer_ = new QTimer(this);
    blink_timer_->setInterval(500);
    connect(blink_timer_, &QTimer::timeout,
            this, &BroadcastMonitorWindow::updateOnAirBlink);

    /* ============================================================== */
    /*  Window style                                                   */
    /* ============================================================== */
    setStyleSheet(QStringLiteral(
        "BroadcastMonitorWindow { background: #0f1724; border: 1px solid #2a3a4e;"
        " border-radius: 6px; }"));
}

/* ------------------------------------------------------------------ */
/*  Slots                                                              */
/* ------------------------------------------------------------------ */

void BroadcastMonitorWindow::onGoLive()
{
    is_live_    = true;
    is_dry_run_ = false;

    /* Button state */
    btn_go_live_->setEnabled(false);
    btn_go_live_->setText(QStringLiteral("\u25CF  ON AIR"));
    btn_stop_->setEnabled(true);
    btn_dry_run_->setChecked(false);
    btn_dry_run_->setEnabled(false);

    /* Timers */
    elapsed_.start();
    duration_timer_->start();
    blink_timer_->start();
    blink_state_ = false;

    /* Status labels */
    lbl_status_->setText(QStringLiteral("LIVE ON-AIR"));
    lbl_status_->setStyleSheet(QStringLiteral(
        "color: #ff3333; font-size: 11px; font-weight: bold;"));
    lbl_duration_->setText(QStringLiteral("00:00:00"));

    /* Title bar dot — red */
    lbl_dot_->setStyleSheet(QStringLiteral("color: #ff3333; font-size: 10px;"));

    emit goLiveRequested();
}

void BroadcastMonitorWindow::onStop()
{
    is_live_    = false;
    is_dry_run_ = false;

    /* Stop timers */
    duration_timer_->stop();
    blink_timer_->stop();
    blink_state_ = false;

    /* Button state */
    btn_go_live_->setEnabled(true);
    btn_go_live_->setText(QStringLiteral("\u25CF  GO LIVE"));
    btn_go_live_->setStyleSheet(QStringLiteral(
        "QPushButton#goLiveBtn {"
        "  background: #ff3333; color: white; font-weight: bold;"
        "  border: none; border-radius: 4px; padding: 8px 16px;"
        "  font-size: 13px;"
        "}"
        "QPushButton#goLiveBtn:hover { background: #ff5555; }"
        "QPushButton#goLiveBtn:disabled { background: #333; color: #666; }"));
    btn_stop_->setEnabled(false);
    btn_dry_run_->setChecked(false);
    btn_dry_run_->setEnabled(true);

    /* Status labels */
    lbl_status_->setText(QStringLiteral("IDLE"));
    lbl_status_->setStyleSheet(QStringLiteral("color: #7a8ba0; font-size: 11px;"));
    lbl_duration_->setText(QStringLiteral("00:00:00"));

    /* Title bar dot — gray */
    lbl_dot_->setStyleSheet(QStringLiteral("color: #666; font-size: 10px;"));

    emit stopRequested();
}

void BroadcastMonitorWindow::onDryRun()
{
    is_dry_run_ = btn_dry_run_->isChecked();

    if (is_dry_run_) {
        /* Activate dry run */
        btn_dry_run_->setText(QStringLiteral("\u25B6  DRY RUN ACTIVE"));
        btn_go_live_->setEnabled(true);
        btn_stop_->setEnabled(true);

        elapsed_.start();
        duration_timer_->start();

        lbl_status_->setText(QStringLiteral("DRY RUN"));
        lbl_status_->setStyleSheet(QStringLiteral(
            "color: #00d4aa; font-size: 11px; font-weight: bold;"));
        lbl_duration_->setText(QStringLiteral("00:00:00"));

        /* Title bar dot — green */
        lbl_dot_->setStyleSheet(QStringLiteral("color: #00d4aa; font-size: 10px;"));
    } else {
        /* Deactivate dry run */
        btn_dry_run_->setText(QStringLiteral("\u25B6  DRY RUN"));
        btn_stop_->setEnabled(false);

        duration_timer_->stop();

        lbl_status_->setText(QStringLiteral("IDLE"));
        lbl_status_->setStyleSheet(QStringLiteral("color: #7a8ba0; font-size: 11px;"));
        lbl_duration_->setText(QStringLiteral("00:00:00"));

        /* Title bar dot — gray */
        lbl_dot_->setStyleSheet(QStringLiteral("color: #666; font-size: 10px;"));
    }

    emit dryRunToggled(is_dry_run_);
}

void BroadcastMonitorWindow::updateDuration()
{
    qint64 ms = elapsed_.elapsed();
    int total_secs = static_cast<int>(ms / 1000);
    int h = total_secs / 3600;
    int m = (total_secs % 3600) / 60;
    int s = total_secs % 60;

    lbl_duration_->setText(
        QStringLiteral("%1:%2:%3")
            .arg(h, 2, 10, QLatin1Char('0'))
            .arg(m, 2, 10, QLatin1Char('0'))
            .arg(s, 2, 10, QLatin1Char('0')));
}

void BroadcastMonitorWindow::updateOnAirBlink()
{
    blink_state_ = !blink_state_;

    if (!is_live_)
        return;

    /* Blink GO LIVE / ON AIR button between red and dark */
    if (blink_state_) {
        btn_go_live_->setStyleSheet(QStringLiteral(
            "QPushButton#goLiveBtn {"
            "  background: #2a1a1a; color: #ff3333; font-weight: bold;"
            "  border: none; border-radius: 4px; padding: 8px 16px;"
            "  font-size: 13px;"
            "}"
            "QPushButton#goLiveBtn:disabled {"
            "  background: #2a1a1a; color: #ff3333;"
            "}"));
    } else {
        btn_go_live_->setStyleSheet(QStringLiteral(
            "QPushButton#goLiveBtn {"
            "  background: #ff3333; color: white; font-weight: bold;"
            "  border: none; border-radius: 4px; padding: 8px 16px;"
            "  font-size: 13px;"
            "}"
            "QPushButton#goLiveBtn:disabled {"
            "  background: #ff3333; color: white;"
            "}"));
    }

    /* Blink title bar status dot: red <-> dark red */
    if (blink_state_) {
        lbl_dot_->setStyleSheet(QStringLiteral("color: #661111; font-size: 10px;"));
    } else {
        lbl_dot_->setStyleSheet(QStringLiteral("color: #ff3333; font-size: 10px;"));
    }
}

/* ------------------------------------------------------------------ */
/*  Phase M9: Live stats from VideoStreamPipeline                      */
/* ------------------------------------------------------------------ */

void BroadcastMonitorWindow::updateStats(double bitrate_kbps, double fps,
                                          double uptime_sec)
{
    if (bitrate_kbps > 0.0)
        lbl_bitrate_->setText(QString::number(static_cast<int>(bitrate_kbps))
                              + QStringLiteral(" kbps"));
    else
        lbl_bitrate_->setText(QStringLiteral("\u2014 kbps"));

    if (fps > 0.0)
        lbl_fps_->setText(QString::number(fps, 'f', 1) + QStringLiteral(" fps"));
    else
        lbl_fps_->setText(QStringLiteral("\u2014"));

    /* Update duration from pipeline uptime (more accurate than QElapsedTimer
       when pipeline start is delayed) */
    if (uptime_sec > 0.0) {
        int total = static_cast<int>(uptime_sec);
        int h = total / 3600;
        int m = (total % 3600) / 60;
        int s = total % 60;
        lbl_duration_->setText(
            QStringLiteral("%1:%2:%3")
                .arg(h, 2, 10, QLatin1Char('0'))
                .arg(m, 2, 10, QLatin1Char('0'))
                .arg(s, 2, 10, QLatin1Char('0')));
    }
}

/* ------------------------------------------------------------------ */
/*  Paint / mouse / hide events                                        */
/* ------------------------------------------------------------------ */

void BroadcastMonitorWindow::paintEvent(QPaintEvent * /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(0x0f, 0x17, 0x24));
    p.setPen(QPen(QColor(0x2a, 0x3a, 0x4e), 1));
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 6, 6);
}

void BroadcastMonitorWindow::hideEvent(QHideEvent *event)
{
    emit windowHidden();
    QWidget::hideEvent(event);
}

void BroadcastMonitorWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && event->position().y() < 28) {
        dragging_ = true;
        drag_start_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}

void BroadcastMonitorWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (dragging_) {
        move(event->globalPosition().toPoint() - drag_start_);
        event->accept();
    }
}

void BroadcastMonitorWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        dragging_ = false;
        event->accept();
    }
}

} // namespace mc1
