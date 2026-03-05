/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * video/video_encoder_windows.cpp — Media Foundation H.264 encoder stub
 *
 * Windows API: Media Foundation Transform (IMFTransform) for H.264 encoding.
 *              Hardware-accelerated via Intel QSV, NVIDIA NVENC, or AMD AMF
 *              when available; falls back to Microsoft H.264 software MFT.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "video_encoder_windows.h"

namespace mc1 {

VideoEncoder::VideoEncoder(int width, int height, int fps, int bitrate_kbps)
    : width_(width)
    , height_(height)
    , fps_(fps)
    , bitrate_kbps_(bitrate_kbps)
{
}

VideoEncoder::~VideoEncoder()
{
    close();
}

bool VideoEncoder::init(EncodedCallback cb)
{
    callback_ = std::move(cb);

    // TODO: Implement with Windows API (Media Foundation)
    // 1. MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, ..., MFVideoFormat_H264, ...)
    //    → prefer hardware MFT (NVENC/QSV/AMF)
    // 2. CoCreateInstance → IMFTransform
    // 3. Set input type: MFVideoFormat_RGB32 (BGRA), width_, height_, fps_
    // 4. Set output type: MFVideoFormat_H264, bitrate_kbps_ * 1000
    // 5. ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING)

    return false; // stub: always fails until implemented
}

bool VideoEncoder::encode(const VideoFrame& /*frame*/)
{
    // TODO: Implement with Windows API (Media Foundation)
    // 1. Create IMFMediaBuffer from frame.data (MFCreateMemoryBuffer)
    // 2. Create IMFSample, add buffer, set timestamp (frame.pts_us)
    // 3. IMFTransform::ProcessInput(sample)
    // 4. IMFTransform::ProcessOutput → extract H.264 NALUs
    // 5. On first keyframe: extract SPS/PPS from output
    // 6. callback_(nalu_data, nalu_len, pts_us, is_keyframe)

    return false; // stub: always fails until implemented
}

void VideoEncoder::flush()
{
    // TODO: Implement with Windows API
    // IMFTransform::ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN)
    // Read remaining output samples
}

void VideoEncoder::close()
{
    // TODO: Implement with Windows API
    // Release IMFTransform
    mft_encoder_ = nullptr;
    sps_.clear();
    pps_.clear();
    sps_pps_extracted_ = false;
    callback_ = nullptr;
}

} // namespace mc1
