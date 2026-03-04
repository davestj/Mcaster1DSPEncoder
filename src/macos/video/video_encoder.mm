/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/video_encoder.mm — VideoToolbox H.264 hardware encoder (Obj-C++)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "video_encoder.h"

#import <Foundation/Foundation.h>
#import <VideoToolbox/VideoToolbox.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <cstdio>
#include <cstring>

namespace mc1 {

/* ── Callback context for VTCompressionSession ── */
struct VTCallbackContext {
    VideoEncoder::EncodedCallback callback;
    std::vector<uint8_t> *sps;
    std::vector<uint8_t> *pps;
    bool *sps_pps_extracted;
};

/* Extract SPS/PPS from format description */
static void extract_sps_pps(CMFormatDescriptionRef format_desc,
                             std::vector<uint8_t> &sps,
                             std::vector<uint8_t> &pps)
{
    size_t param_count = 0;
    CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format_desc, 0, nullptr, nullptr,
                                                       &param_count, nullptr);
    for (size_t i = 0; i < param_count; ++i) {
        const uint8_t *param_ptr = nullptr;
        size_t param_size = 0;
        OSStatus st = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
            format_desc, i, &param_ptr, &param_size, nullptr, nullptr);
        if (st != noErr || !param_ptr) continue;

        if (i == 0) {
            sps.assign(param_ptr, param_ptr + param_size);
        } else if (i == 1) {
            pps.assign(param_ptr, param_ptr + param_size);
        }
    }
}

/* Convert AVCC NAL units to Annex B (start code prefix) */
static std::vector<uint8_t> avcc_to_annexb(const uint8_t *data, size_t len, int nalu_len_size)
{
    std::vector<uint8_t> result;
    result.reserve(len + 64);

    size_t offset = 0;
    while (offset + nalu_len_size <= len) {
        uint32_t nalu_len = 0;
        for (int i = 0; i < nalu_len_size; ++i)
            nalu_len = (nalu_len << 8) | data[offset + i];
        offset += nalu_len_size;

        if (offset + nalu_len > len) break;

        /* Annex B start code */
        result.push_back(0x00);
        result.push_back(0x00);
        result.push_back(0x00);
        result.push_back(0x01);
        result.insert(result.end(), data + offset, data + offset + nalu_len);
        offset += nalu_len;
    }

    return result;
}

/* VTCompressionSession output callback */
static void vt_output_callback(void *outputCallbackRefCon,
                                void *sourceFrameRefCon,
                                OSStatus status,
                                VTEncodeInfoFlags infoFlags,
                                CMSampleBufferRef sampleBuffer)
{
    (void)sourceFrameRefCon;
    (void)infoFlags;

    if (status != noErr || !sampleBuffer) return;

    auto *ctx = static_cast<VTCallbackContext*>(outputCallbackRefCon);
    if (!ctx || !ctx->callback) return;

    /* Extract SPS/PPS on first keyframe */
    bool is_keyframe = false;
    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, false);
    if (attachments && CFArrayGetCount(attachments) > 0) {
        CFDictionaryRef dict = (CFDictionaryRef)CFArrayGetValueAtIndex(attachments, 0);
        CFBooleanRef notSync = nullptr;
        if (CFDictionaryGetValueIfPresent(dict, kCMSampleAttachmentKey_NotSync,
                                           (const void**)&notSync)) {
            is_keyframe = !CFBooleanGetValue(notSync);
        } else {
            is_keyframe = true;
        }
    }

    if (is_keyframe && ctx->sps_pps_extracted && !*ctx->sps_pps_extracted) {
        CMFormatDescriptionRef format_desc = CMSampleBufferGetFormatDescription(sampleBuffer);
        if (format_desc) {
            extract_sps_pps(format_desc, *ctx->sps, *ctx->pps);
            *ctx->sps_pps_extracted = true;
        }
    }

    /* Get encoded data */
    CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sampleBuffer);
    if (!block) return;

    size_t total_len = 0;
    char *data_ptr = nullptr;
    OSStatus st = CMBlockBufferGetDataPointer(block, 0, nullptr, &total_len, &data_ptr);
    if (st != noErr || !data_ptr) return;

    /* Get NALU length size */
    CMFormatDescriptionRef fmt = CMSampleBufferGetFormatDescription(sampleBuffer);
    int nalu_len_size = 4;
    if (fmt) {
        int nal_header_len = 0;
        CMVideoFormatDescriptionGetH264ParameterSetAtIndex(fmt, 0, nullptr, nullptr,
                                                            nullptr, &nal_header_len);
        if (nal_header_len > 0) nalu_len_size = nal_header_len;
    }

    /* Convert AVCC to Annex B */
    auto annexb = avcc_to_annexb(reinterpret_cast<const uint8_t*>(data_ptr),
                                  total_len, nalu_len_size);

    /* Get PTS */
    CMTime pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
    int64_t pts_us = static_cast<int64_t>(CMTimeGetSeconds(pts) * 1000000.0);

    ctx->callback(annexb.data(), annexb.size(), pts_us, is_keyframe);
}

