/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * input_level_monitor.h — Lightweight audio input level monitor for VU metering
 *
 * Opens a PortAudio stream on the selected device and computes peak dB levels
 * per channel. Runs independently of the encoding pipeline so VU meters show
 * real levels even when no encoder slots are active.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_INPUT_LEVEL_MONITOR_H
#define MC1_INPUT_LEVEL_MONITOR_H

#include <atomic>
#include <cstdint>
#include <string>
#include <portaudio.h>

namespace mc1 {

class InputLevelMonitor {
public:
    InputLevelMonitor();
    ~InputLevelMonitor();

    /* Start monitoring the given device.
     * device_index: PortAudio device index, or -1 for default.
     * Returns false if the device cannot be opened. */
    bool start(int device_index = -1, int sample_rate = 44100, int channels = 2);

    /* Stop monitoring and close the stream. */
    void stop();

    bool is_running() const { return running_.load(); }

    /* Current peak levels in dB (range: -60 to 0).
     * Thread-safe — read from any thread (UI timer). */
    float peak_left_db()  const { return peak_left_db_.load(); }
    float peak_right_db() const { return peak_right_db_.load(); }

private:
    std::atomic<bool>  running_{false};
    std::atomic<float> peak_left_db_{-60.0f};
    std::atomic<float> peak_right_db_{-60.0f};
    int channels_ = 2;

    static constexpr float DB_FLOOR = -60.0f;

    PaStream *stream_ = nullptr;

    static int pa_callback(const void *input, void *output,
                           unsigned long frame_count,
                           const PaStreamCallbackTimeInfo *time_info,
                           PaStreamCallbackFlags status_flags,
                           void *user_data);
};

} // namespace mc1

#endif // MC1_INPUT_LEVEL_MONITOR_H
