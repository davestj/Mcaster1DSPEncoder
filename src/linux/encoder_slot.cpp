// encoder_slot.cpp — Per-encoder-slot state machine
// Phase L5.1 — Mcaster1DSPEncoder Linux v1.4.1
#include "encoder_slot.h"
#include "file_source.h"
#include "audio_source.h"
#include "mc1_logger.h"
#include "mc1_db.h"
#include "server_monitors.h"

#include <cstring>
#include <cstdio>
#include <ctime>
#include <algorithm>
#include <random>
#include <stdexcept>
#include <chrono>

// Codec headers — guarded so encoder_slot compiles in HTTP-test mode too
#ifdef HAVE_LAME
#include <lame/lame.h>
#endif
#ifdef HAVE_VORBIS
#include <vorbis/vorbisenc.h>
#endif
#ifdef HAVE_OPUS
#include <opusenc.h>
#endif
#ifdef HAVE_FLAC
#include <FLAC/stream_encoder.h>
#endif
#ifdef HAVE_FDK_AAC
#include <fdk-aac/aacenc_lib.h>
#endif
#ifdef HAVE_PORTAUDIO
#include <portaudio.h>
#endif

// ---------------------------------------------------------------------------
// Codec output-buffer state helpers (anonymous namespace — file-scope only)
// ---------------------------------------------------------------------------
namespace {

#ifdef HAVE_OPUS
struct OpusState {
    OggOpusEnc*          enc = nullptr;
    std::vector<uint8_t> pending;
};

int opus_write_cb(void* user, const unsigned char* ptr, opus_int32 len)
{
    auto* s = static_cast<OpusState*>(user);
    s->pending.insert(s->pending.end(), ptr, ptr + len);
    return 0;
}

int opus_close_cb(void* /*user*/) { return 0; }
#endif // HAVE_OPUS

#ifdef HAVE_FLAC
struct FlacState {
    FLAC__StreamEncoder* enc = nullptr;
    std::vector<uint8_t> pending;
};

FLAC__StreamEncoderWriteStatus flac_write_cb(
    const FLAC__StreamEncoder* /*enc*/,
    const FLAC__byte buf[], size_t bytes,
    uint32_t /*samples*/, uint32_t /*current_frame*/,
    void* client_data)
{
    auto* s = static_cast<FlacState*>(client_data);
    s->pending.insert(s->pending.end(), buf, buf + bytes);
    return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}
#endif // HAVE_FLAC

#ifdef HAVE_FDK_AAC
// AacState — fdk-aac per-slot encoder state
// PCM accumulation: fdk-aac requires exactly frame_size*channels INT_PCM per encode call.
// Samples arriving from the audio callback are buffered here until a complete frame
// is available, then flushed through aacEncEncode() into out_buf as ADTS packets.
struct AacState {
    HANDLE_AACENCODER     enc        = nullptr;
    int                   frame_size = 1024;  // samples per channel per AAC frame
    int                   channels   = 2;
    std::vector<INT_PCM>  pcm_buf;            // float→INT_PCM accumulation buffer
    size_t                pcm_head   = 0;     // read offset (avoid front-erase O(n))
    std::vector<uint8_t>  out_buf;            // encoded ADTS output accumulation
};
#endif // HAVE_FDK_AAC

} // namespace

// ---------------------------------------------------------------------------
// EncoderSlot — constructor / destructor
// ---------------------------------------------------------------------------
EncoderSlot::EncoderSlot(const EncoderConfig& cfg)
    : cfg_(cfg)
{
}

EncoderSlot::~EncoderSlot()
{
    stop();
    close_codec();
}

