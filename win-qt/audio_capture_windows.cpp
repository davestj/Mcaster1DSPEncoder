/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * audio_capture_windows.cpp — Windows system audio capture stub (WASAPI loopback)
 *
 * Windows API: WASAPI (IAudioClient::Initialize with AUDCLNT_STREAMFLAGS_LOOPBACK,
 *              IAudioCaptureClient::GetBuffer for loopback PCM capture)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "audio_capture_windows.h"

namespace mc1 {

SystemAudioSource::SystemAudioSource(int sample_rate, int channels)
    : sample_rate_(sample_rate)
    , channels_(channels)
{
}

SystemAudioSource::~SystemAudioSource()
{
    stop();
}

bool SystemAudioSource::start(AudioCallback cb)
{
    if (running_.load()) return false;

    callback_ = std::move(cb);

    // TODO: Implement with Windows API
    // 1. CoCreateInstance(CLSID_MMDeviceEnumerator) → IMMDeviceEnumerator
    // 2. GetDefaultAudioEndpoint(eRender, eConsole) → IMMDevice (render device for loopback)
    // 3. Activate(IID_IAudioClient) → IAudioClient
    // 4. Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, ...)
    // 5. GetService(IID_IAudioCaptureClient) → IAudioCaptureClient
    // 6. Start capture_thread_ running capture_loop()

    return false; // stub: always fails until implemented
}

void SystemAudioSource::stop()
{
    if (!running_.load()) return;

    running_.store(false);

    if (capture_thread_.joinable())
        capture_thread_.join();

    // TODO: Implement with Windows API
    // Release IAudioCaptureClient, IAudioClient, IMMDevice
    audio_client_ = nullptr;
    capture_client_ = nullptr;
    callback_ = nullptr;
}

bool SystemAudioSource::is_available()
{
    // TODO: Implement with Windows API
    // Check for WASAPI availability (Windows Vista+, practically always true on Win10+)
    return false;
}

void SystemAudioSource::request_permission()
{
    // No-op on Windows — WASAPI loopback doesn't require explicit permission
}

void SystemAudioSource::capture_loop()
{
    // TODO: Implement with Windows API
    // Loop: IAudioCaptureClient::GetBuffer() → convert to float32 → callback_(pcm, frames, channels_, sample_rate_)
    // Sleep on IAudioClient event handle between buffer reads
}

} // namespace mc1
