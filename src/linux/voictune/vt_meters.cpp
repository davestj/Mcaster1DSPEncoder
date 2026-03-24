/*
 * Mcaster1 VoicTune — Audio Meters v1.0.0
 * voictune/vt_meters.cpp
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "vt_meters.h"
#include <cmath>
#include <algorithm>

namespace mc1vt {

void AudioMeters::process(const float* pcm, size_t frames, int sample_rate)
{
    if (!pcm || frames == 0) return;

    /* Update LUFS window size if sample rate changed */
    lufs_window_ = (int)(0.4f * (float)sample_rate);  /* 400ms */

    float sum_sq = 0.0f;
    float peak   = 0.0f;

    for (size_t i = 0; i < frames; ++i) {
        float s   = pcm[i];
        float abs_s = std::fabs(s);
        sum_sq += s * s;
        if (abs_s > peak) peak = abs_s;
    }

    /* RMS in dB */
    float rms = (frames > 0) ? std::sqrt(sum_sq / (float)frames) : 0.0f;
    float rms_db = (rms > 1e-10f) ? 20.0f * std::log10(rms) : -96.0f;
    rms_db_.store(rms_db, std::memory_order_relaxed);

    /* Peak in dB */
    float peak_db = (peak > 1e-10f) ? 20.0f * std::log10(peak) : -96.0f;
    peak_db_.store(peak_db, std::memory_order_relaxed);

    /* Peak hold (update only if new peak is higher) */
    float cur_hold = peak_hold_db_.load(std::memory_order_relaxed);
    if (peak_db > cur_hold) {
        peak_hold_db_.store(peak_db, std::memory_order_relaxed);
    }

    /* LUFS (ITU-R BS.1770-4 simplified momentary loudness)
     * We accumulate sum-of-squares over a 400ms sliding window.
     * Full BS.1770 has K-weighting filter — we approximate for v1.0. */
    lufs_sum_sq_ += sum_sq;
    lufs_samples_ += (int)frames;

    if (lufs_samples_ >= lufs_window_) {
        float mean_sq = lufs_sum_sq_ / (float)lufs_samples_;
        float lufs_val = (mean_sq > 1e-10f)
            ? -0.691f + 10.0f * std::log10(mean_sq)  /* BS.1770 offset */
            : -96.0f;
        lufs_.store(lufs_val, std::memory_order_relaxed);
        lufs_sum_sq_  = 0.0f;
        lufs_samples_ = 0;
    }
}

void AudioMeters::reset()
{
    rms_db_.store(-96.0f, std::memory_order_relaxed);
    peak_db_.store(-96.0f, std::memory_order_relaxed);
    lufs_.store(-96.0f, std::memory_order_relaxed);
    peak_hold_db_.store(-96.0f, std::memory_order_relaxed);
    lufs_sum_sq_  = 0.0f;
    lufs_samples_ = 0;
}

} // namespace mc1vt
