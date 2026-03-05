/*
 * Mcaster1DSPEncoder — Dead Air Detector Implementation
 * dsp/dead_air_detector.cpp
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dead_air_detector.h"
#include <cmath>
#include <algorithm>

namespace mc1dsp {

void DeadAirDetector::update_coeffs() {
    threshold_lin_       = std::pow(10.0f, cfg_.threshold_db / 20.0f);
    timeout_samples_     = cfg_.timeout_sec * sample_rate_;
    skip_timeout_samples_ = cfg_.skip_timeout_sec * sample_rate_;
}

void DeadAirDetector::process(const float* pcm, size_t frames, int channels) {
    if (!cfg_.enabled) return;

    /* We compute RMS of the buffer */
    float sum_sq = 0.0f;
    size_t total = frames * channels;
    for (size_t i = 0; i < total; ++i) {
        sum_sq += pcm[i] * pcm[i];
    }
    float rms = (total > 0) ? std::sqrt(sum_sq / static_cast<float>(total)) : 0.0f;
    rms_db_ = (rms > 1e-10f) ? 20.0f * std::log10(rms) : -96.0f;

    if (rms < threshold_lin_) {
        /* We are in silence */
        silence_samples_ += static_cast<int>(frames);

        if (!skip_triggered_ && silence_samples_ >= timeout_samples_) {
            /* We trigger skip track */
            in_dead_air_.store(true);
            skip_triggered_ = true;
            post_skip_samples_ = 0;
            if (cb_) cb_(0);  // action=0 → skip track
        }

        if (skip_triggered_) {
            post_skip_samples_ += static_cast<int>(frames);
            if (post_skip_samples_ >= skip_timeout_samples_ && !cfg_.fallback_playlist.empty()) {
                /* We skipped but still silent — load fallback playlist */
                if (cb_) cb_(1);  // action=1 → load fallback
                /* We reset to avoid repeated fallback triggers */
                skip_triggered_ = false;
                silence_samples_ = 0;
                post_skip_samples_ = 0;
            }
        }
    } else {
        /* We have audio — reset all counters */
        silence_samples_ = 0;
        post_skip_samples_ = 0;
        skip_triggered_ = false;
        in_dead_air_.store(false);
    }
}

void DeadAirDetector::reset() {
    silence_samples_ = 0;
    post_skip_samples_ = 0;
    skip_triggered_ = false;
    in_dead_air_.store(false);
    rms_db_ = -96.0f;
}

float DeadAirDetector::silence_sec() const {
    return (sample_rate_ > 0) ? static_cast<float>(silence_samples_) / sample_rate_ : 0.0f;
}

} // namespace mc1dsp
