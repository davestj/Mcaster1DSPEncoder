/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video_effects_panel.cpp — Side panel for real-time video effects chain editing
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "video_effects_panel.h"
#include "video/video_effects.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>

namespace mc1 {

// ===================================================================
// Stylesheet constants
// ===================================================================

static const char* kPanelStyle = R"(
    QWidget#VideoEffectsPanel {
        background: #0f1724;
        color: #c8d6e5;
    }
    QLabel {
        color: #c8d6e5;
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
        padding: 4px 8px;
    }
    QPushButton:hover {
        background: #3a4a5e;
    }
)";

static const char* kEffectWidgetStyle = R"(
    QWidget#EffectCard {
        background: #1a2332;
        border: 1px solid #2a3a4e;
        border-radius: 4px;
    }
)";

// ===================================================================
// Constructor
// ===================================================================

VideoEffectsPanel::VideoEffectsPanel(VideoEffectsChain *chain, QWidget *parent)
    : QWidget(parent)
    , chain_(chain)
{
    setObjectName("VideoEffectsPanel");
    setFixedWidth(280);
    setMinimumHeight(400);
    setStyleSheet(kPanelStyle);

    auto *root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(8, 8, 8, 8);
    root_layout->setSpacing(6);

    // --- Title ---
    auto *lbl_title = new QLabel("Video Effects");
    lbl_title->setStyleSheet("font-weight: bold; font-size: 13px; color: #00d4aa;");
    root_layout->addWidget(lbl_title);

    // --- Bypass All ---
    chk_bypass_ = new QCheckBox("Bypass All");
    chk_bypass_->setChecked(chain_ ? chain_->is_bypass() : false);
    connect(chk_bypass_, &QCheckBox::toggled, this, &VideoEffectsPanel::onBypassToggled);
    root_layout->addWidget(chk_bypass_);

    // --- Presets ---
    auto *lbl_presets = new QLabel("Preset:");
    lbl_presets->setStyleSheet("font-size: 11px; color: #7a8ba0;");
    root_layout->addWidget(lbl_presets);

    cmb_presets_ = new QComboBox;
    cmb_presets_->addItem("None (Manual)");
    cmb_presets_->addItem("Broadcast Standard");
    cmb_presets_->addItem("Night Mode");
    cmb_presets_->addItem("Vintage Film");
    cmb_presets_->addItem("Privacy Blur");
    cmb_presets_->addItem("Chroma Key Studio");
    connect(cmb_presets_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VideoEffectsPanel::onPresetSelected);
    root_layout->addWidget(cmb_presets_);

    // --- Add Effect ---
    auto *lbl_add = new QLabel("Add Effect:");
    lbl_add->setStyleSheet("font-size: 11px; color: #7a8ba0;");
    root_layout->addWidget(lbl_add);

    cmb_add_effect_ = new QComboBox;
    cmb_add_effect_->addItem(QString::fromUtf8("\xe2\x80\x94 Add Effect \xe2\x80\x94"));
    for (int i = 0; i < kVideoEffectTypeCount; ++i)
        cmb_add_effect_->addItem(video_effect_type_name(i));
    connect(cmb_add_effect_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VideoEffectsPanel::onAddEffect);
    root_layout->addWidget(cmb_add_effect_);

    // --- Scroll area for effect list ---
    scroll_area_ = new QScrollArea;
    scroll_area_->setWidgetResizable(true);
    scroll_area_->setFrameShape(QFrame::NoFrame);
    scroll_area_->setStyleSheet("QScrollArea { background: transparent; border: none; }");

    auto *scroll_widget = new QWidget;
    scroll_widget->setStyleSheet("background: transparent;");
    effect_list_layout_ = new QVBoxLayout(scroll_widget);
    effect_list_layout_->setContentsMargins(0, 0, 0, 0);
    effect_list_layout_->setSpacing(4);
    effect_list_layout_->addStretch();

    scroll_area_->setWidget(scroll_widget);
    root_layout->addWidget(scroll_area_, 1);

    // Build initial effect list
    rebuildEffectList();
}

