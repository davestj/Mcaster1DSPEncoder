/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * dbx_voice_dialog.cpp — DBX 286s Voice Processor settings dialog
 *
 * 3-tab horizontal rack-unit layout:
 *   Tab 1: Dynamics (Expander/Gate + Compressor side-by-side)
 *   Tab 2: De-Esser
 *   Tab 3: Enhancers (LF Enhancer + HF Detail side-by-side)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dbx_voice_dialog.h"
#include <QCloseEvent>
#include "agc_settings_dialog.h"  /* MeterBar */
#include "audio_pipeline.h"
#include "encoder_slot.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTabWidget>
#include <QVBoxLayout>

namespace mc1 {

DbxVoiceDialog::DbxVoiceDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("DBX 286s Voice Processor"));
    setMinimumSize(700, 340);
    resize(780, 400);
    buildUi();
}

/* -- Helper: create a horizontal slider row -- */
static QSlider* makeSlider(int min_val, int max_val, int initial, QWidget *parent)
{
    auto *s = new QSlider(Qt::Horizontal, parent);
    s->setRange(min_val, max_val);
    s->setValue(initial);
    s->setFixedHeight(20);
    return s;
}

void DbxVoiceDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    /* ── Preset bar ──────────────────────────────────────────── */
    auto *preset_row = new QHBoxLayout;
    preset_row->addWidget(new QLabel(QStringLiteral("Preset:")));
    cmb_preset_ = new QComboBox;
    for (int i = 0; i < mc1dsp::DspDbxVoice::kPresetCount; ++i)
        cmb_preset_->addItem(QString::fromUtf8(mc1dsp::DspDbxVoice::preset_name(i)));
    connect(cmb_preset_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DbxVoiceDialog::onPresetChanged);
    preset_row->addWidget(cmb_preset_, 1);

    chk_enabled_ = new QCheckBox(QStringLiteral("Enable DBX Voice"));
    preset_row->addWidget(chk_enabled_);
    connect(chk_enabled_, &QCheckBox::toggled, this, &DbxVoiceDialog::onSliderChanged);

    root->addLayout(preset_row);

    /* ── Tab widget with 3 tabs ──────────────────────────────── */
    tabs_ = new QTabWidget;
    tabs_->addTab(buildDynamicsTab(),  QStringLiteral("Dynamics"));
    tabs_->addTab(buildDeEsserTab(),   QStringLiteral("De-Esser"));
    tabs_->addTab(buildEnhancersTab(), QStringLiteral("Enhancers"));
    root->addWidget(tabs_, 1);

    /* ── Button bar ──────────────────────────────────────────── */
    auto *btn_row = new QHBoxLayout;
    btn_row->addStretch();

    btn_apply_ = new QPushButton(QStringLiteral("Apply"));
    btn_apply_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #00d4aa; color: #0a0a1e; padding: 6px 16px; "
        "border: 1px solid #00b088; border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background: #00e8bb; border-color: #00d4aa; }"
        "QPushButton:pressed { background: #009977; border-color: #008866; "
        "padding-top: 8px; padding-bottom: 4px; }"));
    connect(btn_apply_, &QPushButton::clicked, this, &DbxVoiceDialog::onApply);
    btn_row->addWidget(btn_apply_);

    btn_save_settings_ = new QPushButton(QStringLiteral("Save Settings"));
    btn_save_settings_->setToolTip(QStringLiteral("Save current settings to global defaults file"));
    btn_save_settings_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #1a6a4a; color: #d0f0e0; padding: 6px 16px; "
        "border: 1px solid #2a7a5a; border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background: #2a8a6a; border-color: #3a9a7a; }"
        "QPushButton:pressed { background: #0a5a3a; border-color: #1a6a4a; "
        "padding-top: 8px; padding-bottom: 4px; }"));
    connect(btn_save_settings_, &QPushButton::clicked, this, [this]() {
        cfg_.enabled = chk_enabled_->isChecked();
        emit saveRequested(cfg_);
    });
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

    /* Meter timer (50ms for smooth meters) */
    meter_timer_ = new QTimer(this);
    meter_timer_->setInterval(50);
    connect(meter_timer_, &QTimer::timeout, this, &DbxVoiceDialog::onMeterTick);
    meter_timer_->start();

    /* Connect all sliders */
    auto connectSlider = [this](QSlider *s) {
        connect(s, &QSlider::valueChanged, this, &DbxVoiceDialog::onSliderChanged);
    };

    connectSlider(sld_gate_thresh_);
    connectSlider(sld_gate_ratio_);
    connectSlider(sld_gate_attack_);
    connectSlider(sld_gate_release_);
    connectSlider(sld_comp_thresh_);
    connectSlider(sld_comp_ratio_);
    connectSlider(sld_comp_attack_);
    connectSlider(sld_comp_release_);
    connectSlider(sld_deess_freq_);
    connectSlider(sld_deess_thresh_);
    connectSlider(sld_deess_reduce_);
    connectSlider(sld_deess_q_);
    connectSlider(sld_lf_freq_);
    connectSlider(sld_lf_amount_);
    connectSlider(sld_hf_freq_);
    connectSlider(sld_hf_amount_);

    /* Connect checkboxes */
    connect(chk_gate_on_,  &QCheckBox::toggled, this, &DbxVoiceDialog::onSliderChanged);
    connect(chk_comp_on_,  &QCheckBox::toggled, this, &DbxVoiceDialog::onSliderChanged);
    connect(chk_deess_on_, &QCheckBox::toggled, this, &DbxVoiceDialog::onSliderChanged);
    connect(chk_lf_on_,    &QCheckBox::toggled, this, &DbxVoiceDialog::onSliderChanged);
    connect(chk_hf_on_,    &QCheckBox::toggled, this, &DbxVoiceDialog::onSliderChanged);
}

