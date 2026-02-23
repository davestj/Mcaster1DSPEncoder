// file_source.cpp — File-based audio source (libmpg123 + libavcodec)
// Phase 3 — Mcaster1DSPEncoder Linux v1.2.0
#include "file_source.h"

#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <filesystem>

#ifdef HAVE_MPG123
#include <mpg123.h>
#endif

#ifdef HAVE_AVFORMAT
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
}
#endif

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// FileSource — constructor / destructor
// ---------------------------------------------------------------------------
FileSource::FileSource(int target_sample_rate, int target_channels)
    : target_sr_(target_sample_rate)
    , target_ch_(target_channels)
{
#ifdef HAVE_MPG123
    static bool mpg123_initialized = false;
    if (!mpg123_initialized) {
        mpg123_init();
        mpg123_initialized = true;
    }
#endif
}

FileSource::~FileSource()
{
    stop();
    close_mpg123();
    close_avcodec();
}

// ---------------------------------------------------------------------------
// open — select decoder based on file extension
// ---------------------------------------------------------------------------
bool FileSource::open(const std::string& file_path)
{
    stop();
    close_mpg123();
    close_avcodec();

    current_path_ = file_path;
    track_info_ = TrackInfo{};
    track_info_.path = file_path;
    position_ms_.store(0);

    // Try to get filename without path for display
    try {
        track_info_.title = fs::path(file_path).stem().string();
    } catch (...) {
        track_info_.title = file_path;
    }

    // Choose decoder by extension
    std::string ext;
    {
        auto p = file_path.rfind('.');
        if (p != std::string::npos) ext = file_path.substr(p);
        for (auto& c : ext) c = static_cast<char>(std::tolower(c));
    }

    if (ext == ".mp3") {
#ifdef HAVE_MPG123
        if (open_mpg123(file_path)) {
            decoder_ = Decoder::MPG123;
            return true;
        }
#endif
        // Fall through to avcodec
    }

    // avcodec handles everything else (and MP3 fallback)
#ifdef HAVE_AVFORMAT
    if (open_avcodec(file_path)) {
        decoder_ = Decoder::AVCODEC;
        return true;
    }
#endif

    fprintf(stderr, "[FileSource] Cannot open: %s\n", file_path.c_str());
    return false;
}

// ---------------------------------------------------------------------------
// start — launch decode thread
// ---------------------------------------------------------------------------
bool FileSource::start(AudioCallback cb)
{
    if (running_.load()) return false;
    if (decoder_ == Decoder::NONE) return false;

    callback_  = cb;
    stop_req_.store(false);
    running_.store(true);

    decode_thread_ = std::thread(&FileSource::decode_loop, this);
    return true;
}

// ---------------------------------------------------------------------------
// stop
// ---------------------------------------------------------------------------
void FileSource::stop()
{
    stop_req_.store(true);
    if (decode_thread_.joinable()) decode_thread_.join();
    running_.store(false);
}

// ---------------------------------------------------------------------------
// seek
// ---------------------------------------------------------------------------
bool FileSource::seek(int64_t pos_ms)
{
#ifdef HAVE_MPG123
    if (decoder_ == Decoder::MPG123 && mpg123_handle_) {
        auto* mh = static_cast<mpg123_handle*>(mpg123_handle_);
        off_t sample = static_cast<off_t>(pos_ms * target_sr_ / 1000);
        mpg123_seek(mh, sample, SEEK_SET);
        position_ms_.store(pos_ms);
        return true;
    }
#endif
#ifdef HAVE_AVFORMAT
    if (decoder_ == Decoder::AVCODEC && av_fmt_ctx_) {
        auto* ctx = static_cast<AVFormatContext*>(av_fmt_ctx_);
        int64_t ts = pos_ms * AV_TIME_BASE / 1000;
        av_seek_frame(ctx, -1, ts, AVSEEK_FLAG_BACKWARD);
        if (av_codec_ctx_) {
            avcodec_flush_buffers(static_cast<AVCodecContext*>(av_codec_ctx_));
        }
        position_ms_.store(pos_ms);
        return true;
    }
#endif
    return false;
}

// ---------------------------------------------------------------------------
// decode_loop — dispatcher
// ---------------------------------------------------------------------------
void FileSource::decode_loop()
{
    if (decoder_ == Decoder::MPG123)
        decode_loop_mpg123();
    else
        decode_loop_avcodec();

    running_.store(false);
    if (eof_cb_) eof_cb_();
}

