/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/video_stream_pipeline.h — Live video streaming pipeline orchestrator
 *
 * Owns the complete capture → effects → encode → mux → stream chain:
 *
 *   VideoSource (camera/screen/image)
 *       → VideoEffectsChain (BGRA effects)
 *       → OverlayRenderer (text/logo/ticker)
 *       → TransitionEngine (A/B blending)
 *       → VideoEncoder (H.264 via VideoToolbox, or VP8/VP9 via libvpx)
 *       → Container Muxer:
 *           ├─ FlvMuxer → RtmpClient (YouTube/Twitch)
 *           ├─ FlvMuxer → StreamClient (Icecast2/DNAS FLV stream)
 *           ├─ WebmMuxer → StreamClient (Icecast2 VP8/VP9)
 *           └─ HlsSegmenter → disk (MPEG-TS segments + M3U8)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_VIDEO_STREAM_PIPELINE_H
#define MC1_VIDEO_STREAM_PIPELINE_H

#include "../config_types.h"
#include "video_source.h"
#include "video_encoder.h"
#include "flv_muxer.h"
#include "webm_muxer.h"
#include "hls_segmenter.h"

#include <cstdint>
#include <atomic>
#include <mutex>
#include <memory>
#include <string>

namespace mc1 {

class VideoEffectsChain;
class OverlayRenderer;
class TransitionEngine;
class RtmpClient;
class StreamClient;
#ifdef HAVE_VPX
class VpxEncoder;
#endif

struct VideoStreamConfig {
    VideoConfig      video;
    StreamTarget     target;

    /* RTMP-specific */
    std::string      stream_key;

    /* HLS-specific */
    std::string      hls_output_dir;
    int              hls_segment_duration = 6;
    int              hls_max_segments     = 5;

    /* Transport selection */
    enum class Transport { RTMP, FLV_ICECAST, WEBM_ICECAST, HLS };
    Transport        transport = Transport::RTMP;

    /* Dry run — process pipeline but don't send to network */
    bool             dry_run = false;
};

struct PipelineStats {
    uint64_t frames_encoded  = 0;
    uint64_t bytes_sent      = 0;
    uint64_t dropped_frames  = 0;
    double   avg_fps         = 0.0;
    double   uptime_sec      = 0.0;
    bool     is_live         = false;
};

class VideoStreamPipeline {
public:
    VideoStreamPipeline();
    ~VideoStreamPipeline();

    /* Start the pipeline with the given configuration.
     * video_source: owned externally, must outlive the pipeline.
     * effects/overlay/transition: optional, may be nullptr. */
    bool start(const VideoStreamConfig& cfg,
               VideoSource* video_source,
               VideoEffectsChain* effects = nullptr,
               OverlayRenderer* overlay = nullptr,
               TransitionEngine* transition = nullptr);

    /* Stop the pipeline and tear down all resources */
    void stop();

    /* Feed encoded audio from an EncoderSlot audio tap */
    void feed_audio(const uint8_t* data, size_t len,
                    uint32_t timestamp_ms, bool is_aac);

    /* Get pipeline statistics */
    PipelineStats stats() const;

    bool is_running() const { return running_.load(); }

private:
    /* Video frame callback from the source */
    void on_video_frame(const VideoFrame& frame);

    /* Encoded NALU callback from VideoEncoder */
    void on_encoded_nalu(const uint8_t* data, size_t len,
                         int64_t pts_us, bool is_keyframe);

    VideoStreamConfig cfg_;

    /* Pipeline components (owned) */
    std::unique_ptr<VideoEncoder>  encoder_;
#ifdef HAVE_VPX
    std::unique_ptr<VpxEncoder>    vpx_encoder_;
#endif
    std::unique_ptr<FlvMuxer>      flv_muxer_;
    std::unique_ptr<WebmMuxer>     webm_muxer_;
    std::unique_ptr<HlsSegmenter>  hls_segmenter_;
    std::unique_ptr<RtmpClient>    rtmp_client_;
    std::shared_ptr<StreamClient>  stream_client_;

    /* External references (not owned) */
    VideoSource*       source_     = nullptr;
    VideoEffectsChain* effects_    = nullptr;
    OverlayRenderer*   overlay_    = nullptr;
    TransitionEngine*  transition_ = nullptr;

    std::atomic<bool>     running_{false};
    std::atomic<uint64_t> frames_encoded_{0};
    std::atomic<uint64_t> bytes_sent_{0};
    std::atomic<uint64_t> dropped_frames_{0};
    uint64_t              start_time_us_ = 0;

    mutable std::mutex    mtx_;

    /* SPS/PPS sent flag for FLV/RTMP */
    bool sps_pps_sent_ = false;
    bool aac_header_sent_ = false;
};

} // namespace mc1

#endif // MC1_VIDEO_STREAM_PIPELINE_H
