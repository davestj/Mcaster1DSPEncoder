/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * eq_curve_widget.h — Bode magnitude plot for 10-band parametric EQ
 *                     + interactive EQ editor dialog
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_EQ_CURVE_WIDGET_H
#define MC1_EQ_CURVE_WIDGET_H

#include <QDialog>
#include <QWidget>
#include <array>
#include <vector>
#include "dsp/eq.h"
#include "dsp/eq31.h"

class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QDoubleSpinBox;

namespace mc1 {

/* ── EQ Frequency Response Widget ──
 * Paints a Bode magnitude plot (20 Hz - 20 kHz, +/-18 dB)
 * showing the combined frequency response of all EQ bands.
 * Supports both 10-band parametric and 31-band graphic modes.
 * In 31-band dual-channel mode, draws L (teal) and R (orange) curves.
 */
class EqCurveWidget : public QWidget {
    Q_OBJECT

public:
    explicit EqCurveWidget(QWidget *parent = nullptr);

    /* 10-band parametric mode */
    void setBands(const std::array<mc1dsp::EqBandConfig, 10> &bands,
                  int sample_rate);

    /* 31-band graphic mode (dual-channel) */
    void setBands31(const std::array<float, 31> &gains_l,
                    const std::array<float, 31> &gains_r,
                    int sample_rate);

    QSize minimumSizeHint() const override { return {400, 200}; }
    QSize sizeHint()        const override { return {600, 280}; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    /* Compute |H(e^jw)| in dB for a single biquad at frequency f */
    static float biquadMagnitudeDb(const mc1dsp::EqBandConfig &band,
                                   int sample_rate, float freq_hz);
    /* Compute peaking filter magnitude for 31-band mode */
    static float peakingMagnitudeDb(float gain_db, float center_freq,
                                    float q, int sample_rate, float freq_hz);

    std::array<mc1dsp::EqBandConfig, 10> bands_{};
    int sample_rate_ = 44100;

    /* 31-band mode */
    bool mode_31_ = false;
    std::array<float, 31> gains_l_{};
    std::array<float, 31> gains_r_{};
};

/* ── Interactive 10-band parametric EQ editor dialog ── */
class EqVisualizerDialog : public QDialog {
    Q_OBJECT

public:
    explicit EqVisualizerDialog(QWidget *parent = nullptr);
    EqCurveWidget *curveWidget() { return curve_; }
    const std::array<mc1dsp::EqBandConfig, 10>& bands() const { return bands_; }

    /* Load band config from saved per-encoder params into the dialog */
    void setBands(const std::array<mc1dsp::EqBandConfig, 10> &bands, int sample_rate);

    void loadFromPipeline();

    QString currentPresetName() const;
    void setCurrentPresetName(const QString& name);

Q_SIGNALS:
    void eqConfigApplied();
    void saveRequested();

protected:
    void closeEvent(QCloseEvent *event) override;

private Q_SLOTS:
    void onPresetChanged(int index);
    void onBandGainChanged(int band_index);
    void onSelectedBandChanged(int band_index);
    void onBandFreqChanged(double val);
    void onBandQChanged(double val);
    void onApply();
    void onSaveSettings();
    void onReset();

private:
    void buildUi();
    void updateCurve();
    void syncSlidersFromBands();

    EqCurveWidget *curve_ = nullptr;

    /* Preset */
    QComboBox *cmb_preset_ = nullptr;

    /* Per-band gain sliders (10 vertical sliders) */
    std::array<QSlider*, 10> sld_gain_{};
    std::array<QLabel*, 10>  lbl_gain_{};
    std::array<QLabel*, 10>  lbl_freq_{};

    /* Selected band detail (freq + Q editing) */
    int selected_band_ = 0;
    QLabel          *lbl_selected_band_ = nullptr;
    QDoubleSpinBox  *spin_freq_         = nullptr;
    QDoubleSpinBox  *spin_q_            = nullptr;
    QComboBox       *cmb_band_type_     = nullptr;

    /* Band configs */
    std::array<mc1dsp::EqBandConfig, 10> bands_{};
    int sample_rate_ = 44100;

    QPushButton *btn_apply_         = nullptr;
    QPushButton *btn_save_settings_ = nullptr;
    QPushButton *btn_reset_         = nullptr;
    QPushButton *btn_close_         = nullptr;
};

} // namespace mc1

#endif // MC1_EQ_CURVE_WIDGET_H