// ---------------------------------------------------------------------------
// start
// ---------------------------------------------------------------------------
bool EncoderSlot::start()
{
    // ------------------------------------------------------------------
    // Hold mtx_ only for initialization.  Release BEFORE the blocking
    // TCP connect to DNAS so that stats() / HTTP handlers are never
    // starved while we wait for the DNAS handshake.
    // is_playlist_source is hoisted out so the post-lock section can use it.
    // ------------------------------------------------------------------
    bool is_playlist_source = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);

        if (state_.load() != State::IDLE && state_.load() != State::ERROR)
            return false;

        set_state(State::STARTING);
        last_error_.clear();

        // --- Init codec ---
        bool codec_ok = false;
        switch (cfg_.codec) {
            case EncoderConfig::Codec::MP3:      codec_ok = init_lame();   break;
            case EncoderConfig::Codec::VORBIS:   codec_ok = init_vorbis(); break;
            case EncoderConfig::Codec::OPUS:     codec_ok = init_opus();   break;
            case EncoderConfig::Codec::FLAC:     codec_ok = init_flac();   break;
            case EncoderConfig::Codec::AAC_LC:
            case EncoderConfig::Codec::AAC_HE:
            case EncoderConfig::Codec::AAC_HE_V2:
            case EncoderConfig::Codec::AAC_ELD:  codec_ok = init_aac();   break;
        }
        if (!codec_ok) {
            // set_error() also acquires mtx_ — write fields directly here
            last_error_ = "Codec init failed";
            state_.store(State::ERROR);
            fprintf(stderr, "[EncoderSlot %d] Error: Codec init failed\n", cfg_.slot_id);
            return false;
        }

        // --- Init stream client ---
        stopping_.store(false);
        ++meta_gen_;               // invalidate any sleeping push_metadata threads from before this start()
        first_connect_done_.store(false);   // reset so the first CONNECTED fires no re-push

        // We build the stream target with reconnect policy from our config before handing
        // it to StreamClient — this ensures the watchdog loop uses our DB settings.
        StreamTarget st = cfg_.stream_target;
        st.retry_interval_sec = cfg_.reconnect_interval_sec;
        if (!cfg_.auto_reconnect) {
            st.max_retries = 0;    // 0 = never reconnect after first drop
        } else if (cfg_.reconnect_max_attempts > 0) {
            st.max_retries = cfg_.reconnect_max_attempts;
        } else {
            st.max_retries = -1;   // -1 = retry forever
        }

        MC1_INFO("[Slot " + std::to_string(cfg_.slot_id) + "] start(): reconnect="
                 + (cfg_.auto_reconnect ? "yes" : "no")
                 + " interval=" + std::to_string(cfg_.reconnect_interval_sec) + "s"
                 + " max=" + std::to_string(cfg_.reconnect_max_attempts));

        stream_client_ = std::make_shared<StreamClient>(st);
        stream_client_->set_state_callback([this](StreamClient::State cs) {
            if (cs == StreamClient::State::CONNECTED) {
                set_state(State::LIVE);
                // On watchdog RE-connect only: re-push the current song title so the DNAS
                // webplayer is never blank while the same track is still playing.
                // first_connect_done_ is false on the very first CONNECTED event — we skip
                // the re-push there because open_next_track()'s push_metadata() already
                // queued a send.  On all subsequent reconnects the flag is true, so we send.
                const bool is_reconnect = first_connect_done_.exchange(true);
                if (is_reconnect) {
                    std::string ttl, art;
                    {
                        std::lock_guard<std::mutex> lk(mtx_);
                        ttl = current_title_;
                        art = current_artist_;
                    }
                    if (!ttl.empty()) {
                        auto sc = stream_client_;  // shared_ptr by value — keeps object alive
                        std::thread([sc, ttl, art]() {
                            std::this_thread::sleep_for(std::chrono::milliseconds(500));
                            if (sc) sc->send_admin_metadata(ttl, art);
                        }).detach();
                    }
                }
            }
            if (cs == StreamClient::State::RECONNECTING) {
                set_state(State::RECONNECTING);
            }
            // STOPPED fires when max_retries is exhausted (not when we call stop() ourselves).
            // We distinguish this from a voluntary stop by checking stopping_.
            if (cs == StreamClient::State::STOPPED && !stopping_.load()) {
                MC1_WARN("[Slot " + std::to_string(cfg_.slot_id)
                         + "] SLEEP — max reconnect attempts exhausted");
                mc1log.encoder(cfg_.slot_id, "SLEEP",
                               "max reconnect attempts (" +
                               std::to_string(cfg_.reconnect_max_attempts) + ") exhausted");
                set_state(State::SLEEP);
            }
        });

        // --- Init DSP chain ---
        {
            mc1dsp::DspChainConfig dsp_cfg;
            dsp_cfg.sample_rate        = cfg_.sample_rate;
            dsp_cfg.channels           = cfg_.channels;
            dsp_cfg.eq_enabled         = cfg_.dsp_eq_enabled;
            dsp_cfg.agc_enabled        = cfg_.dsp_agc_enabled;
            dsp_cfg.crossfader_enabled = cfg_.dsp_crossfade_enabled;
            dsp_cfg.crossfade_duration = cfg_.dsp_crossfade_duration;
            dsp_cfg.eq_preset          = cfg_.dsp_eq_preset;
            dsp_chain_ = std::make_unique<mc1dsp::DspChain>();
            dsp_chain_->configure(dsp_cfg);
            fprintf(stderr, "[EncoderSlot %d] DSP: EQ=%s AGC=%s XFade=%s (%.1fs) preset='%s'\n",
                    cfg_.slot_id,
                    cfg_.dsp_eq_enabled  ? "on" : "off",
                    cfg_.dsp_agc_enabled ? "on" : "off",
                    cfg_.dsp_crossfade_enabled ? "on" : "off",
                    cfg_.dsp_crossfade_duration,
                    cfg_.dsp_eq_preset.empty() ? "none" : cfg_.dsp_eq_preset.c_str());
        }

        // --- Init archive ---
        if (cfg_.archive_enabled && !cfg_.archive_dir.empty()) {
            ArchiveWriter::Config ac;
            ac.archive_dir  = cfg_.archive_dir;
            ac.station_name = cfg_.name;
            archive_ = std::make_unique<ArchiveWriter>(ac);
            archive_->open(cfg_.sample_rate, cfg_.channels);
        }

        // --- Create audio source object (playlist load happens AFTER lock release) ---
        if (cfg_.input_type == EncoderConfig::InputType::DEVICE) {
            auto* pa = new PortAudioSource(cfg_.device_index,
                                           cfg_.sample_rate,
                                           cfg_.channels);
            source_.reset(pa);
        } else {
            auto* fs = new FileSource(cfg_.sample_rate, cfg_.channels);
            source_.reset(fs);
            is_playlist_source = true;
        }

        set_state(State::CONNECTING);
    } // ← mtx_ released here — open_next_track / connect can run without holding mtx_

    // --- Load playlist + first track (OUTSIDE lock — open_next_track acquires mtx_ internally) ---
    if (is_playlist_source) {
        if (!cfg_.playlist_path.empty()) {
            auto entries = PlaylistParser::parse(cfg_.playlist_path);
            {
                std::lock_guard<std::mutex> plk(playlist_mtx_);
                playlist_ = std::move(entries);
                playlist_pos_ = 0;
                if (cfg_.shuffle) shuffle_playlist();
            } // ← playlist_mtx_ released before open_next_track
        }
        if (!open_next_track()) {
            set_error("No tracks in playlist");
            source_.reset();
            stream_client_.reset();
            close_codec();
            return false;
        }
    }

    // --- Audio callback (no lock needed — captures this only) ---
    auto callback = [this](const float* pcm, size_t frames, int ch, int sr) {
        on_audio(pcm, frames, ch, sr);
    };

    // --- Connect to DNAS (blocking TCP — intentionally no lock held) ---
    stream_client_->connect();

    // --- Start audio capture / decode ---
    start_time_ = static_cast<uint64_t>(std::time(nullptr));
    if (!source_->start(callback)) {
        set_error("AudioSource::start failed");
        stream_client_->disconnect();
        source_.reset();
        close_codec();
        return false;
    }

    fprintf(stderr, "[EncoderSlot %d] Started\n", cfg_.slot_id);
    return true;
}

// ---------------------------------------------------------------------------
// stop
// ---------------------------------------------------------------------------
void EncoderSlot::stop()
{
    stopping_.store(true);   // signal detached threads (push_metadata, advance_playlist)
    set_state(State::STOPPING);

    if (source_)        { source_->stop(); source_.reset(); }
    if (stream_client_) { stream_client_->disconnect(); stream_client_.reset(); }
    if (archive_)       { archive_->close(); archive_.reset(); }
    if (dsp_chain_)     { dsp_chain_.reset(); }

    close_codec();
    set_state(State::IDLE);
    fprintf(stderr, "[EncoderSlot %d] Stopped\n", cfg_.slot_id);
}

