/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * audio_capture_macos.h — macOS system audio capture via ScreenCaptureKit
 *
 * Captures system audio output (loopback) on macOS 13+ Ventura
 * without requiring BlackHole or Soundflower virtual audio devices.
 * Requires Screen Recording permission on first use.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_AUDIO_CAPTURE_MACOS_H
#define MC1_AUDIO_CAPTURE_MACOS_H

#include "audio_source.h"
#include <atomic>
#include <string>

namespace mc1 {

/* ScreenCaptureKit system audio capture source.
 * Implements the AudioSource interface for uniform use in EncoderSlot.
 * macOS 13+ (Ventura) required. */
class SystemAudioSource : public AudioSource {
public:
    explicit SystemAudioSource(int sample_rate = 44100,
                               int channels    = 2);
    ~SystemAudioSource() override;

    bool start(AudioCallback cb) override;
    void stop() override;
    bool is_running()   const override { return running_.load(); }
    int  sample_rate()  const override { return sample_rate_; }
    int  channels()     const override { return channels_; }
    std::string name()  const override { return "System Audio (ScreenCaptureKit)"; }

    /* Check if ScreenCaptureKit is available on this macOS version */
    static bool is_available();

    /* Request screen recording permission (async — shows system dialog) */
    static void request_permission();

private:
    int               sample_rate_;
    int               channels_;
    std::atomic<bool> running_{false};
    AudioCallback     callback_;

    /* Opaque Obj-C handle for SCStream + delegate */
    void *sc_stream_ = nullptr;    /* id<SCStream> */
    void *sc_delegate_ = nullptr;  /* id<SCStreamOutput> */
};

} // namespace mc1

#endif // MC1_AUDIO_CAPTURE_MACOS_H
