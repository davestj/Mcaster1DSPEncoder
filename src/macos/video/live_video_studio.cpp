/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/live_video_studio.cpp — Live Video Stream Studio dialog
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "live_video_studio.h"
#include "stream_target_editor.h"
#include "camera_preview_widget.h"
#include "transition_engine.h"
#include "screen_capture_source.h"
#include "image_source.h"
#include "video_file_source.h"
#include "video_capture_macos.h"

#include <QComboBox>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

namespace mc1 {

LiveVideoStudioDialog::LiveVideoStudioDialog(const EncoderConfig &cfg, QWidget *parent)
    : QDialog(parent), cfg_(cfg)
{
    setWindowTitle(QStringLiteral("Live Video Stream Studio"));
    resize(900, 620);
    buildUI();
}

void LiveVideoStudioDialog::buildUI()
{
    auto *root = new QVBoxLayout(this);

    /* ── Title bar ──────────────────────────────────────────── */
    auto *title = new QLabel(QStringLiteral("LIVE VIDEO STREAM STUDIO"));
    QFont tf = title->font();
    tf.setPointSize(14);
    tf.setBold(true);
    title->setFont(tf);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral("color: #00d4aa; padding: 4px;"));
    root->addWidget(title);

    /* ── Main split: preview (left) | settings (right) ───── */
    auto *splitter = new QSplitter(Qt::Horizontal);

    /* Left: camera preview */
    preview_ = new CameraPreviewWidget;
    preview_->setMinimumSize(400, 300);
    splitter->addWidget(preview_);

    /* Right: settings panel */
    auto *right_panel = new QWidget;
    auto *right_lay = new QVBoxLayout(right_panel);
    right_lay->setContentsMargins(4, 0, 4, 0);

    /* Video Settings group */
    auto *vid_group = new QGroupBox(QStringLiteral("Video Settings"));
    auto *vid_form  = new QFormLayout(vid_group);

    combo_codec_ = new QComboBox;
    combo_codec_->addItem(QStringLiteral("H.264"),   static_cast<int>(VideoConfig::VideoCodec::H264));
    combo_codec_->addItem(QStringLiteral("Theora"),   static_cast<int>(VideoConfig::VideoCodec::THEORA));
    combo_codec_->addItem(QStringLiteral("VP8"),      static_cast<int>(VideoConfig::VideoCodec::VP8));
    combo_codec_->addItem(QStringLiteral("VP9"),      static_cast<int>(VideoConfig::VideoCodec::VP9));
    connect(combo_codec_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LiveVideoStudioDialog::onCodecChanged);
    vid_form->addRow(QStringLiteral("Codec:"), combo_codec_);

    combo_container_ = new QComboBox;
    vid_form->addRow(QStringLiteral("Container:"), combo_container_);
    updateContainerForCodec();

    combo_resolution_ = new QComboBox;
    combo_resolution_->addItem(QStringLiteral("480p  (854x480)"),   QStringLiteral("854x480"));
    combo_resolution_->addItem(QStringLiteral("720p  (1280x720)"),  QStringLiteral("1280x720"));
    combo_resolution_->addItem(QStringLiteral("1080p (1920x1080)"), QStringLiteral("1920x1080"));
    combo_resolution_->setCurrentIndex(1); /* 720p default */
    vid_form->addRow(QStringLiteral("Resolution:"), combo_resolution_);

    combo_fps_ = new QComboBox;
    combo_fps_->addItems({QStringLiteral("15"), QStringLiteral("24"),
                          QStringLiteral("25"), QStringLiteral("30"),
                          QStringLiteral("60")});
    combo_fps_->setCurrentIndex(3); /* 30 fps default */
    vid_form->addRow(QStringLiteral("FPS:"), combo_fps_);

    spin_bitrate_ = new QSpinBox;
    spin_bitrate_->setRange(500, 20000);
    spin_bitrate_->setValue(cfg_.video.bitrate_kbps);
    spin_bitrate_->setSuffix(QStringLiteral(" kbps"));
    spin_bitrate_->setSingleStep(500);
    vid_form->addRow(QStringLiteral("Bitrate:"), spin_bitrate_);

    right_lay->addWidget(vid_group);

    /* Transitions group */
    auto *trans_group = new QGroupBox(QStringLiteral("Transitions"));
    auto *trans_lay   = new QVBoxLayout(trans_group);

