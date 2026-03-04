/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * overlay_settings_dialog.cpp — Dialog for configuring video overlays
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "overlay_settings_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace mc1 {

// ===================================================================
// Stylesheet constants
// ===================================================================

static const char* kDialogStyle = R"(
    QDialog {
        background: #0f1724;
        color: #c8d6e5;
    }
    QLabel {
        color: #c8d6e5;
    }
    QTabWidget::pane {
        border: 1px solid #2a3a4e;
        background: #1a2332;
    }
    QTabBar::tab {
        background: #1a2332;
        color: #7a8ba0;
        padding: 8px 16px;
        border: 1px solid #2a3a4e;
        border-bottom: none;
    }
    QTabBar::tab:selected {
        background: #2a3a4e;
        color: #00d4aa;
    }
    QTabBar::tab:hover:!selected {
        background: #222e3e;
    }
    QLineEdit, QSpinBox {
        background: #1a2332;
        color: #c8d6e5;
        border: 1px solid #2a3a4e;
        border-radius: 3px;
        padding: 4px;
    }
    QLineEdit:focus, QSpinBox:focus {
        border-color: #00d4aa;
    }
    QCheckBox {
        color: #c8d6e5;
    }
    QCheckBox::indicator {
        width: 14px; height: 14px;
        border: 1px solid #2a3a4e;
        border-radius: 3px;
        background: #1a2332;
    }
    QCheckBox::indicator:checked {
        background: #00d4aa;
        border-color: #00d4aa;
    }
    QSlider::groove:horizontal {
        background: #2a3a4e;
        height: 4px;
        border-radius: 2px;
    }
    QSlider::handle:horizontal {
        background: #00d4aa;
        width: 14px;
        margin: -5px 0;
        border-radius: 7px;
    }
    QSlider::sub-page:horizontal {
        background: #00d4aa;
        border-radius: 2px;
    }
    QPushButton {
        background: #2a3a4e;
        color: #c8d6e5;
        border: 1px solid #3a4a5e;
        border-radius: 4px;
        padding: 6px 12px;
    }
    QPushButton:hover {
        background: #3a4a5e;
    }
    QPushButton:pressed {
        background: #1a2a3e;
    }
    QComboBox {
        background: #1a2332;
        color: #c8d6e5;
        border: 1px solid #2a3a4e;
        border-radius: 3px;
        padding: 4px 8px;
    }
    QComboBox::drop-down {
        border: none;
    }
    QComboBox QAbstractItemView {
        background: #1a2332;
        color: #c8d6e5;
        selection-background-color: #2a3a4e;
    }
)";

// ===================================================================
// Constructor
// ===================================================================

OverlaySettingsDialog::OverlaySettingsDialog(OverlayRenderer *renderer, QWidget *parent)
    : QDialog(parent)
    , renderer_(renderer)
{
    setWindowTitle("Overlay Settings");
    resize(480, 500);
    setStyleSheet(kDialogStyle);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // --- Title ---
    auto *lbl_title = new QLabel("Overlay Settings");
    lbl_title->setStyleSheet("font-weight: bold; font-size: 14px; color: #00d4aa;");
    root->addWidget(lbl_title);

    // --- Tab widget ---
    auto *tabs = new QTabWidget;
    tabs->addTab(createTextTab(),       "Text");
    tabs->addTab(createImageTab(),      "Image");
    tabs->addTab(createLowerThirdTab(), "Lower Third");
    tabs->addTab(createTickerTab(),     "Ticker");
    tabs->addTab(createSubtitlesTab(),  "Subtitles");
    root->addWidget(tabs, 1);

    // --- Close button ---
    auto *btn_row = new QHBoxLayout;
    btn_row->addStretch();
    auto *btn_close = new QPushButton("Close");
    connect(btn_close, &QPushButton::clicked, this, &QDialog::accept);
    btn_row->addWidget(btn_close);
    root->addLayout(btn_row);
}

// ===================================================================
// Text Tab
// ===================================================================

