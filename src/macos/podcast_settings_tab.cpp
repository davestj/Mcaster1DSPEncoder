/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * podcast_settings_tab.cpp — Podcast Settings tab (RSS, output, SFTP, simulcast)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "podcast_settings_tab.h"

#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>

namespace mc1 {

PodcastSettingsTab::PodcastSettingsTab(QWidget *parent)
    : QWidget(parent)
{
    buildUI();
}

void PodcastSettingsTab::buildUI()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    auto *tabs = new QTabWidget;

    /* ═══════════════════════════════════════════════════════════════
     * Tab 1: RSS Feed
     * ═══════════════════════════════════════════════════════════════ */
    {
        auto *page = new QWidget;
        auto *lay  = new QVBoxLayout(page);

        auto *rss_group = new QGroupBox(QStringLiteral("RSS Feed Settings"));
        auto *rss_form  = new QFormLayout(rss_group);

        chk_generate_rss_ = new QCheckBox(QStringLiteral("Generate RSS 2.0 feed"));
        rss_form->addRow(chk_generate_rss_);

        chk_use_yp_ = new QCheckBox(QStringLiteral("Copy settings from YP tab"));
        rss_form->addRow(chk_use_yp_);

        edit_title_ = new QLineEdit;
        edit_title_->setPlaceholderText(QStringLiteral("My Podcast"));
        rss_form->addRow(QStringLiteral("Title:"), edit_title_);

        edit_author_ = new QLineEdit;
        rss_form->addRow(QStringLiteral("Author:"), edit_author_);

        combo_category_ = new QComboBox;
        combo_category_->addItems({
            QStringLiteral("Technology"), QStringLiteral("Music"),
            QStringLiteral("Comedy"), QStringLiteral("News"),
            QStringLiteral("Sports"), QStringLiteral("Education"),
            QStringLiteral("Arts"), QStringLiteral("Business"),
            QStringLiteral("Health & Fitness"), QStringLiteral("Science"),
            QStringLiteral("Society & Culture"), QStringLiteral("True Crime"),
            QStringLiteral("History"), QStringLiteral("Government"),
        });
        combo_category_->setEditable(true);
        rss_form->addRow(QStringLiteral("Category:"), combo_category_);

        edit_language_ = new QLineEdit(QStringLiteral("en-us"));
        rss_form->addRow(QStringLiteral("Language:"), edit_language_);

        edit_description_ = new QPlainTextEdit;
        edit_description_->setMaximumHeight(60);
        edit_description_->setPlaceholderText(QStringLiteral("Podcast description..."));
        rss_form->addRow(QStringLiteral("Description:"), edit_description_);

        edit_copyright_ = new QLineEdit;
        rss_form->addRow(QStringLiteral("Copyright:"), edit_copyright_);

        edit_website_ = new QLineEdit;
        edit_website_->setPlaceholderText(QStringLiteral("https://example.com/podcast"));
        rss_form->addRow(QStringLiteral("Website:"), edit_website_);

        edit_cover_art_ = new QLineEdit;
        edit_cover_art_->setPlaceholderText(QStringLiteral("https://example.com/cover.jpg"));
        rss_form->addRow(QStringLiteral("Cover Art URL:"), edit_cover_art_);

        lay->addWidget(rss_group);
        lay->addStretch();
        tabs->addTab(page, QIcon(QStringLiteral(":/icons/rss.svg")),
                     QStringLiteral("RSS Feed"));
    }

