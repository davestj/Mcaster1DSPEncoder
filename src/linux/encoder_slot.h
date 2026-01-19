// encoder_slot.h — Per-encoder-slot state machine
// Phase 3 — Mcaster1DSPEncoder Linux v1.2.0
#pragma once

#include "audio_source.h"
#include "stream_client.h"
#include "archive_writer.h"
#include "playlist_parser.h"

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
    enum class Codec { MP3, VORBIS, OPUS, FLAC } codec = Codec::MP3;
    int         bitrate_kbps = 128;
    int         quality      = 5;          // VBR quality (codec-specific)
    int         sample_rate  = 44100;
    int         channels     = 2;

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

    // Load a new playlist without restarting
    bool load_playlist(const std::string& path);

    // Skip to next track in playlist
    void skip_track();

    // Update volume (applied in audio callback before encoding)
    void set_volume(float v);

    // Push ICY metadata to the connected server
    void push_metadata(const std::string& title, const std::string& artist = "");

    // Get current runtime statistics
    Stats stats() const;

    State state() const { return state_.load(); }

    const EncoderConfig& config() const { return cfg_; }
    void update_config(const EncoderConfig& cfg);

    static std::string state_to_string(State s);

private:
    EncoderConfig   cfg_;
    mutable std::mutex mtx_;

    std::atomic<State> state_{State::IDLE};
    std::string     last_error_;

    // Audio sources
    std::unique_ptr<AudioSource> source_;

    // Codec encoder (opaque handles to avoid header pollution)
    void*   lame_enc_    = nullptr;   // lame_global_flags*
    void*   vorbis_enc_  = nullptr;   // vorbis_dsp_state*
    void*   vorbis_blk_  = nullptr;   // vorbis_block*
    void*   opus_enc_    = nullptr;   // OggOpusEnc*
    void*   flac_enc_    = nullptr;   // FLAC__StreamEncoder*

    // Output
    std::unique_ptr<StreamClient>  stream_client_;
    std::unique_ptr<ArchiveWriter> archive_;

    // Playlist state
    std::vector<PlaylistEntry> playlist_;
    int                        playlist_pos_ = 0;
    bool                       advance_requested_ = false;
    mutable std::mutex         playlist_mtx_;

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

    // Codec frame encoders — return bytes written to out_buf
    int  encode_mp3   (const float* pcm, size_t frames, uint8_t* out_buf, size_t buf_len);
    int  encode_vorbis(const float* pcm, size_t frames, uint8_t* out_buf, size_t buf_len);
    int  encode_opus  (const float* pcm, size_t frames, uint8_t* out_buf, size_t buf_len);
    int  encode_flac  (const float* pcm, size_t frames, uint8_t* out_buf, size_t buf_len);

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
