/*
 * Mcaster1DSPEncoder -- macOS Qt 6 Build
 * encoder_slot.h -- Per-encoder-slot state machine
 *
 * Phase M3/M4 -- Audio encoding pipeline + streaming client
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "config_types.h"
#include "stream_client.h"
#include "audio_source.h"
#include "archive_writer.h"
#include "playlist_parser.h"
#include "dsp/dsp_chain.h"
#include "video/video_source.h"
#include "video/video_encoder.h"

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <functional>
#include <cstdint>

// ---------------------------------------------------------------------------
// EncoderSlot -- one encoder + source + DSP chain + archive
//
// Uses mc1::EncoderConfig from config_types.h (no duplicate struct).
// StreamClient wired in Phase M4 -- full TCP streaming to Icecast2/Shoutcast/DNAS.
// ---------------------------------------------------------------------------
class EncoderSlot {
public:
    // State mirrors mc1::SlotState but is scoped to this class for API
    // compatibility with the Linux EncoderSlot interface.
    enum class State {
        IDLE,
        STARTING,
        CONNECTING,
        LIVE,
        RECONNECTING,
        SLEEP,       // was LIVE, exhausted all reconnect attempts -- needs manual wake()
        ERROR,
        STOPPING
    };

    struct Stats {
        int         slot_id       = 0;
        State       state         = State::IDLE;
        std::string state_str;
        uint64_t    bytes_sent    = 0;
        uint64_t    uptime_sec    = 0;
        int         track_index   = 0;
        int         track_count   = 0;
        std::string current_title;
        std::string current_artist;
        int64_t     position_ms   = 0;
        int64_t     duration_ms   = 0;
        float       volume        = 1.0f;
        bool        is_live       = false;
        std::string last_error;

        /* Phase M5: per-slot DNAS stats (populated by DnasSlotPoller) */
        int         listeners      = 0;
        int         listener_peak  = 0;
        int         out_kbps       = 0;
    };

    explicit EncoderSlot(const mc1::EncoderConfig& cfg);
    ~EncoderSlot();

    // Start encoding (and streaming once StreamClient is ported in Phase M4).
    bool start();

    // Stop gracefully.
    void stop();

    // Restart -- stop + start
    void restart();

    // Wake from SLEEP state (max reconnect attempts exhausted) -- resets to IDLE and restarts
    void wake();

    // Load a new playlist without restarting
    bool load_playlist(const std::string& path);

    // Skip to next track in playlist
    void skip_track();

    // Update volume (applied in audio callback before encoding)
    void set_volume(float v);

    // Live-update DSP chain parameters (takes effect on the next audio buffer)
    void reconfigure_dsp(const mc1dsp::DspChainConfig& cfg);

    // Phase M6: Direct access to DSP chain (for 31-band EQ / Sonic Enhancer)
    bool has_dsp_chain() const { return dsp_chain_ != nullptr; }
    mc1dsp::DspChain& dsp_chain() { return *dsp_chain_; }

    // Phase M9: Audio tap — video pipeline can receive a copy of encoded audio
    using AudioTapCallback = std::function<void(const uint8_t* data, size_t len,
                                                 uint32_t timestamp_ms, bool is_aac)>;
    void set_audio_tap(AudioTapCallback cb);
    void clear_audio_tap();

    // Push ICY metadata to the connected server
    // Phase M4: will forward to StreamClient::send_admin_metadata()
    void push_metadata(const std::string& title,
                       const std::string& artist  = "",
                       const std::string& album   = "",
                       const std::string& artwork = "");

    // Get current runtime statistics
    Stats stats() const;

    State state() const { return state_.load(); }

    const mc1::EncoderConfig& config() const { return cfg_; }
    void update_config(const mc1::EncoderConfig& cfg);

    static std::string state_to_string(State s);

private:
    mc1::EncoderConfig  cfg_;
    float               volume_ = 1.0f;     // runtime volume (0.0 - 2.0)
    mutable std::mutex  mtx_;

    std::atomic<State>    state_{State::IDLE};
    std::atomic<bool>     stopping_{false};          // set in stop(); guards detached threads
    std::atomic<uint64_t> meta_gen_{0};              // incremented on start(); stale-thread guard
    std::atomic<bool>     first_connect_done_{false}; // false until first CONNECTED; guards re-push
    std::string           last_error_;

    // Audio source
    std::unique_ptr<AudioSource> source_;

    // Codec encoder (opaque handles to avoid header pollution)
    void*   lame_enc_    = nullptr;   // lame_global_flags*
    void*   vorbis_enc_  = nullptr;   // vorbis_dsp_state* (via VorbisState*)
    void*   vorbis_blk_  = nullptr;   // vorbis_block* (unused -- managed inside VorbisState)
    void*   opus_enc_    = nullptr;   // OpusState* (OggOpusEnc* + pending buffer)
    void*   flac_enc_    = nullptr;   // FlacState* (FLAC__StreamEncoder* + pending buffer)
    void*   aac_enc_     = nullptr;   // AacState*  (HANDLE_AACENCODER + buffers)

    // StreamClient — manages TCP connection to broadcast server
    std::shared_ptr<mc1::StreamClient> stream_client_;

    // Archive writer
    std::unique_ptr<ArchiveWriter> archive_;

    // Phase M9: Audio tap for video pipeline
    AudioTapCallback audio_tap_;
    std::mutex       audio_tap_mtx_;

    // DSP chain (EQ -> AGC; crossfader applied at track boundaries)
    std::unique_ptr<mc1dsp::DspChain> dsp_chain_;

    // Phase M6: Video
    std::unique_ptr<mc1::VideoSource>  video_source_;
    std::unique_ptr<mc1::VideoEncoder> video_encoder_;

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
    bool init_aac();     // libfdk-aac (AAC-LC / HE-AACv1 / HE-AACv2 / AAC-ELD)

    // Codec frame encoders -- return bytes written to out_buf
    int  encode_mp3   (const float* pcm, size_t frames, uint8_t* out_buf, size_t buf_len);
    int  encode_vorbis(const float* pcm, size_t frames, uint8_t* out_buf, size_t buf_len);
    int  encode_opus  (const float* pcm, size_t frames, uint8_t* out_buf, size_t buf_len);
    int  encode_flac  (const float* pcm, size_t frames, uint8_t* out_buf, size_t buf_len);
    int  encode_aac   (const float* pcm, size_t frames, uint8_t* out_buf, size_t buf_len);

    void close_codec();

    // Audio callback -- called from the source's audio thread
    void on_audio(const float* pcm, size_t frames, int channels, int sample_rate);

    // Playlist management
    bool advance_playlist();
    bool open_next_track();
    void shuffle_playlist();

    void set_state(State s);
    void set_error(const std::string& msg);
};
