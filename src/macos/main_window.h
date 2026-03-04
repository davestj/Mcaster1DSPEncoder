/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * main_window.h — Main application window
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_MAIN_WINDOW_H
#define MC1_MAIN_WINDOW_H

#include <QMainWindow>
#include <QSystemTrayIcon>
#include <memory>
#include "config_types.h"
#include "video/camera_preview_window.h"
#include "video/video_effects.h"
#include "video/overlay_renderer.h"
#include "video/video_stream_pipeline.h"
#include "input_level_monitor.h"
#include "dsp_effects_rack.h"

class QAction;
class QComboBox;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QMenu;
class QPushButton;
class QSlider;
class QTabWidget;
class QTableView;
class QTimer;

namespace mc1 {

class BroadcastMonitorWindow;
class CameraSource;
class DnasStats;
class OverlaySettingsDialog;
class SystemAudioSource;
class EncoderListModel;
class LogViewerDialog;
class VideoEffectsPanel;
class VuMeterWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    LogViewerDialog *logViewer() const { return log_viewer_; }

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onConnect();
    void onAddEncoder();
    void onEditMetadata();
    void onAgcSettings();
    void onPreferences();
    void onLogViewer();
    void onPresetBrowser();
    void onEqVisualizer();
    void onEqGraphic();
    void onSonicEnhancer();
    void onDbxVoice();
    void onPttPressed();
    void onPttReleased();
    void onEncoderContextMenu(const QPoint &pos);
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void onDemoTick();
    void onBroadcastMonitor();
    void onEffectsPanel();
    void onOverlaySettings();
    void showAbout();

private:
    void createActions();
    void createMenus();
    void createTrayIcon();
    void setupUi();
    void populateDeviceCombo();
    void populateCameraCombo();
    void paintBanner();
    void startLevelMonitor();
    void startCameraPreview();
    void stopCameraPreview();
    void saveSettings();
    void restoreSettings();

    /* Phase M10: Config persistence */
    void loadSavedProfiles();
    void saveAllEncoderProfiles();
    void syncDspFromPipeline(EncoderConfig& cfg);
    void applyDspToSlot(int slot_id, const EncoderConfig& cfg);

    /* Toolbar / menu actions */
    QAction *act_connect_     = nullptr;
    QAction *act_add_encoder_ = nullptr;
    QAction *act_agc_         = nullptr;
    QAction *act_prefs_       = nullptr;
    QAction *act_log_         = nullptr;
    QAction *act_preset_      = nullptr;
    QAction *act_eq_          = nullptr;
    QAction *act_eq31_        = nullptr;
    QAction *act_sonic_       = nullptr;
    QAction *act_about_       = nullptr;
    QAction *act_quit_        = nullptr;
    QAction *act_theme_       = nullptr;
    QAction *act_broadcast_   = nullptr;
    QAction *act_dbx_voice_   = nullptr;
    QAction *act_ptt_         = nullptr;

    /* Banner */
    QLabel             *banner_label_   = nullptr;

    /* Central UI widgets */
    QLabel             *lbl_metadata_   = nullptr;
    VuMeterWidget      *vu_meter_       = nullptr;
    QComboBox          *cmb_device_     = nullptr;
    QSlider            *sld_volume_     = nullptr;
    QLabel             *lbl_volume_     = nullptr;
    QPushButton        *btn_connect_    = nullptr;
    QPushButton        *btn_add_        = nullptr;
    QPushButton        *btn_edit_meta_  = nullptr;
    /* Phase M8: PTT */
    QComboBox          *cmb_ptt_device_ = nullptr;
    QPushButton        *btn_ptt_        = nullptr;
    /* Phase M7: Three-tab encoder list */
    QTabWidget         *tab_encoders_   = nullptr;
    QTableView         *tbl_radio_      = nullptr;
    QTableView         *tbl_podcast_    = nullptr;
    QTableView         *tbl_video_      = nullptr;
    EncoderListModel   *model_radio_    = nullptr;
    EncoderListModel   *model_podcast_  = nullptr;
    EncoderListModel   *model_video_    = nullptr;
    /* Phase M8.5: DSP Effects Rack tab */
    DspEffectsRack     *dsp_rack_       = nullptr;
    /* Active model/table — follows current tab selection */
    EncoderListModel   *encoder_model_  = nullptr;
    QTableView         *tbl_encoders_   = nullptr; /* points to active tab's table */

    /* Phase M6: Video */
    QComboBox             *cmb_camera_        = nullptr;
    QPushButton           *btn_video_preview_ = nullptr;
    QPushButton           *btn_broadcast_     = nullptr;
    CameraPreviewWindow   *camera_preview_win_ = nullptr;

    /* Phase M6.5: Broadcast pipeline */
    BroadcastMonitorWindow *broadcast_win_    = nullptr;
    VideoEffectsChain       effects_chain_;
    OverlayRenderer         overlay_renderer_;
    VideoEffectsPanel      *effects_panel_    = nullptr;
    OverlaySettingsDialog  *overlay_dialog_   = nullptr;
    bool                    is_broadcast_live_ = false;

    /* Phase M9: Video stream pipeline */
    std::unique_ptr<VideoStreamPipeline> video_pipeline_;

    /* System tray */
    QSystemTrayIcon    *tray_icon_      = nullptr;
    QMenu              *tray_menu_      = nullptr;

    /* Audio level monitor for VU metering */
    InputLevelMonitor   level_monitor_;

    /* System audio metering via ScreenCaptureKit */
    enum class MeterSource { NONE, INPUT_DEVICE, SYSTEM_AUDIO };
    MeterSource                        meter_source_ = MeterSource::NONE;
    std::unique_ptr<SystemAudioSource> sys_audio_monitor_;
    std::atomic<float>                 sys_peak_left_{-60.0f};
    std::atomic<float>                 sys_peak_right_{-60.0f};

    /* Camera capture source for preview */
    std::unique_ptr<CameraSource> camera_source_;

    /* UI refresh timer */
    QTimer             *demo_timer_     = nullptr;
    float               demo_phase_     = 0.0f;

    /* DNAS stats (Phase M4) */
    DnasStats          *dnas_stats_     = nullptr;
    QLabel             *lbl_dnas_       = nullptr;   // status bar DNAS indicator

    /* Phase M5: persistent dialogs */
    LogViewerDialog    *log_viewer_     = nullptr;

    /* Phase M5: notification tracking */
    int                 prev_live_count_ = 0;

    /* Metadata config (Phase M2) */
    MetadataConfig      metadata_cfg_;

    /* Phase M11: Global config + DSP rack defaults */
    GlobalConfig        global_cfg_;
    DspRackDefaults     dsp_defaults_;
};

} // namespace mc1

#endif // MC1_MAIN_WINDOW_H
