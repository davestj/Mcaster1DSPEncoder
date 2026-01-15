// archive_writer.cpp — WAV + optional MP3 simultaneous archive output
// Phase 3 — Mcaster1DSPEncoder Linux v1.2.0
#include "archive_writer.h"

#include <cstring>
#include <ctime>
#include <cstdio>
#include <cerrno>
#include <vector>
#include <filesystem>
#include <climits>

#ifdef HAVE_LAME
#include <lame/lame.h>
#endif

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// WAV file header (44 bytes, PCM 16-bit)
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct WavHeader {
    char     riff[4]      = {'R','I','F','F'};
    uint32_t file_size    = 0;    // filled on close
    char     wave[4]      = {'W','A','V','E'};
    char     fmt[4]       = {'f','m','t',' '};
    uint32_t fmt_len      = 16;
    uint16_t audio_fmt    = 1;    // PCM
    uint16_t num_channels = 2;
    uint32_t sample_rate  = 44100;
    uint32_t byte_rate    = 0;
    uint16_t block_align  = 0;
    uint16_t bits_per_smpl= 16;
    char     data[4]      = {'d','a','t','a'};
    uint32_t data_size    = 0;    // filled on close
};
#pragma pack(pop)

// ---------------------------------------------------------------------------
// ArchiveWriter — constructor
// ---------------------------------------------------------------------------
ArchiveWriter::ArchiveWriter()
    : cfg_(Config{})
{
}

ArchiveWriter::ArchiveWriter(const Config& cfg)
    : cfg_(cfg)
{
}

ArchiveWriter::~ArchiveWriter()
{
    if (is_open_.load()) close();
}

// ---------------------------------------------------------------------------
// open — create new archive files
// ---------------------------------------------------------------------------
bool ArchiveWriter::open(int sample_rate, int channels)
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (is_open_.load()) return false;

    sample_rate_ = sample_rate;
    channels_    = channels;
    frames_written_.store(0);

    // Create output directory
    try { fs::create_directories(cfg_.archive_dir); }
    catch (const std::exception& e) {
        fprintf(stderr, "[Archive] mkdir failed: %s\n", e.what());
        return false;
    }

    // Generate filename from timestamp
    time_t now = std::time(nullptr);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d_%H-%M-%S", &tm_buf);

    wav_path_ = cfg_.archive_dir + "/" + cfg_.station_name + "_" + ts + ".wav";
    mp3_path_ = cfg_.archive_dir + "/" + cfg_.station_name + "_" + ts + ".mp3";

    // Open WAV file
    if (cfg_.write_wav) {
        wav_file_ = fopen(wav_path_.c_str(), "wb");
        if (!wav_file_) {
            fprintf(stderr, "[Archive] Cannot open WAV: %s: %s\n",
                    wav_path_.c_str(), strerror(errno));
            return false;
        }
        if (!write_wav_header()) {
            fclose(wav_file_);
            wav_file_ = nullptr;
            return false;
        }
    }

    // Init LAME for MP3 archive
    if (cfg_.write_mp3) {
        if (!init_lame()) {
            fprintf(stderr, "[Archive] LAME init failed — MP3 archive disabled.\n");
        }
    }

    is_open_.store(true);
    fprintf(stderr, "[Archive] Opened: %s\n", wav_path_.c_str());
    return true;
}

// ---------------------------------------------------------------------------
// write_pcm — accept float32 interleaved, write to WAV + MP3
// ---------------------------------------------------------------------------
bool ArchiveWriter::write_pcm(const float* pcm, size_t frames)
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (!is_open_.load()) return false;

    if (wav_file_) {
        // Convert float32 → int16
        size_t samples = frames * static_cast<size_t>(channels_);
        std::vector<int16_t> i16(samples);
        for (size_t i = 0; i < samples; ++i) {
            float s = pcm[i];
            if (s >  1.0f) s =  1.0f;
            if (s < -1.0f) s = -1.0f;
            i16[i] = static_cast<int16_t>(s * 32767.0f);
        }
        fwrite(i16.data(), sizeof(int16_t), samples, wav_file_);
    }

    if (lame_enc_) {
        write_mp3_frame(pcm, frames);
    }

    frames_written_.fetch_add(frames);
    return true;
}

// ---------------------------------------------------------------------------
// close — finalize files and optionally generate RSS
// ---------------------------------------------------------------------------
std::string ArchiveWriter::close()
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (!is_open_.load()) return {};

    is_open_.store(false);

    // Finalize WAV header
    if (wav_file_) {
        finalize_wav_header();
        fclose(wav_file_);
        wav_file_ = nullptr;
    }

    // Flush + close LAME
    close_lame();

    std::string result = wav_path_;
    fprintf(stderr, "[Archive] Closed: %s  (%llu frames = %.1f s)\n",
            wav_path_.c_str(),
            (unsigned long long)frames_written_.load(),
            static_cast<double>(frames_written_.load()) / sample_rate_);

    // TODO: call podcast_rss_gen if cfg_.generate_rss
    // podcast_rss_gen(wav_path_, cfg_.rss_feed_url, ...);

    return result;
}