    /* ═══════════════════════════════════════════════════════════════
     * Tab 2: Output & SFTP
     * ═══════════════════════════════════════════════════════════════ */
    {
        auto *page = new QWidget;
        auto *lay  = new QVBoxLayout(page);

        /* Output Directories */
        auto *out_group = new QGroupBox(QStringLiteral("Output Directories"));
        auto *out_form  = new QFormLayout(out_group);

        auto *rss_dir_row = new QHBoxLayout;
        edit_rss_dir_ = new QLineEdit;
        edit_rss_dir_->setPlaceholderText(QStringLiteral("~/Podcasts/rss"));
        auto *btn_rss_dir = new QPushButton(QStringLiteral("Browse..."));
        connect(btn_rss_dir, &QPushButton::clicked, this, &PodcastSettingsTab::onBrowseRssDir);
        rss_dir_row->addWidget(edit_rss_dir_, 1);
        rss_dir_row->addWidget(btn_rss_dir);
        out_form->addRow(QStringLiteral("RSS Output Dir:"), rss_dir_row);

        auto *ep_dir_row = new QHBoxLayout;
        edit_episode_dir_ = new QLineEdit;
        edit_episode_dir_->setPlaceholderText(QStringLiteral("~/Podcasts/episodes"));
        auto *btn_ep_dir = new QPushButton(QStringLiteral("Browse..."));
        connect(btn_ep_dir, &QPushButton::clicked, this, &PodcastSettingsTab::onBrowseEpisodeDir);
        ep_dir_row->addWidget(edit_episode_dir_, 1);
        ep_dir_row->addWidget(btn_ep_dir);
        out_form->addRow(QStringLiteral("Episode Dir:"), ep_dir_row);

        edit_filename_tpl_ = new QLineEdit(QStringLiteral("{title}-{date}"));
        out_form->addRow(QStringLiteral("Filename Template:"), edit_filename_tpl_);

        lay->addWidget(out_group);

        /* SFTP Upload */
        auto *sftp_group = new QGroupBox(QStringLiteral("SFTP Upload"));
        auto *sftp_form  = new QFormLayout(sftp_group);

        chk_sftp_enabled_ = new QCheckBox(QStringLiteral("Enable SFTP upload"));
        sftp_form->addRow(chk_sftp_enabled_);

        edit_sftp_host_ = new QLineEdit;
        edit_sftp_host_->setPlaceholderText(QStringLiteral("sftp.example.com"));
        sftp_form->addRow(QStringLiteral("Host:"), edit_sftp_host_);

        spin_sftp_port_ = new QSpinBox;
        spin_sftp_port_->setRange(1, 65535);
        spin_sftp_port_->setValue(22);
        sftp_form->addRow(QStringLiteral("Port:"), spin_sftp_port_);

        edit_sftp_user_ = new QLineEdit;
        sftp_form->addRow(QStringLiteral("Username:"), edit_sftp_user_);

        combo_sftp_auth_ = new QComboBox;
        combo_sftp_auth_->addItem(QStringLiteral("Password"));
        combo_sftp_auth_->addItem(QStringLiteral("SSH Key"));
        connect(combo_sftp_auth_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &PodcastSettingsTab::onAuthMethodChanged);
        sftp_form->addRow(QStringLiteral("Auth Method:"), combo_sftp_auth_);

        edit_sftp_password_ = new QLineEdit;
        edit_sftp_password_->setEchoMode(QLineEdit::Password);
        sftp_form->addRow(QStringLiteral("Password:"), edit_sftp_password_);

        auto *key_row = new QHBoxLayout;
        edit_sftp_key_ = new QLineEdit;
        edit_sftp_key_->setPlaceholderText(QStringLiteral("~/.ssh/id_rsa"));
        btn_browse_key_ = new QPushButton(QStringLiteral("Browse..."));
        connect(btn_browse_key_, &QPushButton::clicked, this, &PodcastSettingsTab::onBrowseKeyFile);
        key_row->addWidget(edit_sftp_key_, 1);
        key_row->addWidget(btn_browse_key_);
        sftp_form->addRow(QStringLiteral("SSH Key:"), key_row);

        edit_sftp_remote_ = new QLineEdit;
        edit_sftp_remote_->setPlaceholderText(QStringLiteral("/var/www/podcast"));
        sftp_form->addRow(QStringLiteral("Remote Dir:"), edit_sftp_remote_);

        btn_test_sftp_ = new QPushButton(QStringLiteral("Test Connection"));
        connect(btn_test_sftp_, &QPushButton::clicked, this, &PodcastSettingsTab::onTestSftp);
        sftp_form->addRow(QString(), btn_test_sftp_);

        /* Start with SSH key fields hidden */
        edit_sftp_key_->setVisible(false);
        btn_browse_key_->setVisible(false);

        lay->addWidget(sftp_group);
        lay->addStretch();
        tabs->addTab(page, QIcon(QStringLiteral(":/icons/sftp.svg")),
                     QStringLiteral("Output & SFTP"));
    }

