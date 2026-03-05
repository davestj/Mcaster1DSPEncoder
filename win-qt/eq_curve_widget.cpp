/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * eq_curve_widget.cpp — EQ frequency response curve visualizer
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "eq_curve_widget.h"
#include <QCloseEvent>
#include "audio_pipeline.h"
#include "encoder_slot.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <cmath>

namespace mc1 {

/* ── EqCurveWidget ── */

EqCurveWidget::EqCurveWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(400, 200);
    /* Initialize flat bands */
    for (int i = 0; i < 10; ++i) {
        bands_[i].gain_db = 0.0f;
        bands_[i].freq_hz = 31.0f * std::pow(2.0f, i); /* 31, 62, 125 ... 16k */
        bands_[i].q       = 1.0f;
        bands_[i].enabled = true;
        bands_[i].type    = (i == 0) ? mc1dsp::EqBandType::LOW_SHELF
                          : (i == 9) ? mc1dsp::EqBandType::HIGH_SHELF
                          : mc1dsp::EqBandType::PEAKING;
    }
}

void EqCurveWidget::setBands(const std::array<mc1dsp::EqBandConfig, 10> &bands,
                              int sample_rate)
{
    mode_31_ = false;
    bands_       = bands;
    sample_rate_ = sample_rate;
    update(); /* trigger repaint */
}

void EqCurveWidget::setBands31(const std::array<float, 31> &gains_l,
                                const std::array<float, 31> &gains_r,
                                int sample_rate)
{
    mode_31_ = true;
    gains_l_ = gains_l;
    gains_r_ = gains_r;
    sample_rate_ = sample_rate;
    update();
}

float EqCurveWidget::peakingMagnitudeDb(float gain_db, float center_freq,
                                         float q, int sample_rate, float freq_hz)
{
    if (std::abs(gain_db) < 0.001f) return 0.0f;

    const double pi = 3.14159265358979323846;
    double A     = std::pow(10.0, gain_db / 40.0);
    double w0    = 2.0 * pi * center_freq / sample_rate;
    double alpha = std::sin(w0) / (2.0 * q);

    double b0 = 1.0 + alpha * A;
    double b1 = -2.0 * std::cos(w0);
    double b2 = 1.0 - alpha * A;
    double a0 = 1.0 + alpha / A;
    double a1 = -2.0 * std::cos(w0);
    double a2 = 1.0 - alpha / A;

    b0 /= a0; b1 /= a0; b2 /= a0;
    a1 /= a0; a2 /= a0;

    double w     = 2.0 * pi * freq_hz / sample_rate;
    double cos_w = std::cos(w), cos_2w = std::cos(2.0 * w);
    double sin_w = std::sin(w), sin_2w = std::sin(2.0 * w);

    double nr = b0 + b1*cos_w + b2*cos_2w;
    double ni = -(b1*sin_w + b2*sin_2w);
    double dr = 1.0 + a1*cos_w + a2*cos_2w;
    double di = -(a1*sin_w + a2*sin_2w);

    double num2 = nr*nr + ni*ni;
    double den2 = dr*dr + di*di;
    if (den2 < 1e-20) return 0.0f;

    return static_cast<float>(10.0 * std::log10(num2 / den2));
}

