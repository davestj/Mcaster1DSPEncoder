/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * preset_browser_dialog.cpp — Encoder preset browser implementation
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "preset_browser_dialog.h"
#include "encoder_presets.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace mc1 {

static QString codecName(EncoderConfig::Codec c) {
    switch (c) {
    case EncoderConfig::Codec::MP3:       return QStringLiteral("MP3");
    case EncoderConfig::Codec::VORBIS:    return QStringLiteral("Vorbis");
    case EncoderConfig::Codec::OPUS:      return QStringLiteral("Opus");
    case EncoderConfig::Codec::FLAC:      return QStringLiteral("FLAC");
    case EncoderConfig::Codec::AAC_LC:    return QStringLiteral("AAC-LC");
    case EncoderConfig::Codec::AAC_HE:    return QStringLiteral("AAC-HE");
    case EncoderConfig::Codec::AAC_HE_V2: return QStringLiteral("AAC-HEv2");
    case EncoderConfig::Codec::AAC_ELD:   return QStringLiteral("AAC-ELD");
    default: return QStringLiteral("Unknown");
    }
}

PresetBrowserDialog::PresetBrowserDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Encoder Preset Browser"));
    setMinimumSize(600, 400);
    resize(700, 480);

    auto *root = new QHBoxLayout(this);

    /* Left: preset list */
    auto *left = new QVBoxLayout;
    list_presets_ = new QListWidget;
    list_presets_->setMinimumWidth(220);
    connect(list_presets_, &QListWidget::currentRowChanged,
            this, &PresetBrowserDialog::onPresetClicked);
    left->addWidget(list_presets_);

    /* Buttons below list */
    auto *btn_row = new QHBoxLayout;
    btn_save_ = new QPushButton(QStringLiteral("Save Current"));
    btn_save_->setToolTip(QStringLiteral("Save current encoder config as a custom preset"));
    connect(btn_save_, &QPushButton::clicked, this, &PresetBrowserDialog::onSaveCustom);
    btn_row->addWidget(btn_save_);

    btn_delete_ = new QPushButton(QStringLiteral("Delete"));
    btn_delete_->setEnabled(false);
    connect(btn_delete_, &QPushButton::clicked, this, &PresetBrowserDialog::onDeleteCustom);
    btn_row->addWidget(btn_delete_);
    left->addLayout(btn_row);

    root->addLayout(left);

    /* Right: details panel */
    auto *right = new QVBoxLayout;
    right->setSpacing(8);

    auto addDetail = [&](const QString &label) -> QLabel* {
        auto *row = new QHBoxLayout;
        auto *lbl = new QLabel(label);
        lbl->setStyleSheet(QStringLiteral("font-weight: bold;"));
        lbl->setFixedWidth(80);
        auto *val = new QLabel(QStringLiteral("—"));
        row->addWidget(lbl);
        row->addWidget(val, 1);
        right->addLayout(row);
        return val;
    };

    lbl_codec_   = addDetail(QStringLiteral("Codec:"));
    lbl_bitrate_ = addDetail(QStringLiteral("Bitrate:"));
    lbl_sr_      = addDetail(QStringLiteral("Sample Rate:"));
    lbl_ch_      = addDetail(QStringLiteral("Channels:"));

    right->addSpacing(8);
    lbl_desc_ = new QLabel;
    lbl_desc_->setWordWrap(true);
    lbl_desc_->setStyleSheet(QStringLiteral("color: #888;"));
    right->addWidget(lbl_desc_);

    right->addStretch();

    btn_apply_ = new QPushButton(QStringLiteral("Apply to Selected Encoder"));
    btn_apply_->setEnabled(false);
    connect(btn_apply_, &QPushButton::clicked, this, &PresetBrowserDialog::onApply);
    right->addWidget(btn_apply_);

    root->addLayout(right, 1);

    populateList();
}

void PresetBrowserDialog::setCurrentConfig(const EncoderConfig &cfg)
{
    current_cfg_ = cfg;
}

void PresetBrowserDialog::populateList()
{
    list_presets_->clear();

    /* Built-in presets */
    const auto &presets = builtinPresets();
    builtin_count_ = presets.size();
    for (const auto &p : presets) {
        list_presets_->addItem(p.name);
    }

    /* Custom presets from QSettings */
    QSettings s;
    int count = s.beginReadArray(QStringLiteral("customEncoderPresets"));
    for (int i = 0; i < count; ++i) {
        s.setArrayIndex(i);
        QString name = s.value(QStringLiteral("name")).toString();
        list_presets_->addItem(QStringLiteral("[Custom] ") + name);
    }
    s.endArray();
}

void PresetBrowserDialog::onPresetClicked(int row)
{
    if (row < 0) return;
    showPresetDetails(row);
    btn_apply_->setEnabled(true);
    btn_delete_->setEnabled(row >= builtin_count_);
}

