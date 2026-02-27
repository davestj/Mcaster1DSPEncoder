/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * video/live_video_studio.h — Live Video Stream Studio dialog
 *
 * Pro-grade multi-source video switcher with:
 *   - 3 source preview panes (each with device selector)
 *   - 1 program monitor (composited output = what gets streamed)
 *   - Audio device capture + codec constraint by container
 *   - Transition engine (Cut, Crossfade, Fade to Black, Wipe)
 *   - Stream target management (RTMP, Icecast, HLS)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_LIVE_VIDEO_STUDIO_H
#define MC1_LIVE_VIDEO_STUDIO_H

#include <QDialog>
#include <QVector>
#include <memory>
#include <string>
#include <vector>
#include "config_types.h"
#include "video_source.h"
#include "image_source.h"
#include "video_stream_monitor.h"
#include "../virtual_camera/vcam_frame_writer.h"

class QComboBox;
class QMenu;
class QSlider;
class QSpinBox;
class QLabel;
class QPushButton;
class QListWidget;
class QElapsedTimer;
class QTimer;

namespace mc1 {

class CameraPreviewWidget;
class CameraSource;
class TransitionEngine;

/* Represents a single stream target */
struct StreamTargetEntry {
    std::string server_type;   /* "Icecast2", "Mcaster1 DNAS", "YouTube Live", "Twitch", "HLS (Local)" */
    std::string host;
    int         port = 8000;
    std::string mount;
    std::string password;
    std::string stream_key;    /* RTMP stream key for YouTube/Twitch */
    std::string output_dir;    /* HLS output directory */
    int         hls_segment_duration = 6;
    int         hls_max_segments     = 5;
};

/* Represents a video source entry in the source list */
struct SourceEntry {
    enum Type { CAMERA, SCREEN, IMAGE, VIDEO_FILE };
    Type type = CAMERA;
    std::string label;
    std::string path;          /* device ID, display ID, or file path */
    std::unique_ptr<VideoSource> source;
};

/* Audio codec compatibility entry */
struct AudioCodecOption {
    EncoderConfig::Codec codec;
    const char* label;
};

class LiveVideoStudioDialog : public QDialog {
    Q_OBJECT

public:
    explicit LiveVideoStudioDialog(const EncoderConfig &cfg, QWidget *parent = nullptr);
    LiveVideoStudioDialog(const EncoderConfig &cfg,
                          const std::vector<EncoderConfig> &tv_encoders,
                          QWidget *parent = nullptr);
    ~LiveVideoStudioDialog() override;

    VideoConfig videoConfig() const;

    /* Returns the active program video source (camera, image, or video file).
     * May be nullptr if no program source is set. */
    VideoSource *programSource() const;

    /* Returns the studio's transition engine for the pipeline to use */
    TransitionEngine *transitionEngine() const { return transition_engine_; }

    /* Called by MainWindow after pipeline confirms stream is live / stopped */
    void confirmLive();
    void confirmStopped();

    /* Set the active video pipeline so program frames are forwarded to it */
    void setVideoPipeline(class VideoStreamPipeline *pipeline);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

signals:
    void goLiveRequested(const VideoConfig &vcfg, const StreamTargetEntry &target);
    void stopRequested();

private slots:
    void onCodecChanged(int index);
    void onContainerChanged(int index);
    void onAddTarget();
    void onEditTarget();
    void onRemoveTarget();
    void onGoLive();
    void onStop();
    void onDryRun();
    void updateDuration();

    /* Source pane device selection */
    void onSourceDeviceChanged(int pane_index, int combo_index);

    /* Transitions */
    void onTransitionCut();
    void onTransitionAuto();
    void onTransitionTick();
    void onTransitionDurationChanged(int value);

    /* Sources (legacy list) */
    void onAddSource();
    void onRemoveSource();
    void onSourceDoubleClicked();

    /* Virtual Camera */
    void onManageDevices();
    void onVCamToggle();

    /* Stream Monitor */
    void onStreamMonitor();

    /* Drag-and-drop / context menu */
    void onDropFile(int pane_index, const QString &path);
    void showPaneContextMenu(int pane_index, const QPoint &globalPos);

private:
    void buildUI();
    void populateTargetsFromEncoders(const std::vector<EncoderConfig> &tv_encoders);
    void saveStudioConfig();
    void loadStudioConfig();
    static std::string studioConfigPath();
    void updateContainerForCodec();
    void updateAudioCodecsForContainer();
    void updateAudioBitrateRange();
    void populateSourceDeviceCombos();
    void startSourceCapture(int pane_index, int device_idx);
    void stopSourceCapture(int pane_index);
    void updateProgramMonitor();
    void selectPreviewPane(int pane_index);

