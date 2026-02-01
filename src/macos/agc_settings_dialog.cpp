/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * agc_settings_dialog.cpp — Professional AGC audio processing dialog
 *
 * Modern Qt 6 broadcast-grade AGC settings UI with custom-painted level
 * meters, 13 broadcast presets, and full parameter control.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "agc_settings_dialog.h"
#include <QCloseEvent>
#include "dsp_presets.h"
#include "audio_pipeline.h"
#include "global_config_manager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>

namespace mc1 {

/* ═══════════════════════════════════════════════════════════════════════════
 * MeterBar — Custom-painted vertical level/GR meter
 * ═══════════════════════════════════════════════════════════════════════════ */

MeterBar::MeterBar(Style style, QWidget *parent)
    : QWidget(parent), style_(style)
{
    setMinimumSize(minimumSizeHint());
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
}

void MeterBar::setLevel(float db)
{
    level_db_ = std::clamp(db, DB_MIN, DB_MAX);
    if (level_db_ > peak_hold_db_) {
        peak_hold_db_ = level_db_;
        peak_hold_cnt_ = 40; /* hold for ~40 frames (~2s at 20fps) */
    }
    update();
}

void MeterBar::setPeakHold(float db)
{
    peak_hold_db_ = std::clamp(db, DB_MIN, DB_MAX);
    peak_hold_cnt_ = 40;
}

void MeterBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int w = width();
    const int h = height();
    const int margin = 2;
    const int bar_w = w - margin * 2;
    const int bar_h = h - margin * 2;

    /* Background */
    p.fillRect(rect(), QColor(30, 30, 36));

    /* Border */
    p.setPen(QPen(QColor(60, 60, 70), 1));
    p.drawRect(margin, margin, bar_w - 1, bar_h - 1);

    if (bar_h <= 0 || bar_w <= 0) return;

    /* Normalize level to 0.0-1.0 */
    float norm = (level_db_ - DB_MIN) / (DB_MAX - DB_MIN);
    norm = std::clamp(norm, 0.0f, 1.0f);
    int fill_h = static_cast<int>(norm * bar_h);

    if (fill_h > 0) {
        QRect fill_rect(margin + 1, margin + bar_h - fill_h, bar_w - 2, fill_h);

        if (style_ == GR_METER) {
            /* Gain Reduction: amber/orange, fills from TOP down */
            float gr_norm = norm;  /* 0 = no GR, 1 = max GR */
            int gr_fill = static_cast<int>(gr_norm * bar_h);
            QRect gr_rect(margin + 1, margin + 1, bar_w - 2, gr_fill);

            QLinearGradient grad(0, margin, 0, margin + bar_h);
            grad.setColorAt(0.0, QColor(255, 60, 30));      /* top: red (heavy GR) */
            grad.setColorAt(0.4, QColor(255, 160, 0));       /* mid: orange */
            grad.setColorAt(1.0, QColor(255, 220, 80));      /* bottom: yellow (light GR) */
            p.fillRect(gr_rect, grad);
        } else {
            /* Input/Output meter: green → yellow → red gradient, fills from BOTTOM up */
            QLinearGradient grad(0, margin + bar_h, 0, margin);
            grad.setColorAt(0.0,  QColor(0, 200, 80));       /* bottom: green */
            grad.setColorAt(0.55, QColor(0, 200, 80));       /* green zone */
            grad.setColorAt(0.70, QColor(220, 220, 0));      /* yellow zone */
            grad.setColorAt(0.85, QColor(255, 160, 0));      /* orange zone */
            grad.setColorAt(1.0,  QColor(255, 40, 30));      /* top: red (clipping) */
            p.fillRect(fill_rect, grad);
        }
    }

    /* Peak hold indicator line */
    if (style_ != GR_METER && peak_hold_cnt_ > 0) {
        float peak_norm = (peak_hold_db_ - DB_MIN) / (DB_MAX - DB_MIN);
        peak_norm = std::clamp(peak_norm, 0.0f, 1.0f);
        int peak_y = margin + bar_h - static_cast<int>(peak_norm * bar_h);
        p.setPen(QPen(QColor(255, 255, 255, 200), 2));
        p.drawLine(margin + 1, peak_y, margin + bar_w - 2, peak_y);
        peak_hold_cnt_--;
        if (peak_hold_cnt_ == 0) peak_hold_db_ = level_db_;
    }

    /* Scale markings */
    p.setPen(QColor(120, 120, 140));
    QFont sf = font();
    sf.setPixelSize(9);
    p.setFont(sf);

    const float marks[] = { 0.0f, -6.0f, -12.0f, -24.0f, -48.0f };
    for (float db : marks) {
        float mn = (db - DB_MIN) / (DB_MAX - DB_MIN);
        int y = margin + bar_h - static_cast<int>(mn * bar_h);
        p.drawLine(margin, y, margin + 3, y);
        if (w > 24) {
            p.drawText(margin + 4, y + 3, QString::number(static_cast<int>(db)));
        }
    }
}


/* ═══════════════════════════════════════════════════════════════════════════
 * AgcSettingsDialog
 * ═══════════════════════════════════════════════════════════════════════════ */