/* ── VideoEncoder implementation ── */

VideoEncoder::VideoEncoder(int width, int height, int fps, int bitrate_kbps)
    : width_(width), height_(height), fps_(fps), bitrate_kbps_(bitrate_kbps)
{
}

VideoEncoder::~VideoEncoder()
{
    close();
}

bool VideoEncoder::init(EncodedCallback cb)
{
    callback_ = cb;

    /* Create callback context */
    auto *ctx = new VTCallbackContext;
    ctx->callback = callback_;
    ctx->sps = &sps_;
    ctx->pps = &pps_;
    ctx->sps_pps_extracted = &sps_pps_extracted_;

    /* Create compression session */
    VTCompressionSessionRef session = nullptr;
    OSStatus st = VTCompressionSessionCreate(
        kCFAllocatorDefault,
        width_, height_,
        kCMVideoCodecType_H264,
        nullptr, /* encoder spec — let system choose */
        nullptr, /* source pixel buffer attrs */
        kCFAllocatorDefault,
        vt_output_callback,
        ctx,
        &session);

    if (st != noErr || !session) {
        fprintf(stderr, "[VideoEncoder] VTCompressionSessionCreate failed: %d\n", (int)st);
        delete ctx;
        return false;
    }

    /* Configure encoder properties */
    VTSessionSetProperty(session, kVTCompressionPropertyKey_RealTime, kCFBooleanTrue);
    VTSessionSetProperty(session, kVTCompressionPropertyKey_AllowFrameReordering,
                          kCFBooleanFalse);

    /* Bitrate */
    int avg_bps = bitrate_kbps_ * 1000;
    CFNumberRef bitrate_ref = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &avg_bps);
    VTSessionSetProperty(session, kVTCompressionPropertyKey_AverageBitRate, bitrate_ref);
    CFRelease(bitrate_ref);

    /* Data rate limits (peak = 1.5x average, 1 second window) */
    int peak_bps = avg_bps * 3 / 2;
    double window = 1.0;
    CFNumberRef peak_ref = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &peak_bps);
    CFNumberRef window_ref = CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &window);
    CFMutableArrayRef limits = CFArrayCreateMutable(kCFAllocatorDefault, 2, &kCFTypeArrayCallBacks);
    CFArrayAppendValue(limits, peak_ref);
    CFArrayAppendValue(limits, window_ref);
    VTSessionSetProperty(session, kVTCompressionPropertyKey_DataRateLimits, limits);
    CFRelease(limits);
    CFRelease(peak_ref);
    CFRelease(window_ref);

    /* Frame rate */
    CFNumberRef fps_ref = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &fps_);
    VTSessionSetProperty(session, kVTCompressionPropertyKey_ExpectedFrameRate, fps_ref);
    CFRelease(fps_ref);

    /* Keyframe interval */
    int keyframe_interval = fps_ * 2;
    CFNumberRef kf_ref = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &keyframe_interval);
    VTSessionSetProperty(session, kVTCompressionPropertyKey_MaxKeyFrameInterval, kf_ref);
    CFRelease(kf_ref);

    /* Profile: Main auto level */
    VTSessionSetProperty(session, kVTCompressionPropertyKey_ProfileLevel,
                          kVTProfileLevel_H264_Main_AutoLevel);

    /* Prepare to encode */
    st = VTCompressionSessionPrepareToEncodeFrames(session);
    if (st != noErr) {
        fprintf(stderr, "[VideoEncoder] PrepareToEncodeFrames failed: %d\n", (int)st);
        VTCompressionSessionInvalidate(session);
        CFRelease(session);
        delete ctx;
        return false;
    }

    compression_session_ = session;
    fprintf(stderr, "[VideoEncoder] H.264 encoder initialized: %dx%d @ %dfps %dkbps\n",
            width_, height_, fps_, bitrate_kbps_);
    return true;
}