float EqCurveWidget::biquadMagnitudeDb(const mc1dsp::EqBandConfig &band,
                                        int sample_rate, float freq_hz)
{
    if (!band.enabled || std::abs(band.gain_db) < 0.001f)
        return 0.0f;

    /* RBJ biquad coefficient computation */
    const double pi  = 3.14159265358979323846;
    double fs = sample_rate;
    double f0 = band.freq_hz;
    double Q  = std::max(0.1, static_cast<double>(band.q));
    double dBgain = band.gain_db;
    double A = std::pow(10.0, dBgain / 40.0);

    double w0    = 2.0 * pi * f0 / fs;
    double alpha = std::sin(w0) / (2.0 * Q);

    double b0, b1, b2, a0, a1, a2;

    switch (band.type) {
    case mc1dsp::EqBandType::LOW_SHELF: {
        double sq = 2.0 * std::sqrt(A) * alpha;
        b0 =     A*((A+1) - (A-1)*std::cos(w0) + sq);
        b1 = 2*A*  ((A-1) - (A+1)*std::cos(w0));
        b2 =     A*((A+1) - (A-1)*std::cos(w0) - sq);
        a0 =        (A+1) + (A-1)*std::cos(w0) + sq;
        a1 =   -2*((A-1) + (A+1)*std::cos(w0));
        a2 =        (A+1) + (A-1)*std::cos(w0) - sq;
        break;
    }
    case mc1dsp::EqBandType::HIGH_SHELF: {
        double sq = 2.0 * std::sqrt(A) * alpha;
        b0 =     A*((A+1) + (A-1)*std::cos(w0) + sq);
        b1 = -2*A*((A-1) + (A+1)*std::cos(w0));
        b2 =     A*((A+1) + (A-1)*std::cos(w0) - sq);
        a0 =       (A+1) - (A-1)*std::cos(w0) + sq;
        a1 =    2*((A-1) - (A+1)*std::cos(w0));
        a2 =       (A+1) - (A-1)*std::cos(w0) - sq;
        break;
    }
    default: /* PEAKING */ {
        b0 = 1.0 + alpha * A;
        b1 = -2.0 * std::cos(w0);
        b2 = 1.0 - alpha * A;
        a0 = 1.0 + alpha / A;
        a1 = -2.0 * std::cos(w0);
        a2 = 1.0 - alpha / A;
        break;
    }
    }

    /* Normalize */
    b0 /= a0; b1 /= a0; b2 /= a0;
    a1 /= a0; a2 /= a0;

    /* Evaluate |H(e^jw)| at the requested frequency */
    double w = 2.0 * pi * freq_hz / fs;
    double cos_w  = std::cos(w);
    double cos_2w = std::cos(2.0 * w);
    double sin_w  = std::sin(w);
    double sin_2w = std::sin(2.0 * w);

    double num_re = b0 + b1 * cos_w + b2 * cos_2w;
    double num_im = -(b1 * sin_w + b2 * sin_2w);
    double den_re = 1.0 + a1 * cos_w + a2 * cos_2w;
    double den_im = -(a1 * sin_w + a2 * sin_2w);

    double num_mag2 = num_re * num_re + num_im * num_im;
    double den_mag2 = den_re * den_re + den_im * den_im;

    if (den_mag2 < 1e-20) return 0.0f;

    double mag_db = 10.0 * std::log10(num_mag2 / den_mag2);
    return static_cast<float>(mag_db);
}

void EqCurveWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int w = width();
    const int h = height();
    const int margin_l = 40, margin_r = 10, margin_t = 10, margin_b = 24;
    const int plot_w = w - margin_l - margin_r;
    const int plot_h = h - margin_t - margin_b;

    /* Background */
    p.fillRect(rect(), QColor(0x1a, 0x1a, 0x2e));

    /* Grid */
    p.setPen(QPen(QColor(0x33, 0x33, 0x55), 1));

    /* Frequency grid lines: 100, 1k, 10k */
    const float log_min = std::log10(20.0f);
    const float log_max = std::log10(20000.0f);

    auto freqToX = [&](float freq) -> int {
        float t = (std::log10(freq) - log_min) / (log_max - log_min);
        return margin_l + static_cast<int>(t * plot_w);
    };
    auto dbToY = [&](float db) -> int {
        float t = (18.0f - db) / 36.0f; /* ±18 dB */
        return margin_t + static_cast<int>(t * plot_h);
    };

    /* Vertical grid: freq */
    float grid_freqs[] = {50, 100, 200, 500, 1000, 2000, 5000, 10000};
    p.setPen(QPen(QColor(0x33, 0x33, 0x55), 1, Qt::DotLine));
    QFont small_font(QStringLiteral("Menlo"), 8);
    p.setFont(small_font);
    for (float f : grid_freqs) {
        int x = freqToX(f);
        p.drawLine(x, margin_t, x, margin_t + plot_h);
        QString label = (f >= 1000) ? QString::number(f / 1000) + QStringLiteral("k")
                                     : QString::number(static_cast<int>(f));
        p.setPen(QColor(0x88, 0x88, 0xaa));
        p.drawText(x - 12, h - 4, label);
        p.setPen(QPen(QColor(0x33, 0x33, 0x55), 1, Qt::DotLine));
    }

    /* Horizontal grid: dB */
    float grid_dbs[] = {-12, -6, 0, 6, 12};
    for (float db : grid_dbs) {
        int y = dbToY(db);
        p.setPen(QPen(db == 0.0f ? QColor(0x55, 0x55, 0x77) : QColor(0x33, 0x33, 0x55),
                       db == 0.0f ? 1 : 1, db == 0.0f ? Qt::SolidLine : Qt::DotLine));
        p.drawLine(margin_l, y, margin_l + plot_w, y);
        p.setPen(QColor(0x88, 0x88, 0xaa));
        p.drawText(2, y + 4, QString::number(static_cast<int>(db)) + QStringLiteral("dB"));
    }

    /* ── EQ curve ── */
    if (mode_31_) {
        /* 31-band dual-channel mode */
        constexpr float Q31 = mc1dsp::DspEq31::Q_THIRD_OCTAVE;
        auto draw31Curve = [&](const std::array<float, 31> &gains,
                               QColor lineColor, QColor fillColor) {
            QPainterPath path;
            bool first = true;
            for (int i = 0; i <= plot_w; ++i) {
                float t = static_cast<float>(i) / plot_w;
                float freq = std::pow(10.0f, log_min + t * (log_max - log_min));
                float total_db = 0.0f;
                for (int b = 0; b < 31; ++b)
                    total_db += peakingMagnitudeDb(gains[b],
                        mc1dsp::DspEq31::CENTER_FREQS[b], Q31, sample_rate_, freq);
                total_db = std::clamp(total_db, -18.0f, 18.0f);
                int x = margin_l + i;
                int y = dbToY(total_db);
                if (first) { path.moveTo(x, y); first = false; }
                else       { path.lineTo(x, y); }
            }
            QPainterPath fill = path;
            fill.lineTo(margin_l + plot_w, dbToY(0));
            fill.lineTo(margin_l, dbToY(0));
            fill.closeSubpath();
            p.fillPath(fill, fillColor);
            p.setPen(QPen(lineColor, 2));
            p.drawPath(path);
        };

        /* L channel: teal */
        draw31Curve(gains_l_, QColor(0x0d, 0x94, 0x88), QColor(0x0d, 0x94, 0x88, 30));
        /* R channel: orange */
        draw31Curve(gains_r_, QColor(0xe0, 0x8a, 0x30), QColor(0xe0, 0x8a, 0x30, 30));

        /* Band markers for L channel */
        p.setPen(QPen(QColor(0x0d, 0x94, 0x88), 1));
        for (int b = 0; b < 31; ++b) {
            float total_db = 0.0f;
            for (int j = 0; j < 31; ++j)
                total_db += peakingMagnitudeDb(gains_l_[j],
                    mc1dsp::DspEq31::CENTER_FREQS[j], Q31, sample_rate_,
                    mc1dsp::DspEq31::CENTER_FREQS[b]);
            total_db = std::clamp(total_db, -18.0f, 18.0f);
            int bx = freqToX(mc1dsp::DspEq31::CENTER_FREQS[b]);
            int by = dbToY(total_db);
            p.setBrush(QColor(0x0d, 0x94, 0x88));
            p.drawEllipse(QPoint(bx, by), 3, 3);
        }
    } else {
        /* 10-band parametric mode */
        QPainterPath path;
        const int npoints = plot_w;
        bool first = true;

        for (int i = 0; i <= npoints; ++i) {
            float t = static_cast<float>(i) / npoints;
            float freq = std::pow(10.0f, log_min + t * (log_max - log_min));

            float total_db = 0.0f;
            for (int b = 0; b < 10; ++b)
                total_db += biquadMagnitudeDb(bands_[b], sample_rate_, freq);
            total_db = std::clamp(total_db, -18.0f, 18.0f);

            int x = margin_l + i;
            int y = dbToY(total_db);

            if (first) { path.moveTo(x, y); first = false; }
            else       { path.lineTo(x, y); }
        }

        QPainterPath fill = path;
        fill.lineTo(margin_l + plot_w, dbToY(0));
        fill.lineTo(margin_l, dbToY(0));
        fill.closeSubpath();
        p.fillPath(fill, QColor(0x0d, 0x94, 0x88, 40));
        p.setPen(QPen(QColor(0x0d, 0x94, 0x88), 2));
        p.drawPath(path);

        p.setPen(QPen(QColor(0x0d, 0x94, 0x88), 1));
        for (int b = 0; b < 10; ++b) {
            if (!bands_[b].enabled) continue;
            float total_at_band = 0.0f;
            for (int j = 0; j < 10; ++j)
                total_at_band += biquadMagnitudeDb(bands_[j], sample_rate_, bands_[b].freq_hz);
            total_at_band = std::clamp(total_at_band, -18.0f, 18.0f);
            int bx = freqToX(bands_[b].freq_hz);
            int by = dbToY(total_at_band);
            p.setBrush(QColor(0x0d, 0x94, 0x88));
            p.drawEllipse(QPoint(bx, by), 4, 4);
        }
    }

    /* Border */
    p.setPen(QPen(QColor(0x44, 0x44, 0x66), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(margin_l, margin_t, plot_w, plot_h);
}

/* ── EqVisualizerDialog — Interactive 10-band parametric EQ editor ── */

/* Default band center frequencies (Hz) */
static const float kDefaultFreqs[10] = {
    31.0f, 62.0f, 125.0f, 250.0f, 500.0f,
    1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
};

/* Preset names matching dsp/eq.h */
static const char* kPresetNames[] = {
    "(Flat)", "Classic Rock", "Country", "Modern Rock", "Broadcast", "Spoken Word"
};
static constexpr int kPresetCount = 6;

EqVisualizerDialog::EqVisualizerDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("10-Band Parametric EQ"));
    setMinimumSize(780, 520);
    resize(840, 580);

    /* Init flat bands */
    for (int i = 0; i < 10; ++i) {
        bands_[i].gain_db = 0.0f;
        bands_[i].freq_hz = kDefaultFreqs[i];
        bands_[i].q       = 1.0f;
        bands_[i].enabled = true;
        bands_[i].type    = (i == 0) ? mc1dsp::EqBandType::LOW_SHELF
                          : (i == 9) ? mc1dsp::EqBandType::HIGH_SHELF
                          : mc1dsp::EqBandType::PEAKING;
    }

    buildUi();
    loadFromPipeline();
}

void EqVisualizerDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    /* ── Preset row ──────────────────────────────────────────── */
    auto *preset_row = new QHBoxLayout;
    preset_row->addWidget(new QLabel(QStringLiteral("Preset:")));
    cmb_preset_ = new QComboBox;
    for (int i = 0; i < kPresetCount; ++i)
        cmb_preset_->addItem(QString::fromUtf8(kPresetNames[i]));
    connect(cmb_preset_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EqVisualizerDialog::onPresetChanged);
    preset_row->addWidget(cmb_preset_, 1);
    root->addLayout(preset_row);

    /* ── Frequency response curve (top) ──────────────────────── */
    curve_ = new EqCurveWidget;
    curve_->setMinimumHeight(220);
    root->addWidget(curve_, 3);

    /* ── Band gain sliders (10 vertical sliders) ─────────────── */
    auto *slider_grp = new QGroupBox(QStringLiteral("Band Gains"));
    auto *slider_row = new QHBoxLayout(slider_grp);
    slider_row->setSpacing(6);

    for (int i = 0; i < 10; ++i) {
        auto *col = new QVBoxLayout;
        col->setSpacing(2);

        /* Gain value label */
        lbl_gain_[i] = new QLabel(QStringLiteral("0 dB"));
        lbl_gain_[i]->setAlignment(Qt::AlignCenter);
        lbl_gain_[i]->setFixedWidth(50);
        col->addWidget(lbl_gain_[i], 0, Qt::AlignCenter);

        /* Vertical gain slider: -18 to +18 dB (inverted so up = positive) */
        sld_gain_[i] = new QSlider(Qt::Vertical);
        sld_gain_[i]->setRange(-180, 180);  /* x10 for 0.1 dB resolution */
        sld_gain_[i]->setValue(0);
        sld_gain_[i]->setFixedWidth(30);
        sld_gain_[i]->setMinimumHeight(120);
        connect(sld_gain_[i], &QSlider::valueChanged, this,
                [this, i]() { onBandGainChanged(i); });
        col->addWidget(sld_gain_[i], 1, Qt::AlignCenter);

        /* Frequency label below slider */
        float freq = kDefaultFreqs[i];
        QString freq_str = (freq >= 1000.0f)
            ? QString::number(freq / 1000.0f, 'g', 3) + QStringLiteral("k")
            : QString::number(static_cast<int>(freq));
        lbl_freq_[i] = new QLabel(freq_str);
        lbl_freq_[i]->setAlignment(Qt::AlignCenter);
        lbl_freq_[i]->setStyleSheet(QStringLiteral("font-size: 10px; color: #0d9488;"));
        col->addWidget(lbl_freq_[i], 0, Qt::AlignCenter);

        slider_row->addLayout(col);
    }
    root->addWidget(slider_grp, 2);

    /* ── Selected band detail row ────────────────────────────── */
    auto *detail_grp = new QGroupBox(QStringLiteral("Band Detail"));
    auto *detail_lay = new QHBoxLayout(detail_grp);

    lbl_selected_band_ = new QLabel(QStringLiteral("Band 1 (31 Hz)"));
    lbl_selected_band_->setStyleSheet(QStringLiteral("font-weight: bold;"));
    detail_lay->addWidget(lbl_selected_band_);

    detail_lay->addWidget(new QLabel(QStringLiteral("Type:")));
    cmb_band_type_ = new QComboBox;
    cmb_band_type_->addItem(QStringLiteral("Low Shelf"),  static_cast<int>(mc1dsp::EqBandType::LOW_SHELF));
    cmb_band_type_->addItem(QStringLiteral("Peaking"),    static_cast<int>(mc1dsp::EqBandType::PEAKING));
    cmb_band_type_->addItem(QStringLiteral("High Shelf"), static_cast<int>(mc1dsp::EqBandType::HIGH_SHELF));
    connect(cmb_band_type_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        bands_[selected_band_].type = static_cast<mc1dsp::EqBandType>(
            cmb_band_type_->currentData().toInt());
        updateCurve();
    });
    detail_lay->addWidget(cmb_band_type_);

    detail_lay->addWidget(new QLabel(QStringLiteral("Freq:")));
    spin_freq_ = new QDoubleSpinBox;
    spin_freq_->setRange(20.0, 20000.0);
    spin_freq_->setSuffix(QStringLiteral(" Hz"));
    spin_freq_->setDecimals(0);
    spin_freq_->setValue(31.0);
    connect(spin_freq_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &EqVisualizerDialog::onBandFreqChanged);
    detail_lay->addWidget(spin_freq_);

    detail_lay->addWidget(new QLabel(QStringLiteral("Q:")));
    spin_q_ = new QDoubleSpinBox;
    spin_q_->setRange(0.1, 20.0);
    spin_q_->setSingleStep(0.1);
    spin_q_->setDecimals(1);
    spin_q_->setValue(1.0);
    connect(spin_q_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &EqVisualizerDialog::onBandQChanged);
    detail_lay->addWidget(spin_q_);

    root->addWidget(detail_grp);

    /* Click on gain slider selects that band for detail editing */
    for (int i = 0; i < 10; ++i) {
        connect(sld_gain_[i], &QSlider::sliderPressed, this,
                [this, i]() { onSelectedBandChanged(i); });
    }

    /* ── Button bar ──────────────────────────────────────────── */
    auto *btn_row = new QHBoxLayout;
    btn_row->addStretch();

    btn_reset_ = new QPushButton(QStringLiteral("Flat"));
    btn_reset_->setToolTip(QStringLiteral("Reset all bands to flat (0 dB)"));
    btn_reset_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #4a3a2a; color: #e0c8a0; padding: 6px 16px; "
        "border: 1px solid #5a4a3a; border-radius: 4px; }"
        "QPushButton:hover { background: #5a4a3a; border-color: #7a6a5a; }"
        "QPushButton:pressed { background: #3a2a1a; border-color: #4a3a2a; "
        "padding-top: 8px; padding-bottom: 4px; }"));
    connect(btn_reset_, &QPushButton::clicked, this, &EqVisualizerDialog::onReset);
    btn_row->addWidget(btn_reset_);

    btn_apply_ = new QPushButton(QStringLiteral("Apply"));
    btn_apply_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #00d4aa; color: #0a0a1e; padding: 6px 16px; "
        "border: 1px solid #00b088; border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background: #00e8bb; border-color: #00d4aa; }"
        "QPushButton:pressed { background: #009977; border-color: #008866; "
        "padding-top: 8px; padding-bottom: 4px; }"));
    connect(btn_apply_, &QPushButton::clicked, this, &EqVisualizerDialog::onApply);
    btn_row->addWidget(btn_apply_);

    btn_save_settings_ = new QPushButton(QStringLiteral("Save Settings"));
    btn_save_settings_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #1a6a4a; color: #d0f0e0; padding: 6px 16px; "
        "border: 1px solid #2a7a5a; border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background: #2a8a6a; border-color: #3a9a7a; }"
        "QPushButton:pressed { background: #0a5a3a; border-color: #1a6a4a; "
        "padding-top: 8px; padding-bottom: 4px; }"));
    connect(btn_save_settings_, &QPushButton::clicked, this, &EqVisualizerDialog::onSaveSettings);
    btn_row->addWidget(btn_save_settings_);

    btn_close_ = new QPushButton(QStringLiteral("Close"));
    btn_close_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #2a3a4e; color: #c8d6e5; padding: 6px 16px; "
        "border: 1px solid #3a4a5e; border-radius: 4px; }"
        "QPushButton:hover { background: #3a4a5e; border-color: #5a6a7e; }"
        "QPushButton:pressed { background: #1a2a3e; border-color: #2a3a4e; "
        "padding-top: 8px; padding-bottom: 4px; }"));
    connect(btn_close_, &QPushButton::clicked, this, &QDialog::close);
    btn_row->addWidget(btn_close_);

    root->addLayout(btn_row);

    updateCurve();
    onSelectedBandChanged(0);
}

