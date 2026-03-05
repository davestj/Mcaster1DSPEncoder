/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * audio_capture_windows.h — Windows system audio capture via WASAPI loopback
 *
 * Captures system audio output (loopback) on Windows 10+ using
 * WASAPI in loopback mode. No virtual audio devices required.
 *
 * Windows API: WASAPI (IAudioClient, IAudioCaptureClient) in loopback mode
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_AUDIO_CAPTURE_WINDOWS_H
#define MC1_AUDIO_CAPTURE_WINDOWS_H

#include "audio_source.h"
#include <atomic>
#include <string>
#include <thread>

namespace mc1 {

/* WASAPI loopback system audio capture source.
 * Implements the AudioSource interface for uniform use in EncoderSlot.
 * Windows 10+ required. */
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
    std::string name()  const override { return "System Audio (WASAPI Loopback)"; }

    /* Check if WASAPI loopback is available on this Windows version */
    static bool is_available();

    /* Request audio capture permission (no-op on Windows) */
    static void request_permission();

private:
    int               sample_rate_;
    int               channels_;
    std::atomic<bool> running_{false};
    AudioCallback     callback_;

    /* WASAPI handles (opaque — defined in .cpp) */
    void *audio_client_   = nullptr;  /* IAudioClient* */
    void *capture_client_ = nullptr;  /* IAudioCaptureClient* */
    std::thread capture_thread_;

    void capture_loop();
};

} // namespace mc1

#endif // MC1_AUDIO_CAPTURE_WINDOWS_H