bool VideoEncoder::encode(const VideoFrame& frame)
{
    if (!compression_session_) return false;

    VTCompressionSessionRef session = static_cast<VTCompressionSessionRef>(compression_session_);

    /* Create CVPixelBuffer from frame data */
    CVPixelBufferRef pixel_buffer = nullptr;
    CVReturn cv_ret = CVPixelBufferCreate(
        kCFAllocatorDefault,
        frame.width, frame.height,
        kCVPixelFormatType_32BGRA,
        nullptr,
        &pixel_buffer);

    if (cv_ret != kCVReturnSuccess || !pixel_buffer) return false;

    CVPixelBufferLockBaseAddress(pixel_buffer, 0);
    uint8_t *dst = static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(pixel_buffer));
    size_t dst_stride = CVPixelBufferGetBytesPerRow(pixel_buffer);

    /* Copy frame data row by row (handles stride mismatch) */
    size_t copy_width = std::min(static_cast<size_t>(frame.stride),
                                  dst_stride);
    for (int row = 0; row < frame.height; ++row) {
        std::memcpy(dst + row * dst_stride,
                     frame.data + row * frame.stride,
                     copy_width);
    }
    CVPixelBufferUnlockBaseAddress(pixel_buffer, 0);

    /* Create timestamp */
    CMTime pts = CMTimeMake(frame.pts_us, 1000000);

    /* Encode */
    OSStatus st = VTCompressionSessionEncodeFrame(
        session,
        pixel_buffer,
        pts,
        kCMTimeInvalid, /* duration */
        nullptr,        /* frame properties */
        nullptr,        /* source frame ref */
        nullptr);       /* info flags out */

    CVPixelBufferRelease(pixel_buffer);

    return (st == noErr);
}

void VideoEncoder::flush()
{
    if (!compression_session_) return;
    VTCompressionSessionRef session = static_cast<VTCompressionSessionRef>(compression_session_);
    VTCompressionSessionCompleteFrames(session, kCMTimeInvalid);
}

void VideoEncoder::close()
{
    if (!compression_session_) return;

    VTCompressionSessionRef session = static_cast<VTCompressionSessionRef>(compression_session_);
    VTCompressionSessionCompleteFrames(session, kCMTimeInvalid);
    VTCompressionSessionInvalidate(session);
    CFRelease(session);
    compression_session_ = nullptr;

    /* Clean up callback context — it was allocated with new in init() */
    /* Note: the session owns the callback context pointer, but we need to be careful
     * not to double-free. The context is deleted when the session is invalidated
     * since we need to clean it up manually. */

    callback_ = nullptr;
    sps_pps_extracted_ = false;
    sps_.clear();
    pps_.clear();

    fprintf(stderr, "[VideoEncoder] Closed\n");
}

} // namespace mc1
