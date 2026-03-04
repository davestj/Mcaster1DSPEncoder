/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * advanced_settings_tab.h — Advanced/Podcast/Logging settings tab
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_ADVANCED_SETTINGS_TAB_H
#define MC1_ADVANCED_SETTINGS_TAB_H

#include <QWidget>
#include <QCheckBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QTextEdit>
#include <QPushButton>
#include "config_types.h"

namespace mc1 {

class AdvancedSettingsTab : public QWidget {
    Q_OBJECT

public:
    explicit AdvancedSettingsTab(QWidget *parent = nullptr);

    void loadFromConfig(const EncoderConfig &cfg);
    void saveToConfig(EncoderConfig &cfg) const;

private Q_SLOTS:
    void onBrowseArchive();
    void onSaveStreamToggled(bool on);
    void onGenerateRSSToggled(bool on);

private:
    void updatePodcastEnabled();

    /* Archive */
    QCheckBox   *chk_save_stream_;
    QCheckBox   *chk_save_wav_;
    QLineEdit   *edit_archive_dir_;
    QPushButton *btn_browse_archive_;

    /* Podcast RSS */
    QCheckBox   *chk_generate_rss_;
    QCheckBox   *chk_rss_use_yp_;
    QLineEdit   *edit_podcast_title_;
    QLineEdit   *edit_podcast_author_;
    QLineEdit   *edit_podcast_category_;
    QLineEdit   *edit_podcast_language_;
    QLineEdit   *edit_podcast_copyright_;
    QLineEdit   *edit_podcast_website_;
    QLineEdit   *edit_podcast_cover_art_;
    QTextEdit   *edit_podcast_desc_;

    /* Logging */
    QSpinBox    *spin_log_level_;
    QLineEdit   *edit_log_file_;
};

} // namespace mc1

#endif // MC1_ADVANCED_SETTINGS_TAB_H
