/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/image_source.h — Static image as a repeating VideoSource
 *
 * Loads a PNG/JPEG image and delivers the same BGRA frame at the
 * configured FPS via a background thread.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_IMAGE_SOURCE_H
#define MC1_IMAGE_SOURCE_H

#include "video_source.h"
#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace mc1 {

class ImageSource : public VideoSource {
public:
    explicit ImageSource(const std::string& image_path, int fps = 30);
    ~ImageSource() override;

    bool start(VideoCallback cb) override;
    void stop() override;
    bool is_running() const override { return running_.load(); }

    int width()  const override { return width_; }
    int height() const override { return height_; }
    int fps()    const override { return fps_; }
    std::string name() const override;

    bool load_image(const std::string& path);

private:
    void frame_loop();

    std::string image_path_;
    int fps_;
    int width_  = 0;
    int height_ = 0;
    int stride_ = 0;

    std::vector<uint8_t> bgra_data_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    VideoCallback callback_;
};

} // namespace mc1

#endif // MC1_IMAGE_SOURCE_H