QWidget *OverlaySettingsDialog::createTextTab()
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(8);

    // Enabled
    chk_text_enabled_ = new QCheckBox("Enabled");
    if (renderer_)
        chk_text_enabled_->setChecked(renderer_->text_overlay().enabled);
    connect(chk_text_enabled_, &QCheckBox::toggled, this, [this](bool) { applyTextOverlay(); });
    lay->addWidget(chk_text_enabled_);

    // Text input
    lay->addWidget(new QLabel("Text:"));
    txt_text_ = new QLineEdit;
    if (renderer_)
        txt_text_->setText(QString::fromStdString(renderer_->text_overlay().text));
    txt_text_->setPlaceholderText("Overlay text...");
    connect(txt_text_, &QLineEdit::textChanged, this, [this](const QString&) { applyTextOverlay(); });
    lay->addWidget(txt_text_);

    // Position combo
    lay->addWidget(new QLabel("Position:"));
    cmb_text_pos_ = new QComboBox;
    cmb_text_pos_->addItem("Top-Left");
    cmb_text_pos_->addItem("Top-Right");
    cmb_text_pos_->addItem("Bottom-Left");
    cmb_text_pos_->addItem("Bottom-Right");
    cmb_text_pos_->addItem("Custom");
    connect(cmb_text_pos_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        bool custom = (idx == 4);
        spn_text_x_->setEnabled(custom);
        spn_text_y_->setEnabled(custom);
        applyTextOverlay();
    });
    lay->addWidget(cmb_text_pos_);

    // Custom X/Y
    auto *xy_row = new QHBoxLayout;
    xy_row->addWidget(new QLabel("X:"));
    spn_text_x_ = new QSpinBox;
    spn_text_x_->setRange(0, 1920);
    spn_text_x_->setValue(renderer_ ? renderer_->text_overlay().x : 10);
    spn_text_x_->setEnabled(false);
    connect(spn_text_x_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { applyTextOverlay(); });
    xy_row->addWidget(spn_text_x_);

    xy_row->addWidget(new QLabel("Y:"));
    spn_text_y_ = new QSpinBox;
    spn_text_y_->setRange(0, 1080);
    spn_text_y_->setValue(renderer_ ? renderer_->text_overlay().y : 10);
    spn_text_y_->setEnabled(false);
    connect(spn_text_y_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { applyTextOverlay(); });
    xy_row->addWidget(spn_text_y_);
    lay->addLayout(xy_row);

    // Font size slider
    auto *size_row = new QHBoxLayout;
    size_row->addWidget(new QLabel("Font Size:"));
    sld_text_size_ = new QSlider(Qt::Horizontal);
    sld_text_size_->setRange(1, 4);
    sld_text_size_->setValue(renderer_ ? renderer_->text_overlay().font_size : 2);
    connect(sld_text_size_, &QSlider::valueChanged, this, [this](int) { applyTextOverlay(); });
    size_row->addWidget(sld_text_size_);
    auto *lbl_size_val = new QLabel(QString::number(sld_text_size_->value()));
    lbl_size_val->setFixedWidth(20);
    lbl_size_val->setAlignment(Qt::AlignRight);
    connect(sld_text_size_, &QSlider::valueChanged, lbl_size_val,
            [lbl_size_val](int v) { lbl_size_val->setText(QString::number(v)); });
    size_row->addWidget(lbl_size_val);
    lay->addLayout(size_row);

    lay->addStretch();
    return page;
}

// ===================================================================
// Image Tab
// ===================================================================

