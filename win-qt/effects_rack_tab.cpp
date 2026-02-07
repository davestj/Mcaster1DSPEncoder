/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * effects_rack_tab.cpp — Per-encoder DSP effects rack tab for ConfigDialog
 *
 * Displays DSP plugins in signal-chain order with enable checkboxes.
 * Unlike the global DspEffectsRack, this tab saves toggles into
 * EncoderConfig for per-encoder YAML persistence.
 *
 * Processing order:
 *   10-Band EQ → 31-Band EQ → Sonic Enhancer → PTT Duck → AGC → DBX Voice
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "effects_rack_tab.h"
#include "global_config_manager.h"

#include <QDebug>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QGroupBox>
#include <QIcon>
#include <QFont>

namespace mc1 {

struct PluginDef {
    const char *name;
    const char *icon;
    const char *description;
};

static const PluginDef kPlugins[] = {
    { "10-Band Parametric EQ",       ":/icons/equalizer.svg",
      "Studio-grade parametric EQ with adjustable frequency, gain, and Q per band" },
    { "31-Band Graphic EQ",          ":/icons/equalizer.svg",
      "ISO 1/3-octave graphic equalizer from 20 Hz to 20 kHz" },
    { "Sonic Enhancer",              ":/icons/sonic.svg",
      "BBE Sonic Maximizer-style exciter — adds presence, clarity, and bass punch" },
    { "Push-to-Talk Duck",           ":/icons/ptt-duck.svg",
      "Ducks main audio when PTT mic is active — talkover/voiceover mode" },
    { "AGC / Compressor / Limiter",  ":/icons/agc.svg",
      "Automatic gain control with look-ahead limiter for broadcast loudness" },
    { "DBX 286s Voice Processor",    ":/icons/dbx-voice.svg",
      "Channel-strip emulation: compressor, de-esser, enhancer, expander/gate" },
};
static constexpr int kNumPlugins = 6;

EffectsRackTab::EffectsRackTab(QWidget *parent)
    : QWidget(parent)
{
    buildUI();
}

void EffectsRackTab::buildUI()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget;
    auto *vbox    = new QVBoxLayout(content);
    vbox->setSpacing(4);
    vbox->setContentsMargins(8, 8, 8, 8);

    auto *hdr = new QLabel(QStringLiteral(
        "Select which DSP effects to apply to this encoder's audio chain.\n"
        "Plugins process audio in order from top to bottom."));
    hdr->setWordWrap(true);
    auto hdr_font = hdr->font();
    hdr_font.setPointSize(11);
    hdr->setFont(hdr_font);
    hdr->setStyleSheet(QStringLiteral("color: #8899aa; padding: 2px 0 6px 0;"));
    vbox->addWidget(hdr);

    rows_.resize(kNumPlugins);