AgcSettingsDialog::AgcSettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("AGC Audio Processing"));
    setMinimumSize(680, 540);
    resize(740, 580);
    buildUi();
    populatePresets();

    /* Load saved system-wide config from YAML */
    cfg_ = GlobalConfigManager::load_dsp_defaults().agc;

    syncSlidersToConfig(cfg_);

    /* Meter refresh at 20 fps */
    meter_timer_ = new QTimer(this);
    connect(meter_timer_, &QTimer::timeout, this, &AgcSettingsDialog::onMeterTick);
    meter_timer_->start(50);
}

AgcSettingsDialog::~AgcSettingsDialog()
{
    meter_timer_->stop();
}

mc1dsp::AgcConfig AgcSettingsDialog::agcConfig() const
{
    return cfg_;
}

void AgcSettingsDialog::setAgcConfig(const mc1dsp::AgcConfig& cfg)
{
    cfg_ = cfg;
    syncSlidersToConfig(cfg_);
}

bool AgcSettingsDialog::isSystemWide() const
{
    return rb_system_wide_->isChecked();
}

void AgcSettingsDialog::setSystemWide(bool sw)
{
    rb_system_wide_->setChecked(sw);
    rb_per_encoder_->setChecked(!sw);
}

/* ─── UI Construction ───────────────────────────────────────────────────── */

void AgcSettingsDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setSpacing(8);

    /* ── Preset Bar ── */
    auto *preset_row = new QHBoxLayout;
    preset_row->addWidget(new QLabel(QStringLiteral("Preset:")));
    cmb_preset_ = new QComboBox;
    cmb_preset_->setMinimumWidth(220);
    cmb_preset_->setToolTip(QStringLiteral(
        "Select a broadcast-grade AGC preset optimized for your format.\n"
        "Each preset includes industry-standard compression, gating,\n"
        "and limiting settings."));
    preset_row->addWidget(cmb_preset_, 1);

    btn_save_preset_ = new QPushButton(QStringLiteral("Save As..."));
    btn_save_preset_->setToolTip(QStringLiteral("Save current settings as a custom preset"));
    btn_delete_preset_ = new QPushButton(QStringLiteral("Delete"));
    btn_delete_preset_->setToolTip(QStringLiteral("Delete the selected custom preset"));
    btn_delete_preset_->setEnabled(false);
    preset_row->addWidget(btn_save_preset_);
    preset_row->addWidget(btn_delete_preset_);
    root->addLayout(preset_row);

    /* ── Scope + Enable Row ── */
    auto *scope_row = new QHBoxLayout;
    rb_system_wide_ = new QRadioButton(QStringLiteral("System-Wide"));
    rb_system_wide_->setToolTip(QStringLiteral(
        "Apply these AGC settings to ALL encoder slots.\n"
        "Individual encoders use these unless they have\n"
        "their own per-encoder override."));
    rb_system_wide_->setChecked(true);

    rb_per_encoder_ = new QRadioButton(QStringLiteral("Per-Encoder Override"));
    rb_per_encoder_->setToolTip(QStringLiteral(
        "Override system-wide AGC for a specific encoder slot.\n"
        "This encoder will use its own AGC settings instead\n"
        "of the global configuration."));

    chk_enabled_ = new QCheckBox(QStringLiteral("AGC Enabled"));
    chk_enabled_->setToolTip(QStringLiteral(
        "Enable or disable the Automatic Gain Control processor.\n"
        "When disabled, audio passes through unprocessed."));
    chk_enabled_->setChecked(cfg_.enabled);

    scope_row->addWidget(rb_system_wide_);
    scope_row->addWidget(rb_per_encoder_);
    scope_row->addStretch();
    scope_row->addWidget(chk_enabled_);
    root->addLayout(scope_row);

    /* ── Separator ── */
    auto *sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    root->addWidget(sep);

    /* ── Main controls area ── */
    auto *main_area = new QHBoxLayout;
    main_area->setSpacing(12);

    /* Left column: Input + Limiter */
    auto *left_col = new QVBoxLayout;
    left_col->addWidget(buildInputSection());
    left_col->addWidget(buildLimiterSection());
    left_col->addStretch();
    main_area->addLayout(left_col);

    /* Center column: Compressor */
    main_area->addWidget(buildCompressorSection(), 1);

    /* Right column: Meters */
    main_area->addWidget(buildMeterSection());

    root->addLayout(main_area, 1);

    /* ── Button Row ── */
    auto *btn_row = new QHBoxLayout;
    btn_apply_ = new QPushButton(QStringLiteral("Apply"));
    btn_apply_->setToolTip(QStringLiteral("Apply current settings to the audio pipeline"));
    btn_apply_->setDefault(true);
    btn_apply_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #00d4aa; color: #0a0a1e; padding: 6px 16px; "
        "border: 1px solid #00b088; border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background: #00e8bb; border-color: #00d4aa; }"
        "QPushButton:pressed { background: #009977; border-color: #008866; "
        "padding-top: 8px; padding-bottom: 4px; }"));

    btn_save_settings_ = new QPushButton(QStringLiteral("Save Settings"));
    btn_save_settings_->setToolTip(QStringLiteral("Save current settings to global defaults file"));
    btn_save_settings_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #1a6a4a; color: #d0f0e0; padding: 6px 16px; "
        "border: 1px solid #2a7a5a; border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background: #2a8a6a; border-color: #3a9a7a; }"
        "QPushButton:pressed { background: #0a5a3a; border-color: #1a6a4a; "
        "padding-top: 8px; padding-bottom: 4px; }"));

    btn_reset_ = new QPushButton(QStringLiteral("Reset Defaults"));
    btn_reset_->setToolTip(QStringLiteral("Reset all parameters to the Broadcast Standard preset"));
    btn_reset_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #4a3a2a; color: #e0c8a0; padding: 6px 16px; "
        "border: 1px solid #5a4a3a; border-radius: 4px; }"
        "QPushButton:hover { background: #5a4a3a; border-color: #7a6a5a; }"
        "QPushButton:pressed { background: #3a2a1a; border-color: #4a3a2a; "
        "padding-top: 8px; padding-bottom: 4px; }"));

    btn_close_ = new QPushButton(QStringLiteral("Close"));
    btn_close_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #2a3a4e; color: #c8d6e5; padding: 6px 16px; "
        "border: 1px solid #3a4a5e; border-radius: 4px; }"
        "QPushButton:hover { background: #3a4a5e; border-color: #5a6a7e; }"
        "QPushButton:pressed { background: #1a2a3e; border-color: #2a3a4e; "
        "padding-top: 8px; padding-bottom: 4px; }"));

    btn_row->addWidget(btn_apply_);
    btn_row->addWidget(btn_save_settings_);
    btn_row->addWidget(btn_reset_);
    btn_row->addStretch();
    btn_row->addWidget(btn_close_);
    root->addLayout(btn_row);

    /* ── Connections ── */
    connect(cmb_preset_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AgcSettingsDialog::onPresetChanged);
    connect(btn_save_preset_, &QPushButton::clicked, this, &AgcSettingsDialog::onSavePreset);
    connect(btn_delete_preset_, &QPushButton::clicked, this, &AgcSettingsDialog::onDeletePreset);
    connect(btn_apply_, &QPushButton::clicked, this, &AgcSettingsDialog::onApply);
    connect(btn_save_settings_, &QPushButton::clicked, this, [this]() {
        cfg_.enabled = chk_enabled_->isChecked();
        emit saveRequested(cfg_);
    });
    connect(btn_reset_, &QPushButton::clicked, this, &AgcSettingsDialog::onReset);
    connect(btn_close_, &QPushButton::clicked, this, &QDialog::close);

    connect(chk_enabled_, &QCheckBox::toggled, this, [this](bool on) {
        cfg_.enabled = on;
    });
}