    /* ═══════════════════════════════════════════════════════════════
     * Tab 3: Simulcast
     * ═══════════════════════════════════════════════════════════════ */
    {
        auto *page = new QWidget;
        auto *lay  = new QVBoxLayout(page);

        auto *sim_group = new QGroupBox(QStringLiteral("Simulcast"));
        auto *sim_form  = new QFormLayout(sim_group);

        chk_simulcast_ = new QCheckBox(QStringLiteral("Simulcast to DNAS /podcast mount"));
        sim_form->addRow(chk_simulcast_);

        lay->addWidget(sim_group);
        lay->addStretch();
        tabs->addTab(page, QIcon(QStringLiteral(":/icons/broadcast.svg")),
                     QStringLiteral("Simulcast"));
    }

    /* ═══════════════════════════════════════════════════════════════
     * Tab 4: Publishing Destinations
     * ═══════════════════════════════════════════════════════════════ */
    {
        auto *page = new QWidget;
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);

        auto *content = new QWidget;
        auto *lay  = new QVBoxLayout(content);

        /* WordPress */
        auto *wp_group = new QGroupBox(QStringLiteral("WordPress (PowerPress / SSP / Podlove)"));
        auto *wp_form  = new QFormLayout(wp_group);

        chk_wp_enabled_ = new QCheckBox(QStringLiteral("Enable"));
        wp_form->addRow(chk_wp_enabled_);

        edit_wp_url_ = new QLineEdit;
        edit_wp_url_->setPlaceholderText(QStringLiteral("https://example.com"));
        wp_form->addRow(QStringLiteral("Site URL:"), edit_wp_url_);

        edit_wp_user_ = new QLineEdit;
        wp_form->addRow(QStringLiteral("Username:"), edit_wp_user_);

        edit_wp_password_ = new QLineEdit;
        edit_wp_password_->setEchoMode(QLineEdit::Password);
        edit_wp_password_->setPlaceholderText(QStringLiteral("Application Password"));
        wp_form->addRow(QStringLiteral("App Password:"), edit_wp_password_);

        combo_wp_post_type_ = new QComboBox;
        combo_wp_post_type_->addItems({
            QStringLiteral("post"),
            QStringLiteral("podcast"),
        });
        wp_form->addRow(QStringLiteral("Post Type:"), combo_wp_post_type_);

        lay->addWidget(wp_group);

        /* Buzzsprout */
        auto *buzz_group = new QGroupBox(QStringLiteral("Buzzsprout"));
        auto *buzz_form  = new QFormLayout(buzz_group);

        chk_buzz_enabled_ = new QCheckBox(QStringLiteral("Enable"));
        buzz_form->addRow(chk_buzz_enabled_);

        edit_buzz_token_ = new QLineEdit;
        edit_buzz_token_->setEchoMode(QLineEdit::Password);
        buzz_form->addRow(QStringLiteral("API Token:"), edit_buzz_token_);

        edit_buzz_podcast_id_ = new QLineEdit;
        edit_buzz_podcast_id_->setPlaceholderText(QStringLiteral("123456"));
        buzz_form->addRow(QStringLiteral("Podcast ID:"), edit_buzz_podcast_id_);

        lay->addWidget(buzz_group);

        /* Transistor.fm */
        auto *tr_group = new QGroupBox(QStringLiteral("Transistor.fm"));
        auto *tr_form  = new QFormLayout(tr_group);

        chk_transistor_enabled_ = new QCheckBox(QStringLiteral("Enable"));
        tr_form->addRow(chk_transistor_enabled_);

        edit_transistor_key_ = new QLineEdit;
        edit_transistor_key_->setEchoMode(QLineEdit::Password);
        tr_form->addRow(QStringLiteral("API Key:"), edit_transistor_key_);

        edit_transistor_show_id_ = new QLineEdit;
        tr_form->addRow(QStringLiteral("Show ID:"), edit_transistor_show_id_);

        lay->addWidget(tr_group);

        /* Podbean */
        auto *pb_group = new QGroupBox(QStringLiteral("Podbean"));
        auto *pb_form  = new QFormLayout(pb_group);

        chk_podbean_enabled_ = new QCheckBox(QStringLiteral("Enable"));
        pb_form->addRow(chk_podbean_enabled_);

        edit_podbean_client_id_ = new QLineEdit;
        pb_form->addRow(QStringLiteral("Client ID:"), edit_podbean_client_id_);

        edit_podbean_secret_ = new QLineEdit;
        edit_podbean_secret_->setEchoMode(QLineEdit::Password);
        pb_form->addRow(QStringLiteral("Client Secret:"), edit_podbean_secret_);

