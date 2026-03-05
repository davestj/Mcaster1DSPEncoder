/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * effects_rack_tab.h — Per-encoder DSP effects rack tab for ConfigDialog
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_EFFECTS_RACK_TAB_H
#define MC1_EFFECTS_RACK_TAB_H

#include <QWidget>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QVector>
#include "config_types.h"

namespace mc1 {

class EffectsRackTab : public QWidget {
    Q_OBJECT

public:
    explicit EffectsRackTab(QWidget *parent = nullptr);

    void loadFromConfig(const EncoderConfig &cfg);
    void saveToConfig(EncoderConfig &cfg) const;

    /* Snapshot of config for Configure dialogs to read */
    EncoderConfig& cfgSnapshot() { return cfg_snapshot_; }
    const EncoderConfig& cfgSnapshot() const { return cfg_snapshot_; }

Q_SIGNALS:
    /* Emitted when user clicks Configure on a specific plugin */
    void openEq10();
    void openEq31();
    void openSonic();
    void openPttDuck();
    void openAgc();
    void openDbxVoice();

private:
    void buildUI();

    struct PluginRow {
        QCheckBox   *chk_enable;
        QLabel      *lbl_name;
        QLabel      *lbl_status;
        QPushButton *btn_configure;
        QLabel      *led;
    };

    QVector<PluginRow> rows_;
    EncoderConfig cfg_snapshot_;
};

} // namespace mc1

#endif // MC1_EFFECTS_RACK_TAB_H