// ===================================================================
// Public
// ===================================================================

void VideoEffectsPanel::refresh()
{
    if (chain_)
        chk_bypass_->setChecked(chain_->is_bypass());
    rebuildEffectList();
}

// ===================================================================
// Slots
// ===================================================================

void VideoEffectsPanel::onBypassToggled(bool checked)
{
    if (chain_)
        chain_->set_bypass(checked);
    emit effectsChanged();
}

void VideoEffectsPanel::onAddEffect(int effect_type)
{
    if (effect_type <= 0 || !chain_)
        return;

    // effect_type index in combo: 1 maps to type 0, 2 maps to type 1, etc.
    int type_index = effect_type - 1;
    auto fx = create_video_effect(type_index);
    if (fx) {
        chain_->add_effect(std::move(fx));
        rebuildEffectList();
        emit effectsChanged();
    }

    // Reset combo to placeholder
    cmb_add_effect_->blockSignals(true);
    cmb_add_effect_->setCurrentIndex(0);
    cmb_add_effect_->blockSignals(false);
}

void VideoEffectsPanel::onPresetSelected(int index)
{
    if (!chain_ || index <= 0)
        return;

    // Clear all existing effects
    while (chain_->count() > 0)
        chain_->remove_effect(0);

    switch (index) {
    case 1: { // Broadcast Standard
        auto bc = create_video_effect(0); // BrightnessContrast
        bc->set_param("brightness", 5.0f);
        bc->set_param("contrast", 1.1f);
        chain_->add_effect(std::move(bc));

        auto sat = create_video_effect(1); // Saturation
        sat->set_param("saturation", 1.2f);
        chain_->add_effect(std::move(sat));

        auto sh = create_video_effect(4); // Sharpen
        sh->set_param("strength", 1.0f);
        chain_->add_effect(std::move(sh));
        break;
    }
    case 2: { // Night Mode
        auto bc = create_video_effect(0); // BrightnessContrast
        bc->set_param("brightness", 10.0f);
        bc->set_param("contrast", 1.3f);
        chain_->add_effect(std::move(bc));

        auto ct = create_video_effect(2); // ColorTemperature
        ct->set_param("temperature", -30.0f);
        chain_->add_effect(std::move(ct));

        auto vig = create_video_effect(9); // Vignette
        vig->set_param("radius", 1.0f);
        vig->set_param("softness", 0.5f);
        chain_->add_effect(std::move(vig));
        break;
    }
    case 3: { // Vintage Film
        auto sep = create_video_effect(6); // Sepia
        sep->set_param("intensity", 0.6f);
        chain_->add_effect(std::move(sep));

        auto vig = create_video_effect(9); // Vignette
        vig->set_param("radius", 0.8f);
        vig->set_param("softness", 0.4f);
        chain_->add_effect(std::move(vig));

        auto bc = create_video_effect(0); // BrightnessContrast
        bc->set_param("brightness", -5.0f);
        bc->set_param("contrast", 1.1f);
        chain_->add_effect(std::move(bc));
        break;
    }
    case 4: { // Privacy Blur
        auto pix = create_video_effect(11); // Pixelize
        pix->set_param("block_size", 16.0f);
        chain_->add_effect(std::move(pix));
        break;
    }
    case 5: { // Chroma Key Studio
        auto ck = create_video_effect(8); // ChromaKey
        ck->set_param("key_hue", 120.0f);
        ck->set_param("tolerance", 40.0f);
        ck->set_param("spill", 30.0f);
        chain_->add_effect(std::move(ck));
        break;
    }
    default:
        break;
    }

    rebuildEffectList();
    emit effectsChanged();
}

// ===================================================================
// Private helpers
// ===================================================================