AgcSettingsDialog::SliderRow AgcSettingsDialog::makeSlider(
    const QString& label, const QString& tooltip,
    int min_val, int max_val, int initial, QWidget *parent)
{
    Q_UNUSED(parent);
    SliderRow row;
    row.slider = new QSlider(Qt::Horizontal);
    row.slider->setRange(min_val, max_val);
    row.slider->setValue(initial);
    row.slider->setToolTip(tooltip);

    row.value_label = new QLabel;
    row.value_label->setFixedWidth(64);
    row.value_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    Q_UNUSED(label);
    connect(row.slider, &QSlider::valueChanged, this, &AgcSettingsDialog::onSliderChanged);
    return row;
}

QGroupBox* AgcSettingsDialog::buildInputSection()
{
    auto *grp = new QGroupBox(QStringLiteral("Input"));
    auto *lay = new QGridLayout(grp);
    lay->setSpacing(6);

    /* Input Gain: -24 to +24 dB, slider 0-480 (0.1 dB steps) */
    auto ig = makeSlider("Input Gain", QStringLiteral(
        "Pre-compression input gain adjustment.\n"
        "Boost or cut the signal level before it enters\n"
        "the compressor. Use to drive harder or softer\n"
        "into the threshold.\n"
        "Range: -24 dB to +24 dB"), 0, 480, 240, grp);
    sld_input_gain_ = ig.slider;
    lbl_input_gain_ = ig.value_label;

    lay->addWidget(new QLabel(QStringLiteral("Input Gain")), 0, 0);
    lay->addWidget(sld_input_gain_, 0, 1);
    lay->addWidget(lbl_input_gain_, 0, 2);

    /* Gate Threshold: -80 to 0 dB, slider 0-800 (0.1 dB steps) */
    auto gt = makeSlider("Gate", QStringLiteral(
        "Noise gate threshold level.\n"
        "Audio below this level is muted to eliminate\n"
        "background noise, hiss, and hum.\n"
        "Set just above your noise floor.\n"
        "Range: -80 dB to 0 dB"), 0, 800, 200, grp);
    sld_gate_threshold_ = gt.slider;
    lbl_gate_threshold_ = gt.value_label;

    lay->addWidget(new QLabel(QStringLiteral("Gate Threshold")), 1, 0);
    lay->addWidget(sld_gate_threshold_, 1, 1);
    lay->addWidget(lbl_gate_threshold_, 1, 2);

    return grp;
}