    auto *trans_type_row = new QHBoxLayout;
    trans_type_row->addWidget(new QLabel(QStringLiteral("Type:")));
    combo_transition_type_ = new QComboBox;
    combo_transition_type_->addItem(QStringLiteral("Cut"),           0);
    combo_transition_type_->addItem(QStringLiteral("Crossfade"),     1);
    combo_transition_type_->addItem(QStringLiteral("Fade to Black"), 2);
    combo_transition_type_->addItem(QStringLiteral("Wipe Left"),     3);
    combo_transition_type_->addItem(QStringLiteral("Wipe Right"),    4);
    combo_transition_type_->setCurrentIndex(1); /* Crossfade default */
    trans_type_row->addWidget(combo_transition_type_);
    trans_lay->addLayout(trans_type_row);

    auto *trans_dur_row = new QHBoxLayout;
    trans_dur_row->addWidget(new QLabel(QStringLiteral("Duration:")));
    slider_transition_dur_ = new QSlider(Qt::Horizontal);
    slider_transition_dur_->setRange(200, 3000);
    slider_transition_dur_->setValue(1000);
    slider_transition_dur_->setSingleStep(100);
    connect(slider_transition_dur_, &QSlider::valueChanged,
            this, &LiveVideoStudioDialog::onTransitionDurationChanged);
    trans_dur_row->addWidget(slider_transition_dur_);
    lbl_transition_dur_ = new QLabel(QStringLiteral("1.0s"));
    lbl_transition_dur_->setMinimumWidth(36);
    trans_dur_row->addWidget(lbl_transition_dur_);
    trans_lay->addLayout(trans_dur_row);

