/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * main_window.cpp — Main application window
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "main_window.h"
#include "app.h"
#include "encoder_list_model.h"
#include "vu_meter_widget.h"
#include "about_dialog.h"
#include "config_dialog.h"
#include "edit_metadata_dialog.h"
#include "agc_settings_dialog.h"
#include "preferences_dialog.h"
#include "log_viewer_dialog.h"
#include "preset_browser_dialog.h"
#include "eq_curve_widget.h"
#include "eq_graphic_dialog.h"
#include "sonic_enhancer_dialog.h"
#include "dsp/dsp_chain.h"
#include "config_types.h"
#include "audio_pipeline.h"
#include "audio_source.h"
#ifdef _WIN32
#include "audio_capture_windows.h"
#include "platform_windows.h"
#include "video/video_capture_windows.h"
#else
#include "audio_capture_macos.h"
#include "platform_macos.h"
#include "video/video_capture_macos.h"
#endif
#include "dnas_stats.h"
#include "video/broadcast_monitor_window.h"
#include "video_effects_panel.h"
#include "overlay_settings_dialog.h"
#include "profile_manager.h"
#include "encoder_type_chooser.h"
#include "video/live_video_studio.h"
#include "dbx_voice_dialog.h"
#include "dsp_effects_rack.h"
#include "encoder_slot.h"
#include "global_config_manager.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QLinearGradient>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

#include <QResizeEvent>
#include <algorithm>
#include <QScreen>

#include <cmath>

namespace mc1 {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Mcaster1 DSP Encoder"));
    setMinimumSize(720, 520);
    resize(900, 620);

    /* Phase M5: Accept drag-and-drop for playlist files */
    setAcceptDrops(true);

    createActions();
    createMenus();
    createTrayIcon();
    setupUi();

    /* Phase M5: Create persistent log viewer (hidden) */
    log_viewer_ = new LogViewerDialog(this);

    /* UI refresh timer — polls audio levels + pipeline stats */
    demo_timer_ = new QTimer(this);
    connect(demo_timer_, &QTimer::timeout, this, &MainWindow::onDemoTick);
    demo_timer_->start(50); /* 20 fps */

    /* Start audio level monitor for VU metering from selected input device */
    startLevelMonitor();

    /* DNAS status indicator — permanent widget in status bar */
    lbl_dnas_ = new QLabel(QStringLiteral("DNAS: —"));
    lbl_dnas_->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    lbl_dnas_->setMinimumWidth(140);
    statusBar()->addPermanentWidget(lbl_dnas_);

    /* Phase M5: Start per-slot DNAS stats poller */
    if (g_pipeline)
        g_pipeline->start_dnas_poller(15);

    /* Phase M5: Restore saved window state */
    restoreSettings();

    /* Phase M10: Request macOS notification permission */
    platform::request_notification_permission();

    /* Phase M10: Auto-load saved encoder profiles */
    loadSavedProfiles();

    statusBar()->showMessage(
        QStringLiteral("Mcaster1 DSP Encoder v") + App::versionString() +
        QStringLiteral(" — ") + App::buildPhase());
}

MainWindow::~MainWindow()
{
    stopCameraPreview();
    level_monitor_.stop();
    if (sys_audio_monitor_) {
        sys_audio_monitor_->stop();
        sys_audio_monitor_.reset();
    }
}

/* ───── UI Construction ───── */