QWidget *OverlaySettingsDialog::createImageTab()
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(8);

    // Enabled
    chk_img_enabled_ = new QCheckBox("Enabled");
    if (renderer_)
        chk_img_enabled_->setChecked(renderer_->image_overlay().enabled);
    connect(chk_img_enabled_, &QCheckBox::toggled, this, [this](bool) { applyImageOverlay(); });
    lay->addWidget(chk_img_enabled_);

    // Image file path + browse
    lay->addWidget(new QLabel("Image File:"));
    auto *path_row = new QHBoxLayout;
    txt_image_path_ = new QLineEdit;
    if (renderer_)
        txt_image_path_->setText(QString::fromStdString(renderer_->image_overlay().file_path));
    txt_image_path_->setPlaceholderText("/path/to/logo.png");
    path_row->addWidget(txt_image_path_, 1);

    auto *btn_browse = new QPushButton("Browse...");
    connect(btn_browse, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(
            this, "Select Image", QString(),
            "Images (*.png *.jpg *.jpeg *.bmp);;All Files (*)");
        if (!path.isEmpty()) {
            txt_image_path_->setText(path);
            applyImageOverlay();
        }
    });
    path_row->addWidget(btn_browse);
    lay->addLayout(path_row);

    // Info label (shows loaded image dimensions)
    lbl_img_info_ = new QLabel("No image loaded.");
    lbl_img_info_->setStyleSheet("color: #7a8ba0; font-size: 11px;");
    lay->addWidget(lbl_img_info_);

    // Position X / Y
    auto *pos_row = new QHBoxLayout;
    pos_row->addWidget(new QLabel("X:"));
    spn_img_x_ = new QSpinBox;
    spn_img_x_->setRange(-3840, 3840);
    spn_img_x_->setValue(renderer_ ? renderer_->image_overlay().x : 0);
    connect(spn_img_x_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { if (!syncing_) applyImageOverlay(); });
    pos_row->addWidget(spn_img_x_);

    pos_row->addWidget(new QLabel("Y:"));
    spn_img_y_ = new QSpinBox;
    spn_img_y_->setRange(-2160, 2160);
    spn_img_y_->setValue(renderer_ ? renderer_->image_overlay().y : 0);
    connect(spn_img_y_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { if (!syncing_) applyImageOverlay(); });
    pos_row->addWidget(spn_img_y_);
    lay->addLayout(pos_row);

    // Size W / H
    auto *size_row = new QHBoxLayout;
    size_row->addWidget(new QLabel("W:"));
    spn_img_w_ = new QSpinBox;
    spn_img_w_->setRange(0, 3840);
    spn_img_w_->setSpecialValueText("auto");
    spn_img_w_->setValue(renderer_ ? renderer_->image_overlay().render_width : 0);
    connect(spn_img_w_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { if (!syncing_) applyImageOverlay(); });
    size_row->addWidget(spn_img_w_);

    size_row->addWidget(new QLabel("H:"));
    spn_img_h_ = new QSpinBox;
    spn_img_h_->setRange(0, 2160);
    spn_img_h_->setSpecialValueText("auto");
    spn_img_h_->setValue(renderer_ ? renderer_->image_overlay().render_height : 0);
    connect(spn_img_h_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { if (!syncing_) applyImageOverlay(); });
    size_row->addWidget(spn_img_h_);
    lay->addLayout(size_row);

    auto *hint_label = new QLabel("Tip: Drag the image on the preview to reposition.\n"
                                   "Drag handles to resize. Set W/H to 0 for original size.");
    hint_label->setStyleSheet("color: #5a6b80; font-size: 10px;");
    hint_label->setWordWrap(true);
    lay->addWidget(hint_label);

    // Opacity slider
    auto *opacity_row = new QHBoxLayout;
    opacity_row->addWidget(new QLabel("Opacity:"));
    sld_img_opacity_ = new QSlider(Qt::Horizontal);
    sld_img_opacity_->setRange(0, 100);
    int init_opacity = renderer_ ? static_cast<int>(renderer_->image_overlay().opacity * 100.0f) : 100;
    sld_img_opacity_->setValue(init_opacity);
    connect(sld_img_opacity_, &QSlider::valueChanged, this, [this](int) { applyImageOverlay(); });
    opacity_row->addWidget(sld_img_opacity_);

    auto *lbl_opacity = new QLabel(QString::number(init_opacity) + "%");
    lbl_opacity->setFixedWidth(40);
    lbl_opacity->setAlignment(Qt::AlignRight);
    connect(sld_img_opacity_, &QSlider::valueChanged, lbl_opacity,
            [lbl_opacity](int v) { lbl_opacity->setText(QString::number(v) + "%"); });
    opacity_row->addWidget(lbl_opacity);
    lay->addLayout(opacity_row);

    lay->addStretch();
    return page;
}

