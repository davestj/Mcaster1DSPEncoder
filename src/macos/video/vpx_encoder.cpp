/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/vpx_encoder.cpp — VP8/VP9 encoder via libvpx
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifdef HAVE_VPX

#include "vpx_encoder.h"
#include <cstring>

namespace mc1 {

VpxEncoder::VpxEncoder(int width, int height, int fps, int bitrate_kbps, Codec codec)
    : width_(width), height_(height), fps_(fps > 0 ? fps : 30),
      bitrate_kbps_(bitrate_kbps > 0 ? bitrate_kbps : 2000),
      codec_(codec)
{
}

VpxEncoder::~VpxEncoder()
{
    close();
}

bool VpxEncoder::init(EncodedCallback cb)
{
    callback_ = std::move(cb);

    const vpx_codec_iface_t *iface = (codec_ == Codec::VP9)
        ? vpx_codec_vp9_cx() : vpx_codec_vp8_cx();

    vpx_codec_enc_cfg_t cfg;
    vpx_codec_err_t res = vpx_codec_enc_config_default(iface, &cfg, 0);
    if (res != VPX_CODEC_OK) return false;

    cfg.g_w = static_cast<unsigned int>(width_);
    cfg.g_h = static_cast<unsigned int>(height_);
    cfg.g_timebase.num = 1;
    cfg.g_timebase.den = 1000000; /* microseconds */
    cfg.rc_target_bitrate = static_cast<unsigned int>(bitrate_kbps_);
    cfg.g_threads = 4;
    cfg.g_lag_in_frames = 0;           /* realtime — no look-ahead */
    cfg.rc_end_usage = VPX_CBR;
    cfg.kf_max_dist = fps_ * 2;       /* keyframe every 2 seconds */
    cfg.kf_min_dist = 0;
    cfg.g_error_resilient = VPX_ERROR_RESILIENT_DEFAULT;

    res = vpx_codec_enc_init(&codec_ctx_, iface, &cfg, 0);
    if (res != VPX_CODEC_OK) return false;

    codec_init_ = true;

    /* Realtime speed settings */
    if (codec_ == Codec::VP9) {
        vpx_codec_control(&codec_ctx_, VP8E_SET_CPUUSED, 6);
        vpx_codec_control(&codec_ctx_, VP9E_SET_ROW_MT, 1);
    } else {
        vpx_codec_control(&codec_ctx_, VP8E_SET_CPUUSED, -6);
    }

    frame_count_ = 0;
    return true;
}

bool VpxEncoder::encode(const VideoFrame& frame)
{
    if (!codec_init_ || !callback_) return false;

    vpx_image_t img;
    vpx_img_alloc(&img, VPX_IMG_FMT_I420, static_cast<unsigned int>(width_),
                  static_cast<unsigned int>(height_), 16);

    bgra_to_i420(frame.data, frame.stride, frame.width, frame.height, &img);

    int64_t pts_us = frame_count_ * 1000000LL / fps_;
    int64_t duration_us = 1000000LL / fps_;

    vpx_codec_err_t res = vpx_codec_encode(&codec_ctx_, &img,
                                            pts_us, duration_us,
                                            0, VPX_DL_REALTIME);
    vpx_img_free(&img);

    if (res != VPX_CODEC_OK) return false;

    /* Retrieve encoded packets */
    const vpx_codec_cx_pkt_t *pkt = nullptr;
    vpx_codec_iter_t iter = nullptr;

    while ((pkt = vpx_codec_get_cx_data(&codec_ctx_, &iter)) != nullptr) {
        if (pkt->kind == VPX_CODEC_CX_FRAME_PKT) {
            bool is_key = (pkt->data.frame.flags & VPX_FRAME_IS_KEY) != 0;
            callback_(static_cast<const uint8_t*>(pkt->data.frame.buf),
                      pkt->data.frame.sz,
                      pts_us, is_key);
        }
    }

    frame_count_++;
    return true;
}

void VpxEncoder::flush()
{
    if (!codec_init_) return;

    /* Flush by encoding nullptr */
    vpx_codec_encode(&codec_ctx_, nullptr, 0, 0, 0, VPX_DL_REALTIME);

    const vpx_codec_cx_pkt_t *pkt = nullptr;
    vpx_codec_iter_t iter = nullptr;
    while ((pkt = vpx_codec_get_cx_data(&codec_ctx_, &iter)) != nullptr) {
        if (pkt->kind == VPX_CODEC_CX_FRAME_PKT) {
            bool is_key = (pkt->data.frame.flags & VPX_FRAME_IS_KEY) != 0;
            int64_t pts_us = frame_count_ * 1000000LL / fps_;
            callback_(static_cast<const uint8_t*>(pkt->data.frame.buf),
                      pkt->data.frame.sz,
                      pts_us, is_key);
        }
    }
}

void VpxEncoder::close()
{
    if (codec_init_) {
        vpx_codec_destroy(&codec_ctx_);
        codec_init_ = false;
    }
    callback_ = nullptr;
}

void VpxEncoder::bgra_to_i420(const uint8_t* bgra, int stride,
                               int w, int h, vpx_image_t* img)
{
    /* Simple BGRA → I420 conversion (BT.601) */
    int yw = img->stride[VPX_PLANE_Y];
    int uw = img->stride[VPX_PLANE_U];
    int vw = img->stride[VPX_PLANE_V];
    uint8_t *y_plane = img->planes[VPX_PLANE_Y];
    uint8_t *u_plane = img->planes[VPX_PLANE_U];
    uint8_t *v_plane = img->planes[VPX_PLANE_V];

    for (int row = 0; row < h; row++) {
        const uint8_t *src = bgra + row * stride;
        for (int col = 0; col < w; col++) {
            uint8_t b = src[col * 4 + 0];
            uint8_t g = src[col * 4 + 1];
            uint8_t r = src[col * 4 + 2];

            /* Y = 0.299R + 0.587G + 0.114B */
            int y = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
            y_plane[row * yw + col] = static_cast<uint8_t>(y < 0 ? 0 : (y > 255 ? 255 : y));

            if ((row & 1) == 0 && (col & 1) == 0) {
                int cr = (row / 2) * uw + (col / 2);
                int cv = (row / 2) * vw + (col / 2);
                /* U = -0.169R - 0.331G + 0.500B + 128 */
                int u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
                /* V =  0.500R - 0.419G - 0.081B + 128 */
                int v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
                u_plane[cr] = static_cast<uint8_t>(u < 0 ? 0 : (u > 255 ? 255 : u));
                v_plane[cv] = static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
            }
        }
    }
}

} // namespace mc1

#endif // HAVE_VPX
