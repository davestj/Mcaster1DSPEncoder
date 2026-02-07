/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * sonic_enhancer_dialog.cpp — BBE Sonic Maximizer clone UI implementation
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "sonic_enhancer_dialog.h"
#include <QCloseEvent>

#include <QCheckBox>
#include <QComboBox>
#include <QDial>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace mc1 {

QLabel *SonicEnhancerDialog::makeKnobLabel(const QString &text)
{
    auto *lbl = new QLabel(text);
    lbl->setAlignment(Qt::AlignCenter);
    QFont f = lbl->font();
    f.setBold(true);
    f.setPointSize(11);
    lbl->setFont(f);
    return lbl;
}

SonicEnhancerDialog::SonicEnhancerDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Sonic Enhancer"));
    setMinimumSize(520, 380);
    resize(580, 420);

    auto *root = new QVBoxLayout(this);

    /* Enable + Preset row */
    auto *top = new QHBoxLayout;
    chk_enable_ = new QCheckBox(QStringLiteral("Enable Sonic Enhancer"));
    connect(chk_enable_, &QCheckBox::toggled, this, &SonicEnhancerDialog::onEnableToggled);
    top->addWidget(chk_enable_);

    top->addSpacing(20);
    top->addWidget(new QLabel(QStringLiteral("Preset:")));
    cmb_preset_ = new QComboBox;
    for (int i = 0; i < mc1dsp::DspSonicEnhancer::builtin_preset_count(); ++i) {
        auto* p = &mc1dsp::DspSonicEnhancer::builtin_presets()[i];
        cmb_preset_->addItem(QString::fromUtf8(p->name));
        cmb_preset_->setItemData(i, QString::fromUtf8(p->description), Qt::ToolTipRole);
    }
    connect(cmb_preset_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SonicEnhancerDialog::onPresetChanged);
    top->addWidget(cmb_preset_);
    top->addStretch();
    root->addLayout(top);

    root->addSpacing(10);

    /* ── Three knobs ── */
    auto *knobs_box = new QGroupBox(QStringLiteral("Sonic Enhancer Controls"));
    auto *knobs_lay = new QHBoxLayout(knobs_box);
    knobs_lay->setSpacing(30);

    /* Helper to create a knob column */
    auto makeKnobCol = [&](const QString &title, const QString &tooltip,
                           int min, int max, int initial,
                           QDial *&dial, QLabel *&val_label) {
        auto *col = new QVBoxLayout;
        col->addWidget(makeKnobLabel(title));

        dial = new QDial;
        dial->setRange(min, max);
        dial->setValue(initial);
        dial->setNotchesVisible(true);
        dial->setWrapping(false);
        dial->setFixedSize(100, 100);
        dial->setToolTip(tooltip);
        col->addWidget(dial, 0, Qt::AlignCenter);

        val_label = new QLabel;
        val_label->setAlignment(Qt::AlignCenter);
        QFont vf = val_label->font();
        vf.setPointSize(14);
        vf.setBold(true);
        val_label->setFont(vf);
        col->addWidget(val_label);

        knobs_lay->addLayout(col);
    };

    /* Lo Contour: 0-100 (maps to 0.0-10.0) */
    makeKnobCol(QStringLiteral("Lo Contour"),
                QStringLiteral("Bass Definition: controls low-frequency phase alignment and bass warmth"),
                0, 100, 50, dial_lo_contour_, lbl_lo_val_);

    /* Process: 0-100 (maps to 0.0-10.0) */
    makeKnobCol(QStringLiteral("Process"),
                QStringLiteral("High Frequency Presence: controls high-frequency definition and harmonic richness"),
                0, 100, 50, dial_process_, lbl_proc_val_);

    /* Output: -120 to +60 (maps to -12.0 to +6.0 dB) */
    makeKnobCol(QStringLiteral("Output Level"),
                QStringLiteral("Output Level: makeup gain applied after enhancement"),
                -120, 60, 0, dial_output_, lbl_out_val_);

    root->addWidget(knobs_box);

    /* Connect knob signals */
    connect(dial_lo_contour_, &QDial::valueChanged, this, &SonicEnhancerDialog::onLoContourChanged);
    connect(dial_process_,    &QDial::valueChanged, this, &SonicEnhancerDialog::onProcessChanged);
    connect(dial_output_,     &QDial::valueChanged, this, &SonicEnhancerDialog::onOutputChanged);

    /* Sub-labels below knobs group */
    auto *sub_labels = new QHBoxLayout;
    auto *lbl1 = new QLabel(QStringLiteral("Bass Definition"));
    lbl1->setAlignment(Qt::AlignCenter);
    lbl1->setStyleSheet(QStringLiteral("color: #888;"));
    sub_labels->addWidget(lbl1);
    auto *lbl2 = new QLabel(QStringLiteral("HF Presence"));
    lbl2->setAlignment(Qt::AlignCenter);
    lbl2->setStyleSheet(QStringLiteral("color: #888;"));
    sub_labels->addWidget(lbl2);
    auto *lbl3 = new QLabel(QStringLiteral("Gain"));
    lbl3->setAlignment(Qt::AlignCenter);
    lbl3->setStyleSheet(QStringLiteral("color: #888;"));
    sub_labels->addWidget(lbl3);
    root->addLayout(sub_labels);

    root->addStretch();

    /* ── Button row ── */
    auto *btn_row = new QHBoxLayout;
    btn_row->addStretch();

    auto *btn_apply = new QPushButton(QStringLiteral("Apply"));
    btn_apply->setToolTip(QStringLiteral("Apply current settings to the audio pipeline"));
    btn_apply->setStyleSheet(QStringLiteral(
        "QPushButton { background: #00d4aa; color: #0a0a1e; padding: 6px 16px; "
        "border: 1px solid #00b088; border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background: #00e8bb; border-color: #00d4aa; }"
        "QPushButton:pressed { background: #009977; border-color: #008866; "
        "padding-top: 8px; padding-bottom: 4px; }"));
    connect(btn_apply, &QPushButton::clicked, this, [this]() {
        emit configChanged(config());
    });
    btn_row->addWidget(btn_apply);

    auto *btn_save = new QPushButton(QStringLiteral("Save Settings"));
    btn_save->setToolTip(QStringLiteral("Save current settings to global defaults file"));
    btn_save->setStyleSheet(QStringLiteral(
        "QPushButton { background: #1a6a4a; color: #d0f0e0; padding: 6px 16px; "
        "border: 1px solid #2a7a5a; border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background: #2a8a6a; border-color: #3a9a7a; }"
        "QPushButton:pressed { background: #0a5a3a; border-color: #1a6a4a; "
        "padding-top: 8px; padding-bottom: 4px; }"));
    connect(btn_save, &QPushButton::clicked, this, [this]() {
        emit saveRequested(config());
    });
    btn_row->addWidget(btn_save);

    auto *btn_close = new QPushButton(QStringLiteral("Close"));
    btn_close->setStyleSheet(QStringLiteral(
        "QPushButton { background: #2a3a4e; color: #c8d6e5; padding: 6px 16px; "
        "border: 1px solid #3a4a5e; border-radius: 4px; }"
        "QPushButton:hover { background: #3a4a5e; border-color: #5a6a7e; }"
        "QPushButton:pressed { background: #1a2a3e; border-color: #2a3a4e; "
        "padding-top: 8px; padding-bottom: 4px; }"));
    connect(btn_close, &QPushButton::clicked, this, &QDialog::close);
    btn_row->addWidget(btn_close);

    root->addLayout(btn_row);

    /* Set initial display values */
    onLoContourChanged(50);
    onProcessChanged(50);
    onOutputChanged(0);
}

