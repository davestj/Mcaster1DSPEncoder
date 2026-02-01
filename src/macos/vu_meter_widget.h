/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * vu_meter_widget.h — Stereo VU peak meter (custom paint widget)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_VU_METER_WIDGET_H
#define MC1_VU_METER_WIDGET_H

#include <QWidget>
#include <QTimer>

namespace mc1 {

class VuMeterWidget : public QWidget {
    Q_OBJECT

public:
    explicit VuMeterWidget(QWidget *parent = nullptr);

    void setLevels(float left_db, float right_db);
    void setPeakHold(bool enabled) { peak_hold_ = enabled; }

    QSize minimumSizeHint() const override { return {200, 32}; }
    QSize sizeHint()        const override { return {400, 40}; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void decayPeaks();

    float left_db_    = -60.0f;
    float right_db_   = -60.0f;
    float peak_left_  = -60.0f;
    float peak_right_ = -60.0f;
    bool  peak_hold_  = true;

    QTimer decay_timer_;

    static constexpr float DB_MIN   = -60.0f;
    static constexpr float DB_MAX   =   0.0f;
    static constexpr float DB_RANGE =  60.0f;
};

} // namespace mc1

#endif // MC1_VU_METER_WIDGET_H