// ---------------------------------------------------------------------------
// decode_loop_mpg123
// ---------------------------------------------------------------------------
void FileSource::decode_loop_mpg123()
{
#ifdef HAVE_MPG123
    auto* mh = static_cast<mpg123_handle*>(mpg123_handle_);
    if (!mh) return;

    // Buffer for decoded output: 4096 frames × channels × sizeof(float)
    const size_t BUF_FRAMES = 4096;
    size_t       buf_bytes  = BUF_FRAMES * static_cast<size_t>(target_ch_) * sizeof(float);
    std::vector<uint8_t> buf(buf_bytes);

    while (!stop_req_.load()) {
        size_t done = 0;
        int err = mpg123_read(mh, buf.data(), buf_bytes, &done);

        if (err == MPG123_DONE || err == MPG123_ERR) break;
        if (err != MPG123_OK && err != MPG123_NEW_FORMAT) continue;
        if (done == 0) continue;

        size_t frames = done / (static_cast<size_t>(target_ch_) * sizeof(float));
        if (callback_) {
            callback_(reinterpret_cast<const float*>(buf.data()),
                      frames, target_ch_, target_sr_);
        }

        // Update position
        off_t sample = mpg123_tell(mh);
        if (sample >= 0)
            position_ms_.store(static_cast<int64_t>(sample) * 1000 / target_sr_);
    }
#endif
}

// ---------------------------------------------------------------------------
// decode_loop_avcodec
// ---------------------------------------------------------------------------
void FileSource::decode_loop_avcodec()
{
#ifdef HAVE_AVFORMAT
    auto* fmt_ctx   = static_cast<AVFormatContext*>(av_fmt_ctx_);
    auto* codec_ctx = static_cast<AVCodecContext*>(av_codec_ctx_);
    auto* swr       = static_cast<SwrContext*>(swr_ctx_);

    if (!fmt_ctx || !codec_ctx) return;

    AVPacket* pkt  = av_packet_alloc();
    AVFrame*  frame = av_frame_alloc();
    if (!pkt || !frame) return;

    // Output float32 buffer
    const size_t OUT_FRAMES = 4096;
    std::vector<float> out_buf(OUT_FRAMES * static_cast<size_t>(target_ch_));

    while (!stop_req_.load()) {
        int ret = av_read_frame(fmt_ctx, pkt);
        if (ret < 0) break;  // EOF or error

        if (pkt->stream_index != av_stream_idx_) {
            av_packet_unref(pkt);
            continue;
        }

        ret = avcodec_send_packet(codec_ctx, pkt);
        av_packet_unref(pkt);
        if (ret < 0) continue;

        while (!stop_req_.load()) {
            ret = avcodec_receive_frame(codec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) goto done;

            if (swr) {
                // Resample to target sample rate + channel count + float32
                int out_count = static_cast<int>(
                    av_rescale_rnd(swr_get_delay(swr, codec_ctx->sample_rate) + frame->nb_samples,
                                   target_sr_, codec_ctx->sample_rate, AV_ROUND_UP));

                if (out_count > static_cast<int>(OUT_FRAMES)) {
                    out_buf.resize(static_cast<size_t>(out_count) * static_cast<size_t>(target_ch_));
                }

                uint8_t* out_ptr = reinterpret_cast<uint8_t*>(out_buf.data());
                ret = swr_convert(swr,
                                  &out_ptr, out_count,
                                  const_cast<const uint8_t**>(frame->data), frame->nb_samples);
                if (ret > 0 && callback_) {
                    callback_(out_buf.data(),
                              static_cast<size_t>(ret),
                              target_ch_, target_sr_);
                }
            } else {
                // Direct float32 — no resampling needed
                if (callback_) {
                    callback_(reinterpret_cast<const float*>(frame->data[0]),
                              static_cast<size_t>(frame->nb_samples),
                              target_ch_, target_sr_);
                }
            }

            // Update position
            if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                AVStream* st = fmt_ctx->streams[av_stream_idx_];
                double pts_sec = static_cast<double>(frame->best_effort_timestamp)
                               * av_q2d(st->time_base);
                position_ms_.store(static_cast<int64_t>(pts_sec * 1000.0));
            }

            av_frame_unref(frame);
        }
    }

done:
    av_packet_free(&pkt);
    av_frame_free(&frame);
#endif
}

// ---------------------------------------------------------------------------
// open_mpg123
// ---------------------------------------------------------------------------
bool FileSource::open_mpg123(const std::string& path)
{
#ifdef HAVE_MPG123
    int err = MPG123_OK;
    mpg123_handle* mh = mpg123_new(nullptr, &err);
    if (!mh) return false;

    // Force output to float32
    mpg123_format_none(mh);
    mpg123_format(mh, target_sr_, target_ch_,
                  MPG123_ENC_FLOAT_32);

    if (mpg123_open(mh, path.c_str()) != MPG123_OK) {
        mpg123_delete(mh);
        return false;
    }

    // Read format
    long rate; int ch, enc;
    mpg123_getformat(mh, &rate, &ch, &enc);
    track_info_.sample_rate = static_cast<int>(rate);
    track_info_.channels    = ch;

    // Duration
    off_t len = mpg123_length(mh);
    if (len > 0)
        track_info_.duration_ms = static_cast<int64_t>(len) * 1000 / target_sr_;

    mpg123_handle_ = mh;
    return true;
#else
    (void)path;
    return false;
#endif
}

