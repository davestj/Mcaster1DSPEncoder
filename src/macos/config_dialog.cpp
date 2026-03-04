/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * config_dialog.cpp — Encoder configuration dialog (4-tab QTabWidget)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config_dialog.h"
#include "basic_settings_tab.h"
#include "yp_settings_tab.h"
#include "advanced_settings_tab.h"
#include "icy22_settings_tab.h"
#include "podcast_settings_tab.h"
#include "effects_rack_tab.h"
#include "profile_manager.h"
#include "agc_settings_dialog.h"
#include "sonic_enhancer_dialog.h"
#include "dbx_voice_dialog.h"
#include "eq_curve_widget.h"
#include "eq_graphic_dialog.h"
#include <QTabWidget>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QMainWindow>
#include <QPushButton>
#include <QScreen>
#include <QStatusBar>
#include <QGuiApplication>
#include <QDebug>

namespace mc1 {

ConfigDialog::ConfigDialog(const EncoderConfig &cfg, QWidget *parent)
    : QDialog(parent), cfg_(cfg)
{
    setWindowTitle(QString("Configure Encoder \u2014 Slot %1").arg(cfg.slot_id));

    /* Screen-aware sizing: cap at 85% of screen height */
    QScreen *screen = QGuiApplication::primaryScreen();
    int max_h = screen ? static_cast<int>(screen->availableGeometry().height() * 0.85) : 700;
    resize(580, qMin(700, max_h));
    setMinimumSize(480, 400);
    setSizeGripEnabled(true);

    auto *top = new QVBoxLayout(this);

    /* Tab widget */
    auto *tabs = new QTabWidget;

    tab_basic_    = new BasicSettingsTab(cfg_.encoder_type);
    tab_advanced_ = new AdvancedSettingsTab;

    tabs->addTab(tab_basic_,    QStringLiteral("Basic Settings"));

    /* Phase M7: Show/hide tabs based on encoder type.
     * Radio & TV/Video: YP + ICY 2.2 (streaming metadata).
     * Podcast: Podcast tab (RSS/SFTP/publishing), no YP/ICY. */
    if (cfg_.encoder_type != EncoderConfig::EncoderType::PODCAST) {
        tab_yp_    = new YPSettingsTab;
        tab_icy22_ = new ICY22SettingsTab;
        tabs->addTab(tab_yp_,    QStringLiteral("YP Settings"));
        tabs->addTab(tab_icy22_, QStringLiteral("ICY 2.2"));
    }

    tabs->addTab(tab_advanced_, QStringLiteral("Advanced"));

    /* Effects Rack tab — available for all encoder types */
    tab_effects_ = new EffectsRackTab;
    tabs->addTab(tab_effects_, QStringLiteral("Effects Rack"));

    if (cfg_.encoder_type == EncoderConfig::EncoderType::PODCAST) {
        tab_podcast_ = new PodcastSettingsTab;
        tabs->addTab(tab_podcast_, QStringLiteral("Podcast"));
    }

    top->addWidget(tabs, 1);

    /* Buttons */
    auto *bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);

    /* Save Config button — writes YAML to disk and confirms path */
    auto *btn_save = bb->addButton(QStringLiteral("Save Config"),
                                   QDialogButtonBox::ActionRole);
    top->addWidget(bb);

    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(btn_save, &QPushButton::clicked, this, [this]() {
        /* Gather current values from all tabs */
        tab_basic_->saveToConfig(cfg_);
        if (tab_yp_)       tab_yp_->saveToConfig(cfg_);
        tab_advanced_->saveToConfig(cfg_);
        if (tab_icy22_)    tab_icy22_->saveToConfig(cfg_);
        if (tab_podcast_)  tab_podcast_->saveToConfig(cfg_);
        tab_effects_->saveToConfig(cfg_);

        std::string path = ProfileManager::save_profile(cfg_);
        if (!path.empty()) {
            QMessageBox::information(this, QStringLiteral("Config Saved"),
                QStringLiteral("Wrote to %1").arg(QString::fromStdString(path)));
        } else {
            QMessageBox::warning(this, QStringLiteral("Save Failed"),
                QStringLiteral("Could not write config file."));
        }
    });

