// encoder_slot.h — Per-encoder-slot state machine
// Phase L5.1 — Mcaster1DSPEncoder Linux v1.4.1
#pragma once

#include "audio_source.h"
#include "stream_client.h"
#include "archive_writer.h"
#include "playlist_parser.h"
#include "dsp/dsp_chain.h"

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <functional>
#include <cstdint>

// ---------------------------------------------------------------------------
// EncoderConfig — everything needed to define one encoder slot
// ---------------------------------------------------------------------------
struct EncoderConfig {
    // Identity
    int         slot_id     = 0;
    std::string name        = "Encoder 1";

    // Audio input
    enum class InputType { DEVICE, PLAYLIST, URL } input_type = InputType::PLAYLIST;
    int         device_index = -1;         // for InputType::DEVICE
    std::string playlist_path;             // for InputType::PLAYLIST

    // Codec
    enum class Codec {
        MP3,
        VORBIS,
        OPUS,
        FLAC,
        AAC_LC,    // AAC Low Complexity (ISO 14496-3)
        AAC_HE,    // HE-AAC v1 / AAC+ (SBR)
        AAC_HE_V2, // HE-AAC v2 / AAC++ (SBR + Parametric Stereo)
        AAC_ELD    // AAC Enhanced Low Delay (SBR-ELD, low-latency)
    } codec = Codec::MP3;
    int         bitrate_kbps = 128;
    int         quality      = 5;          // VBR quality (codec-specific, 0-10)
    int         sample_rate  = 44100;
    int         channels     = 2;

    // Encode mode (MP3: CBR/VBR/ABR; Opus: CBR/VBR; others: ignored)
    enum class EncodeMode { CBR, VBR, ABR } encode_mode = EncodeMode::CBR;

    // Channel mode (MP3: JOINT/STEREO/MONO; AAC HE-v2: must be STEREO)
    enum class ChannelMode { JOINT, STEREO, MONO } channel_mode = ChannelMode::JOINT;

    // Server target
    StreamTarget stream_target;

    // Volume
    float       volume = 1.0f;             // 0.0 – 2.0

    // Playlist behaviour
    bool        shuffle    = false;
    bool        repeat_all = true;

    // Archive
    bool        archive_enabled = false;
    std::string archive_dir;

    // DSP chain
    bool        dsp_eq_enabled         = false;
    bool        dsp_agc_enabled        = false;
    bool        dsp_crossfade_enabled  = true;
    float       dsp_crossfade_duration = 3.0f;   // seconds
    std::string dsp_eq_preset;                    // "flat","classic_rock","country",...

    // Auto-start on encoder launch
    bool        auto_start           = false;
    int         auto_start_delay_sec = 0;         // seconds to wait before auto-starting

    // Auto-reconnect on connection loss; 0 attempts = unlimited
    bool        auto_reconnect            = true;
    int         reconnect_interval_sec    = 5;    // seconds between reconnect attempts
    int         reconnect_max_attempts    = 0;    // 0 = unlimited; N > 0 = stop after N fails → SLEEP
};

// ---------------------------------------------------------------------------
// EncoderSlot — one encoder + source + stream client + archive
// ---------------------------------------------------------------------------
class EncoderSlot {
public:
    enum class State {
        IDLE,
        STARTING,
        CONNECTING,
        LIVE,
        RECONNECTING,
        SLEEP,       // was LIVE, exhausted all reconnect attempts — needs manual wake()
        ERROR,
        STOPPING
    };

    struct Stats {
        int         slot_id      = 0;
        State       state        = State::IDLE;
        std::string state_str;
        uint64_t    bytes_sent   = 0;
        uint64_t    uptime_sec   = 0;
        int         track_index  = 0;
        int         track_count  = 0;
        std::string current_title;
        std::string current_artist;
        int64_t     position_ms  = 0;
        int64_t     duration_ms  = 0;
        float       volume       = 1.0f;
        bool        is_live      = false;
        std::string last_error;
    };

    explicit EncoderSlot(const EncoderConfig& cfg);
    ~EncoderSlot();

    // Start encoding and streaming.
    bool start();

