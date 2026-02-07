/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/image_source.cpp — Static image as a repeating VideoSource
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "image_source.h"
#include <QImage>
#include <chrono>
#include <cstring>

namespace mc1 {

ImageSource::ImageSource(const std::string& image_path, int fps)
    : image_path_(image_path), fps_(fps > 0 ? fps : 30)
{
}

ImageSource::~ImageSource()
{
    stop();
}

std::string ImageSource::name() const
{
    /* Extract filename from path */
    auto pos = image_path_.rfind('/');
    if (pos != std::string::npos)
        return "Image: " + image_path_.substr(pos + 1);
    return "Image: " + image_path_;
}

bool ImageSource::load_image(const std::string& path)
{
    QImage img(QString::fromStdString(path));
    if (img.isNull()) return false;

    /* Convert to BGRA (Qt ARGB32 is BGRA in memory on little-endian) */
    img = img.convertToFormat(QImage::Format_ARGB32);

    width_  = img.width();
    height_ = img.height();
    stride_ = static_cast<int>(img.bytesPerLine());

    size_t sz = static_cast<size_t>(stride_) * static_cast<size_t>(height_);
    bgra_data_.resize(sz);
    std::memcpy(bgra_data_.data(), img.constBits(), sz);

    image_path_ = path;
    return true;
}

bool ImageSource::start(VideoCallback cb)
{
    if (running_.load()) return false;

    if (bgra_data_.empty()) {
        if (!load_image(image_path_))
            return false;
    }

    callback_ = std::move(cb);
    running_.store(true);
    thread_ = std::thread(&ImageSource::frame_loop, this);
    return true;
}

void ImageSource::stop()
{
    running_.store(false);
    if (thread_.joinable())
        thread_.join();
}

void ImageSource::frame_loop()
{
    using clock = std::chrono::steady_clock;
    auto frame_interval = std::chrono::microseconds(1000000 / fps_);
    auto next_frame = clock::now();
    int64_t pts = 0;

    while (running_.load()) {
        next_frame += frame_interval;

        VideoFrame vf;
        vf.data         = bgra_data_.data();
        vf.data_len     = bgra_data_.size();
        vf.width        = width_;
        vf.height       = height_;
        vf.stride       = stride_;
        vf.pixel_format = 'BGRA';
        vf.pts_us       = pts;

        if (callback_) callback_(vf);

        pts += static_cast<int64_t>(frame_interval.count());

        std::this_thread::sleep_until(next_frame);
    }
}

} // namespace mc1
