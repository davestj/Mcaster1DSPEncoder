/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * sonic_enhancer_dialog.h — BBE Sonic Maximizer clone UI
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_SONIC_ENHANCER_DIALOG_H
#define MC1_SONIC_ENHANCER_DIALOG_H

#include <QDialog>
#include "dsp/sonic_enhancer.h"

class QCheckBox;
class QComboBox;
class QDial;
class QLabel;

namespace mc1 {

class SonicEnhancerDialog : public QDialog {
    Q_OBJECT

public:
    explicit SonicEnhancerDialog(QWidget *parent = nullptr);

    void setConfig(const mc1dsp::SonicEnhancerConfig &cfg);
    mc1dsp::SonicEnhancerConfig config() const;

    /* Preset name persistence */
    QString currentPresetName() const;
    void setCurrentPresetName(const QString& name);

Q_SIGNALS:
    void configChanged(const mc1dsp::SonicEnhancerConfig &cfg);
    void saveRequested(const mc1dsp::SonicEnhancerConfig &cfg);

protected:
    void closeEvent(QCloseEvent *event) override;

private Q_SLOTS:
    void onLoContourChanged(int value);
    void onProcessChanged(int value);
    void onOutputChanged(int value);
    void onEnableToggled(bool checked);
    void onPresetChanged(int index);

private:
    void emitConfig();
    QLabel *makeKnobLabel(const QString &text);

    QDial     *dial_lo_contour_ = nullptr;
    QDial     *dial_process_    = nullptr;
    QDial     *dial_output_     = nullptr;
    QLabel    *lbl_lo_val_      = nullptr;
    QLabel    *lbl_proc_val_    = nullptr;
    QLabel    *lbl_out_val_     = nullptr;
    QCheckBox *chk_enable_      = nullptr;
    QComboBox *cmb_preset_      = nullptr;
};

} // namespace mc1

#endif // MC1_SONIC_ENHANCER_DIALOG_H
