/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/video_source.h — Abstract video source interface
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_VIDEO_SOURCE_H
#define MC1_VIDEO_SOURCE_H

#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace mc1 {

struct VideoFrame {
    const uint8_t* data     = nullptr;
    size_t         data_len = 0;
    int            width    = 0;
    int            height   = 0;
    int            stride   = 0;
    uint32_t       pixel_format = 0; /* kCVPixelFormatType_32BGRA etc. */
    int64_t        pts_us   = 0;     /* presentation timestamp (microseconds) */
};

using VideoCallback = std::function<void(const VideoFrame&)>;

class VideoSource {
public:
    virtual ~VideoSource() = default;

    virtual bool start(VideoCallback cb) = 0;
    virtual void stop() = 0;
    virtual bool is_running() const = 0;

    virtual int width()  const = 0;
    virtual int height() const = 0;
    virtual int fps()    const = 0;
    virtual std::string name() const = 0;
};

} // namespace mc1

#endif // MC1_VIDEO_SOURCE_H
