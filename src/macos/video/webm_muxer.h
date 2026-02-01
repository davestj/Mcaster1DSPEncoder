/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/webm_muxer.h — WebM (EBML/Matroska) container muxer for VP8/VP9 streaming
 *
 * Produces a live-streamable WebM byte stream:
 *   EBML Header → Segment (unknown size) → Info → Tracks →
 *   Cluster₁ [SimpleBlock ...] → Cluster₂ [SimpleBlock ...] → ...
 *
 * Clusters auto-close at ~5 seconds or on keyframe.
 * Output goes to a callback (for piping into StreamClient → Icecast2).
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_WEBM_MUXER_H
#define MC1_WEBM_MUXER_H

#include <cstdint>
#include <cstddef>
#include <functional>
#include <vector>
#include <string>

namespace mc1 {

class WebmMuxer {
public:
    using OutputCallback = std::function<void(const uint8_t* data, size_t len)>;

    WebmMuxer();

    void set_output(OutputCallback cb) { output_ = cb; }

    /* Write EBML header + Segment header + Info + Tracks.
     * Must call before writing any frames. */
    void write_header(int width, int height, int fps,
                      const std::string& codec_id,  /* "V_VP8" or "V_VP9" */
                      bool has_audio, int sample_rate, int channels);

    /* Write a video frame as a SimpleBlock on track 1 */
    void write_video(const uint8_t* data, size_t len,
                     uint32_t timestamp_ms, bool is_keyframe);

    /* Write an audio frame as a SimpleBlock on track 2 */
    void write_audio(const uint8_t* data, size_t len,
                     uint32_t timestamp_ms);

    /* Force-close current cluster and start a new one */
    void flush_cluster();

private:
    /* EBML element writers */
    void write_ebml_header();
    void write_segment_start();
    void write_info(int fps);
    void write_tracks(int width, int height, const std::string& codec_id,
                      bool has_audio, int sample_rate, int channels);
    void open_cluster(uint32_t timestamp_ms);
    void close_cluster();
    void write_simple_block(uint8_t track_num, const uint8_t* data, size_t len,
                            uint32_t timestamp_ms, bool is_keyframe);

    /* Low-level EBML helpers */
    void write_ebml_id(uint32_t id);
    void write_ebml_size(uint64_t size);
    void write_ebml_size_unknown();
    void write_ebml_uint(uint32_t id, uint64_t val);
    void write_ebml_float(uint32_t id, double val);
    void write_ebml_string(uint32_t id, const std::string& val);
    void write_ebml_binary(uint32_t id, const uint8_t* data, size_t len);
    size_t ebml_uint_size(uint64_t val);
    void   ebml_encode_uint(uint64_t val, size_t bytes);

    void emit_bytes(const uint8_t* data, size_t len);

    OutputCallback output_;

    bool     header_written_ = false;
    bool     cluster_open_   = false;
    uint32_t cluster_ts_ms_  = 0;    /* cluster start timestamp */
    uint32_t last_ts_ms_     = 0;

    static constexpr uint32_t kClusterDurationMs = 5000;
};

} // namespace mc1

#endif // MC1_WEBM_MUXER_H
