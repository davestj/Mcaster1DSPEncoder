/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * icy22_settings_tab.h — ICY 2.2 Extended Settings tab + 6 popup dialogs
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_ICY22_SETTINGS_TAB_H
#define MC1_ICY22_SETTINGS_TAB_H

#include <QWidget>
#include <QDialog>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include "config_types.h"

namespace mc1 {

/* ── Popup Dialog: Station & Show ──────────────────────────────── */
class ICY22StationDialog : public QDialog {
    Q_OBJECT
public:
    explicit ICY22StationDialog(ICY22Config &cfg, QWidget *parent = nullptr);
private:
    ICY22Config &cfg_;
    QLineEdit *edit_station_id_, *edit_station_logo_, *edit_show_title_;
    QLineEdit *edit_show_start_, *edit_show_end_, *edit_next_show_;
    QLineEdit *edit_next_show_time_, *edit_schedule_url_, *edit_playlist_name_;
    QComboBox *combo_verify_;
    QCheckBox *chk_auto_dj_;
    void accept() override;
};

/* ── Popup Dialog: DJ & Host ───────────────────────────────────── */
class ICY22DJDialog : public QDialog {
    Q_OBJECT
public:
    explicit ICY22DJDialog(ICY22Config &cfg, QWidget *parent = nullptr);
private:
    ICY22Config &cfg_;
    QLineEdit *edit_handle_, *edit_bio_, *edit_genre_, *edit_rating_;
    void accept() override;
};

/* ── Popup Dialog: Social & Discovery ──────────────────────────── */
class ICY22SocialDialog : public QDialog {
    Q_OBJECT
public:
    explicit ICY22SocialDialog(ICY22Config &cfg, QWidget *parent = nullptr);
private:
    ICY22Config &cfg_;
    QLineEdit *edit_creator_, *edit_emoji_, *edit_hashtags_;
    QLineEdit *edit_twitter_, *edit_twitch_, *edit_instagram_;
    QLineEdit *edit_tiktok_, *edit_youtube_, *edit_facebook_;
    QLineEdit *edit_linkedin_, *edit_linktree_;
    void accept() override;
};

/* ── Popup Dialog: Engagement ──────────────────────────────────── */
class ICY22EngageDialog : public QDialog {
    Q_OBJECT
public:
    explicit ICY22EngageDialog(ICY22Config &cfg, QWidget *parent = nullptr);
private:
    ICY22Config &cfg_;
    QCheckBox *chk_request_enabled_;
    QLineEdit *edit_request_url_, *edit_chat_url_, *edit_tip_url_;
    QLineEdit *edit_events_url_, *edit_crosspost_, *edit_cdn_region_, *edit_relay_;
    void accept() override;
};

/* ── Popup Dialog: Compliance & Legal ──────────────────────────── */
class ICY22ComplyDialog : public QDialog {
    Q_OBJECT
public:
    explicit ICY22ComplyDialog(ICY22Config &cfg, QWidget *parent = nullptr);
private:
    ICY22Config &cfg_;
    QCheckBox *chk_nsfw_, *chk_ai_, *chk_royalty_free_;
    QComboBox *combo_license_;
    QLineEdit *edit_territory_, *edit_geo_region_;
    QLineEdit *edit_notice_text_, *edit_notice_url_, *edit_notice_expires_;
    QLineEdit *edit_loudness_;
    void accept() override;
};

/* ── Popup Dialog: Video / Simulcast ───────────────────────────── */
class ICY22VideoDialog : public QDialog {
    Q_OBJECT
public:
    explicit ICY22VideoDialog(ICY22Config &cfg, QWidget *parent = nullptr);
private:
    ICY22Config &cfg_;
    QComboBox *combo_type_, *combo_platform_;
    QCheckBox *chk_live_, *chk_nsfw_;
    QLineEdit *edit_title_, *edit_link_, *edit_poster_;
    QLineEdit *edit_codec_, *edit_fps_, *edit_resolution_, *edit_rating_;
    void accept() override;
};

/* ── Main ICY 2.2 Tab (dashboard with 6 category buttons) ─────── */
class ICY22SettingsTab : public QWidget {
    Q_OBJECT

public:
    explicit ICY22SettingsTab(QWidget *parent = nullptr);

    void loadFromConfig(const EncoderConfig &cfg);
    void saveToConfig(EncoderConfig &cfg) const;

private Q_SLOTS:
    void onStation();
    void onDJ();
    void onSocial();
    void onEngage();
    void onComply();
    void onVideo();
    void onGenerateSession();

private:
    ICY22Config cfg_;
    QLineEdit *edit_session_id_;
};

} // namespace mc1

#endif // MC1_ICY22_SETTINGS_TAB_H
