/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * agc_settings_dialog.h — Professional AGC audio processing dialog
 *
 * Broadcast-grade AGC with custom-painted level meters, preset system,
 * system-wide vs per-encoder scope, and industry-standard tooltips.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_AGC_SETTINGS_DIALOG_H
#define MC1_AGC_SETTINGS_DIALOG_H

#include <QDialog>
#include <QTimer>
#include "dsp/agc.h"

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QSlider;

namespace mc1 {

/* ─── MeterBar: Custom-painted vertical level meter ─────────────────────── */
class MeterBar : public QWidget {
    Q_OBJECT
public:
    enum Style { INPUT_METER, GR_METER, OUTPUT_METER };

    explicit MeterBar(Style style, QWidget *parent = nullptr);

    void setLevel(float db);
    void setPeakHold(float db);

    QSize minimumSizeHint() const override { return {28, 120}; }
    QSize sizeHint()        const override { return {32, 180}; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Style style_;
    float level_db_      = -60.0f;
    float peak_hold_db_  = -60.0f;
    int   peak_hold_cnt_ = 0;

    static constexpr float DB_MIN = -60.0f;
    static constexpr float DB_MAX =   0.0f;
};

/* ─── AgcSettingsDialog ─────────────────────────────────────────────────── */
class AgcSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit AgcSettingsDialog(QWidget *parent = nullptr);
    ~AgcSettingsDialog() override;

    /* Get/set the current AGC configuration */
    mc1dsp::AgcConfig agcConfig() const;
    void setAgcConfig(const mc1dsp::AgcConfig& cfg);

    /* Preset name persistence */
    QString currentPresetName() const;
    void setCurrentPresetName(const QString& name);

    /* Scope: system-wide or per-encoder */
    bool isSystemWide() const;
    void setSystemWide(bool sw);

signals:
    void configChanged(const mc1dsp::AgcConfig& cfg);
    void saveRequested(const mc1dsp::AgcConfig& cfg);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onPresetChanged(int index);
    void onSavePreset();
    void onDeletePreset();
    void onSliderChanged();
    void onApply();
    void onReset();
    void onMeterTick();

private:
    void buildUi();
    QGroupBox* buildInputSection();
    QGroupBox* buildCompressorSection();
    QGroupBox* buildLimiterSection();
    QWidget*   buildMeterSection();
    void syncSlidersToConfig(const mc1dsp::AgcConfig& cfg);
    void updateValueLabels();
    void populatePresets();

    /* Slider helper: creates a labeled slider with value readout and tooltip */
    struct SliderRow {
        QSlider *slider;
        QLabel  *value_label;
    };
    SliderRow makeSlider(const QString& label, const QString& tooltip,
                         int min_val, int max_val, int initial,
                         QWidget *parent);

    /* Config state */
    mc1dsp::AgcConfig cfg_;

    /* Preset bar */
    QComboBox   *cmb_preset_       = nullptr;
    QPushButton *btn_save_preset_  = nullptr;
    QPushButton *btn_delete_preset_= nullptr;

    /* Scope */
    QRadioButton *rb_system_wide_  = nullptr;
    QRadioButton *rb_per_encoder_  = nullptr;

    /* Enable */
    QCheckBox *chk_enabled_        = nullptr;

    /* Input section */
    QSlider *sld_input_gain_       = nullptr;
    QSlider *sld_gate_threshold_   = nullptr;
    QLabel  *lbl_input_gain_       = nullptr;
    QLabel  *lbl_gate_threshold_   = nullptr;

    /* Compressor section */
    QSlider *sld_threshold_        = nullptr;
    QSlider *sld_ratio_            = nullptr;
    QSlider *sld_attack_           = nullptr;
    QSlider *sld_release_          = nullptr;
    QSlider *sld_knee_             = nullptr;
    QSlider *sld_makeup_           = nullptr;
    QLabel  *lbl_threshold_        = nullptr;
    QLabel  *lbl_ratio_            = nullptr;
    QLabel  *lbl_attack_           = nullptr;
    QLabel  *lbl_release_          = nullptr;
    QLabel  *lbl_knee_             = nullptr;
    QLabel  *lbl_makeup_           = nullptr;

    /* Limiter section */
    QSlider *sld_limiter_          = nullptr;
    QLabel  *lbl_limiter_          = nullptr;

    /* Meters */
    MeterBar *meter_input_         = nullptr;
    MeterBar *meter_gr_            = nullptr;
    MeterBar *meter_output_        = nullptr;

    /* Meter refresh */
    QTimer  *meter_timer_          = nullptr;

    /* Buttons */
    QPushButton *btn_apply_         = nullptr;
    QPushButton *btn_save_settings_ = nullptr;
    QPushButton *btn_reset_         = nullptr;
    QPushButton *btn_close_         = nullptr;
};

} // namespace mc1

#endif // MC1_AGC_SETTINGS_DIALOG_H
