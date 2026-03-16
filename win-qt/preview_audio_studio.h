/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * preview_audio_studio.h — Preview Audio Studio dialog
 *
 * Features:
 *  - Route audio to any output device (USB Audio, Bluetooth, System categories)
 *  - Global input passthrough (PortAudio input → DSP → selected output)
 *  - Encoder slot eavesdrop (tap pre-encode PCM from any live slot)
 *  - Stream Monitor: tune in to any encoder slot's live stream URL and hear
 *    what listeners hear from the server (decoded via QMediaPlayer)
 *  - IcyStreamReader: independent ICY1/ICY2.2 protocol monitor on same URL
 *    — parses all ICY headers, tracks StreamTitle updates, displays protocol
 *    chatter in a scrollable event log
 *  - Live stream metrics: listener count, now-playing title, bitrate
 *  - ICY2.2 social media / extended metadata display
 *  - Feedback loop risk detection
 *  - USB audio interface scan
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <QDialog>
#include <vector>
#include <atomic>
#include <functional>

#include "icy_stream_reader.h"

class QComboBox;
class QGroupBox;
class QLabel;
class QMediaPlayer;
class QAudioOutput;
class QNetworkAccessManager;
class QNetworkReply;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTabWidget;
class QTimer;

#ifdef HAVE_PORTAUDIO
#include <portaudio.h>
#endif

namespace mc1 {

class PreviewAudioStudio : public QDialog {
    Q_OBJECT

public:
    explicit PreviewAudioStudio(QWidget *parent = nullptr);
    ~PreviewAudioStudio() override;

    /* Info about an encoder slot for eavesdrop + stream monitor */
    struct SlotInfo {
        int     slot_id;
        QString name;
        QString listen_url;  /* http://host:port/mount */
        QString stats_url;   /* http://host:port/status-json.xsl (Icecast2) */
        int     bitrate_kbps = 128;
    };
    void setSlotList(const std::vector<SlotInfo> &slot_entries);

private slots:
    /* Passthrough control */
    void onStartStop();
    void onInputSourceChanged(int index);
    void onOutputDeviceChanged(int index);
    void onScanUsbDevices();

    /* Stream monitor */
    void onTuneIn();
    void onStreamComboChanged(int index);
    void onStatsReply(QNetworkReply *reply);
    void onPollStats();

    /* QMediaPlayer state changes */
    void onPlayerStateChanged();
    void onPlayerError();
    void onMetadataChanged();

    /* IcyStreamReader signals */
    void onIcyConnected(const mc1::IcyStreamReader::IcyHeaders &hdrs);
    void onIcyMetaUpdated(const QString &title, const QString &stream_url,
                          const QString &raw_meta);
    void onIcyProtocolEvent(const QString &line);
    void onIcyDisconnected();
    void onIcyError(const QString &msg);

private:
    /* ---- device info ---- */
    struct DeviceEntry {
        int     pa_index   = -1;
        QString name;
        QString qt_device_id;  /* QAudioDevice.id() for QAudioOutput routing */
        QString category;      /* "System", "USB Audio", "Bluetooth" */
        bool    is_input   = false;
        bool    is_output  = false;
        int     in_ch      = 0;
        int     out_ch     = 0;
        double  sample_rate = 44100.0;
    };

    void populateDevices();
    void buildOutputCombo();
    void buildInputCombo();
    void buildStreamCombo();

    /* Passthrough */
    void startPassthrough();
    void stopPassthrough();
    void checkFeedbackRisk();
    void pushStereo(const float *pcm, int frames, int src_channels);

    /* Stream monitor */
    void startStream();
    void stopStream();
    void updateMetricsDisplay(const QString &title, int listeners, int bitrate_kbps,
                               const QString &server_type);
    void populateIcyMetadataTab(const IcyStreamReader::IcyHeaders &hdrs);

    /* Protocol log helpers */
    void logEvent(const QString &line);

    /* PortAudio callbacks */
#ifdef HAVE_PORTAUDIO
    static int paInputCb(const void *in, void *out, unsigned long frames,
                         const PaStreamCallbackTimeInfo *, PaStreamCallbackFlags, void *ud);
    static int paOutputCb(const void *in, void *out, unsigned long frames,
                          const PaStreamCallbackTimeInfo *, PaStreamCallbackFlags, void *ud);
    PaStream *pa_input_  = nullptr;
    PaStream *pa_output_ = nullptr;
#endif

    /* Ring buffer (SPSC lock-free, stereo float, power-of-2) */
    static constexpr int RING_CAP = 65536;
    float            ring_buf_[RING_CAP] = {};
    std::atomic<int> ring_head_{0};
    std::atomic<int> ring_tail_{0};
    void rbPush(const float *data, int n);
    int  rbPop (float *out,        int n);

    /* State */
    std::vector<DeviceEntry> output_devs_;
    std::vector<DeviceEntry> input_devs_;
    std::vector<SlotInfo>    slot_list_;

    bool   passthrough_running_  = false;
    bool   stream_tuned_in_      = false;
    int    eavesdrop_slot_id_    = -1;   /* -1 = use PA input device */
    int    out_dev_pa_idx_       = -1;
    int    in_dev_pa_idx_        = -1;
    int    in_channels_          = 2;
    int    stream_combo_slot_idx_= -1;   /* index into slot_list_ */

    std::atomic<float> peak_level_{0.0f};

    /* Qt Multimedia (stream monitor playback) */
    QMediaPlayer         *stream_player_    = nullptr;
    QAudioOutput         *stream_audio_out_ = nullptr;
    QNetworkAccessManager*nam_              = nullptr;
    QTimer               *stats_poll_timer_ = nullptr;

    /* ICY1/ICY2.2 protocol monitor (separate TCP connection, no audio decode) */
    IcyStreamReader      *icy_reader_       = nullptr;

    /* ---- UI: output device panel ---- */
    QComboBox    *cmb_output_        = nullptr;
    QPushButton  *btn_scan_usb_      = nullptr;

    /* ---- UI: passthrough panel ---- */
    QComboBox    *cmb_input_source_       = nullptr;
    QComboBox    *cmb_input_sample_rate_  = nullptr;
    QComboBox    *cmb_input_channels_     = nullptr;
    QLabel       *lbl_feedback_warn_ = nullptr;
    QLabel       *lbl_passthru_stat_ = nullptr;
    QPushButton  *btn_start_stop_    = nullptr;
    QProgressBar *pb_level_          = nullptr;

    /* ---- UI: stream monitor panel ---- */
    QComboBox    *cmb_stream_         = nullptr;
    QPushButton  *btn_tune_in_        = nullptr;
    QPushButton  *btn_play_pause_     = nullptr;  /* mini player: play/pause */
    QPushButton  *btn_stop_stream_    = nullptr;  /* mini player: stop */
    QLabel       *lbl_stream_status_  = nullptr;
    QLabel       *lbl_listeners_      = nullptr;
    QLabel       *lbl_now_playing_    = nullptr;
    QLabel       *lbl_stream_bitrate_ = nullptr;
    QLabel       *lbl_server_type_    = nullptr;

    /* ---- UI: bottom tab widget ---- */
    QTabWidget     *bottom_tabs_     = nullptr;
    QPlainTextEdit *log_view_        = nullptr;  /* Protocol Log tab */
    QPlainTextEdit *icy_meta_view_   = nullptr;  /* ICY Metadata tab */
};

} // namespace mc1
