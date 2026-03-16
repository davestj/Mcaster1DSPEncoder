/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * video/live_video_studio.cpp — Live Video Stream Studio dialog
 *
 * Pro-grade multi-source video switcher:
 *   3 source previews + 1 program monitor + audio/video codec constraint
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "live_video_studio.h"
#include "manage_devices_dialog.h"
#include "stream_target_editor.h"
#include "camera_preview_widget.h"
#include "transition_engine.h"
#include "screen_capture_source.h"
#include "image_source.h"
#include "video_file_source.h"
#include "video_stream_pipeline.h"
#ifdef _WIN32
#include "video_capture_windows.h"
#else
#include "video_capture_macos.h"
#endif
#include "../audio_source.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <yaml.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

namespace mc1 {

// ── Codec Compatibility Matrix ──────────────────────────────────────────
//
// Container determines which audio codecs can be muxed with the video stream.
// The video codec determines which containers are valid.
//
//   H.264  → FLV  → AAC-LC, AAC-HE, MP3
//   H.264  → MKV  → AAC-LC, AAC-HE, MP3, Vorbis, Opus, FLAC
//   Theora → OGG  → Vorbis
//   VP8    → WebM → Vorbis, Opus
//   VP9    → WebM → Vorbis, Opus
//   VP8    → MKV  → Vorbis, Opus, AAC-LC, MP3
//   VP9    → MKV  → Vorbis, Opus, AAC-LC, MP3

std::vector<AudioCodecOption>
LiveVideoStudioDialog::audioCodecsForContainer(VideoConfig::VideoContainer container)
{
    switch (container) {
    case VideoConfig::VideoContainer::FLV:
        return {
            { EncoderConfig::Codec::AAC_LC,  "AAC-LC"  },
            { EncoderConfig::Codec::AAC_HE,  "HE-AAC"  },
            { EncoderConfig::Codec::MP3,     "MP3"     },
        };
    case VideoConfig::VideoContainer::WEBM:
        return {
            { EncoderConfig::Codec::VORBIS,  "Vorbis"  },
            { EncoderConfig::Codec::OPUS,    "Opus"    },
        };
    case VideoConfig::VideoContainer::OGG:
        return {
            { EncoderConfig::Codec::VORBIS,  "Vorbis"  },
        };
    case VideoConfig::VideoContainer::MKV:
        return {
            { EncoderConfig::Codec::AAC_LC,  "AAC-LC"  },
            { EncoderConfig::Codec::AAC_HE,  "HE-AAC"  },
            { EncoderConfig::Codec::MP3,     "MP3"     },
            { EncoderConfig::Codec::VORBIS,  "Vorbis"  },
            { EncoderConfig::Codec::OPUS,    "Opus"    },
            { EncoderConfig::Codec::FLAC,    "FLAC"    },
        };
    }
    return {};
}

std::pair<int,int>
LiveVideoStudioDialog::audioBitrateRange(EncoderConfig::Codec codec)
{
    switch (codec) {
    case EncoderConfig::Codec::MP3:       return {64, 320};
    case EncoderConfig::Codec::VORBIS:    return {64, 320};
    case EncoderConfig::Codec::OPUS:      return {32, 320};
    case EncoderConfig::Codec::FLAC:      return {0, 0};      /* lossless */
    case EncoderConfig::Codec::AAC_LC:    return {64, 320};
    case EncoderConfig::Codec::AAC_HE:    return {24, 128};
    case EncoderConfig::Codec::AAC_HE_V2: return {16, 64};
    case EncoderConfig::Codec::AAC_ELD:   return {24, 192};
    }
    return {64, 320};
}

// ── Constructor / Destructor ────────────────────────────────────────────

LiveVideoStudioDialog::LiveVideoStudioDialog(const EncoderConfig &cfg,
                                             QWidget *parent)
    : QDialog(parent), cfg_(cfg)
{
    setWindowFlags(Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    setWindowTitle(QStringLiteral("Live Video Stream Studio"));
    resize(1200, 780);
    buildUI();
}

LiveVideoStudioDialog::LiveVideoStudioDialog(const EncoderConfig &cfg,
                                             const std::vector<EncoderConfig> &tv_encoders,
                                             QWidget *parent)
    : QDialog(parent), cfg_(cfg)
{
    setWindowFlags(Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    setWindowTitle(QStringLiteral("Live Video Stream Studio"));
    resize(1200, 780);
    buildUI();
    populateTargetsFromEncoders(tv_encoders);
}

LiveVideoStudioDialog::~LiveVideoStudioDialog()
{
    /* Save config on close */
    saveStudioConfig();

    /* Stop virtual camera output */
    vcam_writer_.close();

    /* Stop all source cameras */
    for (int i = 0; i < kSourcePaneCount; ++i)
        stopSourceCapture(i);
}

// ── Populate targets from TV/Video encoder slots ────────────────────────

void LiveVideoStudioDialog::populateTargetsFromEncoders(
    const std::vector<EncoderConfig> &tv_encoders)
{
    for (const auto &enc : tv_encoders) {
        if (enc.encoder_type != EncoderConfig::EncoderType::TV_VIDEO)
            continue;
        const auto &st = enc.stream_target;
        if (st.host.empty()) continue;

        /* Skip if a target with the same host:port/mount is already loaded */
        bool dup = false;
        for (const auto &existing : targets_) {
            if (existing.host == st.host &&
                existing.port == static_cast<int>(st.port) &&
                existing.mount == st.mount) {
                dup = true;
                break;
            }
        }
        if (dup) continue;

        StreamTargetEntry entry;
        entry.host     = st.host;
        entry.port     = st.port;
        entry.mount    = st.mount;
        entry.password = st.password;

        switch (st.protocol) {
        case StreamTarget::Protocol::ICECAST2:
            entry.server_type = "Icecast2";
            break;
        case StreamTarget::Protocol::MCASTER1_DNAS:
            entry.server_type = "Mcaster1 DNAS";
            break;
        case StreamTarget::Protocol::YOUTUBE:
            entry.server_type = "YouTube Live";
            entry.stream_key  = st.password;
            break;
        case StreamTarget::Protocol::TWITCH:
            entry.server_type = "Twitch";
            entry.stream_key  = st.password;
            break;
        default:
            entry.server_type = "Icecast2";
            break;
        }

        targets_.append(entry);

        QString label;
        if (!enc.name.empty()) {
            label = QString("[%1] %2 — %3:%4%5")
                .arg(QString::fromStdString(enc.name))
                .arg(QString::fromStdString(entry.server_type))
                .arg(QString::fromStdString(entry.host))
                .arg(entry.port)
                .arg(QString::fromStdString(entry.mount));
        } else {
            label = QString("%1 — %2:%3%4")
                .arg(QString::fromStdString(entry.server_type))
                .arg(QString::fromStdString(entry.host))
                .arg(entry.port)
                .arg(QString::fromStdString(entry.mount));
        }
        list_targets_->addItem(label);
    }
}

// ── Studio Config YAML persistence ──────────────────────────────────────

std::string LiveVideoStudioDialog::studioConfigPath()
{
    QString dir = QCoreApplication::applicationDirPath();
    if (dir.isEmpty()) dir = QStringLiteral(".");
    return (dir + QStringLiteral("/live_video_studio.yaml")).toStdString();
}

void LiveVideoStudioDialog::saveStudioConfig()
{
    QString path = QString::fromStdString(studioConfigPath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        fprintf(stderr, "[Studio] Failed to save config: %s\n",
                path.toUtf8().constData());
        return;
    }

    QTextStream ts(&file);
    ts << "# Mcaster1 Live Video Stream Studio config (auto-saved)\n\n";

    /* Video settings */
    ts << "video:\n";
    ts << "  codec: " << combo_codec_->currentData().toInt() << "\n";
    ts << "  container: " << combo_container_->currentData().toInt() << "\n";
    ts << "  resolution: \"" << combo_resolution_->currentData().toString() << "\"\n";
    ts << "  fps: " << combo_fps_->currentText() << "\n";
    ts << "  bitrate_kbps: " << spin_bitrate_->value() << "\n";

    /* Audio settings */
    ts << "\naudio:\n";
    ts << "  device_index: " << combo_audio_device_->currentData().toInt() << "\n";
    ts << "  codec: " << combo_audio_codec_->currentData().toInt() << "\n";
    ts << "  bitrate_kbps: " << spin_audio_bitrate_->value() << "\n";
    ts << "  sample_rate: " << combo_audio_rate_->currentText() << "\n";
    ts << "  channels: " << combo_audio_ch_->currentData().toInt() << "\n";

    /* Transition settings */
    ts << "\ntransition:\n";
    ts << "  type: " << combo_transition_type_->currentData().toInt() << "\n";
    ts << "  duration_ms: " << slider_transition_dur_->value() << "\n";

    /* Stream targets (manually added ones) */
    ts << "\nstream_targets:\n";
    for (int i = 0; i < targets_.size(); ++i) {
        const auto &t = targets_[i];
        ts << "  - server_type: \"" << QString::fromStdString(t.server_type) << "\"\n";
        ts << "    host: \"" << QString::fromStdString(t.host) << "\"\n";
        ts << "    port: " << t.port << "\n";
        ts << "    mount: \"" << QString::fromStdString(t.mount) << "\"\n";
        ts << "    password: \"" << QString::fromStdString(t.password) << "\"\n";
        ts << "    stream_key: \"" << QString::fromStdString(t.stream_key) << "\"\n";
        ts << "    output_dir: \"" << QString::fromStdString(t.output_dir) << "\"\n";
        ts << "    hls_segment_duration: " << t.hls_segment_duration << "\n";
        ts << "    hls_max_segments: " << t.hls_max_segments << "\n";
    }

    file.close();
    fprintf(stderr, "[Studio] Config saved to %s (%d targets)\n",
            path.toUtf8().constData(), static_cast<int>(targets_.size()));
}

/* ── YAML reading helpers (local copies — same as profile_manager) ─── */

namespace {

struct YamlDoc {
    yaml_document_t doc;
    bool valid = false;
    ~YamlDoc() { if (valid) yaml_document_delete(&doc); }
};

const char* node_scalar(yaml_document_t*, yaml_node_t* node) {
    if (!node || node->type != YAML_SCALAR_NODE) return "";
    return reinterpret_cast<const char*>(node->data.scalar.value);
}

yaml_node_t* map_get(yaml_document_t* doc, yaml_node_t* map, const char* key) {
    if (!map || map->type != YAML_MAPPING_NODE) return nullptr;
    for (auto* p = map->data.mapping.pairs.start; p < map->data.mapping.pairs.top; ++p) {
        yaml_node_t* k = yaml_document_get_node(doc, p->key);
        if (k && k->type == YAML_SCALAR_NODE &&
            strcmp(reinterpret_cast<const char*>(k->data.scalar.value), key) == 0)
            return yaml_document_get_node(doc, p->value);
    }
    return nullptr;
}

std::string ystr(yaml_document_t* doc, yaml_node_t* map, const char* key) {
    yaml_node_t* n = map_get(doc, map, key);
    return n ? std::string(node_scalar(doc, n)) : std::string();
}

int yint(yaml_document_t* doc, yaml_node_t* map, const char* key, int def = 0) {
    yaml_node_t* n = map_get(doc, map, key);
    if (!n) return def;
    const char* s = node_scalar(doc, n);
    if (!s || !*s) return def;
    char* end = nullptr;
    long v = strtol(s, &end, 10);
    return (end != s) ? static_cast<int>(v) : def;
}

} // anonymous namespace

void LiveVideoStudioDialog::loadStudioConfig()
{
    std::string path = studioConfigPath();
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly))
        return; /* No saved config yet — use defaults */

    QByteArray data = file.readAll();
    file.close();

    yaml_parser_t parser;
    yaml_parser_initialize(&parser);
    yaml_parser_set_input_string(&parser,
        reinterpret_cast<const unsigned char*>(data.constData()),
        static_cast<size_t>(data.size()));

    YamlDoc yd;
    yd.valid = yaml_parser_load(&parser, &yd.doc);
    yaml_parser_delete(&parser);
    if (!yd.valid) return;

    yaml_node_t* root = yaml_document_get_node(&yd.doc, 1);
    if (!root || root->type != YAML_MAPPING_NODE) return;

    /* Video settings */
    yaml_node_t* vid = map_get(&yd.doc, root, "video");
    if (vid) {
        int codec_val = yint(&yd.doc, vid, "codec", -1);
        if (codec_val >= 0) {
            int idx = combo_codec_->findData(codec_val);
            if (idx >= 0) combo_codec_->setCurrentIndex(idx);
        }
        int container_val = yint(&yd.doc, vid, "container", -1);
        if (container_val >= 0) {
            int idx = combo_container_->findData(container_val);
            if (idx >= 0) combo_container_->setCurrentIndex(idx);
        }
        std::string res = ystr(&yd.doc, vid, "resolution");
        if (!res.empty()) {
            int idx = combo_resolution_->findData(QString::fromStdString(res));
            if (idx >= 0) combo_resolution_->setCurrentIndex(idx);
        }
        int fps = yint(&yd.doc, vid, "fps", 0);
        if (fps > 0) {
            int idx = combo_fps_->findText(QString::number(fps));
            if (idx >= 0) combo_fps_->setCurrentIndex(idx);
        }
        int vbr = yint(&yd.doc, vid, "bitrate_kbps", 0);
        if (vbr > 0) spin_bitrate_->setValue(vbr);
    }

    /* Audio settings */
    yaml_node_t* aud = map_get(&yd.doc, root, "audio");
    if (aud) {
        int dev = yint(&yd.doc, aud, "device_index", -1);
        int idx = combo_audio_device_->findData(dev);
        if (idx >= 0) combo_audio_device_->setCurrentIndex(idx);

        int acodec = yint(&yd.doc, aud, "codec", -1);
        if (acodec >= 0) {
            int ai = combo_audio_codec_->findData(acodec);
            if (ai >= 0) combo_audio_codec_->setCurrentIndex(ai);
        }
        int abr = yint(&yd.doc, aud, "bitrate_kbps", 0);
        if (abr > 0) spin_audio_bitrate_->setValue(abr);

        int sr = yint(&yd.doc, aud, "sample_rate", 0);
        if (sr > 0) {
            int si = combo_audio_rate_->findText(QString::number(sr));
            if (si >= 0) combo_audio_rate_->setCurrentIndex(si);
        }
        int ch = yint(&yd.doc, aud, "channels", 0);
        if (ch > 0) {
            int ci = combo_audio_ch_->findData(ch);
            if (ci >= 0) combo_audio_ch_->setCurrentIndex(ci);
        }
    }

    /* Transition settings */
    yaml_node_t* trans = map_get(&yd.doc, root, "transition");
    if (trans) {
        int ttype = yint(&yd.doc, trans, "type", 1);
        int ti = combo_transition_type_->findData(ttype);
        if (ti >= 0) combo_transition_type_->setCurrentIndex(ti);

        int dur = yint(&yd.doc, trans, "duration_ms", 500);
        slider_transition_dur_->setValue(dur);
    }

    /* Stream targets */
    yaml_node_t* tgts = map_get(&yd.doc, root, "stream_targets");
    if (tgts && tgts->type == YAML_SEQUENCE_NODE) {
        for (auto *it = tgts->data.sequence.items.start;
             it < tgts->data.sequence.items.top; ++it) {
            yaml_node_t* item = yaml_document_get_node(&yd.doc, *it);
            if (!item || item->type != YAML_MAPPING_NODE) continue;

            StreamTargetEntry entry;
            entry.server_type         = ystr(&yd.doc, item, "server_type");
            entry.host                = ystr(&yd.doc, item, "host");
            entry.port                = yint(&yd.doc, item, "port", 8000);
            entry.mount               = ystr(&yd.doc, item, "mount");
            entry.password            = ystr(&yd.doc, item, "password");
            entry.stream_key          = ystr(&yd.doc, item, "stream_key");
            entry.output_dir          = ystr(&yd.doc, item, "output_dir");
            entry.hls_segment_duration = yint(&yd.doc, item, "hls_segment_duration", 6);
            entry.hls_max_segments    = yint(&yd.doc, item, "hls_max_segments", 5);

            if (entry.host.empty() && entry.output_dir.empty()) continue;

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
    }

    fprintf(stderr, "[Studio] Config loaded from %s (%d targets)\n",
            path.c_str(), static_cast<int>(targets_.size()));
}

// ── Build UI ────────────────────────────────────────────────────────────

void LiveVideoStudioDialog::buildUI()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 4, 8, 4);
    root->setSpacing(4);