void MainWindow::setupUi()
{
    auto *central = new QWidget(this);
    auto *root    = new QVBoxLayout(central);
    root->setContentsMargins(10, 0, 10, 6);
    root->setSpacing(8);

    /* ── Theme-aware banner (repaints on resize + theme change) ── */
    banner_label_ = new QLabel;
    banner_label_->setFixedHeight(48);
    banner_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    root->addWidget(banner_label_);
    connect(App::instance(), &App::themeChanged, this, &MainWindow::paintBanner);
    /* Defer first paint until layout assigns width */
    QTimer::singleShot(0, this, &MainWindow::paintBanner);

    /* ── Metadata display ── */
    auto *meta_row = new QHBoxLayout;
    btn_edit_meta_ = new QPushButton(QIcon(QStringLiteral(":/icons/metadata.svg")),
                                     QStringLiteral("Edit"));
    btn_edit_meta_->setFixedWidth(70);
    btn_edit_meta_->setIconSize(QSize(16, 16));
    connect(btn_edit_meta_, &QPushButton::clicked, this, &MainWindow::onEditMetadata);

    lbl_metadata_ = new QLabel(QStringLiteral("No metadata — idle"));
    lbl_metadata_->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    lbl_metadata_->setMinimumHeight(36);
    lbl_metadata_->setAlignment(Qt::AlignCenter);
    lbl_metadata_->setWordWrap(true);
    QFont mf = lbl_metadata_->font();
    mf.setPointSize(12);
    lbl_metadata_->setFont(mf);

    meta_row->addWidget(lbl_metadata_, 1);
    meta_row->addWidget(btn_edit_meta_);
    root->addLayout(meta_row);

    /* ── VU Meter ── */
    auto *meter_group = new QGroupBox(QStringLiteral("Peak Meter"));
    auto *meter_lay   = new QVBoxLayout(meter_group);
    vu_meter_ = new VuMeterWidget;
    meter_lay->addWidget(vu_meter_);
    root->addWidget(meter_group);

    /* ── Live Recording / Audio Device ── */
    auto *rec_group = new QGroupBox(QStringLiteral("Audio Input"));
    auto *rec_lay   = new QHBoxLayout(rec_group);

    rec_lay->addWidget(new QLabel(QStringLiteral("Input Device:")));
    cmb_device_ = new QComboBox;
    populateDeviceCombo();
    cmb_device_->setMinimumWidth(240);
    rec_lay->addWidget(cmb_device_);
    connect(cmb_device_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { startLevelMonitor(); });

    rec_lay->addSpacing(12);
    rec_lay->addWidget(new QLabel(QStringLiteral("Output:")));
    sld_volume_ = new QSlider(Qt::Horizontal);
    sld_volume_->setRange(0, 200);
    sld_volume_->setValue(100);
    sld_volume_->setFixedWidth(100);
    rec_lay->addWidget(sld_volume_);

    lbl_volume_ = new QLabel(QStringLiteral("100%"));
    lbl_volume_->setFixedWidth(40);
    rec_lay->addWidget(lbl_volume_);
    connect(sld_volume_, &QSlider::valueChanged, this, [this](int v) {
        lbl_volume_->setText(QString::number(v) + QStringLiteral("%"));
        /* Phase M3: Apply master volume to pipeline */
        if (g_pipeline)
            g_pipeline->set_master_volume(v / 100.0f);
    });

    /* Phase M8.5: Push-to-Talk inline with Audio Input */
    rec_lay->addSpacing(16);
    rec_lay->addWidget(new QLabel(QStringLiteral("PTT Mic:")));
    cmb_ptt_device_ = new QComboBox;
    cmb_ptt_device_->addItem(QStringLiteral("(None)"), QStringLiteral(""));
    /* Use CoreAudio enumeration (same as main device combo) for reliable mic listing */
    auto ptt_devs = enumerate_coreaudio_devices();
    for (auto &d : ptt_devs) {
        if (d.input_channels < 1) continue;  /* only show input-capable devices */
        QString label = QString::fromStdString(d.name);
        if (d.is_default_input)
            label += QStringLiteral(" (Default)");
        cmb_ptt_device_->addItem(label, QString::fromStdString(d.uid));
    }
    cmb_ptt_device_->setMinimumWidth(160);
    rec_lay->addWidget(cmb_ptt_device_);

    rec_lay->addSpacing(8);
    btn_ptt_ = new QPushButton(QStringLiteral("Push to Talk"));
    btn_ptt_->setCheckable(false);
    btn_ptt_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #2a3a5c; color: #d0d8e8; padding: 6px 14px; "
        "border: 1px solid #3a4a6c; border-radius: 6px; font-weight: bold; font-size: 11px; }"
        "QPushButton:hover { background: #3a4a6c; border-color: #5a7aac; }"
        "QPushButton:pressed { background: #cc2233; color: white; border-color: #ff3344; }"));
    btn_ptt_->setToolTip(QStringLiteral("Hold to talk \u2014 ducks main audio and mixes PTT mic (Cmd+T)"));
    connect(btn_ptt_, &QPushButton::pressed, this, &MainWindow::onPttPressed);
    connect(btn_ptt_, &QPushButton::released, this, &MainWindow::onPttReleased);
    rec_lay->addWidget(btn_ptt_);

    rec_lay->addStretch();
    root->addWidget(rec_group);

    /* ── Video / Camera Input ── */
    auto *vid_group = new QGroupBox(QStringLiteral("Video Input"));
    auto *vid_lay   = new QHBoxLayout(vid_group);

    vid_lay->addWidget(new QLabel(QStringLiteral("Camera:")));
    cmb_camera_ = new QComboBox;
    populateCameraCombo();
    cmb_camera_->setMinimumWidth(200);
    vid_lay->addWidget(cmb_camera_);

    vid_lay->addSpacing(8);
    btn_video_preview_ = new QPushButton;
    btn_video_preview_->setIcon(QIcon(QStringLiteral(":/icons/video-camera.svg")));
    btn_video_preview_->setIconSize(QSize(20, 20));
    btn_video_preview_->setToolTip(QStringLiteral("Open Video Preview"));
    btn_video_preview_->setFixedSize(32, 32);
    vid_lay->addWidget(btn_video_preview_);

    /* Create frameless popout preview/editor window */
    camera_preview_win_ = new CameraPreviewWindow(this);
    camera_preview_win_->previewWidget()->setOverlayRenderer(&overlay_renderer_);
    connect(camera_preview_win_->previewWidget(),
            &CameraPreviewWidget::imageOverlayMoved,
            this, [this](int x, int y, int w, int h) {
        if (overlay_dialog_)
            overlay_dialog_->updateImagePosition(x, y, w, h);
    });
    connect(btn_video_preview_, &QPushButton::clicked, this, [this]() {
        if (camera_preview_win_->isVisible()) {
            camera_preview_win_->hide(); /* triggers stopCameraPreview via windowHidden */
        } else {
            QPoint pos = btn_video_preview_->mapToGlobal(
                QPoint(btn_video_preview_->width() + 8, 0));
            camera_preview_win_->move(pos);
            camera_preview_win_->show();
            camera_preview_win_->raise();
            startCameraPreview();
        }
    });
    connect(camera_preview_win_, &CameraPreviewWindow::windowHidden,
            this, &MainWindow::stopCameraPreview);
    connect(camera_preview_win_, &CameraPreviewWindow::effectsRequested,
            this, &MainWindow::onEffectsPanel);
    connect(camera_preview_win_, &CameraPreviewWindow::overlayRequested,
            this, &MainWindow::onOverlaySettings);
    connect(cmb_camera_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        if (camera_preview_win_ && camera_preview_win_->isVisible())
            startCameraPreview();
    });

    /* Broadcast Monitor button */
    vid_lay->addSpacing(4);
    btn_broadcast_ = new QPushButton;
    btn_broadcast_->setIcon(QIcon(QStringLiteral(":/icons/monitor.svg")));
    btn_broadcast_->setIconSize(QSize(20, 20));
    btn_broadcast_->setToolTip(QStringLiteral("Open Broadcast Monitor (Cmd+Shift+B)"));
    btn_broadcast_->setFixedSize(32, 32);
    connect(btn_broadcast_, &QPushButton::clicked, this, &MainWindow::onBroadcastMonitor);
    vid_lay->addWidget(btn_broadcast_);

    /* Create Broadcast Monitor window (hidden) */
    broadcast_win_ = new BroadcastMonitorWindow(this);
    connect(broadcast_win_, &BroadcastMonitorWindow::goLiveRequested, this, [this]() {
        is_broadcast_live_ = true;
        statusBar()->showMessage(QStringLiteral("LIVE ON-AIR — broadcasting"), 5000);

        /* Phase M9: Start video stream pipeline */
        if (!video_pipeline_)
            video_pipeline_ = std::make_unique<mc1::VideoStreamPipeline>();

        if (camera_source_ && !video_pipeline_->is_running()) {
            mc1::VideoStreamConfig vcfg;
            vcfg.video.enabled      = true;
            vcfg.video.width        = 1280;
            vcfg.video.height       = 720;
            vcfg.video.fps          = 30;
            vcfg.video.bitrate_kbps = 2500;
            vcfg.dry_run            = broadcast_win_->isDryRun();
            vcfg.transport = mc1::VideoStreamConfig::Transport::RTMP;
            video_pipeline_->start(vcfg, camera_source_.get(),
                                    &effects_chain_, &overlay_renderer_);
        }
    });
    connect(broadcast_win_, &BroadcastMonitorWindow::stopRequested, this, [this]() {
        is_broadcast_live_ = false;
        if (video_pipeline_) video_pipeline_->stop();
        statusBar()->showMessage(QStringLiteral("Broadcast stopped"), 3000);
    });

    vid_lay->addStretch();
    root->addWidget(vid_group);

    /* ── Phase M7: Three-Tab Encoder List (Radio | Podcast | TV/Video) ── */
    auto *enc_group = new QGroupBox(QStringLiteral("Encoder Slots"));
    auto *enc_lay   = new QVBoxLayout(enc_group);

    tab_encoders_ = new QTabWidget;
    tab_encoders_->setIconSize(QSize(18, 18));

    /* Helper lambda: create a table view for a given model */
    auto makeTable = [this](EncoderListModel *model) -> QTableView* {
        auto *tbl = new QTableView;
        tbl->setModel(model);
        tbl->setSelectionBehavior(QAbstractItemView::SelectRows);
        tbl->setSelectionMode(QAbstractItemView::ExtendedSelection);
        tbl->setAlternatingRowColors(true);
        tbl->verticalHeader()->hide();
        tbl->horizontalHeader()->setStretchLastSection(true);
        tbl->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(tbl, &QTableView::customContextMenuRequested,
                this, &MainWindow::onEncoderContextMenu);
        connect(tbl, &QTableView::doubleClicked, this, [this, model](const QModelIndex &idx) {
            if (!idx.isValid() || !model) return;
            int row = idx.row();
            EncoderConfig c = model->configAt(row);
            qDebug() << "[MainWindow] doubleClick open: row=" << row
                     << "slot=" << c.slot_id
                     << "eq_en=" << c.dsp_eq_enabled
                     << "eq_mode=" << c.dsp_eq_mode
                     << "hasConfig=" << model->hasConfig(row);
            auto *dlg = new ConfigDialog(c, this);
            connect(dlg, &ConfigDialog::configAccepted, this, [this, model, row](const EncoderConfig &accepted) {
                EncoderConfig cfg_copy = accepted;
                qDebug() << "[MainWindow] configAccepted: row=" << row
                         << "slot=" << cfg_copy.slot_id
                         << "eq_en=" << cfg_copy.dsp_eq_enabled
                         << "eq_mode=" << cfg_copy.dsp_eq_mode;
                applyDspToSlot(cfg_copy.slot_id, cfg_copy);
                ProfileManager::save_profile(cfg_copy);
                model->updateSlot(row, cfg_copy);
                statusBar()->showMessage(
                    QString("Updated encoder slot %1").arg(cfg_copy.slot_id), 3000);
            });
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->show();
        });
        /* Column widths */
        tbl->setColumnWidth(EncoderListModel::ColSlot, 40);
        tbl->setColumnWidth(EncoderListModel::ColName, 120);
        tbl->setColumnWidth(EncoderListModel::ColStatus, 90);
        tbl->setColumnWidth(EncoderListModel::ColCodec, 70);
        tbl->setColumnWidth(EncoderListModel::ColBitrate, 80);
        tbl->setColumnWidth(EncoderListModel::ColServer, 140);
        tbl->setColumnWidth(EncoderListModel::ColBytesSent, 80);
        return tbl;
    };

    /* Radio tab */
    model_radio_ = new EncoderListModel(EncoderConfig::EncoderType::RADIO, this);
    model_radio_->addPlaceholderSlots(3);
    tbl_radio_ = makeTable(model_radio_);
    tab_encoders_->addTab(tbl_radio_,
        QIcon(QStringLiteral(":/icons/radio.svg")), QStringLiteral("Radio"));

    /* Podcast tab */
    model_podcast_ = new EncoderListModel(EncoderConfig::EncoderType::PODCAST, this);
    tbl_podcast_ = makeTable(model_podcast_);
    tab_encoders_->addTab(tbl_podcast_,
        QIcon(QStringLiteral(":/icons/podcast.svg")), QStringLiteral("Podcast"));

    /* TV/Video tab */
    model_video_ = new EncoderListModel(EncoderConfig::EncoderType::TV_VIDEO, this);
    tbl_video_ = makeTable(model_video_);
    tab_encoders_->addTab(tbl_video_,
        QIcon(QStringLiteral(":/icons/tv-video.svg")), QStringLiteral("TV/Video"));

    /* Phase M8.5: DSP Effects Rack — 4th tab */
    dsp_rack_ = new DspEffectsRack;
    tab_encoders_->addTab(dsp_rack_,
        QIcon(QStringLiteral(":/icons/dsp.svg")), QStringLiteral("DSP Effects Rack"));
    connect(dsp_rack_, &DspEffectsRack::openEq10,    this, &MainWindow::onEqVisualizer);
    connect(dsp_rack_, &DspEffectsRack::openEq31,     this, &MainWindow::onEqGraphic);
    connect(dsp_rack_, &DspEffectsRack::openSonic,    this, &MainWindow::onSonicEnhancer);
    connect(dsp_rack_, &DspEffectsRack::openAgc,      this, &MainWindow::onAgcSettings);
    connect(dsp_rack_, &DspEffectsRack::openDbxVoice, this, &MainWindow::onDbxVoice);
    connect(dsp_rack_, &DspEffectsRack::openPttDuck,  this, [this]() {
        QMessageBox::information(this, QStringLiteral("Push-to-Talk Duck"),
            QStringLiteral("PTT Duck settings are configured via the main window PTT button.\n\n"
                           "Use the Push-to-Talk button (Cmd+T) to activate ducking.\n"
                           "When PTT is active, main audio is ducked to allow talkover."));
    });
    connect(dsp_rack_, &DspEffectsRack::dspToggleChanged, this, &MainWindow::saveAllEncoderProfiles);
    connect(dsp_rack_, &DspEffectsRack::statusMessage, this, [this](const QString &msg) {
        statusBar()->showMessage(msg, 3000);
    });

    /* Load persisted rack enable state */
    auto rack_state = GlobalConfigManager::load_rack_enable_state();
    dsp_rack_->loadFromState(rack_state);

    /* Active model/table — follows current tab */
    encoder_model_ = model_radio_;
    tbl_encoders_  = tbl_radio_;
    connect(tab_encoders_, &QTabWidget::currentChanged, this, [this](int index) {
        switch (index) {
            case 0: encoder_model_ = model_radio_;   tbl_encoders_ = tbl_radio_;   break;
            case 1: encoder_model_ = model_podcast_; tbl_encoders_ = tbl_podcast_; break;
            case 2: encoder_model_ = model_video_;   tbl_encoders_ = tbl_video_;   break;
        }
    });

    enc_lay->addWidget(tab_encoders_);

    /* Buttons row */
    auto *btn_row = new QHBoxLayout;
    btn_connect_ = new QPushButton(QIcon(QStringLiteral(":/icons/connect.svg")),
                                   QStringLiteral("Connect"));
    btn_connect_->setIconSize(QSize(16, 16));
    btn_connect_->setDefault(true);
    connect(btn_connect_, &QPushButton::clicked, this, &MainWindow::onConnect);

    btn_add_ = new QPushButton(QIcon(QStringLiteral(":/icons/add-encoder.svg")),
                               QStringLiteral("Add Encoder"));
    btn_add_->setIconSize(QSize(16, 16));
    connect(btn_add_, &QPushButton::clicked, this, &MainWindow::onAddEncoder);

    btn_row->addWidget(btn_connect_);
    btn_row->addWidget(btn_add_);
    btn_row->addStretch();
    enc_lay->addLayout(btn_row);

    root->addWidget(enc_group, 1); /* stretch factor 1 — fills space */

    setCentralWidget(central);
}

