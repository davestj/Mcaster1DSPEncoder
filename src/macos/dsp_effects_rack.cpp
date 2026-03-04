/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * dsp_effects_rack.cpp — DSP Effects Rack tab (4th tab in encoder list)
 *
 * Displays all DSP plugins in signal-chain order with
 * enable checkboxes, status indicators, and configure buttons.
 *
 * Processing order:
 *   10-Band EQ → 31-Band EQ → Sonic Enhancer → PTT Duck → AGC → DBX Voice
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dsp_effects_rack.h"
#include "audio_pipeline.h"
#include "global_config_manager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QGroupBox>
#include <QIcon>
#include <QFont>

namespace mc1 {

/* ── Plugin definitions in DSP chain order ── */
struct PluginDef {
    const char *name;
    const char *icon;
    const char *description;
    int signal_index;  // 0-5 maps to the 6 open* signals
};

static const PluginDef kPlugins[] = {
    { "10-Band Parametric EQ",       ":/icons/equalizer.svg",
      "Studio-grade parametric EQ with adjustable frequency, gain, and Q per band", 0 },
    { "31-Band Graphic EQ",          ":/icons/equalizer.svg",
      "ISO 1/3-octave graphic equalizer from 20 Hz to 20 kHz",                     1 },
    { "Sonic Enhancer",              ":/icons/sonic.svg",
      "BBE Sonic Maximizer-style exciter — adds presence, clarity, and bass punch", 2 },
    { "Push-to-Talk Duck",           ":/icons/ptt-duck.svg",
      "Ducks main audio when PTT mic is active — talkover/voiceover mode",          3 },
    { "AGC / Compressor / Limiter",  ":/icons/agc.svg",
      "Automatic gain control with look-ahead limiter for broadcast loudness",      4 },
    { "DBX 286s Voice Processor",    ":/icons/dbx-voice.svg",
      "Channel-strip emulation: compressor, de-esser, enhancer, expander/gate",     5 },
};
static constexpr int kNumPlugins = 6;

DspEffectsRack::DspEffectsRack(QWidget *parent)
    : QWidget(parent)
{
    buildUI();
}

void DspEffectsRack::buildUI()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    /* Scroll area wraps everything */
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget;
    auto *vbox    = new QVBoxLayout(content);
    vbox->setSpacing(4);
    vbox->setContentsMargins(8, 8, 8, 8);

    /* Header label */
    auto *hdr = new QLabel(QStringLiteral(
        "DSP signal chain — plugins process audio in order from top to bottom."));
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

        /* Enable checkbox */
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

        /* Icon */
        auto *icon_lbl = new QLabel;
        icon_lbl->setPixmap(QIcon(QString::fromUtf8(pd.icon)).pixmap(24, 24));
        icon_lbl->setFixedSize(28, 28);
        row_lay->addWidget(icon_lbl);

        /* Name + description column */
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

        /* LED status indicator */
        auto *led = new QLabel;
        led->setFixedSize(10, 10);
        led->setStyleSheet(QStringLiteral(
            "background: #3a4a5c; border-radius: 5px; border: 1px solid #2a3a4c;"));
        row_lay->addWidget(led);

        /* Status label */
        auto *lbl_status = new QLabel(QStringLiteral("Bypassed"));
        lbl_status->setFixedWidth(70);
        lbl_status->setAlignment(Qt::AlignCenter);
        lbl_status->setStyleSheet(QStringLiteral(
            "color: #667788; font-size: 11px; font-weight: bold; background: transparent;"));
        row_lay->addWidget(lbl_status);

