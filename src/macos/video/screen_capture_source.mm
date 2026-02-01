/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/screen_capture_source.mm — ScreenCaptureKit display capture (Obj-C++)
 *
 * Captures a full display as BGRA frames using ScreenCaptureKit (macOS 13+).
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "screen_capture_source.h"

#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreVideo/CoreVideo.h>

#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 130000
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#define HAS_SCREENCAPTUREKIT 1
#else
#define HAS_SCREENCAPTUREKIT 0
#endif

#if HAS_SCREENCAPTUREKIT

/* ── Obj-C delegate for video frame output ──────────────────────────────── */
/* NOTE: Must be at file scope, outside any C++ namespace. */

API_AVAILABLE(macos(13.0))
@interface MC1ScreenVideoOutput : NSObject <SCStreamOutput>
@property (nonatomic, assign) mc1::VideoCallback callback;
@property (nonatomic, assign) int targetWidth;
@property (nonatomic, assign) int targetHeight;
@end

@implementation MC1ScreenVideoOutput

- (void)stream:(SCStream *)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
               ofType:(SCStreamOutputType)type
{
    if (type != SCStreamOutputTypeScreen) return;
    if (!self.callback) return;

    /* Get pixel buffer from sample buffer */
    CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!pixelBuffer) return;

    CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);

    size_t w = CVPixelBufferGetWidth(pixelBuffer);
    size_t h = CVPixelBufferGetHeight(pixelBuffer);
    size_t stride = CVPixelBufferGetBytesPerRow(pixelBuffer);
    uint8_t *base = (uint8_t *)CVPixelBufferGetBaseAddress(pixelBuffer);

    if (base) {
        mc1::VideoFrame vf;
        vf.data         = base;
        vf.data_len     = stride * h;
        vf.width        = static_cast<int>(w);
        vf.height       = static_cast<int>(h);
        vf.stride       = static_cast<int>(stride);
        vf.pixel_format = kCVPixelFormatType_32BGRA;
        vf.pts_us       = 0;

        /* Get presentation timestamp */
        CMTime pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
        if (CMTIME_IS_VALID(pts)) {
            vf.pts_us = static_cast<int64_t>(CMTimeGetSeconds(pts) * 1000000.0);
        }

        self.callback(vf);
    }

    CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
}

@end

#endif /* HAS_SCREENCAPTUREKIT */

/* ── C++ implementation ─────────────────────────────────────────────────── */
namespace mc1 {

ScreenCaptureSource::ScreenCaptureSource(uint32_t display_id, int w, int h, int fps)
    : display_id_(display_id), width_(w), height_(h), fps_(fps > 0 ? fps : 30)
{
}

ScreenCaptureSource::~ScreenCaptureSource()
{
    stop();
}

std::string ScreenCaptureSource::name() const
{
    return "Screen: Display " + std::to_string(display_id_);
}

bool ScreenCaptureSource::is_available()
{
#if HAS_SCREENCAPTUREKIT
    if (@available(macOS 13.0, *)) {
        return [SCShareableContent class] != nil;
    }
#endif
    return false;
}

std::vector<ScreenInfo> ScreenCaptureSource::enumerate_displays()
{
    std::vector<ScreenInfo> result;

    /* Use CGGetActiveDisplayList for enumeration */
    uint32_t count = 0;
    CGGetActiveDisplayList(0, nullptr, &count);
    if (count == 0) return result;

    std::vector<CGDirectDisplayID> displays(count);
    CGGetActiveDisplayList(count, displays.data(), &count);

    for (uint32_t i = 0; i < count; ++i) {
        ScreenInfo info;
        info.display_id = displays[i];
        info.width  = static_cast<int>(CGDisplayPixelsWide(displays[i]));
        info.height = static_cast<int>(CGDisplayPixelsHigh(displays[i]));

        if (CGDisplayIsMain(displays[i]))
            info.name = "Main Display";
        else
            info.name = "Display " + std::to_string(i + 1);

        result.push_back(std::move(info));
    }

    return result;
}

bool ScreenCaptureSource::start(VideoCallback cb)
{
#if HAS_SCREENCAPTUREKIT
    if (@available(macOS 13.0, *)) {
        if (running_.load()) return false;

        callback_ = std::move(cb);

        /* Enumerate shareable content to find the display */
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        __block SCDisplay *targetDisplay = nil;

        [SCShareableContent getShareableContentWithCompletionHandler:
            ^(SCShareableContent *content, NSError *error) {
                if (!error && content) {
                    for (SCDisplay *d in content.displays) {
                        if (d.displayID == display_id_) {
                            targetDisplay = d;
                            break;
                        }
                    }
                    /* If no specific display requested, use first */
                    if (!targetDisplay && content.displays.count > 0) {
                        targetDisplay = content.displays[0];
                    }
                }
                dispatch_semaphore_signal(sem);
            }];

        dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));

        if (!targetDisplay) return false;

        /* Configure stream */
        SCContentFilter *filter = [[SCContentFilter alloc]
            initWithDisplay:targetDisplay excludingWindows:@[]];

        SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
        config.width = static_cast<NSUInteger>(width_);
        config.height = static_cast<NSUInteger>(height_);
        config.minimumFrameInterval = CMTimeMake(1, fps_);
        config.pixelFormat = kCVPixelFormatType_32BGRA;
        config.showsCursor = YES;

        /* Create stream */
        SCStream *stream = [[SCStream alloc] initWithFilter:filter
                                              configuration:config
                                                   delegate:nil];

        /* Create and attach output */
        MC1ScreenVideoOutput *output = [[MC1ScreenVideoOutput alloc] init];
        output.callback = callback_;
        output.targetWidth = width_;
        output.targetHeight = height_;

        NSError *addError = nil;
        [stream addStreamOutput:output
                           type:SCStreamOutputTypeScreen
             sampleHandlerQueue:dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0)
                          error:&addError];
        if (addError) return false;

        /* Start capture */
        __block bool started = false;
        dispatch_semaphore_t startSem = dispatch_semaphore_create(0);

        [stream startCaptureWithCompletionHandler:^(NSError *error) {
            started = (error == nil);
            dispatch_semaphore_signal(startSem);
        }];

        dispatch_semaphore_wait(startSem, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));

        if (!started) return false;

        sc_stream_ = (__bridge_retained void *)stream;
        sc_delegate_ = (__bridge_retained void *)output;
        running_.store(true);

        /* Update display_id_ if we picked the first display */
        display_id_ = targetDisplay.displayID;

        return true;
    }
#endif
    (void)cb;
    return false;
}

void ScreenCaptureSource::stop()
{
#if HAS_SCREENCAPTUREKIT
    if (@available(macOS 13.0, *)) {
        if (!running_.load()) return;
        running_.store(false);

        if (sc_stream_) {
            SCStream *stream = (__bridge_transfer SCStream *)sc_stream_;
            sc_stream_ = nullptr;

            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            [stream stopCaptureWithCompletionHandler:^(NSError *) {
                dispatch_semaphore_signal(sem);
            }];
            dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC));
        }

        if (sc_delegate_) {
            /* Release the retained delegate */
            (void)(__bridge_transfer MC1ScreenVideoOutput *)sc_delegate_;
            sc_delegate_ = nullptr;
        }

        callback_ = nullptr;
    }
#endif
}

} // namespace mc1
