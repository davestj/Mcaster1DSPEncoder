/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * vu_meter_widget.cpp — Stereo VU peak meter
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "vu_meter_widget.h"

#include <QPainter>
#include <QLinearGradient>
#include <algorithm>
#include <cmath>

namespace mc1 {

VuMeterWidget::VuMeterWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(minimumSizeHint());

    connect(&decay_timer_, &QTimer::timeout, this, &VuMeterWidget::decayPeaks);
    decay_timer_.start(30); /* ~33 fps decay */
}

void VuMeterWidget::setLevels(float left_db, float right_db)
{
    left_db_  = std::clamp(left_db,  DB_MIN, DB_MAX);
    right_db_ = std::clamp(right_db, DB_MIN, DB_MAX);

    if (left_db_ > peak_left_)   peak_left_  = left_db_;
    if (right_db_ > peak_right_) peak_right_ = right_db_;

    update();
}

void VuMeterWidget::decayPeaks()
{
    constexpr float decay_rate = 0.5f; /* dB per tick */
    bool changed = false;

    if (peak_left_ > left_db_ + 0.1f) {
        peak_left_ -= decay_rate;
        changed = true;
    }
    if (peak_right_ > right_db_ + 0.1f) {
        peak_right_ -= decay_rate;
        changed = true;
    }

    if (changed) update();
}

void VuMeterWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int w = width();
    const int h = height();
    const int bar_h = (h - 14) / 2; /* room for labels */
    const int margin = 40;          /* left margin for dB labels */
    const int bar_w = w - margin - 8;

    /* Background */
    p.fillRect(rect(), QColor(20, 20, 30));

    /* dB scale labels */
    p.setPen(QColor(140, 140, 160));
    QFont font = p.font();
    font.setPixelSize(9);
    p.setFont(font);

    const float db_marks[] = {-60, -45, -30, -15, -6, -3, 0};
    for (float db : db_marks) {
        float norm = (db - DB_MIN) / DB_RANGE;
        int x = margin + static_cast<int>(norm * bar_w);
        p.drawText(x - 8, h - 1, QString::number(static_cast<int>(db)));
    }

    /* Channel labels */
    p.setPen(QColor(180, 180, 200));
    p.drawText(4, 4 + bar_h / 2 + 3, QStringLiteral("L"));
    p.drawText(4, 4 + bar_h + 6 + bar_h / 2 + 3, QStringLiteral("R"));

    /* Draw meter bars */
    auto drawBar = [&](int y, float level_db, float peak_db) {
        /* Gradient: green → yellow → red */
        QLinearGradient grad(margin, 0, margin + bar_w, 0);
        grad.setColorAt(0.0,  QColor(0, 180, 80));    /* green */
        grad.setColorAt(0.6,  QColor(0, 200, 60));    /* green */
        grad.setColorAt(0.75, QColor(220, 200, 0));    /* yellow at -15 dB */
        grad.setColorAt(0.9,  QColor(255, 120, 0));    /* orange at -6 dB */
        grad.setColorAt(1.0,  QColor(255, 30, 30));    /* red at 0 dB */

        /* Background track */
        p.fillRect(margin, y, bar_w, bar_h, QColor(30, 30, 45));

        /* Active fill */
        float norm = (level_db - DB_MIN) / DB_RANGE;
        norm = std::clamp(norm, 0.0f, 1.0f);
        int fill_w = static_cast<int>(norm * bar_w);

        if (fill_w > 0) {
            p.setBrush(grad);
            p.setPen(Qt::NoPen);
            p.drawRect(margin, y, fill_w, bar_h);
        }

        /* Peak indicator */
        if (peak_hold_) {
            float pnorm = (peak_db - DB_MIN) / DB_RANGE;
            pnorm = std::clamp(pnorm, 0.0f, 1.0f);
            int px = margin + static_cast<int>(pnorm * bar_w);
            p.setPen(QPen(QColor(255, 255, 255), 2));
            p.drawLine(px, y, px, y + bar_h);
        }

        /* Segment lines */
        p.setPen(QPen(QColor(20, 20, 30), 1));
        for (int seg = 1; seg < 30; ++seg) {
            int sx = margin + (bar_w * seg) / 30;
            p.drawLine(sx, y, sx, y + bar_h);
        }
    };

    drawBar(4, left_db_, peak_left_);
    drawBar(4 + bar_h + 6, right_db_, peak_right_);
}

} // namespace mc1