void PresetBrowserDialog::showPresetDetails(int row)
{
    if (row < builtin_count_) {
        /* Built-in preset */
        const auto &presets = builtinPresets();
        if (row < 0 || row >= presets.size()) return;
        const auto &p = presets[row];
        lbl_codec_->setText(codecName(p.codec));
        lbl_bitrate_->setText(p.bitrate_kbps > 0
            ? QString::number(p.bitrate_kbps) + QStringLiteral(" kbps")
            : QStringLiteral("VBR / Lossless"));
        lbl_sr_->setText(QString::number(p.sample_rate) + QStringLiteral(" Hz"));
        lbl_ch_->setText(p.channels == 1 ? QStringLiteral("Mono")
                                         : QStringLiteral("Stereo"));
        lbl_desc_->setText(p.description);
    } else {
        /* Custom preset from QSettings */
        int ci = row - builtin_count_;
        QSettings s;
        s.beginReadArray(QStringLiteral("customEncoderPresets"));
        s.setArrayIndex(ci);
        lbl_codec_->setText(s.value(QStringLiteral("codec")).toString());
        lbl_bitrate_->setText(s.value(QStringLiteral("bitrate")).toString() +
                              QStringLiteral(" kbps"));
        lbl_sr_->setText(s.value(QStringLiteral("sampleRate")).toString() +
                         QStringLiteral(" Hz"));
        lbl_ch_->setText(s.value(QStringLiteral("channels")).toInt() == 1
            ? QStringLiteral("Mono") : QStringLiteral("Stereo"));
        lbl_desc_->setText(s.value(QStringLiteral("description"),
            QStringLiteral("Custom user preset")).toString());
        s.endArray();
    }
}

void PresetBrowserDialog::onApply()
{
    int row = list_presets_->currentRow();
    if (row < 0) return;

    EncoderConfig cfg = current_cfg_;
    if (row < builtin_count_) {
        const auto &presets = builtinPresets();
        applyPreset(presets[row], cfg);
    } else {
        int ci = row - builtin_count_;
        QSettings s;
        s.beginReadArray(QStringLiteral("customEncoderPresets"));
        s.setArrayIndex(ci);
        cfg.codec = static_cast<EncoderConfig::Codec>(
            s.value(QStringLiteral("codecEnum")).toInt());
        cfg.bitrate_kbps = s.value(QStringLiteral("bitrate")).toInt();
        cfg.sample_rate  = s.value(QStringLiteral("sampleRate")).toInt();
        cfg.channels     = s.value(QStringLiteral("channels")).toInt();
        s.endArray();
    }

    Q_EMIT presetSelected(cfg);
}

void PresetBrowserDialog::onSaveCustom()
{
    bool ok = false;
    QString name = QInputDialog::getText(
        this, QStringLiteral("Save Custom Preset"),
        QStringLiteral("Preset name:"),
        QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    name = name.trimmed();

    QSettings s;
    int count = s.beginReadArray(QStringLiteral("customEncoderPresets"));
    s.endArray();

    s.beginWriteArray(QStringLiteral("customEncoderPresets"), count + 1);
    s.setArrayIndex(count);
    s.setValue(QStringLiteral("name"), name);
    s.setValue(QStringLiteral("codec"), codecName(current_cfg_.codec));
    s.setValue(QStringLiteral("codecEnum"), static_cast<int>(current_cfg_.codec));
    s.setValue(QStringLiteral("bitrate"), current_cfg_.bitrate_kbps);
    s.setValue(QStringLiteral("sampleRate"), current_cfg_.sample_rate);
    s.setValue(QStringLiteral("channels"), current_cfg_.channels);
    s.setValue(QStringLiteral("description"),
        QStringLiteral("Custom preset: ") + name);
    s.endArray();

    populateList();
}

void PresetBrowserDialog::onDeleteCustom()
{
    int row = list_presets_->currentRow();
    if (row < builtin_count_) return;
    int ci = row - builtin_count_;

    QSettings s;
    int count = s.beginReadArray(QStringLiteral("customEncoderPresets"));
    /* Read all entries except the one to delete */
    struct Entry {
        QString name, codec, description;
        int codecEnum, bitrate, sampleRate, channels;
    };
    QVector<Entry> kept;
    for (int i = 0; i < count; ++i) {
        if (i == ci) continue;
        s.setArrayIndex(i);
        Entry e;
        e.name        = s.value(QStringLiteral("name")).toString();
        e.codec       = s.value(QStringLiteral("codec")).toString();
        e.codecEnum   = s.value(QStringLiteral("codecEnum")).toInt();
        e.bitrate     = s.value(QStringLiteral("bitrate")).toInt();
        e.sampleRate  = s.value(QStringLiteral("sampleRate")).toInt();
        e.channels    = s.value(QStringLiteral("channels")).toInt();
        e.description = s.value(QStringLiteral("description")).toString();
        kept.append(e);
    }
    s.endArray();

    s.beginWriteArray(QStringLiteral("customEncoderPresets"), kept.size());
    for (int i = 0; i < kept.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("name"),        kept[i].name);
        s.setValue(QStringLiteral("codec"),       kept[i].codec);
        s.setValue(QStringLiteral("codecEnum"),   kept[i].codecEnum);
        s.setValue(QStringLiteral("bitrate"),     kept[i].bitrate);
        s.setValue(QStringLiteral("sampleRate"),  kept[i].sampleRate);
        s.setValue(QStringLiteral("channels"),    kept[i].channels);
        s.setValue(QStringLiteral("description"), kept[i].description);
    }
    s.endArray();

    populateList();
}

} // namespace mc1
