/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/camera_preview_window.cpp — Preview / Editor window (frameless popout)
 *
 * Phase M6.5: Added effects + overlay toolbar buttons and EDITOR badge.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "camera_preview_window.h"

#include <QHBoxLayout>
#include <QHideEvent>
#include <QIcon>
#include <QLabel>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>

namespace mc1 {

CameraPreviewWindow::CameraPreviewWindow(QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::Tool)
{
    setAttribute(Qt::WA_TranslucentBackground, false);
    setMinimumSize(400, 310);
    resize(640, 500);
    setWindowTitle(QStringLiteral("Preview / Editor"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(2, 2, 2, 2);
    root->setSpacing(0);

    /* Title bar with EDITOR badge, effects/overlay buttons, and close */
    auto *title_bar = new QWidget;
    title_bar->setFixedHeight(28);
    title_bar->setStyleSheet(
        QStringLiteral("background: #1a2332; border-top-left-radius: 6px;"
                        " border-top-right-radius: 6px;"));

    auto *title_lay = new QHBoxLayout(title_bar);
    title_lay->setContentsMargins(8, 2, 4, 2);

    /* EDITOR badge */
    lbl_editor_ = new QLabel(QStringLiteral("EDITOR"));
    lbl_editor_->setStyleSheet(QStringLiteral(
        "color: #00d4aa; font-size: 9px; font-weight: bold;"
        " background: rgba(0,212,170,0.15); border: 1px solid #00d4aa;"
        " border-radius: 3px; padding: 1px 5px;"));
    title_lay->addWidget(lbl_editor_);

    auto *lbl = new QLabel(QStringLiteral("Preview / Editor"));
    lbl->setStyleSheet(QStringLiteral(
        "color: #7a8ba0; font-size: 11px; font-weight: 600;"));
    title_lay->addWidget(lbl);
    title_lay->addStretch();

    /* Effects button */
    auto btn_style = QStringLiteral(
        "QPushButton { background: transparent; color: #7a8ba0;"
        " border: none; font-size: 11px; padding: 2px 6px; }"
        "QPushButton:hover { color: #00d4aa; }");

    btn_effects_ = new QPushButton(QIcon(QStringLiteral(":/icons/effects.svg")),
                                    QStringLiteral("FX"));
    btn_effects_->setFixedHeight(22);
    btn_effects_->setIconSize(QSize(14, 14));
    btn_effects_->setToolTip(QStringLiteral("Video Effects"));
    btn_effects_->setStyleSheet(btn_style);
    connect(btn_effects_, &QPushButton::clicked, this, &CameraPreviewWindow::effectsRequested);
    title_lay->addWidget(btn_effects_);

    /* Overlay button */
    btn_overlay_ = new QPushButton(QIcon(QStringLiteral(":/icons/overlay.svg")),
                                    QStringLiteral("OVL"));
    btn_overlay_->setFixedHeight(22);
    btn_overlay_->setIconSize(QSize(14, 14));
    btn_overlay_->setToolTip(QStringLiteral("Overlay Settings"));
    btn_overlay_->setStyleSheet(btn_style);
    connect(btn_overlay_, &QPushButton::clicked, this, &CameraPreviewWindow::overlayRequested);
    title_lay->addWidget(btn_overlay_);

    title_lay->addSpacing(4);

    btn_close_ = new QPushButton(QStringLiteral("\u2715")); /* Unicode X */
    btn_close_->setFixedSize(22, 22);
    btn_close_->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; color: #7a8ba0;"
        " border: none; font-size: 14px; font-weight: bold; }"
        "QPushButton:hover { color: #ff5252; }"));
    connect(btn_close_, &QPushButton::clicked, this, &QWidget::hide);
    title_lay->addWidget(btn_close_);

    root->addWidget(title_bar);

    /* Camera preview widget fills the rest */
    preview_ = new CameraPreviewWidget;
    root->addWidget(preview_, 1);

    /* Dark border style */
    setStyleSheet(QStringLiteral(
        "CameraPreviewWindow { background: #0f1724; border: 1px solid #2a3a4e;"
        " border-radius: 6px; }"));
}

void CameraPreviewWindow::hideEvent(QHideEvent *event)
{
    emit windowHidden();
    QWidget::hideEvent(event);
}

void CameraPreviewWindow::paintEvent(QPaintEvent * /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(0x0f, 0x17, 0x24));
    p.setPen(QPen(QColor(0x2a, 0x3a, 0x4e), 1));
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 6, 6);
}

void CameraPreviewWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && event->position().y() < 28) {
        dragging_ = true;
        drag_start_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}

void CameraPreviewWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (dragging_) {
        move(event->globalPosition().toPoint() - drag_start_);
        event->accept();
    }
}

void CameraPreviewWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        dragging_ = false;
        event->accept();
    }
}

} // namespace mc1