    /* ── Title bar ─────────────────────────────────────────── */
    auto *title = new QLabel(QStringLiteral("LIVE VIDEO STREAM STUDIO"));
    QFont tf = title->font();
    tf.setPointSize(14);
    tf.setBold(true);
    title->setFont(tf);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral("color: #00d4aa; padding: 2px;"));
    root->addWidget(title);

    /* ── Main split: monitors (left) | settings (right) ──── */
    auto *splitter = new QSplitter(Qt::Horizontal);

    /* ════════ LEFT PANEL: Monitors ════════ */
    auto *left_panel = new QWidget;
    auto *left_lay = new QVBoxLayout(left_panel);
    left_lay->setContentsMargins(0, 0, 0, 0);
    left_lay->setSpacing(4);

    /* ── Source preview panes (3 across) ── */
    auto *source_row = new QHBoxLayout;
    source_row->setSpacing(6);

    auto devices = CameraSource::enumerate_devices();

    for (int i = 0; i < kSourcePaneCount; ++i) {
        auto *pane_box = new QVBoxLayout;
        pane_box->setSpacing(2);

        /* Device selector combo */
        source_panes_[i].combo = new QComboBox;
        source_panes_[i].combo->addItem(QStringLiteral("(None)"), -1);
        for (auto &dev : devices) {
            source_panes_[i].combo->addItem(
                QString::fromStdString(dev.name), dev.index);
        }
        source_panes_[i].combo->setMaximumWidth(200);
        pane_box->addWidget(source_panes_[i].combo);

        /* Preview widget */
        source_panes_[i].preview = new CameraPreviewWidget;
        source_panes_[i].preview->setMinimumSize(200, 150);
        source_panes_[i].preview->setMaximumHeight(200);
        source_panes_[i].preview->setStyleSheet(
            QStringLiteral("border: 2px solid #333355;"));
        pane_box->addWidget(source_panes_[i].preview);

        /* Label under preview */
        source_panes_[i].label = new QLabel(
            QString("Source %1").arg(i + 1));
        source_panes_[i].label->setAlignment(Qt::AlignCenter);
        source_panes_[i].label->setStyleSheet(
            QStringLiteral("color: #667788; font-size: 10px;"));
        pane_box->addWidget(source_panes_[i].label);

        /* Click to select as preview; accept drag-and-drop files */
        source_panes_[i].preview->installEventFilter(this);
        source_panes_[i].preview->setAcceptDrops(true);

        /* Connect device change (AFTER items added to avoid signal during addItem) */
        int pane_idx = i;
        connect(source_panes_[i].combo,
                QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, pane_idx](int combo_idx) {
                    onSourceDeviceChanged(pane_idx, combo_idx);
                });

        source_row->addLayout(pane_box);
    }

    left_lay->addLayout(source_row);

    /* ── Program Monitor (large) ── */
    lbl_program_ = new QLabel(QStringLiteral("PROGRAM OUTPUT"));
    lbl_program_->setAlignment(Qt::AlignCenter);
    lbl_program_->setStyleSheet(QStringLiteral(
        "color: #ff3344; font-weight: bold; font-size: 11px; padding: 2px;"));
    left_lay->addWidget(lbl_program_);

    program_monitor_ = new CameraPreviewWidget;
    program_monitor_->setMinimumSize(480, 320);
    program_monitor_->setStyleSheet(
        QStringLiteral("border: 2px solid #ff3344;"));
    left_lay->addWidget(program_monitor_, 1);

    /* ── Transition control row ── */
    auto *trans_ctrl = new QHBoxLayout;
    trans_ctrl->setSpacing(6);

    btn_cut_ = new QPushButton(QStringLiteral("CUT"));
    btn_cut_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #dd2244; color: white; padding: 4px 12px; "
        "border-radius: 3px; font-weight: bold; font-size: 11px; }"
        "QPushButton:hover { background: #ee3355; }"));
    connect(btn_cut_, &QPushButton::clicked,
            this, &LiveVideoStudioDialog::onTransitionCut);
    trans_ctrl->addWidget(btn_cut_);

    combo_transition_type_ = new QComboBox;
    combo_transition_type_->addItem(QStringLiteral("Crossfade"),      1);
    combo_transition_type_->addItem(QStringLiteral("Fade to Black"),  2);
    combo_transition_type_->addItem(QStringLiteral("Dip to White"),   3);
    combo_transition_type_->addItem(QStringLiteral("Wipe Left"),      4);
    combo_transition_type_->addItem(QStringLiteral("Wipe Right"),     5);
    combo_transition_type_->addItem(QStringLiteral("Wipe Up"),        6);
    combo_transition_type_->addItem(QStringLiteral("Wipe Down"),      7);
    combo_transition_type_->addItem(QStringLiteral("Push Left"),      8);
    combo_transition_type_->addItem(QStringLiteral("Push Right"),     9);
    combo_transition_type_->addItem(QStringLiteral("Iris Circle"),   10);
    combo_transition_type_->addItem(QStringLiteral("Dissolve"),      11);
    combo_transition_type_->setCurrentIndex(0);
    trans_ctrl->addWidget(combo_transition_type_);

    slider_transition_dur_ = new QSlider(Qt::Horizontal);
    slider_transition_dur_->setRange(200, 5000);
    slider_transition_dur_->setValue(1000);
    slider_transition_dur_->setSingleStep(100);
    slider_transition_dur_->setMaximumWidth(120);
    connect(slider_transition_dur_, &QSlider::valueChanged,
            this, &LiveVideoStudioDialog::onTransitionDurationChanged);
    trans_ctrl->addWidget(slider_transition_dur_);

    lbl_transition_dur_ = new QLabel(QStringLiteral("1.0s"));
    lbl_transition_dur_->setMinimumWidth(36);
    trans_ctrl->addWidget(lbl_transition_dur_);

    btn_transition_ = new QPushButton(QStringLiteral("AUTO"));
    btn_transition_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #00d4aa; color: #0a0a1e; padding: 4px 12px; "
        "border-radius: 3px; font-weight: bold; font-size: 11px; }"
        "QPushButton:hover { background: #00e8bb; }"));
    connect(btn_transition_, &QPushButton::clicked,
            this, &LiveVideoStudioDialog::onTransitionAuto);
    trans_ctrl->addWidget(btn_transition_);

    /* Separator */
    auto *sep = new QLabel(QStringLiteral("|"));
    sep->setStyleSheet(QStringLiteral("color: #333355; padding: 0 4px;"));
    trans_ctrl->addWidget(sep);

    /* Virtual Camera toggle */
    btn_vcam_toggle_ = new QPushButton(QStringLiteral("VCAM OFF"));
    btn_vcam_toggle_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #333355; color: #99aabb; padding: 4px 10px; "
        "border-radius: 3px; font-size: 10px; }"
        "QPushButton:hover { background: #444466; }"));
    connect(btn_vcam_toggle_, &QPushButton::clicked,
            this, &LiveVideoStudioDialog::onVCamToggle);
    trans_ctrl->addWidget(btn_vcam_toggle_);

    /* Manage Devices button */
    btn_manage_devices_ = new QPushButton(QStringLiteral("Manage Devices"));
    btn_manage_devices_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #333355; color: #99aabb; padding: 4px 10px; "
        "border-radius: 3px; font-size: 10px; }"
        "QPushButton:hover { background: #444466; }"));
    connect(btn_manage_devices_, &QPushButton::clicked,
            this, &LiveVideoStudioDialog::onManageDevices);
    trans_ctrl->addWidget(btn_manage_devices_);

    /* Stream Monitor button */
    btn_stream_monitor_ = new QPushButton(QStringLiteral("Stream Monitor"));
    btn_stream_monitor_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #1a3a5c; color: #ccddee; padding: 4px 10px; "
        "border-radius: 3px; font-size: 10px; border: 1px solid #2a5a8c; }"
        "QPushButton:hover { background: #2a5a8c; color: white; }"));
    btn_stream_monitor_->setToolTip(
        QStringLiteral("Open AIR/CUE Video Stream Monitor"));
    connect(btn_stream_monitor_, &QPushButton::clicked,
            this, &LiveVideoStudioDialog::onStreamMonitor);
    trans_ctrl->addWidget(btn_stream_monitor_);

    trans_ctrl->addStretch();
    left_lay->addLayout(trans_ctrl);

    splitter->addWidget(left_panel);

    /* ════════ RIGHT PANEL: Settings ════════ */
    auto *right_scroll = new QScrollArea;
    right_scroll->setWidgetResizable(true);
    right_scroll->setFrameShape(QFrame::NoFrame);
    auto *right_panel = new QWidget;
    auto *right_lay = new QVBoxLayout(right_panel);
    right_lay->setContentsMargins(4, 0, 4, 0);
    right_lay->setSpacing(4);

    /* ── Video Settings group ── */
    auto *vid_group = new QGroupBox(QStringLiteral("Video"));
    auto *vid_form  = new QFormLayout(vid_group);
    vid_form->setSpacing(4);

    combo_codec_ = new QComboBox;
    combo_codec_->addItem(QStringLiteral("H.264"),  static_cast<int>(VideoConfig::VideoCodec::H264));
    combo_codec_->addItem(QStringLiteral("Theora"), static_cast<int>(VideoConfig::VideoCodec::THEORA));
    combo_codec_->addItem(QStringLiteral("VP8"),    static_cast<int>(VideoConfig::VideoCodec::VP8));
    combo_codec_->addItem(QStringLiteral("VP9"),    static_cast<int>(VideoConfig::VideoCodec::VP9));
    connect(combo_codec_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LiveVideoStudioDialog::onCodecChanged);
    vid_form->addRow(QStringLiteral("Codec:"), combo_codec_);

    combo_container_ = new QComboBox;
    connect(combo_container_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LiveVideoStudioDialog::onContainerChanged);
    vid_form->addRow(QStringLiteral("Container:"), combo_container_);
    updateContainerForCodec();

    combo_resolution_ = new QComboBox;
    combo_resolution_->addItem(QStringLiteral("480p  (854x480)"),   QStringLiteral("854x480"));
    combo_resolution_->addItem(QStringLiteral("720p  (1280x720)"),  QStringLiteral("1280x720"));
    combo_resolution_->addItem(QStringLiteral("1080p (1920x1080)"), QStringLiteral("1920x1080"));
    combo_resolution_->setCurrentIndex(1);
    vid_form->addRow(QStringLiteral("Resolution:"), combo_resolution_);

    combo_fps_ = new QComboBox;
    combo_fps_->addItems({QStringLiteral("15"), QStringLiteral("24"),
                          QStringLiteral("25"), QStringLiteral("30"),
                          QStringLiteral("60")});
    combo_fps_->setCurrentIndex(3);
    vid_form->addRow(QStringLiteral("FPS:"), combo_fps_);

    spin_bitrate_ = new QSpinBox;
    spin_bitrate_->setRange(500, 20000);
    spin_bitrate_->setValue(cfg_.video.bitrate_kbps);
    spin_bitrate_->setSuffix(QStringLiteral(" kbps"));
    spin_bitrate_->setSingleStep(500);
    vid_form->addRow(QStringLiteral("Video Bitrate:"), spin_bitrate_);

    right_lay->addWidget(vid_group);

    /* ── Audio Settings group (NEW) ── */
    auto *aud_group = new QGroupBox(QStringLiteral("Audio"));
    auto *aud_form  = new QFormLayout(aud_group);
    aud_form->setSpacing(4);

    combo_audio_device_ = new QComboBox;
    combo_audio_device_->addItem(QStringLiteral("(None — no audio)"), -1);
    {
        auto audio_devs = PortAudioSource::enumerate_devices();
        for (auto &d : audio_devs) {
            QString name = QString::fromStdString(d.name);
            if (d.is_default_input) name += QStringLiteral(" (default)");
            combo_audio_device_->addItem(name, d.index);
        }
    }
    aud_form->addRow(QStringLiteral("Device:"), combo_audio_device_);

    combo_audio_codec_ = new QComboBox;
    aud_form->addRow(QStringLiteral("Codec:"), combo_audio_codec_);
    connect(combo_audio_codec_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updateAudioBitrateRange(); });

    spin_audio_bitrate_ = new QSpinBox;
    spin_audio_bitrate_->setSuffix(QStringLiteral(" kbps"));
    spin_audio_bitrate_->setSingleStep(16);
    aud_form->addRow(QStringLiteral("Bitrate:"), spin_audio_bitrate_);

    combo_audio_rate_ = new QComboBox;
    combo_audio_rate_->addItems({QStringLiteral("22050"), QStringLiteral("44100"),
                                 QStringLiteral("48000")});
    combo_audio_rate_->setCurrentIndex(1);  /* 44100 */
    aud_form->addRow(QStringLiteral("Sample Rate:"), combo_audio_rate_);

    combo_audio_ch_ = new QComboBox;
    combo_audio_ch_->addItem(QStringLiteral("Mono"),   1);
    combo_audio_ch_->addItem(QStringLiteral("Stereo"), 2);
    combo_audio_ch_->setCurrentIndex(1);  /* Stereo */
    aud_form->addRow(QStringLiteral("Channels:"), combo_audio_ch_);

    /* Populate audio codecs based on initial container selection */
    updateAudioCodecsForContainer();

    right_lay->addWidget(aud_group);

    /* ── Stream Targets group ── */
    auto *tgt_group = new QGroupBox(QStringLiteral("Stream Targets"));
    auto *tgt_lay   = new QVBoxLayout(tgt_group);

    list_targets_ = new QListWidget;
    list_targets_->setMaximumHeight(90);
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

    /* ── Extra Sources (images, video files) ── */
    auto *src_group = new QGroupBox(QStringLiteral("Media Sources"));
    auto *src_lay   = new QVBoxLayout(src_group);

    list_sources_ = new QListWidget;
    list_sources_->setMaximumHeight(70);
    connect(list_sources_, &QListWidget::itemDoubleClicked,
            this, &LiveVideoStudioDialog::onSourceDoubleClicked);
    src_lay->addWidget(list_sources_);

    auto *src_btn_row = new QHBoxLayout;
    btn_add_source_ = new QPushButton(QStringLiteral("+ Add"));
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
    right_lay->addStretch();

    right_scroll->setWidget(right_panel);
    right_scroll->setMinimumWidth(260);
    right_scroll->setMaximumWidth(320);
    splitter->addWidget(right_scroll);

    splitter->setStretchFactor(0, 4); /* monitors get more space */
    splitter->setStretchFactor(1, 1);

    root->addWidget(splitter, 1);

    /* ── Bottom control bar ──────────────────────────────────── */
    auto *bot = new QHBoxLayout;

    lbl_status_ = new QLabel(QStringLiteral("IDLE"));
    lbl_status_->setStyleSheet(QStringLiteral(
        "font-weight: bold; font-size: 12px; color: #667788; padding: 4px 8px;"));
    bot->addWidget(lbl_status_);

    /* ON-AIR flashing indicator — hidden until confirmed live */
    lbl_on_air_ = new QLabel(QStringLiteral("  \xF0\x9F\x94\xB4  ON-AIR  "));
    lbl_on_air_->setStyleSheet(QStringLiteral(
        "background: #cc0000; color: white; font-weight: bold; font-size: 14px; "
        "padding: 4px 14px; border-radius: 6px; letter-spacing: 2px;"));
    lbl_on_air_->setVisible(false);
    bot->addWidget(lbl_on_air_);

    on_air_timer_ = new QTimer(this);
    on_air_timer_->setInterval(600);
    connect(on_air_timer_, &QTimer::timeout, this, [this]() {
        on_air_visible_ = !on_air_visible_;
        lbl_on_air_->setVisible(on_air_visible_);
    });

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

    QString res = QString("%1x%2").arg(cfg_.video.width).arg(cfg_.video.height);
    int res_idx = combo_resolution_->findData(res);
    if (res_idx >= 0) combo_resolution_->setCurrentIndex(res_idx);

    int fps_idx = combo_fps_->findText(QString::number(cfg_.video.fps));
    if (fps_idx >= 0) combo_fps_->setCurrentIndex(fps_idx);

    /* Load saved studio config (overrides defaults above) */
    loadStudioConfig();

    /* ── Auto-save on any config change ─────────────────────── */
    auto autoSave = [this]() { saveStudioConfig(); };

    connect(combo_codec_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, autoSave);
    connect(combo_container_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, autoSave);
    connect(combo_resolution_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, autoSave);
    connect(combo_fps_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, autoSave);
    connect(spin_bitrate_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, autoSave);
    connect(combo_audio_device_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, autoSave);
    connect(combo_audio_codec_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, autoSave);
    connect(spin_audio_bitrate_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, autoSave);
    connect(combo_audio_rate_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, autoSave);
    connect(combo_audio_ch_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, autoSave);
    connect(combo_transition_type_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, autoSave);
    connect(slider_transition_dur_, &QSlider::valueChanged, this, autoSave);
}

