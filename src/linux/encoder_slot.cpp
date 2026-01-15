// encoder_slot.cpp — Per-encoder-slot state machine
// Phase 3 — Mcaster1DSPEncoder Linux v1.2.0
#include "encoder_slot.h"
#include "file_source.h"
#include "audio_source.h"

#include <cstring>
#include <cstdio>
#include <ctime>
#include <algorithm>
#include <random>
#include <stdexcept>

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
#ifdef HAVE_PORTAUDIO
#include <portaudio.h>
#endif

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
    std::lock_guard<std::mutex> lk(mtx_);

    if (state_.load() != State::IDLE && state_.load() != State::ERROR)
        return false;

    set_state(State::STARTING);
    last_error_.clear();

    // --- Init codec ---
    bool codec_ok = false;
    switch (cfg_.codec) {
        case EncoderConfig::Codec::MP3:    codec_ok = init_lame();   break;
        case EncoderConfig::Codec::VORBIS: codec_ok = init_vorbis(); break;
        case EncoderConfig::Codec::OPUS:   codec_ok = init_opus();   break;
        case EncoderConfig::Codec::FLAC:   codec_ok = init_flac();   break;
    }
    if (!codec_ok) {
        set_error("Codec init failed");
        return false;
    }

    // --- Init stream client ---
    stream_client_ = std::make_unique<StreamClient>(cfg_.stream_target);
    stream_client_->set_state_callback([this](StreamClient::State cs) {
        if (cs == StreamClient::State::CONNECTED)   set_state(State::LIVE);
        if (cs == StreamClient::State::RECONNECTING) set_state(State::RECONNECTING);
    });

    // --- Init archive ---
    if (cfg_.archive_enabled && !cfg_.archive_dir.empty()) {
        ArchiveWriter::Config ac;
        ac.archive_dir  = cfg_.archive_dir;
        ac.station_name = cfg_.name;
        archive_ = std::make_unique<ArchiveWriter>(ac);
        archive_->open(cfg_.sample_rate, cfg_.channels);
    }

    // --- Init audio source ---
    if (cfg_.input_type == EncoderConfig::InputType::DEVICE) {
        auto* pa = new PortAudioSource(cfg_.device_index,
                                       cfg_.sample_rate,
                                       cfg_.channels);
        source_.reset(pa);
    } else {
        // Playlist / file source
        if (!cfg_.playlist_path.empty()) {
            auto entries = PlaylistParser::parse(cfg_.playlist_path);
            std::lock_guard<std::mutex> plk(playlist_mtx_);
            playlist_ = std::move(entries);
            playlist_pos_ = 0;
            if (cfg_.shuffle) shuffle_playlist();
        }
        auto* fs = new FileSource(cfg_.sample_rate, cfg_.channels);
        source_.reset(fs);

        // Load first track
        if (!open_next_track()) {
            set_error("No tracks in playlist");
            source_.reset();
            stream_client_.reset();
            close_codec();
            return false;
        }
    }

    // --- Register audio callback ---
    auto callback = [this](const float* pcm, size_t frames, int ch, int sr) {
        on_audio(pcm, frames, ch, sr);
    };

    // --- Connect to server ---
    set_state(State::CONNECTING);
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
    set_state(State::STOPPING);

    if (source_)        { source_->stop(); source_.reset(); }
    if (stream_client_) { stream_client_->disconnect(); stream_client_.reset(); }
    if (archive_)       { archive_->close(); archive_.reset(); }

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
// push_metadata
// ---------------------------------------------------------------------------
void EncoderSlot::push_metadata(const std::string& title, const std::string& artist)
{
    if (stream_client_) {
        stream_client_->send_icy_metadata(title, artist);
    }
    std::lock_guard<std::mutex> lk(mtx_);
    current_title_  = title;
    current_artist_ = artist;
}