// ---------------------------------------------------------------------------
// write_wav_header — write placeholder header (overwritten on close)
// ---------------------------------------------------------------------------
bool ArchiveWriter::write_wav_header()
{
    WavHeader hdr;
    hdr.num_channels  = static_cast<uint16_t>(channels_);
    hdr.sample_rate   = static_cast<uint32_t>(sample_rate_);
    hdr.bits_per_smpl = 16;
    hdr.block_align   = static_cast<uint16_t>(channels_ * 2);
    hdr.byte_rate     = static_cast<uint32_t>(sample_rate_ * channels_ * 2);
    hdr.file_size     = 36;   // placeholder
    hdr.data_size     = 0;    // placeholder

    return fwrite(&hdr, sizeof(hdr), 1, wav_file_) == 1;
}

// ---------------------------------------------------------------------------
// finalize_wav_header — patch sizes into WAV header
// ---------------------------------------------------------------------------
bool ArchiveWriter::finalize_wav_header()
{
    if (!wav_file_) return false;

    uint32_t data_bytes = static_cast<uint32_t>(
        frames_written_.load() * static_cast<uint64_t>(channels_) * 2);
    uint32_t file_size  = 36 + data_bytes;

    // Patch file_size at byte 4
    fseek(wav_file_, 4, SEEK_SET);
    fwrite(&file_size, 4, 1, wav_file_);

    // Patch data_size at byte 40
    fseek(wav_file_, 40, SEEK_SET);
    fwrite(&data_bytes, 4, 1, wav_file_);

    fseek(wav_file_, 0, SEEK_END);
    return true;
}

// ---------------------------------------------------------------------------
// LAME helpers
// ---------------------------------------------------------------------------
bool ArchiveWriter::init_lame()
{
#ifdef HAVE_LAME
    lame_global_flags* gfp = lame_init();
    if (!gfp) return false;

    lame_set_in_samplerate(gfp, sample_rate_);
    lame_set_num_channels(gfp, channels_);
    lame_set_brate(gfp, cfg_.mp3_bitrate);
    lame_set_quality(gfp, 2);  // 2 = near-best quality
    lame_set_mode(gfp, channels_ == 1 ? MONO : STEREO);

    if (lame_init_params(gfp) < 0) {
        lame_close(gfp);
        return false;
    }

    // Open MP3 output file
    FILE* mp3f = fopen(mp3_path_.c_str(), "wb");
    if (!mp3f) { lame_close(gfp); return false; }

    // We store both: pack gfp and FILE* into a struct
    // Simple approach: store FILE* in lame_enc_'s high bits — instead
    // use a tiny heap struct
    struct LameState { lame_global_flags* gfp; FILE* f; };
    auto* ls = new LameState{gfp, mp3f};
    lame_enc_ = ls;
    return true;
#else
    return false;
#endif
}

bool ArchiveWriter::write_mp3_frame(const float* pcm, size_t frames)
{
#ifdef HAVE_LAME
    if (!lame_enc_) return false;
    struct LameState { lame_global_flags* gfp; FILE* f; };
    auto* ls = static_cast<LameState*>(lame_enc_);

    // Separate L/R channels for lame_encode_buffer_ieee_float
    size_t f = frames;
    std::vector<float> left(f), right(f);
    if (channels_ == 2) {
        for (size_t i = 0; i < f; ++i) {
            left[i]  = pcm[i * 2];
            right[i] = pcm[i * 2 + 1];
        }
    } else {
        for (size_t i = 0; i < f; ++i) {
            left[i] = right[i] = pcm[i];
        }
    }

    std::vector<unsigned char> mp3buf(f + f / 4 + 7200);
    int n = lame_encode_buffer_ieee_float(ls->gfp,
                left.data(), right.data(),
                static_cast<int>(f),
                mp3buf.data(),
                static_cast<int>(mp3buf.size()));
    if (n > 0) fwrite(mp3buf.data(), 1, static_cast<size_t>(n), ls->f);
    return n >= 0;
#else
    (void)pcm; (void)frames;
    return false;
#endif
}

void ArchiveWriter::close_lame()
{
#ifdef HAVE_LAME
    if (!lame_enc_) return;
    struct LameState { lame_global_flags* gfp; FILE* f; };
    auto* ls = static_cast<LameState*>(lame_enc_);

    // Flush
    std::vector<unsigned char> buf(7200);
    int n = lame_encode_flush(ls->gfp, buf.data(), static_cast<int>(buf.size()));
    if (n > 0) fwrite(buf.data(), 1, static_cast<size_t>(n), ls->f);

    fclose(ls->f);
    lame_close(ls->gfp);
    delete ls;
    lame_enc_ = nullptr;
#endif
}