        lay->addWidget(pb_group);
        lay->addStretch();

        scroll->setWidget(content);
        auto *page_lay = new QVBoxLayout(page);
        page_lay->setContentsMargins(0, 0, 0, 0);
        page_lay->addWidget(scroll);

        tabs->addTab(page, QIcon(QStringLiteral(":/icons/connect.svg")),
                     QStringLiteral("Publishing"));
    }

    root->addWidget(tabs);
}

void PodcastSettingsTab::loadFromConfig(const EncoderConfig &cfg)
{
    const auto &pc = cfg.podcast;

    chk_generate_rss_->setChecked(pc.generate_rss);
    chk_use_yp_->setChecked(pc.use_yp_settings);
    edit_title_->setText(QString::fromStdString(pc.title));
    edit_author_->setText(QString::fromStdString(pc.author));

    int cat_idx = combo_category_->findText(QString::fromStdString(pc.category));
    if (cat_idx >= 0)
        combo_category_->setCurrentIndex(cat_idx);
    else if (!pc.category.empty())
        combo_category_->setCurrentText(QString::fromStdString(pc.category));

    edit_language_->setText(QString::fromStdString(pc.language));
    edit_description_->setPlainText(QString::fromStdString(pc.description));
    edit_copyright_->setText(QString::fromStdString(pc.copyright));
    edit_website_->setText(QString::fromStdString(pc.website));
    edit_cover_art_->setText(QString::fromStdString(pc.cover_art_url));

    edit_rss_dir_->setText(QString::fromStdString(pc.rss_output_dir));
    edit_episode_dir_->setText(QString::fromStdString(pc.episode_output_dir));
    edit_filename_tpl_->setText(QString::fromStdString(pc.episode_filename_tpl));

    chk_sftp_enabled_->setChecked(pc.sftp_enabled);
    edit_sftp_host_->setText(QString::fromStdString(pc.sftp_host));
    spin_sftp_port_->setValue(pc.sftp_port);
    edit_sftp_user_->setText(QString::fromStdString(pc.sftp_username));
    edit_sftp_password_->setText(QString::fromStdString(pc.sftp_password));
    edit_sftp_key_->setText(QString::fromStdString(pc.sftp_key_path));
    edit_sftp_remote_->setText(QString::fromStdString(pc.sftp_remote_dir));

    /* Set auth method based on whether key path is present */
    if (!pc.sftp_key_path.empty()) {
        combo_sftp_auth_->setCurrentIndex(1); /* SSH Key */
    } else {
        combo_sftp_auth_->setCurrentIndex(0); /* Password */
    }
    onAuthMethodChanged(combo_sftp_auth_->currentIndex());

    chk_simulcast_->setChecked(pc.simulcast_dnas);

    /* Publishing destinations */
    chk_wp_enabled_->setChecked(pc.wordpress.enabled);
    edit_wp_url_->setText(QString::fromStdString(pc.wordpress.site_url));
    edit_wp_user_->setText(QString::fromStdString(pc.wordpress.username));
    edit_wp_password_->setText(QString::fromStdString(pc.wordpress.app_password));
    int pt_idx = combo_wp_post_type_->findText(QString::fromStdString(pc.wordpress.post_type));
    if (pt_idx >= 0) combo_wp_post_type_->setCurrentIndex(pt_idx);

    chk_buzz_enabled_->setChecked(pc.buzzsprout.enabled);
    edit_buzz_token_->setText(QString::fromStdString(pc.buzzsprout.api_token));
    edit_buzz_podcast_id_->setText(QString::fromStdString(pc.buzzsprout.podcast_id));

    chk_transistor_enabled_->setChecked(pc.transistor.enabled);
    edit_transistor_key_->setText(QString::fromStdString(pc.transistor.api_key));
    edit_transistor_show_id_->setText(QString::fromStdString(pc.transistor.show_id));

    chk_podbean_enabled_->setChecked(pc.podbean.enabled);
    edit_podbean_client_id_->setText(QString::fromStdString(pc.podbean.client_id));
    edit_podbean_secret_->setText(QString::fromStdString(pc.podbean.client_secret));
}

