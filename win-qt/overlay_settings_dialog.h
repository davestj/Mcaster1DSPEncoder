/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * overlay_settings_dialog.h — Dialog for configuring video overlays:
 *                             text, image, lower-third, news ticker, SRT subtitles
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_OVERLAY_SETTINGS_DIALOG_H
#define MC1_OVERLAY_SETTINGS_DIALOG_H

#include <QDialog>
#include "video/overlay_renderer.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QTabWidget;

namespace mc1 {

class OverlaySettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit OverlaySettingsDialog(OverlayRenderer *renderer, QWidget *parent = nullptr);

public slots:
    /* Called when the preview widget drag/resize changes the image overlay */
    void updateImagePosition(int x, int y, int w, int h);

signals:
    void overlayChanged();

private:
    QWidget *createTextTab();
    QWidget *createImageTab();
    QWidget *createLowerThirdTab();
    QWidget *createTickerTab();
    QWidget *createSubtitlesTab();

    void applyTextOverlay();
    void applyImageOverlay();
    void applyLowerThird();
    void applyTicker();

    OverlayRenderer *renderer_ = nullptr;

    // Text tab
    QLineEdit *txt_text_ = nullptr;
    QSpinBox *spn_text_x_ = nullptr;
    QSpinBox *spn_text_y_ = nullptr;
    QComboBox *cmb_text_pos_ = nullptr;
    QSlider *sld_text_size_ = nullptr;
    QCheckBox *chk_text_enabled_ = nullptr;

    // Image tab
    QLineEdit *txt_image_path_ = nullptr;
    QSpinBox *spn_img_x_ = nullptr;
    QSpinBox *spn_img_y_ = nullptr;
    QSpinBox *spn_img_w_ = nullptr;
    QSpinBox *spn_img_h_ = nullptr;
    QSlider *sld_img_opacity_ = nullptr;
    QCheckBox *chk_img_enabled_ = nullptr;
    QLabel *lbl_img_info_ = nullptr;
    bool syncing_ = false;  // guard against recursive updates

    // Lower third tab
    QLineEdit *txt_lt_line1_ = nullptr;
    QLineEdit *txt_lt_line2_ = nullptr;
    QSpinBox *spn_lt_duration_ = nullptr;
    QCheckBox *chk_lt_enabled_ = nullptr;
    QPushButton *btn_lt_trigger_ = nullptr;

    // Ticker tab
    QLineEdit *txt_ticker_ = nullptr;
    QSlider *sld_ticker_speed_ = nullptr;
    QCheckBox *chk_ticker_enabled_ = nullptr;

    // Subtitles tab
    QLineEdit *txt_srt_path_ = nullptr;
    QCheckBox *chk_srt_enabled_ = nullptr;
    QLabel *lbl_srt_info_ = nullptr;
};

} // namespace mc1

#endif // MC1_OVERLAY_SETTINGS_DIALOG_H
