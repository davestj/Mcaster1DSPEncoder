/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * preset_browser_dialog.h — Encoder preset browser + custom presets
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_PRESET_BROWSER_DIALOG_H
#define MC1_PRESET_BROWSER_DIALOG_H

#include <QDialog>
#include "config_types.h"

class QLabel;
class QListWidget;
class QPushButton;

namespace mc1 {

class PresetBrowserDialog : public QDialog {
    Q_OBJECT

public:
    explicit PresetBrowserDialog(QWidget *parent = nullptr);

    void setCurrentConfig(const EncoderConfig &cfg);

Q_SIGNALS:
    void presetSelected(const EncoderConfig &cfg);

private Q_SLOTS:
    void onPresetClicked(int row);
    void onApply();
    void onSaveCustom();
    void onDeleteCustom();

private:
    void populateList();
    void showPresetDetails(int row);

    QListWidget  *list_presets_ = nullptr;
    QLabel       *lbl_codec_   = nullptr;
    QLabel       *lbl_bitrate_ = nullptr;
    QLabel       *lbl_sr_      = nullptr;
    QLabel       *lbl_ch_      = nullptr;
    QLabel       *lbl_desc_    = nullptr;
    QPushButton  *btn_apply_   = nullptr;
    QPushButton  *btn_save_    = nullptr;
    QPushButton  *btn_delete_  = nullptr;

    EncoderConfig current_cfg_;
    int           builtin_count_ = 0;
};

} // namespace mc1

#endif // MC1_PRESET_BROWSER_DIALOG_H