    for (int i = 0; i < kNumPlugins; ++i) {
        const auto &pd = kPlugins[i];

        auto *group = new QGroupBox;
        group->setStyleSheet(QStringLiteral(
            "QGroupBox { "
            "  border: 1px solid #1a2a4c; "
            "  border-left: 3px solid #00d4aa; "
            "  border-radius: 4px; "
            "  padding: 10px; margin: 3px 0; "
            "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
            "    stop:0 rgba(20,25,40,0.9), stop:1 rgba(10,15,30,0.9)); }"));
        auto *row_lay = new QHBoxLayout(group);
        row_lay->setSpacing(10);

        /* Rack unit number */
        auto *lbl_unit = new QLabel(QString::number(i + 1) + QStringLiteral("U"));
        lbl_unit->setFixedWidth(24);
        lbl_unit->setAlignment(Qt::AlignCenter);
        lbl_unit->setStyleSheet(QStringLiteral(
            "color: #4a6a8c; font-size: 9px; font-weight: bold; "
            "background: transparent; border: none;"));
        row_lay->addWidget(lbl_unit);

        auto *chk = new QCheckBox;
        chk->setToolTip(QStringLiteral("Enable / bypass this plugin"));
        chk->setStyleSheet(QStringLiteral(
            "QCheckBox { background: transparent; spacing: 4px; }"
            "QCheckBox::indicator { width: 18px; height: 18px; "
            "  border: 2px solid #4a6a8c; border-radius: 3px; "
            "  background: rgba(20,30,50,0.8); }"
            "QCheckBox::indicator:checked { "
            "  background: #00d4aa; border-color: #00d4aa; "
            "  image: none; }"
            "QCheckBox::indicator:hover { border-color: #00d4aa; }"));
        row_lay->addWidget(chk);

        auto *icon_lbl = new QLabel;
        icon_lbl->setPixmap(QIcon(QString::fromUtf8(pd.icon)).pixmap(24, 24));
        icon_lbl->setFixedSize(28, 28);
        row_lay->addWidget(icon_lbl);

        auto *text_col = new QVBoxLayout;
        text_col->setSpacing(1);

        auto *lbl_name = new QLabel(QString::fromUtf8(pd.name));
        auto name_font = lbl_name->font();
        name_font.setPointSize(12);
        name_font.setBold(true);
        lbl_name->setFont(name_font);
        lbl_name->setStyleSheet(QStringLiteral("color: #d0e0f0; background: transparent;"));
        text_col->addWidget(lbl_name);

        auto *lbl_desc = new QLabel(QString::fromUtf8(pd.description));
        lbl_desc->setWordWrap(true);
        auto desc_font = lbl_desc->font();
        desc_font.setPointSize(10);
        lbl_desc->setFont(desc_font);
        lbl_desc->setStyleSheet(QStringLiteral("color: #7788aa; background: transparent;"));
        text_col->addWidget(lbl_desc);

        row_lay->addLayout(text_col, 1);

        /* Configure button — opens per-encoder DSP dialog */
        auto *btn_cfg = new QPushButton(QStringLiteral("Configure\u2026"));
        btn_cfg->setFixedWidth(100);
        btn_cfg->setToolTip(QStringLiteral("Configure this plugin for this encoder"));
        btn_cfg->setStyleSheet(QStringLiteral(
            "QPushButton { background: #1a2a4c; color: #c0d0e8; padding: 5px 8px; "
            "border: 1px solid #2a3a6c; border-radius: 4px; font-size: 11px; }"
            "QPushButton:hover { background: #2a3a6c; border-color: #00d4aa; }"
            "QPushButton:disabled { color: #4a5a6c; background: #0e1a2c; border-color: #1a2a3c; }"));
        row_lay->addWidget(btn_cfg);

        /* LED status indicator */
        auto *led = new QLabel;
        led->setFixedSize(10, 10);
        led->setStyleSheet(QStringLiteral(
            "background: #3a4a5c; border-radius: 5px; border: 1px solid #2a3a4c;"));
        row_lay->addWidget(led);

        auto *lbl_status = new QLabel(QStringLiteral("Bypassed"));
        lbl_status->setFixedWidth(70);
        lbl_status->setAlignment(Qt::AlignCenter);
        lbl_status->setStyleSheet(QStringLiteral(
            "color: #667788; font-size: 11px; font-weight: bold; background: transparent;"));
        row_lay->addWidget(lbl_status);

        rows_[i] = { chk, lbl_name, lbl_status, btn_cfg, led };
        vbox->addWidget(group);

        connect(chk, &QCheckBox::toggled, this, [this, i](bool on) {
            rows_[i].lbl_status->setText(on
                ? QStringLiteral("Active")
                : QStringLiteral("Bypassed"));
            rows_[i].lbl_status->setStyleSheet(on
                ? QStringLiteral("color: #22cc66; font-size: 11px; font-weight: bold; background: transparent;")
                : QStringLiteral("color: #667788; font-size: 11px; font-weight: bold; background: transparent;"));
            rows_[i].led->setStyleSheet(on
                ? QStringLiteral("background: #22cc66; border-radius: 5px; border: 1px solid #1a9a4a;")
                : QStringLiteral("background: #3a4a5c; border-radius: 5px; border: 1px solid #2a3a4c;"));

            /* cfg_snapshot_ already holds params from loadFromConfig() or
             * from Configure dialogs — do NOT overwrite with global defaults
             * as that would destroy any custom per-encoder settings. */
        });
    }

    /* Wire Configure button signals */
    connect(rows_[0].btn_configure, &QPushButton::clicked, this, &EffectsRackTab::openEq10);
    connect(rows_[1].btn_configure, &QPushButton::clicked, this, &EffectsRackTab::openEq31);
    connect(rows_[2].btn_configure, &QPushButton::clicked, this, &EffectsRackTab::openSonic);
    connect(rows_[3].btn_configure, &QPushButton::clicked, this, &EffectsRackTab::openPttDuck);
    connect(rows_[4].btn_configure, &QPushButton::clicked, this, &EffectsRackTab::openAgc);
    connect(rows_[5].btn_configure, &QPushButton::clicked, this, &EffectsRackTab::openDbxVoice);

