/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * input_level_monitor.cpp — Lightweight audio input level monitor
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "input_level_monitor.h"
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace mc1 {

InputLevelMonitor::InputLevelMonitor() = default;

InputLevelMonitor::~InputLevelMonitor()
{
    stop();
}

bool InputLevelMonitor::start(int device_index, int sample_rate, int channels)
{
    if (running_.load()) stop();

    channels_ = channels;
    peak_left_db_.store(DB_FLOOR);
    peak_right_db_.store(DB_FLOOR);

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        fprintf(stderr, "[LevelMon] PortAudio init failed: %s\n", Pa_GetErrorText(err));
        return false;
    }

    PaStreamParameters params{};
    if (device_index >= 0) {
        params.device = device_index;
    } else {
        params.device = Pa_GetDefaultInputDevice();
    }

    if (params.device == paNoDevice) {
        fprintf(stderr, "[LevelMon] No input device available\n");
        Pa_Terminate();
        return false;
    }

    const PaDeviceInfo *info = Pa_GetDeviceInfo(params.device);
    if (!info) {
        Pa_Terminate();
        return false;
    }

    /* Clamp channels to what the device supports */
    int max_ch = info->maxInputChannels;
    if (max_ch <= 0) {
        fprintf(stderr, "[LevelMon] Device has no input channels\n");
        Pa_Terminate();
        return false;
    }
    channels_ = std::min(channels, max_ch);

    params.channelCount = channels_;
    params.sampleFormat = paFloat32;
    params.suggestedLatency = info->defaultLowInputLatency;
    params.hostApiSpecificStreamInfo = nullptr;

    err = Pa_OpenStream(&stream_, &params, nullptr,
                        sample_rate, 512, paClipOff,
                        pa_callback, this);
    if (err != paNoError) {
        fprintf(stderr, "[LevelMon] Failed to open stream: %s\n", Pa_GetErrorText(err));
        Pa_Terminate();
        return false;
    }

    err = Pa_StartStream(stream_);
    if (err != paNoError) {
        fprintf(stderr, "[LevelMon] Failed to start stream: %s\n", Pa_GetErrorText(err));
        Pa_CloseStream(stream_);
        stream_ = nullptr;
        Pa_Terminate();
        return false;
    }

    running_.store(true);
    fprintf(stderr, "[LevelMon] Monitoring started: %s (%dch, %dHz)\n",
            info->name, channels_, sample_rate);
    return true;
}

void InputLevelMonitor::stop()
{
    if (!running_.load()) return;
    running_.store(false);

    if (stream_) {
        Pa_StopStream(stream_);
        Pa_CloseStream(stream_);
        stream_ = nullptr;
    }
    Pa_Terminate();

    peak_left_db_.store(DB_FLOOR);
    peak_right_db_.store(DB_FLOOR);
    fprintf(stderr, "[LevelMon] Monitoring stopped\n");
}

int InputLevelMonitor::pa_callback(const void *input, void * /*output*/,
                                    unsigned long frame_count,
                                    const PaStreamCallbackTimeInfo * /*time_info*/,
                                    PaStreamCallbackFlags /*status_flags*/,
                                    void *user_data)
{
    auto *self = static_cast<InputLevelMonitor *>(user_data);
    if (!input || !self->running_.load()) return paContinue;

    const auto *pcm = static_cast<const float *>(input);
    int ch = self->channels_;

    float peak_l = 0.0f;
    float peak_r = 0.0f;

    for (unsigned long i = 0; i < frame_count; ++i) {
        float left = std::fabs(pcm[i * ch]);
        if (left > peak_l) peak_l = left;

        if (ch >= 2) {
            float right = std::fabs(pcm[i * ch + 1]);
            if (right > peak_r) peak_r = right;
        }
    }

    /* Convert to dB with floor */
    auto to_db = [](float linear) -> float {
        if (linear < 1e-10f) return -60.0f;
        float db = 20.0f * std::log10(linear);
        return std::max(db, -60.0f);
    };

    float left_db  = to_db(peak_l);
    float right_db = (ch >= 2) ? to_db(peak_r) : left_db;

    /* Ballistic metering: instant attack, smooth release (~35 dB/sec) */
    float prev_l = self->peak_left_db_.load();
    float prev_r = self->peak_right_db_.load();

    constexpr float release_per_cb = 0.4f; /* dB per callback */

    float new_l = (left_db > prev_l) ? left_db : (prev_l - release_per_cb);
    float new_r = (right_db > prev_r) ? right_db : (prev_r - release_per_cb);

    self->peak_left_db_.store(std::max(new_l, -60.0f));
    self->peak_right_db_.store(std::max(new_r, -60.0f));

    return paContinue;
}

} // namespace mc1
