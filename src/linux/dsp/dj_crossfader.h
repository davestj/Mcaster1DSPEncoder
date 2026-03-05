/*
 * Mcaster1DSPEncoder — DJ Crossfader
 * dsp/dj_crossfader.h
 *
 * Dual-deck crossfader with 9 selectable curve algorithms (ported from MC1AMP).
 * Uses one-pole IIR smoothing (default k=0.85) for zipper-free fader movement.
 * Thread-safe: position/curve/smoothing use std::atomic for lock-free audio thread access.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "crossfader_curves.h"
#include <atomic>
#include <cstddef>

namespace mc1dsp {

struct DjCrossfaderConfig {
    mc1xf::Curve curve      = mc1xf::Curve::ConstantPower;
    float        position   = 0.0f;    // 0.0 = full Deck A, 1.0 = full Deck B
    float        smoothing  = 0.85f;   // one-pole IIR coefficient [0, 0.999]
    bool         reversed   = false;   // hamster mode (swap A/B)
    float        auto_duration_sec = 0.0f; // 0 = no auto-crossfade
};

class DjCrossfader {
public:
    DjCrossfader() = default;

    /* ── Configuration ──────────────────────────────────────────────────────── */

    void set_curve(mc1xf::Curve c)     { curve_.store(static_cast<int>(c)); }
    mc1xf::Curve curve() const         { return static_cast<mc1xf::Curve>(curve_.load()); }

    void set_position(float pos)       { target_pos_.store(std::clamp(pos, 0.0f, 1.0f)); }
    float target_position() const      { return target_pos_.load(); }
    float smoothed_position() const    { return smoothed_pos_; }

    void set_smoothing(float k)        { smooth_k_ = std::clamp(k, 0.0f, 0.999f); }
    float smoothing() const            { return smooth_k_; }

    void set_reversed(bool on)         { reversed_.store(on); }
    bool is_reversed() const           { return reversed_.load(); }

    void set_sample_rate(int sr)       { sample_rate_ = sr; }

    /* ── Real-time blend ────────────────────────────────────────────────────── */

    // We blend two interleaved PCM buffers using the selected curve + smoothed position.
    // out = deck_a * gain_a + deck_b * gain_b
    void blend(const float* deck_a, const float* deck_b, float* out,
               size_t frames, int channels);

    // We return the current gain pair (after smoothing)
    mc1xf::Gains current_gains() const { return last_gains_; }

    /* ── Auto-crossfade ─────────────────────────────────────────────────────── */

    // We start an auto-crossfade from current position toward target_pos over duration_sec
    void start_auto_crossfade(float target, float duration_sec);
    void stop_auto_crossfade()          { auto_active_.store(false); }
    bool is_auto_active() const         { return auto_active_.load(); }

    // We advance the auto-crossfade by `frames` samples. Returns true while active.
    bool auto_advance(size_t frames);

    /* ── Reset ──────────────────────────────────────────────────────────────── */
    void reset();

private:
    std::atomic<float> target_pos_{0.0f};
    float              smoothed_pos_ = 0.0f;
    float              smooth_k_     = 0.85f;
    std::atomic<int>   curve_{static_cast<int>(mc1xf::Curve::ConstantPower)};
    std::atomic<bool>  reversed_{false};
    mc1xf::Gains       last_gains_{1.0f, 0.0f};
    int                sample_rate_  = 44100;

    // Auto-crossfade state
    std::atomic<bool>  auto_active_{false};
    float              auto_start_pos_ = 0.0f;
    float              auto_end_pos_   = 1.0f;
    float              auto_progress_  = 0.0f;  // 0.0 → 1.0
    float              auto_step_per_sample_ = 0.0f;
};

} // namespace mc1dsp
