/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video_effects_panel.h — Side panel for real-time video effects chain editing
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_VIDEO_EFFECTS_PANEL_H
#define MC1_VIDEO_EFFECTS_PANEL_H

#include <QWidget>
#include <QScrollArea>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QVBoxLayout;

namespace mc1 {
class VideoEffectsChain;
class VideoEffect;

class VideoEffectsPanel : public QWidget {
    Q_OBJECT
public:
    explicit VideoEffectsPanel(VideoEffectsChain *chain, QWidget *parent = nullptr);

    void refresh();  // rebuild UI from chain state

signals:
    void effectsChanged();

private slots:
    void onBypassToggled(bool checked);
    void onAddEffect(int effect_type);
    void onPresetSelected(int index);

private:
    void rebuildEffectList();
    QWidget *createEffectWidget(VideoEffect *fx, int index);
    QWidget *createSlider(const QString &label, float value, float min_val,
                          float max_val, VideoEffect *fx, const std::string &param_name);

    VideoEffectsChain *chain_ = nullptr;
    QVBoxLayout *effect_list_layout_ = nullptr;
    QScrollArea *scroll_area_ = nullptr;
    QCheckBox *chk_bypass_ = nullptr;
    QComboBox *cmb_add_effect_ = nullptr;
    QComboBox *cmb_presets_ = nullptr;
};

} // namespace mc1

#endif // MC1_VIDEO_EFFECTS_PANEL_H
