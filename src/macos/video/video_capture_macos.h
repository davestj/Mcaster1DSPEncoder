/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/video_capture_macos.h — AVFoundation camera capture
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_VIDEO_CAPTURE_MACOS_H
#define MC1_VIDEO_CAPTURE_MACOS_H

#include "video_source.h"
#include <atomic>
#include <string>

namespace mc1 {

struct CameraDeviceInfo {
    int         index;
    std::string name;
    std::string unique_id;
    bool        is_front;
};

class CameraSource : public VideoSource {
public:
    explicit CameraSource(int device_index = 0,
                          int width        = 1280,
                          int height       = 720,
                          int fps          = 30);
    ~CameraSource() override;

    bool start(VideoCallback cb) override;
    void stop()                  override;
    bool is_running() const      override { return running_.load(); }

    int width()  const override { return width_;  }
    int height() const override { return height_; }
    int fps()    const override { return fps_;    }
    std::string name() const override { return device_name_; }

    static std::vector<CameraDeviceInfo> enumerate_devices();
    static void request_permission();

private:
    int               device_index_;
    int               width_;
    int               height_;
    int               fps_;
    std::string       device_name_;
    std::atomic<bool> running_{false};
    VideoCallback     callback_;

    void *capture_session_ = nullptr; /* AVCaptureSession* */
    void *video_delegate_  = nullptr; /* MC1VideoOutputDelegate* */
};

} // namespace mc1

#endif // MC1_VIDEO_CAPTURE_MACOS_H