void EqVisualizerDialog::setBands(const std::array<mc1dsp::EqBandConfig, 10> &bands,
                                  int sample_rate)
{
    bands_ = bands;
    sample_rate_ = sample_rate;
    syncSlidersFromBands();
    updateCurve();
}

void EqVisualizerDialog::loadFromPipeline()
{
    if (!g_pipeline) return;

    /* Read EQ config from first active slot */
    g_pipeline->for_each_slot([this](int /*id*/, EncoderSlot &slot) {
        if (!slot.has_dsp_chain()) return;
        auto &eq = slot.dsp_chain().eq();
        sample_rate_ = 44100; /* default */
        for (int b = 0; b < 10; ++b)
            bands_[b] = eq.get_band(b);
        return; /* stop after first slot */
    });

    syncSlidersFromBands();
    updateCurve();
}

void EqVisualizerDialog::syncSlidersFromBands()
{
    for (int i = 0; i < 10; ++i) {
        QSignalBlocker blk(sld_gain_[i]);
        sld_gain_[i]->setValue(static_cast<int>(bands_[i].gain_db * 10.0f));
        lbl_gain_[i]->setText(QString("%1 dB").arg(bands_[i].gain_db, 0, 'f', 1));

        /* Update frequency label */
        float freq = bands_[i].freq_hz;
        QString freq_str = (freq >= 1000.0f)
            ? QString::number(freq / 1000.0f, 'g', 3) + QStringLiteral("k")
            : QString::number(static_cast<int>(freq));
        lbl_freq_[i]->setText(freq_str);
    }
    onSelectedBandChanged(selected_band_);
}

