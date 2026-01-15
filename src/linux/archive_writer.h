// archive_writer.h — WAV + optional MP3 simultaneous archive output
// Phase 3 — Mcaster1DSPEncoder Linux v1.2.0
#pragma once

#include <string>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <mutex>
#include <atomic>

// ---------------------------------------------------------------------------
// ArchiveWriter — writes simultaneous WAV (always) + MP3 (if LAME available)
//
// Usage:
//   ArchiveWriter aw("/var/archives", "station1");
//   aw.open(44100, 2);
//   // feed PCM float32 interleaved stereo
//   aw.write_pcm(pcm_buf, frames);
//   aw.close();        // also triggers RSS generation
// ---------------------------------------------------------------------------
class ArchiveWriter {
public:
    struct Config {
        std::string archive_dir   = "/var/archives";   // base output directory
        std::string station_name  = "station";         // used in filename
        bool        write_wav     = true;              // always-on raw PCM
        bool        write_mp3     = true;              // requires LAME
        int         mp3_bitrate   = 128;               // kbps
        bool        generate_rss  = true;              // call podcast_rss_gen on close
        std::string rss_feed_url;                      // base URL for RSS enclosure hrefs
    };

    ArchiveWriter();                          // default config (/var/archives, PCM WAV)
    explicit ArchiveWriter(const Config& cfg);
    ~ArchiveWriter();

    // Open new archive files for this session.
    // Generates filenames from timestamp: {station}_{YYYY-MM-DD}_{HH-MM-SS}.wav
    bool open(int sample_rate, int channels);

    // Write interleaved float32 PCM (range [-1.0, +1.0])
    bool write_pcm(const float* pcm, size_t frames);

    // Close and finalize files.  Returns path of written WAV file.
    std::string close();

    bool        is_open()        const { return is_open_.load(); }
    uint64_t    frames_written() const { return frames_written_.load(); }
    std::string current_wav_path() const { return wav_path_; }

private:
    Config      cfg_;
    std::mutex  mtx_;
    std::atomic<bool>     is_open_{false};
    std::atomic<uint64_t> frames_written_{0};

    int         sample_rate_ = 44100;
    int         channels_    = 2;

    std::string wav_path_;
    std::string mp3_path_;
    FILE*       wav_file_ = nullptr;

    // LAME handle (opaque void* to avoid lame.h header exposure)
    void*       lame_enc_ = nullptr;

    bool write_wav_header();
    bool finalize_wav_header();
    bool init_lame();
    bool write_mp3_frame(const float* pcm, size_t frames);
    void close_lame();
};