        /* Configure button */
        auto *btn = new QPushButton(QStringLiteral("Configure\u2026"));
        btn->setFixedWidth(100);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton { background: #1a2a4c; color: #c0d0e8; padding: 5px 10px; "
            "border: 1px solid #2a3a6c; border-radius: 4px; font-size: 11px; }"
            "QPushButton:hover { background: #2a3a6c; border-color: #00d4aa; }"));
        row_lay->addWidget(btn);

        rows_[i] = { chk, lbl_name, lbl_status, btn, led };
        vbox->addWidget(group);

        /* Connect checkbox → toggle DSP flag on all slots */
        connect(chk, &QCheckBox::toggled, this, [this, i](bool on) {
            /* Apply to pipeline — works on IDLE slots too (persists to cfg_).
             * When slot starts later, it reads cfg_ to init the DSP chain. */
            if (g_pipeline) {
                g_pipeline->for_each_slot([i, on](int /*id*/, EncoderSlot &slot) {
                    /* Build DspChainConfig from existing state */
                    mc1dsp::DspChainConfig cfg;
                    if (slot.has_dsp_chain()) {
                        cfg = slot.dsp_chain().config();
                    } else {
                        /* No chain yet (IDLE) — build from slot's EncoderConfig */
                        const auto &ec = slot.config();
                        cfg.sample_rate        = ec.sample_rate;
                        cfg.channels           = ec.channels;
                        cfg.eq_enabled         = ec.dsp_eq_enabled;
                        cfg.agc_enabled        = ec.dsp_agc_enabled;
                        cfg.sonic_enabled      = ec.dsp_sonic_enabled;
                        cfg.ptt_duck_enabled   = ec.dsp_ptt_duck_enabled;
                        cfg.dbx_voice_enabled  = ec.dsp_dbx_voice_enabled;
                        cfg.crossfader_enabled = ec.dsp_crossfade_enabled;
                        cfg.crossfade_duration = ec.dsp_crossfade_duration;
                        cfg.eq_preset          = ec.dsp_eq_preset;
                        cfg.eq_mode            = static_cast<mc1dsp::EqMode>(ec.dsp_eq_mode);
                    }
                    switch (i) {
                        case 0: /* 10-Band EQ */
                            cfg.eq_enabled = on;
                            cfg.eq_mode    = mc1dsp::EqMode::PARAMETRIC_10;
                            break;
                        case 1: /* 31-Band EQ */
                            cfg.eq_enabled = on;
                            cfg.eq_mode    = mc1dsp::EqMode::GRAPHIC_31;
                            break;
                        case 2: /* Sonic Enhancer */
                            cfg.sonic_enabled = on;
                            break;
                        case 3: /* PTT Duck */
                            cfg.ptt_duck_enabled = on;
                            break;
                        case 4: /* AGC */
                            cfg.agc_enabled = on;
                            break;
                        case 5: /* DBX Voice */
                            cfg.dbx_voice_enabled = on;
                            break;
                    }
                    slot.reconfigure_dsp(cfg);
                });
            }

            /* Update status + LED immediately */
            rows_[i].lbl_status->setText(on
                ? QStringLiteral("Active")
                : QStringLiteral("Bypassed"));
            rows_[i].lbl_status->setStyleSheet(on
                ? QStringLiteral("color: #22cc66; font-size: 11px; font-weight: bold; background: transparent;")
                : QStringLiteral("color: #667788; font-size: 11px; font-weight: bold; background: transparent;"));
            rows_[i].led->setStyleSheet(on
                ? QStringLiteral("background: #22cc66; border-radius: 5px; border: 1px solid #1a9a4a;")
                : QStringLiteral("background: #3a4a5c; border-radius: 5px; border: 1px solid #2a3a4c;"));

            /* Emit status message with plugin name */
            QString name = rows_[i].lbl_name->text();
            emit statusMessage(name + (on ? QStringLiteral(" enabled globally")
                                          : QStringLiteral(" bypassed globally")));
            emit dspToggleChanged();

            /* Persist rack enable state to YAML */
            DspRackEnableState state;
            state.eq10_enabled      = rows_[0].chk_enable->isChecked();
            state.eq31_enabled      = rows_[1].chk_enable->isChecked();
            state.sonic_enabled     = rows_[2].chk_enable->isChecked();
            state.ptt_duck_enabled  = rows_[3].chk_enable->isChecked();
            state.agc_enabled       = rows_[4].chk_enable->isChecked();
            state.dbx_voice_enabled = rows_[5].chk_enable->isChecked();
            GlobalConfigManager::save_rack_enable_state(state);
        });

        /* Connect configure button → corresponding signal */
        switch (i) {
            case 0: connect(btn, &QPushButton::clicked, this, &DspEffectsRack::openEq10);     break;
            case 1: connect(btn, &QPushButton::clicked, this, &DspEffectsRack::openEq31);     break;
            case 2: connect(btn, &QPushButton::clicked, this, &DspEffectsRack::openSonic);    break;
            case 3: connect(btn, &QPushButton::clicked, this, &DspEffectsRack::openPttDuck);  break;
            case 4: connect(btn, &QPushButton::clicked, this, &DspEffectsRack::openAgc);      break;
            case 5: connect(btn, &QPushButton::clicked, this, &DspEffectsRack::openDbxVoice); break;
        }
    }

    vbox->addStretch();
    scroll->setWidget(content);
    outer->addWidget(scroll);
}