// ===================================================================
// Lower Third Tab
// ===================================================================

QWidget *OverlaySettingsDialog::createLowerThirdTab()
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(8);

    // Enabled
    chk_lt_enabled_ = new QCheckBox("Enabled");
    if (renderer_)
        chk_lt_enabled_->setChecked(renderer_->lower_third().enabled);
    connect(chk_lt_enabled_, &QCheckBox::toggled, this, [this](bool) { applyLowerThird(); });
    lay->addWidget(chk_lt_enabled_);

    // Line 1
    lay->addWidget(new QLabel("Line 1 (Name):"));
    txt_lt_line1_ = new QLineEdit;
    if (renderer_)
        txt_lt_line1_->setText(QString::fromStdString(renderer_->lower_third().line1));
    txt_lt_line1_->setPlaceholderText("Guest name or title");
    connect(txt_lt_line1_, &QLineEdit::textChanged, this, [this](const QString&) { applyLowerThird(); });
    lay->addWidget(txt_lt_line1_);

    // Line 2
    lay->addWidget(new QLabel("Line 2 (Subtitle):"));
    txt_lt_line2_ = new QLineEdit;
    if (renderer_)
        txt_lt_line2_->setText(QString::fromStdString(renderer_->lower_third().line2));
    txt_lt_line2_->setPlaceholderText("Position or description");
    connect(txt_lt_line2_, &QLineEdit::textChanged, this, [this](const QString&) { applyLowerThird(); });
    lay->addWidget(txt_lt_line2_);

    // Duration
    auto *dur_row = new QHBoxLayout;
    dur_row->addWidget(new QLabel("Duration (sec):"));
    spn_lt_duration_ = new QSpinBox;
    spn_lt_duration_->setRange(1, 30);
    int init_dur = renderer_ ? renderer_->lower_third().duration_ms / 1000 : 5;
    spn_lt_duration_->setValue(init_dur);
    connect(spn_lt_duration_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { applyLowerThird(); });
    dur_row->addWidget(spn_lt_duration_);
    dur_row->addStretch();
    lay->addLayout(dur_row);

    // Trigger button
    btn_lt_trigger_ = new QPushButton("Show Now");
    btn_lt_trigger_->setStyleSheet(
        "QPushButton { background: #00d4aa; color: #0f1724; font-weight: bold;"
        " border: none; border-radius: 4px; padding: 8px 16px; }"
        "QPushButton:hover { background: #00eabb; }"
        "QPushButton:pressed { background: #00b896; }");
    connect(btn_lt_trigger_, &QPushButton::clicked, this, [this]() {
        applyLowerThird();
        emit overlayChanged();
    });
    lay->addWidget(btn_lt_trigger_);

    lay->addStretch();
    return page;
}

// ===================================================================
// Ticker Tab
// ===================================================================