void MainWindow::createActions()
{
    act_quit_ = new QAction(QIcon(QStringLiteral(":/icons/quit.svg")),
                            QStringLiteral("&Quit"), this);
    act_quit_->setShortcut(QKeySequence::Quit);
    connect(act_quit_, &QAction::triggered, qApp, &QApplication::quit);

    act_connect_ = new QAction(QIcon(QStringLiteral(":/icons/connect.svg")),
                               QStringLiteral("&Connect"), this);
    act_connect_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Return")));
    connect(act_connect_, &QAction::triggered, this, &MainWindow::onConnect);

    act_add_encoder_ = new QAction(QIcon(QStringLiteral(":/icons/add-encoder.svg")),
                                   QStringLiteral("&Add Encoder"), this);
    act_add_encoder_->setShortcut(QKeySequence::New);
    connect(act_add_encoder_, &QAction::triggered, this, &MainWindow::onAddEncoder);

    act_about_ = new QAction(QIcon(QStringLiteral(":/icons/about.svg")),
                             QStringLiteral("About Mcaster1"), this);
    connect(act_about_, &QAction::triggered, this, &MainWindow::showAbout);

    act_theme_ = new QAction(QIcon(QStringLiteral(":/icons/theme.svg")),
                             QStringLiteral("Toggle Branded Theme"), this);
    connect(act_theme_, &QAction::triggered, this, []() {
        auto *app = App::instance();
        app->setTheme(app->theme() == App::Theme::Native
                      ? App::Theme::Branded : App::Theme::Native);
    });

    act_agc_ = new QAction(QIcon(QStringLiteral(":/icons/agc.svg")),
                           QStringLiteral("AGC Settings..."), this);
    act_agc_->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));
    connect(act_agc_, &QAction::triggered, this, &MainWindow::onAgcSettings);

    /* Phase M5: Preferences (Cmd+,) */
    act_prefs_ = new QAction(QIcon(QStringLiteral(":/icons/settings.svg")),
                             QStringLiteral("Preferences..."), this);
    act_prefs_->setShortcut(QKeySequence::Preferences);
    connect(act_prefs_, &QAction::triggered, this, &MainWindow::onPreferences);

    /* Phase M5: Log Viewer (Cmd+L) */
    act_log_ = new QAction(QIcon(QStringLiteral(":/icons/log-viewer.svg")),
                           QStringLiteral("Log Viewer"), this);
    act_log_->setShortcut(QKeySequence(QStringLiteral("Ctrl+L")));
    connect(act_log_, &QAction::triggered, this, &MainWindow::onLogViewer);

    /* Phase M5: Preset Browser (Cmd+P) */
    act_preset_ = new QAction(QIcon(QStringLiteral(":/icons/presets.svg")),
                              QStringLiteral("Preset Browser..."), this);
    act_preset_->setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));
    connect(act_preset_, &QAction::triggered, this, &MainWindow::onPresetBrowser);

    /* Phase M5: EQ Visualizer (Cmd+E) */
    act_eq_ = new QAction(QIcon(QStringLiteral(":/icons/equalizer.svg")),
                          QStringLiteral("EQ Visualizer"), this);
    act_eq_->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    connect(act_eq_, &QAction::triggered, this, &MainWindow::onEqVisualizer);

    /* Phase M6: 31-Band Graphic EQ (Cmd+Shift+E) */
    act_eq31_ = new QAction(QIcon(QStringLiteral(":/icons/equalizer.svg")),
                            QStringLiteral("31-Band Graphic EQ..."), this);
    act_eq31_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+E")));
    connect(act_eq31_, &QAction::triggered, this, &MainWindow::onEqGraphic);

    /* Phase M6: Sonic Enhancer (Cmd+Shift+S) */
    act_sonic_ = new QAction(QIcon(QStringLiteral(":/icons/sonic.svg")),
                             QStringLiteral("Sonic Enhancer..."), this);
    act_sonic_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    connect(act_sonic_, &QAction::triggered, this, &MainWindow::onSonicEnhancer);

    /* Phase M6.5: Broadcast Monitor (Cmd+Shift+B) */
    act_broadcast_ = new QAction(QIcon(QStringLiteral(":/icons/monitor.svg")),
                                  QStringLiteral("Broadcast Monitor"), this);
    act_broadcast_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+B")));
    connect(act_broadcast_, &QAction::triggered, this, &MainWindow::onBroadcastMonitor);

    /* Phase M8: DBX Voice Processor (Cmd+Shift+V) */
    act_dbx_voice_ = new QAction(QIcon(QStringLiteral(":/icons/dsp.svg")),
                                  QStringLiteral("DBX Voice Processor..."), this);
    act_dbx_voice_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    connect(act_dbx_voice_, &QAction::triggered, this, &MainWindow::onDbxVoice);

    /* Phase M8: PTT action (Cmd+T) */
    act_ptt_ = new QAction(QStringLiteral("Push-to-Talk"), this);
    act_ptt_->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
    act_ptt_->setCheckable(true);
    connect(act_ptt_, &QAction::toggled, this, [this](bool on) {
        if (on) onPttPressed(); else onPttReleased();
    });
}

void MainWindow::createMenus()
{
    auto *file_menu = menuBar()->addMenu(QStringLiteral("&File"));
    file_menu->addAction(act_connect_);
    file_menu->addAction(act_add_encoder_);
    file_menu->addSeparator();
    file_menu->addAction(act_prefs_);
    file_menu->addSeparator();
    file_menu->addAction(act_quit_);

    auto *dsp_menu = menuBar()->addMenu(QIcon(QStringLiteral(":/icons/dsp.svg")),
                                        QStringLiteral("&DSP"));
    dsp_menu->addAction(act_agc_);
    dsp_menu->addAction(act_eq_);
    dsp_menu->addAction(act_eq31_);
    dsp_menu->addAction(act_sonic_);
    dsp_menu->addAction(act_dbx_voice_);
    dsp_menu->addSeparator();
    dsp_menu->addAction(act_ptt_);
    dsp_menu->addSeparator();
    dsp_menu->addAction(act_preset_);

    /* Phase M6.5: Broadcast menu */
    auto *bcast_menu = menuBar()->addMenu(QIcon(QStringLiteral(":/icons/broadcast.svg")),
                                           QStringLiteral("&Broadcast"));
    bcast_menu->addAction(act_broadcast_);

    auto *view_menu = menuBar()->addMenu(QStringLiteral("&View"));
    view_menu->addAction(act_log_);
    view_menu->addSeparator();
    view_menu->addAction(act_theme_);

    auto *help_menu = menuBar()->addMenu(QStringLiteral("&Help"));
    help_menu->addAction(act_about_);
}

void MainWindow::createTrayIcon()
{
    tray_menu_ = new QMenu(this);
    tray_menu_->addAction(QStringLiteral("Show"), this, &QWidget::showNormal);
    tray_menu_->addSeparator();
    tray_menu_->addAction(act_quit_);

    tray_icon_ = new QSystemTrayIcon(this);
    tray_icon_->setContextMenu(tray_menu_);
    tray_icon_->setToolTip(QStringLiteral("Mcaster1 DSP Encoder"));

    tray_icon_->setIcon(QIcon(QStringLiteral(":/icons/tray-icon.svg")));
    tray_icon_->show();

    connect(tray_icon_, &QSystemTrayIcon::activated,
            this, &MainWindow::onTrayActivated);
}

/* ───── Slots ───── */

void MainWindow::onConnect()
{
    if (!g_pipeline) return;
    QModelIndex idx = tbl_encoders_->currentIndex();
    if (!idx.isValid()) {
        statusBar()->showMessage(QStringLiteral("Select an encoder slot first"), 3000);
        return;
    }
    int row = idx.row();
    EncoderConfig cfg = encoder_model_->configAt(row);

    /* Check if slot exists in pipeline */
    EncoderConfig existing;
    if (!g_pipeline->get_slot_config(cfg.slot_id, existing)) {
        /* Add it to the pipeline first */
        g_pipeline->add_slot(cfg);
    }

    /* Toggle: if running, stop; if sleeping, wake; otherwise start */
    auto stats = g_pipeline->slot_stats(cfg.slot_id);
    if (stats.state == EncoderSlot::State::LIVE ||
        stats.state == EncoderSlot::State::STARTING ||
        stats.state == EncoderSlot::State::CONNECTING ||
        stats.state == EncoderSlot::State::RECONNECTING) {
        g_pipeline->stop_slot(cfg.slot_id);
        statusBar()->showMessage(
            QString("Stopping slot %1").arg(cfg.slot_id), 3000);
    } else if (stats.state == EncoderSlot::State::SLEEP) {
        g_pipeline->wake_slot(cfg.slot_id);
        statusBar()->showMessage(
            QString("Waking slot %1").arg(cfg.slot_id), 3000);
    } else {
        g_pipeline->start_slot(cfg.slot_id);
        statusBar()->showMessage(
            QString("Starting slot %1 — connecting to %2:%3")
                .arg(cfg.slot_id)
                .arg(QString::fromStdString(cfg.stream_target.host))
                .arg(cfg.stream_target.port), 5000);
    }
}

