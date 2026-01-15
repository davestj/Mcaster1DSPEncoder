// file_source.h — File-based audio source (libmpg123 + libavcodec)
// Phase 3 — Mcaster1DSPEncoder Linux v1.2.0
#pragma once

#include "audio_source.h"
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>

// ---------------------------------------------------------------------------
// FileSource — decode an audio file and feed PCM to an AudioCallback.
//
// Supports:
//   MP3  : libmpg123 (fast, low-latency)
//   Other: libavcodec/avformat (OGG, FLAC, Opus, AAC, M4A, WAV, AIFF, ...)
//
// The decode loop runs in a background thread.
// When the file ends, end_of_file_cb is called (on the decode thread).
// The caller may then call open() + start() again for the next track.
// ---------------------------------------------------------------------------
class FileSource : public AudioSource {
public:
    using EOFCallback = std::function<void()>;

    struct TrackInfo {
        std::string path;
        std::string title;
        std::string artist;
        std::string album;
        std::string genre;
        int         year          = 0;
        int64_t     duration_ms   = 0;
        int         bitrate_kbps  = 0;
        int         sample_rate   = 0;
        int         channels      = 0;
    };

    explicit FileSource(int target_sample_rate = 44100, int target_channels = 2);
    ~FileSource() override;

    // Open a file for decoding.  Does NOT start decoding yet.
    // Returns false if format is unsupported or file can't be opened.
    bool open(const std::string& file_path);

    // Start the decode thread.  cb will be called with PCM buffers.
    bool start(AudioCallback cb) override;

    // Stop the decode thread (waits for current buffer to finish).
    void stop() override;

    bool        is_running() const override { return running_.load(); }
    int         sample_rate() const override { return target_sr_;   }
    int         channels()    const override { return target_ch_;   }
    std::string name()        const override { return current_path_; }

    // Current track metadata (populated after open())
    const TrackInfo& track_info() const { return track_info_; }

    // Called at end of file, from the decode thread.
    void set_eof_callback(EOFCallback cb) { eof_cb_ = cb; }

    // Seek to position (milliseconds). Returns false if not supported.
    bool seek(int64_t pos_ms);

    // Current playback position
    int64_t position_ms() const { return position_ms_.load(); }

private:
    int target_sr_;   // target sample rate for output
    int target_ch_;   // target channels for output

    std::string  current_path_;
    TrackInfo    track_info_;

    AudioCallback callback_;
    EOFCallback   eof_cb_;

    std::thread   decode_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_req_{false};
    std::atomic<int64_t> position_ms_{0};

    // Opaque decoder handles — avoid header pollution
    void* mpg123_handle_ = nullptr;   // mpg123_handle*
    void* av_fmt_ctx_    = nullptr;   // AVFormatContext*
    void* av_codec_ctx_  = nullptr;   // AVCodecContext*
    void* swr_ctx_       = nullptr;   // SwrContext*
    int   av_stream_idx_ = -1;

    enum class Decoder { NONE, MPG123, AVCODEC } decoder_ = Decoder::NONE;

    bool open_mpg123(const std::string& path);
    bool open_avcodec(const std::string& path);

    void decode_loop();
    void decode_loop_mpg123();
    void decode_loop_avcodec();

    void close_mpg123();
    void close_avcodec();
};
