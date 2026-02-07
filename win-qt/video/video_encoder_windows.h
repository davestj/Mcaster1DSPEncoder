/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * video/video_encoder_windows.h — Media Foundation H.264 hardware encoder
 *
 * Windows API: Media Foundation Transform (MFT) H.264 encoder,
 *              or Intel QSV / NVIDIA NVENC via MFT
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_VIDEO_ENCODER_WINDOWS_H
#define MC1_VIDEO_ENCODER_WINDOWS_H

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
    void *mft_encoder_ = nullptr; /* IMFTransform* (H.264 MFT) */

    std::vector<uint8_t> sps_;
    std::vector<uint8_t> pps_;
    bool sps_pps_extracted_ = false;
};

} // namespace mc1

#endif // MC1_VIDEO_ENCODER_WINDOWS_H
