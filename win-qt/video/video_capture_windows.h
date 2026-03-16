/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * video/video_capture_windows.h — Media Foundation camera capture
 *
 * Windows API: Media Foundation (IMFSourceReader for webcam capture)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_VIDEO_CAPTURE_WINDOWS_H
#define MC1_VIDEO_CAPTURE_WINDOWS_H

#include "video_source.h"
#include <atomic>
#include <string>
#include <thread>
#include <vector>

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

    /* Media Foundation handles (opaque — defined in .cpp) */
    void *media_source_  = nullptr;  /* IMFMediaSource*  */
    void *source_reader_ = nullptr;  /* IMFSourceReader* */
    bool  mf_started_    = false;    /* MFStartup called */
    int   stride_        = 0;        /* actual bytes-per-row (abs value) */
    bool  bottom_up_     = false;    /* true if camera outputs bottom-up frames */
    int   capture_fmt_   = 0;       /* 0=RGB32, 1=NV12, 2=YUY2 */
    std::vector<uint8_t> flip_buf_;  /* reusable buffer for bottom-up → top-down flip */
    std::vector<uint8_t> rgb_buf_;   /* NV12/YUY2 → BGRA conversion buffer */
    std::thread capture_thread_;

    void capture_loop();
};

} // namespace mc1

#endif // MC1_VIDEO_CAPTURE_WINDOWS_H
