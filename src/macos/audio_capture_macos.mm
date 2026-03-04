/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * audio_capture_macos.mm — ScreenCaptureKit system audio capture (Obj-C++)
 *
 * Captures system audio output via ScreenCaptureKit (macOS 13+ Ventura).
 * Audio-only mode: captures all system audio without screen content.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "audio_capture_macos.h"

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>

/* ScreenCaptureKit requires macOS 13.0+ — guard with availability check */
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 130000
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#define HAS_SCREENCAPTUREKIT 1
#else
#define HAS_SCREENCAPTUREKIT 0
#endif

#if HAS_SCREENCAPTUREKIT

/* ── Obj-C delegate that receives audio sample buffers ──────────────────── */
/* NOTE: Obj-C declarations must be at file scope, outside any C++ namespace. */

API_AVAILABLE(macos(13.0))
@interface MC1AudioStreamOutput : NSObject <SCStreamOutput>
@property (nonatomic, assign) AudioCallback callback;
@property (nonatomic, assign) int targetSampleRate;
@property (nonatomic, assign) int targetChannels;
@end

@implementation MC1AudioStreamOutput

- (void)stream:(SCStream *)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
               ofType:(SCStreamOutputType)type
{
    if (type != SCStreamOutputTypeAudio) return;
    if (!self.callback) return;

    /* Extract audio buffer from CMSampleBuffer */
    CMBlockBufferRef blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);
    if (!blockBuffer) return;

    size_t totalLength = 0;
    char *dataPointer = nullptr;
    OSStatus status = CMBlockBufferGetDataPointer(blockBuffer, 0, nullptr,
                                                   &totalLength, &dataPointer);
    if (status != kCMBlockBufferNoErr || !dataPointer) return;

    /* Get the audio format description */
    CMFormatDescriptionRef formatDesc = CMSampleBufferGetFormatDescription(sampleBuffer);
    if (!formatDesc) return;

    const AudioStreamBasicDescription *asbd =
        CMAudioFormatDescriptionGetStreamBasicDescription(formatDesc);
    if (!asbd) return;

    /* ScreenCaptureKit delivers Float32 non-interleaved by default.
     * We need interleaved float32 for our AudioCallback. */
    int srcChannels = (int)asbd->mChannelsPerFrame;
    int srcSR       = (int)asbd->mSampleRate;
    size_t frames   = totalLength / (srcChannels * sizeof(float));

    if (frames == 0) return;

    /* If format matches our target, deliver directly */
    if (srcChannels == self.targetChannels && srcSR == self.targetSampleRate &&
        (asbd->mFormatFlags & kAudioFormatFlagIsNonInterleaved) == 0) {
        self.callback(reinterpret_cast<const float*>(dataPointer),
                      frames, srcChannels, srcSR);
        return;
    }

    /* Otherwise, convert to interleaved float32 at target specs.
     * For simplicity in Phase M3, we pass through the native format
     * and let the encoder handle sample rate conversion. */
    self.callback(reinterpret_cast<const float*>(dataPointer),
                  frames, srcChannels, srcSR);
}

@end

#endif /* HAS_SCREENCAPTUREKIT */

namespace mc1 {

/* ── C++ implementation ─────────────────────────────────────────────────── */

SystemAudioSource::SystemAudioSource(int sample_rate, int channels)
    : sample_rate_(sample_rate), channels_(channels)
{
}

SystemAudioSource::~SystemAudioSource()
{
    stop();
}

bool SystemAudioSource::is_available()
{
#if HAS_SCREENCAPTUREKIT
    if (@available(macOS 13.0, *)) {
        return true;
    }
#endif
    return false;
}

void SystemAudioSource::request_permission()
{
#if HAS_SCREENCAPTUREKIT
    if (@available(macOS 13.0, *)) {
        /* Requesting shareable content triggers the permission dialog */
        [SCShareableContent getShareableContentWithCompletionHandler:
            ^(SCShareableContent * _Nullable content, NSError * _Nullable error) {
                if (error) {
                    NSLog(@"Mcaster1: ScreenCaptureKit permission error: %@", error);
                }
            }];
    }
#endif
}

bool SystemAudioSource::start(AudioCallback cb)
{
#if HAS_SCREENCAPTUREKIT
    if (@available(macOS 13.0, *)) {
        if (running_.load()) return false;

        callback_ = cb;

        /* Get shareable content (displays) — we need at least one display
         * to create an SCStreamConfiguration, even for audio-only capture */
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        __block SCShareableContent *sharedContent = nil;
        __block NSError *contentError = nil;

        [SCShareableContent getShareableContentWithCompletionHandler:
            ^(SCShareableContent * _Nullable content, NSError * _Nullable error) {
                sharedContent = content;
                contentError = error;
                dispatch_semaphore_signal(sem);
            }];

        dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));