    btn_transition_ = new QPushButton(QStringLiteral("TRANSITION"));
    btn_transition_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #00d4aa; color: #0a0a1e; padding: 6px 12px; "
        "border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background: #00e8bb; }"));
    connect(btn_transition_, &QPushButton::clicked,
            this, &LiveVideoStudioDialog::onTransition);
    trans_lay->addWidget(btn_transition_);

    right_lay->addWidget(trans_group);

    /* Sources group */
    auto *src_group = new QGroupBox(QStringLiteral("Sources"));
    auto *src_lay   = new QVBoxLayout(src_group);

    list_sources_ = new QListWidget;
    list_sources_->setMaximumHeight(100);
    connect(list_sources_, &QListWidget::itemDoubleClicked,
            this, &LiveVideoStudioDialog::onSourceDoubleClicked);
    src_lay->addWidget(list_sources_);

    auto *src_btn_row = new QHBoxLayout;
    btn_add_source_ = new QPushButton(QStringLiteral("+ Add Source"));
    btn_remove_source_ = new QPushButton(QStringLiteral("Remove"));
    btn_remove_source_->setEnabled(false);
    connect(btn_add_source_, &QPushButton::clicked,
            this, &LiveVideoStudioDialog::onAddSource);
    connect(btn_remove_source_, &QPushButton::clicked,
            this, &LiveVideoStudioDialog::onRemoveSource);
    connect(list_sources_, &QListWidget::currentRowChanged, this, [this](int row) {
        btn_remove_source_->setEnabled(row >= 0);
    });
    src_btn_row->addWidget(btn_add_source_);
    src_btn_row->addWidget(btn_remove_source_);
    src_btn_row->addStretch();
    src_lay->addLayout(src_btn_row);

    right_lay->addWidget(src_group);

    /* Stream Targets group */
    auto *tgt_group = new QGroupBox(QStringLiteral("Stream Targets"));
    auto *tgt_lay   = new QVBoxLayout(tgt_group);

    list_targets_ = new QListWidget;
    list_targets_->setMaximumHeight(120);
    tgt_lay->addWidget(list_targets_);

    auto *tgt_btn_row = new QHBoxLayout;
    btn_add_target_ = new QPushButton(QStringLiteral("+ Add"));
    btn_edit_target_ = new QPushButton(QStringLiteral("Edit"));
    btn_remove_target_ = new QPushButton(QStringLiteral("Remove"));
    btn_edit_target_->setEnabled(false);
    btn_remove_target_->setEnabled(false);
    connect(btn_add_target_, &QPushButton::clicked, this, &LiveVideoStudioDialog::onAddTarget);
    connect(btn_edit_target_, &QPushButton::clicked, this, &LiveVideoStudioDialog::onEditTarget);
    connect(btn_remove_target_, &QPushButton::clicked, this, &LiveVideoStudioDialog::onRemoveTarget);
    connect(list_targets_, &QListWidget::currentRowChanged, this, [this](int row) {
        btn_edit_target_->setEnabled(row >= 0);
        btn_remove_target_->setEnabled(row >= 0);
    });
    tgt_btn_row->addWidget(btn_add_target_);
    tgt_btn_row->addWidget(btn_edit_target_);
    tgt_btn_row->addWidget(btn_remove_target_);
    tgt_btn_row->addStretch();
    tgt_lay->addLayout(tgt_btn_row);

    right_lay->addWidget(tgt_group);
    right_lay->addStretch();

    splitter->addWidget(right_panel);
    splitter->setStretchFactor(0, 3); /* preview gets more space */
    splitter->setStretchFactor(1, 2);

    root->addWidget(splitter, 1);

    /* ── Bottom control bar ─────────────────────────────────── */
    auto *bot = new QHBoxLayout;

    lbl_status_ = new QLabel(QStringLiteral("IDLE"));
    lbl_status_->setStyleSheet(QStringLiteral(
        "font-weight: bold; font-size: 12px; color: #667788; padding: 4px 8px;"));
    bot->addWidget(lbl_status_);

    lbl_duration_ = new QLabel(QStringLiteral("--:--:--"));
    lbl_duration_->setStyleSheet(QStringLiteral(
        "font-family: monospace; font-size: 12px; color: #99aabb; padding: 4px 8px;"));
    bot->addWidget(lbl_duration_);

    bot->addStretch();

    btn_dry_run_ = new QPushButton(QStringLiteral("DRY RUN"));
    btn_dry_run_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #333355; color: #ccccdd; padding: 6px 16px; "
        "border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background: #444466; }"));
    connect(btn_dry_run_, &QPushButton::clicked, this, &LiveVideoStudioDialog::onDryRun);
    bot->addWidget(btn_dry_run_);

    btn_go_live_ = new QPushButton(QStringLiteral("GO LIVE"));
    btn_go_live_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #00d4aa; color: #0a0a1e; padding: 6px 16px; "
        "border-radius: 4px; font-weight: bold; font-size: 13px; }"
        "QPushButton:hover { background: #00e8bb; }"));
    connect(btn_go_live_, &QPushButton::clicked, this, &LiveVideoStudioDialog::onGoLive);
    bot->addWidget(btn_go_live_);

    btn_stop_ = new QPushButton(QStringLiteral("STOP"));
    btn_stop_->setEnabled(false);
    btn_stop_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #aa2233; color: white; padding: 6px 16px; "
        "border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background: #cc3344; }"
        "QPushButton:disabled { background: #444; color: #666; }"));
    connect(btn_stop_, &QPushButton::clicked, this, &LiveVideoStudioDialog::onStop);
    bot->addWidget(btn_stop_);

    root->addLayout(bot);

    /* Duration timer */
    duration_timer_ = new QTimer(this);
    duration_timer_->setInterval(1000);
    connect(duration_timer_, &QTimer::timeout, this, &LiveVideoStudioDialog::updateDuration);

    /* Transition engine + tick timer */
    transition_engine_ = new TransitionEngine;
    transition_tick_timer_ = new QTimer(this);
    transition_tick_timer_->setInterval(33); /* ~30 fps */
    connect(transition_tick_timer_, &QTimer::timeout,
            this, &LiveVideoStudioDialog::onTransitionTick);

    /* Load initial values from config */
    int codec_idx = combo_codec_->findData(static_cast<int>(cfg_.video.video_codec));
    if (codec_idx >= 0) combo_codec_->setCurrentIndex(codec_idx);

    /* Resolution */
    QString res = QString("%1x%2").arg(cfg_.video.width).arg(cfg_.video.height);
    int res_idx = combo_resolution_->findData(res);
    if (res_idx >= 0) combo_resolution_->setCurrentIndex(res_idx);

    /* FPS */
    int fps_idx = combo_fps_->findText(QString::number(cfg_.video.fps));
    if (fps_idx >= 0) combo_fps_->setCurrentIndex(fps_idx);
}