QGroupBox* AgcSettingsDialog::buildCompressorSection()
{
    auto *grp = new QGroupBox(QStringLiteral("Compressor"));
    auto *lay = new QGridLayout(grp);
    lay->setSpacing(6);

    int row = 0;

    /* Threshold: -60 to 0 dB, slider 0-600 */
    auto th = makeSlider("Threshold", QStringLiteral(
        "Compression threshold level.\n"
        "Gain reduction begins when the signal exceeds\n"
        "this level. Lower values = more compression.\n"
        "Typical: -12 to -24 dB for broadcast.\n"
        "Range: -60 dB to 0 dB"), 0, 600, 420, grp);
    sld_threshold_ = th.slider;
    lbl_threshold_ = th.value_label;
    lay->addWidget(new QLabel(QStringLiteral("Threshold")), row, 0);
    lay->addWidget(sld_threshold_, row, 1);
    lay->addWidget(lbl_threshold_, row, 2);
    ++row;

    /* Ratio: 1.0 to 20.0, slider 10-200 (0.1 steps) */
    auto ra = makeSlider("Ratio", QStringLiteral(
        "Compression ratio (input:output).\n"
        "At 4:1, every 4 dB above threshold produces\n"
        "only 1 dB of output change.\n"
        "  1:1 = no compression\n"
        "  2-3:1 = gentle (jazz, classical)\n"
        "  4-6:1 = moderate (broadcast standard)\n"
        "  8-20:1 = heavy (limiting)\n"
        "Range: 1.0:1 to 20.0:1"), 10, 200, 40, grp);
    sld_ratio_ = ra.slider;
    lbl_ratio_ = ra.value_label;
    lay->addWidget(new QLabel(QStringLiteral("Ratio")), row, 0);
    lay->addWidget(sld_ratio_, row, 1);
    lay->addWidget(lbl_ratio_, row, 2);
    ++row;

    /* Attack: 0.1 to 100 ms, slider 1-1000 (0.1 ms steps) */
    auto at = makeSlider("Attack", QStringLiteral(
        "Compressor attack time (milliseconds).\n"
        "How fast gain reduction kicks in when the\n"
        "signal exceeds the threshold.\n"
        "  1-5 ms = fast (catches transients)\n"
        "  10-20 ms = moderate (preserves punch)\n"
        "  25-50 ms = slow (natural dynamics)\n"
        "Range: 0.1 ms to 100 ms"), 1, 1000, 100, grp);
    sld_attack_ = at.slider;
    lbl_attack_ = at.value_label;
    lay->addWidget(new QLabel(QStringLiteral("Attack")), row, 0);
    lay->addWidget(sld_attack_, row, 1);
    lay->addWidget(lbl_attack_, row, 2);
    ++row;

    /* Release: 10 to 2000 ms, slider 10-2000 */
    auto rl = makeSlider("Release", QStringLiteral(
        "Compressor release time (milliseconds).\n"
        "How fast gain recovers when signal drops below\n"
        "threshold. Too fast = pumping/breathing.\n"
        "  50-100 ms = fast (aggressive formats)\n"
        "  150-300 ms = moderate (broadcast)\n"
        "  400-1000 ms = slow (classical, jazz)\n"
        "Range: 10 ms to 2000 ms"), 10, 2000, 200, grp);
    sld_release_ = rl.slider;
    lbl_release_ = rl.value_label;
    lay->addWidget(new QLabel(QStringLiteral("Release")), row, 0);
    lay->addWidget(sld_release_, row, 1);
    lay->addWidget(lbl_release_, row, 2);
    ++row;

    /* Knee: 0 to 24 dB, slider 0-240 (0.1 dB steps) */
    auto kn = makeSlider("Knee", QStringLiteral(
        "Soft knee width (dB).\n"
        "Controls the transition zone around the threshold.\n"
        "  0 dB = hard knee (abrupt compression onset)\n"
        "  3-6 dB = moderate (broadcast standard)\n"
        "  8-12 dB = soft (transparent, musical)\n"
        "Wider knee = smoother, less audible compression.\n"
        "Range: 0 dB to 24 dB"), 0, 240, 60, grp);
    sld_knee_ = kn.slider;
    lbl_knee_ = kn.value_label;
    lay->addWidget(new QLabel(QStringLiteral("Knee")), row, 0);
    lay->addWidget(sld_knee_, row, 1);
    lay->addWidget(lbl_knee_, row, 2);
    ++row;

    /* Makeup Gain: -12 to +24 dB, slider 0-360 (0.1 dB steps) */
    auto mk = makeSlider("Makeup", QStringLiteral(
        "Post-compression makeup gain (dB).\n"
        "Restores loudness lost through compression.\n"
        "Increase to match perceived volume of the\n"
        "uncompressed signal. Auto-makeup is the gain\n"
        "reduction at threshold.\n"
        "Range: -12 dB to +24 dB"), 0, 360, 120, grp);
    sld_makeup_ = mk.slider;
    lbl_makeup_ = mk.value_label;
    lay->addWidget(new QLabel(QStringLiteral("Makeup Gain")), row, 0);
    lay->addWidget(sld_makeup_, row, 1);
    lay->addWidget(lbl_makeup_, row, 2);

    return grp;
}