    /* Codec compatibility matrix */
    static std::vector<AudioCodecOption> audioCodecsForContainer(
        VideoConfig::VideoContainer container);
    static std::pair<int,int> audioBitrateRange(EncoderConfig::Codec codec);

    EncoderConfig cfg_;

    /* ── Source preview panes (3 small previews) ── */
    static constexpr int kSourcePaneCount = 3;
    enum class PaneSourceType { NONE, CAMERA, IMAGE, VIDEO_FILE };
    struct SourcePane {
        CameraPreviewWidget *preview  = nullptr;
        QComboBox           *combo    = nullptr;
        QLabel              *label    = nullptr;
        std::unique_ptr<CameraSource> camera;
        std::unique_ptr<ImageSource>  image_src;
        std::unique_ptr<VideoSource>  video_src;  /* VideoFileSource */
        PaneSourceType source_type = PaneSourceType::NONE;
        QString        source_path;               /* file path for image/video */
        int device_index = -1;  /* -1 = none */
    };
    SourcePane source_panes_[kSourcePaneCount];

    /* Program monitor (large — composited output) */
    CameraPreviewWidget *program_monitor_ = nullptr;
    QLabel              *lbl_program_     = nullptr;

    /* Which source pane is PROGRAM (green border) and PREVIEW (orange border) */
    int program_pane_idx_ = -1;
    int preview_pane_idx_ = -1;

    /* Video settings */
    QComboBox *combo_codec_     = nullptr;
    QComboBox *combo_container_ = nullptr;
    QComboBox *combo_resolution_ = nullptr;
    QComboBox *combo_fps_       = nullptr;
    QSpinBox  *spin_bitrate_    = nullptr;

    /* Audio settings (NEW) */
    QComboBox *combo_audio_device_  = nullptr;
    QComboBox *combo_audio_codec_   = nullptr;
    QSpinBox  *spin_audio_bitrate_  = nullptr;
    QComboBox *combo_audio_rate_    = nullptr;
    QComboBox *combo_audio_ch_      = nullptr;

    /* Stream targets */
    QListWidget *list_targets_ = nullptr;
    QPushButton *btn_add_target_ = nullptr;
    QPushButton *btn_edit_target_ = nullptr;
    QPushButton *btn_remove_target_ = nullptr;
    QVector<StreamTargetEntry> targets_;

    /* Transitions */
    TransitionEngine *transition_engine_ = nullptr;
    QComboBox   *combo_transition_type_  = nullptr;
    QSlider     *slider_transition_dur_  = nullptr;
    QLabel      *lbl_transition_dur_     = nullptr;
    QPushButton *btn_cut_               = nullptr;
    QPushButton *btn_transition_         = nullptr;
    QTimer      *transition_tick_timer_  = nullptr;

    /* Sources list (legacy, for images/video files) */
    QListWidget *list_sources_       = nullptr;
    QPushButton *btn_add_source_     = nullptr;
    QPushButton *btn_remove_source_  = nullptr;
    std::vector<SourceEntry> sources_;

    /* Controls */
    QPushButton *btn_go_live_ = nullptr;
    QPushButton *btn_stop_    = nullptr;
    QPushButton *btn_dry_run_ = nullptr;
    QLabel      *lbl_status_  = nullptr;
    QLabel      *lbl_duration_ = nullptr;
    QTimer      *duration_timer_ = nullptr;

    bool is_live_    = false;
    bool is_dry_run_ = false;
    qint64 start_ms_ = 0;

    /* Reusable buffer for transition blending output */
    std::vector<uint8_t> blend_buf_;

    /* Active video pipeline — program frames forwarded when non-null */
    VideoStreamPipeline *video_pipeline_ = nullptr;

    /* ON-AIR flashing indicator */
    QLabel  *lbl_on_air_     = nullptr;
    QTimer  *on_air_timer_   = nullptr;
    bool     on_air_visible_ = true;

    /* Virtual Camera frame writer */
    VCamFrameWriter  vcam_writer_;
    QPushButton     *btn_manage_devices_ = nullptr;
    QPushButton     *btn_vcam_toggle_    = nullptr;
    bool             vcam_active_        = false;

    /* Video Stream Monitor (AIR/CUE) */
    VideoStreamMonitor *stream_monitor_     = nullptr;
    QPushButton        *btn_stream_monitor_ = nullptr;
};

} // namespace mc1

#endif // MC1_LIVE_VIDEO_STUDIO_H
