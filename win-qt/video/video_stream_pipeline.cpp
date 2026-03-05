/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/video_stream_pipeline.cpp — Live video streaming pipeline orchestrator
 *
 * Coordinates the full video pipeline:
 *   source → effects → encode → mux → transport
 *
 * Transport selection:
 *   - RTMP: FlvMuxer → RtmpClient (YouTube/Twitch)
 *   - FLV_ICECAST: FlvMuxer → StreamClient (Icecast2/DNAS)
 *   - WEBM_ICECAST: WebmMuxer → StreamClient (Icecast2)
 *   - HLS: HlsSegmenter → disk
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "video_stream_pipeline.h"
#include "rtmp_client.h"
#include "video_effects.h"
#include "overlay_renderer.h"
#include "transition_engine.h"
#include "../stream_client.h"
#ifdef HAVE_VPX
#include "vpx_encoder.h"
#endif

#include <chrono>
#include <cstring>

namespace mc1 {

VideoStreamPipeline::VideoStreamPipeline() = default;

VideoStreamPipeline::~VideoStreamPipeline()
{
    stop();
}

bool VideoStreamPipeline::start(const VideoStreamConfig& cfg,
                                 VideoSource* video_source,
                                 VideoEffectsChain* effects,
                                 OverlayRenderer* overlay,
                                 TransitionEngine* transition)
{
    if (running_.load()) stop();

    std::lock_guard<std::mutex> lk(mtx_);

    cfg_        = cfg;
    source_     = video_source;
    effects_    = effects;
    overlay_    = overlay;
    transition_ = transition;

    frames_encoded_ = 0;
    bytes_sent_     = 0;
    dropped_frames_ = 0;
    sps_pps_sent_   = false;
    aac_header_sent_ = false;

    auto now = std::chrono::steady_clock::now();
    start_time_us_ = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count());

    /* 1. Create video encoder */
    bool ok = false;
#ifdef HAVE_VPX
    if (cfg.video.video_codec == VideoConfig::VideoCodec::VP8 ||
        cfg.video.video_codec == VideoConfig::VideoCodec::VP9) {
        auto vpx_codec = (cfg.video.video_codec == VideoConfig::VideoCodec::VP9)
            ? VpxEncoder::Codec::VP9 : VpxEncoder::Codec::VP8;
        vpx_encoder_ = std::make_unique<VpxEncoder>(
            cfg.video.width, cfg.video.height, cfg.video.fps,
            cfg.video.bitrate_kbps, vpx_codec);
        ok = vpx_encoder_->init([this](const uint8_t* data, size_t len,
                                        int64_t pts_us, bool is_keyframe) {
            on_encoded_nalu(data, len, pts_us, is_keyframe);
        });
    } else
#endif
    {
        /* H.264 via VideoToolbox */
        encoder_ = std::make_unique<VideoEncoder>(
            cfg.video.width, cfg.video.height, cfg.video.fps, cfg.video.bitrate_kbps);
        ok = encoder_->init([this](const uint8_t* data, size_t len,
                                     int64_t pts_us, bool is_keyframe) {
            on_encoded_nalu(data, len, pts_us, is_keyframe);
        });
    }
    if (!ok) return false;

