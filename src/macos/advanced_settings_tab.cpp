/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * advanced_settings_tab.cpp — Advanced/Podcast/Logging settings tab
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "advanced_settings_tab.h"
#include <QFormLayout>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QScrollArea>

namespace mc1 {

AdvancedSettingsTab::AdvancedSettingsTab(QWidget *parent)
    : QWidget(parent)
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    auto *inner = new QWidget;
    auto *top = new QVBoxLayout(inner);
    top->setContentsMargins(8, 8, 8, 8);
    scroll->setWidget(inner);

    /* ── Archive ──────────────────────────────────────────────── */
    auto *arc_grp = new QGroupBox(QStringLiteral("Archive / Recording"));
    auto *arc_form = new QFormLayout(arc_grp);
    arc_form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    chk_save_stream_ = new QCheckBox(QStringLiteral("Save archive of stream"));
    arc_form->addRow(QString(), chk_save_stream_);

    chk_save_wav_ = new QCheckBox(QStringLiteral("Also save as WAV"));
    arc_form->addRow(QString(), chk_save_wav_);

    auto *dir_row = new QHBoxLayout;
    edit_archive_dir_ = new QLineEdit;
    edit_archive_dir_->setPlaceholderText(QStringLiteral("/path/to/archive"));
    btn_browse_archive_ = new QPushButton(QStringLiteral("Browse..."));
    dir_row->addWidget(edit_archive_dir_, 1);
    dir_row->addWidget(btn_browse_archive_);
    arc_form->addRow(QStringLiteral("Directory:"), dir_row);

    top->addWidget(arc_grp);

    /* ── Podcast RSS ──────────────────────────────────────────── */
    auto *pod_grp = new QGroupBox(QStringLiteral("Podcast Settings"));
    auto *pod_form = new QFormLayout(pod_grp);
    pod_form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    chk_generate_rss_ = new QCheckBox(QStringLiteral("Generate RSS feed alongside saved file"));
    pod_form->addRow(QString(), chk_generate_rss_);

    chk_rss_use_yp_ = new QCheckBox(QStringLiteral("Populate RSS from YP Settings"));
    pod_form->addRow(QString(), chk_rss_use_yp_);

    edit_podcast_title_ = new QLineEdit;
    pod_form->addRow(QStringLiteral("Title:"), edit_podcast_title_);

    edit_podcast_author_ = new QLineEdit;
    pod_form->addRow(QStringLiteral("Author / Host:"), edit_podcast_author_);

    edit_podcast_category_ = new QLineEdit;
    pod_form->addRow(QStringLiteral("Category:"), edit_podcast_category_);

    edit_podcast_language_ = new QLineEdit;
    edit_podcast_language_->setText(QStringLiteral("en-us"));
    pod_form->addRow(QStringLiteral("Language:"), edit_podcast_language_);

    edit_podcast_copyright_ = new QLineEdit;
    pod_form->addRow(QStringLiteral("Copyright:"), edit_podcast_copyright_);

    edit_podcast_website_ = new QLineEdit;
    pod_form->addRow(QStringLiteral("Website URL:"), edit_podcast_website_);

    edit_podcast_cover_art_ = new QLineEdit;
    pod_form->addRow(QStringLiteral("Cover Art URL:"), edit_podcast_cover_art_);

    edit_podcast_desc_ = new QTextEdit;
    edit_podcast_desc_->setMaximumHeight(80);
    pod_form->addRow(QStringLiteral("Description:"), edit_podcast_desc_);

    top->addWidget(pod_grp);

    /* ── Logging ──────────────────────────────────────────────── */
    auto *log_grp = new QGroupBox(QStringLiteral("Logging"));
    auto *log_form = new QFormLayout(log_grp);
    log_form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    spin_log_level_ = new QSpinBox;
    spin_log_level_->setRange(1, 5);
    spin_log_level_->setValue(4);
    spin_log_level_->setToolTip(QStringLiteral("1=Critical, 2=Error, 3=Warn, 4=Info, 5=Debug"));
    log_form->addRow(QStringLiteral("Log Level:"), spin_log_level_);

    edit_log_file_ = new QLineEdit;
    edit_log_file_->setPlaceholderText(QStringLiteral("/var/log/mcaster1/encoder.log"));
    log_form->addRow(QStringLiteral("Log File:"), edit_log_file_);

    top->addWidget(log_grp);
    top->addStretch();