    // Stop gracefully.
    void stop();

    // Restart — reconnect without full stop
    void restart();

    // Wake from SLEEP state (max reconnect attempts exhausted) — resets to IDLE and restarts
    void wake();

    // Load a new playlist without restarting
    bool load_playlist(const std::string& path);

    // Skip to next track in playlist
    void skip_track();

    // Update volume (applied in audio callback before encoding)
    void set_volume(float v);

    // Live-update DSP chain parameters (takes effect on the next audio buffer)
    void reconfigure_dsp(const mc1dsp::DspChainConfig& cfg);

    // Push ICY metadata to the connected server
    void push_metadata(const std::string& title,
                       const std::string& artist  = "",
                       const std::string& album   = "",
                       const std::string& artwork = "");

    // Get current runtime statistics
    Stats stats() const;

    State state() const { return state_.load(); }

    const EncoderConfig& config() const { return cfg_; }
    void update_config(const EncoderConfig& cfg);

    static std::string state_to_string(State s);

private:
    EncoderConfig   cfg_;
    mutable std::mutex mtx_;

    std::atomic<State>    state_{State::IDLE};
    std::atomic<bool>     stopping_{false};         // set in stop(); guards detached threads
    std::atomic<uint64_t> meta_gen_{0};             // incremented on start(); stale-thread guard
    std::atomic<bool>     first_connect_done_{false}; // false until first CONNECTED; guards re-push
    std::string     last_error_;

    // Audio sources
    std::unique_ptr<AudioSource> source_;

    // Codec encoder (opaque handles to avoid header pollution)
    void*   lame_enc_    = nullptr;   // lame_global_flags*
    void*   vorbis_enc_  = nullptr;   // vorbis_dsp_state*
    void*   vorbis_blk_  = nullptr;   // vorbis_block*
    void*   opus_enc_    = nullptr;   // OggOpusEnc*
    void*   flac_enc_    = nullptr;   // FLAC__StreamEncoder*
    void*   aac_enc_     = nullptr;   // AacState* (fdk-aac)

    // Output
    std::shared_ptr<StreamClient>  stream_client_;   // shared_ptr: detached threads hold a copy
    std::unique_ptr<ArchiveWriter> archive_;

    // DSP chain (EQ → AGC; crossfader applied at track boundaries)
    std::unique_ptr<mc1dsp::DspChain> dsp_chain_;

    // Playlist state
    std::vector<PlaylistEntry> playlist_;
    int                        playlist_pos_ = 0;
    bool                       advance_requested_ = false;
    mutable std::mutex         playlist_mtx_;
    std::mutex                 advance_mtx_;   // serializes concurrent advance_playlist() calls

    // Metadata
    std::string current_title_;
    std::string current_artist_;
    int64_t     current_duration_ms_ = 0;

    // Stats
    uint64_t    start_time_     = 0;   // unix epoch seconds
    uint64_t    bytes_encoded_  = 0;

    // Codec initializers
    bool init_lame();
    bool init_vorbis();
    bool init_opus();
    bool init_flac();
    bool init_aac();    // libfdk-aac (AAC-LC / HE-AACv1 / HE-AACv2 / AAC-ELD)

    // Codec frame encoders — return bytes written to out_buf
    int  encode_mp3   (const float* pcm, size_t frames, uint8_t* out_buf, size_t buf_len);
    int  encode_vorbis(const float* pcm, size_t frames, uint8_t* out_buf, size_t buf_len);
    int  encode_opus  (const float* pcm, size_t frames, uint8_t* out_buf, size_t buf_len);
    int  encode_flac  (const float* pcm, size_t frames, uint8_t* out_buf, size_t buf_len);
    int  encode_aac   (const float* pcm, size_t frames, uint8_t* out_buf, size_t buf_len);

    void close_codec();

    // Audio callback — called from the source's audio thread
    void on_audio(const float* pcm, size_t frames, int channels, int sample_rate);

    // Playlist management
    bool advance_playlist();
    bool open_next_track();
    void shuffle_playlist();

    void set_state(State s);
    void set_error(const std::string& msg);
};