QGroupBox* AgcSettingsDialog::buildLimiterSection()
{
    auto *grp = new QGroupBox(QStringLiteral("Limiter"));
    auto *lay = new QGridLayout(grp);
    lay->setSpacing(6);

    /* Limiter Ceiling: -12 to 0 dB, slider 0-120 (0.1 dB steps) */
    auto lm = makeSlider("Ceiling", QStringLiteral(
        "Brick-wall limiter ceiling (dBFS).\n"
        "Absolute maximum output level — no audio will\n"
        "ever exceed this level regardless of input.\n"
        "Prevents digital clipping in downstream codecs.\n"
        "  -0.1 to -0.5 dB = hot (competitive loudness)\n"
        "  -1.0 dB = broadcast standard\n"
        "  -2.0 to -3.0 dB = conservative (safe)\n"
        "Range: -12 dB to 0 dB"), 0, 120, 10, grp);
    sld_limiter_ = lm.slider;
    lbl_limiter_ = lm.value_label;

    lay->addWidget(new QLabel(QStringLiteral("Ceiling")), 0, 0);
    lay->addWidget(sld_limiter_, 0, 1);
    lay->addWidget(lbl_limiter_, 0, 2);

    return grp;
}

QWidget* AgcSettingsDialog::buildMeterSection()
{
    auto *container = new QWidget;
    auto *lay = new QVBoxLayout(container);
    lay->setSpacing(4);

    auto *title = new QLabel(QStringLiteral("Levels"));
    QFont tf = title->font();
    tf.setBold(true);
    title->setFont(tf);
    title->setAlignment(Qt::AlignCenter);
    lay->addWidget(title);

    auto *meters = new QHBoxLayout;
    meters->setSpacing(6);

    /* Input meter */
    auto *in_col = new QVBoxLayout;
    meter_input_ = new MeterBar(MeterBar::INPUT_METER, container);
    in_col->addWidget(meter_input_, 1);
    auto *in_lbl = new QLabel(QStringLiteral("In"));
    in_lbl->setAlignment(Qt::AlignCenter);
    in_col->addWidget(in_lbl);
    meters->addLayout(in_col);

    /* Gain Reduction meter */
    auto *gr_col = new QVBoxLayout;
    meter_gr_ = new MeterBar(MeterBar::GR_METER, container);
    gr_col->addWidget(meter_gr_, 1);
    auto *gr_lbl = new QLabel(QStringLiteral("GR"));
    gr_lbl->setAlignment(Qt::AlignCenter);
    gr_lbl->setToolTip(QStringLiteral(
        "Gain Reduction meter.\n"
        "Shows how much the compressor is reducing the\n"
        "signal level. More fill = more compression.\n"
        "Aim for 3-6 dB GR for gentle processing,\n"
        "6-12 dB for moderate, 12+ dB for heavy."));
    gr_col->addWidget(gr_lbl);
    meters->addLayout(gr_col);

    /* Output meter */
    auto *out_col = new QVBoxLayout;
    meter_output_ = new MeterBar(MeterBar::OUTPUT_METER, container);
    out_col->addWidget(meter_output_, 1);
    auto *out_lbl = new QLabel(QStringLiteral("Out"));
    out_lbl->setAlignment(Qt::AlignCenter);
    out_col->addWidget(out_lbl);
    meters->addLayout(out_col);

    lay->addLayout(meters, 1);
    return container;
}

/* ─── Presets ──────────────────────────────────────────────────────────── */

void AgcSettingsDialog::populatePresets()
{
    cmb_preset_->blockSignals(true);
    cmb_preset_->clear();

    /* Built-in presets */
    cmb_preset_->addItem(QStringLiteral("— Custom —"), QStringLiteral("__custom__"));
    for (const auto& p : agc_presets()) {
        cmb_preset_->addItem(QString::fromStdString(p.name),
                             QString::fromStdString(p.name));
    }

    /* User-saved presets from QSettings */
    QSettings s;
    int count = s.beginReadArray(QStringLiteral("agc_custom_presets"));
    for (int i = 0; i < count; ++i) {
        s.setArrayIndex(i);
        QString name = s.value("name").toString();
        if (!name.isEmpty())
            cmb_preset_->addItem(name, QStringLiteral("custom:") + name);
    }
    s.endArray();

    cmb_preset_->blockSignals(false);
}