// ── Program Source accessor ──────────────────────────────────────────────

VideoSource *LiveVideoStudioDialog::programSource() const
{
    if (program_pane_idx_ < 0 || program_pane_idx_ >= kSourcePaneCount)
        return nullptr;
    const auto &pane = source_panes_[program_pane_idx_];
    switch (pane.source_type) {
    case PaneSourceType::CAMERA:     return pane.camera.get();
    case PaneSourceType::IMAGE:      return pane.image_src.get();
    case PaneSourceType::VIDEO_FILE: return pane.video_src.get();
    default:                         return nullptr;
    }
}

// ── Video Config extraction ─────────────────────────────────────────────

VideoConfig LiveVideoStudioDialog::videoConfig() const
{
    VideoConfig vc = cfg_.video;

    vc.video_codec = static_cast<VideoConfig::VideoCodec>(
        combo_codec_->currentData().toInt());
    vc.video_container = static_cast<VideoConfig::VideoContainer>(
        combo_container_->currentData().toInt());

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

// ── Codec / Container / Audio constraint cascade ────────────────────────

void LiveVideoStudioDialog::onCodecChanged(int /*index*/)
{
    updateContainerForCodec();
}

void LiveVideoStudioDialog::onContainerChanged(int /*index*/)
{
    updateAudioCodecsForContainer();
}

void LiveVideoStudioDialog::updateContainerForCodec()
{
    combo_container_->blockSignals(true);
    combo_container_->clear();

    auto codec = static_cast<VideoConfig::VideoCodec>(
        combo_codec_->currentData().toInt());

    switch (codec) {
    case VideoConfig::VideoCodec::H264:
        combo_container_->addItem(QStringLiteral("FLV"),
            static_cast<int>(VideoConfig::VideoContainer::FLV));
        combo_container_->addItem(QStringLiteral("MKV"),
            static_cast<int>(VideoConfig::VideoContainer::MKV));
        break;
    case VideoConfig::VideoCodec::THEORA:
        combo_container_->addItem(QStringLiteral("OGG"),
            static_cast<int>(VideoConfig::VideoContainer::OGG));
        break;
    case VideoConfig::VideoCodec::VP8:
    case VideoConfig::VideoCodec::VP9:
        combo_container_->addItem(QStringLiteral("WebM"),
            static_cast<int>(VideoConfig::VideoContainer::WEBM));
        combo_container_->addItem(QStringLiteral("MKV"),
            static_cast<int>(VideoConfig::VideoContainer::MKV));
        break;
    }

    combo_container_->blockSignals(false);

    /* Cascade: container change → update audio codecs */
    updateAudioCodecsForContainer();
}

void LiveVideoStudioDialog::updateAudioCodecsForContainer()
{
    /* Guard: combo_audio_codec_ may not exist yet during initial buildUI */
    if (!combo_audio_codec_) return;
    if (!combo_container_ || combo_container_->count() == 0) return;

    combo_audio_codec_->blockSignals(true);
    combo_audio_codec_->clear();

    auto container = static_cast<VideoConfig::VideoContainer>(
        combo_container_->currentData().toInt());

    auto options = audioCodecsForContainer(container);
    for (auto &opt : options) {
        combo_audio_codec_->addItem(
            QString::fromUtf8(opt.label),
            static_cast<int>(opt.codec));
    }

    combo_audio_codec_->blockSignals(false);

    /* Cascade: audio codec change → update bitrate range */
    updateAudioBitrateRange();
}

void LiveVideoStudioDialog::updateAudioBitrateRange()
{
    if (!combo_audio_codec_ || !spin_audio_bitrate_) return;
    if (combo_audio_codec_->count() == 0) return;

    auto codec = static_cast<EncoderConfig::Codec>(
        combo_audio_codec_->currentData().toInt());

    auto [lo, hi] = audioBitrateRange(codec);
    if (lo == 0 && hi == 0) {
        /* FLAC — lossless, no bitrate setting */
        spin_audio_bitrate_->setEnabled(false);
        spin_audio_bitrate_->setRange(0, 0);
        spin_audio_bitrate_->setValue(0);
        spin_audio_bitrate_->setSuffix(QStringLiteral(" (lossless)"));
    } else {
        spin_audio_bitrate_->setEnabled(true);
        spin_audio_bitrate_->setRange(lo, hi);
        spin_audio_bitrate_->setSuffix(QStringLiteral(" kbps"));
        /* Default to a sensible value */
        int cur = spin_audio_bitrate_->value();
        if (cur < lo || cur > hi) {
            int def = (codec == EncoderConfig::Codec::AAC_HE) ? 64 : 128;
            spin_audio_bitrate_->setValue(std::clamp(def, lo, hi));
        }
    }
}

// ── Source pane device management ───────────────────────────────────────

void LiveVideoStudioDialog::onSourceDeviceChanged(int pane_index, int combo_index)
{
    if (pane_index < 0 || pane_index >= kSourcePaneCount) return;

    int device_data = source_panes_[pane_index].combo->itemData(combo_index).toInt();

    /* Stop existing capture */
    stopSourceCapture(pane_index);

    if (device_data == -1) {
        /* (None) selected */
        source_panes_[pane_index].label->setText(
            QString("Source %1").arg(pane_index + 1));
        return;
    }

    /* TODO: Negative values < -1 are screen captures (encoded as -(100+display_id)) */
    if (device_data < -1) {
        source_panes_[pane_index].label->setText(
            QString("Source %1: Screen").arg(pane_index + 1));
        /* Screen capture not wired yet */
        return;
    }

    /* Start camera capture for this pane */
    startSourceCapture(pane_index, device_data);
    source_panes_[pane_index].label->setText(
        QString("Source %1: %2").arg(pane_index + 1)
            .arg(source_panes_[pane_index].combo->currentText()));
}

void LiveVideoStudioDialog::startSourceCapture(int pane_index, int device_idx)
{
    if (pane_index < 0 || pane_index >= kSourcePaneCount) return;

    auto &pane = source_panes_[pane_index];
    pane.device_index = device_idx;

    /* Create camera source */
    pane.camera = std::make_unique<CameraSource>(device_idx, 640, 480, 30);

    CameraPreviewWidget *preview_widget = pane.preview;
    CameraPreviewWidget *program_widget = program_monitor_;

    bool ok = pane.camera->start([this, pane_index, preview_widget,
                                  program_widget](const VideoFrame &frame) {
        /* Always feed to the source preview pane */
        preview_widget->pushFrame(frame.data, frame.width, frame.height, frame.stride);

        /* During a transition, feed A/B frames to transition engine for blending */
        if (transition_engine_ && transition_engine_->is_transitioning()) {
            if (pane_index == program_pane_idx_) {
                transition_engine_->push_program_frame(
                    frame.data, frame.width, frame.height, frame.stride);
            } else if (pane_index == preview_pane_idx_) {
                transition_engine_->push_preview_frame(
                    frame.data, frame.width, frame.height, frame.stride);
            }
        } else {
            /* Normal: feed program source directly to program monitor */
            if (pane_index == program_pane_idx_) {
                program_widget->pushFrame(
                    frame.data, frame.width, frame.height, frame.stride);

                /* Forward program frames to live video pipeline for encoding/streaming */
                if (video_pipeline_ && video_pipeline_->is_running()) {
                    video_pipeline_->push_video_frame(frame);
                }

                /* Also feed to virtual camera shared memory if active */
                if (vcam_active_) {
                    vcam_writer_.pushFrame(
                        frame.data, frame.width, frame.height, frame.stride);
                }

                /* Feed to Video Stream Monitor CUE preview */
                if (stream_monitor_ && stream_monitor_->isVisible()) {
                    stream_monitor_->pushCueFrame(
                        frame.data, frame.width, frame.height, frame.stride);
                }
            }
        }
    });

    if (ok) {
        pane.source_type = PaneSourceType::CAMERA;
        pane.source_path.clear();

        /* Auto-assign first source as program */
        if (program_pane_idx_ < 0) {
            selectPreviewPane(-1); /* clear preview */
            program_pane_idx_ = pane_index;
            pane.preview->setStyleSheet(
                QStringLiteral("border: 2px solid #ff3344;")); /* red = PROGRAM */
            pane.label->setStyleSheet(
                QStringLiteral("color: #ff3344; font-size: 10px; font-weight: bold;"));
        }
    } else {
        pane.camera.reset();
        pane.device_index = -1;
        pane.source_type = PaneSourceType::NONE;
    }
}

void LiveVideoStudioDialog::stopSourceCapture(int pane_index)
{
    if (pane_index < 0 || pane_index >= kSourcePaneCount) return;

    auto &pane = source_panes_[pane_index];

    /* Stop camera source */
    if (pane.camera) {
        if (pane.camera->is_running())
            pane.camera->stop();
        pane.camera.reset();
    }

    /* Stop image source */
    if (pane.image_src) {
        if (pane.image_src->is_running())
            pane.image_src->stop();
        pane.image_src.reset();
    }

    /* Stop video file source */
    if (pane.video_src) {
        if (pane.video_src->is_running())
            pane.video_src->stop();
        pane.video_src.reset();
    }

    pane.device_index = -1;
    pane.source_type = PaneSourceType::NONE;
    pane.source_path.clear();

    /* Clear program/preview assignment if this was it */
    if (program_pane_idx_ == pane_index) program_pane_idx_ = -1;
    if (preview_pane_idx_ == pane_index) preview_pane_idx_ = -1;

    /* Reset border style */
    pane.preview->setStyleSheet(
        QStringLiteral("border: 2px solid #333355;"));
    pane.label->setStyleSheet(
        QStringLiteral("color: #667788; font-size: 10px;"));
}

void LiveVideoStudioDialog::selectPreviewPane(int pane_index)
{
    /* Clear previous preview selection */
    if (preview_pane_idx_ >= 0 && preview_pane_idx_ < kSourcePaneCount &&
        preview_pane_idx_ != program_pane_idx_) {
        source_panes_[preview_pane_idx_].preview->setStyleSheet(
            QStringLiteral("border: 2px solid #333355;"));
        source_panes_[preview_pane_idx_].label->setStyleSheet(
            QStringLiteral("color: #667788; font-size: 10px;"));
    }

    preview_pane_idx_ = pane_index;

    /* Highlight new preview */
    if (pane_index >= 0 && pane_index < kSourcePaneCount &&
        pane_index != program_pane_idx_) {
        source_panes_[pane_index].preview->setStyleSheet(
            QStringLiteral("border: 2px solid #ff8800;")); /* orange = PREVIEW */
        source_panes_[pane_index].label->setStyleSheet(
            QStringLiteral("color: #ff8800; font-size: 10px; font-weight: bold;"));
    }
}

// ── Transition slots ────────────────────────────────────────────────────

void LiveVideoStudioDialog::onTransitionCut()
{
    /* Auto-pick preview if none selected: first active non-program source */
    if (preview_pane_idx_ < 0 || preview_pane_idx_ == program_pane_idx_) {
        for (int i = 0; i < kSourcePaneCount; ++i) {
            if (i != program_pane_idx_ &&
                source_panes_[i].source_type != PaneSourceType::NONE) {
                selectPreviewPane(i);
                break;
            }
        }
    }

    /* Still no valid preview → nothing to cut to */
    if (preview_pane_idx_ < 0 || preview_pane_idx_ == program_pane_idx_) return;
    if (source_panes_[preview_pane_idx_].source_type == PaneSourceType::NONE) return;

    /* De-highlight old program */
    if (program_pane_idx_ >= 0 && program_pane_idx_ < kSourcePaneCount) {
        source_panes_[program_pane_idx_].preview->setStyleSheet(
            QStringLiteral("border: 2px solid #333355;"));
        source_panes_[program_pane_idx_].label->setStyleSheet(
            QStringLiteral("color: #667788; font-size: 10px;"));
    }

    /* New program = old preview */
    program_pane_idx_ = preview_pane_idx_;
    preview_pane_idx_ = -1;

    /* Highlight new program */
    source_panes_[program_pane_idx_].preview->setStyleSheet(
        QStringLiteral("border: 2px solid #ff3344;"));
    source_panes_[program_pane_idx_].label->setStyleSheet(
        QStringLiteral("color: #ff3344; font-size: 10px; font-weight: bold;"));
}

void LiveVideoStudioDialog::onTransitionAuto()
{
    if (!transition_engine_ || transition_engine_->is_transitioning()) return;

    /* Auto-pick preview if none selected */
    if (preview_pane_idx_ < 0 || preview_pane_idx_ == program_pane_idx_) {
        for (int i = 0; i < kSourcePaneCount; ++i) {
            if (i != program_pane_idx_ &&
                source_panes_[i].source_type != PaneSourceType::NONE) {
                selectPreviewPane(i);
                break;
            }
        }
    }

    if (preview_pane_idx_ < 0 || preview_pane_idx_ == program_pane_idx_) return;
    if (source_panes_[preview_pane_idx_].source_type == PaneSourceType::NONE) return;

    int type_idx = combo_transition_type_->currentData().toInt();
    transition_engine_->set_type(static_cast<TransitionEngine::Type>(type_idx));

    float dur = slider_transition_dur_->value() / 1000.0f;
    transition_engine_->set_duration(dur);

    transition_engine_->begin();
    transition_tick_timer_->start();

    btn_transition_->setEnabled(false);
    btn_transition_->setText(QStringLiteral("TRANSITIONING..."));
    btn_cut_->setEnabled(false);
}

void LiveVideoStudioDialog::onTransitionTick()
{
    if (!transition_engine_) return;

    bool still_going = transition_engine_->tick(0.033);

    /* Render blended A/B frame to program monitor during transition */
    constexpr int kBlendW = 640;
    constexpr int kBlendH = 480;
    constexpr int kBlendStride = kBlendW * 4; /* BGRA */

    if (!blend_buf_.size())
        blend_buf_.resize(kBlendStride * kBlendH);

    if (transition_engine_->render(blend_buf_.data(), kBlendW, kBlendH, kBlendStride)) {
        program_monitor_->pushFrame(blend_buf_.data(), kBlendW, kBlendH, kBlendStride);

        /* Feed blended frame to virtual camera if active */
        if (vcam_active_) {
            vcam_writer_.pushFrame(blend_buf_.data(), kBlendW, kBlendH, kBlendStride);
        }

        /* Feed blended frame to Video Stream Monitor CUE preview */
        if (stream_monitor_ && stream_monitor_->isVisible()) {
            stream_monitor_->pushCueFrame(
                blend_buf_.data(), kBlendW, kBlendH, kBlendStride);
        }
    }

    if (!still_going) {
        transition_tick_timer_->stop();
        btn_transition_->setEnabled(true);
        btn_transition_->setText(QStringLiteral("AUTO"));
        btn_cut_->setEnabled(true);

        /* Complete transition: swap preview → program */
        onTransitionCut();
    }
}

void LiveVideoStudioDialog::onTransitionDurationChanged(int value)
{
    float sec = value / 1000.0f;
    lbl_transition_dur_->setText(QString("%1s").arg(sec, 0, 'f', 1));
}

// ── Stream Target management ────────────────────────────────────────────

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
        saveStudioConfig();
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
        saveStudioConfig();
    }
    editor->deleteLater();
}