// ---------------------------------------------------------------------------
// open_avcodec
// ---------------------------------------------------------------------------
bool FileSource::open_avcodec(const std::string& path)
{
#ifdef HAVE_AVFORMAT
    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, path.c_str(), nullptr, nullptr) < 0)
        return false;

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        avformat_close_input(&fmt_ctx);
        return false;
    }

    // Find best audio stream
    int stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (stream_idx < 0) {
        avformat_close_input(&fmt_ctx);
        return false;
    }

    AVStream*     st      = fmt_ctx->streams[stream_idx];
    const AVCodec* codec  = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&fmt_ctx);
        return false;
    }

    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, st->codecpar);
    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return false;
    }

    // Build SWR context for resampling to float32
    SwrContext* swr = nullptr;
    bool needs_swr = (codec_ctx->sample_rate != target_sr_ ||
                      codec_ctx->ch_layout.nb_channels != target_ch_ ||
                      codec_ctx->sample_fmt != AV_SAMPLE_FMT_FLT);

    if (needs_swr) {
        swr = swr_alloc();
        AVChannelLayout out_layout;
        if (target_ch_ == 1) {
            AVChannelLayout mono = AV_CHANNEL_LAYOUT_MONO;
            out_layout = mono;
        } else {
            AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
            out_layout = stereo;
        }
        av_opt_set_chlayout (swr, "in_chlayout",    &codec_ctx->ch_layout, 0);
        av_opt_set_int      (swr, "in_sample_rate",   codec_ctx->sample_rate, 0);
        av_opt_set_sample_fmt(swr, "in_sample_fmt",  codec_ctx->sample_fmt, 0);
        av_opt_set_chlayout (swr, "out_chlayout",   &out_layout, 0);
        av_opt_set_int      (swr, "out_sample_rate",  target_sr_, 0);
        av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
        if (swr_init(swr) < 0) {
            swr_free(&swr);
            swr = nullptr;
        }
    }

    // Track info
    track_info_.sample_rate  = codec_ctx->sample_rate;
    track_info_.channels     = codec_ctx->ch_layout.nb_channels;
    track_info_.bitrate_kbps = static_cast<int>(fmt_ctx->bit_rate / 1000);
    if (fmt_ctx->duration != AV_NOPTS_VALUE)
        track_info_.duration_ms = fmt_ctx->duration / (AV_TIME_BASE / 1000);

    // Metadata
    AVDictionary* meta = fmt_ctx->metadata;
    auto get_tag = [meta](const char* key) -> std::string {
        AVDictionaryEntry* e = av_dict_get(meta, key, nullptr, AV_DICT_IGNORE_SUFFIX);
        return e ? e->value : "";
    };
    track_info_.title  = get_tag("title");
    track_info_.artist = get_tag("artist");
    track_info_.album  = get_tag("album");
    track_info_.genre  = get_tag("genre");
    try { track_info_.year = std::stoi(get_tag("date")); } catch (...) {}

    if (track_info_.title.empty())
        track_info_.title = std::filesystem::path(path).stem().string();

    av_fmt_ctx_    = fmt_ctx;
    av_codec_ctx_  = codec_ctx;
    swr_ctx_       = swr;
    av_stream_idx_ = stream_idx;
    return true;
#else
    (void)path;
    return false;
#endif
}

// ---------------------------------------------------------------------------
// close helpers
// ---------------------------------------------------------------------------
void FileSource::close_mpg123()
{
#ifdef HAVE_MPG123
    if (mpg123_handle_) {
        mpg123_close(static_cast<mpg123_handle*>(mpg123_handle_));
        mpg123_delete(static_cast<mpg123_handle*>(mpg123_handle_));
        mpg123_handle_ = nullptr;
    }
    decoder_ = Decoder::NONE;
#endif
}

void FileSource::close_avcodec()
{
#ifdef HAVE_AVFORMAT
    if (swr_ctx_) {
        swr_free(reinterpret_cast<SwrContext**>(&swr_ctx_));
    }
    if (av_codec_ctx_) {
        avcodec_free_context(reinterpret_cast<AVCodecContext**>(&av_codec_ctx_));
    }
    if (av_fmt_ctx_) {
        avformat_close_input(reinterpret_cast<AVFormatContext**>(&av_fmt_ctx_));
    }
    av_stream_idx_ = -1;
    decoder_ = Decoder::NONE;
#endif
}