void AgcSettingsDialog::onPresetChanged(int index)
{
    if (index < 0) return;

    QString key = cmb_preset_->itemData(index).toString();
    if (key == QStringLiteral("__custom__")) {
        btn_delete_preset_->setEnabled(false);
        return;
    }

    /* Built-in preset? */
    std::string name = cmb_preset_->itemText(index).toStdString();
    const AgcPreset* preset = agc_preset_by_name(name);
    if (preset) {
        cfg_ = preset->config;
        cfg_.enabled = chk_enabled_->isChecked();
        syncSlidersToConfig(cfg_);
        btn_delete_preset_->setEnabled(false);
        return;
    }

    /* Custom preset from QSettings */
    if (key.startsWith(QStringLiteral("custom:"))) {
        QString cname = key.mid(7);
        QSettings s;
        int count = s.beginReadArray(QStringLiteral("agc_custom_presets"));
        for (int i = 0; i < count; ++i) {
            s.setArrayIndex(i);
            if (s.value("name").toString() == cname) {
                cfg_.input_gain_db     = s.value("input_gain_db", 0.0).toFloat();
                cfg_.gate_threshold_db = s.value("gate_threshold_db", -60.0).toFloat();
                cfg_.threshold_db      = s.value("threshold_db", -18.0).toFloat();
                cfg_.ratio             = s.value("ratio", 4.0).toFloat();
                cfg_.attack_ms         = s.value("attack_ms", 10.0).toFloat();
                cfg_.release_ms        = s.value("release_ms", 200.0).toFloat();
                cfg_.knee_db           = s.value("knee_db", 6.0).toFloat();
                cfg_.makeup_gain_db    = s.value("makeup_gain_db", 0.0).toFloat();
                cfg_.limiter_db        = s.value("limiter_db", -1.0).toFloat();
                cfg_.enabled = chk_enabled_->isChecked();
                syncSlidersToConfig(cfg_);
                break;
            }
        }
        s.endArray();
        btn_delete_preset_->setEnabled(true);
    }
}

void AgcSettingsDialog::onSavePreset()
{
    /* Simple input via QMessageBox + QLineEdit would be cleaner,
       but QInputDialog is sufficient for MVP */
    QString name;
    {
        QDialog dlg(this);
        dlg.setWindowTitle(QStringLiteral("Save AGC Preset"));
        auto *lay = new QVBoxLayout(&dlg);
        lay->addWidget(new QLabel(QStringLiteral("Preset name:")));
        auto *edit = new QLineEdit(&dlg);
        lay->addWidget(edit);
        auto *btns = new QHBoxLayout;
        auto *ok = new QPushButton(QStringLiteral("Save"));
        auto *cancel = new QPushButton(QStringLiteral("Cancel"));
        btns->addWidget(ok);
        btns->addWidget(cancel);
        lay->addLayout(btns);
        connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
        connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted) return;
        name = edit->text().trimmed();
    }
    if (name.isEmpty()) return;

    /* Read existing custom presets */
    QSettings s;
    int count = s.beginReadArray(QStringLiteral("agc_custom_presets"));
    std::vector<std::pair<QString, mc1dsp::AgcConfig>> customs;
    for (int i = 0; i < count; ++i) {
        s.setArrayIndex(i);
        QString n = s.value("name").toString();
        mc1dsp::AgcConfig c;
        c.input_gain_db     = s.value("input_gain_db", 0.0).toFloat();
        c.gate_threshold_db = s.value("gate_threshold_db", -60.0).toFloat();
        c.threshold_db      = s.value("threshold_db", -18.0).toFloat();
        c.ratio             = s.value("ratio", 4.0).toFloat();
        c.attack_ms         = s.value("attack_ms", 10.0).toFloat();
        c.release_ms        = s.value("release_ms", 200.0).toFloat();
        c.knee_db           = s.value("knee_db", 6.0).toFloat();
        c.makeup_gain_db    = s.value("makeup_gain_db", 0.0).toFloat();
        c.limiter_db        = s.value("limiter_db", -1.0).toFloat();
        if (n != name) customs.emplace_back(n, c); /* replace if same name */
    }
    s.endArray();

    /* Add/replace the new preset */
    customs.emplace_back(name, cfg_);

    /* Write back */
    s.beginWriteArray(QStringLiteral("agc_custom_presets"), static_cast<int>(customs.size()));
    for (int i = 0; i < static_cast<int>(customs.size()); ++i) {
        s.setArrayIndex(i);
        s.setValue("name",              customs[i].first);
        s.setValue("input_gain_db",     static_cast<double>(customs[i].second.input_gain_db));
        s.setValue("gate_threshold_db", static_cast<double>(customs[i].second.gate_threshold_db));
        s.setValue("threshold_db",      static_cast<double>(customs[i].second.threshold_db));
        s.setValue("ratio",             static_cast<double>(customs[i].second.ratio));
        s.setValue("attack_ms",         static_cast<double>(customs[i].second.attack_ms));
        s.setValue("release_ms",        static_cast<double>(customs[i].second.release_ms));
        s.setValue("knee_db",           static_cast<double>(customs[i].second.knee_db));
        s.setValue("makeup_gain_db",    static_cast<double>(customs[i].second.makeup_gain_db));
        s.setValue("limiter_db",        static_cast<double>(customs[i].second.limiter_db));
    }
    s.endArray();

    populatePresets();
    /* Select the newly saved preset */
    for (int i = 0; i < cmb_preset_->count(); ++i) {
        if (cmb_preset_->itemText(i) == name) {
            cmb_preset_->setCurrentIndex(i);
            break;
        }
    }
}

