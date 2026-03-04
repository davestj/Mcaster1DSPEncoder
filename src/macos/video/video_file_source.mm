/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/video_file_source.mm — Video file playback source (AVAssetReader stub)
 *
 * Structural stub — opens video files via AVAssetReader, sets up BGRA output.
 * Full decode loop is TODO for a later phase.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "video_file_source.h"

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>

#include <chrono>

namespace mc1 {

VideoFileSource::VideoFileSource(const std::string& path, int w, int h, int fps)
    : file_path_(path), width_(w), height_(h), fps_(fps > 0 ? fps : 30)
{
}

VideoFileSource::~VideoFileSource()
{
    stop();
}

std::string VideoFileSource::name() const
{
    auto pos = file_path_.rfind('/');
    if (pos != std::string::npos)
        return "Video: " + file_path_.substr(pos + 1);
    return "Video: " + file_path_;
}

bool VideoFileSource::start(VideoCallback cb)
{
    if (running_.load()) return false;

    callback_ = std::move(cb);

    /* Open asset */
    NSURL *fileURL = [NSURL fileURLWithPath:
        [NSString stringWithUTF8String:file_path_.c_str()]];

    AVAsset *asset = [AVAsset assetWithURL:fileURL];
    if (!asset) return false;

    NSError *error = nil;
    AVAssetReader *reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
    if (error || !reader) return false;

    /* Find video track */
    NSArray<AVAssetTrack *> *tracks = [asset tracksWithMediaType:AVMediaTypeVideo];
    if (tracks.count == 0) return false;

    AVAssetTrack *videoTrack = tracks[0];

    /* Configure BGRA output */
    NSDictionary *outputSettings = @{
        (NSString *)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
        (NSString *)kCVPixelBufferWidthKey  : @(width_),
        (NSString *)kCVPixelBufferHeightKey : @(height_),
    };

    AVAssetReaderTrackOutput *output =
        [[AVAssetReaderTrackOutput alloc] initWithTrack:videoTrack
                                         outputSettings:outputSettings];

    if (![reader canAddOutput:output]) return false;
    [reader addOutput:output];

    if (![reader startReading]) return false;

    asset_reader_ = (__bridge_retained void *)reader;
    track_output_ = (__bridge_retained void *)output;
    running_.store(true);
    finished_.store(false);

    /* Phase M10: Launch background decode thread */
    decode_thread_ = std::thread(&VideoFileSource::decode_loop, this);

    return true;
}

void VideoFileSource::stop()
{
    running_.store(false);

    if (decode_thread_.joinable())
        decode_thread_.join();

    if (asset_reader_) {
        AVAssetReader *reader = (__bridge_transfer AVAssetReader *)asset_reader_;
        asset_reader_ = nullptr;
        if (reader.status == AVAssetReaderStatusReading)
            [reader cancelReading];
    }

    if (track_output_) {
        (void)(__bridge_transfer AVAssetReaderTrackOutput *)track_output_;
        track_output_ = nullptr;
    }

    callback_ = nullptr;
}

void VideoFileSource::decode_loop()
{
    @autoreleasepool {
        AVAssetReaderTrackOutput *output =
            (__bridge AVAssetReaderTrackOutput *)track_output_;
        AVAssetReader *reader = (__bridge AVAssetReader *)asset_reader_;

        auto frame_interval = std::chrono::microseconds(1000000 / fps_);
        auto next_frame = std::chrono::steady_clock::now();

        while (running_.load()) {
            /* Check reader state */
            if (reader.status != AVAssetReaderStatusReading) {
                finished_.store(true);
                running_.store(false);
                break;
            }

            CMSampleBufferRef sampleBuffer = [output copyNextSampleBuffer];
            if (!sampleBuffer) {
                finished_.store(true);
                running_.store(false);
                break;
            }

            CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
            if (imageBuffer) {
                CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);

                const uint8_t *base =
                    static_cast<const uint8_t *>(CVPixelBufferGetBaseAddress(imageBuffer));
                size_t bpr = CVPixelBufferGetBytesPerRow(imageBuffer);
                size_t pw  = CVPixelBufferGetWidth(imageBuffer);
                size_t ph  = CVPixelBufferGetHeight(imageBuffer);

                if (base && callback_) {
                    VideoFrame frame;
                    frame.data   = base;
                    frame.width  = static_cast<int>(pw);
                    frame.height = static_cast<int>(ph);
                    frame.stride = static_cast<int>(bpr);
                    callback_(frame);
                }

                CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
            }

            CFRelease(sampleBuffer);

            /* Pace to target FPS */
            next_frame += frame_interval;
            auto now = std::chrono::steady_clock::now();
            if (next_frame > now) {
                std::this_thread::sleep_until(next_frame);
            } else {
                /* Falling behind — reset pacing */
                next_frame = now;
            }
        }
    }
}

} // namespace mc1
