/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * config_dialog.h — Encoder configuration dialog (4-tab QTabWidget)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_CONFIG_DIALOG_H
#define MC1_CONFIG_DIALOG_H

#include <QDialog>
#include "config_types.h"

namespace mc1 {

class BasicSettingsTab;
class YPSettingsTab;
class AdvancedSettingsTab;
class ICY22SettingsTab;
class PodcastSettingsTab;
class EffectsRackTab;

class ConfigDialog : public QDialog {
    Q_OBJECT

public:
    explicit ConfigDialog(const EncoderConfig &cfg, QWidget *parent = nullptr);

    EncoderConfig result() const;

Q_SIGNALS:
    void configAccepted(const EncoderConfig &cfg);

private:
    void accept() override;

    EncoderConfig cfg_;
    BasicSettingsTab    *tab_basic_    = nullptr;
    YPSettingsTab       *tab_yp_       = nullptr;
    AdvancedSettingsTab *tab_advanced_  = nullptr;
    ICY22SettingsTab    *tab_icy22_    = nullptr;
    PodcastSettingsTab  *tab_podcast_  = nullptr;
    EffectsRackTab      *tab_effects_  = nullptr;
};

} // namespace mc1

#endif // MC1_CONFIG_DIALOG_H