    /* 2. Create transport + muxer based on config */
    switch (cfg.transport) {
        case VideoStreamConfig::Transport::RTMP: {
            /* Create RTMP client and connect */
            if (!cfg.dry_run && !cfg.stream_key.empty()) {
                rtmp_client_ = std::make_unique<RtmpClient>();
                std::string rtmp_url = "rtmp://" + cfg.target.host + ":" +
                                       std::to_string(cfg.target.port) + "/" +
                                       cfg.target.mount;
                if (!rtmp_client_->connect(rtmp_url, cfg.stream_key)) {
                    /* Connection failed — fall through to dry-run mode */
                    rtmp_client_.reset();
                }
            }

            /* FLV muxer wraps encoded data — the pipeline sends video/audio
             * directly through the RTMP client, not through FLV muxer output.
             * FLV muxer is only used for FLV_ICECAST transport.
             * For RTMP: VideoEncoder NALUs → on_encoded_nalu → rtmp_client_->send_video()
             * and audio tap → feed_audio → rtmp_client_->send_audio() */
            flv_muxer_ = std::make_unique<FlvMuxer>();
            flv_muxer_->set_output([](const uint8_t*, size_t) {
                /* No-op: RTMP sends via send_video/send_audio directly */
            });
            break;
        }

        case VideoStreamConfig::Transport::FLV_ICECAST: {
            flv_muxer_ = std::make_unique<FlvMuxer>();
            /* Create StreamClient for Icecast2/DNAS FLV push */
            if (!cfg.dry_run) {
                StreamTarget st = cfg.target;
                st.content_type = "video/x-flv";
                stream_client_ = std::make_shared<StreamClient>(st);
                stream_client_->connect();
            }
            flv_muxer_->set_output([this](const uint8_t* data, size_t len) {
                if (cfg_.dry_run) return;
                if (stream_client_ && stream_client_->is_connected()) {
                    stream_client_->write(data, len);
                    bytes_sent_ += len;
                }
            });
            break;
        }

        case VideoStreamConfig::Transport::WEBM_ICECAST: {
            webm_muxer_ = std::make_unique<WebmMuxer>();
            if (!cfg.dry_run) {
                StreamTarget st = cfg.target;
                st.content_type = "video/webm";
                stream_client_ = std::make_shared<StreamClient>(st);
                stream_client_->connect();
            }
            webm_muxer_->set_output([this](const uint8_t* data, size_t len) {
                if (cfg_.dry_run) return;
                if (stream_client_ && stream_client_->is_connected()) {
                    stream_client_->write(data, len);
                    bytes_sent_ += len;
                }
            });
            /* Write WebM header */
            std::string codec_id = (cfg.video.video_codec == VideoConfig::VideoCodec::VP9)
                                   ? "V_VP9" : "V_VP8";
            webm_muxer_->write_header(cfg.video.width, cfg.video.height, cfg.video.fps,
                                       codec_id, true, 44100, 2);
            break;
        }

        case VideoStreamConfig::Transport::HLS:
            hls_segmenter_ = std::make_unique<HlsSegmenter>();
            if (!hls_segmenter_->init(cfg.hls_output_dir,
                                       cfg.hls_segment_duration,
                                       cfg.hls_max_segments)) {
                return false;
            }
            hls_segmenter_->set_video_params(cfg.video.width, cfg.video.height, cfg.video.fps);
            hls_segmenter_->set_audio_params(44100, 2);
            break;
    }

    /* 3. Start video source — feeds frames to on_video_frame() */
    if (source_) {
        ok = source_->start([this](const VideoFrame& frame) {
            on_video_frame(frame);
        });
        if (!ok) return false;
    }

    running_ = true;
    return true;
}

void VideoStreamPipeline::stop()
{
    if (!running_.exchange(false)) return;

    std::lock_guard<std::mutex> lk(mtx_);

    /* Stop video source (if we started it) */
    if (source_ && source_->is_running()) {
        source_->stop();
    }

    /* Flush encoder */
#ifdef HAVE_VPX
    if (vpx_encoder_) {
        vpx_encoder_->flush();
        vpx_encoder_->close();
        vpx_encoder_.reset();
    }
#endif
    if (encoder_) {
        encoder_->flush();
        encoder_->close();
        encoder_.reset();
    }

    /* Close muxers */
    if (flv_muxer_)     flv_muxer_.reset();
    if (webm_muxer_) {
        webm_muxer_->flush_cluster();
        webm_muxer_.reset();
    }
    if (hls_segmenter_) {
        hls_segmenter_->close();
        hls_segmenter_.reset();
    }

    /* Disconnect transport */
    if (rtmp_client_)    rtmp_client_.reset();
    if (stream_client_) {
        stream_client_->disconnect();
        stream_client_.reset();
    }

    source_     = nullptr;
    effects_    = nullptr;
    overlay_    = nullptr;
    transition_ = nullptr;
}

