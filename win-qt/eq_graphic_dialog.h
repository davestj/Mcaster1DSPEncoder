/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * eq_graphic_dialog.h — 31-band graphic EQ dialog with dual-channel control
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_EQ_GRAPHIC_DIALOG_H
#define MC1_EQ_GRAPHIC_DIALOG_H

#include <QDialog>
#include <array>
#include "dsp/eq31.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QRadioButton;
class QSlider;

namespace mc1 {

class EqGraphicDialog : public QDialog {
    Q_OBJECT

public:
    explicit EqGraphicDialog(QWidget *parent = nullptr);

    /* Set/get current band gains (per channel) */
    void setBandGain(int channel, int band, float gain_db);
    float bandGain(int channel, int band) const;
    void setLinked(bool linked);
    bool isLinked() const;

    QString currentPresetName() const;
    void setCurrentPresetName(const QString& name);

Q_SIGNALS:
    void configChanged();
    void saveRequested();

protected:
    void closeEvent(QCloseEvent *event) override;

private Q_SLOTS:
    void onSliderChanged(int value);
    void onChannelChanged();
    void onPresetChanged(int index);
    void onFlatClicked();
    void onLinkToggled(bool checked);

private:
    void updateSliderLabels();
    int  activeChannel() const;

    /* 31 slider columns */
    std::array<QSlider*, mc1dsp::DspEq31::NUM_BANDS> sliders_{};
    std::array<QLabel*,  mc1dsp::DspEq31::NUM_BANDS> lbl_gains_{};
    std::array<QLabel*,  mc1dsp::DspEq31::NUM_BANDS> lbl_freqs_{};

    /* Per-channel gain storage */
    std::array<std::array<float, mc1dsp::DspEq31::NUM_BANDS>, 2> gains_{};

    /* Channel selection */
    QRadioButton *rb_both_  = nullptr;
    QRadioButton *rb_left_  = nullptr;
    QRadioButton *rb_right_ = nullptr;
    QCheckBox    *chk_link_ = nullptr;

    /* Preset */
    QComboBox *cmb_preset_ = nullptr;
};

} // namespace mc1

#endif // MC1_EQ_GRAPHIC_DIALOG_H