void LiveVideoStudioDialog::onRemoveTarget()
{
    int row = list_targets_->currentRow();
    if (row >= 0 && row < targets_.size()) {
        targets_.removeAt(row);
        delete list_targets_->takeItem(row);
        saveStudioConfig();
    }
}

// ── Go Live / Stop / Dry Run ────────────────────────────────────────────

void LiveVideoStudioDialog::onGoLive()
{
    if (targets_.isEmpty()) {
        QMessageBox::warning(this,
            QStringLiteral("No Targets"),
            QStringLiteral("Add at least one stream target before going live."));
        return;
    }
    if (program_pane_idx_ < 0) {
        QMessageBox::warning(this,
            QStringLiteral("No Program Source"),
            QStringLiteral("Select a camera source and set it as program before going live."));
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

    /* Stop ON-AIR flashing */
    on_air_timer_->stop();
    lbl_on_air_->setVisible(false);

    btn_go_live_->setEnabled(true);
    btn_dry_run_->setEnabled(true);
    btn_stop_->setEnabled(false);
    lbl_status_->setText(QStringLiteral("IDLE"));
    lbl_status_->setStyleSheet(QStringLiteral(
        "font-weight: bold; font-size: 12px; color: #667788; padding: 4px 8px;"));
    lbl_duration_->setText(QStringLiteral("--:--:--"));

    emit stopRequested();
}

// ── confirmLive / confirmStopped — called from MainWindow ──────────────
void LiveVideoStudioDialog::confirmLive()
{
    /* Start the ON-AIR flashing indicator */
    on_air_visible_ = true;
    lbl_on_air_->setVisible(true);
    on_air_timer_->start();

    /* Update status label */
    lbl_status_->setText(QStringLiteral("STREAMING"));
    lbl_status_->setStyleSheet(QStringLiteral(
        "font-weight: bold; font-size: 12px; color: #ff3344; padding: 4px 8px;"));

    /* Flash the window title bar (system notification) */
    setWindowTitle(QStringLiteral("\xF0\x9F\x94\xB4 LIVE ON-AIR — Video Stream Studio"));
}

void LiveVideoStudioDialog::confirmStopped()
{
    video_pipeline_ = nullptr;  /* stop forwarding frames */

    on_air_timer_->stop();
    lbl_on_air_->setVisible(false);

    lbl_status_->setText(QStringLiteral("IDLE"));
    lbl_status_->setStyleSheet(QStringLiteral(
        "font-weight: bold; font-size: 12px; color: #667788; padding: 4px 8px;"));

    setWindowTitle(QStringLiteral("Live Video Stream Studio"));
}

void LiveVideoStudioDialog::setVideoPipeline(VideoStreamPipeline *pipeline)
{
    video_pipeline_ = pipeline;
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

// ── Source management (legacy list for images/video files) ──────────────

void LiveVideoStudioDialog::onAddSource()
{
    auto *menu = new QMenu(this);

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

    if (sources_[row].source && sources_[row].source->is_running())
        sources_[row].source->stop();

    sources_.erase(sources_.begin() + row);
    delete list_sources_->takeItem(row);
}

void LiveVideoStudioDialog::onSourceDoubleClicked()
{
    /* Legacy: select a source from the list */
    int row = list_sources_->currentRow();
    if (row < 0 || row >= static_cast<int>(sources_.size())) return;

    for (int i = 0; i < list_sources_->count(); ++i) {
        auto *item = list_sources_->item(i);
        item->setBackground(i == row ? QColor(255, 136, 0, 40) : Qt::transparent);
    }
}

// ── Virtual Camera ──────────────────────────────────────────────────────

void LiveVideoStudioDialog::onManageDevices()
{
    ManageDevicesDialog dlg(this);
    dlg.exec();
}

void LiveVideoStudioDialog::onVCamToggle()
{
    if (!vcam_active_) {
        /* Start virtual camera output */
        if (vcam_writer_.open(640, 480, 30, 0)) {
            vcam_active_ = true;
            btn_vcam_toggle_->setText(QStringLiteral("VCAM ON"));
            btn_vcam_toggle_->setStyleSheet(QStringLiteral(
                "QPushButton { background: #00d4aa; color: #0a0a1e; padding: 4px 10px; "
                "border-radius: 3px; font-size: 10px; font-weight: bold; }"
                "QPushButton:hover { background: #00e8bb; }"));
        }
    } else {
        /* Stop virtual camera output */
        vcam_writer_.close();
        vcam_active_ = false;
        btn_vcam_toggle_->setText(QStringLiteral("VCAM OFF"));
        btn_vcam_toggle_->setStyleSheet(QStringLiteral(
            "QPushButton { background: #333355; color: #99aabb; padding: 4px 10px; "
            "border-radius: 3px; font-size: 10px; }"
            "QPushButton:hover { background: #444466; }"));
    }
}

// ── Event filter for source pane interactions ───────────────────────────

static bool isImageFile(const QString &path)
{
    static const QStringList exts = {
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("bmp"), QStringLiteral("gif"), QStringLiteral("tiff"),
        QStringLiteral("webp"), QStringLiteral("ico")
    };
    QString ext = QFileInfo(path).suffix().toLower();
    return exts.contains(ext);
}

static bool isVideoFile(const QString &path)
{
    static const QStringList exts = {
        QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("avi"),
        QStringLiteral("mov"), QStringLiteral("wmv"), QStringLiteral("webm"),
        QStringLiteral("flv"), QStringLiteral("ts"),  QStringLiteral("m4v")
    };
    QString ext = QFileInfo(path).suffix().toLower();
    return exts.contains(ext);
}

bool LiveVideoStudioDialog::eventFilter(QObject *obj, QEvent *event)
{
    for (int i = 0; i < kSourcePaneCount; ++i) {
        if (obj != source_panes_[i].preview) continue;

        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                /* Don't select as preview if this is already program */
                if (i == program_pane_idx_) break;
                /* Don't select empty panes */
                if (source_panes_[i].source_type == PaneSourceType::NONE) break;
                selectPreviewPane(i);
                return true;
            }
            if (me->button() == Qt::RightButton) {
                showPaneContextMenu(i, me->globalPosition().toPoint());
                return true;
            }
            break;
        }
        case QEvent::DragEnter: {
            auto *de = static_cast<QDragEnterEvent *>(event);
            if (de->mimeData()->hasUrls()) {
                for (auto &url : de->mimeData()->urls()) {
                    if (url.isLocalFile()) {
                        QString path = url.toLocalFile();
                        if (isImageFile(path) || isVideoFile(path)) {
                            de->acceptProposedAction();
                            return true;
                        }
                    }
                }
            }
            break;
        }
        case QEvent::Drop: {
            auto *de = static_cast<QDropEvent *>(event);
            if (de->mimeData()->hasUrls()) {
                for (auto &url : de->mimeData()->urls()) {
                    if (url.isLocalFile()) {
                        QString path = url.toLocalFile();
                        if (isImageFile(path) || isVideoFile(path)) {
                            onDropFile(i, path);
                            de->acceptProposedAction();
                            return true;
                        }
                    }
                }
            }
            break;
        }
        default:
            break;
        }
        break; /* found our pane, stop loop */
    }
    return QDialog::eventFilter(obj, event);
}