void EqVisualizerDialog::updateCurve()
{
    curve_->setBands(bands_, sample_rate_);
}

void EqVisualizerDialog::onPresetChanged(int index)
{
    if (index <= 0) {
        /* (Flat) — reset all bands */
        onReset();
        return;
    }

    /* Apply named preset via temporary DspEq instance */
    mc1dsp::DspEq tmp;
    tmp.set_sample_rate(sample_rate_);

    const char* presets[] = {
        "flat", "classic_rock", "country", "modern_rock", "broadcast", "spoken_word"
    };
    if (index >= 0 && index < kPresetCount)
        mc1dsp::eq_apply_preset(tmp, presets[index]);

    for (int b = 0; b < 10; ++b)
        bands_[b] = tmp.get_band(b);

    syncSlidersFromBands();
    updateCurve();
}

void EqVisualizerDialog::onBandGainChanged(int band_index)
{
    float gain = sld_gain_[band_index]->value() / 10.0f;
    bands_[band_index].gain_db = gain;
    lbl_gain_[band_index]->setText(QString("%1 dB").arg(gain, 0, 'f', 1));
    cmb_preset_->setCurrentIndex(0); /* switch to (Flat/Custom) */
    updateCurve();
}

void EqVisualizerDialog::onSelectedBandChanged(int band_index)
{
    selected_band_ = band_index;

    /* Highlight selected slider */
    for (int i = 0; i < 10; ++i) {
        lbl_freq_[i]->setStyleSheet(
            i == band_index
                ? QStringLiteral("font-size: 10px; color: #00e8bb; font-weight: bold;")
                : QStringLiteral("font-size: 10px; color: #0d9488;"));
    }

    float freq = bands_[band_index].freq_hz;
    QString freq_str = (freq >= 1000.0f)
        ? QString::number(freq / 1000.0f, 'g', 3) + QStringLiteral("k Hz")
        : QString::number(static_cast<int>(freq)) + QStringLiteral(" Hz");
    lbl_selected_band_->setText(
        QString("Band %1 (%2)").arg(band_index + 1).arg(freq_str));

    {
        QSignalBlocker b1(spin_freq_);
        spin_freq_->setValue(bands_[band_index].freq_hz);
    }
    {
        QSignalBlocker b2(spin_q_);
        spin_q_->setValue(bands_[band_index].q);
    }
    {
        QSignalBlocker b3(cmb_band_type_);
        int type_idx = cmb_band_type_->findData(static_cast<int>(bands_[band_index].type));
        if (type_idx >= 0) cmb_band_type_->setCurrentIndex(type_idx);
    }
}

