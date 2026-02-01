/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/camera_preview_widget.cpp — Live camera preview widget with
 *                                   interactive overlay drag/resize
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "camera_preview_widget.h"
#include "overlay_renderer.h"

#include <QMouseEvent>
#include <QPainter>
#include <cstring>

namespace mc1 {

CameraPreviewWidget::CameraPreviewWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(320, 240);
    setMouseTracking(true);  // receive moves without button held

    /* Refresh timer: 30 fps display rate */
    refresh_timer_ = new QTimer(this);
    refresh_timer_->setInterval(33); /* ~30 fps */
    connect(refresh_timer_, &QTimer::timeout, this, [this]() {
        std::lock_guard<std::mutex> lk(frame_mutex_);
        if (has_new_frame_) {
            display_frame_ = pending_frame_;
            has_new_frame_ = false;
            update();
        }
    });
    refresh_timer_->start();
}

void CameraPreviewWidget::pushFrame(const uint8_t* bgra_data, int width,
                                      int height, int stride)
{
    /* Copy frame data into QImage — thread-safe via mutex */
    QImage img(width, height, QImage::Format_ARGB32);

    for (int row = 0; row < height; ++row) {
        const uint8_t *src = bgra_data + row * stride;
        uint8_t *dst = img.scanLine(row);
        std::memcpy(dst, src, std::min(static_cast<qsizetype>(stride),
                                       img.bytesPerLine()));
    }

    std::lock_guard<std::mutex> lk(frame_mutex_);
    pending_frame_ = std::move(img);
    has_new_frame_ = true;
}

void CameraPreviewWidget::setOverlayRenderer(OverlayRenderer *renderer)
{
    overlay_renderer_ = renderer;
}

// ---------------------------------------------------------------------------
// Coordinate mapping: widget ↔ video space
// ---------------------------------------------------------------------------

QRect CameraPreviewWidget::videoDisplayRect() const
{
    if (display_frame_.isNull()) return {};

    QSize img_size = display_frame_.size();
    img_size.scale(size(), Qt::KeepAspectRatio);

    int x = (width()  - img_size.width())  / 2;
    int y = (height() - img_size.height()) / 2;
    return QRect(x, y, img_size.width(), img_size.height());
}

QPoint CameraPreviewWidget::widgetToVideo(const QPoint &wp) const
{
    QRect vr = videoDisplayRect();
    if (vr.isEmpty() || display_frame_.isNull()) return {};

    int vx = (wp.x() - vr.x()) * display_frame_.width()  / vr.width();
    int vy = (wp.y() - vr.y()) * display_frame_.height() / vr.height();
    return {vx, vy};
}

QPoint CameraPreviewWidget::videoToWidget(const QPoint &vp) const
{
    QRect vr = videoDisplayRect();
    if (vr.isEmpty() || display_frame_.isNull()) return {};

    int wx = vr.x() + vp.x() * vr.width()  / display_frame_.width();
    int wy = vr.y() + vp.y() * vr.height() / display_frame_.height();
    return {wx, wy};
}

// ---------------------------------------------------------------------------
// Hit-testing against the image overlay rect
// ---------------------------------------------------------------------------

