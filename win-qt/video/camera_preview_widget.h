/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/camera_preview_widget.h — Live camera preview widget with
 *                                 interactive overlay drag/resize
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_CAMERA_PREVIEW_WIDGET_H
#define MC1_CAMERA_PREVIEW_WIDGET_H

#include <QWidget>
#include <QImage>
#include <QTimer>
#include <mutex>

namespace mc1 {

class OverlayRenderer;

class CameraPreviewWidget : public QWidget {
    Q_OBJECT

public:
    explicit CameraPreviewWidget(QWidget *parent = nullptr);

    /* Thread-safe: call from any thread (e.g. camera capture callback) */
    void pushFrame(const uint8_t* bgra_data, int width, int height, int stride);

    /* Set the overlay renderer so we can query / manipulate image overlay */
    void setOverlayRenderer(OverlayRenderer *renderer);

    QSize minimumSizeHint() const override { return {320, 240}; }
    QSize sizeHint()        const override { return {640, 480}; }

signals:
    /* Emitted when the user drags or resizes the image overlay */
    void imageOverlayMoved(int x, int y, int w, int h);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    /* Which part of the overlay the mouse grabbed */
    enum class HitZone {
        NONE, BODY,
        TOP_LEFT, TOP, TOP_RIGHT,
        RIGHT, BOTTOM_RIGHT, BOTTOM,
        BOTTOM_LEFT, LEFT
    };

    /* Map widget pixel → video pixel (accounting for aspect-ratio scaling) */
    QPoint widgetToVideo(const QPoint &wp) const;
    QPoint videoToWidget(const QPoint &vp) const;

    /* Hit-test the mouse position against the image overlay rect */
    HitZone hitTest(const QPoint &widgetPos) const;

    /* Update cursor shape based on hit zone */
    void updateCursor(HitZone zone);

    /* Compute the scaled video display rect within the widget */
    QRect videoDisplayRect() const;

    QImage        pending_frame_;
    QImage        display_frame_;
    std::mutex    frame_mutex_;
    bool          has_new_frame_ = false;
    QTimer       *refresh_timer_ = nullptr;

    /* Interactive overlay state */
    OverlayRenderer *overlay_renderer_ = nullptr;
    HitZone          drag_zone_    = HitZone::NONE;
    QPoint           drag_start_;       // widget coords at mouse press
    int              orig_x_ = 0, orig_y_ = 0;   // overlay pos at drag start
    int              orig_w_ = 0, orig_h_ = 0;    // overlay size at drag start

    static constexpr int kHandleSize = 8;  // resize handle radius in widget px
};

} // namespace mc1

#endif // MC1_CAMERA_PREVIEW_WIDGET_H
