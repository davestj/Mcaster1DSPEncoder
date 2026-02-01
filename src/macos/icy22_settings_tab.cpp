/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * icy22_settings_tab.cpp — ICY 2.2 Extended Settings tab + 6 popup dialogs
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "icy22_settings_tab.h"
#include <QFormLayout>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QUuid>

namespace mc1 {

/* ════════════════════════════════════════════════════════════════════
 *  Helper: standard dialog layout with OK/Cancel
 * ════════════════════════════════════════════════════════════════════ */
static QDialogButtonBox* addButtons(QDialog *dlg, QVBoxLayout *lay)
{
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    lay->addStretch();
    lay->addWidget(bb);
    QObject::connect(bb, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    return bb;
}

/* ════════════════════════════════════════════════════════════════════
 *  Station & Show Dialog
 * ════════════════════════════════════════════════════════════════════ */
ICY22StationDialog::ICY22StationDialog(ICY22Config &cfg, QWidget *parent)
    : QDialog(parent), cfg_(cfg)
{
    setWindowTitle(QStringLiteral("Station & Show Settings"));
    resize(460, 380);
    auto *lay = new QVBoxLayout(this);

    auto *id_grp = new QGroupBox(QStringLiteral("Station Identity"));
    auto *id_form = new QFormLayout(id_grp);
    id_form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    edit_station_id_  = new QLineEdit(QString::fromStdString(cfg_.station_id));
    edit_station_logo_ = new QLineEdit(QString::fromStdString(cfg_.station_logo));
    combo_verify_ = new QComboBox;
    combo_verify_->addItems({QStringLiteral("unverified"), QStringLiteral("pending"),
                             QStringLiteral("verified"), QStringLiteral("gold")});
    combo_verify_->setCurrentText(QString::fromStdString(cfg_.verify_status));
    id_form->addRow(QStringLiteral("Station ID:"), edit_station_id_);
    id_form->addRow(QStringLiteral("Logo URL:"), edit_station_logo_);
    id_form->addRow(QStringLiteral("Verify Status:"), combo_verify_);
    lay->addWidget(id_grp);

    auto *show_grp = new QGroupBox(QStringLiteral("Show / Programming"));
    auto *show_form = new QFormLayout(show_grp);
    show_form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    edit_show_title_     = new QLineEdit(QString::fromStdString(cfg_.show_title));
    edit_show_start_     = new QLineEdit(QString::fromStdString(cfg_.show_start));
    edit_show_end_       = new QLineEdit(QString::fromStdString(cfg_.show_end));
    edit_next_show_      = new QLineEdit(QString::fromStdString(cfg_.next_show));
    edit_next_show_time_ = new QLineEdit(QString::fromStdString(cfg_.next_show_time));
    edit_schedule_url_   = new QLineEdit(QString::fromStdString(cfg_.schedule_url));
    chk_auto_dj_         = new QCheckBox(QStringLiteral("AutoDJ"));
    chk_auto_dj_->setChecked(cfg_.auto_dj);
    edit_playlist_name_  = new QLineEdit(QString::fromStdString(cfg_.playlist_name));

    show_form->addRow(QStringLiteral("Show Title:"), edit_show_title_);
    show_form->addRow(QStringLiteral("Start:"), edit_show_start_);
    show_form->addRow(QStringLiteral("End:"), edit_show_end_);
    show_form->addRow(QStringLiteral("Next Show:"), edit_next_show_);
    show_form->addRow(QStringLiteral("Next Time:"), edit_next_show_time_);
    show_form->addRow(QStringLiteral("Schedule URL:"), edit_schedule_url_);
    show_form->addRow(QString(), chk_auto_dj_);
    show_form->addRow(QStringLiteral("Playlist:"), edit_playlist_name_);
    lay->addWidget(show_grp);

    addButtons(this, lay);
}

void ICY22StationDialog::accept()
{
    cfg_.station_id    = edit_station_id_->text().toStdString();
    cfg_.station_logo  = edit_station_logo_->text().toStdString();
    cfg_.verify_status = combo_verify_->currentText().toStdString();
    cfg_.show_title    = edit_show_title_->text().toStdString();
    cfg_.show_start    = edit_show_start_->text().toStdString();
    cfg_.show_end      = edit_show_end_->text().toStdString();
    cfg_.next_show     = edit_next_show_->text().toStdString();
    cfg_.next_show_time = edit_next_show_time_->text().toStdString();
    cfg_.schedule_url  = edit_schedule_url_->text().toStdString();
    cfg_.auto_dj       = chk_auto_dj_->isChecked();
    cfg_.playlist_name = edit_playlist_name_->text().toStdString();
    QDialog::accept();
}

/* ════════════════════════════════════════════════════════════════════
 *  DJ & Host Dialog
 * ════════════════════════════════════════════════════════════════════ */
ICY22DJDialog::ICY22DJDialog(ICY22Config &cfg, QWidget *parent)
    : QDialog(parent), cfg_(cfg)
{
    setWindowTitle(QStringLiteral("DJ & Host Settings"));
    resize(400, 220);
    auto *lay = new QVBoxLayout(this);
    auto *grp = new QGroupBox(QStringLiteral("DJ / Host"));
    auto *form = new QFormLayout(grp);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    edit_handle_ = new QLineEdit(QString::fromStdString(cfg_.dj_handle));
    edit_bio_    = new QLineEdit(QString::fromStdString(cfg_.dj_bio));
    edit_genre_  = new QLineEdit(QString::fromStdString(cfg_.dj_genre));
    edit_rating_ = new QLineEdit(QString::fromStdString(cfg_.dj_rating));

    form->addRow(QStringLiteral("Handle:"), edit_handle_);
    form->addRow(QStringLiteral("Bio:"), edit_bio_);
    form->addRow(QStringLiteral("Genre:"), edit_genre_);
    form->addRow(QStringLiteral("Rating:"), edit_rating_);
    lay->addWidget(grp);
    addButtons(this, lay);
}

void ICY22DJDialog::accept()
{
    cfg_.dj_handle = edit_handle_->text().toStdString();
    cfg_.dj_bio    = edit_bio_->text().toStdString();
    cfg_.dj_genre  = edit_genre_->text().toStdString();
    cfg_.dj_rating = edit_rating_->text().toStdString();
    QDialog::accept();
}

/* ════════════════════════════════════════════════════════════════════
 *  Social & Discovery Dialog
 * ════════════════════════════════════════════════════════════════════ */
ICY22SocialDialog::ICY22SocialDialog(ICY22Config &cfg, QWidget *parent)
    : QDialog(parent), cfg_(cfg)
{
    setWindowTitle(QStringLiteral("Social & Discovery Settings"));
    resize(460, 380);
    auto *lay = new QVBoxLayout(this);
    auto *grp = new QGroupBox(QStringLiteral("Social & Discovery"));
    auto *form = new QFormLayout(grp);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    edit_creator_   = new QLineEdit(QString::fromStdString(cfg_.creator_handle));
    edit_emoji_     = new QLineEdit(QString::fromStdString(cfg_.emoji));
    edit_hashtags_  = new QLineEdit(QString::fromStdString(cfg_.hashtags));
    edit_twitter_   = new QLineEdit(QString::fromStdString(cfg_.social_twitter));
    edit_twitch_    = new QLineEdit(QString::fromStdString(cfg_.social_twitch));
    edit_instagram_ = new QLineEdit(QString::fromStdString(cfg_.social_instagram));
    edit_tiktok_    = new QLineEdit(QString::fromStdString(cfg_.social_tiktok));
    edit_youtube_   = new QLineEdit(QString::fromStdString(cfg_.social_youtube));
    edit_facebook_  = new QLineEdit(QString::fromStdString(cfg_.social_facebook));
    edit_linkedin_  = new QLineEdit(QString::fromStdString(cfg_.social_linkedin));
    edit_linktree_  = new QLineEdit(QString::fromStdString(cfg_.social_linktree));

    form->addRow(QStringLiteral("Creator Handle:"), edit_creator_);
    form->addRow(QStringLiteral("Emoji:"), edit_emoji_);
    form->addRow(QStringLiteral("Hashtags:"), edit_hashtags_);
    form->addRow(QStringLiteral("Twitter:"), edit_twitter_);
    form->addRow(QStringLiteral("Twitch:"), edit_twitch_);
    form->addRow(QStringLiteral("Instagram:"), edit_instagram_);
    form->addRow(QStringLiteral("TikTok:"), edit_tiktok_);
    form->addRow(QStringLiteral("YouTube:"), edit_youtube_);
    form->addRow(QStringLiteral("Facebook:"), edit_facebook_);
    form->addRow(QStringLiteral("LinkedIn:"), edit_linkedin_);
    form->addRow(QStringLiteral("Linktree:"), edit_linktree_);
    lay->addWidget(grp);
    addButtons(this, lay);
}

void ICY22SocialDialog::accept()
{
    cfg_.creator_handle  = edit_creator_->text().toStdString();
    cfg_.emoji           = edit_emoji_->text().toStdString();
    cfg_.hashtags        = edit_hashtags_->text().toStdString();
    cfg_.social_twitter  = edit_twitter_->text().toStdString();
    cfg_.social_twitch   = edit_twitch_->text().toStdString();
    cfg_.social_instagram = edit_instagram_->text().toStdString();
    cfg_.social_tiktok   = edit_tiktok_->text().toStdString();
    cfg_.social_youtube  = edit_youtube_->text().toStdString();
    cfg_.social_facebook = edit_facebook_->text().toStdString();
    cfg_.social_linkedin = edit_linkedin_->text().toStdString();
    cfg_.social_linktree = edit_linktree_->text().toStdString();
    QDialog::accept();
}

/* ════════════════════════════════════════════════════════════════════
 *  Engagement Dialog
 * ════════════════════════════════════════════════════════════════════ */
ICY22EngageDialog::ICY22EngageDialog(ICY22Config &cfg, QWidget *parent)
    : QDialog(parent), cfg_(cfg)
{
    setWindowTitle(QStringLiteral("Listener Engagement Settings"));
    resize(460, 320);
    auto *lay = new QVBoxLayout(this);

    auto *eng_grp = new QGroupBox(QStringLiteral("Listener Engagement"));
    auto *eng_form = new QFormLayout(eng_grp);
    eng_form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    chk_request_enabled_ = new QCheckBox(QStringLiteral("Requests Enabled"));
    chk_request_enabled_->setChecked(cfg_.request_enabled);
    edit_request_url_ = new QLineEdit(QString::fromStdString(cfg_.request_url));
    edit_chat_url_    = new QLineEdit(QString::fromStdString(cfg_.chat_url));
    edit_tip_url_     = new QLineEdit(QString::fromStdString(cfg_.tip_url));
    edit_events_url_  = new QLineEdit(QString::fromStdString(cfg_.events_url));
    eng_form->addRow(QString(), chk_request_enabled_);
    eng_form->addRow(QStringLiteral("Request URL:"), edit_request_url_);
    eng_form->addRow(QStringLiteral("Chat URL:"), edit_chat_url_);
    eng_form->addRow(QStringLiteral("Tip URL:"), edit_tip_url_);
    eng_form->addRow(QStringLiteral("Events URL:"), edit_events_url_);
    lay->addWidget(eng_grp);

    auto *dist_grp = new QGroupBox(QStringLiteral("Distribution"));
    auto *dist_form = new QFormLayout(dist_grp);
    dist_form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    edit_crosspost_  = new QLineEdit(QString::fromStdString(cfg_.crosspost_platforms));
    edit_cdn_region_ = new QLineEdit(QString::fromStdString(cfg_.cdn_region));
    edit_relay_      = new QLineEdit(QString::fromStdString(cfg_.relay_origin));
    dist_form->addRow(QStringLiteral("Crosspost Platforms:"), edit_crosspost_);
    dist_form->addRow(QStringLiteral("CDN Region:"), edit_cdn_region_);
    dist_form->addRow(QStringLiteral("Relay Origin:"), edit_relay_);
    lay->addWidget(dist_grp);

    addButtons(this, lay);
}

void ICY22EngageDialog::accept()
{
    cfg_.request_enabled     = chk_request_enabled_->isChecked();
    cfg_.request_url         = edit_request_url_->text().toStdString();
    cfg_.chat_url            = edit_chat_url_->text().toStdString();
    cfg_.tip_url             = edit_tip_url_->text().toStdString();
    cfg_.events_url          = edit_events_url_->text().toStdString();
    cfg_.crosspost_platforms = edit_crosspost_->text().toStdString();
    cfg_.cdn_region          = edit_cdn_region_->text().toStdString();
    cfg_.relay_origin        = edit_relay_->text().toStdString();
    QDialog::accept();
}

/* ════════════════════════════════════════════════════════════════════
 *  Compliance & Legal Dialog
 * ════════════════════════════════════════════════════════════════════ */
ICY22ComplyDialog::ICY22ComplyDialog(ICY22Config &cfg, QWidget *parent)
    : QDialog(parent), cfg_(cfg)
{
    setWindowTitle(QStringLiteral("Compliance & Legal Settings"));
    resize(460, 380);
    auto *lay = new QVBoxLayout(this);

    auto *flags_grp = new QGroupBox(QStringLiteral("Content Flags"));
    auto *flags_form = new QFormLayout(flags_grp);
    flags_form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    chk_nsfw_         = new QCheckBox(QStringLiteral("NSFW"));
    chk_nsfw_->setChecked(cfg_.nsfw);
    chk_ai_           = new QCheckBox(QStringLiteral("AI Generated"));
    chk_ai_->setChecked(cfg_.ai_generated);
    chk_royalty_free_ = new QCheckBox(QStringLiteral("Royalty Free"));
    chk_royalty_free_->setChecked(cfg_.royalty_free);
    combo_license_ = new QComboBox;
    combo_license_->addItems({QStringLiteral("all-rights-reserved"),
        QStringLiteral("cc-by"), QStringLiteral("cc-by-sa"),
        QStringLiteral("cc0"), QStringLiteral("pro-licensed")});
    combo_license_->setCurrentText(QString::fromStdString(cfg_.license_type));
    edit_territory_  = new QLineEdit(QString::fromStdString(cfg_.license_territory));
    edit_geo_region_ = new QLineEdit(QString::fromStdString(cfg_.geo_region));
    flags_form->addRow(QString(), chk_nsfw_);
    flags_form->addRow(QString(), chk_ai_);
    flags_form->addRow(QString(), chk_royalty_free_);
    flags_form->addRow(QStringLiteral("License:"), combo_license_);
    flags_form->addRow(QStringLiteral("Territory:"), edit_territory_);
    flags_form->addRow(QStringLiteral("Geo Region:"), edit_geo_region_);
    lay->addWidget(flags_grp);

    auto *notice_grp = new QGroupBox(QStringLiteral("Station Notice"));
    auto *notice_form = new QFormLayout(notice_grp);
    notice_form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    edit_notice_text_    = new QLineEdit(QString::fromStdString(cfg_.notice_text));
    edit_notice_url_     = new QLineEdit(QString::fromStdString(cfg_.notice_url));
    edit_notice_expires_ = new QLineEdit(QString::fromStdString(cfg_.notice_expires));
    notice_form->addRow(QStringLiteral("Notice:"), edit_notice_text_);
    notice_form->addRow(QStringLiteral("URL:"), edit_notice_url_);
    notice_form->addRow(QStringLiteral("Expires:"), edit_notice_expires_);
    lay->addWidget(notice_grp);

    auto *audio_grp = new QGroupBox(QStringLiteral("Audio Technical"));
    auto *audio_form = new QFormLayout(audio_grp);
    edit_loudness_ = new QLineEdit(QString::fromStdString(cfg_.loudness_lufs));
    audio_form->addRow(QStringLiteral("Loudness (LUFS):"), edit_loudness_);
    lay->addWidget(audio_grp);

    addButtons(this, lay);
}

void ICY22ComplyDialog::accept()
{
    cfg_.nsfw            = chk_nsfw_->isChecked();
    cfg_.ai_generated    = chk_ai_->isChecked();
    cfg_.royalty_free    = chk_royalty_free_->isChecked();
    cfg_.license_type    = combo_license_->currentText().toStdString();
    cfg_.license_territory = edit_territory_->text().toStdString();
    cfg_.geo_region      = edit_geo_region_->text().toStdString();
    cfg_.notice_text     = edit_notice_text_->text().toStdString();
    cfg_.notice_url      = edit_notice_url_->text().toStdString();
    cfg_.notice_expires  = edit_notice_expires_->text().toStdString();
    cfg_.loudness_lufs   = edit_loudness_->text().toStdString();
    QDialog::accept();
}

/* ════════════════════════════════════════════════════════════════════
 *  Video / Simulcast Dialog
 * ════════════════════════════════════════════════════════════════════ */
ICY22VideoDialog::ICY22VideoDialog(ICY22Config &cfg, QWidget *parent)
    : QDialog(parent), cfg_(cfg)
{
    setWindowTitle(QStringLiteral("Video / Simulcast Settings"));
    resize(460, 380);
    auto *lay = new QVBoxLayout(this);

    auto *grp = new QGroupBox(QStringLiteral("Video / Simulcast"));
    auto *form = new QFormLayout(grp);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    combo_type_ = new QComboBox;
    combo_type_->addItems({QStringLiteral("live"), QStringLiteral("short"),
        QStringLiteral("clip"), QStringLiteral("trailer"), QStringLiteral("ad")});
    combo_type_->setCurrentText(QString::fromStdString(cfg_.video_type));

    combo_platform_ = new QComboBox;
    combo_platform_->addItems({QStringLiteral("youtube"), QStringLiteral("tiktok"),
        QStringLiteral("twitch"), QStringLiteral("kick"), QStringLiteral("rumble"),
        QStringLiteral("vimeo"), QStringLiteral("custom")});
    combo_platform_->setCurrentText(QString::fromStdString(cfg_.video_platform));

    chk_live_ = new QCheckBox(QStringLiteral("Live"));
    chk_live_->setChecked(cfg_.video_live);
    chk_nsfw_ = new QCheckBox(QStringLiteral("NSFW"));
    chk_nsfw_->setChecked(cfg_.video_nsfw);

    edit_title_      = new QLineEdit(QString::fromStdString(cfg_.video_title));
    edit_link_       = new QLineEdit(QString::fromStdString(cfg_.video_link));
    edit_poster_     = new QLineEdit(QString::fromStdString(cfg_.video_poster));
    edit_codec_      = new QLineEdit(QString::fromStdString(cfg_.video_codec));
    edit_fps_        = new QLineEdit(QString::fromStdString(cfg_.video_fps));
    edit_resolution_ = new QLineEdit(QString::fromStdString(cfg_.video_resolution));
    edit_rating_     = new QLineEdit(QString::fromStdString(cfg_.video_rating));

    form->addRow(QStringLiteral("Type:"), combo_type_);
    form->addRow(QStringLiteral("Platform:"), combo_platform_);
    form->addRow(QString(), chk_live_);
    form->addRow(QString(), chk_nsfw_);
    form->addRow(QStringLiteral("Title:"), edit_title_);
    form->addRow(QStringLiteral("Link:"), edit_link_);
    form->addRow(QStringLiteral("Poster URL:"), edit_poster_);
    form->addRow(QStringLiteral("Codec:"), edit_codec_);
    form->addRow(QStringLiteral("FPS:"), edit_fps_);
    form->addRow(QStringLiteral("Resolution:"), edit_resolution_);
    form->addRow(QStringLiteral("Rating:"), edit_rating_);
    lay->addWidget(grp);

    addButtons(this, lay);
}

void ICY22VideoDialog::accept()
{
    cfg_.video_type       = combo_type_->currentText().toStdString();
    cfg_.video_platform   = combo_platform_->currentText().toStdString();
    cfg_.video_live       = chk_live_->isChecked();
    cfg_.video_nsfw       = chk_nsfw_->isChecked();
    cfg_.video_title      = edit_title_->text().toStdString();
    cfg_.video_link       = edit_link_->text().toStdString();
    cfg_.video_poster     = edit_poster_->text().toStdString();
    cfg_.video_codec      = edit_codec_->text().toStdString();
    cfg_.video_fps        = edit_fps_->text().toStdString();
    cfg_.video_resolution = edit_resolution_->text().toStdString();
    cfg_.video_rating     = edit_rating_->text().toStdString();
    QDialog::accept();
}

/* ════════════════════════════════════════════════════════════════════
 *  ICY 2.2 Settings Tab (Dashboard)
 * ════════════════════════════════════════════════════════════════════ */
ICY22SettingsTab::ICY22SettingsTab(QWidget *parent)
    : QWidget(parent)
{
    auto *top = new QVBoxLayout(this);
    top->setContentsMargins(8, 8, 8, 8);

    auto *info = new QLabel(
        QStringLiteral("ICY 2.2 Extended Metadata — Click each category to configure fields.\n"
                       "Audio technical fields (sample rate, channels, bitrate) are auto-populated from encoding settings."));
    info->setWordWrap(true);
    top->addWidget(info);

    /* 6 category buttons in 2x3 grid */
    auto *grid = new QGridLayout;
    grid->setSpacing(8);

    auto makeBtn = [](const QString &text) {
        auto *b = new QPushButton(text);
        b->setMinimumHeight(40);
        return b;
    };

    auto *btn_station = makeBtn(QStringLiteral("Station && Show >>"));
    auto *btn_dj      = makeBtn(QStringLiteral("DJ && Host >>"));
    auto *btn_social  = makeBtn(QStringLiteral("Social && Discovery >>"));
    auto *btn_engage  = makeBtn(QStringLiteral("Engagement >>"));
    auto *btn_comply  = makeBtn(QStringLiteral("Compliance && Legal >>"));
    auto *btn_video   = makeBtn(QStringLiteral("Video / Simulcast >>"));

    grid->addWidget(btn_station, 0, 0);
    grid->addWidget(btn_dj,      0, 1);
    grid->addWidget(btn_social,  1, 0);
    grid->addWidget(btn_engage,  1, 1);
    grid->addWidget(btn_comply,  2, 0);
    grid->addWidget(btn_video,   2, 1);
    top->addLayout(grid);

    /* Session ID */
    auto *sess_grp = new QGroupBox(QStringLiteral("Broadcast Session"));
    auto *sess_lay = new QHBoxLayout(sess_grp);
    edit_session_id_ = new QLineEdit;
    edit_session_id_->setPlaceholderText(QStringLiteral("UUID (auto-generated if empty)"));
    auto *btn_gen = new QPushButton(QStringLiteral("Generate"));
    sess_lay->addWidget(new QLabel(QStringLiteral("Session ID:")));
    sess_lay->addWidget(edit_session_id_, 1);
    sess_lay->addWidget(btn_gen);
    top->addWidget(sess_grp);

    top->addStretch();

    /* Signals */
    connect(btn_station, &QPushButton::clicked, this, &ICY22SettingsTab::onStation);
    connect(btn_dj,      &QPushButton::clicked, this, &ICY22SettingsTab::onDJ);
    connect(btn_social,  &QPushButton::clicked, this, &ICY22SettingsTab::onSocial);
    connect(btn_engage,  &QPushButton::clicked, this, &ICY22SettingsTab::onEngage);
    connect(btn_comply,  &QPushButton::clicked, this, &ICY22SettingsTab::onComply);
    connect(btn_video,   &QPushButton::clicked, this, &ICY22SettingsTab::onVideo);
    connect(btn_gen,     &QPushButton::clicked, this, &ICY22SettingsTab::onGenerateSession);
}

void ICY22SettingsTab::onStation()  { ICY22StationDialog dlg(cfg_, this); dlg.exec(); }
void ICY22SettingsTab::onDJ()      { ICY22DJDialog dlg(cfg_, this);      dlg.exec(); }
void ICY22SettingsTab::onSocial()  { ICY22SocialDialog dlg(cfg_, this);  dlg.exec(); }
void ICY22SettingsTab::onEngage()  { ICY22EngageDialog dlg(cfg_, this);  dlg.exec(); }
void ICY22SettingsTab::onComply()  { ICY22ComplyDialog dlg(cfg_, this);  dlg.exec(); }
void ICY22SettingsTab::onVideo()   { ICY22VideoDialog dlg(cfg_, this);   dlg.exec(); }

void ICY22SettingsTab::onGenerateSession()
{
    edit_session_id_->setText(QUuid::createUuid().toString(QUuid::WithBraces));
}

void ICY22SettingsTab::loadFromConfig(const EncoderConfig &cfg)
{
    cfg_ = cfg.icy22;
    edit_session_id_->setText(QString::fromStdString(cfg_.session_id));
}

void ICY22SettingsTab::saveToConfig(EncoderConfig &cfg) const
{
    cfg.icy22 = cfg_;
    cfg.icy22.session_id = edit_session_id_->text().toStdString();
}

} // namespace mc1
