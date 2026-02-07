/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * dbx_voice_dialog.h — DBX 286s Voice Processor settings dialog
 *
 * 5-section voice processor arranged in 3 tabs (Dynamics, De-Esser, Enhancers)
 * with horizontal rack-unit layout, sliders, presets, and GR meters.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_DBX_VOICE_DIALOG_H
#define MC1_DBX_VOICE_DIALOG_H

#include <QDialog>
#include <QTimer>
#include "dsp/dbx_voice.h"

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSlider;
class QTabWidget;

namespace mc1 {

class MeterBar;  // from agc_settings_dialog.h

class DbxVoiceDialog : public QDialog {
    Q_OBJECT

public:
    explicit DbxVoiceDialog(QWidget *parent = nullptr);

    mc1dsp::DbxVoiceConfig dbxConfig() const;
    void setDbxConfig(const mc1dsp::DbxVoiceConfig& cfg);

    /* Preset name persistence */
    QString currentPresetName() const;
    void setCurrentPresetName(const QString& name);

signals:
    void configChanged(const mc1dsp::DbxVoiceConfig &cfg);
    void saveRequested(const mc1dsp::DbxVoiceConfig &cfg);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onPresetChanged(int index);
    void onSliderChanged();
    void onApply();
    void onMeterTick();

private:
    void buildUi();
    QWidget* buildDynamicsTab();
    QWidget* buildDeEsserTab();
    QWidget* buildEnhancersTab();
    void syncSlidersToConfig(const mc1dsp::DbxVoiceConfig& cfg);
    void updateValueLabels();

    mc1dsp::DbxVoiceConfig cfg_;

    QTabWidget *tabs_ = nullptr;

    /* Preset */
    QComboBox *cmb_preset_ = nullptr;

    /* Master enable */
    QCheckBox *chk_enabled_ = nullptr;

    /* Gate section */
    QCheckBox *chk_gate_on_       = nullptr;
    QSlider   *sld_gate_thresh_   = nullptr;
    QSlider   *sld_gate_ratio_    = nullptr;
    QSlider   *sld_gate_attack_   = nullptr;
    QSlider   *sld_gate_release_  = nullptr;
    QLabel    *lbl_gate_thresh_   = nullptr;
    QLabel    *lbl_gate_ratio_    = nullptr;
    QLabel    *lbl_gate_attack_   = nullptr;
    QLabel    *lbl_gate_release_  = nullptr;

    /* Compressor section */
    QCheckBox *chk_comp_on_       = nullptr;
    QSlider   *sld_comp_thresh_   = nullptr;
    QSlider   *sld_comp_ratio_    = nullptr;
    QSlider   *sld_comp_attack_   = nullptr;
    QSlider   *sld_comp_release_  = nullptr;
    QLabel    *lbl_comp_thresh_   = nullptr;
    QLabel    *lbl_comp_ratio_    = nullptr;
    QLabel    *lbl_comp_attack_   = nullptr;
    QLabel    *lbl_comp_release_  = nullptr;

    /* De-Esser section */
    QCheckBox *chk_deess_on_      = nullptr;
    QSlider   *sld_deess_freq_    = nullptr;
    QSlider   *sld_deess_thresh_  = nullptr;
    QSlider   *sld_deess_reduce_  = nullptr;
    QSlider   *sld_deess_q_       = nullptr;
    QLabel    *lbl_deess_freq_    = nullptr;
    QLabel    *lbl_deess_thresh_  = nullptr;
    QLabel    *lbl_deess_reduce_  = nullptr;
    QLabel    *lbl_deess_q_       = nullptr;

    /* LF Enhancer section */
    QCheckBox *chk_lf_on_         = nullptr;
    QSlider   *sld_lf_freq_       = nullptr;
    QSlider   *sld_lf_amount_     = nullptr;
    QLabel    *lbl_lf_freq_       = nullptr;
    QLabel    *lbl_lf_amount_     = nullptr;

    /* HF Detail section */
    QCheckBox *chk_hf_on_         = nullptr;
    QSlider   *sld_hf_freq_       = nullptr;
    QSlider   *sld_hf_amount_     = nullptr;
    QLabel    *lbl_hf_freq_       = nullptr;
    QLabel    *lbl_hf_amount_     = nullptr;

    /* Meters */
    MeterBar  *meter_gate_gr_     = nullptr;
    MeterBar  *meter_comp_gr_     = nullptr;
    MeterBar  *meter_deess_gr_    = nullptr;
    QTimer    *meter_timer_       = nullptr;

    /* Buttons */
    QPushButton *btn_apply_         = nullptr;
    QPushButton *btn_save_settings_ = nullptr;
    QPushButton *btn_close_         = nullptr;
};

} // namespace mc1

#endif // MC1_DBX_VOICE_DIALOG_H
