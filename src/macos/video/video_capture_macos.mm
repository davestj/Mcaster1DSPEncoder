/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/video_capture_macos.mm — AVFoundation camera capture (Obj-C++)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "video_capture_macos.h"

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <cstdio>
#include <cstring>

/* ── Obj-C delegate — must be at file scope, outside C++ namespace ───────── */

@interface MC1VideoOutputDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property (nonatomic, assign) mc1::VideoCallback callback;
@property (nonatomic, assign) int targetWidth;
@property (nonatomic, assign) int targetHeight;
@end

@implementation MC1VideoOutputDelegate

- (void)captureOutput:(AVCaptureOutput *)output
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
           fromConnection:(AVCaptureConnection *)connection
{
    (void)output;
    (void)connection;

    if (!self.callback) return;

    CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!imageBuffer) return;

    CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);

    mc1::VideoFrame frame;
    frame.data     = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(imageBuffer));
    frame.data_len = CVPixelBufferGetDataSize(imageBuffer);
    frame.width    = static_cast<int>(CVPixelBufferGetWidth(imageBuffer));
    frame.height   = static_cast<int>(CVPixelBufferGetHeight(imageBuffer));
    frame.stride   = static_cast<int>(CVPixelBufferGetBytesPerRow(imageBuffer));
    frame.pixel_format = static_cast<uint32_t>(CVPixelBufferGetPixelFormatType(imageBuffer));

    CMTime pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
    frame.pts_us = static_cast<int64_t>(CMTimeGetSeconds(pts) * 1000000.0);

    self.callback(frame);

    CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
}

- (void)captureOutput:(AVCaptureOutput *)output
    didDropSampleBuffer:(CMSampleBufferRef)sampleBuffer
         fromConnection:(AVCaptureConnection *)connection
{
    (void)output;
    (void)sampleBuffer;
    (void)connection;
    /* Frame dropped — no action needed */
}

@end

/* ── C++ implementation ─────────────────────────────────────────────────── */

namespace mc1 {

CameraSource::CameraSource(int device_index, int width, int height, int fps)
    : device_index_(device_index)
    , width_(width)
    , height_(height)
    , fps_(fps)
{
}

CameraSource::~CameraSource()
{
    stop();
}

std::vector<CameraDeviceInfo> CameraSource::enumerate_devices()
{
    std::vector<CameraDeviceInfo> result;

    AVCaptureDeviceDiscoverySession *session =
        [AVCaptureDeviceDiscoverySession
            discoverySessionWithDeviceTypes:@[
                AVCaptureDeviceTypeBuiltInWideAngleCamera,
                AVCaptureDeviceTypeExternal
            ]
            mediaType:AVMediaTypeVideo
            position:AVCaptureDevicePositionUnspecified];

    int idx = 0;
    for (AVCaptureDevice *device in session.devices) {
        CameraDeviceInfo info;
        info.index     = idx++;
        info.name      = device.localizedName.UTF8String;
        info.unique_id = device.uniqueID.UTF8String;
        info.is_front  = (device.position == AVCaptureDevicePositionFront);
        result.push_back(info);
    }

    return result;
}

void CameraSource::request_permission()
{
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
        completionHandler:^(BOOL granted) {
            if (!granted) {
                fprintf(stderr, "[Camera] Permission denied\n");
            }
        }];
}

bool CameraSource::start(VideoCallback cb)
{
    if (running_.load()) return false;
    callback_ = cb;

    /* Find the requested camera device */
    AVCaptureDeviceDiscoverySession *discovery =
        [AVCaptureDeviceDiscoverySession
            discoverySessionWithDeviceTypes:@[
                AVCaptureDeviceTypeBuiltInWideAngleCamera,
                AVCaptureDeviceTypeExternal
            ]
            mediaType:AVMediaTypeVideo
            position:AVCaptureDevicePositionUnspecified];

    NSArray<AVCaptureDevice*> *devices = discovery.devices;
    if (devices.count == 0) {
        fprintf(stderr, "[Camera] No video capture devices found\n");
        return false;
    }

    AVCaptureDevice *device = (device_index_ >= 0 && device_index_ < (int)devices.count)
                              ? devices[device_index_]
                              : devices.firstObject;
    device_name_ = device.localizedName.UTF8String;

    /* Create capture session */
    AVCaptureSession *session = [[AVCaptureSession alloc] init];

    /* Set session preset based on resolution */
    if (width_ >= 1920 && height_ >= 1080) {
        session.sessionPreset = AVCaptureSessionPreset1920x1080;
    } else if (width_ >= 1280 && height_ >= 720) {
        session.sessionPreset = AVCaptureSessionPreset1280x720;
    } else {
        session.sessionPreset = AVCaptureSessionPreset640x480;
    }

    /* Create input */
    NSError *inputError = nil;
    AVCaptureDeviceInput *input = [AVCaptureDeviceInput deviceInputWithDevice:device
                                                                       error:&inputError];
    if (inputError || !input) {
        fprintf(stderr, "[Camera] Failed to create input: %s\n",
                inputError.localizedDescription.UTF8String);
        return false;
    }

    if (![session canAddInput:input]) {
        fprintf(stderr, "[Camera] Cannot add input to session\n");
        return false;
    }
    [session addInput:input];

    /* Create video data output */
    AVCaptureVideoDataOutput *output = [[AVCaptureVideoDataOutput alloc] init];
    output.alwaysDiscardsLateVideoFrames = YES;
    output.videoSettings = @{
        (NSString *)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)
    };

    /* Create delegate and dispatch queue */
    MC1VideoOutputDelegate *delegate = [[MC1VideoOutputDelegate alloc] init];
    delegate.callback    = callback_;
    delegate.targetWidth = width_;
    delegate.targetHeight = height_;

    dispatch_queue_t videoQueue = dispatch_queue_create("com.mcaster1.video.capture",
                                                        DISPATCH_QUEUE_SERIAL);
    [output setSampleBufferDelegate:delegate queue:videoQueue];

    if (![session canAddOutput:output]) {
        fprintf(stderr, "[Camera] Cannot add video output to session\n");
        return false;
    }
    [session addOutput:output];

    /* Configure frame rate */
    AVCaptureConnection *conn = [output connectionWithMediaType:AVMediaTypeVideo];
    if (conn && conn.isVideoMinFrameDurationSupported) {
        conn.videoMinFrameDuration = CMTimeMake(1, fps_);
    }
    if (conn && conn.isVideoMaxFrameDurationSupported) {
        conn.videoMaxFrameDuration = CMTimeMake(1, fps_);
    }

    /* Start capture */
    [session startRunning];

    /* Store Obj-C references */
    capture_session_ = (__bridge_retained void *)session;
    video_delegate_  = (__bridge_retained void *)delegate;
    running_.store(true);

    fprintf(stderr, "[Camera] Capture started: %s  %dx%d @ %dfps\n",
            device_name_.c_str(), width_, height_, fps_);
    return true;
}

void CameraSource::stop()
{
    if (!running_.load()) return;
    running_.store(false);

    if (capture_session_) {
        AVCaptureSession *session = (__bridge_transfer AVCaptureSession *)capture_session_;
        capture_session_ = nullptr;
        [session stopRunning];
    }

    if (video_delegate_) {
        (void)(__bridge_transfer MC1VideoOutputDelegate *)video_delegate_;
        video_delegate_ = nullptr;
    }

    callback_ = nullptr;
    fprintf(stderr, "[Camera] Capture stopped\n");
}

} // namespace mc1