/* ── Tab 1: Dynamics — Gate + Compressor side-by-side + meters ───────── */

QWidget* DbxVoiceDialog::buildDynamicsTab()
{
    auto *page = new QWidget;
    auto *h = new QHBoxLayout(page);

    /* ── Gate section ──────────────────────────────── */
    auto *gate_grp = new QGroupBox(QStringLiteral("Expander / Gate"));
    auto *gate_lay = new QFormLayout(gate_grp);

    chk_gate_on_ = new QCheckBox(QStringLiteral("Enable"));
    chk_gate_on_->setChecked(true);
    gate_lay->addRow(chk_gate_on_);

    sld_gate_thresh_ = makeSlider(-80, 0, -50, gate_grp);
    lbl_gate_thresh_ = new QLabel(QStringLiteral("-50 dB"));
    auto *r1 = new QHBoxLayout; r1->addWidget(sld_gate_thresh_); r1->addWidget(lbl_gate_thresh_);
    gate_lay->addRow(QStringLiteral("Threshold:"), r1);

    sld_gate_ratio_ = makeSlider(10, 100, 20, gate_grp);
    lbl_gate_ratio_ = new QLabel(QStringLiteral("2.0:1"));
    auto *r2 = new QHBoxLayout; r2->addWidget(sld_gate_ratio_); r2->addWidget(lbl_gate_ratio_);
    gate_lay->addRow(QStringLiteral("Ratio:"), r2);

    sld_gate_attack_ = makeSlider(5, 500, 10, gate_grp);
    lbl_gate_attack_ = new QLabel(QStringLiteral("1.0 ms"));
    auto *r3 = new QHBoxLayout; r3->addWidget(sld_gate_attack_); r3->addWidget(lbl_gate_attack_);
    gate_lay->addRow(QStringLiteral("Attack:"), r3);

    sld_gate_release_ = makeSlider(10, 500, 100, gate_grp);
    lbl_gate_release_ = new QLabel(QStringLiteral("100 ms"));
    auto *r4 = new QHBoxLayout; r4->addWidget(sld_gate_release_); r4->addWidget(lbl_gate_release_);
    gate_lay->addRow(QStringLiteral("Release:"), r4);

    h->addWidget(gate_grp, 3);

    /* Gate GR meter */
    auto *gate_meter = new QVBoxLayout;
    gate_meter->addWidget(new QLabel(QStringLiteral("GR")), 0, Qt::AlignCenter);
    meter_gate_gr_ = new MeterBar(MeterBar::GR_METER);
    gate_meter->addWidget(meter_gate_gr_, 1, Qt::AlignCenter);
    h->addLayout(gate_meter);

    /* ── Compressor section ────────────────────────── */
    auto *comp_grp = new QGroupBox(QStringLiteral("Compressor"));
    auto *comp_lay = new QFormLayout(comp_grp);

    chk_comp_on_ = new QCheckBox(QStringLiteral("Enable"));
    chk_comp_on_->setChecked(true);
    comp_lay->addRow(chk_comp_on_);

    sld_comp_thresh_ = makeSlider(-60, 0, -20, comp_grp);
    lbl_comp_thresh_ = new QLabel(QStringLiteral("-20 dB"));
    auto *c1 = new QHBoxLayout; c1->addWidget(sld_comp_thresh_); c1->addWidget(lbl_comp_thresh_);
    comp_lay->addRow(QStringLiteral("Threshold:"), c1);

    sld_comp_ratio_ = makeSlider(10, 200, 30, comp_grp);
    lbl_comp_ratio_ = new QLabel(QStringLiteral("3.0:1"));
    auto *c2 = new QHBoxLayout; c2->addWidget(sld_comp_ratio_); c2->addWidget(lbl_comp_ratio_);
    comp_lay->addRow(QStringLiteral("Ratio:"), c2);

    sld_comp_attack_ = makeSlider(1, 1000, 50, comp_grp);
    lbl_comp_attack_ = new QLabel(QStringLiteral("5.0 ms"));
    auto *c3 = new QHBoxLayout; c3->addWidget(sld_comp_attack_); c3->addWidget(lbl_comp_attack_);
    comp_lay->addRow(QStringLiteral("Attack:"), c3);

    sld_comp_release_ = makeSlider(10, 1000, 150, comp_grp);
    lbl_comp_release_ = new QLabel(QStringLiteral("150 ms"));
    auto *c4 = new QHBoxLayout; c4->addWidget(sld_comp_release_); c4->addWidget(lbl_comp_release_);
    comp_lay->addRow(QStringLiteral("Release:"), c4);

    h->addWidget(comp_grp, 3);

    /* Compressor GR meter */
    auto *comp_meter = new QVBoxLayout;
    comp_meter->addWidget(new QLabel(QStringLiteral("GR")), 0, Qt::AlignCenter);
    meter_comp_gr_ = new MeterBar(MeterBar::GR_METER);
    comp_meter->addWidget(meter_comp_gr_, 1, Qt::AlignCenter);
    h->addLayout(comp_meter);

    return page;
}