void SonicEnhancerDialog::setConfig(const mc1dsp::SonicEnhancerConfig &cfg)
{
    chk_enable_->blockSignals(true);
    dial_lo_contour_->blockSignals(true);
    dial_process_->blockSignals(true);
    dial_output_->blockSignals(true);

    chk_enable_->setChecked(cfg.enabled);
    dial_lo_contour_->setValue(static_cast<int>(cfg.lo_contour * 10.0f));
    dial_process_->setValue(static_cast<int>(cfg.process * 10.0f));
    dial_output_->setValue(static_cast<int>(cfg.output_gain * 10.0f));

    onLoContourChanged(dial_lo_contour_->value());
    onProcessChanged(dial_process_->value());
    onOutputChanged(dial_output_->value());

    chk_enable_->blockSignals(false);
    dial_lo_contour_->blockSignals(false);
    dial_process_->blockSignals(false);
    dial_output_->blockSignals(false);
}

mc1dsp::SonicEnhancerConfig SonicEnhancerDialog::config() const
{
    mc1dsp::SonicEnhancerConfig cfg;
    cfg.enabled     = chk_enable_->isChecked();
    cfg.lo_contour  = dial_lo_contour_->value() / 10.0f;
    cfg.process     = dial_process_->value() / 10.0f;
    cfg.output_gain = dial_output_->value() / 10.0f;
    return cfg;
}

void SonicEnhancerDialog::onLoContourChanged(int value)
{
    lbl_lo_val_->setText(QString::number(value / 10.0f, 'f', 1));
    emitConfig();
}

void SonicEnhancerDialog::onProcessChanged(int value)
{
    lbl_proc_val_->setText(QString::number(value / 10.0f, 'f', 1));
    emitConfig();
}

void SonicEnhancerDialog::onOutputChanged(int value)
{
    float db = value / 10.0f;
    lbl_out_val_->setText(QString::number(db, 'f', 1) + QStringLiteral(" dB"));
    emitConfig();
}

void SonicEnhancerDialog::onEnableToggled(bool /*checked*/)
{
    emitConfig();
}

void SonicEnhancerDialog::onPresetChanged(int index)
{
    if (index < 0 || index >= mc1dsp::DspSonicEnhancer::builtin_preset_count()) return;
    const auto& preset = mc1dsp::DspSonicEnhancer::builtin_presets()[index];
    setConfig(preset.cfg);
    emitConfig();
}

void SonicEnhancerDialog::emitConfig()
{
    Q_EMIT configChanged(config());
}

QString SonicEnhancerDialog::currentPresetName() const
{
    return cmb_preset_ ? cmb_preset_->currentText() : QString();
}

void SonicEnhancerDialog::setCurrentPresetName(const QString& name)
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

void SonicEnhancerDialog::closeEvent(QCloseEvent *event)
{
    emit configChanged(config());
    QDialog::closeEvent(event);
}

} // namespace mc1