    /* ── Signals ──────────────────────────────────────────────── */
    connect(btn_browse_archive_, &QPushButton::clicked,
            this, &AdvancedSettingsTab::onBrowseArchive);
    connect(chk_save_stream_, &QCheckBox::toggled,
            this, &AdvancedSettingsTab::onSaveStreamToggled);
    connect(chk_generate_rss_, &QCheckBox::toggled,
            this, &AdvancedSettingsTab::onGenerateRSSToggled);

    /* Initial state */
    onSaveStreamToggled(false);
}

void AdvancedSettingsTab::onBrowseArchive()
{
    QString dir = QFileDialog::getExistingDirectory(this,
        QStringLiteral("Select Archive Directory"),
        edit_archive_dir_->text());
    if (!dir.isEmpty())
        edit_archive_dir_->setText(dir);
}

void AdvancedSettingsTab::onSaveStreamToggled(bool on)
{
    chk_save_wav_->setEnabled(on);
    edit_archive_dir_->setEnabled(on);
    btn_browse_archive_->setEnabled(on);
    chk_generate_rss_->setEnabled(on);
    updatePodcastEnabled();
}

void AdvancedSettingsTab::onGenerateRSSToggled(bool /*on*/)
{
    updatePodcastEnabled();
}

void AdvancedSettingsTab::updatePodcastEnabled()
{
    bool enabled = chk_save_stream_->isChecked() && chk_generate_rss_->isChecked();
    chk_rss_use_yp_->setEnabled(enabled);
    edit_podcast_title_->setEnabled(enabled);
    edit_podcast_author_->setEnabled(enabled);
    edit_podcast_category_->setEnabled(enabled);
    edit_podcast_language_->setEnabled(enabled);
    edit_podcast_copyright_->setEnabled(enabled);
    edit_podcast_website_->setEnabled(enabled);
    edit_podcast_cover_art_->setEnabled(enabled);
    edit_podcast_desc_->setEnabled(enabled);
}

void AdvancedSettingsTab::loadFromConfig(const EncoderConfig &cfg)
{
    chk_save_stream_->setChecked(cfg.archive_enabled);
    chk_save_wav_->setChecked(cfg.archive_wav);
    edit_archive_dir_->setText(QString::fromStdString(cfg.archive_dir));
    chk_generate_rss_->setChecked(cfg.podcast.generate_rss);
    chk_rss_use_yp_->setChecked(cfg.podcast.use_yp_settings);
    edit_podcast_title_->setText(QString::fromStdString(cfg.podcast.title));
    edit_podcast_author_->setText(QString::fromStdString(cfg.podcast.author));
    edit_podcast_category_->setText(QString::fromStdString(cfg.podcast.category));
    edit_podcast_language_->setText(QString::fromStdString(cfg.podcast.language));
    edit_podcast_copyright_->setText(QString::fromStdString(cfg.podcast.copyright));
    edit_podcast_website_->setText(QString::fromStdString(cfg.podcast.website));
    edit_podcast_cover_art_->setText(QString::fromStdString(cfg.podcast.cover_art_url));
    edit_podcast_desc_->setPlainText(QString::fromStdString(cfg.podcast.description));
    spin_log_level_->setValue(cfg.log_level);
    edit_log_file_->setText(QString::fromStdString(cfg.log_file));

    onSaveStreamToggled(cfg.archive_enabled);
}

void AdvancedSettingsTab::saveToConfig(EncoderConfig &cfg) const
{
    cfg.archive_enabled      = chk_save_stream_->isChecked();
    cfg.archive_wav          = chk_save_wav_->isChecked();
    cfg.archive_dir          = edit_archive_dir_->text().toStdString();
    cfg.podcast.generate_rss = chk_generate_rss_->isChecked();
    cfg.podcast.use_yp_settings = chk_rss_use_yp_->isChecked();
    cfg.podcast.title        = edit_podcast_title_->text().toStdString();
    cfg.podcast.author       = edit_podcast_author_->text().toStdString();
    cfg.podcast.category     = edit_podcast_category_->text().toStdString();
    cfg.podcast.language     = edit_podcast_language_->text().toStdString();
    cfg.podcast.copyright    = edit_podcast_copyright_->text().toStdString();
    cfg.podcast.website      = edit_podcast_website_->text().toStdString();
    cfg.podcast.cover_art_url = edit_podcast_cover_art_->text().toStdString();
    cfg.podcast.description  = edit_podcast_desc_->toPlainText().toStdString();
    cfg.log_level            = spin_log_level_->value();
    cfg.log_file             = edit_log_file_->text().toStdString();
}

} // namespace mc1
