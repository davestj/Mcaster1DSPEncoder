/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * eq_graphic_dialog.cpp — 31-band graphic EQ dialog implementation
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "eq_graphic_dialog.h"
#include <QCloseEvent>

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>

namespace mc1 {

static QString freqLabel(float freq)
{
    if (freq >= 1000.0f)
        return QString::number(freq / 1000.0f, 'g', 3) + QStringLiteral("k");
    if (freq == static_cast<int>(freq))
        return QString::number(static_cast<int>(freq));
    return QString::number(freq, 'g', 3);
}

EqGraphicDialog::EqGraphicDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("31-Band Graphic EQ"));
    setMinimumSize(900, 500);
    resize(1200, 580);

    auto *root = new QVBoxLayout(this);

    /* ── Top: Channel selection + Preset + Flat ── */
    auto *top_row = new QHBoxLayout;

    rb_both_  = new QRadioButton(QStringLiteral("Both"));
    rb_left_  = new QRadioButton(QStringLiteral("Left"));
    rb_right_ = new QRadioButton(QStringLiteral("Right"));
    rb_both_->setChecked(true);
    connect(rb_both_,  &QRadioButton::toggled, this, &EqGraphicDialog::onChannelChanged);
    connect(rb_left_,  &QRadioButton::toggled, this, &EqGraphicDialog::onChannelChanged);
    connect(rb_right_, &QRadioButton::toggled, this, &EqGraphicDialog::onChannelChanged);
    top_row->addWidget(rb_both_);
    top_row->addWidget(rb_left_);
    top_row->addWidget(rb_right_);

    chk_link_ = new QCheckBox(QStringLiteral("Link L+R"));
    chk_link_->setChecked(true);
    connect(chk_link_, &QCheckBox::toggled, this, &EqGraphicDialog::onLinkToggled);
    top_row->addWidget(chk_link_);

    top_row->addSpacing(20);

    top_row->addWidget(new QLabel(QStringLiteral("Preset:")));
    cmb_preset_ = new QComboBox;
    for (int i = 0; i < mc1dsp::DspEq31::builtin_preset_count(); ++i)
        cmb_preset_->addItem(QString::fromUtf8(mc1dsp::DspEq31::builtin_presets()[i].name));
    connect(cmb_preset_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EqGraphicDialog::onPresetChanged);
    top_row->addWidget(cmb_preset_);

    auto *btn_flat = new QPushButton(QStringLiteral("Flat"));
    btn_flat->setToolTip(QStringLiteral("Reset all bands to 0 dB"));
    connect(btn_flat, &QPushButton::clicked, this, &EqGraphicDialog::onFlatClicked);
    top_row->addWidget(btn_flat);

    top_row->addStretch();
    root->addLayout(top_row);

    /* ── Sliders: scrollable area with 31 vertical sliders ── */
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *slider_container = new QWidget;
    auto *grid = new QHBoxLayout(slider_container);
    grid->setSpacing(2);

    for (int b = 0; b < mc1dsp::DspEq31::NUM_BANDS; ++b) {
        auto *col = new QVBoxLayout;
        col->setSpacing(1);

        /* Gain label (top) */
        lbl_gains_[b] = new QLabel(QStringLiteral("0"));
        lbl_gains_[b]->setAlignment(Qt::AlignCenter);
        lbl_gains_[b]->setFixedWidth(36);
        QFont gf = lbl_gains_[b]->font();
        gf.setPointSize(8);
        lbl_gains_[b]->setFont(gf);
        col->addWidget(lbl_gains_[b], 0, Qt::AlignCenter);

        /* Vertical slider: -150 to +150 (maps to -15.0 to +15.0 dB) */
        sliders_[b] = new QSlider(Qt::Vertical);
        sliders_[b]->setRange(-150, 150);
        sliders_[b]->setValue(0);
        sliders_[b]->setTickPosition(QSlider::TicksBothSides);
        sliders_[b]->setTickInterval(30); /* every 3 dB */
        sliders_[b]->setMinimumHeight(200);
        sliders_[b]->setFixedWidth(28);
        sliders_[b]->setProperty("bandIndex", b);
        connect(sliders_[b], &QSlider::valueChanged,
                this, &EqGraphicDialog::onSliderChanged);
        col->addWidget(sliders_[b], 1, Qt::AlignCenter);

        /* Frequency label (bottom) */
        float freq = mc1dsp::DspEq31::CENTER_FREQS[b];
        lbl_freqs_[b] = new QLabel(freqLabel(freq));
        lbl_freqs_[b]->setAlignment(Qt::AlignCenter);
        QFont ff = lbl_freqs_[b]->font();
        ff.setPointSize(7);
        lbl_freqs_[b]->setFont(ff);
        col->addWidget(lbl_freqs_[b], 0, Qt::AlignCenter);

        grid->addLayout(col);
    }

    scroll->setWidget(slider_container);
    root->addWidget(scroll, 1);

    /* ── Button row ── */
    auto *btn_row = new QHBoxLayout;
    btn_row->addStretch();

    auto *btn_apply = new QPushButton(QStringLiteral("Apply"));
    btn_apply->setToolTip(QStringLiteral("Apply current EQ settings to the audio pipeline"));
    btn_apply->setStyleSheet(QStringLiteral(
        "QPushButton { background: #00d4aa; color: #0a0a1e; padding: 6px 16px; "
        "border: 1px solid #00b088; border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background: #00e8bb; border-color: #00d4aa; }"
        "QPushButton:pressed { background: #009977; border-color: #008866; "
        "padding-top: 8px; padding-bottom: 4px; }"));
    connect(btn_apply, &QPushButton::clicked, this, [this]() {
        emit configChanged();
    });
    btn_row->addWidget(btn_apply);

    auto *btn_save = new QPushButton(QStringLiteral("Save Settings"));
    btn_save->setToolTip(QStringLiteral("Save current EQ settings to global defaults file"));
    btn_save->setStyleSheet(QStringLiteral(
        "QPushButton { background: #1a6a4a; color: #d0f0e0; padding: 6px 16px; "
        "border: 1px solid #2a7a5a; border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background: #2a8a6a; border-color: #3a9a7a; }"
        "QPushButton:pressed { background: #0a5a3a; border-color: #1a6a4a; "
        "padding-top: 8px; padding-bottom: 4px; }"));
    connect(btn_save, &QPushButton::clicked, this, [this]() {
        emit saveRequested();
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

    /* Initialize gains to 0 */
    for (auto& ch : gains_)
        ch.fill(0.0f);
}

void EqGraphicDialog::setBandGain(int channel, int band, float gain_db)
{
    if (channel < 0 || channel > 1 || band < 0 || band >= mc1dsp::DspEq31::NUM_BANDS) return;
    gains_[channel][band] = gain_db;

    /* Update slider if this channel is currently displayed */
    int active = activeChannel();
    if (active == -1 || active == channel) {
        int val = static_cast<int>(gain_db * 10.0f);
        sliders_[band]->blockSignals(true);
        sliders_[band]->setValue(val);
        sliders_[band]->blockSignals(false);
    }
    updateSliderLabels();
}

float EqGraphicDialog::bandGain(int channel, int band) const
{
    if (channel < 0 || channel > 1 || band < 0 || band >= mc1dsp::DspEq31::NUM_BANDS) return 0.0f;
    return gains_[channel][band];
}

void EqGraphicDialog::setLinked(bool linked)
{
    chk_link_->setChecked(linked);
}

bool EqGraphicDialog::isLinked() const
{
    return chk_link_->isChecked();
}

int EqGraphicDialog::activeChannel() const
{
    if (rb_left_->isChecked())  return 0;
    if (rb_right_->isChecked()) return 1;
    return -1; /* Both */
}

void EqGraphicDialog::onSliderChanged(int value)
{
    auto *slider = qobject_cast<QSlider*>(sender());
    if (!slider) return;

    int band = slider->property("bandIndex").toInt();
    float gain_db = value / 10.0f;

    int ch = activeChannel();
    if (ch == -1) {
        /* Both channels */
        gains_[0][band] = gain_db;
        gains_[1][band] = gain_db;
    } else {
        gains_[ch][band] = gain_db;
        if (chk_link_->isChecked())
            gains_[1 - ch][band] = gain_db;
    }

    updateSliderLabels();
    Q_EMIT configChanged();
}

void EqGraphicDialog::onChannelChanged()
{
    /* Update sliders to show the selected channel's values */
    int ch = activeChannel();
    int src = (ch == -1) ? 0 : ch;

    for (int b = 0; b < mc1dsp::DspEq31::NUM_BANDS; ++b) {
        int val = static_cast<int>(gains_[src][b] * 10.0f);
        sliders_[b]->blockSignals(true);
        sliders_[b]->setValue(val);
        sliders_[b]->blockSignals(false);
    }
    updateSliderLabels();
}

void EqGraphicDialog::onPresetChanged(int index)
{
    if (index < 0 || index >= mc1dsp::DspEq31::builtin_preset_count()) return;
    const auto& preset = mc1dsp::DspEq31::builtin_presets()[index];

    for (int b = 0; b < mc1dsp::DspEq31::NUM_BANDS; ++b) {
        int ch = activeChannel();
        if (ch == -1) {
            gains_[0][b] = preset.gains[b];
            gains_[1][b] = preset.gains[b];
        } else {
            gains_[ch][b] = preset.gains[b];
            if (chk_link_->isChecked())
                gains_[1 - ch][b] = preset.gains[b];
        }
        int val = static_cast<int>(preset.gains[b] * 10.0f);
        sliders_[b]->blockSignals(true);
        sliders_[b]->setValue(val);
        sliders_[b]->blockSignals(false);
    }

    updateSliderLabels();
    Q_EMIT configChanged();
}

void EqGraphicDialog::onFlatClicked()
{
    for (int b = 0; b < mc1dsp::DspEq31::NUM_BANDS; ++b) {
        gains_[0][b] = 0.0f;
        gains_[1][b] = 0.0f;
        sliders_[b]->blockSignals(true);
        sliders_[b]->setValue(0);
        sliders_[b]->blockSignals(false);
    }
    updateSliderLabels();
    Q_EMIT configChanged();
}

void EqGraphicDialog::onLinkToggled(bool checked)
{
    if (checked) {
        /* Sync R to L */
        for (int b = 0; b < mc1dsp::DspEq31::NUM_BANDS; ++b)
            gains_[1][b] = gains_[0][b];
    }
    Q_EMIT configChanged();
}

void EqGraphicDialog::updateSliderLabels()
{
    int ch = activeChannel();
    int src = (ch == -1) ? 0 : ch;

    for (int b = 0; b < mc1dsp::DspEq31::NUM_BANDS; ++b) {
        float g = gains_[src][b];
        if (g == 0.0f)
            lbl_gains_[b]->setText(QStringLiteral("0"));
        else
            lbl_gains_[b]->setText(QString::number(g, 'f', 1));
    }
}

QString EqGraphicDialog::currentPresetName() const
{
    return cmb_preset_ ? cmb_preset_->currentText() : QString();
}

void EqGraphicDialog::setCurrentPresetName(const QString& name)
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

void EqGraphicDialog::closeEvent(QCloseEvent *event)
{
    emit configChanged();
    QDialog::closeEvent(event);
}

} // namespace mc1