QWidget *OverlaySettingsDialog::createTickerTab()
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(8);

    // Enabled
    chk_ticker_enabled_ = new QCheckBox("Enabled");
    if (renderer_)
        chk_ticker_enabled_->setChecked(renderer_->news_ticker().enabled);
    connect(chk_ticker_enabled_, &QCheckBox::toggled, this, [this](bool) { applyTicker(); });
    lay->addWidget(chk_ticker_enabled_);

    // Ticker text
    lay->addWidget(new QLabel("Ticker Text:"));
    txt_ticker_ = new QLineEdit;
    if (renderer_)
        txt_ticker_->setText(QString::fromStdString(renderer_->news_ticker().text));
    txt_ticker_->setPlaceholderText("Breaking news text that scrolls across the screen...");
    connect(txt_ticker_, &QLineEdit::textChanged, this, [this](const QString&) { applyTicker(); });
    lay->addWidget(txt_ticker_);

    // Scroll speed slider
    auto *speed_row = new QHBoxLayout;
    speed_row->addWidget(new QLabel("Scroll Speed:"));
    sld_ticker_speed_ = new QSlider(Qt::Horizontal);
    sld_ticker_speed_->setRange(5, 50);  // maps to 0.5 - 5.0
    float init_speed = renderer_ ? renderer_->news_ticker().scroll_speed : 2.0f;
    sld_ticker_speed_->setValue(static_cast<int>(init_speed * 10.0f));
    connect(sld_ticker_speed_, &QSlider::valueChanged, this, [this](int) { applyTicker(); });
    speed_row->addWidget(sld_ticker_speed_);

    auto *lbl_speed = new QLabel(QString::number(static_cast<double>(init_speed), 'f', 1));
    lbl_speed->setFixedWidth(30);
    lbl_speed->setAlignment(Qt::AlignRight);
    lbl_speed->setStyleSheet("color: #00d4aa;");
    connect(sld_ticker_speed_, &QSlider::valueChanged, lbl_speed,
            [lbl_speed](int v) { lbl_speed->setText(QString::number(v / 10.0, 'f', 1)); });
    speed_row->addWidget(lbl_speed);
    lay->addLayout(speed_row);

    lay->addStretch();
    return page;
}

// ===================================================================
// Subtitles Tab
// ===================================================================

QWidget *OverlaySettingsDialog::createSubtitlesTab()
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(8);

    // Enabled
    chk_srt_enabled_ = new QCheckBox("Enabled");
    connect(chk_srt_enabled_, &QCheckBox::toggled, this, [this](bool checked) {
        if (renderer_) {
            renderer_->set_srt_enabled(checked);
            emit overlayChanged();
        }
    });
    lay->addWidget(chk_srt_enabled_);

    // SRT file path + browse
    lay->addWidget(new QLabel("SRT File:"));
    auto *path_row = new QHBoxLayout;
    txt_srt_path_ = new QLineEdit;
    txt_srt_path_->setPlaceholderText("/path/to/subtitles.srt");
    path_row->addWidget(txt_srt_path_, 1);

    auto *btn_browse = new QPushButton("Browse...");
    connect(btn_browse, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(
            this, "Select SRT File", QString(),
            "Subtitle Files (*.srt);;All Files (*)");
        if (!path.isEmpty()) {
            txt_srt_path_->setText(path);
            if (renderer_) {
                bool ok = renderer_->load_srt(path.toStdString());
                if (ok) {
                    lbl_srt_info_->setText("Loaded successfully.");
                    lbl_srt_info_->setStyleSheet("color: #00d4aa; font-size: 11px;");
                } else {
                    lbl_srt_info_->setText("Failed to load SRT file.");
                    lbl_srt_info_->setStyleSheet("color: #ff6b6b; font-size: 11px;");
                }
                emit overlayChanged();
            }
        }
    });
    path_row->addWidget(btn_browse);
    lay->addLayout(path_row);

    // Info label
    lbl_srt_info_ = new QLabel("No SRT file loaded.");
    lbl_srt_info_->setStyleSheet("color: #7a8ba0; font-size: 11px;");
    lay->addWidget(lbl_srt_info_);

    lay->addStretch();
    return page;
}

// ===================================================================
// Apply helpers
// ===================================================================