// ── Drag-and-drop file handler ──────────────────────────────────────────

void LiveVideoStudioDialog::onDropFile(int pane_index, const QString &path)
{
    if (pane_index < 0 || pane_index >= kSourcePaneCount) return;

    /* Stop any existing source on this pane */
    stopSourceCapture(pane_index);
    auto &pane = source_panes_[pane_index];

    QString filename = QFileInfo(path).fileName();

    if (isImageFile(path)) {
        /* Load image source */
        auto img = std::make_unique<ImageSource>(path.toStdString(), 30);

        auto *preview_widget = pane.preview;
        auto *program_widget = program_monitor_;

        bool ok = img->start([this, pane_index, preview_widget,
                              program_widget](const VideoFrame &frame) {
            preview_widget->pushFrame(frame.data, frame.width, frame.height, frame.stride);

            if (transition_engine_ && transition_engine_->is_transitioning()) {
                if (pane_index == program_pane_idx_) {
                    transition_engine_->push_program_frame(
                        frame.data, frame.width, frame.height, frame.stride);
                } else if (pane_index == preview_pane_idx_) {
                    transition_engine_->push_preview_frame(
                        frame.data, frame.width, frame.height, frame.stride);
                }
            } else {
                if (pane_index == program_pane_idx_) {
                    program_widget->pushFrame(
                        frame.data, frame.width, frame.height, frame.stride);
                    if (vcam_active_) {
                        vcam_writer_.pushFrame(
                            frame.data, frame.width, frame.height, frame.stride);
                    }
                    /* Feed to Video Stream Monitor CUE preview */
                    if (stream_monitor_ && stream_monitor_->isVisible()) {
                        stream_monitor_->pushCueFrame(
                            frame.data, frame.width, frame.height, frame.stride);
                    }
                }
            }
        });

        if (ok) {
            pane.image_src = std::move(img);
            pane.source_type = PaneSourceType::IMAGE;
            pane.source_path = path;
            pane.combo->blockSignals(true);
            pane.combo->setCurrentIndex(0); /* "(None)" in camera list */
            pane.combo->blockSignals(false);
            pane.label->setText(filename);

            /* Auto-assign as program if none set */
            if (program_pane_idx_ < 0) {
                program_pane_idx_ = pane_index;
                pane.preview->setStyleSheet(
                    QStringLiteral("border: 2px solid #ff3344;"));
                pane.label->setStyleSheet(
                    QStringLiteral("color: #ff3344; font-size: 10px; font-weight: bold;"));
            } else {
                pane.preview->setStyleSheet(
                    QStringLiteral("border: 2px solid #4488ff;"));
                pane.label->setStyleSheet(
                    QStringLiteral("color: #4488ff; font-size: 10px; font-weight: bold;"));
            }
        }
    } else if (isVideoFile(path)) {
        /* Video file source — stub for now, show placeholder */
        pane.source_type = PaneSourceType::VIDEO_FILE;
        pane.source_path = path;
        pane.combo->blockSignals(true);
        pane.combo->setCurrentIndex(0);
        pane.combo->blockSignals(false);
        pane.label->setText(filename);
        pane.preview->setStyleSheet(
            QStringLiteral("border: 2px solid #ff8800;"));
        pane.label->setStyleSheet(
            QStringLiteral("color: #ff8800; font-size: 10px; font-weight: bold;"));

        QMessageBox::information(this, QStringLiteral("Video File"),
            QString("Video file loaded: %1\n\n"
                    "Video file playback will be implemented in a future update.\n"
                    "Image files (PNG, JPG, BMP) work now — drag one onto a pane.")
                .arg(filename));
    }
}

