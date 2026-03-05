/*
 * Mcaster1DSPEncoder — DJ Crossfader Implementation
 * dsp/dj_crossfader.cpp
 *
 * We implement the dual-deck crossfader blend with one-pole IIR smoothing.
 * The smoothing removes zipper noise when the fader position changes rapidly.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dj_crossfader.h"
#include <algorithm>
#include <cmath>

namespace mc1dsp {

/* ── blend() — real-time dual-buffer crossfade ─────────────────────────────── */

void DjCrossfader::blend(const float* deck_a, const float* deck_b, float* out,
                         size_t frames, int channels)
{
    const float k = smooth_k_;
    const mc1xf::Curve cv = static_cast<mc1xf::Curve>(curve_.load());
    const bool rev = reversed_.load();
    float sp = smoothed_pos_;

    for (size_t f = 0; f < frames; ++f) {
        /* We read the target position (may be updated by UI thread at any time) */
        float raw = target_pos_.load(std::memory_order_relaxed);
        float eff = rev ? (1.0f - raw) : raw;

        /* We apply one-pole IIR smoother: removes zipper noise on fader drags */
        sp = sp * k + eff * (1.0f - k);

        /* We compute gains using the selected curve algorithm */
        const mc1xf::Gains g = mc1xf::computeGains(sp, cv);

        /* We blend deck_a and deck_b into output */
        const size_t base = f * channels;
        for (int c = 0; c < channels; ++c) {
            out[base + c] = deck_a[base + c] * g.a + deck_b[base + c] * g.b;
        }
    }

    smoothed_pos_ = sp;
    last_gains_ = mc1xf::computeGains(sp, cv);
}

/* ── Auto-crossfade ────────────────────────────────────────────────────────── */

void DjCrossfader::start_auto_crossfade(float target, float duration_sec)
{
    if (duration_sec <= 0.0f || sample_rate_ <= 0) return;

    auto_start_pos_ = smoothed_pos_;
    auto_end_pos_   = std::clamp(target, 0.0f, 1.0f);
    auto_progress_  = 0.0f;

    /* We compute the position increment per sample */
    float total_samples = duration_sec * static_cast<float>(sample_rate_);
    auto_step_per_sample_ = (total_samples > 0.0f) ? (1.0f / total_samples) : 1.0f;

    auto_active_.store(true);
}

bool DjCrossfader::auto_advance(size_t frames)
{
    if (!auto_active_.load()) return false;

    auto_progress_ += auto_step_per_sample_ * static_cast<float>(frames);
    if (auto_progress_ >= 1.0f) {
        auto_progress_ = 1.0f;
        auto_active_.store(false);
    }

    /* We interpolate between start and end positions */
    float pos = auto_start_pos_ + (auto_end_pos_ - auto_start_pos_) * auto_progress_;
    target_pos_.store(pos);

    return auto_active_.load();
}

/* ── Reset ─────────────────────────────────────────────────────────────────── */

void DjCrossfader::reset()
{
    target_pos_.store(0.0f);
    smoothed_pos_ = 0.0f;
    last_gains_ = {1.0f, 0.0f};
    auto_active_.store(false);
    auto_progress_ = 0.0f;
}

} // namespace mc1dsp
