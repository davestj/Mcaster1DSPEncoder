/*
 * Mcaster1DSPEncoder — Sidechain Ducker Implementation
 * dsp/sidechain_ducker.cpp
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "sidechain_ducker.h"
#include <cmath>
#include <algorithm>

namespace mc1dsp {

void SidechainDucker::set_sample_rate(int sr) {
    sample_rate_ = sr;
    update_coeffs();
}

void SidechainDucker::update_coeffs() {
    float sr = static_cast<float>(sample_rate_);
    attack_c_   = (cfg_.attack_ms > 0 && sr > 0)  ? 1.0f - std::exp(-2.2f / (cfg_.attack_ms * sr / 1000.0f))  : 1.0f;
    release_c_  = (cfg_.release_ms > 0 && sr > 0)  ? 1.0f - std::exp(-2.2f / (cfg_.release_ms * sr / 1000.0f)) : 1.0f;
    target_lin_ = std::pow(10.0f, cfg_.duck_amount_db / 20.0f);
}

void SidechainDucker::process(float* pcm, size_t frames, int channels) {
    if (!cfg_.enabled) return;

    bool ducking = ptt_active_.load(std::memory_order_relaxed);
    float target = ducking ? target_lin_ : 1.0f;

    for (size_t f = 0; f < frames; ++f) {
        /* We apply attack/release envelope toward target */
        float coeff = (target < duck_gain_) ? attack_c_ : release_c_;
        duck_gain_ += coeff * (target - duck_gain_);

        /* We apply duck gain to all channels */
        for (int c = 0; c < channels; ++c) {
            pcm[f * channels + c] *= duck_gain_;
        }
    }

    /* We update metering value */
    duck_db_ = (duck_gain_ < 1.0f && duck_gain_ > 1e-6f) ? 20.0f * std::log10(duck_gain_) : 0.0f;
}

void SidechainDucker::feed_sidechain(const float* mic_pcm, size_t frames, int channels) {
    /* We detect peak level from the sidechain (mic) input */
    float peak = 0.0f;
    for (size_t i = 0; i < frames * channels; ++i) {
        float s = std::fabs(mic_pcm[i]);
        if (s > peak) peak = s;
    }
    sc_level_ = peak;
    /* We auto-trigger ducking if sidechain level exceeds threshold (optional mode) */
    float thresh_lin = std::pow(10.0f, cfg_.threshold_db / 20.0f);
    if (peak > thresh_lin && !ptt_active_.load()) {
        /* We don't auto-set PTT here — sidechain is informational only.
         * PTT is always manually controlled via the API. */
    }
}

json SidechainDucker::get_params() const {
    return {
        {"enabled",         cfg_.enabled},
        {"duck_amount_db",  cfg_.duck_amount_db},
        {"attack_ms",       cfg_.attack_ms},
        {"release_ms",      cfg_.release_ms},
        {"threshold_db",    cfg_.threshold_db},
        {"fade_curve",      cfg_.fade_curve},
        {"ptt_active",      ptt_active_.load()},
        {"current_duck_db", duck_db_},
        {"sidechain_level", sc_level_}
    };
}

void SidechainDucker::set_params(const json& j) {
    if (j.contains("enabled"))        cfg_.enabled         = j["enabled"].get<bool>();
    if (j.contains("duck_amount_db")) cfg_.duck_amount_db  = j["duck_amount_db"].get<float>();
    if (j.contains("attack_ms"))      cfg_.attack_ms       = j["attack_ms"].get<float>();
    if (j.contains("release_ms"))     cfg_.release_ms      = j["release_ms"].get<float>();
    if (j.contains("threshold_db"))   cfg_.threshold_db    = j["threshold_db"].get<float>();
    if (j.contains("fade_curve"))     cfg_.fade_curve      = j["fade_curve"].get<int>();
    update_coeffs();
}

} // namespace mc1dsp