void MainWindow::onAddEncoder()
{
    /* Phase M7: Show type chooser dialog */
    auto *chooser = new EncoderTypeChooser(this);
    if (chooser->exec() != QDialog::Accepted) {
        delete chooser;
        return;
    }
    EncoderConfig::EncoderType enc_type = chooser->selectedType();
    delete chooser;

    /* Switch to the matching tab */
    EncoderListModel *target_model = model_radio_;
    if (tab_encoders_) {
        switch (enc_type) {
        case EncoderConfig::EncoderType::RADIO:
            tab_encoders_->setCurrentIndex(0);
            target_model = model_radio_;
            break;
        case EncoderConfig::EncoderType::PODCAST:
            tab_encoders_->setCurrentIndex(1);
            target_model = model_podcast_;
            break;
        case EncoderConfig::EncoderType::TV_VIDEO:
            tab_encoders_->setCurrentIndex(2);
            target_model = model_video_;
            break;
        }
    }

    int next_slot = target_model->rowCount() + 1;
    EncoderConfig cfg;
    cfg.slot_id = next_slot;
    cfg.encoder_type = enc_type;
    cfg.name = std::string(encoder_type_str(enc_type)) + " " + std::to_string(next_slot);

    auto *dlg = new ConfigDialog(cfg, this);
    connect(dlg, &ConfigDialog::configAccepted, this, [this, target_model](const EncoderConfig &accepted) {
        EncoderConfig cfg_copy = accepted;
        ProfileManager::save_profile(cfg_copy);
        target_model->addSlot(cfg_copy);

        statusBar()->showMessage(
            QString("Added %1 encoder: %2").arg(
                QString::fromUtf8(encoder_type_str(cfg_copy.encoder_type)),
                QString::fromStdString(cfg_copy.name)), 3000);
    });
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::onEditMetadata()
{
    auto *dlg = new EditMetadataDialog(metadata_cfg_, this);
    connect(dlg, &QDialog::accepted, this, [this]() {
        /* Update the metadata display in the main window */
        if (!metadata_cfg_.manual_text.empty()) {
            lbl_metadata_->setText(QString::fromStdString(metadata_cfg_.manual_text));
        }
        statusBar()->showMessage(QStringLiteral("Metadata updated"), 3000);
    });
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::onAgcSettings()
{
    auto *dlg = new AgcSettingsDialog(this);
    dlg->setAgcConfig(dsp_defaults_.agc);
    dlg->setCurrentPresetName(QString::fromStdString(dsp_defaults_.agc_preset));
    connect(dlg, &AgcSettingsDialog::configChanged, this,
            [this, dlg](const mc1dsp::AgcConfig &cfg) {
        if (g_pipeline) {
            g_pipeline->for_each_slot([&cfg](int /*id*/, EncoderSlot &slot) {
                if (!slot.has_dsp_chain()) return;
                auto dsp_cfg = slot.dsp_chain().config();
                dsp_cfg.agc_enabled = cfg.enabled;
                slot.reconfigure_dsp(dsp_cfg);
                slot.dsp_chain().agc().set_config(cfg);
            });
        }
        dsp_defaults_.agc = cfg;
        dsp_defaults_.agc_preset = dlg->currentPresetName().toStdString();
        statusBar()->showMessage(
            cfg.enabled ? QStringLiteral("AGC processing applied to all slots")
                        : QStringLiteral("AGC processing disabled"),
            3000);
        saveAllEncoderProfiles();
    });
    connect(dlg, &AgcSettingsDialog::saveRequested, this,
            [this, dlg](const mc1dsp::AgcConfig &cfg) {
        dsp_defaults_.agc = cfg;
        dsp_defaults_.agc_preset = dlg->currentPresetName().toStdString();
        bool ok = GlobalConfigManager::save_dsp_unit(DspUnitId::AGC, dsp_defaults_);
        if (ok) {
            statusBar()->showMessage(QStringLiteral("AGC settings saved"), 3000);
            QMessageBox::information(this, QStringLiteral("Settings Saved"),
                QStringLiteral("AGC settings saved to:\n%1")
                .arg(QString::fromStdString(GlobalConfigManager::config_dir()
                     + "/" + GlobalConfigManager::unit_filename(DspUnitId::AGC))));
        } else {
            QMessageBox::warning(this, QStringLiteral("Save Failed"),
                QStringLiteral("Could not write AGC settings to disk."));
        }
    });
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::onPreferences()
{
    auto *dlg = new PreferencesDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::onLogViewer()
{
    if (!log_viewer_->isVisible()) {
        log_viewer_->show();
        log_viewer_->raise();
    } else {
        log_viewer_->activateWindow();
    }
}

void MainWindow::onPresetBrowser()
{
    auto *dlg = new PresetBrowserDialog(this);
    /* Pass current selected encoder config if available */
    QModelIndex idx = tbl_encoders_->currentIndex();
    if (idx.isValid()) {
        dlg->setCurrentConfig(encoder_model_->configAt(idx.row()));
    }
    connect(dlg, &PresetBrowserDialog::presetSelected, this,
            [this](const EncoderConfig &cfg) {
        QModelIndex sel = tbl_encoders_->currentIndex();
        if (sel.isValid()) {
            encoder_model_->updateSlot(sel.row(), cfg);
            statusBar()->showMessage(
                QStringLiteral("Preset applied to slot ") +
                QString::number(cfg.slot_id), 3000);
        }
    });
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::onEqVisualizer()
{
    auto *dlg = new EqVisualizerDialog(this);

    /* Load current defaults into dialog (sets both dialog bands_ + curve widget) */
    std::array<mc1dsp::EqBandConfig, 10> eq10_bands;
    for (int i = 0; i < 10; ++i)
        eq10_bands[i] = dsp_defaults_.eq10.bands[i];
    dlg->setBands(eq10_bands, 44100);

    /* Apply → push to pipeline */
    connect(dlg, &EqVisualizerDialog::eqConfigApplied, this, [this, dlg]() {
        const auto &b = dlg->bands();
        for (int i = 0; i < 10; ++i)
            dsp_defaults_.eq10.bands[i] = b[i];
        if (g_pipeline) {
            g_pipeline->for_each_slot([this](int /*id*/, EncoderSlot &slot) {
                if (!slot.has_dsp_chain()) return;
                auto &eq = slot.dsp_chain().eq();
                for (int i = 0; i < 10; ++i)
                    eq.set_band(i, dsp_defaults_.eq10.bands[i]);
            });
        }
        statusBar()->showMessage(QStringLiteral("10-band EQ updated"), 2000);
        saveAllEncoderProfiles();
    });

    /* Save Settings → persist to YAML */
    connect(dlg, &EqVisualizerDialog::saveRequested, this, [this, dlg]() {
        /* Read bands from dialog (user's current settings) */
        const auto &b = dlg->bands();
        for (int i = 0; i < 10; ++i)
            dsp_defaults_.eq10.bands[i] = b[i];
        bool ok = GlobalConfigManager::save_dsp_unit(DspUnitId::EQ10, dsp_defaults_);
        if (ok) {
            statusBar()->showMessage(QStringLiteral("10-band EQ settings saved"), 3000);
            QMessageBox::information(this, QStringLiteral("Settings Saved"),
                QStringLiteral("10-band EQ settings saved to:\n%1")
                .arg(QString::fromStdString(GlobalConfigManager::config_dir()
                     + "/" + GlobalConfigManager::unit_filename(DspUnitId::EQ10))));
        } else {
            QMessageBox::warning(this, QStringLiteral("Save Failed"),
                QStringLiteral("Could not write EQ settings to disk."));
        }
    });

    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::onEqGraphic()
{
    auto *dlg = new EqGraphicDialog(this);
    connect(dlg, &EqGraphicDialog::configChanged, this, [this, dlg]() {
        /* Apply 31-band EQ to pipeline */
        if (g_pipeline) {
            g_pipeline->for_each_slot([dlg](int /*id*/, EncoderSlot &slot) {
                if (!slot.has_dsp_chain()) return;
                auto dsp_cfg = slot.dsp_chain().config();
                dsp_cfg.eq_enabled = true;
                dsp_cfg.eq_mode = mc1dsp::EqMode::GRAPHIC_31;
                slot.reconfigure_dsp(dsp_cfg);
                for (int b = 0; b < mc1dsp::DspEq31::NUM_BANDS; ++b) {
                    slot.dsp_chain().eq31().set_band(0, b, dlg->bandGain(0, b));
                    slot.dsp_chain().eq31().set_band(1, b, dlg->bandGain(1, b));
                }
                slot.dsp_chain().eq31().set_linked(dlg->isLinked());
            });
        }
        for (int b = 0; b < 31; ++b) {
            dsp_defaults_.eq31.gains_l[b] = dlg->bandGain(0, b);
            dsp_defaults_.eq31.gains_r[b] = dlg->bandGain(1, b);
        }
        dsp_defaults_.eq31.linked = dlg->isLinked();
        statusBar()->showMessage(QStringLiteral("31-band EQ updated"), 2000);
        saveAllEncoderProfiles();
    });
    connect(dlg, &EqGraphicDialog::saveRequested, this, [this, dlg]() {
        for (int b = 0; b < 31; ++b) {
            dsp_defaults_.eq31.gains_l[b] = dlg->bandGain(0, b);
            dsp_defaults_.eq31.gains_r[b] = dlg->bandGain(1, b);
        }
        dsp_defaults_.eq31.linked = dlg->isLinked();
        bool ok = GlobalConfigManager::save_dsp_unit(DspUnitId::EQ31, dsp_defaults_);
        if (ok) {
            statusBar()->showMessage(QStringLiteral("31-band EQ settings saved"), 3000);
            QMessageBox::information(this, QStringLiteral("Settings Saved"),
                QStringLiteral("31-band EQ settings saved to:\n%1")
                .arg(QString::fromStdString(GlobalConfigManager::config_dir()
                     + "/" + GlobalConfigManager::unit_filename(DspUnitId::EQ31))));
        } else {
            QMessageBox::warning(this, QStringLiteral("Save Failed"),
                QStringLiteral("Could not write EQ settings to disk."));
        }
    });
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::onSonicEnhancer()
{
    auto *dlg = new SonicEnhancerDialog(this);
    dlg->setConfig(dsp_defaults_.sonic);
    dlg->setCurrentPresetName(QString::fromStdString(dsp_defaults_.sonic_preset));
    connect(dlg, &SonicEnhancerDialog::configChanged, this,
            [this, dlg](const mc1dsp::SonicEnhancerConfig &cfg) {
        if (g_pipeline) {
            g_pipeline->for_each_slot([&cfg](int /*id*/, EncoderSlot &slot) {
                if (!slot.has_dsp_chain()) return;
                slot.dsp_chain().sonic_enhancer().configure(cfg,
                    slot.dsp_chain().config().sample_rate);
            });
        }
        dsp_defaults_.sonic = cfg;
        dsp_defaults_.sonic_preset = dlg->currentPresetName().toStdString();
        statusBar()->showMessage(
            cfg.enabled ? QStringLiteral("Sonic Enhancer applied to all slots")
                        : QStringLiteral("Sonic Enhancer disabled"),
            3000);
        saveAllEncoderProfiles();
    });
    connect(dlg, &SonicEnhancerDialog::saveRequested, this,
            [this, dlg](const mc1dsp::SonicEnhancerConfig &cfg) {
        dsp_defaults_.sonic = cfg;
        dsp_defaults_.sonic_preset = dlg->currentPresetName().toStdString();
        bool ok = GlobalConfigManager::save_dsp_unit(DspUnitId::SONIC_ENHANCER, dsp_defaults_);
        if (ok) {
            statusBar()->showMessage(QStringLiteral("Sonic Enhancer settings saved"), 3000);
            QMessageBox::information(this, QStringLiteral("Settings Saved"),
                QStringLiteral("Sonic Enhancer settings saved to:\n%1")
                .arg(QString::fromStdString(GlobalConfigManager::config_dir()
                     + "/" + GlobalConfigManager::unit_filename(DspUnitId::SONIC_ENHANCER))));
        } else {
            QMessageBox::warning(this, QStringLiteral("Save Failed"),
                QStringLiteral("Could not write Sonic Enhancer settings to disk."));
        }
    });
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::onDbxVoice()
{
    auto *dlg = new DbxVoiceDialog(this);
    dlg->setDbxConfig(dsp_defaults_.dbx_voice);
    dlg->setCurrentPresetName(QString::fromStdString(dsp_defaults_.dbx_preset));
    connect(dlg, &DbxVoiceDialog::configChanged, this,
            [this, dlg](const mc1dsp::DbxVoiceConfig &cfg) {
        if (g_pipeline) {
            g_pipeline->for_each_slot([&cfg](int /*id*/, EncoderSlot &slot) {
                if (!slot.has_dsp_chain()) return;
                slot.dsp_chain().dbx_voice().configure(cfg,
                    slot.dsp_chain().config().sample_rate);
            });
        }
        dsp_defaults_.dbx_voice = cfg;
        dsp_defaults_.dbx_preset = dlg->currentPresetName().toStdString();
        statusBar()->showMessage(
            cfg.enabled ? QStringLiteral("DBX Voice Processor applied to all slots")
                        : QStringLiteral("DBX Voice Processor disabled"),
            3000);
        saveAllEncoderProfiles();
    });
    connect(dlg, &DbxVoiceDialog::saveRequested, this,
            [this, dlg](const mc1dsp::DbxVoiceConfig &cfg) {
        dsp_defaults_.dbx_voice = cfg;
        dsp_defaults_.dbx_preset = dlg->currentPresetName().toStdString();
        bool ok = GlobalConfigManager::save_dsp_unit(DspUnitId::DBX_VOICE, dsp_defaults_);
        if (ok) {
            statusBar()->showMessage(QStringLiteral("DBX Voice settings saved"), 3000);
            QMessageBox::information(this, QStringLiteral("Settings Saved"),
                QStringLiteral("DBX Voice settings saved to:\n%1")
                .arg(QString::fromStdString(GlobalConfigManager::config_dir()
                     + "/" + GlobalConfigManager::unit_filename(DspUnitId::DBX_VOICE))));
        } else {
            QMessageBox::warning(this, QStringLiteral("Save Failed"),
                QStringLiteral("Could not write DBX Voice settings to disk."));
        }
    });
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::onPttPressed()
{
    if (!g_pipeline) return;
    g_pipeline->for_each_slot([](int /*id*/, EncoderSlot &slot) {
        if (!slot.has_dsp_chain()) return;
        slot.dsp_chain().ptt_duck().set_ptt_active(true);
    });

    /* Visual duck: drop volume slider to 30% to show ducking is active */
    {
        QSignalBlocker blk(sld_volume_);
        sld_volume_->setValue(30);
        lbl_volume_->setText(QStringLiteral("30%"));
    }
    if (g_pipeline)
        g_pipeline->set_master_volume(0.30f);

    btn_ptt_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #ff4444; color: white; padding: 6px 14px; "
        "border: 1px solid #ff6666; border-radius: 6px; font-weight: bold; font-size: 11px; }"));
    statusBar()->showMessage(QStringLiteral("PTT ACTIVE \u2014 ducking main audio"), 0);
}

void MainWindow::onPttReleased()
{
    if (!g_pipeline) return;
    g_pipeline->for_each_slot([](int /*id*/, EncoderSlot &slot) {
        if (!slot.has_dsp_chain()) return;
        slot.dsp_chain().ptt_duck().set_ptt_active(false);
    });

    /* Restore volume to 100% */
    {
        QSignalBlocker blk(sld_volume_);
        sld_volume_->setValue(100);
        lbl_volume_->setText(QStringLiteral("100%"));
    }
    if (g_pipeline)
        g_pipeline->set_master_volume(1.0f);

    btn_ptt_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #2a3a5c; color: #d0d8e8; padding: 6px 14px; "
        "border: 1px solid #3a4a6c; border-radius: 6px; font-weight: bold; font-size: 11px; }"
        "QPushButton:hover { background: #3a4a6c; border-color: #5a7aac; }"
        "QPushButton:pressed { background: #cc2233; color: white; border-color: #ff3344; }"));
    statusBar()->clearMessage();
}

void MainWindow::onEncoderContextMenu(const QPoint &pos)
{
    QModelIndex idx = tbl_encoders_->indexAt(pos);
    if (!idx.isValid()) return;

    /* Collect all selected rows */
    QModelIndexList sel = tbl_encoders_->selectionModel()->selectedRows();
    if (sel.isEmpty()) return;

    const bool multi = (sel.size() > 1);
    int row = idx.row();

    QMenu ctx(this);

    /* Capture the model pointer at context menu creation time — safe across tab switches */
    EncoderListModel *ctx_model = encoder_model_;

    if (!multi) {
        /* ── Single-selection: full menu ── */
        EncoderConfig cfg = ctx_model->configAt(row);
        int slot_id = cfg.slot_id;

        ctx.addAction(QStringLiteral("Configure..."), this, [this, ctx_model, row]() {
            EncoderConfig c = ctx_model->configAt(row);
            qDebug() << "[MainWindow] ctxMenu open: row=" << row
                     << "slot=" << c.slot_id
                     << "eq_en=" << c.dsp_eq_enabled
                     << "eq_mode=" << c.dsp_eq_mode
                     << "hasConfig=" << ctx_model->hasConfig(row);
            auto *dlg = new ConfigDialog(c, this);
            connect(dlg, &ConfigDialog::configAccepted, this, [this, ctx_model, row](const EncoderConfig &accepted) {
                EncoderConfig cfg_copy = accepted;
                qDebug() << "[MainWindow] ctxMenu configAccepted: row=" << row
                         << "slot=" << cfg_copy.slot_id
                         << "eq_en=" << cfg_copy.dsp_eq_enabled;
                /* Apply DSP settings to the specific slot's pipeline */
                applyDspToSlot(cfg_copy.slot_id, cfg_copy);
                ProfileManager::save_profile(cfg_copy);
                ctx_model->updateSlot(row, cfg_copy);
                statusBar()->showMessage(
                    QString("Updated encoder slot %1").arg(cfg_copy.slot_id), 3000);
            });
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->show();
        });

        /* Phase M7: "Open Studio" for TV/Video encoder slots */
        if (cfg.encoder_type == EncoderConfig::EncoderType::TV_VIDEO) {
            ctx.addAction(QStringLiteral("Open Studio..."), this, [this, ctx_model, row, slot_id]() {
                EncoderConfig c = ctx_model->configAt(row);
                auto *studio = new LiveVideoStudioDialog(c, this);
                connect(studio, &LiveVideoStudioDialog::goLiveRequested,
                        this, [this, slot_id](const mc1::VideoConfig &vcfg,
                                              const mc1::StreamTargetEntry &tgt) {
                    if (g_pipeline) g_pipeline->start_slot(slot_id);
                    if (!video_pipeline_)
                        video_pipeline_ = std::make_unique<mc1::VideoStreamPipeline>();
                    if (camera_source_ && !video_pipeline_->is_running()) {
                        mc1::VideoStreamConfig sc;
                        sc.video = vcfg;
                        sc.stream_key = tgt.stream_key;
                        sc.hls_output_dir = tgt.output_dir;
                        sc.hls_segment_duration = tgt.hls_segment_duration;
                        sc.hls_max_segments = tgt.hls_max_segments;
                        if (tgt.server_type == "HLS (Local)") {
                            sc.transport = mc1::VideoStreamConfig::Transport::HLS;
                        } else if (tgt.server_type == "YouTube Live" ||
                                   tgt.server_type == "Twitch" ||
                                   tgt.server_type == "RTMP") {
                            sc.transport = mc1::VideoStreamConfig::Transport::RTMP;
                            sc.target.host = tgt.host;
                            sc.target.port = tgt.port;
                            sc.target.mount = tgt.mount;
                        } else {
                            sc.target.host = tgt.host;
                            sc.target.port = tgt.port;
                            sc.target.mount = tgt.mount;
                            sc.target.password = tgt.password;
                            if (vcfg.video_codec == mc1::VideoConfig::VideoCodec::VP8 ||
                                vcfg.video_codec == mc1::VideoConfig::VideoCodec::VP9)
                                sc.transport = mc1::VideoStreamConfig::Transport::WEBM_ICECAST;
                            else
                                sc.transport = mc1::VideoStreamConfig::Transport::FLV_ICECAST;
                        }
                        video_pipeline_->start(sc, camera_source_.get(),
                                                &effects_chain_, &overlay_renderer_);
                    }
                    statusBar()->showMessage(
                        QString("TV/Video slot %1 going live").arg(slot_id), 3000);
                });
                connect(studio, &LiveVideoStudioDialog::stopRequested,
                        this, [this, slot_id]() {
                    if (g_pipeline) g_pipeline->stop_slot(slot_id);
                    if (video_pipeline_) video_pipeline_->stop();
                });
                studio->setAttribute(Qt::WA_DeleteOnClose);
                studio->show();
            });
        }

        ctx.addSeparator();

        if (g_pipeline) {
            auto stats = g_pipeline->slot_stats(slot_id);
            if (stats.state == EncoderSlot::State::IDLE ||
                stats.state == EncoderSlot::State::ERROR) {
                ctx.addAction(QStringLiteral("Start"), this, [slot_id]() {
                    if (g_pipeline) g_pipeline->start_slot(slot_id);
                });
            }
            if (stats.state == EncoderSlot::State::LIVE ||
                stats.state == EncoderSlot::State::STARTING ||
                stats.state == EncoderSlot::State::CONNECTING ||
                stats.state == EncoderSlot::State::RECONNECTING) {
                ctx.addAction(QStringLiteral("Stop"), this, [slot_id]() {
                    if (g_pipeline) g_pipeline->stop_slot(slot_id);
                });
                ctx.addAction(QStringLiteral("Restart"), this, [slot_id]() {
                    if (g_pipeline) g_pipeline->restart_slot(slot_id);
                });
            }
            if (stats.state == EncoderSlot::State::SLEEP) {
                ctx.addAction(QStringLiteral("Wake"), this, [slot_id]() {
                    if (g_pipeline) g_pipeline->wake_slot(slot_id);
                });
            }
        }

        ctx.addSeparator();
        ctx.addAction(QStringLiteral("Delete"), this, [this, ctx_model, row, slot_id]() {
            auto reply = QMessageBox::question(this, QStringLiteral("Delete Encoder"),
                QStringLiteral("Delete encoder slot %1?").arg(slot_id),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (reply != QMessageBox::Yes) return;
            EncoderConfig cfg = ctx_model->configAt(row);
            if (!cfg.config_file_path.empty())
                ProfileManager::delete_profile(cfg.config_file_path);
            if (g_pipeline) g_pipeline->remove_slot(slot_id);
            ctx_model->removeSlot(row);
            statusBar()->showMessage(QStringLiteral("Encoder slot removed"), 3000);
        });
    } else {
        /* ── Multi-selection: only Delete is available ── */
        ctx.addAction(QString("Delete %1 encoder slots").arg(sel.size()), this, [this, ctx_model, sel]() {
            auto reply = QMessageBox::question(this, QStringLiteral("Delete Encoders"),
                QStringLiteral("Delete %1 selected encoder slots?\n\n"
                               "This action cannot be undone.").arg(sel.size()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (reply != QMessageBox::Yes) return;

            /* Collect rows in descending order to avoid index shifts */
            std::vector<int> rows;
            rows.reserve(sel.size());
            for (const auto &idx : sel)
                rows.push_back(idx.row());
            std::sort(rows.begin(), rows.end(), std::greater<int>());

            int deleted = 0;
            for (int r : rows) {
                EncoderConfig cfg = ctx_model->configAt(r);
                if (!cfg.config_file_path.empty())
                    ProfileManager::delete_profile(cfg.config_file_path);
                if (g_pipeline) g_pipeline->remove_slot(cfg.slot_id);
                ctx_model->removeSlot(r);
                ++deleted;
            }
            statusBar()->showMessage(
                QString("%1 encoder slot(s) removed").arg(deleted), 3000);
        });
    }

    ctx.exec(tbl_encoders_->viewport()->mapToGlobal(pos));
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger ||
        reason == QSystemTrayIcon::DoubleClick) {
        showNormal();
        raise();
        activateWindow();
    }
}

void MainWindow::onDemoTick()
{
    /* Phase M3/M4: Poll pipeline stats and update encoder list + VU meter. */
    if (g_pipeline && g_pipeline->slot_count() > 0) {
        auto all = g_pipeline->all_stats();

        /* Update all three tab models so background tabs stay current */
        if (model_radio_)   model_radio_->updateLiveStats(all);
        if (model_podcast_) model_podcast_->updateLiveStats(all);
        if (model_video_)   model_video_->updateLiveStats(all);

        /* Update Connect button text based on selected slot state */
        QModelIndex idx = tbl_encoders_->currentIndex();
        if (idx.isValid()) {
            EncoderConfig cfg = encoder_model_->configAt(idx.row());
            auto st = g_pipeline->slot_stats(cfg.slot_id);
            switch (st.state) {
                case EncoderSlot::State::LIVE:
                    btn_connect_->setText(QStringLiteral("Disconnect"));
                    break;
                case EncoderSlot::State::CONNECTING:
                case EncoderSlot::State::STARTING:
                    btn_connect_->setText(QStringLiteral("Connecting..."));
                    break;
                case EncoderSlot::State::RECONNECTING:
                    btn_connect_->setText(QStringLiteral("Reconnecting..."));
                    break;
                case EncoderSlot::State::STOPPING:
                    btn_connect_->setText(QStringLiteral("Stopping..."));
                    break;
                default:
                    btn_connect_->setText(QStringLiteral("Connect"));
                    break;
            }
        }

        /* Update status bar with total bandwidth + listeners */
        uint64_t total_bytes = 0;
        int live_count = 0;
        int total_listeners = 0;
        for (const auto &st : all) {
            total_bytes += st.bytes_sent;
            total_listeners += st.listeners;
            if (st.state == EncoderSlot::State::LIVE) ++live_count;
        }
        if (live_count > 0) {
            statusBar()->showMessage(
                QString("%1 slot%2 live — %3 listener%4 — %5 sent")
                    .arg(live_count)
                    .arg(live_count == 1 ? "" : "s")
                    .arg(total_listeners)
                    .arg(total_listeners == 1 ? "" : "s")
                    .arg(EncoderListModel::formatBytes(total_bytes)));
        }
        /* Update DNAS label with total listeners */
        if (total_listeners > 0) {
            lbl_dnas_->setText(QString("DNAS: %1 listener%2")
                .arg(total_listeners)
                .arg(total_listeners == 1 ? "" : "s"));
        } else if (live_count > 0) {
            lbl_dnas_->setText(QStringLiteral("DNAS: polling..."));
        } else {
            lbl_dnas_->setText(QStringLiteral("DNAS: —"));
        }

        /* Phase M5: Dock badge — show live slot count */
        if (live_count > 0)
            platform::set_dock_badge(std::to_string(live_count).c_str());
        else
            platform::clear_dock_badge();

        /* Phase M5/M10: macOS notifications on state transitions */
        if (live_count > prev_live_count_ && prev_live_count_ >= 0) {
            int delta = live_count - prev_live_count_;
            auto msg = QString("%1 encoder slot%2 now LIVE")
                           .arg(delta).arg(delta == 1 ? "" : "s");
            if (tray_icon_ && QSystemTrayIcon::supportsMessages()) {
                tray_icon_->showMessage(
                    QStringLiteral("Mcaster1 DSP Encoder"),
                    msg, QSystemTrayIcon::Information, 3000);
            }
            platform::send_notification("Mcaster1 DSP Encoder",
                                        msg.toUtf8().constData());
        } else if (live_count < prev_live_count_ && live_count == 0) {
            if (tray_icon_ && QSystemTrayIcon::supportsMessages()) {
                tray_icon_->showMessage(
                    QStringLiteral("Mcaster1 DSP Encoder"),
                    QStringLiteral("All encoder slots stopped"),
                    QSystemTrayIcon::Warning, 3000);
            }
            platform::send_notification("Mcaster1 DSP Encoder",
                                        "All encoder slots stopped");
        }
        prev_live_count_ = live_count;

        /* Phase M8.5: Refresh DSP Effects Rack status (~every 500ms via demo_timer_) */
        if (dsp_rack_) dsp_rack_->refresh();
    }

    /* VU metering — read from the active source */
    float vu_left = -60.0f, vu_right = -60.0f;

    if (meter_source_ == MeterSource::SYSTEM_AUDIO) {
        vu_left  = sys_peak_left_.load();
        vu_right = sys_peak_right_.load();
    } else if (meter_source_ == MeterSource::INPUT_DEVICE &&
               level_monitor_.is_running()) {
        vu_left  = level_monitor_.peak_left_db();
        vu_right = level_monitor_.peak_right_db();
    }

    /* Apply volume slider as dB gain offset for visual feedback */
    if (sld_volume_) {
        int vol_pct = sld_volume_->value();
        if (vol_pct <= 0) {
            vu_left = vu_right = -60.0f;
        } else if (vol_pct != 100) {
            float vol_db = 20.0f * std::log10(vol_pct / 100.0f);
            vu_left  = std::max(vu_left  + vol_db, -60.0f);
            vu_right = std::max(vu_right + vol_db, -60.0f);
        }
    }

    vu_meter_->setLevels(vu_left, vu_right);

    /* Phase M9: Poll video pipeline stats (~1 Hz, every 20 ticks) */
    if (video_pipeline_ && video_pipeline_->is_running() && broadcast_win_) {
        static int video_stats_tick = 0;
        if (++video_stats_tick >= 20) {
            video_stats_tick = 0;
            auto vs = video_pipeline_->stats();
            double bitrate_kbps = 0.0;
            if (vs.uptime_sec > 0.0)
                bitrate_kbps = (vs.bytes_sent * 8.0) / (vs.uptime_sec * 1000.0);
            broadcast_win_->updateStats(bitrate_kbps, vs.avg_fps, vs.uptime_sec);
        }
    }
}

void MainWindow::populateDeviceCombo()
{
    cmb_device_->clear();

    /* Restore previously selected device UID from QSettings */
    QSettings s;
    QString saved_uid = s.value(QStringLiteral("audio/device_uid")).toString();
    int select_index = -1;

    /* ── System Audio ── */
    if (mc1::SystemAudioSource::is_available()) {
        cmb_device_->addItem(QStringLiteral("--- System Audio ---"));
        auto *model = qobject_cast<QStandardItemModel*>(cmb_device_->model());
        if (model) {
            auto *item = model->item(cmb_device_->count() - 1);
            item->setEnabled(false);
            item->setSelectable(false);
        }
        cmb_device_->addItem(
            QStringLiteral("  System Audio (ScreenCaptureKit)"),
            QVariant(-2));
        if (saved_uid == QStringLiteral("__system_audio__"))
            select_index = cmb_device_->count() - 1;
    }

    /* ── CoreAudio devices (comprehensive) ── */
    auto ca_devices = enumerate_coreaudio_devices();

    /* Input devices */
    bool has_input = false;
    for (const auto &d : ca_devices) {
        if (d.input_channels > 0) { has_input = true; break; }
    }
    if (has_input) {
        cmb_device_->addItem(QStringLiteral("--- Input Devices ---"));
        auto *model = qobject_cast<QStandardItemModel*>(cmb_device_->model());
        if (model) {
            auto *item = model->item(cmb_device_->count() - 1);
            item->setEnabled(false);
            item->setSelectable(false);
        }
        for (const auto &d : ca_devices) {
            if (d.input_channels <= 0) continue;
            QString label = QStringLiteral("  ") + QString::fromStdString(d.name);
            label += QStringLiteral(" [%1ch, %2Hz]")
                .arg(d.input_channels)
                .arg(static_cast<int>(d.sample_rate));
            if (d.is_default_input) label += QStringLiteral(" (Default)");
            cmb_device_->addItem(label, QString::fromStdString(d.uid));
            if (saved_uid == QString::fromStdString(d.uid))
                select_index = cmb_device_->count() - 1;
        }
    }

    /* Output devices (for loopback/monitoring) */
    bool has_output = false;
    for (const auto &d : ca_devices) {
        if (d.output_channels > 0 && d.input_channels == 0) { has_output = true; break; }
    }
    if (has_output) {
        cmb_device_->addItem(QStringLiteral("--- Output Devices ---"));
        auto *model = qobject_cast<QStandardItemModel*>(cmb_device_->model());
        if (model) {
            auto *item = model->item(cmb_device_->count() - 1);
            item->setEnabled(false);
            item->setSelectable(false);
        }
        for (const auto &d : ca_devices) {
            if (d.output_channels <= 0 || d.input_channels > 0) continue;
            QString label = QStringLiteral("  ") + QString::fromStdString(d.name);
            label += QStringLiteral(" [%1ch, %2Hz]")
                .arg(d.output_channels)
                .arg(static_cast<int>(d.sample_rate));
            if (d.is_default_output) label += QStringLiteral(" (Default)");
            cmb_device_->addItem(label, QString::fromStdString(d.uid));
            if (saved_uid == QString::fromStdString(d.uid))
                select_index = cmb_device_->count() - 1;
        }
    }

    if (cmb_device_->count() == 0) {
        cmb_device_->addItem(QStringLiteral("No audio devices found"));
    }

    /* Restore selection */
    if (select_index >= 0) {
        cmb_device_->setCurrentIndex(select_index);
    } else if (cmb_device_->count() > 0) {
        /* Select first selectable item (skip headers) */
        for (int i = 0; i < cmb_device_->count(); ++i) {
            QVariant data = cmb_device_->itemData(i);
            if (data.isValid()) {
                cmb_device_->setCurrentIndex(i);
                break;
            }
        }
    }
}

void MainWindow::populateCameraCombo()
{
    cmb_camera_->clear();

    auto cameras = mc1::AudioPipeline::list_cameras();
    if (cameras.empty()) {
        cmb_camera_->addItem(QStringLiteral("No cameras found"));
        return;
    }

    for (const auto &cam : cameras) {
        QString label = QString::fromStdString(cam.name);
        cmb_camera_->addItem(label, QString::fromStdString(cam.unique_id));
    }
}

void MainWindow::startLevelMonitor()
{
    /* Stop all existing monitors */
    level_monitor_.stop();
    if (sys_audio_monitor_) {
        sys_audio_monitor_->stop();
        sys_audio_monitor_.reset();
    }
    meter_source_ = MeterSource::NONE;
    sys_peak_left_.store(-60.0f);
    sys_peak_right_.store(-60.0f);

    if (!cmb_device_ || cmb_device_->currentIndex() < 0) return;

    QVariant data = cmb_device_->currentData();
    if (!data.isValid()) return;

    /* ── System Audio (ScreenCaptureKit) ── */
    if (data.typeId() == QMetaType::Int && data.toInt() == -2) {
        sys_audio_monitor_ = std::make_unique<mc1::SystemAudioSource>(44100, 2);
        auto *self = this;
        bool ok = sys_audio_monitor_->start(
            [self](const float *pcm, size_t frames, int ch, int /*sr*/) {
                float peak_l = 0.0f, peak_r = 0.0f;
                for (size_t i = 0; i < frames; ++i) {
                    float s0 = std::fabs(pcm[i * ch]);
                    if (s0 > peak_l) peak_l = s0;
                    if (ch >= 2) {
                        float s1 = std::fabs(pcm[i * ch + 1]);
                        if (s1 > peak_r) peak_r = s1;
                    }
                }
                auto to_db = [](float v) {
                    if (v < 1e-10f) return -60.0f;
                    return std::max(20.0f * std::log10(v), -60.0f);
                };
                float ld = to_db(peak_l);
                float rd = (ch >= 2) ? to_db(peak_r) : ld;
                /* Ballistic: instant attack, smooth release */
                float pl = self->sys_peak_left_.load();
                float pr = self->sys_peak_right_.load();
                constexpr float rel = 0.4f;
                self->sys_peak_left_.store(std::max((ld > pl) ? ld : (pl - rel), -60.0f));
                self->sys_peak_right_.store(std::max((rd > pr) ? rd : (pr - rel), -60.0f));
            });
        if (ok) {
            meter_source_ = MeterSource::SYSTEM_AUDIO;
            statusBar()->showMessage(
                QStringLiteral("Metering: System Audio (ScreenCaptureKit)"), 3000);
        } else {
            sys_audio_monitor_.reset();
            /* Fallback to default input device */
            level_monitor_.start(-1, 44100, 2);
            meter_source_ = MeterSource::INPUT_DEVICE;
            statusBar()->showMessage(
                QStringLiteral("System Audio unavailable — metering default mic"), 3000);
        }
        return;
    }

    /* ── CoreAudio device selected by UID ── */
    QString uid = data.toString();
    QString ca_name;
    int ca_input_channels = 0;
    int ca_channels = 2;
    int ca_sample_rate = 44100;

    auto ca_devices = enumerate_coreaudio_devices();
    for (const auto &d : ca_devices) {
        if (QString::fromStdString(d.uid) == uid) {
            ca_name = QString::fromStdString(d.name);
            ca_input_channels = d.input_channels;
            ca_channels = std::max(d.input_channels, 1);
            ca_sample_rate = static_cast<int>(d.sample_rate);
            break;
        }
    }

    /* Output-only device — cannot meter directly via PortAudio */
    if (ca_input_channels <= 0) {
        statusBar()->showMessage(
            QString("Output device \"%1\" — no input to meter").arg(ca_name), 4000);
        return;
    }

    /* Find matching PortAudio device by name (both APIs enumerate CoreAudio) */
    int pa_index = -1;
    auto pa_devices = PortAudioSource::enumerate_devices();

    /* Log available PA devices for debugging */
    fprintf(stderr, "[LevelMon] Looking for CoreAudio device: \"%s\" (uid=%s)\n",
            ca_name.toStdString().c_str(), uid.toStdString().c_str());
    for (const auto &d : pa_devices) {
        fprintf(stderr, "[LevelMon]   PA device %d: \"%s\" (%dch)%s\n",
                d.index, d.name.c_str(), d.max_input_channels,
                d.is_default_input ? " [DEFAULT]" : "");
    }

    /* Exact name match first */
    for (const auto &d : pa_devices) {
        if (!ca_name.isEmpty() &&
            QString::fromStdString(d.name) == ca_name) {
            pa_index = d.index;
            ca_channels = std::min(ca_channels, d.max_input_channels);
            fprintf(stderr, "[LevelMon] Exact match → PA device %d\n", pa_index);
            break;
        }
    }

    /* Substring match fallback */
    if (pa_index < 0 && !ca_name.isEmpty()) {
        for (const auto &d : pa_devices) {
            QString pa_name = QString::fromStdString(d.name);
            if (pa_name.contains(ca_name, Qt::CaseInsensitive) ||
                ca_name.contains(pa_name, Qt::CaseInsensitive)) {
                pa_index = d.index;
                ca_channels = std::min(ca_channels, d.max_input_channels);
                fprintf(stderr, "[LevelMon] Substring match → PA device %d\n", pa_index);
                break;
            }
        }
    }

    if (pa_index < 0) {
        fprintf(stderr, "[LevelMon] No PA match found, using default input\n");
    }

    level_monitor_.start(pa_index, ca_sample_rate, ca_channels);
    meter_source_ = MeterSource::INPUT_DEVICE;

    statusBar()->showMessage(
        QString("Metering: %1 (PA:%2, %3ch, %4Hz)")
            .arg(ca_name)
            .arg(pa_index)
            .arg(ca_channels)
            .arg(ca_sample_rate), 3000);
}

void MainWindow::onBroadcastMonitor()
{
    if (!broadcast_win_) return;
    if (broadcast_win_->isVisible()) {
        broadcast_win_->raise();
        broadcast_win_->activateWindow();
    } else {
        /* Position to the right of the main window */
        QPoint pos = geometry().topRight() + QPoint(12, 0);
        broadcast_win_->move(pos);
        broadcast_win_->show();
        broadcast_win_->raise();
        /* Start camera if not already running */
        if (!camera_source_) startCameraPreview();
    }
}

void MainWindow::onEffectsPanel()
{
    if (!effects_panel_) {
        effects_panel_ = new VideoEffectsPanel(&effects_chain_, this);
        effects_panel_->setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint);
        effects_panel_->setAttribute(Qt::WA_DeleteOnClose);
        connect(effects_panel_, &QObject::destroyed, this, [this]() {
            effects_panel_ = nullptr;
        });
    }
    if (effects_panel_->isVisible()) {
        effects_panel_->raise();
    } else {
        /* Position near the preview window */
        if (camera_preview_win_ && camera_preview_win_->isVisible()) {
            QPoint pos = camera_preview_win_->geometry().topLeft() - QPoint(290, 0);
            effects_panel_->move(pos);
        }
        effects_panel_->show();
        effects_panel_->raise();
    }
}

void MainWindow::onOverlaySettings()
{
    if (!overlay_dialog_) {
        overlay_dialog_ = new OverlaySettingsDialog(&overlay_renderer_, this);
        overlay_dialog_->setAttribute(Qt::WA_DeleteOnClose);
        connect(overlay_dialog_, &QObject::destroyed, this, [this]() {
            overlay_dialog_ = nullptr;
        });
    }
    overlay_dialog_->show();
    overlay_dialog_->raise();
}

void MainWindow::startCameraPreview()
{
    stopCameraPreview();

    int cam_index = 0;
    if (cmb_camera_ && cmb_camera_->currentIndex() >= 0)
        cam_index = cmb_camera_->currentIndex();

    /* Request camera permission (no-op if already granted) */
    CameraSource::request_permission();

    camera_source_ = std::make_unique<CameraSource>(cam_index, 1280, 720, 30);

    auto *preview = camera_preview_win_->previewWidget();
    auto *bcast_preview = broadcast_win_ ? broadcast_win_->previewWidget() : nullptr;
    auto *self = this;

    bool started = camera_source_->start(
        [preview, bcast_preview, self](const mc1::VideoFrame &frame) {
        /* Phase M6.5: Apply effects + overlays to a working copy */
        bool has_effects = !self->effects_chain_.is_bypass() &&
                           self->effects_chain_.count() > 0;
        bool has_overlays = true; /* always check overlays */

        if (has_effects || has_overlays) {
            /* Copy frame to mutable buffer */
            std::vector<uint8_t> buf(frame.data, frame.data + frame.data_len);

            /* Apply effects chain */
            self->effects_chain_.process(buf.data(), frame.width,
                                          frame.height, frame.stride);

            /* Apply overlays (text, logo, SRT, ticker, lower-third) */
            self->overlay_renderer_.render(buf.data(), frame.width,
                                            frame.height, frame.stride);

            /* Push processed frame to Preview/Editor */
            preview->pushFrame(buf.data(), frame.width,
                                frame.height, frame.stride);

            /* Push to Broadcast Monitor if visible */
            if (bcast_preview)
                bcast_preview->pushFrame(buf.data(), frame.width,
                                          frame.height, frame.stride);
        } else {
            /* No processing — push raw frame */
            preview->pushFrame(frame.data, frame.width,
                                frame.height, frame.stride);
            if (bcast_preview)
                bcast_preview->pushFrame(frame.data, frame.width,
                                          frame.height, frame.stride);
        }
    });

    if (started) {
        statusBar()->showMessage(
            QString("Camera started: %1").arg(
                QString::fromStdString(camera_source_->name())), 3000);
    } else {
        camera_source_.reset();
        statusBar()->showMessage(QStringLiteral("Failed to start camera"), 3000);
    }
}

void MainWindow::stopCameraPreview()
{
    if (camera_source_) {
        camera_source_->stop();
        camera_source_.reset();
    }
}

void MainWindow::showAbout()
{
    AboutDialog dlg(this);
    dlg.exec();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    saveAllEncoderProfiles();

    QSettings s;
    bool tray_close = s.value(QStringLiteral("prefs/trayOnClose"), true).toBool();
    if (tray_close && tray_icon_ && tray_icon_->isVisible()) {
        hide();
        event->ignore();
    } else {
        platform::clear_dock_badge();
        event->accept();
    }
}

/* ───── Drag & Drop (Phase M5) ───── */

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event->mimeData()->hasUrls()) return;

    for (const QUrl &url : event->mimeData()->urls()) {
        QString path = url.toLocalFile().toLower();
        if (path.endsWith(QStringLiteral(".m3u"))  ||
            path.endsWith(QStringLiteral(".m3u8")) ||
            path.endsWith(QStringLiteral(".pls"))  ||
            path.endsWith(QStringLiteral(".xspf")) ||
            path.endsWith(QStringLiteral(".txt"))) {
            event->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    for (const QUrl &url : event->mimeData()->urls()) {
        QString path = url.toLocalFile();
        QString lower = path.toLower();
        if (lower.endsWith(QStringLiteral(".m3u"))  ||
            lower.endsWith(QStringLiteral(".m3u8")) ||
            lower.endsWith(QStringLiteral(".pls"))  ||
            lower.endsWith(QStringLiteral(".xspf")) ||
            lower.endsWith(QStringLiteral(".txt"))) {
            /* Load into selected encoder slot */
            QModelIndex idx = tbl_encoders_->currentIndex();
            int slot_id = 1;
            if (idx.isValid()) {
                EncoderConfig cfg = encoder_model_->configAt(idx.row());
                slot_id = cfg.slot_id;
            }
            if (g_pipeline) {
                g_pipeline->load_playlist(slot_id, path.toStdString());
                statusBar()->showMessage(
                    QString("Loaded playlist into slot %1: %2")
                        .arg(slot_id).arg(QFileInfo(path).fileName()), 5000);
            }
            break; /* only load first valid file */
        }
    }
    event->acceptProposedAction();
}

/* ───── Theme-Aware Banner (Phase M8.5) ───── */

void MainWindow::paintBanner()
{
    if (!banner_label_) return;

    const int bh = 48;
    int bw = banner_label_->width();
    if (bw < 200) bw = width() - 20; /* fallback before layout */

    QPixmap px(bw, bh);
    px.fill(Qt::transparent);

    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    bool dark = (App::instance()->theme() == App::Theme::Branded);

    if (dark) {
        /* Dark branded: navy gradient + teal accents */
        QLinearGradient grad(0, 0, bw, 0);
        grad.setColorAt(0.0, QColor(0x0a, 0x0a, 0x1e));
        grad.setColorAt(1.0, QColor(0x14, 0x14, 0x32));
        p.fillRect(0, 0, bw, bh, grad);

        /* Title */
        QFont tf(QStringLiteral("Menlo"), 18, QFont::Bold);
        p.setFont(tf);
        p.setPen(QColor(0xe0, 0xe0, 0xff));
        p.drawText(54, 0, bw - 60, bh, Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("Mcaster1DSPEncoder"));

        /* Version (right) */
        QFont vf(QStringLiteral("Menlo"), 9);
        p.setFont(vf);
        p.setPen(QColor(0x66, 0x77, 0x88));
        p.drawText(0, 0, bw - 16, bh, Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("v") + App::versionString());

        /* Teal accent line */
        p.setPen(QPen(QColor(0x00, 0xd4, 0xaa), 2));
        p.drawLine(0, bh - 1, bw, bh - 1);
    } else {
        /* Native / Enterprise Pro: light chrome gradient */
        QLinearGradient grad(0, 0, bw, 0);
        grad.setColorAt(0.0, QColor(0xf8, 0xf8, 0xfa));
        grad.setColorAt(0.5, QColor(0xff, 0xff, 0xff));
        grad.setColorAt(1.0, QColor(0xe8, 0xe8, 0xee));
        p.fillRect(0, 0, bw, bh, grad);

        /* Title — dark text */
        QFont tf(QStringLiteral("Menlo"), 18, QFont::Bold);
        p.setFont(tf);
        p.setPen(QColor(0x22, 0x22, 0x33));
        p.drawText(54, 0, bw - 60, bh, Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("Mcaster1DSPEncoder"));

        /* Version (right) */
        QFont vf(QStringLiteral("Menlo"), 9);
        p.setFont(vf);
        p.setPen(QColor(0x88, 0x88, 0x99));
        p.drawText(0, 0, bw - 16, bh, Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("v") + App::versionString());

        /* Nickel/chrome accent line — gradient */
        QLinearGradient accent(0, 0, bw, 0);
        accent.setColorAt(0.0,  QColor(0x99, 0x99, 0xaa));
        accent.setColorAt(0.35, QColor(0xcc, 0xcc, 0xdd));
        accent.setColorAt(0.5,  QColor(0xdd, 0xdd, 0xee));
        accent.setColorAt(0.65, QColor(0xcc, 0xcc, 0xdd));
        accent.setColorAt(1.0,  QColor(0x99, 0x99, 0xaa));
        p.setPen(QPen(QBrush(accent), 2));
        p.drawLine(0, bh - 1, bw, bh - 1);
    }

    /* Broadcast tower icon (tinted for light theme) */
    QIcon tower(QStringLiteral(":/icons/banner-broadcast.svg"));
    if (!tower.isNull()) {
        QPixmap icon_px = tower.pixmap(QSize(32, 32));
        p.drawPixmap(12, 8, icon_px);
    }

    p.end();
    banner_label_->setPixmap(px);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    paintBanner();
}

/* ───── Settings Persistence (Phase M5) ───── */

void MainWindow::saveSettings()
{
    QSettings s;
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("window/state"),    saveState());

    /* Save column widths */
    auto *header = tbl_encoders_->horizontalHeader();
    for (int i = 0; i < header->count(); ++i) {
        s.setValue(QStringLiteral("window/col_%1").arg(i),
                   header->sectionSize(i));
    }

    /* Save theme */
    auto *app = App::instance();
    s.setValue(QStringLiteral("prefs/theme"),
              app->theme() == App::Theme::Native ? 0 : 1);

    /* Save selected audio device UID */
    if (cmb_device_ && cmb_device_->currentIndex() >= 0) {
        QVariant data = cmb_device_->currentData();
        if (data.typeId() == QMetaType::Int && data.toInt() == -2) {
            s.setValue(QStringLiteral("audio/device_uid"),
                       QStringLiteral("__system_audio__"));
            global_cfg_.audio_device_uid = "__system_audio__";
        } else if (data.typeId() == QMetaType::QString) {
            s.setValue(QStringLiteral("audio/device_uid"), data.toString());
            global_cfg_.audio_device_uid = data.toString().toStdString();
        }
    }

    /* Phase M11: Save global config + DSP rack defaults to YAML */
    QRect geo = geometry();
    global_cfg_.window_x = geo.x();
    global_cfg_.window_y = geo.y();
    global_cfg_.window_width = geo.width();
    global_cfg_.window_height = geo.height();
    global_cfg_.window_maximized = isMaximized();
    global_cfg_.theme = (app->theme() == App::Theme::Native) ? 0 : 1;

    GlobalConfigManager::save_global(global_cfg_);
    GlobalConfigManager::save_dsp_defaults(dsp_defaults_);
}

void MainWindow::restoreSettings()
{
    /* Phase M11: Load global config + DSP rack defaults from YAML */
    global_cfg_   = GlobalConfigManager::load_global();
    dsp_defaults_ = GlobalConfigManager::load_dsp_defaults();

    fprintf(stderr, "[M11.5] restoreSettings: loaded global config + DSP defaults from YAML\n");
    fprintf(stderr, "[M11.5]   AGC: enabled=%d threshold=%.1f ratio=%.1f\n",
            dsp_defaults_.agc.enabled, dsp_defaults_.agc.threshold_db,
            dsp_defaults_.agc.ratio);
    fprintf(stderr, "[M11.5]   Sonic: process=%.1f lo_contour=%.1f\n",
            dsp_defaults_.sonic.process, dsp_defaults_.sonic.lo_contour);

    QSettings s;
    if (s.contains(QStringLiteral("window/geometry"))) {
        restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray());
    }
    if (s.contains(QStringLiteral("window/state"))) {
        restoreState(s.value(QStringLiteral("window/state")).toByteArray());
    }

    /* Restore column widths */
    auto *header = tbl_encoders_->horizontalHeader();
    for (int i = 0; i < header->count(); ++i) {
        QString key = QStringLiteral("window/col_%1").arg(i);
        if (s.contains(key)) {
            header->resizeSection(i, s.value(key).toInt());
        }
    }
}

/* ── Phase M10: Config persistence helpers ──────────────────────────── */

void MainWindow::syncDspFromPipeline(EncoderConfig& cfg)
{
    if (!g_pipeline) return;
    g_pipeline->for_each_slot([&](int id, EncoderSlot &slot) {
        if (id != cfg.slot_id) return;
        if (!slot.has_dsp_chain()) return;
        auto &chain = slot.dsp_chain();

        /* Enable flags */
        cfg.dsp_agc_enabled       = chain.agc().is_enabled();
        cfg.dsp_eq_enabled        = chain.eq().is_enabled();
        cfg.dsp_sonic_enabled     = chain.sonic_enhancer().is_enabled();
        cfg.dsp_ptt_duck_enabled  = chain.ptt_duck().is_enabled();
        cfg.dsp_dbx_voice_enabled = chain.dbx_voice().is_enabled();
        cfg.dsp_crossfade_enabled = chain.crossfader().is_enabled();

        /* AGC */
        cfg.agc_params = chain.agc().config();

        /* Sonic Enhancer */
        cfg.sonic_params = chain.sonic_enhancer().config();

        /* PTT Duck */
        cfg.ptt_duck_params = chain.ptt_duck().config();

        /* DBX Voice */
        cfg.dbx_voice_params = chain.dbx_voice().config();

        /* Crossfader */
        cfg.crossfader_params = chain.crossfader().config();

        /* 10-band EQ */
        for (int b = 0; b < mc1dsp::DspEq::NUM_BANDS; ++b)
            cfg.eq10_params.bands[b] = chain.eq().get_band(b);

        /* 31-band EQ */
        for (int b = 0; b < mc1dsp::DspEq31::NUM_BANDS; ++b) {
            cfg.eq31_params.gains_l[b] = chain.eq31().get_band(0, b);
            cfg.eq31_params.gains_r[b] = chain.eq31().get_band(1, b);
        }
    });
}

void MainWindow::applyDspToSlot(int slot_id, const EncoderConfig& cfg)
{
    if (!g_pipeline) return;
    g_pipeline->for_each_slot([&](int id, EncoderSlot &slot) {
        if (id != slot_id) return;

        /* Build DspChainConfig with all enable flags */
        mc1dsp::DspChainConfig dsp_cfg;
        if (slot.has_dsp_chain()) {
            dsp_cfg = slot.dsp_chain().config();
        } else {
            dsp_cfg.sample_rate = cfg.sample_rate;
            dsp_cfg.channels    = cfg.channels;
        }
        dsp_cfg.eq_enabled        = cfg.dsp_eq_enabled;
        dsp_cfg.agc_enabled       = cfg.dsp_agc_enabled;
        dsp_cfg.sonic_enabled     = cfg.dsp_sonic_enabled;
        dsp_cfg.ptt_duck_enabled  = cfg.dsp_ptt_duck_enabled;
        dsp_cfg.dbx_voice_enabled = cfg.dsp_dbx_voice_enabled;
        dsp_cfg.eq_mode = (cfg.dsp_eq_mode == 1)
            ? mc1dsp::EqMode::GRAPHIC_31
            : mc1dsp::EqMode::PARAMETRIC_10;

        /* reconfigure_dsp persists flags to cfg_ even without a chain */
        slot.reconfigure_dsp(dsp_cfg);

        /* Apply parameter structs only if chain exists (they're stored
         * in EncoderConfig and will be applied when slot starts) */
        if (slot.has_dsp_chain()) {
            auto &chain = slot.dsp_chain();
            chain.agc().set_config(cfg.agc_params);
            chain.sonic_enhancer().configure(cfg.sonic_params, cfg.sample_rate);
            chain.ptt_duck().set_config(cfg.ptt_duck_params);
            chain.dbx_voice().set_config(cfg.dbx_voice_params);
            chain.crossfader().set_config(cfg.crossfader_params);

            for (int b = 0; b < mc1dsp::DspEq::NUM_BANDS; ++b)
                chain.eq().set_band(b, cfg.eq10_params.bands[b]);

            for (int b = 0; b < mc1dsp::DspEq31::NUM_BANDS; ++b) {
                chain.eq31().set_band(0, b, cfg.eq31_params.gains_l[b]);
                chain.eq31().set_band(1, b, cfg.eq31_params.gains_r[b]);
            }
        }

        /* Also update the slot's full config so params persist for later start() */
        slot.update_config(cfg);
    });
}

void MainWindow::saveAllEncoderProfiles()
{
    for (auto *model : { model_radio_, model_podcast_, model_video_ }) {
        if (!model) continue;
        for (int i = 0; i < model->rowCount(); ++i) {
            /* Skip placeholder rows that have no real backing config.
             * Saving placeholders creates spurious YAML files that
             * cause duplicate slot growth on every launch. */
            if (!model->hasConfig(i)) {
                fprintf(stderr, "[M11.5] saveAllEncoderProfiles: skipping placeholder row %d\n", i);
                continue;
            }
            EncoderConfig cfg = model->configAt(i);
            syncDspFromPipeline(cfg);
            ProfileManager::save_profile(cfg);
            model->updateSlot(i, cfg);
        }
    }
}

void MainWindow::loadSavedProfiles()
{
    auto profiles = ProfileManager::scan_profiles();

    fprintf(stderr, "[M11.5] loadSavedProfiles: found %zu profile(s) on disk\n",
            profiles.size());

    /* Clear ALL models first — remove placeholders created by setupUi().
     * This prevents the exponential growth bug where placeholders get
     * appended alongside loaded profiles and then saved back to disk. */
    for (auto *model : { model_radio_, model_podcast_, model_video_ }) {
        if (model) model->clearAll();
    }

    if (profiles.empty()) {
        /* No saved profiles — add 3 radio placeholders for fresh install */
        fprintf(stderr, "[M11.5] No profiles on disk — adding 3 radio placeholders\n");
        model_radio_->addPlaceholderSlots(3);
        return;
    }

    int loaded = 0;
    for (auto &cfg : profiles) {
        EncoderListModel *target = nullptr;
        switch (cfg.encoder_type) {
            case EncoderConfig::EncoderType::RADIO:    target = model_radio_;   break;
            case EncoderConfig::EncoderType::PODCAST:  target = model_podcast_; break;
            case EncoderConfig::EncoderType::TV_VIDEO: target = model_video_;   break;
        }
        if (!target) continue;

        /* Per-encoder DSP params + enable flags are loaded from their own
         * YAML file by load_profile() — do NOT overwrite them with global
         * defaults or rack enable state.  Global rack state only controls
         * the DspEffectsRack widget, not per-encoder configs. */

        if (g_pipeline) {
            int new_id = g_pipeline->add_slot(cfg);
            cfg.slot_id = new_id;          // pipeline assigns fresh ID
            applyDspToSlot(new_id, cfg);
        }

        target->addSlot(cfg);
        ++loaded;

        fprintf(stderr, "[M11.5]   Loaded profile: slot=%d type=%d name='%s' file='%s'\n",
                cfg.slot_id, static_cast<int>(cfg.encoder_type),
                cfg.name.c_str(), cfg.config_file_path.c_str());
    }

    fprintf(stderr, "[M11.5] loadSavedProfiles: loaded %d encoder(s)\n", loaded);

    if (loaded > 0)
        statusBar()->showMessage(
            QString("Loaded %1 encoder profile%2")
                .arg(loaded).arg(loaded == 1 ? "" : "s"), 5000);
}

} // namespace mc1