// ---------------------------------------------------------------------------
// restart
// ---------------------------------------------------------------------------
void EncoderSlot::restart()
{
    stop();
    start();
}

// ---------------------------------------------------------------------------
// wake — recover from SLEEP state (max reconnect attempts exhausted).
// We reset to IDLE and restart encoding from scratch.
// ---------------------------------------------------------------------------
void EncoderSlot::wake()
{
    if (state_.load() != State::SLEEP) return;
    MC1_INFO("[Slot " + std::to_string(cfg_.slot_id) + "] wake() requested — resetting from SLEEP to IDLE");
    mc1log.encoder(cfg_.slot_id, "WAKE", "manual wake from SLEEP state");
    set_state(State::IDLE);
    start();
}

// ---------------------------------------------------------------------------
// load_playlist
// ---------------------------------------------------------------------------
bool EncoderSlot::load_playlist(const std::string& path)
{
    auto entries = PlaylistParser::parse(path);
    if (entries.empty()) return false;

    std::lock_guard<std::mutex> lk(playlist_mtx_);
    playlist_     = std::move(entries);
    playlist_pos_ = 0;
    if (cfg_.shuffle) shuffle_playlist();
    return true;
}

// ---------------------------------------------------------------------------
// skip_track
// ---------------------------------------------------------------------------
void EncoderSlot::skip_track()
{
    advance_requested_ = true;
}

// ---------------------------------------------------------------------------
// set_volume
// ---------------------------------------------------------------------------
void EncoderSlot::set_volume(float v)
{
    if (v < 0.0f) v = 0.0f;
    if (v > 2.0f) v = 2.0f;
    std::lock_guard<std::mutex> lk(mtx_);
    cfg_.volume = v;
}

