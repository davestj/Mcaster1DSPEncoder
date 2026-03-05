/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/video_encoder.h — VideoToolbox H.264 hardware encoder
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_VIDEO_ENCODER_H
#define MC1_VIDEO_ENCODER_H

#include "video_source.h"
#include <cstdint>
#include <functional>
#include <vector>

namespace mc1 {

class VideoEncoder {
public:
    using EncodedCallback = std::function<void(const uint8_t* data,
                                               size_t len,
                                               int64_t pts_us,
                                               bool is_keyframe)>;

    VideoEncoder(int width, int height, int fps, int bitrate_kbps);
    ~VideoEncoder();

    bool init(EncodedCallback cb);
    bool encode(const VideoFrame& frame);
    void flush();
    void close();

    /* SPS/PPS for sequence header (populated after first encode) */
    const std::vector<uint8_t>& sps() const { return sps_; }
    const std::vector<uint8_t>& pps() const { return pps_; }

private:
    int width_;
    int height_;
    int fps_;
    int bitrate_kbps_;

    EncodedCallback callback_;
    void *compression_session_ = nullptr; /* VTCompressionSessionRef */

    std::vector<uint8_t> sps_;
    std::vector<uint8_t> pps_;
    bool sps_pps_extracted_ = false;
};

} // namespace mc1

#endif // MC1_VIDEO_ENCODER_H