/* ── Tab 2: De-Esser — controls + meter, stretched horizontally ──────── */

QWidget* DbxVoiceDialog::buildDeEsserTab()
{
    auto *page = new QWidget;
    auto *h = new QHBoxLayout(page);

    auto *de_grp = new QGroupBox(QStringLiteral("De-Esser"));
    auto *de_lay = new QFormLayout(de_grp);

    chk_deess_on_ = new QCheckBox(QStringLiteral("Enable"));
    chk_deess_on_->setChecked(true);
    de_lay->addRow(chk_deess_on_);

    sld_deess_freq_ = makeSlider(3000, 10000, 6000, de_grp);
    lbl_deess_freq_ = new QLabel(QStringLiteral("6000 Hz"));
    auto *d1 = new QHBoxLayout; d1->addWidget(sld_deess_freq_); d1->addWidget(lbl_deess_freq_);
    de_lay->addRow(QStringLiteral("Frequency:"), d1);

    sld_deess_thresh_ = makeSlider(-40, 0, -20, de_grp);
    lbl_deess_thresh_ = new QLabel(QStringLiteral("-20 dB"));
    auto *d2 = new QHBoxLayout; d2->addWidget(sld_deess_thresh_); d2->addWidget(lbl_deess_thresh_);
    de_lay->addRow(QStringLiteral("Threshold:"), d2);

    sld_deess_reduce_ = makeSlider(-20, 0, -6, de_grp);
    lbl_deess_reduce_ = new QLabel(QStringLiteral("-6 dB"));
    auto *d3 = new QHBoxLayout; d3->addWidget(sld_deess_reduce_); d3->addWidget(lbl_deess_reduce_);
    de_lay->addRow(QStringLiteral("Max Reduction:"), d3);

    sld_deess_q_ = makeSlider(5, 60, 20, de_grp);
    lbl_deess_q_ = new QLabel(QStringLiteral("2.0"));
    auto *d4 = new QHBoxLayout; d4->addWidget(sld_deess_q_); d4->addWidget(lbl_deess_q_);
    de_lay->addRow(QStringLiteral("Bandwidth Q:"), d4);

    h->addWidget(de_grp, 5);

    /* De-Ess GR meter */
    auto *de_meter = new QVBoxLayout;
    de_meter->addWidget(new QLabel(QStringLiteral("GR")), 0, Qt::AlignCenter);
    meter_deess_gr_ = new MeterBar(MeterBar::GR_METER);
    de_meter->addWidget(meter_deess_gr_, 1, Qt::AlignCenter);
    h->addLayout(de_meter);

    return page;
}