void OverlaySettingsDialog::applyTextOverlay()
{
    if (!renderer_) return;

    TextOverlay cfg;
    cfg.text = txt_text_->text().toStdString();
    cfg.font_size = sld_text_size_->value();
    cfg.enabled = chk_text_enabled_->isChecked();

    int pos_idx = cmb_text_pos_->currentIndex();
    switch (pos_idx) {
    case 0: // Top-Left
        cfg.x = 10;
        cfg.y = 10;
        break;
    case 1: // Top-Right
        cfg.x = -10;  // negative = offset from right edge (renderer interprets)
        cfg.y = 10;
        break;
    case 2: // Bottom-Left
        cfg.x = 10;
        cfg.y = -10;
        break;
    case 3: // Bottom-Right
        cfg.x = -10;
        cfg.y = -10;
        break;
    case 4: // Custom
        cfg.x = spn_text_x_->value();
        cfg.y = spn_text_y_->value();
        break;
    }

    renderer_->set_text_overlay(cfg);
    emit overlayChanged();
}

void OverlaySettingsDialog::applyImageOverlay()
{
    if (!renderer_) return;

    ImageOverlay cfg;
    cfg.file_path = txt_image_path_->text().toStdString();
    cfg.x = spn_img_x_->value();
    cfg.y = spn_img_y_->value();
    cfg.render_width  = spn_img_w_->value();
    cfg.render_height = spn_img_h_->value();
    cfg.opacity = static_cast<float>(sld_img_opacity_->value()) / 100.0f;
    cfg.enabled = chk_img_enabled_->isChecked();

    renderer_->set_image_overlay(cfg);

    // Load the image file via QImage and pass raw BGRA data to the renderer
    if (!cfg.file_path.empty()) {
        QImage img(QString::fromStdString(cfg.file_path));
        if (!img.isNull()) {
            QImage bgra = img.convertToFormat(QImage::Format_ARGB32);
            renderer_->load_image(cfg.file_path);
            renderer_->set_image_data(bgra.constBits(), bgra.width(), bgra.height());

            // Update info label with loaded image dimensions
            if (lbl_img_info_) {
                auto rect = renderer_->image_rect();
                lbl_img_info_->setText(
                    QString("Loaded: %1x%2 px  |  Display: %3x%4 px")
                        .arg(bgra.width()).arg(bgra.height())
                        .arg(rect.w).arg(rect.h));
                lbl_img_info_->setStyleSheet("color: #00d4aa; font-size: 11px;");
            }
        } else {
            if (lbl_img_info_) {
                lbl_img_info_->setText("Failed to load image.");
                lbl_img_info_->setStyleSheet("color: #ff6b6b; font-size: 11px;");
            }
        }
    }

    emit overlayChanged();
}

void OverlaySettingsDialog::updateImagePosition(int x, int y, int w, int h)
{
    /* Called from preview widget drag — update spinboxes without re-applying */
    syncing_ = true;
    if (spn_img_x_) spn_img_x_->setValue(x);
    if (spn_img_y_) spn_img_y_->setValue(y);
    if (spn_img_w_) spn_img_w_->setValue(w);
    if (spn_img_h_) spn_img_h_->setValue(h);

    // Update info label
    if (lbl_img_info_ && renderer_) {
        auto rect = renderer_->image_rect();
        lbl_img_info_->setText(
            QString("Position: %1, %2  |  Size: %3x%4 px")
                .arg(rect.x).arg(rect.y).arg(rect.w).arg(rect.h));
    }

    syncing_ = false;
}

void OverlaySettingsDialog::applyLowerThird()
{
    if (!renderer_) return;

    LowerThird cfg;
    cfg.line1 = txt_lt_line1_->text().toStdString();
    cfg.line2 = txt_lt_line2_->text().toStdString();
    cfg.duration_ms = spn_lt_duration_->value() * 1000;
    cfg.enabled = chk_lt_enabled_->isChecked();

    renderer_->set_lower_third(cfg);
    emit overlayChanged();
}

void OverlaySettingsDialog::applyTicker()
{
    if (!renderer_) return;

    NewsTicker cfg;
    cfg.text = txt_ticker_->text().toStdString();
    cfg.scroll_speed = static_cast<float>(sld_ticker_speed_->value()) / 10.0f;
    cfg.enabled = chk_ticker_enabled_->isChecked();

    renderer_->set_news_ticker(cfg);
    emit overlayChanged();
}

} // namespace mc1
