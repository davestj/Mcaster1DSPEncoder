/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * audio_capture_windows.cpp — WASAPI loopback system audio capture
 *
 * Captures the default audio render device output using WASAPI in
 * shared-mode loopback. No virtual audio cables required.
 *
 * Windows API: IAudioClient (loopback), IAudioCaptureClient
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <combaseapi.h>
#include <cstdio>
#include <cmath>
#include <chrono>
#include <vector>

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

    /* Initialize COM on this thread */
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool com_init = SUCCEEDED(hr) || hr == S_FALSE; /* S_FALSE = already initialized */

    /* Get default audio render endpoint (for loopback) */
    IMMDeviceEnumerator *enumerator = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                          CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                          reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr) || !enumerator) {
        fprintf(stderr, "[WASAPI] Failed to create device enumerator: 0x%08lx\n", hr);
        return false;
    }

    IMMDevice *device = nullptr;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    enumerator->Release();

    if (FAILED(hr) || !device) {
        fprintf(stderr, "[WASAPI] No default render endpoint: 0x%08lx\n", hr);
        return false;
    }

    /* Activate IAudioClient */
    IAudioClient *client = nullptr;
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                           nullptr, reinterpret_cast<void**>(&client));
    device->Release();

    if (FAILED(hr) || !client) {
        fprintf(stderr, "[WASAPI] Failed to activate audio client: 0x%08lx\n", hr);
        return false;
    }

    /* Get the device's mix format */
    WAVEFORMATEX *mix_format = nullptr;
    hr = client->GetMixFormat(&mix_format);
    if (FAILED(hr) || !mix_format) {
        fprintf(stderr, "[WASAPI] Failed to get mix format: 0x%08lx\n", hr);
        client->Release();
        return false;
    }

    /* Store actual device format for resampling */
    int device_rate = static_cast<int>(mix_format->nSamplesPerSec);
    int device_ch = static_cast<int>(mix_format->nChannels);

    /* Initialize in shared loopback mode */
    REFERENCE_TIME buf_duration = 200000; /* 20ms in 100-ns units */
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                            AUDCLNT_STREAMFLAGS_LOOPBACK,
                            buf_duration, 0, mix_format, nullptr);
    CoTaskMemFree(mix_format);

    if (FAILED(hr)) {
        fprintf(stderr, "[WASAPI] Initialize loopback failed: 0x%08lx\n", hr);
        client->Release();
        return false;
    }

    /* Get capture client */
    IAudioCaptureClient *capture = nullptr;
    hr = client->GetService(__uuidof(IAudioCaptureClient),
                            reinterpret_cast<void**>(&capture));
    if (FAILED(hr) || !capture) {
        fprintf(stderr, "[WASAPI] Failed to get capture client: 0x%08lx\n", hr);
        client->Release();
        return false;
    }

    /* Store COM pointers */
    audio_client_ = client;
    capture_client_ = capture;

    /* Update sample rate/channels to match device */
    sample_rate_ = device_rate;
    channels_ = device_ch;

    /* Start the audio client */
    hr = client->Start();
    if (FAILED(hr)) {
        fprintf(stderr, "[WASAPI] Failed to start: 0x%08lx\n", hr);
        capture->Release();
        client->Release();
        audio_client_ = nullptr;
        capture_client_ = nullptr;
        return false;
    }

    running_.store(true);
    capture_thread_ = std::thread(&SystemAudioSource::capture_loop, this);

    fprintf(stderr, "[WASAPI] Loopback capture started: %d Hz, %d ch\n",
            sample_rate_, channels_);
    return true;
}

void SystemAudioSource::stop()
{
    if (!running_.load()) return;

    running_.store(false);

    if (capture_thread_.joinable())
        capture_thread_.join();

    auto *client = static_cast<IAudioClient*>(audio_client_);
    auto *capture = static_cast<IAudioCaptureClient*>(capture_client_);

    if (client) client->Stop();
    if (capture) capture->Release();
    if (client) client->Release();

    audio_client_ = nullptr;
    capture_client_ = nullptr;
    callback_ = nullptr;

    fprintf(stderr, "[WASAPI] Loopback capture stopped\n");
}

bool SystemAudioSource::is_available()
{
    /* WASAPI loopback available on Windows Vista+ (always true on Win10+) */
    return true;
}

void SystemAudioSource::request_permission()
{
    /* No-op on Windows — WASAPI loopback doesn't require permission */
}

void SystemAudioSource::capture_loop()
{
    auto *capture = static_cast<IAudioCaptureClient*>(capture_client_);
    if (!capture) return;

    /* Buffer for float conversion */
    std::vector<float> float_buf;

    while (running_.load()) {
        UINT32 packet_len = 0;
        HRESULT hr = capture->GetNextPacketSize(&packet_len);
        if (FAILED(hr)) break;

        while (packet_len > 0 && running_.load()) {
            BYTE *data = nullptr;
            UINT32 frames_available = 0;
            DWORD flags = 0;

            hr = capture->GetBuffer(&data, &frames_available, &flags,
                                     nullptr, nullptr);
            if (FAILED(hr)) break;

            if (frames_available > 0 && callback_) {
                size_t total_samples = static_cast<size_t>(frames_available) * channels_;

                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    /* Device is silent — send zeros */
                    float_buf.assign(total_samples, 0.0f);
                } else {
                    /* WASAPI shared mode typically delivers float32 */
                    const float *src = reinterpret_cast<const float*>(data);
                    float_buf.assign(src, src + total_samples);
                }

                callback_(float_buf.data(), frames_available,
                          channels_, sample_rate_);
            }

            capture->ReleaseBuffer(frames_available);

            hr = capture->GetNextPacketSize(&packet_len);
            if (FAILED(hr)) break;
        }

        /* Sleep briefly to avoid spinning — 5ms matches typical buffer period */
        Sleep(5);
    }
}

} // namespace mc1