// ---------------------------------------------------------------------------
// stats
// ---------------------------------------------------------------------------
EncoderSlot::Stats EncoderSlot::stats() const
{
    Stats s;
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
            advance_playlist();
        }
    }

    if (!stream_client_ || !stream_client_->is_connected()) return;

    // Apply volume
    float vol = cfg_.volume;
    std::vector<float> buf;
    if (vol != 1.0f) {
        size_t n = frames * static_cast<size_t>(cfg_.channels);
        buf.resize(n);
        for (size_t i = 0; i < n; ++i) buf[i] = pcm[i] * vol;
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
    lame_set_brate(gfp, cfg_.bitrate_kbps);
    lame_set_quality(gfp, 2);
    lame_set_mode(gfp, cfg_.channels == 1 ? MONO : JOINT_STEREO);

    if (lame_init_params(gfp) < 0) {
        lame_close(gfp);
        return false;
    }
    lame_enc_ = gfp;

    // Update MIME type for stream
    cfg_.stream_target.content_type = "audio/mpeg";
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
    // libopusenc wraps Opus + Ogg container
#ifdef HAVE_OPUS
    // OggOpusEnc requires a file or callbacks — we'll use callbacks to a memory buffer
    // For now we use opusenc with a callback that writes to stream_client_
    // This is set up properly once connected, so just flag readiness
    cfg_.stream_target.content_type = "audio/ogg";
    return true;  // Actual init happens in first encode call
#else
    fprintf(stderr, "[EncoderSlot] Opus not compiled in\n");
    return false;
#endif
}

bool EncoderSlot::init_flac()
{
#ifdef HAVE_FLAC
    FLAC__StreamEncoder* enc = FLAC__stream_encoder_new();
    if (!enc) return false;

    FLAC__stream_encoder_set_channels(enc, static_cast<unsigned>(cfg_.channels));
    FLAC__stream_encoder_set_bits_per_sample(enc, 16);
    FLAC__stream_encoder_set_sample_rate(enc, static_cast<unsigned>(cfg_.sample_rate));
    FLAC__stream_encoder_set_compression_level(enc, 5);

    flac_enc_ = enc;
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
            vorbis_info     vi;
            vorbis_dsp_state vd;
            vorbis_block    vb;
            vorbis_comment  vc;
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
#ifdef HAVE_FLAC
    if (flac_enc_) {
        FLAC__stream_encoder_finish(static_cast<FLAC__StreamEncoder*>(flac_enc_));
        FLAC__stream_encoder_delete(static_cast<FLAC__StreamEncoder*>(flac_enc_));
        flac_enc_ = nullptr;
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

int EncoderSlot::encode_opus(const float* /*pcm*/, size_t /*frames*/,
                              uint8_t* /*out_buf*/, size_t /*buf_len*/)
{
    // Full Opus/Ogg encoding via libopusenc requires writing to a sink
    // Implemented in Phase 3 refinement pass
    return 0;
}

int EncoderSlot::encode_flac(const float* pcm, size_t frames,
                              uint8_t* /*out_buf*/, size_t /*buf_len*/)
{
#ifdef HAVE_FLAC
    if (!flac_enc_) return 0;
    auto* enc = static_cast<FLAC__StreamEncoder*>(flac_enc_);

    // Convert float → int32 (16-bit range)
    std::vector<FLAC__int32> samples(frames * static_cast<size_t>(cfg_.channels));
    for (size_t i = 0; i < samples.size(); ++i) {
        float s = pcm[i];
        if (s >  1.0f) s =  1.0f;
        if (s < -1.0f) s = -1.0f;
        samples[i] = static_cast<FLAC__int32>(s * 32767.0f);
    }

    const FLAC__int32* const channels_arr[] = {
        samples.data(),
        samples.data() + frames
    };
    // Note: FLAC__stream_encoder_process expects non-interleaved
    // This simplified version just returns 0 for now
    // Full deinterleaving done in Phase 3 refinement
    (void)enc; (void)channels_arr;
    return 0;
#else
    (void)pcm; (void)frames;
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
            // Push ICY metadata
            const auto& ti = fs->track_info();
            std::string art = ti.artist.empty() ? "" : ti.artist;
            std::string ttl = ti.title.empty()  ? title : ti.title;
            push_metadata(ttl, art);

            {
                std::lock_guard<std::mutex> lk(mtx_);
                current_title_        = ttl;
                current_artist_       = art;
                current_duration_ms_  = ti.duration_ms;
            }

            fs->set_eof_callback([this]() {
                advance_playlist();
            });

            fprintf(stderr, "[EncoderSlot %d] Playing: %s\n",
                    cfg_.slot_id, path.c_str());
            return true;
        }

        // File failed — advance and try next
        advance_playlist();
    }
    return false;
}

bool EncoderSlot::advance_playlist()
{
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
}

void EncoderSlot::set_error(const std::string& msg)
{
    std::lock_guard<std::mutex> lk(mtx_);
    last_error_ = msg;
    state_.store(State::ERROR);
    fprintf(stderr, "[EncoderSlot %d] Error: %s\n", cfg_.slot_id, msg.c_str());
}