    vbox->addStretch();
    scroll->setWidget(content);
    outer->addWidget(scroll);
}

void EffectsRackTab::loadFromConfig(const EncoderConfig &cfg)
{
    cfg_snapshot_ = cfg;

    qDebug() << "[EffectsRack] loadFromConfig: eq_enabled=" << cfg.dsp_eq_enabled
             << "eq_mode=" << cfg.dsp_eq_mode
             << "agc=" << cfg.dsp_agc_enabled
             << "sonic=" << cfg.dsp_sonic_enabled
             << "dbx=" << cfg.dsp_dbx_voice_enabled
             << "ptt=" << cfg.dsp_ptt_duck_enabled;

    /* Block signals to prevent toggle lambda from overwriting cfg_snapshot_
     * with global defaults during initial load */
    bool checks[kNumPlugins] = {
        cfg.dsp_eq_enabled && cfg.dsp_eq_mode == 0,   /* 0: 10-Band EQ */
        cfg.dsp_eq_enabled && cfg.dsp_eq_mode == 1,   /* 1: 31-Band EQ */
        cfg.dsp_sonic_enabled,                         /* 2: Sonic Enhancer */
        cfg.dsp_ptt_duck_enabled,                      /* 3: PTT Duck */
        cfg.dsp_agc_enabled,                           /* 4: AGC */
        cfg.dsp_dbx_voice_enabled                      /* 5: DBX Voice */
    };

    for (int i = 0; i < kNumPlugins; ++i) {
        rows_[i].chk_enable->blockSignals(true);
        rows_[i].chk_enable->setChecked(checks[i]);
        rows_[i].chk_enable->blockSignals(false);

        /* Update visual state to match */
        rows_[i].lbl_status->setText(checks[i]
            ? QStringLiteral("Active")
            : QStringLiteral("Bypassed"));
        rows_[i].lbl_status->setStyleSheet(checks[i]
            ? QStringLiteral("color: #22cc66; font-size: 11px; font-weight: bold; background: transparent;")
            : QStringLiteral("color: #667788; font-size: 11px; font-weight: bold; background: transparent;"));
        rows_[i].led->setStyleSheet(checks[i]
            ? QStringLiteral("background: #22cc66; border-radius: 5px; border: 1px solid #1a9a4a;")
            : QStringLiteral("background: #3a4a5c; border-radius: 5px; border: 1px solid #2a3a4c;"));
    }
}

void EffectsRackTab::saveToConfig(EncoderConfig &cfg) const
{
    bool eq10 = rows_[0].chk_enable->isChecked();
    bool eq31 = rows_[1].chk_enable->isChecked();

    qDebug() << "[EffectsRack] saveToConfig: eq10_chk=" << eq10
             << "eq31_chk=" << eq31
             << "sonic_chk=" << rows_[2].chk_enable->isChecked()
             << "ptt_chk=" << rows_[3].chk_enable->isChecked()
             << "agc_chk=" << rows_[4].chk_enable->isChecked()
             << "dbx_chk=" << rows_[5].chk_enable->isChecked();

    /* If both checked, 31-band takes priority */
    if (eq31) {
        cfg.dsp_eq_enabled = true;
        cfg.dsp_eq_mode = 1;
    } else if (eq10) {
        cfg.dsp_eq_enabled = true;
        cfg.dsp_eq_mode = 0;
    } else {
        cfg.dsp_eq_enabled = false;
    }

    cfg.dsp_sonic_enabled     = rows_[2].chk_enable->isChecked();
    cfg.dsp_ptt_duck_enabled  = rows_[3].chk_enable->isChecked();
    cfg.dsp_agc_enabled       = rows_[4].chk_enable->isChecked();
    cfg.dsp_dbx_voice_enabled = rows_[5].chk_enable->isChecked();

    /* Persist DSP parameter structs from the snapshot */
    cfg.agc_params        = cfg_snapshot_.agc_params;
    cfg.sonic_params      = cfg_snapshot_.sonic_params;
    cfg.ptt_duck_params   = cfg_snapshot_.ptt_duck_params;
    cfg.dbx_voice_params  = cfg_snapshot_.dbx_voice_params;
    cfg.eq10_params       = cfg_snapshot_.eq10_params;
    cfg.eq31_params       = cfg_snapshot_.eq31_params;
}

} // namespace mc1