VideoConfig LiveVideoStudioDialog::videoConfig() const
{
    VideoConfig vc = cfg_.video;

    vc.video_codec = static_cast<VideoConfig::VideoCodec>(
        combo_codec_->currentData().toInt());
    vc.video_container = static_cast<VideoConfig::VideoContainer>(
        combo_container_->currentData().toInt());

    /* Parse resolution */
    QString res = combo_resolution_->currentData().toString();
    auto parts = res.split('x');
    if (parts.size() == 2) {
        vc.width  = parts[0].toInt();
        vc.height = parts[1].toInt();
    }

    vc.fps          = combo_fps_->currentText().toInt();
    vc.bitrate_kbps = spin_bitrate_->value();

    return vc;
}

void LiveVideoStudioDialog::onCodecChanged(int /*index*/)
{
    updateContainerForCodec();
}

void LiveVideoStudioDialog::updateContainerForCodec()
{
    combo_container_->clear();

    auto codec = static_cast<VideoConfig::VideoCodec>(
        combo_codec_->currentData().toInt());

    switch (codec) {
    case VideoConfig::VideoCodec::H264:
        combo_container_->addItem(QStringLiteral("FLV"),  static_cast<int>(VideoConfig::VideoContainer::FLV));
        combo_container_->addItem(QStringLiteral("MKV"),  static_cast<int>(VideoConfig::VideoContainer::MKV));
        break;
    case VideoConfig::VideoCodec::THEORA:
        combo_container_->addItem(QStringLiteral("OGG"),  static_cast<int>(VideoConfig::VideoContainer::OGG));
        break;
    case VideoConfig::VideoCodec::VP8:
    case VideoConfig::VideoCodec::VP9:
        combo_container_->addItem(QStringLiteral("WebM"), static_cast<int>(VideoConfig::VideoContainer::WEBM));
        combo_container_->addItem(QStringLiteral("MKV"),  static_cast<int>(VideoConfig::VideoContainer::MKV));
        break;
    }
}

void LiveVideoStudioDialog::onAddTarget()
{
    auto *editor = new StreamTargetEditor(StreamTargetEditor::ADD, this,
                                          StreamTargetEditor::VIDEO);
    if (editor->exec() == QDialog::Accepted) {
        auto entry = editor->target();
        targets_.append(entry);

        QString label;
        if (entry.server_type == "HLS (Local)") {
            label = QString("HLS (Local) — %1")
                .arg(QString::fromStdString(entry.output_dir));
        } else {
            label = QString("%1 — %2:%3%4")
                .arg(QString::fromStdString(entry.server_type))
                .arg(QString::fromStdString(entry.host))
                .arg(entry.port)
                .arg(QString::fromStdString(entry.mount));
        }
        list_targets_->addItem(label);
    }
    editor->deleteLater();
}

void LiveVideoStudioDialog::onEditTarget()
{
    int row = list_targets_->currentRow();
    if (row < 0 || row >= targets_.size()) return;

    auto *editor = new StreamTargetEditor(StreamTargetEditor::EDIT, this,
                                          StreamTargetEditor::VIDEO);
    editor->setTarget(targets_[row]);
    if (editor->exec() == QDialog::Accepted) {
        targets_[row] = editor->target();
        auto &e = targets_[row];
        QString label;
        if (e.server_type == "HLS (Local)") {
            label = QString("HLS (Local) — %1")
                .arg(QString::fromStdString(e.output_dir));
        } else {
            label = QString("%1 — %2:%3%4")
                .arg(QString::fromStdString(e.server_type))
                .arg(QString::fromStdString(e.host))
                .arg(e.port)
                .arg(QString::fromStdString(e.mount));
        }
        list_targets_->item(row)->setText(label);
    }
    editor->deleteLater();
}

void LiveVideoStudioDialog::onRemoveTarget()
{
    int row = list_targets_->currentRow();
    if (row >= 0 && row < targets_.size()) {
        targets_.removeAt(row);
        delete list_targets_->takeItem(row);
    }
}

void LiveVideoStudioDialog::onGoLive()
{
    if (targets_.isEmpty()) {
        QMessageBox::warning(this,
            QStringLiteral("No Targets"),
            QStringLiteral("Add at least one stream target before going live."));
        return;
    }

    is_live_ = true;
    is_dry_run_ = false;
    btn_go_live_->setEnabled(false);
    btn_dry_run_->setEnabled(false);
    btn_stop_->setEnabled(true);
    lbl_status_->setText(QStringLiteral("LIVE ON-AIR"));
    lbl_status_->setStyleSheet(QStringLiteral(
        "font-weight: bold; font-size: 12px; color: #ff3344; padding: 4px 8px;"));

    start_ms_ = QDateTime::currentMSecsSinceEpoch();
    duration_timer_->start();

    emit goLiveRequested(videoConfig(), targets_.first());
}