void VideoStreamPipeline::feed_audio(const uint8_t* data, size_t len,
                                      uint32_t timestamp_ms, bool is_aac)
{
    if (!running_.load()) return;

    switch (cfg_.transport) {
        case VideoStreamConfig::Transport::RTMP:
            if (rtmp_client_ && rtmp_client_->is_connected()) {
                /* Build FLV audio tag body and send via RTMP */
                std::vector<uint8_t> tag_body;
                if (is_aac) {
                    if (!aac_header_sent_ && len >= 2) {
                        /* Send AAC sequence header first */
                        uint8_t asc_hdr[4] = { 0xAF, 0x00, data[0], data[1] };
                        rtmp_client_->send_audio(asc_hdr, 4, timestamp_ms);
                        aac_header_sent_ = true;
                    }
                    tag_body.resize(2 + len);
                    tag_body[0] = 0xAF; /* AAC, 44100, 16-bit, stereo */
                    tag_body[1] = 0x01; /* AAC raw */
                    std::memcpy(tag_body.data() + 2, data, len);
                } else {
                    tag_body.resize(1 + len);
                    tag_body[0] = 0x2F; /* MP3, 44100, 16-bit, stereo */
                    std::memcpy(tag_body.data() + 1, data, len);
                }
                rtmp_client_->send_audio(tag_body.data(), tag_body.size(), timestamp_ms);
                bytes_sent_ += tag_body.size();
            }
            break;

        case VideoStreamConfig::Transport::FLV_ICECAST:
            if (flv_muxer_) {
                if (is_aac) {
                    if (!aac_header_sent_) {
                        if (len >= 2) {
                            flv_muxer_->write_aac_sequence_header(data, 2, timestamp_ms);
                            aac_header_sent_ = true;
                        }
                    }
                    flv_muxer_->write_aac_audio(data, len, timestamp_ms);
                } else {
                    flv_muxer_->write_mp3_audio(data, len, timestamp_ms, 44100, true);
                }
            }
            break;

        case VideoStreamConfig::Transport::WEBM_ICECAST:
            if (webm_muxer_) {
                webm_muxer_->write_audio(data, len, timestamp_ms);
            }
            break;

        case VideoStreamConfig::Transport::HLS:
            if (hls_segmenter_) {
                int64_t pts_us = static_cast<int64_t>(timestamp_ms) * 1000;
                hls_segmenter_->write_audio(data, len, pts_us);
            }
            break;
    }
}

PipelineStats VideoStreamPipeline::stats() const
{
    PipelineStats s;
    s.frames_encoded  = frames_encoded_.load();
    s.bytes_sent      = bytes_sent_.load();
    s.dropped_frames  = dropped_frames_.load();
    s.is_live         = running_.load();

    if (s.is_live && start_time_us_ > 0) {
        auto now = std::chrono::steady_clock::now();
        uint64_t now_us = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                now.time_since_epoch()).count());
        s.uptime_sec = static_cast<double>(now_us - start_time_us_) / 1000000.0;
        if (s.uptime_sec > 0.0)
            s.avg_fps = static_cast<double>(s.frames_encoded) / s.uptime_sec;
    }
    return s;
}

// ── Internal callbacks ──────────────────────────────────────────────────

void VideoStreamPipeline::on_video_frame(const VideoFrame& frame)
{
    if (!running_.load()) return;

    /* Apply video effects chain (in-place on BGRA buffer) */
    VideoFrame processed = frame;
    if (effects_) {
        effects_->process(const_cast<uint8_t*>(processed.data),
                          processed.width, processed.height, processed.stride);
    }

    /* Apply overlay (text, logo, ticker) */
    if (overlay_) {
        overlay_->render(const_cast<uint8_t*>(processed.data),
                         processed.width, processed.height, processed.stride);
    }

    /* Encode the frame */
    bool encoded = false;
#ifdef HAVE_VPX
    if (vpx_encoder_) {
        encoded = vpx_encoder_->encode(processed);
    } else
#endif
    if (encoder_) {
        encoded = encoder_->encode(processed);
    }
    if (!encoded) {
        dropped_frames_++;
    }
}