void PodcastSettingsTab::saveToConfig(EncoderConfig &cfg) const
{
    auto &pc = cfg.podcast;

    pc.generate_rss     = chk_generate_rss_->isChecked();
    pc.use_yp_settings  = chk_use_yp_->isChecked();
    pc.title            = edit_title_->text().toStdString();
    pc.author           = edit_author_->text().toStdString();
    pc.category         = combo_category_->currentText().toStdString();
    pc.language         = edit_language_->text().toStdString();
    pc.description      = edit_description_->toPlainText().toStdString();
    pc.copyright        = edit_copyright_->text().toStdString();
    pc.website          = edit_website_->text().toStdString();
    pc.cover_art_url    = edit_cover_art_->text().toStdString();

    pc.rss_output_dir      = edit_rss_dir_->text().toStdString();
    pc.episode_output_dir  = edit_episode_dir_->text().toStdString();
    pc.episode_filename_tpl = edit_filename_tpl_->text().toStdString();

    pc.sftp_enabled    = chk_sftp_enabled_->isChecked();
    pc.sftp_host       = edit_sftp_host_->text().toStdString();
    pc.sftp_port       = static_cast<uint16_t>(spin_sftp_port_->value());
    pc.sftp_username   = edit_sftp_user_->text().toStdString();
    pc.sftp_password   = edit_sftp_password_->text().toStdString();
    pc.sftp_key_path   = edit_sftp_key_->text().toStdString();
    pc.sftp_remote_dir = edit_sftp_remote_->text().toStdString();

    pc.simulcast_dnas  = chk_simulcast_->isChecked();

    /* Publishing destinations */
    pc.wordpress.enabled      = chk_wp_enabled_->isChecked();
    pc.wordpress.site_url     = edit_wp_url_->text().toStdString();
    pc.wordpress.username     = edit_wp_user_->text().toStdString();
    pc.wordpress.app_password = edit_wp_password_->text().toStdString();
    pc.wordpress.post_type    = combo_wp_post_type_->currentText().toStdString();

    pc.buzzsprout.enabled    = chk_buzz_enabled_->isChecked();
    pc.buzzsprout.api_token  = edit_buzz_token_->text().toStdString();
    pc.buzzsprout.podcast_id = edit_buzz_podcast_id_->text().toStdString();

    pc.transistor.enabled = chk_transistor_enabled_->isChecked();
    pc.transistor.api_key = edit_transistor_key_->text().toStdString();
    pc.transistor.show_id = edit_transistor_show_id_->text().toStdString();

    pc.podbean.enabled       = chk_podbean_enabled_->isChecked();
    pc.podbean.client_id     = edit_podbean_client_id_->text().toStdString();
    pc.podbean.client_secret = edit_podbean_secret_->text().toStdString();
}

void PodcastSettingsTab::onBrowseRssDir()
{
    QString dir = QFileDialog::getExistingDirectory(this,
        QStringLiteral("Select RSS Output Directory"),
        edit_rss_dir_->text());
    if (!dir.isEmpty())
        edit_rss_dir_->setText(dir);
}

void PodcastSettingsTab::onBrowseEpisodeDir()
{
    QString dir = QFileDialog::getExistingDirectory(this,
        QStringLiteral("Select Episode Output Directory"),
        edit_episode_dir_->text());
    if (!dir.isEmpty())
        edit_episode_dir_->setText(dir);
}

void PodcastSettingsTab::onBrowseKeyFile()
{
    QString file = QFileDialog::getOpenFileName(this,
        QStringLiteral("Select SSH Private Key"),
        QDir::homePath() + QStringLiteral("/.ssh"),
        QStringLiteral("All Files (*)"));
    if (!file.isEmpty())
        edit_sftp_key_->setText(file);
}

void PodcastSettingsTab::onTestSftp()
{
    /* TODO: Implement actual SFTP test via libssh2 (Task #97) */
    QMessageBox::information(this,
        QStringLiteral("SFTP Test"),
        QStringLiteral("SFTP connection test will be available when libssh2 support is built.\n\n"
                       "Host: %1\nPort: %2\nUser: %3")
            .arg(edit_sftp_host_->text())
            .arg(spin_sftp_port_->value())
            .arg(edit_sftp_user_->text()));
}

void PodcastSettingsTab::onAuthMethodChanged(int index)
{
    bool is_key = (index == 1);
    edit_sftp_password_->setVisible(!is_key);
    edit_sftp_key_->setVisible(is_key);
    btn_browse_key_->setVisible(is_key);
}

} // namespace mc1
