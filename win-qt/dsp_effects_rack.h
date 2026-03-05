/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * dsp_effects_rack.h — DSP Effects Rack tab (4th tab in encoder list)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_DSP_EFFECTS_RACK_H
#define MC1_DSP_EFFECTS_RACK_H

#include <QWidget>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QVector>
#include "global_config_manager.h"

namespace mc1 {

class DspEffectsRack : public QWidget {
    Q_OBJECT

public:
    explicit DspEffectsRack(QWidget *parent = nullptr);

    /* Refresh enable states + status labels from live pipeline */
    void refresh();

    /* Load initial enable state from persisted config */
    void loadFromState(const DspRackEnableState& state);

Q_SIGNALS:
    void openEq10();
    void openEq31();
    void openSonic();
    void openPttDuck();
    void openAgc();
    void openDbxVoice();
    void dspToggleChanged();
    void statusMessage(const QString &msg);

private:
    struct PluginRow {
        QCheckBox   *chk_enable;
        QLabel      *lbl_name;
        QLabel      *lbl_status;
        QPushButton *btn_configure;
        QLabel      *led;
    };

    void buildUI();

    QVector<PluginRow> rows_;
};

} // namespace mc1

#endif // MC1_DSP_EFFECTS_RACK_H
