/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * yp_settings_tab.cpp — YP (Yellow Pages) Settings tab
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yp_settings_tab.h"
#include <QFormLayout>
#include <QGroupBox>
#include <QVBoxLayout>

namespace mc1 {

YPSettingsTab::YPSettingsTab(QWidget *parent)
    : QWidget(parent)
{
    auto *top = new QVBoxLayout(this);
    top->setContentsMargins(8, 8, 8, 8);

    auto *grp = new QGroupBox(QStringLiteral("YP Settings — Stream Directory Listing"));
    auto *form = new QFormLayout(grp);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    chk_public_ = new QCheckBox(QStringLiteral("Public Server (register on YP directory)"));
    form->addRow(QString(), chk_public_);

    edit_stream_name_ = new QLineEdit;
    edit_stream_name_->setPlaceholderText(QStringLiteral("My Radio Station"));
    form->addRow(QStringLiteral("Stream Name:"), edit_stream_name_);

    edit_stream_desc_ = new QLineEdit;
    edit_stream_desc_->setPlaceholderText(QStringLiteral("The best music around the clock"));
    form->addRow(QStringLiteral("Description:"), edit_stream_desc_);

    edit_stream_url_ = new QLineEdit;
    edit_stream_url_->setPlaceholderText(QStringLiteral("https://www.mystation.com"));
    form->addRow(QStringLiteral("URL:"), edit_stream_url_);

    edit_stream_genre_ = new QLineEdit;
    edit_stream_genre_->setPlaceholderText(QStringLiteral("Rock, Pop, Alternative"));
    form->addRow(QStringLiteral("Genre:"), edit_stream_genre_);

    edit_icq_ = new QLineEdit;
    form->addRow(QStringLiteral("ICQ #:"), edit_icq_);

    edit_aim_ = new QLineEdit;
    form->addRow(QStringLiteral("AIM:"), edit_aim_);

    edit_irc_ = new QLineEdit;
    edit_irc_->setPlaceholderText(QStringLiteral("#mychannel"));
    form->addRow(QStringLiteral("IRC:"), edit_irc_);

    top->addWidget(grp);
    top->addStretch();
}

void YPSettingsTab::loadFromConfig(const EncoderConfig &cfg)
{
    chk_public_->setChecked(cfg.yp.is_public);
    edit_stream_name_->setText(QString::fromStdString(cfg.yp.stream_name));
    edit_stream_desc_->setText(QString::fromStdString(cfg.yp.stream_desc));
    edit_stream_url_->setText(QString::fromStdString(cfg.yp.stream_url));
    edit_stream_genre_->setText(QString::fromStdString(cfg.yp.stream_genre));
    edit_icq_->setText(QString::fromStdString(cfg.yp.icq));
    edit_aim_->setText(QString::fromStdString(cfg.yp.aim));
    edit_irc_->setText(QString::fromStdString(cfg.yp.irc));
}

void YPSettingsTab::saveToConfig(EncoderConfig &cfg) const
{
    cfg.yp.is_public    = chk_public_->isChecked();
    cfg.yp.stream_name  = edit_stream_name_->text().toStdString();
    cfg.yp.stream_desc  = edit_stream_desc_->text().toStdString();
    cfg.yp.stream_url   = edit_stream_url_->text().toStdString();
    cfg.yp.stream_genre = edit_stream_genre_->text().toStdString();
    cfg.yp.icq          = edit_icq_->text().toStdString();
    cfg.yp.aim          = edit_aim_->text().toStdString();
    cfg.yp.irc          = edit_irc_->text().toStdString();
}

} // namespace mc1
