/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * video/video_file_source_windows.cpp — Media Foundation video file decode stub
 *
 * Windows API: Media Foundation IMFSourceReader for video file decoding.
 *              Supports MP4/MKV/AVI/WMV via system-installed Media Foundation codecs.
 *              Output format: MFVideoFormat_RGB32 (BGRA) for VideoFrame compatibility.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "video_file_source_windows.h"

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

    // TODO: Implement with Windows API (Media Foundation)
    // 1. MFCreateSourceReaderFromURL(file_path_.c_str(), ...) → IMFSourceReader
    // 2. Configure first video stream output to MFVideoFormat_RGB32
    // 3. Set width_/height_ from actual video dimensions if needed
    // 4. Start decode_thread_ running decode_loop()

    return false; // stub: always fails until implemented
}

void VideoFileSource::stop()
{
    if (!running_.load()) return;

    running_.store(false);

    if (decode_thread_.joinable())
        decode_thread_.join();

    // TODO: Release IMFSourceReader
    source_reader_ = nullptr;
    callback_ = nullptr;
}

std::string VideoFileSource::name() const
{
    // Extract filename from path
    auto pos = file_path_.find_last_of("/\\");
    if (pos != std::string::npos)
        return file_path_.substr(pos + 1);
    return file_path_;
}

void VideoFileSource::decode_loop()
{
    // TODO: Implement with Windows API (Media Foundation)
    // Loop while running_:
    //   1. IMFSourceReader::ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, ...)
    //   2. If end-of-stream: finished_.store(true); break
    //   3. IMFSample::ConvertToContiguousBuffer → IMFMediaBuffer
    //   4. Lock buffer, build VideoFrame (BGRA data, width_, height_, stride)
    //   5. callback_(frame)
    //   6. Pace to fps_ using steady_clock sleep
}

} // namespace mc1