void VideoStreamPipeline::on_encoded_nalu(const uint8_t* data, size_t len,
                                           int64_t pts_us, bool is_keyframe)
{
    if (!running_.load()) return;

    frames_encoded_++;

    uint32_t ts_ms = static_cast<uint32_t>(pts_us / 1000);

    switch (cfg_.transport) {
        case VideoStreamConfig::Transport::RTMP:
            if (rtmp_client_ && rtmp_client_->is_connected()) {
                /* Send metadata + sequence header on first keyframe */
                if (!sps_pps_sent_ && is_keyframe && encoder_) {
                    rtmp_client_->send_metadata(
                        cfg_.video.width, cfg_.video.height,
                        cfg_.video.fps, cfg_.video.bitrate_kbps);

                    const auto& sps = encoder_->sps();
                    const auto& pps = encoder_->pps();
                    if (!sps.empty() && !pps.empty()) {
                        /* Build AVC sequence header (FLV video tag body) */
                        std::vector<uint8_t> seq_hdr;
                        seq_hdr.push_back(0x17); /* keyframe + AVC */
                        seq_hdr.push_back(0x00); /* AVC sequence header */
                        seq_hdr.push_back(0x00); seq_hdr.push_back(0x00); seq_hdr.push_back(0x00);
                        /* AVCDecoderConfigurationRecord */
                        seq_hdr.push_back(0x01);
                        seq_hdr.push_back(sps.size() > 1 ? sps[1] : 0x64);
                        seq_hdr.push_back(sps.size() > 2 ? sps[2] : 0x00);
                        seq_hdr.push_back(sps.size() > 3 ? sps[3] : 0x1F);
                        seq_hdr.push_back(0xFF);
                        seq_hdr.push_back(0xE1);
                        seq_hdr.push_back(static_cast<uint8_t>(sps.size() >> 8));
                        seq_hdr.push_back(static_cast<uint8_t>(sps.size()));
                        seq_hdr.insert(seq_hdr.end(), sps.begin(), sps.end());
                        seq_hdr.push_back(0x01);
                        seq_hdr.push_back(static_cast<uint8_t>(pps.size() >> 8));
                        seq_hdr.push_back(static_cast<uint8_t>(pps.size()));
                        seq_hdr.insert(seq_hdr.end(), pps.begin(), pps.end());
                        rtmp_client_->send_video(seq_hdr.data(), seq_hdr.size(), ts_ms);
                        sps_pps_sent_ = true;
                    }
                }
                /* Build FLV video tag body and send */
                std::vector<uint8_t> tag_body;
                tag_body.reserve(5 + len);
                tag_body.push_back(is_keyframe ? 0x17 : 0x27);
                tag_body.push_back(0x01); /* AVC NALU */
                tag_body.push_back(0x00); tag_body.push_back(0x00); tag_body.push_back(0x00);
                tag_body.insert(tag_body.end(), data, data + len);
                rtmp_client_->send_video(tag_body.data(), tag_body.size(), ts_ms);
                bytes_sent_ += tag_body.size();
            }
            break;

        case VideoStreamConfig::Transport::FLV_ICECAST:
            if (flv_muxer_) {
                if (!sps_pps_sent_ && is_keyframe && encoder_) {
                    const auto& sps = encoder_->sps();
                    const auto& pps = encoder_->pps();
                    if (!sps.empty() && !pps.empty()) {
                        flv_muxer_->write_header(true, true);
                        flv_muxer_->write_avc_sequence_header(
                            sps.data(), sps.size(),
                            pps.data(), pps.size(), ts_ms);
                        sps_pps_sent_ = true;
                    }
                }
                flv_muxer_->write_video(data, len, ts_ms, is_keyframe);
            }
            break;

        case VideoStreamConfig::Transport::WEBM_ICECAST:
            if (webm_muxer_) {
                webm_muxer_->write_video(data, len, ts_ms, is_keyframe);
            }
            break;

        case VideoStreamConfig::Transport::HLS:
            if (hls_segmenter_) {
                hls_segmenter_->write_video(data, len, pts_us, is_keyframe);
            }
            break;
    }
}

} // namespace mc1