// ---------------------------------------------------------------------------
// push_metadata — update internal state immediately, send ICY admin update
// non-blocking. When crossfade is enabled the HTTP GET is delayed so that
// the title on the DNAS/webplayer updates only after the fade-in completes.
// ---------------------------------------------------------------------------
void EncoderSlot::push_metadata(const std::string& title,
                                 const std::string& artist,
                                 const std::string& album,
                                 const std::string& artwork)
{
    {
        std::lock_guard<std::mutex> lk(mtx_);
        current_title_  = title;
        current_artist_ = artist;
    }

    if (!stream_client_) return;

    // Determine delay: when crossfade is on, wait for the fade-in to complete
    // before showing the new title to listeners on the DNAS webplayer.
    int delay_ms = 0;
    if (cfg_.dsp_crossfade_enabled && cfg_.dsp_crossfade_duration > 0.0f)
        delay_ms = static_cast<int>(cfg_.dsp_crossfade_duration * 1000.0f);

    // Fire metadata update in detached thread — never blocks advance_playlist().
    // Capture shared_ptr by value so the StreamClient stays alive for the full
    // crossfade delay even if stop() runs and clears stream_client_ mid-sleep.
    // meta_gen_ guards against stale threads that survived a stop()+start() cycle:
    // start() increments meta_gen_, so any thread carrying the old generation bails.
    auto sc  = stream_client_;      // shared_ptr copy — ref-count keeps object alive
    auto gen = meta_gen_.load();    // generation at the time this push was requested
    std::thread([this, sc, title, artist, album, artwork, delay_ms, gen]() {
        if (delay_ms > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        if (sc && !stopping_.load() && meta_gen_.load() == gen)
            sc->send_admin_metadata(title, artist, album, artwork);
    }).detach();
}

// ---------------------------------------------------------------------------
// stats
// ---------------------------------------------------------------------------
EncoderSlot::Stats EncoderSlot::stats() const
{
    Stats s;
    s.slot_id   = cfg_.slot_id;
    s.state     = state_.load();
    s.state_str = state_to_string(s.state);
    s.is_live   = (s.state == State::LIVE);
    s.volume    = cfg_.volume;

    {
        std::lock_guard<std::mutex> lk(mtx_);
        s.last_error    = last_error_;
        s.current_title = current_title_;
        s.current_artist= current_artist_;
        s.duration_ms   = current_duration_ms_;
    }

    if (stream_client_) {
        s.bytes_sent = stream_client_->bytes_sent();
        uint64_t now = static_cast<uint64_t>(std::time(nullptr));
        s.uptime_sec = stream_client_->is_connected() && stream_client_->connect_time() > 0
                       ? now - stream_client_->connect_time()
                       : 0;
    }

    if (source_) {
        auto* fs = dynamic_cast<FileSource*>(source_.get());
        if (fs) s.position_ms = fs->position_ms();
    }

    {
        std::lock_guard<std::mutex> plk(playlist_mtx_);
        s.track_index = playlist_pos_;
        s.track_count = static_cast<int>(playlist_.size());
    }

    return s;
}

// ---------------------------------------------------------------------------
// update_config
// ---------------------------------------------------------------------------
void EncoderSlot::update_config(const EncoderConfig& cfg)
{
    std::lock_guard<std::mutex> lk(mtx_);
    cfg_ = cfg;
}

// ---------------------------------------------------------------------------
// reconfigure_dsp — live-update DSP chain parameters (takes effect immediately)
// ---------------------------------------------------------------------------
void EncoderSlot::reconfigure_dsp(const mc1dsp::DspChainConfig& dsp_cfg)
{
    if (dsp_chain_) dsp_chain_->configure(dsp_cfg);
    std::lock_guard<std::mutex> lk(mtx_);
    cfg_.dsp_eq_enabled         = dsp_cfg.eq_enabled;
    cfg_.dsp_agc_enabled        = dsp_cfg.agc_enabled;
    cfg_.dsp_crossfade_enabled  = dsp_cfg.crossfader_enabled;
    cfg_.dsp_crossfade_duration = dsp_cfg.crossfade_duration;
    cfg_.dsp_eq_preset          = dsp_cfg.eq_preset;
}

// ---------------------------------------------------------------------------
// state_to_string
// ---------------------------------------------------------------------------
std::string EncoderSlot::state_to_string(State s)
{
    switch (s) {
        case State::IDLE:        return "idle";
        case State::STARTING:    return "starting";
        case State::CONNECTING:  return "connecting";
        case State::LIVE:        return "live";
        case State::RECONNECTING:return "reconnecting";
        case State::SLEEP:       return "sleep";
        case State::ERROR:       return "error";
        case State::STOPPING:    return "stopping";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// on_audio — called from audio thread for each PCM buffer
// ---------------------------------------------------------------------------
void EncoderSlot::on_audio(const float* pcm, size_t frames, int /*ch*/, int /*sr*/)
{
    if (advance_requested_) {
        advance_requested_ = false;
        if (cfg_.input_type == EncoderConfig::InputType::PLAYLIST) {
            // Dispatch to a detached thread: advance_playlist() calls fs->stop()
            // which would deadlock if called from within the decode thread itself.
            std::thread([this]() { advance_playlist(); }).detach();
        }
    }

    if (!stream_client_ || !stream_client_->is_connected()) return;

    // Apply volume + DSP chain (EQ → AGC); allocate mutable copy if needed
    float vol = cfg_.volume;
    const bool need_buf = (vol != 1.0f) || (dsp_chain_ != nullptr);
    std::vector<float> buf;
    if (need_buf) {
        size_t n = frames * static_cast<size_t>(cfg_.channels);
        buf.resize(n);
        for (size_t i = 0; i < n; ++i) buf[i] = pcm[i] * vol;
        if (dsp_chain_) dsp_chain_->process(buf.data(), frames);
        pcm = buf.data();
    }

    // Archive PCM
    if (archive_ && archive_->is_open()) {
        archive_->write_pcm(pcm, frames);
    }

    // Encode and stream
    const size_t MP3_BUF  = frames * 5 / 4 + 7200;
    const size_t OGG_BUF  = 65536;
    std::vector<uint8_t> encoded_buf(std::max(MP3_BUF, OGG_BUF));

    int encoded_bytes = 0;

    switch (cfg_.codec) {
        case EncoderConfig::Codec::MP3:
            encoded_bytes = encode_mp3(pcm, frames, encoded_buf.data(), encoded_buf.size());
            break;
        case EncoderConfig::Codec::VORBIS:
            encoded_bytes = encode_vorbis(pcm, frames, encoded_buf.data(), encoded_buf.size());
            break;
        case EncoderConfig::Codec::OPUS:
            encoded_bytes = encode_opus(pcm, frames, encoded_buf.data(), encoded_buf.size());
            break;
        case EncoderConfig::Codec::FLAC:
            encoded_bytes = encode_flac(pcm, frames, encoded_buf.data(), encoded_buf.size());
            break;
        case EncoderConfig::Codec::AAC_LC:
        case EncoderConfig::Codec::AAC_HE:
        case EncoderConfig::Codec::AAC_HE_V2:
        case EncoderConfig::Codec::AAC_ELD:
            encoded_bytes = encode_aac(pcm, frames, encoded_buf.data(), encoded_buf.size());
            break;
    }

    if (encoded_bytes > 0) {
        stream_client_->write(encoded_buf.data(), static_cast<size_t>(encoded_bytes));
        bytes_encoded_ += static_cast<uint64_t>(encoded_bytes);
    }
}

// ---------------------------------------------------------------------------
// Codec initializers
// ---------------------------------------------------------------------------
bool EncoderSlot::init_lame()
{
#ifdef HAVE_LAME
    lame_global_flags* gfp = lame_init();
    if (!gfp) return false;

    lame_set_in_samplerate(gfp, cfg_.sample_rate);
    lame_set_num_channels(gfp, cfg_.channels);

    // We apply encode mode: CBR uses lame_set_brate; VBR uses quality knob;
    // ABR uses mean bitrate target.  Quality (0-10) is remapped: lower DB value
    // = better quality, which maps to lower LAME algorithm quality (0 = best).
    int lame_quality = 9 - std::min(9, std::max(0, cfg_.quality));  // 0(best)–9(fastest)
    switch (cfg_.encode_mode) {
        case EncoderConfig::EncodeMode::VBR:
            lame_set_VBR(gfp, vbr_default);
            lame_set_VBR_q(gfp, lame_quality);
            break;
        case EncoderConfig::EncodeMode::ABR:
            lame_set_VBR(gfp, vbr_abr);
            lame_set_VBR_mean_bitrate_kbps(gfp, cfg_.bitrate_kbps);
            lame_set_quality(gfp, lame_quality);
            break;
        case EncoderConfig::EncodeMode::CBR:
        default:
            lame_set_brate(gfp, cfg_.bitrate_kbps);
            lame_set_quality(gfp, lame_quality);
            break;
    }

    // We apply channel mode: JOINT_STEREO is recommended for most music;
    // STEREO uses independent L/R coding; MONO downmixes to one channel.
    MPEG_mode mode = JOINT_STEREO;
    if (cfg_.channels == 1 || cfg_.channel_mode == EncoderConfig::ChannelMode::MONO) {
        mode = MONO;
    } else if (cfg_.channel_mode == EncoderConfig::ChannelMode::STEREO) {
        mode = STEREO;
    }
    lame_set_mode(gfp, mode);

    if (lame_init_params(gfp) < 0) {
        lame_close(gfp);
        return false;
    }
    lame_enc_ = gfp;

    cfg_.stream_target.content_type = "audio/mpeg";
    MC1_INFO("[Slot " + std::to_string(cfg_.slot_id) + "] LAME init: "
             + (cfg_.encode_mode == EncoderConfig::EncodeMode::VBR ? "VBR" :
                cfg_.encode_mode == EncoderConfig::EncodeMode::ABR ? "ABR" : "CBR")
             + " " + std::to_string(cfg_.bitrate_kbps) + "kbps quality=" + std::to_string(cfg_.quality));
    return true;
#else
    fprintf(stderr, "[EncoderSlot] LAME not compiled in\n");
    return false;
#endif
}

bool EncoderSlot::init_vorbis()
{
    // Vorbis state is managed via the vorbisenc API
    // Requires two globals: vorbis_info + vorbis_dsp_state + vorbis_block
    // We store them in a small heap struct
#ifdef HAVE_VORBIS
    struct VorbisState {
        vorbis_info     vi;
        vorbis_dsp_state vd;
        vorbis_block    vb;
        vorbis_comment  vc;
    };

    auto* vs = new VorbisState();
    vorbis_info_init(&vs->vi);

    int ret = vorbis_encode_init_vbr(&vs->vi, cfg_.channels, cfg_.sample_rate,
                                      static_cast<float>(cfg_.quality) / 10.0f);
    if (ret != 0) {
        vorbis_info_clear(&vs->vi);
        delete vs;
        return false;
    }

    vorbis_comment_init(&vs->vc);
    vorbis_comment_add_tag(&vs->vc, "ENCODER", "Mcaster1DSPEncoder");
    vorbis_analysis_init(&vs->vd, &vs->vi);
    vorbis_block_init(&vs->vd, &vs->vb);

    vorbis_enc_  = vs;
    cfg_.stream_target.content_type = "application/ogg";
    return true;
#else
    fprintf(stderr, "[EncoderSlot] Vorbis not compiled in\n");
    return false;
#endif
}

bool EncoderSlot::init_opus()
{
#ifdef HAVE_OPUS
    auto* state = new OpusState();

    OpusEncCallbacks cbs{};
    cbs.write = opus_write_cb;
    cbs.close = opus_close_cb;

    OggOpusComments* comments = ope_comments_create();
    ope_comments_add(comments, "ENCODER", "Mcaster1DSPEncoder");

    int error = OPE_OK;
    state->enc = ope_encoder_create_callbacks(
        &cbs, state, comments,
        cfg_.sample_rate, cfg_.channels,
        0 /* Vorbis I channel family */, &error);

    ope_comments_destroy(comments);

    if (!state->enc || error != OPE_OK) {
        delete state;
        fprintf(stderr, "[EncoderSlot] Opus init error: %s\n", ope_strerror(error));
        return false;
    }

    ope_encoder_ctl(state->enc, OPUS_SET_BITRATE(cfg_.bitrate_kbps * 1000));

    opus_enc_ = state;
    cfg_.stream_target.content_type = "audio/ogg";
    return true;
#else
    fprintf(stderr, "[EncoderSlot] Opus not compiled in\n");
    return false;
#endif
}

bool EncoderSlot::init_flac()
{
#ifdef HAVE_FLAC
    auto* state = new FlacState();
    state->enc = FLAC__stream_encoder_new();
    if (!state->enc) { delete state; return false; }

    // We use cfg_.quality (0-10) clamped to FLAC compression range 0-8.
    // Level 0 = fastest/largest, level 8 = best compression/slowest.
    int flac_compression = std::min(8, std::max(0, cfg_.quality));
    FLAC__stream_encoder_set_channels        (state->enc, static_cast<unsigned>(cfg_.channels));
    FLAC__stream_encoder_set_bits_per_sample (state->enc, 16);
    FLAC__stream_encoder_set_sample_rate     (state->enc, static_cast<unsigned>(cfg_.sample_rate));
    FLAC__stream_encoder_set_compression_level(state->enc, static_cast<unsigned>(flac_compression));
    FLAC__stream_encoder_set_streamable_subset(state->enc, true);

    FLAC__StreamEncoderInitStatus st =
        FLAC__stream_encoder_init_stream(state->enc, flac_write_cb,
                                         nullptr, nullptr, nullptr, state);
    if (st != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
        FLAC__stream_encoder_delete(state->enc);
        delete state;
        fprintf(stderr, "[EncoderSlot] FLAC stream init error: %d\n", static_cast<int>(st));
        return false;
    }

    flac_enc_ = state;
    cfg_.stream_target.content_type = "audio/flac";
    return true;
#else
    fprintf(stderr, "[EncoderSlot] FLAC not compiled in\n");
    return false;
#endif
}

void EncoderSlot::close_codec()
{
#ifdef HAVE_LAME
    if (lame_enc_) {
        lame_close(static_cast<lame_global_flags*>(lame_enc_));
        lame_enc_ = nullptr;
    }
#endif
#ifdef HAVE_VORBIS
    if (vorbis_enc_) {
        struct VorbisState {
            vorbis_info      vi;
            vorbis_dsp_state vd;
            vorbis_block     vb;
            vorbis_comment   vc;
        };
        auto* vs = static_cast<VorbisState*>(vorbis_enc_);
        vorbis_block_clear(&vs->vb);
        vorbis_dsp_clear(&vs->vd);
        vorbis_comment_clear(&vs->vc);
        vorbis_info_clear(&vs->vi);
        delete vs;
        vorbis_enc_ = nullptr;
    }
#endif
#ifdef HAVE_OPUS
    if (opus_enc_) {
        auto* state = static_cast<OpusState*>(opus_enc_);
        ope_encoder_drain(state->enc);
        ope_encoder_destroy(state->enc);
        delete state;
        opus_enc_ = nullptr;
    }
#endif
#ifdef HAVE_FLAC
    if (flac_enc_) {
        auto* state = static_cast<FlacState*>(flac_enc_);
        FLAC__stream_encoder_finish(state->enc);
        FLAC__stream_encoder_delete(state->enc);
        delete state;
        flac_enc_ = nullptr;
    }
#endif
#ifdef HAVE_FDK_AAC
    if (aac_enc_) {
        auto* state = static_cast<AacState*>(aac_enc_);
        if (state->enc) aacEncClose(&state->enc);
        delete state;
        aac_enc_ = nullptr;
    }
#endif
}

// ---------------------------------------------------------------------------
// Codec frame encoders
// ---------------------------------------------------------------------------
int EncoderSlot::encode_mp3(const float* pcm, size_t frames,
                             uint8_t* out_buf, size_t buf_len)
{
#ifdef HAVE_LAME
    if (!lame_enc_) return 0;
    auto* gfp = static_cast<lame_global_flags*>(lame_enc_);

    // Deinterleave
    std::vector<float> L(frames), R(frames);
    for (size_t i = 0; i < frames; ++i) {
        L[i] = pcm[i * cfg_.channels];
        R[i] = cfg_.channels > 1 ? pcm[i * cfg_.channels + 1] : pcm[i * cfg_.channels];
    }

    int n = lame_encode_buffer_ieee_float(
        gfp,
        L.data(), R.data(),
        static_cast<int>(frames),
        out_buf,
        static_cast<int>(buf_len));

    return n > 0 ? n : 0;
#else
    (void)pcm; (void)frames; (void)out_buf; (void)buf_len;
    return 0;
#endif
}

int EncoderSlot::encode_vorbis(const float* pcm, size_t frames,
                                uint8_t* out_buf, size_t buf_len)
{
#ifdef HAVE_VORBIS
    if (!vorbis_enc_) return 0;
    struct VorbisState {
        vorbis_info     vi;
        vorbis_dsp_state vd;
        vorbis_block    vb;
        vorbis_comment  vc;
    };
    auto* vs = static_cast<VorbisState*>(vorbis_enc_);

    // Feed samples
    float** buf = vorbis_analysis_buffer(&vs->vd, static_cast<int>(frames));
    for (size_t i = 0; i < frames; ++i) {
        buf[0][i] = pcm[i * cfg_.channels];
        if (cfg_.channels > 1)
            buf[1][i] = pcm[i * cfg_.channels + 1];
    }
    vorbis_analysis_wrote(&vs->vd, static_cast<int>(frames));

    // Collect output pages
    size_t total = 0;
    ogg_packet op;
    ogg_page og;
    // Note: full Ogg muxing requires an ogg_stream_state
    // This is a simplified version — full implementation would use ogg_stream_*
    // For now just encode and write raw Vorbis packets
    while (vorbis_analysis_blockout(&vs->vd, &vs->vb) == 1) {
        vorbis_analysis(&vs->vb, nullptr);
        vorbis_bitrate_addblock(&vs->vb);
        while (vorbis_bitrate_flushpacket(&vs->vd, &op)) {
            size_t plen = static_cast<size_t>(op.bytes);
            if (total + plen < buf_len) {
                memcpy(out_buf + total, op.packet, plen);
                total += plen;
            }
        }
    }
    return static_cast<int>(total);
#else
    (void)pcm; (void)frames; (void)out_buf; (void)buf_len;
    return 0;
#endif
}

int EncoderSlot::encode_opus(const float* pcm, size_t frames,
                              uint8_t* out_buf, size_t buf_len)
{
#ifdef HAVE_OPUS
    if (!opus_enc_) return 0;
    auto* state = static_cast<OpusState*>(opus_enc_);

    state->pending.clear();

    // ope_encoder_write_float: pcm is interleaved, frames = samples per channel
    int ret = ope_encoder_write_float(state->enc, pcm, static_cast<int>(frames));
    if (ret != OPE_OK) {
        fprintf(stderr, "[EncoderSlot] Opus encode error: %s\n", ope_strerror(ret));
        return 0;
    }

    if (state->pending.empty()) return 0;

    size_t copy = std::min(state->pending.size(), buf_len);
    memcpy(out_buf, state->pending.data(), copy);
    // Keep any overflow for next call (rare with typical buf sizes)
    if (copy < state->pending.size())
        state->pending.erase(state->pending.begin(),
                             state->pending.begin() + static_cast<ptrdiff_t>(copy));
    else
        state->pending.clear();

    return static_cast<int>(copy);
#else
    (void)pcm; (void)frames; (void)out_buf; (void)buf_len;
    return 0;
#endif
}

int EncoderSlot::encode_flac(const float* pcm, size_t frames,
                              uint8_t* out_buf, size_t buf_len)
{
#ifdef HAVE_FLAC
    if (!flac_enc_) return 0;
    auto* state = static_cast<FlacState*>(flac_enc_);

    state->pending.clear();

    // Convert interleaved float → FLAC__int32 (16-bit range)
    std::vector<FLAC__int32> samples(frames * static_cast<size_t>(cfg_.channels));
    for (size_t i = 0; i < samples.size(); ++i) {
        float s = pcm[i];
        if (s >  1.0f) s =  1.0f;
        if (s < -1.0f) s = -1.0f;
        samples[i] = static_cast<FLAC__int32>(s * 32767.0f);
    }

    // process_interleaved accepts interleaved samples, frames = samples per channel
    FLAC__bool ok = FLAC__stream_encoder_process_interleaved(
        state->enc, samples.data(), static_cast<uint32_t>(frames));

    if (!ok || state->pending.empty()) return 0;

    size_t copy = std::min(state->pending.size(), buf_len);
    memcpy(out_buf, state->pending.data(), copy);
    return static_cast<int>(copy);
#else
    (void)pcm; (void)frames; (void)out_buf; (void)buf_len;
    return 0;
#endif
}

// ---------------------------------------------------------------------------
// AAC codec (libfdk-aac) — init + encode
// ---------------------------------------------------------------------------
bool EncoderSlot::init_aac()
{
#ifdef HAVE_FDK_AAC
    auto* state = new AacState();
    state->channels = cfg_.channels;

    if (aacEncOpen(&state->enc, 0, static_cast<UINT>(cfg_.channels)) != AACENC_OK) {
        delete state;
        fprintf(stderr, "[EncoderSlot %d] aacEncOpen failed\n", cfg_.slot_id);
        return false;
    }

    // Audio Object Type per codec variant
    UINT aot;
    const char* aot_name;
    switch (cfg_.codec) {
        case EncoderConfig::Codec::AAC_LC:
            aot = 2;   aot_name = "AAC-LC";    break;
        case EncoderConfig::Codec::AAC_HE:
            aot = 5;   aot_name = "HE-AAC v1"; break;  // SBR
        case EncoderConfig::Codec::AAC_HE_V2:
            aot = 29;  aot_name = "HE-AAC v2"; break;  // SBR + PS
        case EncoderConfig::Codec::AAC_ELD:
            aot = 39;  aot_name = "AAC-ELD";   break;  // ER-AAC-ELD
        default:
            aot = 2;   aot_name = "AAC-LC";    break;
    }

    // Core encoder parameters
    aacEncoder_SetParam(state->enc, AACENC_AOT,         aot);
    aacEncoder_SetParam(state->enc, AACENC_SAMPLERATE,  static_cast<UINT>(cfg_.sample_rate));
    aacEncoder_SetParam(state->enc, AACENC_CHANNELMODE,
                        cfg_.channels == 1 ? MODE_1 : MODE_2);
    aacEncoder_SetParam(state->enc, AACENC_BITRATE,
                        static_cast<UINT>(cfg_.bitrate_kbps * 1000));
    aacEncoder_SetParam(state->enc, AACENC_TRANSMUX,    TT_MP4_ADTS);  // ADTS for streaming
    aacEncoder_SetParam(state->enc, AACENC_AFTERBURNER, 1);            // highest quality

    // AAC-ELD: enable SBR and use 512-sample granule (lower latency)
    if (cfg_.codec == EncoderConfig::Codec::AAC_ELD) {
        aacEncoder_SetParam(state->enc, AACENC_SBR_MODE,      1);
        aacEncoder_SetParam(state->enc, AACENC_GRANULE_LENGTH, 512);
    }

    // HE-AAC v1/v2: allow downsampled SBR (halves output sample rate in header)
    if (cfg_.codec == EncoderConfig::Codec::AAC_HE ||
        cfg_.codec == EncoderConfig::Codec::AAC_HE_V2) {
        aacEncoder_SetParam(state->enc, AACENC_SBR_RATIO, 1);  // 2:1 downsampled SBR
    }

    // Initialize encoder (triggers internal reconfiguration)
    AACENC_ERROR err = aacEncEncode(state->enc, nullptr, nullptr, nullptr, nullptr);
    if (err != AACENC_OK) {
        aacEncClose(&state->enc);
        delete state;
        fprintf(stderr, "[EncoderSlot %d] AAC config/init failed: %d\n",
                cfg_.slot_id, static_cast<int>(err));
        return false;
    }

    // Query actual frame length after init
    AACENC_InfoStruct info{};
    aacEncInfo(state->enc, &info);
    state->frame_size = static_cast<int>(info.frameLength);

    aac_enc_ = state;

    // MIME: audio/aacp for HE-AAC profiles, audio/aac for LC and ELD
    if (cfg_.codec == EncoderConfig::Codec::AAC_HE ||
        cfg_.codec == EncoderConfig::Codec::AAC_HE_V2)
        cfg_.stream_target.content_type = "audio/aacp";
    else
        cfg_.stream_target.content_type = "audio/aac";

    fprintf(stderr, "[EncoderSlot %d] %s init: %dk %dHz ch=%d framesize=%d\n",
            cfg_.slot_id, aot_name, cfg_.bitrate_kbps, cfg_.sample_rate,
            cfg_.channels, state->frame_size);
    return true;
#else
    fprintf(stderr, "[EncoderSlot %d] FDK-AAC not compiled in\n", cfg_.slot_id);
    return false;
#endif
}

int EncoderSlot::encode_aac(const float* pcm, size_t frames,
                             uint8_t* out_buf, size_t buf_len)
{
#ifdef HAVE_FDK_AAC
    if (!aac_enc_) return 0;
    auto* state = static_cast<AacState*>(aac_enc_);

    // 1. Convert interleaved float32 → INT_PCM (int16_t), append to accumulation buffer
    const size_t n_samples = frames * static_cast<size_t>(cfg_.channels);
    const size_t old_size  = state->pcm_buf.size();
    state->pcm_buf.resize(old_size + n_samples);
    for (size_t i = 0; i < n_samples; ++i) {
        float s = pcm[i];
        if (s >  1.0f) s =  1.0f;
        if (s < -1.0f) s = -1.0f;
        state->pcm_buf[old_size + i] = static_cast<INT_PCM>(s * 32767.0f);
    }

    // 2. Drain complete AAC frames from the accumulation buffer
    const int frame_samples = state->frame_size * cfg_.channels;  // total INT_PCM per frame

    while (static_cast<int>(state->pcm_buf.size() - state->pcm_head) >= frame_samples) {
        INT_PCM* in_ptr = state->pcm_buf.data() + state->pcm_head;
        void*    in_vptr = static_cast<void*>(in_ptr);

        INT in_id   = IN_AUDIO_DATA;
        INT in_elem = static_cast<INT>(sizeof(INT_PCM));
        INT in_size = frame_samples * static_cast<INT>(sizeof(INT_PCM));

        AACENC_BufDesc in_desc{};
        in_desc.numBufs           = 1;
        in_desc.bufs              = &in_vptr;
        in_desc.bufferIdentifiers = &in_id;
        in_desc.bufSizes          = &in_size;
        in_desc.bufElSizes        = &in_elem;

        // Output: up to 8192 bytes per ADTS frame (far above any realistic bitrate)
        static constexpr int OUT_FRAME_MAX = 8192;
        std::array<uint8_t, OUT_FRAME_MAX> frame_out{};
        void* out_vptr  = static_cast<void*>(frame_out.data());
        INT out_id      = OUT_BITSTREAM_DATA;
        INT out_elem    = 1;
        INT out_sz      = OUT_FRAME_MAX;

        AACENC_BufDesc out_desc{};
        out_desc.numBufs           = 1;
        out_desc.bufs              = &out_vptr;
        out_desc.bufferIdentifiers = &out_id;
        out_desc.bufSizes          = &out_sz;
        out_desc.bufElSizes        = &out_elem;

        AACENC_InArgs  inargs{};
        inargs.numInSamples = frame_samples;
        AACENC_OutArgs outargs{};

        AACENC_ERROR err = aacEncEncode(state->enc, &in_desc, &out_desc, &inargs, &outargs);

        if (err != AACENC_OK && err != AACENC_ENCODE_EOF) {
            fprintf(stderr, "[EncoderSlot %d] aacEncEncode error: %d\n",
                    cfg_.slot_id, static_cast<int>(err));
        } else if (outargs.numOutBytes > 0) {
            state->out_buf.insert(state->out_buf.end(),
                                  frame_out.data(),
                                  frame_out.data() + outargs.numOutBytes);
        }

        state->pcm_head += static_cast<size_t>(frame_samples);

        // Compact accumulation buffer when head advances far enough (>32KB)
        if (state->pcm_head > 16384) {
            state->pcm_buf.erase(state->pcm_buf.begin(),
                                 state->pcm_buf.begin() +
                                     static_cast<ptrdiff_t>(state->pcm_head));
            state->pcm_head = 0;
        }
    }

    // 3. Copy accumulated ADTS output to caller's buffer
    if (state->out_buf.empty()) return 0;
    const size_t copy = std::min(state->out_buf.size(), buf_len);
    memcpy(out_buf, state->out_buf.data(), copy);
    state->out_buf.erase(state->out_buf.begin(),
                         state->out_buf.begin() + static_cast<ptrdiff_t>(copy));
    return static_cast<int>(copy);
#else
    (void)pcm; (void)frames; (void)out_buf; (void)buf_len;
    return 0;
#endif
}

// ---------------------------------------------------------------------------
// Playlist management
// ---------------------------------------------------------------------------
bool EncoderSlot::open_next_track()
{
    auto* fs = dynamic_cast<FileSource*>(source_.get());
    if (!fs) return false;

    for (int attempts = 0; attempts < static_cast<int>(playlist_.size()); ++attempts) {
        std::string path;
        std::string title;
        {
            std::lock_guard<std::mutex> lk(playlist_mtx_);
            if (playlist_.empty()) return false;
            size_t idx = static_cast<size_t>(playlist_pos_) % playlist_.size();
            path  = playlist_[idx].path;
            title = playlist_[idx].title;
        }

        if (fs->open(path)) {
            // Push ICY metadata — pass title, artist, album from embedded tags
            const auto& ti = fs->track_info();
            std::string art = ti.artist.empty() ? "" : ti.artist;
            std::string ttl = ti.title.empty()  ? title : ti.title;
            std::string alb = ti.album.empty()  ? "" : ti.album;
            push_metadata(ttl, art, alb);

            // We record this track play and increment play_count (non-fatal writes).
            if (Mc1Db::instance().is_connected()) {
                std::string title_esc  = Mc1Db::instance().escape(ttl);
                std::string artist_esc = Mc1Db::instance().escape(art);
                int listeners = ServerMonitor::instance().getListenersByMount(
                    cfg_.stream_target.mount);
                Mc1Db::instance().execf(
                    "INSERT INTO mcaster1_metrics.track_plays "
                    "(title, artist, slot_id, played_at, listeners_count) "
                    "VALUES ('%s', '%s', %d, NOW(), %d)",
                    title_esc.c_str(), artist_esc.c_str(), cfg_.slot_id, listeners);
                std::string path_esc = Mc1Db::instance().escape(path);
                Mc1Db::instance().execf(
                    "UPDATE mcaster1_media.tracks SET play_count = play_count + 1, "
                    "last_played_at = NOW() WHERE file_path = '%s'",
                    path_esc.c_str());
            }

            {
                std::lock_guard<std::mutex> lk(mtx_);
                current_title_        = ttl;
                current_artist_       = art;
                current_duration_ms_  = ti.duration_ms;
            }

            // We fire advance_playlist() from a NEW detached thread so that
            // the decode thread can call stop() → join itself freely.
            fs->set_eof_callback([this]() {
                std::thread([this]() { advance_playlist(); }).detach();
            });

            fprintf(stderr, "[EncoderSlot %d] Playing: %s\n",
                    cfg_.slot_id, path.c_str());
            return true;
        }

        // File failed to open — skip it by bumping playlist_pos_ directly.
        // We intentionally do NOT call advance_playlist() here to avoid
        // starting a decode thread mid-loop and to prevent recursion through
        // advance_mtx_ (which we may already hold from advance_playlist()).
        {
            std::lock_guard<std::mutex> lk(playlist_mtx_);
            if (playlist_.empty()) return false;
            ++playlist_pos_;
            if (cfg_.repeat_all)
                playlist_pos_ = playlist_pos_ % static_cast<int>(playlist_.size());
            else if (playlist_pos_ >= static_cast<int>(playlist_.size()))
                return false;
        }
        fprintf(stderr, "[EncoderSlot %d] Skipping missing file: %s\n",
                cfg_.slot_id, path.c_str());
    }
    return false;
}

bool EncoderSlot::advance_playlist()
{
    // Bail out if stop() has been called — the slot is being torn down.
    if (stopping_.load()) return false;

    // We serialize advances so that the EOF-detached thread and any on_audio
    // detached thread cannot race against each other.  The second caller will
    // block briefly then also advance (skipping one extra track at most),
    // which is far preferable to a EDEADLK crash.
    std::lock_guard<std::mutex> alk(advance_mtx_);

    {
        std::lock_guard<std::mutex> lk(playlist_mtx_);
        if (playlist_.empty()) return false;
        ++playlist_pos_;
        if (cfg_.repeat_all)
            playlist_pos_ = playlist_pos_ % static_cast<int>(playlist_.size());
        else if (playlist_pos_ >= static_cast<int>(playlist_.size()))
            return false;
    }

    auto* fs = dynamic_cast<FileSource*>(source_.get());
    if (!fs) return false;

    fs->stop();

    bool ok = open_next_track();
    if (ok) {
        auto cb = [this](const float* pcm, size_t frames, int ch, int sr) {
            on_audio(pcm, frames, ch, sr);
        };
        fs->start(cb);
    }
    return ok;
}

void EncoderSlot::shuffle_playlist()
{
    // Must hold playlist_mtx_
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(playlist_.begin(), playlist_.end(), g);
}

// ---------------------------------------------------------------------------
// set_state / set_error
// ---------------------------------------------------------------------------
void EncoderSlot::set_state(State s)
{
    state_.store(s);

    // We write stream_events for key state transitions (non-fatal fire-and-forget).
    if (s == State::LIVE || s == State::STOPPING ||
        s == State::RECONNECTING || s == State::ERROR) {
        const char* evt = nullptr;
        switch (s) {
            case State::LIVE:         evt = "start";     break;
            case State::STOPPING:     evt = "stop";      break;
            case State::RECONNECTING: evt = "reconnect"; break;
            case State::ERROR:        evt = "error";     break;
            default: break;
        }
        if (evt && Mc1Db::instance().is_connected()) {
            std::string mount_esc = Mc1Db::instance().escape(cfg_.stream_target.mount);
            Mc1Db::instance().execf(
                "INSERT INTO mcaster1_metrics.stream_events "
                "(slot_id, mount, event_type) VALUES (%d, '%s', '%s')",
                cfg_.slot_id, mount_esc.c_str(), evt);
        }
    }
}

void EncoderSlot::set_error(const std::string& msg)
{
    std::lock_guard<std::mutex> lk(mtx_);
    last_error_ = msg;
    state_.store(State::ERROR);
    fprintf(stderr, "[EncoderSlot %d] Error: %s\n", cfg_.slot_id, msg.c_str());
}