// ── Right-click context menu on source panes ────────────────────────────

void LiveVideoStudioDialog::showPaneContextMenu(int pane_index, const QPoint &globalPos)
{
    auto &pane = source_panes_[pane_index];
    QMenu menu(this);

    /* Common header */
    auto *title = menu.addAction(QString("Source %1").arg(pane_index + 1));
    title->setEnabled(false);
    menu.addSeparator();

    /* Set as Program */
    if (pane.source_type != PaneSourceType::NONE && pane_index != program_pane_idx_) {
        menu.addAction(QStringLiteral("Set as Program"), this, [this, pane_index]() {
            /* De-highlight current program */
            if (program_pane_idx_ >= 0 && program_pane_idx_ < kSourcePaneCount) {
                source_panes_[program_pane_idx_].preview->setStyleSheet(
                    QStringLiteral("border: 2px solid #333355;"));
                source_panes_[program_pane_idx_].label->setStyleSheet(
                    QStringLiteral("color: #667788; font-size: 10px;"));
            }
            program_pane_idx_ = pane_index;
            source_panes_[pane_index].preview->setStyleSheet(
                QStringLiteral("border: 2px solid #ff3344;"));
            source_panes_[pane_index].label->setStyleSheet(
                QStringLiteral("color: #ff3344; font-size: 10px; font-weight: bold;"));
            if (preview_pane_idx_ == pane_index)
                preview_pane_idx_ = -1;
        });
    }

    /* Set as Preview */
    if (pane.source_type != PaneSourceType::NONE && pane_index != preview_pane_idx_
        && pane_index != program_pane_idx_) {
        menu.addAction(QStringLiteral("Set as Preview"), this, [this, pane_index]() {
            selectPreviewPane(pane_index);
        });
    }

    menu.addSeparator();

    /* Load Image */
    menu.addAction(QStringLiteral("Load Image..."), this, [this, pane_index]() {
        QString path = QFileDialog::getOpenFileName(this,
            QStringLiteral("Load Image"),
            QStringLiteral("C:\\"),
            QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp)"));
        if (!path.isEmpty())
            onDropFile(pane_index, path);
    });

    /* Load Video File */
    menu.addAction(QStringLiteral("Load Video File..."), this, [this, pane_index]() {
        QString path = QFileDialog::getOpenFileName(this,
            QStringLiteral("Load Video"),
            QStringLiteral("C:\\"),
            QStringLiteral("Videos (*.mp4 *.mkv *.avi *.mov *.wmv *.webm *.flv)"));
        if (!path.isEmpty())
            onDropFile(pane_index, path);
    });

    menu.addSeparator();

    /* Source-type-specific items */
    switch (pane.source_type) {
    case PaneSourceType::CAMERA:
        menu.addAction(QStringLiteral("Stop Camera"), this, [this, pane_index]() {
            stopSourceCapture(pane_index);
            source_panes_[pane_index].combo->blockSignals(true);
            source_panes_[pane_index].combo->setCurrentIndex(0);
            source_panes_[pane_index].combo->blockSignals(false);
        });
        break;
    case PaneSourceType::IMAGE:
        menu.addAction(QString("Image: %1").arg(QFileInfo(pane.source_path).fileName()));
        menu.addAction(QStringLiteral("Remove Image"), this, [this, pane_index]() {
            stopSourceCapture(pane_index);
        });
        break;
    case PaneSourceType::VIDEO_FILE:
        menu.addAction(QString("Video: %1").arg(QFileInfo(pane.source_path).fileName()));
        menu.addAction(QStringLiteral("Remove Video"), this, [this, pane_index]() {
            stopSourceCapture(pane_index);
        });
        break;
    case PaneSourceType::NONE:
        break;
    }

    if (pane.source_type != PaneSourceType::NONE) {
        menu.addSeparator();
        menu.addAction(QStringLiteral("Clear Source"), this, [this, pane_index]() {
            stopSourceCapture(pane_index);
            source_panes_[pane_index].label->setText(
                QString("Source %1").arg(pane_index + 1));
        });
    }

    menu.exec(globalPos);
}

