/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/vpx_encoder.h — VP8/VP9 encoder via libvpx
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_VPX_ENCODER_H
#define MC1_VPX_ENCODER_H

#ifdef HAVE_VPX

#include "video_source.h"
#include <cstdint>
#include <functional>
#include <vector>

#include <vpx/vpx_encoder.h>
#include <vpx/vp8cx.h>

namespace mc1 {

class VpxEncoder {
public:
    using EncodedCallback = std::function<void(const uint8_t* data,
                                               size_t len,
                                               int64_t pts_us,
                                               bool is_keyframe)>;

    enum class Codec { VP8, VP9 };

    VpxEncoder(int width, int height, int fps, int bitrate_kbps, Codec codec);
    ~VpxEncoder();

    bool init(EncodedCallback cb);
    bool encode(const VideoFrame& frame);
    void flush();
    void close();

private:
    void bgra_to_i420(const uint8_t* bgra, int stride,
                      int w, int h, vpx_image_t* img);

    int width_;
    int height_;
    int fps_;
    int bitrate_kbps_;
    Codec codec_;

    EncodedCallback callback_;
    vpx_codec_ctx_t codec_ctx_{};
    bool codec_init_ = false;
    int64_t frame_count_ = 0;
};

} // namespace mc1

#endif // HAVE_VPX
#endif // MC1_VPX_ENCODER_H