void LiveVideoStudioDialog::onStop()
{
    is_live_ = false;
    is_dry_run_ = false;
    duration_timer_->stop();

    btn_go_live_->setEnabled(true);
    btn_dry_run_->setEnabled(true);
    btn_stop_->setEnabled(false);
    lbl_status_->setText(QStringLiteral("IDLE"));
    lbl_status_->setStyleSheet(QStringLiteral(
        "font-weight: bold; font-size: 12px; color: #667788; padding: 4px 8px;"));
    lbl_duration_->setText(QStringLiteral("--:--:--"));

    emit stopRequested();
}

void LiveVideoStudioDialog::onDryRun()
{
    is_dry_run_ = !is_dry_run_;

    if (is_dry_run_) {
        btn_dry_run_->setStyleSheet(QStringLiteral(
            "QPushButton { background: #ff8800; color: #0a0a1e; padding: 6px 16px; "
            "border-radius: 4px; font-weight: bold; }"
            "QPushButton:hover { background: #ffaa33; }"));
        lbl_status_->setText(QStringLiteral("DRY RUN"));
        lbl_status_->setStyleSheet(QStringLiteral(
            "font-weight: bold; font-size: 12px; color: #ff8800; padding: 4px 8px;"));
        start_ms_ = QDateTime::currentMSecsSinceEpoch();
        duration_timer_->start();
    } else {
        btn_dry_run_->setStyleSheet(QStringLiteral(
            "QPushButton { background: #333355; color: #ccccdd; padding: 6px 16px; "
            "border-radius: 4px; font-weight: bold; }"
            "QPushButton:hover { background: #444466; }"));
        lbl_status_->setText(QStringLiteral("IDLE"));
        lbl_status_->setStyleSheet(QStringLiteral(
            "font-weight: bold; font-size: 12px; color: #667788; padding: 4px 8px;"));
        duration_timer_->stop();
        lbl_duration_->setText(QStringLiteral("--:--:--"));
    }
}