void EqVisualizerDialog::onBandFreqChanged(double val)
{
    bands_[selected_band_].freq_hz = static_cast<float>(val);

    /* Update frequency label */
    float freq = static_cast<float>(val);
    QString freq_str = (freq >= 1000.0f)
        ? QString::number(freq / 1000.0f, 'g', 3) + QStringLiteral("k")
        : QString::number(static_cast<int>(freq));
    lbl_freq_[selected_band_]->setText(freq_str);

    updateCurve();
}

void EqVisualizerDialog::onBandQChanged(double val)
{
    bands_[selected_band_].q = static_cast<float>(val);
    updateCurve();
}

void EqVisualizerDialog::onApply()
{
    if (!g_pipeline) return;

    /* Push band config to all encoder slots */
    g_pipeline->for_each_slot([this](int /*id*/, EncoderSlot &slot) {
        if (!slot.has_dsp_chain()) return;
        auto &eq = slot.dsp_chain().eq();
        eq.set_enabled(true);
        for (int b = 0; b < 10; ++b)
            eq.set_band(b, bands_[b]);
    });

    emit eqConfigApplied();
}

void EqVisualizerDialog::onSaveSettings()
{
    emit saveRequested();
}

void EqVisualizerDialog::onReset()
{
    for (int i = 0; i < 10; ++i) {
        bands_[i].gain_db = 0.0f;
        bands_[i].freq_hz = kDefaultFreqs[i];
        bands_[i].q       = 1.0f;
        bands_[i].enabled = true;
        bands_[i].type    = (i == 0) ? mc1dsp::EqBandType::LOW_SHELF
                          : (i == 9) ? mc1dsp::EqBandType::HIGH_SHELF
                          : mc1dsp::EqBandType::PEAKING;
    }
    syncSlidersFromBands();
    updateCurve();
}

QString EqVisualizerDialog::currentPresetName() const
{
    return cmb_preset_ ? cmb_preset_->currentText() : QString();
}

void EqVisualizerDialog::setCurrentPresetName(const QString& name)
{
    if (!cmb_preset_ || name.isEmpty()) return;
    for (int i = 0; i < cmb_preset_->count(); ++i) {
        if (cmb_preset_->itemText(i) == name) {
            cmb_preset_->blockSignals(true);
            cmb_preset_->setCurrentIndex(i);
            cmb_preset_->blockSignals(false);
            return;
        }
    }
}

void EqVisualizerDialog::closeEvent(QCloseEvent *event)
{
    emit eqConfigApplied();
    QDialog::closeEvent(event);
}

} // namespace mc1
