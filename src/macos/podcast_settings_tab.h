/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * podcast_settings_tab.h — Podcast Settings tab (RSS, output, SFTP, simulcast)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_PODCAST_SETTINGS_TAB_H
#define MC1_PODCAST_SETTINGS_TAB_H

#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include "config_types.h"

namespace mc1 {

class PodcastSettingsTab : public QWidget {
    Q_OBJECT

public:
    explicit PodcastSettingsTab(QWidget *parent = nullptr);

    void loadFromConfig(const EncoderConfig &cfg);
    void saveToConfig(EncoderConfig &cfg) const;

private Q_SLOTS:
    void onBrowseRssDir();
    void onBrowseEpisodeDir();
    void onBrowseKeyFile();
    void onTestSftp();
    void onAuthMethodChanged(int index);

private:
    void buildUI();

    /* RSS Settings */
    QCheckBox      *chk_generate_rss_;
    QCheckBox      *chk_use_yp_;
    QLineEdit      *edit_title_;
    QLineEdit      *edit_author_;
    QComboBox      *combo_category_;
    QLineEdit      *edit_language_;
    QPlainTextEdit *edit_description_;
    QLineEdit      *edit_copyright_;
    QLineEdit      *edit_website_;
    QLineEdit      *edit_cover_art_;

    /* Output */
    QLineEdit      *edit_rss_dir_;
    QLineEdit      *edit_episode_dir_;
    QLineEdit      *edit_filename_tpl_;

    /* SFTP */
    QCheckBox      *chk_sftp_enabled_;
    QLineEdit      *edit_sftp_host_;
    QSpinBox       *spin_sftp_port_;
    QLineEdit      *edit_sftp_user_;
    QComboBox      *combo_sftp_auth_;
    QLineEdit      *edit_sftp_password_;
    QLineEdit      *edit_sftp_key_;
    QPushButton    *btn_browse_key_;
    QLineEdit      *edit_sftp_remote_;
    QPushButton    *btn_test_sftp_;

    /* Simulcast */
    QCheckBox      *chk_simulcast_;

    /* Publishing destinations */
    QCheckBox      *chk_wp_enabled_;
    QLineEdit      *edit_wp_url_;
    QLineEdit      *edit_wp_user_;
    QLineEdit      *edit_wp_password_;
    QComboBox      *combo_wp_post_type_;

    QCheckBox      *chk_buzz_enabled_;
    QLineEdit      *edit_buzz_token_;
    QLineEdit      *edit_buzz_podcast_id_;

    QCheckBox      *chk_transistor_enabled_;
    QLineEdit      *edit_transistor_key_;
    QLineEdit      *edit_transistor_show_id_;

    QCheckBox      *chk_podbean_enabled_;
    QLineEdit      *edit_podbean_client_id_;
    QLineEdit      *edit_podbean_secret_;
};

} // namespace mc1

#endif // MC1_PODCAST_SETTINGS_TAB_H
