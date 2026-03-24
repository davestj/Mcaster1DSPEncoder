/*
 * Mcaster1 VoicTune — Audio Meters v1.0.0
 * voictune/vt_meters.h
 *
 * Thread-safe RMS, peak, and LUFS (ITU-R BS.1770-4) meter calculations.
 * Audio thread writes via process(); HTTP thread reads via atomics.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <atomic>
#include <cstddef>

namespace mc1vt {

class AudioMeters {
public:
    AudioMeters() = default;

    /* We process a mono float32 PCM buffer and update all meters.
     * Safe to call from audio callback — no allocations, no mutex. */
    void process(const float* pcm, size_t frames, int sample_rate);

    /* Atomic reads — safe from any thread */
    float rms_db()  const { return rms_db_.load(std::memory_order_relaxed); }
    float peak_db() const { return peak_db_.load(std::memory_order_relaxed); }
    float lufs()    const { return lufs_.load(std::memory_order_relaxed); }

    /* Peak hold with decay (call periodically from display thread) */
    float peak_hold_db() const { return peak_hold_db_.load(std::memory_order_relaxed); }

    void reset();

private:
    std::atomic<float> rms_db_{-96.0f};
    std::atomic<float> peak_db_{-96.0f};
    std::atomic<float> lufs_{-96.0f};
    std::atomic<float> peak_hold_db_{-96.0f};

    /* LUFS integration state (momentary, 400ms window) */
    float lufs_sum_sq_  = 0.0f;
    int   lufs_samples_ = 0;
    int   lufs_window_  = 19200;  /* 400ms at 48kHz */
};

} // namespace mc1vt
