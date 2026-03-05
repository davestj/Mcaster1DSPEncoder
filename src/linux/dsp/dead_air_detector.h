/*
 * Mcaster1DSPEncoder — Dead Air Detector
 * dsp/dead_air_detector.h
 *
 * We monitor PCM audio buffers for extended silence. If RMS level stays below
 * a configurable threshold for longer than timeout_sec, we trigger a callback
 * (skip track or load fallback playlist).
 *
 * Runs inline in the audio callback — no extra thread needed.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>

namespace mc1dsp {

struct DeadAirConfig {
    bool        enabled         = false;
    float       threshold_db    = -60.0f;  // RMS below this = silence
    int         timeout_sec     = 10;      // seconds of continuous silence before trigger
    std::string fallback_playlist;         // loaded if skip doesn't fix it
    int         skip_timeout_sec = 5;      // seconds after skip before loading fallback
};

class DeadAirDetector {
public:
    using TriggerCallback = std::function<void(int action)>;  // 0=skip, 1=fallback

    DeadAirDetector() = default;

    void set_config(const DeadAirConfig& cfg) { cfg_ = cfg; update_coeffs(); }
    const DeadAirConfig& config() const { return cfg_; }

    void set_sample_rate(int sr) { sample_rate_ = sr; update_coeffs(); }
    void set_callback(TriggerCallback cb) { cb_ = cb; }

    /* We process a PCM buffer and check for dead air. Call from audio callback. */
    void process(const float* pcm, size_t frames, int channels);

    /* We reset the silence counter (call on track change or manual intervention) */
    void reset();

    /* We return current silence duration in seconds */
    float silence_sec() const;

    /* We return true if we're currently in a dead air state */
    bool is_dead_air() const { return in_dead_air_.load(); }

    /* We return the current RMS level in dB */
    float current_rms_db() const { return rms_db_; }

private:
    DeadAirConfig    cfg_;
    TriggerCallback  cb_;
    int              sample_rate_      = 44100;
    float            threshold_lin_    = 0.001f;  // -60 dB linear
    int              timeout_samples_  = 0;
    int              skip_timeout_samples_ = 0;
    int              silence_samples_  = 0;
    int              post_skip_samples_= 0;
    bool             skip_triggered_   = false;
    std::atomic<bool> in_dead_air_{false};
    float            rms_db_           = -96.0f;

    void update_coeffs();
};

} // namespace mc1dsp
