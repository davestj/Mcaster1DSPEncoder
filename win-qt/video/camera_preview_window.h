/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/camera_preview_window.h — Preview / Editor window (frameless popout)
 *
 * Phase M6.5: Added effects + overlay toolbar buttons and EDITOR badge.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_CAMERA_PREVIEW_WINDOW_H
#define MC1_CAMERA_PREVIEW_WINDOW_H

#include <QWidget>
#include <QPoint>
#include "camera_preview_widget.h"

class QLabel;
class QPushButton;

namespace mc1 {

class CameraPreviewWindow : public QWidget {
    Q_OBJECT

public:
    explicit CameraPreviewWindow(QWidget *parent = nullptr);

    CameraPreviewWidget *previewWidget() { return preview_; }

signals:
    void windowHidden();
    void effectsRequested();   // user clicked effects button
    void overlayRequested();   // user clicked overlay button

protected:
    void paintEvent(QPaintEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    CameraPreviewWidget *preview_      = nullptr;
    QPushButton         *btn_close_    = nullptr;
    QPushButton         *btn_effects_  = nullptr;
    QPushButton         *btn_overlay_  = nullptr;
    QLabel              *lbl_editor_   = nullptr;

    /* Dragging support */
    bool   dragging_ = false;
    QPoint drag_start_;
};

} // namespace mc1

#endif // MC1_CAMERA_PREVIEW_WINDOW_H
