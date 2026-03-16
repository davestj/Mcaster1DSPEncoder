/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * video/video_file_source_windows.cpp — Media Foundation video file decoder
 *
 * Decodes video files (MP4, MKV, AVI, WMV, MOV) using Media Foundation
 * Source Reader. Outputs BGRA frames at the target FPS via VideoCallback.
 *
 * Windows API: IMFSourceReader (MFCreateSourceReaderFromURL)
 * Supports all codecs with installed Media Foundation decoders (H.264,
 * H.265, VP8, VP9, AV1, MPEG-2, WMV, etc.)
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
#include <mfreadwrite.h>
#include <mferror.h>
#include <cstdio>
#include <chrono>
#include <thread>

#include "video_file_source_windows.h"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace mc1 {

VideoFileSource::VideoFileSource(const std::string& path, int w, int h, int fps)
    : file_path_(path)
    , width_(w)
    , height_(h)
    , fps_(fps)
{
}

VideoFileSource::~VideoFileSource()
{
    stop();
}

bool VideoFileSource::start(VideoCallback cb)
{
    if (running_.load()) return false;

    callback_ = std::move(cb);
    finished_.store(false);

    /* Initialize Media Foundation */
    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        fprintf(stderr, "[MF-File] MFStartup failed: 0x%08lx\n", hr);
        return false;
    }

    /* Convert path to wide string */
    int wlen = MultiByteToWideChar(CP_UTF8, 0, file_path_.c_str(), -1, nullptr, 0);
    std::wstring wpath(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, file_path_.c_str(), -1, wpath.data(), wlen);

    /* Create source reader with video processing enabled */
    IMFAttributes *attrs = nullptr;
    MFCreateAttributes(&attrs, 2);
    if (attrs) {
        attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    }

    IMFSourceReader *reader = nullptr;
    hr = MFCreateSourceReaderFromURL(wpath.c_str(), attrs, &reader);
    if (attrs) attrs->Release();

    if (FAILED(hr) || !reader) {
        fprintf(stderr, "[MF-File] Failed to open '%s': 0x%08lx\n",
                file_path_.c_str(), hr);
        MFShutdown();
        return false;
    }

    /* Configure output to RGB32 (BGRA) */
    IMFMediaType *output_type = nullptr;
    MFCreateMediaType(&output_type);
    if (output_type) {
        output_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        output_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        MFSetAttributeSize(output_type, MF_MT_FRAME_SIZE, width_, height_);

        hr = reader->SetCurrentMediaType(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, output_type);

        if (FAILED(hr)) {
            /* Retry without forcing resolution — let MF scale */
            output_type->DeleteItem(MF_MT_FRAME_SIZE);
            hr = reader->SetCurrentMediaType(
                MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, output_type);
        }
        output_type->Release();
    }

    if (FAILED(hr)) {
        fprintf(stderr, "[MF-File] Failed to set RGB32 output: 0x%08lx\n", hr);
        reader->Release();
        MFShutdown();
        return false;
    }

    /* Read back actual output dimensions */
    IMFMediaType *actual_type = nullptr;
    hr = reader->GetCurrentMediaType(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actual_type);
    if (SUCCEEDED(hr) && actual_type) {
        UINT32 w = 0, h = 0;
        MFGetAttributeSize(actual_type, MF_MT_FRAME_SIZE, &w, &h);
        if (w > 0 && h > 0) {
            width_ = static_cast<int>(w);
            height_ = static_cast<int>(h);
        }
        actual_type->Release();
    }

    source_reader_ = reader;
    running_.store(true);
    decode_thread_ = std::thread(&VideoFileSource::decode_loop, this);

    fprintf(stderr, "[MF-File] Decoding '%s' (%dx%d @ %d fps)\n",
            name().c_str(), width_, height_, fps_);
    return true;
}

void VideoFileSource::stop()
{
    if (!running_.load()) return;

    running_.store(false);

    if (decode_thread_.joinable())
        decode_thread_.join();

    auto *reader = static_cast<IMFSourceReader*>(source_reader_);
    if (reader) reader->Release();
    source_reader_ = nullptr;

    MFShutdown();
    callback_ = nullptr;

    fprintf(stderr, "[MF-File] Decode stopped\n");
}

std::string VideoFileSource::name() const
{
    auto pos = file_path_.find_last_of("/\\");
    if (pos != std::string::npos)
        return file_path_.substr(pos + 1);
    return file_path_;
}

void VideoFileSource::decode_loop()
{
    auto *reader = static_cast<IMFSourceReader*>(source_reader_);
    if (!reader) return;

    auto frame_interval = std::chrono::microseconds(1000000 / fps_);

    while (running_.load()) {
        auto frame_start = std::chrono::steady_clock::now();

        DWORD flags = 0;
        LONGLONG timestamp = 0;
        IMFSample *sample = nullptr;

        HRESULT hr = reader->ReadSample(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0, nullptr, &flags, &timestamp, &sample);

        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
            finished_.store(true);
            if (sample) sample->Release();
            break;
        }

        if (flags & MF_SOURCE_READERF_STREAMTICK) {
            if (sample) sample->Release();
            continue;
        }

        if (sample && callback_) {
            IMFMediaBuffer *buffer = nullptr;
            hr = sample->ConvertToContiguousBuffer(&buffer);

            if (SUCCEEDED(hr) && buffer) {
                BYTE *data = nullptr;
                DWORD max_len = 0, cur_len = 0;
                hr = buffer->Lock(&data, &max_len, &cur_len);

                if (SUCCEEDED(hr) && data) {
                    VideoFrame frame;
                    frame.data = data;
                    frame.data_len = cur_len;
                    frame.width = width_;
                    frame.height = height_;
                    frame.stride = width_ * 4; /* BGRA */
                    frame.pixel_format = 0;
                    frame.pts_us = timestamp / 10; /* 100-ns units → microseconds */

                    callback_(frame);

                    buffer->Unlock();
                }
                buffer->Release();
            }
            sample->Release();
        }

        /* Pace to target FPS */
        auto elapsed = std::chrono::steady_clock::now() - frame_start;
        if (elapsed < frame_interval) {
            std::this_thread::sleep_for(frame_interval - elapsed);
        }
    }
}

} // namespace mc1
