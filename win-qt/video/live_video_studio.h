/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/live_video_studio.h — Live Video Stream Studio dialog
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

class LiveVideoStudioDialog : public QDialog {
    Q_OBJECT

public:
    explicit LiveVideoStudioDialog(const EncoderConfig &cfg, QWidget *parent = nullptr);

    VideoConfig videoConfig() const;

signals:
    void goLiveRequested(const VideoConfig &vcfg, const StreamTargetEntry &target);
    void stopRequested();

private slots:
    void onCodecChanged(int index);
    void onAddTarget();
    void onEditTarget();
    void onRemoveTarget();
    void onGoLive();
    void onStop();
    void onDryRun();
    void updateDuration();

    /* Transitions */
    void onTransition();
    void onTransitionTick();
    void onTransitionDurationChanged(int value);

    /* Sources */
    void onAddSource();
    void onRemoveSource();
    void onSourceDoubleClicked();

private:
    void buildUI();
    void updateContainerForCodec();

    EncoderConfig cfg_;

    /* Camera preview */
    CameraPreviewWidget *preview_ = nullptr;

    /* Video settings */
    QComboBox *combo_codec_     = nullptr;
    QComboBox *combo_container_ = nullptr;
    QComboBox *combo_resolution_ = nullptr;
    QComboBox *combo_fps_       = nullptr;
    QSpinBox  *spin_bitrate_    = nullptr;

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
    QPushButton *btn_transition_         = nullptr;
    QTimer      *transition_tick_timer_  = nullptr;

    /* Sources */
    QListWidget *list_sources_       = nullptr;
    QPushButton *btn_add_source_     = nullptr;
    QPushButton *btn_remove_source_  = nullptr;
    std::vector<SourceEntry> sources_;
    int program_source_idx_ = -1;
    int preview_source_idx_ = -1;

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
};

} // namespace mc1

#endif // MC1_LIVE_VIDEO_STUDIO_H