/* ── Tab 3: Enhancers — LF + HF side-by-side ────────────────────────── */

QWidget* DbxVoiceDialog::buildEnhancersTab()
{
    auto *page = new QWidget;
    auto *h = new QHBoxLayout(page);

    /* ── LF Enhancer ───────────────────────────────── */
    auto *lf_grp = new QGroupBox(QStringLiteral("LF Enhancer"));
    auto *lf_lay = new QFormLayout(lf_grp);

    chk_lf_on_ = new QCheckBox(QStringLiteral("Enable"));
    chk_lf_on_->setChecked(true);
    lf_lay->addRow(chk_lf_on_);

    sld_lf_freq_ = makeSlider(50, 250, 120, lf_grp);
    lbl_lf_freq_ = new QLabel(QStringLiteral("120 Hz"));
    auto *l1 = new QHBoxLayout; l1->addWidget(sld_lf_freq_); l1->addWidget(lbl_lf_freq_);
    lf_lay->addRow(QStringLiteral("Frequency:"), l1);

    sld_lf_amount_ = makeSlider(0, 120, 30, lf_grp);
    lbl_lf_amount_ = new QLabel(QStringLiteral("3.0 dB"));
    auto *l2 = new QHBoxLayout; l2->addWidget(sld_lf_amount_); l2->addWidget(lbl_lf_amount_);
    lf_lay->addRow(QStringLiteral("Amount:"), l2);

    h->addWidget(lf_grp, 1);

    /* ── HF Detail ─────────────────────────────────── */
    auto *hf_grp = new QGroupBox(QStringLiteral("HF Detail"));
    auto *hf_lay = new QFormLayout(hf_grp);

    chk_hf_on_ = new QCheckBox(QStringLiteral("Enable"));
    chk_hf_on_->setChecked(true);
    hf_lay->addRow(chk_hf_on_);

    sld_hf_freq_ = makeSlider(2000, 8000, 4000, hf_grp);
    lbl_hf_freq_ = new QLabel(QStringLiteral("4000 Hz"));
    auto *h1 = new QHBoxLayout; h1->addWidget(sld_hf_freq_); h1->addWidget(lbl_hf_freq_);
    hf_lay->addRow(QStringLiteral("Frequency:"), h1);

    sld_hf_amount_ = makeSlider(0, 120, 30, hf_grp);
    lbl_hf_amount_ = new QLabel(QStringLiteral("3.0 dB"));
    auto *h2 = new QHBoxLayout; h2->addWidget(sld_hf_amount_); h2->addWidget(lbl_hf_amount_);
    hf_lay->addRow(QStringLiteral("Amount:"), h2);

    h->addWidget(hf_grp, 1);

    return page;
}

/* ── Config read-back ──────────────────────────────────────────────────── */