// ── Video Stream Monitor ─────────────────────────────────────────────────

void LiveVideoStudioDialog::onStreamMonitor()
{
    if (!stream_monitor_) {
        stream_monitor_ = new VideoStreamMonitor(this);
        stream_monitor_->setAttribute(Qt::WA_DeleteOnClose, false);

        connect(stream_monitor_, &VideoStreamMonitor::openStudioRequested,
                this, [this]() {
            show();
            raise();
            activateWindow();
        });
    }

    /* Populate stream list from active targets */
    std::vector<VideoStreamMonitorInfo> streams;
    for (int i = 0; i < targets_.size(); ++i) {
        const auto &t = targets_[i];
        VideoStreamMonitorInfo info;

        if (t.server_type == "HLS (Local)") {
            info.name = QString("HLS (Local) — %1")
                .arg(QString::fromStdString(t.output_dir));
            /* HLS doesn't have a listen URL for live stream monitoring */
            continue;
        }

        info.name = QString("%1 — %2:%3%4")
            .arg(QString::fromStdString(t.server_type))
            .arg(QString::fromStdString(t.host))
            .arg(t.port)
            .arg(QString::fromStdString(t.mount));

        /* Build the listener URL */
        info.listen_url = QString("http://%1:%2%3")
            .arg(QString::fromStdString(t.host))
            .arg(t.port)
            .arg(QString::fromStdString(t.mount));

        /* Determine content type from current codec/container selection */
        auto container = static_cast<VideoConfig::VideoContainer>(
            combo_container_->currentData().toInt());
        switch (container) {
        case VideoConfig::VideoContainer::WEBM:
            info.content_type = QStringLiteral("video/webm");
            break;
        case VideoConfig::VideoContainer::FLV:
            info.content_type = QStringLiteral("video/x-flv");
            break;
        case VideoConfig::VideoContainer::MKV:
            info.content_type = QStringLiteral("video/x-matroska");
            break;
        case VideoConfig::VideoContainer::OGG:
            info.content_type = QStringLiteral("video/ogg");
            break;
        }

        info.video_bitrate_kbps = spin_bitrate_->value();
        info.audio_bitrate_kbps = spin_audio_bitrate_->value();

        streams.push_back(info);
    }

    stream_monitor_->setStreamList(streams);
    stream_monitor_->show();
    stream_monitor_->raise();
    stream_monitor_->activateWindow();
}

} // namespace mc1
