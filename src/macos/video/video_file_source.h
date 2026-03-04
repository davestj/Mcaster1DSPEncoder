/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/video_file_source.h — Video file playback source (AVAssetReader stub)
 *
 * Structural stub for video file playback via AVAssetReader.
 * Full decode loop to be implemented in Phase 2.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_VIDEO_FILE_SOURCE_H
#define MC1_VIDEO_FILE_SOURCE_H

#include "video_source.h"
#include <atomic>
#include <string>
#include <thread>

namespace mc1 {

class VideoFileSource : public VideoSource {
public:
    explicit VideoFileSource(const std::string& path,
                              int w = 1280, int h = 720, int fps = 30);
    ~VideoFileSource() override;

    bool start(VideoCallback cb) override;
    void stop() override;
    bool is_running() const override { return running_.load(); }

    int width()  const override { return width_; }
    int height() const override { return height_; }
    int fps()    const override { return fps_; }
    std::string name() const override;

    bool is_finished() const { return finished_.load(); }

private:
    std::string file_path_;
    int width_;
    int height_;
    int fps_;

    std::atomic<bool> running_{false};
    std::atomic<bool> finished_{false};

    /* Opaque AVFoundation pointers */
    void *asset_reader_  = nullptr;   /* AVAssetReader* */
    void *track_output_  = nullptr;   /* AVAssetReaderTrackOutput* */

    VideoCallback callback_;
    std::thread   decode_thread_;

    void decode_loop();
};

} // namespace mc1

#endif // MC1_VIDEO_FILE_SOURCE_H