void AgcSettingsDialog::onDeletePreset()
{
    int index = cmb_preset_->currentIndex();
    if (index < 0) return;
    QString key = cmb_preset_->itemData(index).toString();
    if (!key.startsWith(QStringLiteral("custom:"))) return;

    QString cname = key.mid(7);
    if (QMessageBox::question(this, QStringLiteral("Delete Preset"),
            QString("Delete custom preset \"%1\"?").arg(cname))
        != QMessageBox::Yes) return;

    /* Remove from QSettings */
    QSettings s;
    int count = s.beginReadArray(QStringLiteral("agc_custom_presets"));
    std::vector<std::pair<QString, mc1dsp::AgcConfig>> customs;
    for (int i = 0; i < count; ++i) {
        s.setArrayIndex(i);
        QString n = s.value("name").toString();
        if (n == cname) continue;
        mc1dsp::AgcConfig c;
        c.input_gain_db     = s.value("input_gain_db", 0.0).toFloat();
        c.gate_threshold_db = s.value("gate_threshold_db", -60.0).toFloat();
        c.threshold_db      = s.value("threshold_db", -18.0).toFloat();
        c.ratio             = s.value("ratio", 4.0).toFloat();
        c.attack_ms         = s.value("attack_ms", 10.0).toFloat();
        c.release_ms        = s.value("release_ms", 200.0).toFloat();
        c.knee_db           = s.value("knee_db", 6.0).toFloat();
        c.makeup_gain_db    = s.value("makeup_gain_db", 0.0).toFloat();
        c.limiter_db        = s.value("limiter_db", -1.0).toFloat();
        customs.emplace_back(n, c);
    }
    s.endArray();

    s.beginWriteArray(QStringLiteral("agc_custom_presets"), static_cast<int>(customs.size()));
    for (int i = 0; i < static_cast<int>(customs.size()); ++i) {
        s.setArrayIndex(i);
        s.setValue("name",              customs[i].first);
        s.setValue("input_gain_db",     static_cast<double>(customs[i].second.input_gain_db));
        s.setValue("gate_threshold_db", static_cast<double>(customs[i].second.gate_threshold_db));
        s.setValue("threshold_db",      static_cast<double>(customs[i].second.threshold_db));
        s.setValue("ratio",             static_cast<double>(customs[i].second.ratio));
        s.setValue("attack_ms",         static_cast<double>(customs[i].second.attack_ms));
        s.setValue("release_ms",        static_cast<double>(customs[i].second.release_ms));
        s.setValue("knee_db",           static_cast<double>(customs[i].second.knee_db));
        s.setValue("makeup_gain_db",    static_cast<double>(customs[i].second.makeup_gain_db));
        s.setValue("limiter_db",        static_cast<double>(customs[i].second.limiter_db));
    }
    s.endArray();

    populatePresets();
    cmb_preset_->setCurrentIndex(0); /* back to Custom */
}

/* ─── Slider ↔ Config Synchronization ─────────────────────────────────── */

void AgcSettingsDialog::syncSlidersToConfig(const mc1dsp::AgcConfig& cfg)
{
    /* Block signals to prevent feedback loop */
    sld_input_gain_->blockSignals(true);
    sld_gate_threshold_->blockSignals(true);
    sld_threshold_->blockSignals(true);
    sld_ratio_->blockSignals(true);
    sld_attack_->blockSignals(true);
    sld_release_->blockSignals(true);
    sld_knee_->blockSignals(true);
    sld_makeup_->blockSignals(true);
    sld_limiter_->blockSignals(true);

    /* Input Gain: -24 to +24 → slider 0-480 */
    sld_input_gain_->setValue(static_cast<int>((cfg.input_gain_db + 24.0f) * 10.0f));

    /* Gate: -80 to 0 → slider 0-800 */
    sld_gate_threshold_->setValue(static_cast<int>((cfg.gate_threshold_db + 80.0f) * 10.0f));

    /* Threshold: -60 to 0 → slider 0-600 */
    sld_threshold_->setValue(static_cast<int>((cfg.threshold_db + 60.0f) * 10.0f));

    /* Ratio: 1.0 to 20.0 → slider 10-200 */
    sld_ratio_->setValue(static_cast<int>(cfg.ratio * 10.0f));

    /* Attack: 0.1 to 100 → slider 1-1000 */
    sld_attack_->setValue(static_cast<int>(cfg.attack_ms * 10.0f));

    /* Release: 10 to 2000 → slider 10-2000 */
    sld_release_->setValue(static_cast<int>(cfg.release_ms));

    /* Knee: 0 to 24 → slider 0-240 */
    sld_knee_->setValue(static_cast<int>(cfg.knee_db * 10.0f));

    /* Makeup: -12 to +24 → slider 0-360 */
    sld_makeup_->setValue(static_cast<int>((cfg.makeup_gain_db + 12.0f) * 10.0f));

    /* Limiter: -12 to 0 → slider 0-120 */
    sld_limiter_->setValue(static_cast<int>((cfg.limiter_db + 12.0f) * 10.0f));

    chk_enabled_->setChecked(cfg.enabled);

    sld_input_gain_->blockSignals(false);
    sld_gate_threshold_->blockSignals(false);
    sld_threshold_->blockSignals(false);
    sld_ratio_->blockSignals(false);
    sld_attack_->blockSignals(false);
    sld_release_->blockSignals(false);
    sld_knee_->blockSignals(false);
    sld_makeup_->blockSignals(false);
    sld_limiter_->blockSignals(false);

    updateValueLabels();
}