CameraPreviewWidget::HitZone
CameraPreviewWidget::hitTest(const QPoint &widgetPos) const
{
    if (!overlay_renderer_) return HitZone::NONE;

    const auto& img = overlay_renderer_->image_overlay();
    if (!img.enabled) return HitZone::NONE;

    auto rect = overlay_renderer_->image_rect();
    if (rect.w <= 0 || rect.h <= 0) return HitZone::NONE;

    // Convert overlay rect corners to widget coords
    QPoint tl = videoToWidget({rect.x, rect.y});
    QPoint br = videoToWidget({rect.x + rect.w, rect.y + rect.h});
    QRect wr(tl, br);

    int h = kHandleSize;

    // Check corner handles first (highest priority)
    if (QRect(wr.left()  - h, wr.top()    - h, h*2, h*2).contains(widgetPos))
        return HitZone::TOP_LEFT;
    if (QRect(wr.right() - h, wr.top()    - h, h*2, h*2).contains(widgetPos))
        return HitZone::TOP_RIGHT;
    if (QRect(wr.left()  - h, wr.bottom() - h, h*2, h*2).contains(widgetPos))
        return HitZone::BOTTOM_LEFT;
    if (QRect(wr.right() - h, wr.bottom() - h, h*2, h*2).contains(widgetPos))
        return HitZone::BOTTOM_RIGHT;

    // Edge handles (midpoints)
    int mx = (wr.left() + wr.right()) / 2;
    int my = (wr.top()  + wr.bottom()) / 2;
    if (QRect(mx - h, wr.top()    - h, h*2, h*2).contains(widgetPos))
        return HitZone::TOP;
    if (QRect(mx - h, wr.bottom() - h, h*2, h*2).contains(widgetPos))
        return HitZone::BOTTOM;
    if (QRect(wr.left()  - h, my - h, h*2, h*2).contains(widgetPos))
        return HitZone::LEFT;
    if (QRect(wr.right() - h, my - h, h*2, h*2).contains(widgetPos))
        return HitZone::RIGHT;

    // Body (inside the rect)
    if (wr.contains(widgetPos))
        return HitZone::BODY;

    return HitZone::NONE;
}

void CameraPreviewWidget::updateCursor(HitZone zone)
{
    switch (zone) {
    case HitZone::TOP_LEFT:
    case HitZone::BOTTOM_RIGHT:
        setCursor(Qt::SizeFDiagCursor);
        break;
    case HitZone::TOP_RIGHT:
    case HitZone::BOTTOM_LEFT:
        setCursor(Qt::SizeBDiagCursor);
        break;
    case HitZone::TOP:
    case HitZone::BOTTOM:
        setCursor(Qt::SizeVerCursor);
        break;
    case HitZone::LEFT:
    case HitZone::RIGHT:
        setCursor(Qt::SizeHorCursor);
        break;
    case HitZone::BODY:
        setCursor(Qt::SizeAllCursor);
        break;
    default:
        setCursor(Qt::ArrowCursor);
        break;
    }
}

// ---------------------------------------------------------------------------
// Mouse events
// ---------------------------------------------------------------------------

void CameraPreviewWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    HitZone zone = hitTest(event->pos());
    if (zone == HitZone::NONE) {
        QWidget::mousePressEvent(event);
        return;
    }

    drag_zone_ = zone;
    drag_start_ = event->pos();

    // Store original overlay position/size
    if (overlay_renderer_) {
        auto rect = overlay_renderer_->image_rect();
        orig_x_ = rect.x;
        orig_y_ = rect.y;
        orig_w_ = rect.w;
        orig_h_ = rect.h;
    }

    event->accept();
}

void CameraPreviewWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (drag_zone_ == HitZone::NONE) {
        // Just update cursor on hover
        updateCursor(hitTest(event->pos()));
        QWidget::mouseMoveEvent(event);
        return;
    }

    if (!overlay_renderer_) return;

    // Delta in widget pixels
    int dx_w = event->pos().x() - drag_start_.x();
    int dy_w = event->pos().y() - drag_start_.y();

    // Convert delta to video pixels
    QRect vr = videoDisplayRect();
    if (vr.isEmpty() || display_frame_.isNull()) return;

    int dx_v = dx_w * display_frame_.width()  / vr.width();
    int dy_v = dy_w * display_frame_.height() / vr.height();

    int new_x = orig_x_;
    int new_y = orig_y_;
    int new_w = orig_w_;
    int new_h = orig_h_;

    switch (drag_zone_) {
    case HitZone::BODY:
        new_x = orig_x_ + dx_v;
        new_y = orig_y_ + dy_v;
        break;
    case HitZone::TOP_LEFT:
        new_x = orig_x_ + dx_v;
        new_y = orig_y_ + dy_v;
        new_w = orig_w_ - dx_v;
        new_h = orig_h_ - dy_v;
        break;
    case HitZone::TOP:
        new_y = orig_y_ + dy_v;
        new_h = orig_h_ - dy_v;
        break;
    case HitZone::TOP_RIGHT:
        new_y = orig_y_ + dy_v;
        new_w = orig_w_ + dx_v;
        new_h = orig_h_ - dy_v;
        break;
    case HitZone::RIGHT:
        new_w = orig_w_ + dx_v;
        break;
    case HitZone::BOTTOM_RIGHT:
        new_w = orig_w_ + dx_v;
        new_h = orig_h_ + dy_v;
        break;
    case HitZone::BOTTOM:
        new_h = orig_h_ + dy_v;
        break;
    case HitZone::BOTTOM_LEFT:
        new_x = orig_x_ + dx_v;
        new_w = orig_w_ - dx_v;
        new_h = orig_h_ + dy_v;
        break;
    case HitZone::LEFT:
        new_x = orig_x_ + dx_v;
        new_w = orig_w_ - dx_v;
        break;
    default:
        break;
    }

    // Enforce minimum size (16x16 video pixels)
    if (new_w < 16) new_w = 16;
    if (new_h < 16) new_h = 16;

    // Apply to renderer
    auto img = overlay_renderer_->image_overlay();
    img.x = new_x;
    img.y = new_y;
    img.render_width  = new_w;
    img.render_height = new_h;
    overlay_renderer_->set_image_overlay(img);

    emit imageOverlayMoved(new_x, new_y, new_w, new_h);

    event->accept();
}

void CameraPreviewWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && drag_zone_ != HitZone::NONE) {
        drag_zone_ = HitZone::NONE;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

// ---------------------------------------------------------------------------
// Paint
// ---------------------------------------------------------------------------

void CameraPreviewWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (display_frame_.isNull()) {
        /* No camera — show placeholder */
        p.fillRect(rect(), QColor(0x1a, 0x1a, 0x2e));
        p.setPen(QColor(0x66, 0x66, 0x88));
        QFont f(QStringLiteral("Menlo"), 14);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("No Camera"));
        return;
    }

    /* Scale to widget size maintaining aspect ratio */
    QRect vr = videoDisplayRect();

    /* Letterbox background */
    p.fillRect(rect(), Qt::black);

    /* Draw scaled frame */
    p.drawImage(vr, display_frame_);

    /* Draw image overlay selection handles */
    if (overlay_renderer_) {
        const auto& img = overlay_renderer_->image_overlay();
        if (img.enabled) {
            auto rect = overlay_renderer_->image_rect();
            if (rect.w > 0 && rect.h > 0) {
                QPoint tl = videoToWidget({rect.x, rect.y});
                QPoint br = videoToWidget({rect.x + rect.w, rect.y + rect.h});
                QRect wr(tl, br);

                // Selection border — dashed teal
                QPen selPen(QColor(0x00, 0xd4, 0xaa), 1.5, Qt::DashLine);
                p.setPen(selPen);
                p.setBrush(Qt::NoBrush);
                p.drawRect(wr);

                // Resize handles — solid teal squares
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0x00, 0xd4, 0xaa));

                int h = kHandleSize;
                auto drawHandle = [&](int cx, int cy) {
                    p.drawRect(cx - h/2, cy - h/2, h, h);
                };

                // 4 corners
                drawHandle(wr.left(),  wr.top());
                drawHandle(wr.right(), wr.top());
                drawHandle(wr.left(),  wr.bottom());
                drawHandle(wr.right(), wr.bottom());

                // 4 edge midpoints
                int mx = (wr.left() + wr.right()) / 2;
                int my = (wr.top()  + wr.bottom()) / 2;
                drawHandle(mx, wr.top());
                drawHandle(mx, wr.bottom());
                drawHandle(wr.left(),  my);
                drawHandle(wr.right(), my);
            }
        }
    }
}

} // namespace mc1