        if (contentError || !sharedContent || sharedContent.displays.count == 0) {
            fprintf(stderr, "Mcaster1: ScreenCaptureKit: failed to get shareable content\n");
            return false;
        }

        /* Configure audio-only capture */
        SCContentFilter *filter = [[SCContentFilter alloc]
            initWithDisplay:sharedContent.displays.firstObject
            excludingWindows:@[]];

        SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
        config.capturesAudio = YES;
        config.excludesCurrentProcessAudio = NO;
        config.sampleRate = sample_rate_;
        config.channelCount = channels_;

        /* Minimize video capture overhead — we only want audio */
        config.width = 2;
        config.height = 2;
        config.minimumFrameInterval = CMTimeMake(1, 1); /* 1 fps minimum */

        /* Create stream */
        SCStream *stream = [[SCStream alloc] initWithFilter:filter
                                              configuration:config
                                                   delegate:nil];

        /* Create and attach audio output handler */
        MC1AudioStreamOutput *output = [[MC1AudioStreamOutput alloc] init];
        output.callback = callback_;
        output.targetSampleRate = sample_rate_;
        output.targetChannels = channels_;

        NSError *addError = nil;
        [stream addStreamOutput:output
                           type:SCStreamOutputTypeAudio
             sampleHandlerQueue:dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0)
                          error:&addError];

        if (addError) {
            fprintf(stderr, "Mcaster1: ScreenCaptureKit: failed to add audio output: %s\n",
                    addError.localizedDescription.UTF8String);
            return false;
        }

        /* Start capture */
        __block bool startOK = false;
        dispatch_semaphore_t startSem = dispatch_semaphore_create(0);

        [stream startCaptureWithCompletionHandler:^(NSError * _Nullable error) {
            if (error) {
                fprintf(stderr, "Mcaster1: ScreenCaptureKit: start failed: %s\n",
                        error.localizedDescription.UTF8String);
            } else {
                startOK = true;
            }
            dispatch_semaphore_signal(startSem);
        }];

        dispatch_semaphore_wait(startSem, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));

        if (!startOK) return false;

        /* Store Obj-C references (bridged to void*) */
        sc_stream_   = (__bridge_retained void *)stream;
        sc_delegate_ = (__bridge_retained void *)output;
        running_.store(true);
        return true;
    }
#endif
    (void)cb;
    fprintf(stderr, "Mcaster1: ScreenCaptureKit not available on this macOS version\n");
    return false;
}

void SystemAudioSource::stop()
{
#if HAS_SCREENCAPTUREKIT
    if (@available(macOS 13.0, *)) {
        if (!running_.load()) return;
        running_.store(false);

        if (sc_stream_) {
            SCStream *stream = (__bridge_transfer SCStream *)sc_stream_;
            sc_stream_ = nullptr;

            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            [stream stopCaptureWithCompletionHandler:^(NSError * _Nullable error) {
                (void)error;
                dispatch_semaphore_signal(sem);
            }];
            dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC));
        }

        if (sc_delegate_) {
            /* Release the delegate */
            (void)(__bridge_transfer MC1AudioStreamOutput *)sc_delegate_;
            sc_delegate_ = nullptr;
        }

        callback_ = nullptr;
    }
#endif
}

} // namespace mc1