mc1dsp::DbxVoiceConfig DbxVoiceDialog::dbxConfig() const
{
    mc1dsp::DbxVoiceConfig c;
    c.enabled = chk_enabled_->isChecked();

    c.gate.enabled      = chk_gate_on_->isChecked();
    c.gate.threshold_db = static_cast<float>(sld_gate_thresh_->value());
    c.gate.ratio        = static_cast<float>(sld_gate_ratio_->value()) / 10.0f;
    c.gate.attack_ms    = static_cast<float>(sld_gate_attack_->value()) / 10.0f;
    c.gate.release_ms   = static_cast<float>(sld_gate_release_->value());

    c.compressor.enabled      = chk_comp_on_->isChecked();
    c.compressor.threshold_db = static_cast<float>(sld_comp_thresh_->value());
    c.compressor.ratio        = static_cast<float>(sld_comp_ratio_->value()) / 10.0f;
    c.compressor.attack_ms    = static_cast<float>(sld_comp_attack_->value()) / 10.0f;
    c.compressor.release_ms   = static_cast<float>(sld_comp_release_->value());

    c.deesser.enabled      = chk_deess_on_->isChecked();
    c.deesser.frequency_hz = static_cast<float>(sld_deess_freq_->value());
    c.deesser.threshold_db = static_cast<float>(sld_deess_thresh_->value());
    c.deesser.reduction_db = static_cast<float>(sld_deess_reduce_->value());
    c.deesser.bandwidth_q  = static_cast<float>(sld_deess_q_->value()) / 10.0f;

    c.lf_enhancer.enabled      = chk_lf_on_->isChecked();
    c.lf_enhancer.frequency_hz = static_cast<float>(sld_lf_freq_->value());
    c.lf_enhancer.amount_db    = static_cast<float>(sld_lf_amount_->value()) / 10.0f;

    c.hf_detail.enabled      = chk_hf_on_->isChecked();
    c.hf_detail.frequency_hz = static_cast<float>(sld_hf_freq_->value());
    c.hf_detail.amount_db    = static_cast<float>(sld_hf_amount_->value()) / 10.0f;

    return c;
}

void DbxVoiceDialog::setDbxConfig(const mc1dsp::DbxVoiceConfig& cfg)
{
    cfg_ = cfg;
    syncSlidersToConfig(cfg);
}

void DbxVoiceDialog::syncSlidersToConfig(const mc1dsp::DbxVoiceConfig& cfg)
{
    chk_enabled_->setChecked(cfg.enabled);

    chk_gate_on_->setChecked(cfg.gate.enabled);
    sld_gate_thresh_->setValue(static_cast<int>(cfg.gate.threshold_db));
    sld_gate_ratio_->setValue(static_cast<int>(cfg.gate.ratio * 10.0f));
    sld_gate_attack_->setValue(static_cast<int>(cfg.gate.attack_ms * 10.0f));
    sld_gate_release_->setValue(static_cast<int>(cfg.gate.release_ms));

    chk_comp_on_->setChecked(cfg.compressor.enabled);
    sld_comp_thresh_->setValue(static_cast<int>(cfg.compressor.threshold_db));
    sld_comp_ratio_->setValue(static_cast<int>(cfg.compressor.ratio * 10.0f));
    sld_comp_attack_->setValue(static_cast<int>(cfg.compressor.attack_ms * 10.0f));
    sld_comp_release_->setValue(static_cast<int>(cfg.compressor.release_ms));

    chk_deess_on_->setChecked(cfg.deesser.enabled);
    sld_deess_freq_->setValue(static_cast<int>(cfg.deesser.frequency_hz));
    sld_deess_thresh_->setValue(static_cast<int>(cfg.deesser.threshold_db));
    sld_deess_reduce_->setValue(static_cast<int>(cfg.deesser.reduction_db));
    sld_deess_q_->setValue(static_cast<int>(cfg.deesser.bandwidth_q * 10.0f));

    chk_lf_on_->setChecked(cfg.lf_enhancer.enabled);
    sld_lf_freq_->setValue(static_cast<int>(cfg.lf_enhancer.frequency_hz));
    sld_lf_amount_->setValue(static_cast<int>(cfg.lf_enhancer.amount_db * 10.0f));

    chk_hf_on_->setChecked(cfg.hf_detail.enabled);
    sld_hf_freq_->setValue(static_cast<int>(cfg.hf_detail.frequency_hz));
    sld_hf_amount_->setValue(static_cast<int>(cfg.hf_detail.amount_db * 10.0f));

    updateValueLabels();
}