    connect(bb->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this]() {
        tab_basic_->saveToConfig(cfg_);
        if (tab_yp_)       tab_yp_->saveToConfig(cfg_);
        tab_advanced_->saveToConfig(cfg_);
        if (tab_icy22_)    tab_icy22_->saveToConfig(cfg_);
        if (tab_podcast_)  tab_podcast_->saveToConfig(cfg_);
        tab_effects_->saveToConfig(cfg_);
        qDebug() << "[ConfigDialog] Apply: eq_enabled=" << cfg_.dsp_eq_enabled
                 << "eq_mode=" << cfg_.dsp_eq_mode
                 << "agc=" << cfg_.dsp_agc_enabled
                 << "sonic=" << cfg_.dsp_sonic_enabled
                 << "dbx=" << cfg_.dsp_dbx_voice_enabled
                 << "ptt=" << cfg_.dsp_ptt_duck_enabled;
        emit configAccepted(cfg_);
    });

    /* Load config into tabs */
    tab_basic_->loadFromConfig(cfg_);
    if (tab_yp_)       tab_yp_->loadFromConfig(cfg_);
    tab_advanced_->loadFromConfig(cfg_);
    if (tab_icy22_)    tab_icy22_->loadFromConfig(cfg_);
    if (tab_podcast_)  tab_podcast_->loadFromConfig(cfg_);
    tab_effects_->loadFromConfig(cfg_);

    /* ── Phase M11: Wire per-encoder DSP Configure buttons ────────── */

    /* AGC */
    connect(tab_effects_, &EffectsRackTab::openAgc, this, [this]() {
        auto *dlg = new AgcSettingsDialog(nullptr);
        dlg->setWindowTitle(QStringLiteral("AGC — Per-Encoder"));
        dlg->setAgcConfig(tab_effects_->cfgSnapshot().agc_params);
        dlg->setCurrentPresetName(QString::fromStdString(cfg_.dsp_agc_preset));
        dlg->setSystemWide(false);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &AgcSettingsDialog::configChanged, this, [this, dlg](const mc1dsp::AgcConfig &agc) {
            cfg_.agc_params = agc;
            cfg_.dsp_agc_preset = dlg->currentPresetName().toStdString();
            tab_effects_->cfgSnapshot().agc_params = agc;
        });
        connect(dlg, &AgcSettingsDialog::saveRequested, this, [this, dlg](const mc1dsp::AgcConfig &agc) {
            cfg_.agc_params = agc;
            cfg_.dsp_agc_preset = dlg->currentPresetName().toStdString();
            tab_effects_->cfgSnapshot().agc_params = agc;
            qDebug() << "[DSP Config] AGC per-encoder settings saved";
            if (auto *mw = qobject_cast<QMainWindow*>(parentWidget()))
                mw->statusBar()->showMessage(QStringLiteral("AGC settings saved"), 3000);
        });
        dlg->show();
    });

    /* Sonic Enhancer */
    connect(tab_effects_, &EffectsRackTab::openSonic, this, [this]() {
        auto *dlg = new SonicEnhancerDialog(nullptr);
        dlg->setWindowTitle(QStringLiteral("Sonic Enhancer — Per-Encoder"));
        dlg->setConfig(tab_effects_->cfgSnapshot().sonic_params);
        dlg->setCurrentPresetName(QString::fromStdString(cfg_.dsp_sonic_preset));
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &SonicEnhancerDialog::configChanged, this,
                [this, dlg](const mc1dsp::SonicEnhancerConfig &s) {
            cfg_.sonic_params = s;
            cfg_.dsp_sonic_preset = dlg->currentPresetName().toStdString();
            tab_effects_->cfgSnapshot().sonic_params = s;
        });
        connect(dlg, &SonicEnhancerDialog::saveRequested, this,
                [this, dlg](const mc1dsp::SonicEnhancerConfig &s) {
            cfg_.sonic_params = s;
            cfg_.dsp_sonic_preset = dlg->currentPresetName().toStdString();
            tab_effects_->cfgSnapshot().sonic_params = s;
            qDebug() << "[DSP Config] Sonic Enhancer per-encoder settings saved";
            if (auto *mw = qobject_cast<QMainWindow*>(parentWidget()))
                mw->statusBar()->showMessage(QStringLiteral("Sonic Enhancer settings saved"), 3000);
        });
        dlg->show();
    });

    /* DBX Voice */
    connect(tab_effects_, &EffectsRackTab::openDbxVoice, this, [this]() {
        auto *dlg = new DbxVoiceDialog(nullptr);
        dlg->setWindowTitle(QStringLiteral("DBX 286s — Per-Encoder"));
        dlg->setDbxConfig(tab_effects_->cfgSnapshot().dbx_voice_params);
        dlg->setCurrentPresetName(QString::fromStdString(cfg_.dsp_dbx_preset));
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &DbxVoiceDialog::configChanged, this,
                [this, dlg](const mc1dsp::DbxVoiceConfig &dbx) {
            cfg_.dbx_voice_params = dbx;
            cfg_.dsp_dbx_preset = dlg->currentPresetName().toStdString();
            tab_effects_->cfgSnapshot().dbx_voice_params = dbx;
        });
        connect(dlg, &DbxVoiceDialog::saveRequested, this,
                [this, dlg](const mc1dsp::DbxVoiceConfig &dbx) {
            cfg_.dbx_voice_params = dbx;
            cfg_.dsp_dbx_preset = dlg->currentPresetName().toStdString();
            tab_effects_->cfgSnapshot().dbx_voice_params = dbx;
            qDebug() << "[DSP Config] DBX Voice per-encoder settings saved";
            if (auto *mw = qobject_cast<QMainWindow*>(parentWidget()))
                mw->statusBar()->showMessage(QStringLiteral("DBX Voice settings saved"), 3000);
        });
        dlg->show();
    });

    /* 10-Band Parametric EQ */
    connect(tab_effects_, &EffectsRackTab::openEq10, this, [this]() {
        auto *dlg = new EqVisualizerDialog(nullptr);
        dlg->setWindowTitle(QStringLiteral("10-Band EQ — Per-Encoder"));
        /* Load bands from per-encoder config into the dialog + curve widget */
        auto &snap = tab_effects_->cfgSnapshot();
        std::array<mc1dsp::EqBandConfig, 10> bands;
        for (int i = 0; i < 10; ++i)
            bands[i] = snap.eq10_params.bands[i];
        dlg->setBands(bands, cfg_.sample_rate);
        dlg->setCurrentPresetName(QString::fromStdString(cfg_.dsp_eq_preset));
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &EqVisualizerDialog::eqConfigApplied, this, [this, dlg]() {
            auto &snap2 = tab_effects_->cfgSnapshot();
            const auto &b = dlg->bands();
            for (int i = 0; i < 10; ++i)
                snap2.eq10_params.bands[i] = b[i];
            cfg_.eq10_params = snap2.eq10_params;
            cfg_.dsp_eq_preset = dlg->currentPresetName().toStdString();
        });
        connect(dlg, &EqVisualizerDialog::saveRequested, this, [this, dlg]() {
            auto &snap2 = tab_effects_->cfgSnapshot();
            const auto &b = dlg->bands();
            for (int i = 0; i < 10; ++i)
                snap2.eq10_params.bands[i] = b[i];
            cfg_.eq10_params = snap2.eq10_params;
            cfg_.dsp_eq_preset = dlg->currentPresetName().toStdString();
            qDebug() << "[DSP Config] 10-Band EQ per-encoder settings saved";
            if (auto *mw = qobject_cast<QMainWindow*>(parentWidget()))
                mw->statusBar()->showMessage(QStringLiteral("10-Band EQ settings saved"), 3000);
        });
        dlg->show();
    });

    /* 31-Band Graphic EQ */
    connect(tab_effects_, &EffectsRackTab::openEq31, this, [this]() {
        auto *dlg = new EqGraphicDialog(nullptr);
        dlg->setWindowTitle(QStringLiteral("31-Band EQ — Per-Encoder"));
        auto &snap = tab_effects_->cfgSnapshot();
        dlg->setLinked(snap.eq31_params.linked);
        for (int i = 0; i < 31; ++i) {
            dlg->setBandGain(0, i, snap.eq31_params.gains_l[i]);
            dlg->setBandGain(1, i, snap.eq31_params.gains_r[i]);
        }
        dlg->setCurrentPresetName(QString::fromStdString(cfg_.dsp_eq31_preset));
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &EqGraphicDialog::configChanged, this, [this, dlg]() {
            auto &snap = tab_effects_->cfgSnapshot();
            snap.eq31_params.linked = dlg->isLinked();
            for (int i = 0; i < 31; ++i) {
                snap.eq31_params.gains_l[i] = dlg->bandGain(0, i);
                snap.eq31_params.gains_r[i] = dlg->bandGain(1, i);
            }
            cfg_.eq31_params = snap.eq31_params;
            cfg_.dsp_eq31_preset = dlg->currentPresetName().toStdString();
        });
        connect(dlg, &EqGraphicDialog::saveRequested, this, [this, dlg]() {
            auto &snap = tab_effects_->cfgSnapshot();
            snap.eq31_params.linked = dlg->isLinked();
            for (int i = 0; i < 31; ++i) {
                snap.eq31_params.gains_l[i] = dlg->bandGain(0, i);
                snap.eq31_params.gains_r[i] = dlg->bandGain(1, i);
            }
            cfg_.eq31_params = snap.eq31_params;
            cfg_.dsp_eq31_preset = dlg->currentPresetName().toStdString();
            qDebug() << "[DSP Config] 31-Band EQ per-encoder settings saved";
            if (auto *mw = qobject_cast<QMainWindow*>(parentWidget()))
                mw->statusBar()->showMessage(QStringLiteral("31-Band EQ settings saved"), 3000);
        });
        dlg->show();
    });

    /* PTT Duck — no dedicated dialog yet; just a placeholder */
    connect(tab_effects_, &EffectsRackTab::openPttDuck, this, [this]() {
        QMessageBox::information(this, QStringLiteral("PTT Duck"),
            QStringLiteral("PTT Duck settings are configured via the main window PTT button.\n"
                           "Duck level: %1 dB, Attack: %2 ms, Release: %3 ms")
            .arg(cfg_.ptt_duck_params.duck_level_db, 0, 'f', 1)
            .arg(cfg_.ptt_duck_params.attack_ms, 0, 'f', 1)
            .arg(cfg_.ptt_duck_params.release_ms, 0, 'f', 1));
    });
}

void ConfigDialog::accept()
{
    tab_basic_->saveToConfig(cfg_);
    if (tab_yp_)       tab_yp_->saveToConfig(cfg_);
    tab_advanced_->saveToConfig(cfg_);
    if (tab_icy22_)    tab_icy22_->saveToConfig(cfg_);
    if (tab_podcast_)  tab_podcast_->saveToConfig(cfg_);
    tab_effects_->saveToConfig(cfg_);
    emit configAccepted(cfg_);
    QDialog::accept();
}

EncoderConfig ConfigDialog::result() const
{
    return cfg_;
}

} // namespace mc1