void LiveVideoStudioDialog::updateDuration()
{
    qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - start_ms_;
    int secs = static_cast<int>(elapsed / 1000);
    int h = secs / 3600;
    int m = (secs % 3600) / 60;
    int s = secs % 60;
    lbl_duration_->setText(QString("%1:%2:%3")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0')));
}

/* ── Transition slots ───────────────────────────────────────────────────── */

void LiveVideoStudioDialog::onTransition()
{
    if (!transition_engine_ || transition_engine_->is_transitioning()) return;

    int type_idx = combo_transition_type_->currentData().toInt();
    transition_engine_->set_type(static_cast<TransitionEngine::Type>(type_idx));

    float dur = slider_transition_dur_->value() / 1000.0f;
    transition_engine_->set_duration(dur);

    transition_engine_->begin();
    transition_tick_timer_->start();

    btn_transition_->setEnabled(false);
    btn_transition_->setText(QStringLiteral("TRANSITIONING..."));
}

void LiveVideoStudioDialog::onTransitionTick()
{
    if (!transition_engine_) return;

    bool still_going = transition_engine_->tick(0.033);

    if (!still_going) {
        transition_tick_timer_->stop();
        btn_transition_->setEnabled(true);
        btn_transition_->setText(QStringLiteral("TRANSITION"));

        /* Swap program/preview sources */
        if (preview_source_idx_ >= 0) {
            std::swap(program_source_idx_, preview_source_idx_);
        }
    }
}

void LiveVideoStudioDialog::onTransitionDurationChanged(int value)
{
    float sec = value / 1000.0f;
    lbl_transition_dur_->setText(QString("%1s").arg(sec, 0, 'f', 1));
}

/* ── Source management slots ────────────────────────────────────────────── */

void LiveVideoStudioDialog::onAddSource()
{
    auto *menu = new QMenu(this);

    /* Camera sub-menu */
    auto *cam_menu = menu->addMenu(QStringLiteral("Add Camera..."));
    auto devices = CameraSource::enumerate_devices();
    if (devices.empty()) {
        cam_menu->addAction(QStringLiteral("(No cameras found)"))->setEnabled(false);
    } else {
        for (auto &dev : devices) {
            QString label = QString::fromStdString(dev.name);
            QString devId = QString::fromStdString(dev.unique_id);
            cam_menu->addAction(label, this, [this, devId, label]() {
                SourceEntry entry;
                entry.type  = SourceEntry::CAMERA;
                entry.label = "Camera: " + label.toStdString();
                entry.path  = devId.toStdString();
                list_sources_->addItem(QString::fromStdString(entry.label));
                sources_.push_back(std::move(entry));
                if (program_source_idx_ < 0)
                    program_source_idx_ = 0;
            });
        }
    }

    /* Screen capture sub-menu */
    if (ScreenCaptureSource::is_available()) {
        auto *screen_menu = menu->addMenu(QStringLiteral("Add Screen Capture..."));
        auto displays = ScreenCaptureSource::enumerate_displays();
        for (auto &disp : displays) {
            QString label = QString::fromStdString(disp.name)
                + QString(" (%1x%2)").arg(disp.width).arg(disp.height);
            uint32_t did = disp.display_id;
            screen_menu->addAction(label, this, [this, did, label]() {
                SourceEntry entry;
                entry.type  = SourceEntry::SCREEN;
                entry.label = "Screen: " + label.toStdString();
                entry.path  = std::to_string(did);
                list_sources_->addItem(QString::fromStdString(entry.label));
                sources_.push_back(std::move(entry));
            });
        }
    }

    /* Image file */
    menu->addAction(QStringLiteral("Add Image..."), this, [this]() {
        QString path = QFileDialog::getOpenFileName(this,
            QStringLiteral("Select Image"),
            QString(),
            QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.tiff)"));
        if (path.isEmpty()) return;

        QFileInfo fi(path);
        SourceEntry entry;
        entry.type  = SourceEntry::IMAGE;
        entry.label = "Image: " + fi.fileName().toStdString();
        entry.path  = path.toStdString();
        list_sources_->addItem(QString::fromStdString(entry.label));
        sources_.push_back(std::move(entry));
    });

    /* Video file */
    menu->addAction(QStringLiteral("Add Video File..."), this, [this]() {
        QString path = QFileDialog::getOpenFileName(this,
            QStringLiteral("Select Video File"),
            QString(),
            QStringLiteral("Video Files (*.mp4 *.mov *.mkv *.avi *.webm)"));
        if (path.isEmpty()) return;

        QFileInfo fi(path);
        SourceEntry entry;
        entry.type  = SourceEntry::VIDEO_FILE;
        entry.label = "Video: " + fi.fileName().toStdString();
        entry.path  = path.toStdString();
        list_sources_->addItem(QString::fromStdString(entry.label));
        sources_.push_back(std::move(entry));
    });

    menu->popup(btn_add_source_->mapToGlobal(
        QPoint(0, btn_add_source_->height())));
}

void LiveVideoStudioDialog::onRemoveSource()
{
    int row = list_sources_->currentRow();
    if (row < 0 || row >= static_cast<int>(sources_.size())) return;

    /* Stop the source if running */
    if (sources_[row].source && sources_[row].source->is_running())
        sources_[row].source->stop();

    sources_.erase(sources_.begin() + row);
    delete list_sources_->takeItem(row);

    /* Adjust indices */
    if (program_source_idx_ == row)
        program_source_idx_ = sources_.empty() ? -1 : 0;
    else if (program_source_idx_ > row)
        --program_source_idx_;

    if (preview_source_idx_ == row)
        preview_source_idx_ = -1;
    else if (preview_source_idx_ > row)
        --preview_source_idx_;
}

void LiveVideoStudioDialog::onSourceDoubleClicked()
{
    int row = list_sources_->currentRow();
    if (row < 0 || row >= static_cast<int>(sources_.size())) return;

    /* Set double-clicked source as preview (next transition target) */
    preview_source_idx_ = row;

    /* Visual feedback */
    for (int i = 0; i < list_sources_->count(); ++i) {
        auto *item = list_sources_->item(i);
        if (i == program_source_idx_)
            item->setBackground(QColor(0, 212, 170, 40));   /* teal tint = program */
        else if (i == preview_source_idx_)
            item->setBackground(QColor(255, 136, 0, 40));    /* orange tint = preview */
        else
            item->setBackground(Qt::transparent);
    }
}

} // namespace mc1
