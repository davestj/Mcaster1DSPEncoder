/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/screen_capture_source.h — ScreenCaptureKit display capture as VideoSource
 *
 * Captures a full display using macOS ScreenCaptureKit (macOS 13+).
 * Delivers BGRA frames via the VideoSource callback interface.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_SCREEN_CAPTURE_SOURCE_H
#define MC1_SCREEN_CAPTURE_SOURCE_H

#include "video_source.h"
#include <atomic>
#include <string>
#include <vector>

namespace mc1 {

struct ScreenInfo {
    uint32_t    display_id = 0;
    std::string name;
    int         width  = 0;
    int         height = 0;
};

class ScreenCaptureSource : public VideoSource {
public:
    explicit ScreenCaptureSource(uint32_t display_id = 0,
                                  int w = 1920, int h = 1080, int fps = 30);
    ~ScreenCaptureSource() override;

    bool start(VideoCallback cb) override;
    void stop() override;
    bool is_running() const override { return running_.load(); }

    int width()  const override { return width_; }
    int height() const override { return height_; }
    int fps()    const override { return fps_; }
    std::string name() const override;

    static std::vector<ScreenInfo> enumerate_displays();
    static bool is_available();

private:
    uint32_t display_id_;
    int width_;
    int height_;
    int fps_;
    std::atomic<bool> running_{false};

    /* Opaque Obj-C pointers (SCStream, delegate, etc.) */
    void *sc_stream_  = nullptr;
    void *sc_delegate_ = nullptr;

    VideoCallback callback_;
};

} // namespace mc1

#endif // MC1_SCREEN_CAPTURE_SOURCE_H