void VideoEffectsPanel::rebuildEffectList()
{
    // Remove all widgets from the layout (except the stretch)
    while (effect_list_layout_->count() > 0) {
        QLayoutItem *item = effect_list_layout_->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    if (chain_) {
        for (int i = 0; i < chain_->count(); ++i) {
            VideoEffect *fx = chain_->effect(i);
            if (fx) {
                QWidget *w = createEffectWidget(fx, i);
                effect_list_layout_->addWidget(w);
            }
        }
    }

    effect_list_layout_->addStretch();
}

QWidget *VideoEffectsPanel::createEffectWidget(VideoEffect *fx, int index)
{
    auto *card = new QWidget;
    card->setObjectName("EffectCard");
    card->setStyleSheet(kEffectWidgetStyle);

    auto *card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(6, 6, 6, 6);
    card_layout->setSpacing(4);

    // --- Header row: enabled checkbox + name + remove button ---
    auto *header = new QHBoxLayout;
    header->setSpacing(4);

    auto *chk_enabled = new QCheckBox;
    chk_enabled->setChecked(fx->enabled);
    connect(chk_enabled, &QCheckBox::toggled, this, [this, fx](bool checked) {
        fx->enabled = checked;
        emit effectsChanged();
    });
    header->addWidget(chk_enabled);

    auto *lbl_name = new QLabel(QString::fromStdString(fx->name()));
    lbl_name->setStyleSheet("font-weight: bold; font-size: 11px; color: #c8d6e5;");
    header->addWidget(lbl_name, 1);

    auto *btn_remove = new QPushButton(QString::fromUtf8("\xc3\x97"));
    btn_remove->setFixedSize(20, 20);
    btn_remove->setStyleSheet(
        "QPushButton { background: #3a2020; color: #ff6b6b; border: 1px solid #5a3030;"
        " border-radius: 3px; font-weight: bold; padding: 0; }"
        "QPushButton:hover { background: #5a3030; }");
    connect(btn_remove, &QPushButton::clicked, this, [this, index]() {
        if (chain_) {
            chain_->remove_effect(index);
            rebuildEffectList();
            emit effectsChanged();
        }
    });
    header->addWidget(btn_remove);

    card_layout->addLayout(header);

    // --- Parameter sliders ---
    auto params = fx->params();
    for (const auto &p : params) {
        QWidget *slider_row = createSlider(
            QString::fromStdString(p.name), p.value, p.min_val, p.max_val, fx, p.name);
        card_layout->addWidget(slider_row);
    }

    return card;
}

QWidget *VideoEffectsPanel::createSlider(const QString &label, float value, float min_val,
                                          float max_val, VideoEffect *fx,
                                          const std::string &param_name)
{
    auto *row = new QWidget;
    auto *row_layout = new QVBoxLayout(row);
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(1);

    // Label + value row
    auto *label_row = new QHBoxLayout;
    auto *lbl = new QLabel(label);
    lbl->setStyleSheet("font-size: 10px; color: #7a8ba0;");
    label_row->addWidget(lbl);

    auto *lbl_val = new QLabel(QString::number(static_cast<double>(value), 'f', 2));
    lbl_val->setStyleSheet("font-size: 10px; color: #00d4aa;");
    lbl_val->setAlignment(Qt::AlignRight);
    label_row->addWidget(lbl_val);
    row_layout->addLayout(label_row);

    // Slider
    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange(0, 1000);
    // Map current value to slider position
    float norm = (max_val > min_val) ? (value - min_val) / (max_val - min_val) : 0.0f;
    slider->setValue(static_cast<int>(norm * 1000.0f));
    slider->setFixedHeight(18);

    connect(slider, &QSlider::valueChanged, this,
            [this, fx, param_name, min_val, max_val, lbl_val](int int_val) {
        float norm_v = static_cast<float>(int_val) / 1000.0f;
        float real_val = min_val + norm_v * (max_val - min_val);
        fx->set_param(param_name, real_val);
        lbl_val->setText(QString::number(static_cast<double>(real_val), 'f', 2));
        emit effectsChanged();
    });

    row_layout->addWidget(slider);
    return row;
}

} // namespace mc1
