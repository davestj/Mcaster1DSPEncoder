/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * video/video_encoder_windows.cpp — Media Foundation H.264 encoder
 *
 * Hardware-accelerated H.264 encoding via Media Foundation Transform (MFT).
 * Auto-detects NVENC, Intel QSV, or AMD AMF hardware encoders;
 * falls back to Microsoft H.264 Software Encoder MFT.
 *
 * Input: BGRA (MFVideoFormat_RGB32) VideoFrames
 * Output: H.264 NAL units via EncodedCallback
 *
 * Windows API: IMFTransform (MFT), MFTEnumEx
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <codecapi.h>
#include <cstdio>
#include <cstring>

#include "video_encoder_windows.h"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mf.lib")

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

    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        fprintf(stderr, "[MF-Enc] MFStartup failed: 0x%08lx\n", hr);
        return false;
    }

    /* Find H.264 encoder MFTs — prefer hardware */
    MFT_REGISTER_TYPE_INFO output_type;
    output_type.guidMajorType = MFMediaType_Video;
    output_type.guidSubtype = MFVideoFormat_H264;

    IMFActivate **activates = nullptr;
    UINT32 count = 0;

    hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
                   MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
                   nullptr, &output_type, &activates, &count);

    if (FAILED(hr) || count == 0) {
        /* Fall back to software encoders */
        hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
                       MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT |
                       MFT_ENUM_FLAG_SORTANDFILTER,
                       nullptr, &output_type, &activates, &count);
    }

    if (FAILED(hr) || count == 0) {
        fprintf(stderr, "[MF-Enc] No H.264 encoder MFT found\n");
        MFShutdown();
        return false;
    }

    /* Activate the first available encoder */
    IMFTransform *transform = nullptr;
    for (UINT32 i = 0; i < count; ++i) {
        hr = activates[i]->ActivateObject(__uuidof(IMFTransform),
                                           reinterpret_cast<void**>(&transform));
        if (SUCCEEDED(hr) && transform) {
            LPWSTR friendly_name = nullptr;
            UINT32 name_len = 0;
            activates[i]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute,
                                              &friendly_name, &name_len);
            if (friendly_name) {
                char name_utf8[256]{};
                WideCharToMultiByte(CP_UTF8, 0, friendly_name, -1,
                                    name_utf8, sizeof(name_utf8) - 1, nullptr, nullptr);
                fprintf(stderr, "[MF-Enc] Using encoder: %s\n", name_utf8);
                CoTaskMemFree(friendly_name);
            }
            break;
        }
    }

    /* Free activate objects */
    for (UINT32 i = 0; i < count; ++i)
        activates[i]->Release();
    CoTaskMemFree(activates);

    if (!transform) {
        fprintf(stderr, "[MF-Enc] Failed to activate encoder MFT\n");
        MFShutdown();
        return false;
    }

    /* Set output type: H.264 */
    IMFMediaType *out_mt = nullptr;
    MFCreateMediaType(&out_mt);
    if (!out_mt) { transform->Release(); MFShutdown(); return false; }

    out_mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    out_mt->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    out_mt->SetUINT32(MF_MT_AVG_BITRATE, static_cast<UINT32>(bitrate_kbps_) * 1000);
    MFSetAttributeSize(out_mt, MF_MT_FRAME_SIZE, width_, height_);
    MFSetAttributeRatio(out_mt, MF_MT_FRAME_RATE, fps_, 1);
    out_mt->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeRatio(out_mt, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    out_mt->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_Main);

    hr = transform->SetOutputType(0, out_mt, 0);
    out_mt->Release();

    if (FAILED(hr)) {
        fprintf(stderr, "[MF-Enc] SetOutputType failed: 0x%08lx\n", hr);
        transform->Release();
        MFShutdown();
        return false;
    }

    /* Set input type: RGB32 (BGRA) */
    IMFMediaType *in_mt = nullptr;
    MFCreateMediaType(&in_mt);
    if (!in_mt) { transform->Release(); MFShutdown(); return false; }

    in_mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    in_mt->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    MFSetAttributeSize(in_mt, MF_MT_FRAME_SIZE, width_, height_);
    MFSetAttributeRatio(in_mt, MF_MT_FRAME_RATE, fps_, 1);
    in_mt->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeRatio(in_mt, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

    hr = transform->SetInputType(0, in_mt, 0);
    in_mt->Release();

    if (FAILED(hr)) {
        fprintf(stderr, "[MF-Enc] SetInputType (RGB32) failed: 0x%08lx\n", hr);

        /* Some hardware encoders require NV12 — try that */
        MFCreateMediaType(&in_mt);
        if (in_mt) {
            in_mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            in_mt->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
            MFSetAttributeSize(in_mt, MF_MT_FRAME_SIZE, width_, height_);
            MFSetAttributeRatio(in_mt, MF_MT_FRAME_RATE, fps_, 1);
            in_mt->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
            MFSetAttributeRatio(in_mt, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
            hr = transform->SetInputType(0, in_mt, 0);
            in_mt->Release();
        }

        if (FAILED(hr)) {
            fprintf(stderr, "[MF-Enc] SetInputType (NV12 fallback) also failed\n");
            transform->Release();
            MFShutdown();
            return false;
        }
    }

    /* Notify streaming start */
    transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

    mft_encoder_ = transform;

    fprintf(stderr, "[MF-Enc] H.264 encoder ready: %dx%d @ %d kbps, %d fps\n",
            width_, height_, bitrate_kbps_, fps_);
    return true;
}

bool VideoEncoder::encode(const VideoFrame& frame)
{
    auto *transform = static_cast<IMFTransform*>(mft_encoder_);
    if (!transform || !callback_) return false;

    /* Create input sample from BGRA frame data */
    if (frame.stride <= 0 || frame.height <= 0 ||
        frame.stride > 32768 || frame.height > 8192 || !frame.data) {
        fprintf(stderr, "[MF-Enc] Invalid frame: stride=%d height=%d\n",
                frame.stride, frame.height);
        return false;
    }

    IMFMediaBuffer *in_buf = nullptr;
    DWORD buf_size = static_cast<DWORD>(
        static_cast<size_t>(frame.stride) * static_cast<size_t>(frame.height));
    HRESULT hr = MFCreateMemoryBuffer(buf_size, &in_buf);
    if (FAILED(hr) || !in_buf) return false;

    BYTE *buf_data = nullptr;
    hr = in_buf->Lock(&buf_data, nullptr, nullptr);
    if (SUCCEEDED(hr) && buf_data) {
        std::memcpy(buf_data, frame.data, buf_size);
        in_buf->Unlock();
        in_buf->SetCurrentLength(buf_size);
    }

    IMFSample *in_sample = nullptr;
    MFCreateSample(&in_sample);
    if (!in_sample) { in_buf->Release(); return false; }

    in_sample->AddBuffer(in_buf);
    in_buf->Release();

    /* Set timestamp */
    LONGLONG ts = frame.pts_us * 10; /* microseconds → 100-ns units */
    in_sample->SetSampleTime(ts);
    in_sample->SetSampleDuration(10000000LL / fps_); /* frame duration in 100-ns */

    /* Feed to encoder */
    hr = transform->ProcessInput(0, in_sample, 0);
    in_sample->Release();

    if (FAILED(hr)) return false;

    /* Drain output samples */
    MFT_OUTPUT_DATA_BUFFER out_data{};
    out_data.dwStreamID = 0;

    for (;;) {
        DWORD status = 0;

        /* Check if we need to provide output sample */
        MFT_OUTPUT_STREAM_INFO stream_info{};
        transform->GetOutputStreamInfo(0, &stream_info);

        IMFSample *out_sample = nullptr;
        if (!(stream_info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
            MFCreateSample(&out_sample);
            if (!out_sample) break;
            IMFMediaBuffer *out_buf = nullptr;
            DWORD alloc_size = stream_info.cbSize > 0 ? stream_info.cbSize : 1048576;
            if (FAILED(MFCreateMemoryBuffer(alloc_size, &out_buf)) || !out_buf) {
                out_sample->Release();
                break;
            }
            out_sample->AddBuffer(out_buf);
            out_buf->Release();
            out_data.pSample = out_sample;
        }

        hr = transform->ProcessOutput(0, 1, &out_data, &status);

        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
            if (out_sample) out_sample->Release();
            break;
        }

        if (FAILED(hr)) {
            if (out_sample) out_sample->Release();
            break;
        }

        /* Extract encoded H.264 data */
        IMFSample *result = out_data.pSample;
        if (result) {
            IMFMediaBuffer *result_buf = nullptr;
            result->ConvertToContiguousBuffer(&result_buf);

            if (result_buf) {
                BYTE *nalu_data = nullptr;
                DWORD nalu_len = 0;
                result_buf->Lock(&nalu_data, nullptr, &nalu_len);

                if (nalu_data && nalu_len > 0) {
                    LONGLONG sample_ts = 0;
                    result->GetSampleTime(&sample_ts);
                    int64_t pts = sample_ts / 10; /* 100-ns → microseconds */

                    /* Check for keyframe */
                    UINT32 is_key = 0;
                    result->GetUINT32(MFSampleExtension_CleanPoint, &is_key);

                    /* Extract SPS/PPS from first keyframe */
                    if (is_key && !sps_pps_extracted_) {
                        /* SPS/PPS are typically in the first H.264 NAL units */
                        for (DWORD i = 0; i + 4 < nalu_len; ++i) {
                            if (nalu_data[i] == 0 && nalu_data[i+1] == 0 &&
                                nalu_data[i+2] == 0 && nalu_data[i+3] == 1) {
                                if (i + 5 < nalu_len) {
                                    uint8_t type = nalu_data[i+4] & 0x1F;
                                    /* Find end of this NALU */
                                    DWORD end = nalu_len;
                                    for (DWORD j = i + 4; j + 3 < nalu_len; ++j) {
                                        if (nalu_data[j] == 0 && nalu_data[j+1] == 0 &&
                                            nalu_data[j+2] == 0 && nalu_data[j+3] == 1) {
                                            end = j;
                                            break;
                                        }
                                    }
                                    if (type == 7) /* SPS */
                                        sps_.assign(nalu_data + i + 4, nalu_data + end);
                                    else if (type == 8) /* PPS */
                                        pps_.assign(nalu_data + i + 4, nalu_data + end);
                                }
                            }
                        }
                        if (!sps_.empty() && !pps_.empty())
                            sps_pps_extracted_ = true;
                    }

                    callback_(nalu_data, nalu_len, pts, is_key != 0);
                }

                result_buf->Unlock();
                result_buf->Release();
            }
        }

        if (out_sample && out_sample != out_data.pSample)
            out_sample->Release();
        if (out_data.pSample)
            out_data.pSample->Release();

        out_data.pSample = nullptr;
    }

    return true;
}

void VideoEncoder::flush()
{
    auto *transform = static_cast<IMFTransform*>(mft_encoder_);
    if (!transform) return;

    transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);

    /* Read remaining output */
    MFT_OUTPUT_DATA_BUFFER out_data{};
    DWORD status = 0;

    MFT_OUTPUT_STREAM_INFO stream_info{};
    transform->GetOutputStreamInfo(0, &stream_info);

    for (;;) {
        IMFSample *out_sample = nullptr;
        if (!(stream_info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
            MFCreateSample(&out_sample);
            if (!out_sample) break;
            IMFMediaBuffer *out_buf = nullptr;
            DWORD alloc_size = stream_info.cbSize > 0 ? stream_info.cbSize : 1048576;
            if (FAILED(MFCreateMemoryBuffer(alloc_size, &out_buf)) || !out_buf) {
                out_sample->Release();
                break;
            }
            out_sample->AddBuffer(out_buf);
            out_buf->Release();
            out_data.pSample = out_sample;
        }

        HRESULT hr = transform->ProcessOutput(0, 1, &out_data, &status);

        if (FAILED(hr)) {
            if (out_sample) out_sample->Release();
            break;
        }

        if (out_data.pSample && callback_) {
            IMFMediaBuffer *buf = nullptr;
            out_data.pSample->ConvertToContiguousBuffer(&buf);
            if (buf) {
                BYTE *data = nullptr;
                DWORD len = 0;
                buf->Lock(&data, nullptr, &len);
                if (data && len > 0) {
                    LONGLONG ts = 0;
                    out_data.pSample->GetSampleTime(&ts);
                    UINT32 is_key = 0;
                    out_data.pSample->GetUINT32(MFSampleExtension_CleanPoint, &is_key);
                    callback_(data, len, ts / 10, is_key != 0);
                }
                buf->Unlock();
                buf->Release();
            }
        }

        if (out_sample && out_sample != out_data.pSample)
            out_sample->Release();
        if (out_data.pSample)
            out_data.pSample->Release();

        out_data.pSample = nullptr;
    }
}

void VideoEncoder::close()
{
    auto *transform = static_cast<IMFTransform*>(mft_encoder_);
    if (transform) {
        transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        transform->Release();
    }

    mft_encoder_ = nullptr;
    sps_.clear();
    pps_.clear();
    sps_pps_extracted_ = false;
    callback_ = nullptr;

    MFShutdown();
}

} // namespace mc1
