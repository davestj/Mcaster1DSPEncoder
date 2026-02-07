/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * video/video_capture_windows.cpp — Media Foundation camera capture stub
 *
 * Windows API: Media Foundation (MFCreateDeviceSource, IMFSourceReader,
 *              MFEnumDeviceSources for webcam enumeration)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "video_capture_windows.h"

namespace mc1 {

CameraSource::CameraSource(int device_index, int width, int height, int fps)
    : device_index_(device_index)
    , width_(width)
    , height_(height)
    , fps_(fps)
    , device_name_("Camera " + std::to_string(device_index))
{
}

CameraSource::~CameraSource()
{
    stop();
}

bool CameraSource::start(VideoCallback cb)
{
    if (running_.load()) return false;

    callback_ = std::move(cb);

    // TODO: Implement with Windows API (Media Foundation)
    // 1. MFCreateAttributes → set MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE to VIDEO
    // 2. MFCreateDeviceSource with selected device index
    // 3. MFCreateSourceReaderFromMediaSource → IMFSourceReader
    // 4. Configure output type to MFVideoFormat_RGB32 (BGRA)
    // 5. Start capture_thread_ running capture_loop()

    return false; // stub: always fails until implemented
}

void CameraSource::stop()
{
    if (!running_.load()) return;

    running_.store(false);

    if (capture_thread_.joinable())
        capture_thread_.join();

    // TODO: Release IMFSourceReader, IMFMediaSource
    source_reader_ = nullptr;
    callback_ = nullptr;
}

std::vector<CameraDeviceInfo> CameraSource::enumerate_devices()
{
    // TODO: Implement with Windows API (Media Foundation)
    // MFEnumDeviceSources with MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
    // Iterate results, extract friendly name + symbolic link
    return {};
}

void CameraSource::request_permission()
{
    // TODO: Implement with Windows API
    // Windows 10 1803+: check and request camera access via AppCapability API
    // Older Windows: no-op (camera access is always granted)
}

void CameraSource::capture_loop()
{
    // TODO: Implement with Windows API (Media Foundation)
    // Loop: IMFSourceReader::ReadSample() → IMFMediaBuffer → Lock → VideoFrame → callback_
    // Pace to target fps_ using steady_clock
}

} // namespace mc1