void DspEffectsRack::loadFromState(const DspRackEnableState& state)
{
    /* Block signals to prevent toggle lambda from firing during startup.
     * The toggle lambda would re-save state + apply to pipeline unnecessarily. */
    bool states[kNumPlugins] = {
        state.eq10_enabled, state.eq31_enabled, state.sonic_enabled,
        state.ptt_duck_enabled, state.agc_enabled, state.dbx_voice_enabled
    };
    for (int i = 0; i < kNumPlugins; ++i) {
        rows_[i].chk_enable->blockSignals(true);
        rows_[i].chk_enable->setChecked(states[i]);
        rows_[i].chk_enable->blockSignals(false);

        /* Update visual state to match */
        rows_[i].lbl_status->setText(states[i]
            ? QStringLiteral("Active")
            : QStringLiteral("Bypassed"));
        rows_[i].lbl_status->setStyleSheet(states[i]
            ? QStringLiteral("color: #22cc66; font-size: 11px; font-weight: bold; background: transparent;")
            : QStringLiteral("color: #667788; font-size: 11px; font-weight: bold; background: transparent;"));
        rows_[i].led->setStyleSheet(states[i]
            ? QStringLiteral("background: #22cc66; border-radius: 5px; border: 1px solid #1a9a4a;")
            : QStringLiteral("background: #3a4a5c; border-radius: 5px; border: 1px solid #2a3a4c;"));
    }
}

void DspEffectsRack::refresh()
{
    /* Refresh only updates LED + status text to reflect the checkbox state.
     * It does NOT overwrite checkbox state — the checkbox is the user's input
     * control and must not be reset by the polling loop.  Previously this
     * method read DSP flags from the pipeline/EncoderConfig and forced the
     * checkboxes to match, which constantly reset user changes within 50ms. */
    for (int i = 0; i < kNumPlugins; ++i) {
        bool on = rows_[i].chk_enable->isChecked();
        rows_[i].lbl_status->setText(on
            ? QStringLiteral("Active")
            : QStringLiteral("Bypassed"));
        rows_[i].lbl_status->setStyleSheet(on
            ? QStringLiteral("color: #22cc66; font-size: 11px; font-weight: bold; background: transparent;")
            : QStringLiteral("color: #667788; font-size: 11px; font-weight: bold; background: transparent;"));
        rows_[i].led->setStyleSheet(on
            ? QStringLiteral("background: #22cc66; border-radius: 5px; border: 1px solid #1a9a4a;")
            : QStringLiteral("background: #3a4a5c; border-radius: 5px; border: 1px solid #2a3a4c;"));
    }
}

} // namespace mc1