void AgcSettingsDialog::onSliderChanged()
{
    /* Read all sliders back into config */
    cfg_.input_gain_db     = sld_input_gain_->value() / 10.0f - 24.0f;
    cfg_.gate_threshold_db = sld_gate_threshold_->value() / 10.0f - 80.0f;
    cfg_.threshold_db      = sld_threshold_->value() / 10.0f - 60.0f;
    cfg_.ratio             = sld_ratio_->value() / 10.0f;
    cfg_.attack_ms         = sld_attack_->value() / 10.0f;
    cfg_.release_ms        = static_cast<float>(sld_release_->value());
    cfg_.knee_db           = sld_knee_->value() / 10.0f;
    cfg_.makeup_gain_db    = sld_makeup_->value() / 10.0f - 12.0f;
    cfg_.limiter_db        = sld_limiter_->value() / 10.0f - 12.0f;

    updateValueLabels();

    /* Switch preset combo to "Custom" since user changed a slider */
    cmb_preset_->blockSignals(true);
    cmb_preset_->setCurrentIndex(0);
    cmb_preset_->blockSignals(false);
    btn_delete_preset_->setEnabled(false);
}

void AgcSettingsDialog::updateValueLabels()
{
    auto fmtDb = [](float db) {
        return (db >= 0.0f ? QStringLiteral("+") : QString()) +
               QString::number(db, 'f', 1) + QStringLiteral(" dB");
    };

    lbl_input_gain_->setText(fmtDb(cfg_.input_gain_db));
    lbl_gate_threshold_->setText(fmtDb(cfg_.gate_threshold_db));
    lbl_threshold_->setText(fmtDb(cfg_.threshold_db));
    lbl_ratio_->setText(QString::number(cfg_.ratio, 'f', 1) + QStringLiteral(":1"));
    lbl_attack_->setText(QString::number(cfg_.attack_ms, 'f', 1) + QStringLiteral(" ms"));
    lbl_release_->setText(QString::number(static_cast<int>(cfg_.release_ms)) + QStringLiteral(" ms"));
    lbl_knee_->setText(QString::number(cfg_.knee_db, 'f', 1) + QStringLiteral(" dB"));
    lbl_makeup_->setText(fmtDb(cfg_.makeup_gain_db));
    lbl_limiter_->setText(fmtDb(cfg_.limiter_db));
}

void AgcSettingsDialog::closeEvent(QCloseEvent *event)
{
    emit configChanged(cfg_);
    QDialog::closeEvent(event);
}

/* ─── Preset Name Persistence ──────────────────────────────────────────── */

QString AgcSettingsDialog::currentPresetName() const
{
    return cmb_preset_ ? cmb_preset_->currentText() : QString();
}

void AgcSettingsDialog::setCurrentPresetName(const QString& name)
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

/* ─── Actions ─────────────────────────────────────────────────────────── */

void AgcSettingsDialog::onApply()
{
    cfg_.enabled = chk_enabled_->isChecked();
    emit configChanged(cfg_);
}

void AgcSettingsDialog::onReset()
{
    /* Reset to Broadcast Standard preset */
    const AgcPreset* p = agc_preset_by_name("Broadcast Standard");
    if (p) {
        cfg_ = p->config;
        cfg_.enabled = chk_enabled_->isChecked();
        syncSlidersToConfig(cfg_);
        cmb_preset_->blockSignals(true);
        for (int i = 0; i < cmb_preset_->count(); ++i) {
            if (cmb_preset_->itemText(i) == QStringLiteral("Broadcast Standard")) {
                cmb_preset_->setCurrentIndex(i);
                break;
            }
        }
        cmb_preset_->blockSignals(false);
    }
}

/* ─── Meter Refresh ───────────────────────────────────────────────────── */

void AgcSettingsDialog::onMeterTick()
{
    if (!g_pipeline) {
        /* No pipeline: show demo animation */
        static float phase = 0.0f;
        phase += 0.08f;
        float in = -24.0f + 18.0f * sinf(phase);
        float gr = 3.0f + 3.0f * sinf(phase * 0.7f);
        float out = in + cfg_.makeup_gain_db - gr;

        meter_input_->setLevel(in);
        meter_gr_->setLevel(-gr);   /* GR is negative */
        meter_output_->setLevel(out);
        return;
    }

    /* Read live metering from pipeline AGC atomics */
    float in_peak = -60.0f;
    float gr = 0.0f;

    g_pipeline->for_each_slot([&](int /*id*/, EncoderSlot &slot) {
        if (!slot.has_dsp_chain()) return;
        auto &agc = slot.dsp_chain().agc();
        float ip = agc.input_peak_db();
        if (ip > in_peak) {
            in_peak = ip;
            gr = agc.gain_reduction_db();
        }
    });

    float out_peak = in_peak + cfg_.makeup_gain_db - gr;
    meter_input_->setLevel(in_peak);
    meter_gr_->setLevel(-gr);
    meter_output_->setLevel(out_peak);
}

} // namespace mc1