void DbxVoiceDialog::updateValueLabels()
{
    lbl_gate_thresh_->setText(QString("%1 dB").arg(sld_gate_thresh_->value()));
    lbl_gate_ratio_->setText(QString("%1:1").arg(sld_gate_ratio_->value() / 10.0, 0, 'f', 1));
    lbl_gate_attack_->setText(QString("%1 ms").arg(sld_gate_attack_->value() / 10.0, 0, 'f', 1));
    lbl_gate_release_->setText(QString("%1 ms").arg(sld_gate_release_->value()));

    lbl_comp_thresh_->setText(QString("%1 dB").arg(sld_comp_thresh_->value()));
    lbl_comp_ratio_->setText(QString("%1:1").arg(sld_comp_ratio_->value() / 10.0, 0, 'f', 1));
    lbl_comp_attack_->setText(QString("%1 ms").arg(sld_comp_attack_->value() / 10.0, 0, 'f', 1));
    lbl_comp_release_->setText(QString("%1 ms").arg(sld_comp_release_->value()));

    lbl_deess_freq_->setText(QString("%1 Hz").arg(sld_deess_freq_->value()));
    lbl_deess_thresh_->setText(QString("%1 dB").arg(sld_deess_thresh_->value()));
    lbl_deess_reduce_->setText(QString("%1 dB").arg(sld_deess_reduce_->value()));
    lbl_deess_q_->setText(QString("%1").arg(sld_deess_q_->value() / 10.0, 0, 'f', 1));

    lbl_lf_freq_->setText(QString("%1 Hz").arg(sld_lf_freq_->value()));
    lbl_lf_amount_->setText(QString("%1 dB").arg(sld_lf_amount_->value() / 10.0, 0, 'f', 1));

    lbl_hf_freq_->setText(QString("%1 Hz").arg(sld_hf_freq_->value()));
    lbl_hf_amount_->setText(QString("%1 dB").arg(sld_hf_amount_->value() / 10.0, 0, 'f', 1));
}

void DbxVoiceDialog::onPresetChanged(int index)
{
    if (index < 0 || index >= mc1dsp::DspDbxVoice::kPresetCount) return;
    auto cfg = mc1dsp::DspDbxVoice::preset_config(index);
    cfg.enabled = chk_enabled_->isChecked();  /* preserve enable state */
    syncSlidersToConfig(cfg);
}

void DbxVoiceDialog::onSliderChanged()
{
    updateValueLabels();
}

void DbxVoiceDialog::onApply()
{
    cfg_ = dbxConfig();
    emit configChanged(cfg_);
}

void DbxVoiceDialog::onMeterTick()
{
    if (!g_pipeline) {
        meter_gate_gr_->setLevel(0.0f);
        meter_comp_gr_->setLevel(0.0f);
        meter_deess_gr_->setLevel(0.0f);
        return;
    }

    float gate_gr = 0.0f, comp_gr = 0.0f, deess_gr = 0.0f;
    g_pipeline->for_each_slot([&](int /*id*/, EncoderSlot &slot) {
        if (!slot.has_dsp_chain()) return;
        auto &dbx = slot.dsp_chain().dbx_voice();
        if (!dbx.is_enabled()) return;
        gate_gr  = std::max(gate_gr,  dbx.gate_reduction_db());
        comp_gr  = std::max(comp_gr,  dbx.comp_reduction_db());
        deess_gr = std::max(deess_gr, dbx.deess_reduction_db());
    });

    meter_gate_gr_->setLevel(-gate_gr);
    meter_comp_gr_->setLevel(-comp_gr);
    meter_deess_gr_->setLevel(-deess_gr);
}

QString DbxVoiceDialog::currentPresetName() const
{
    return cmb_preset_ ? cmb_preset_->currentText() : QString();
}

void DbxVoiceDialog::setCurrentPresetName(const QString& name)
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

void DbxVoiceDialog::closeEvent(QCloseEvent *event)
{
    emit configChanged(dbxConfig());
    QDialog::closeEvent(event);
}

} // namespace mc1
