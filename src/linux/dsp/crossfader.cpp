// dsp/crossfader.cpp — Equal-power crossfader implementation
// Phase 4 — Mcaster1DSPEncoder Linux v1.3.0
//
// Equal-power curves:
//   fade-out gain  = cos(t * π/2)   (1→0 as t: 0→1)
//   fade-in  gain  = sin(t * π/2)   (0→1 as t: 0→1)
// Sum of squares = 1 → constant perceived loudness throughout transition.
#include "crossfader.h"

#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace mc1dsp {

float DspCrossfader::step_per_sample() const
{
    const float dur_s = (cfg_.duration_sec > 0.0f) ? cfg_.duration_sec : 0.001f;
    return 1.0f / (dur_s * static_cast<float>(sample_rate_));
}

void DspCrossfader::begin_fade_out(DoneCallback cb)
{
    if (!cfg_.enabled) { if (cb) cb(); return; }
    done_cb_  = cb;
    fade_out_ = true;
    mix_pos_  = 0.0f;
    fading_.store(true);
}

void DspCrossfader::begin_fade_in()
{
    if (!cfg_.enabled) return;
    fade_out_ = false;
    mix_pos_  = 0.0f;
    fading_.store(true);
}

void DspCrossfader::reset()
{
    mix_pos_ = 0.0f;
    fading_.store(false);
    done_cb_ = nullptr;
}

bool DspCrossfader::apply_fade_out(float* pcm, size_t frames, int channels)
{
    if (!cfg_.enabled || !fading_.load()) return false;

    const float step = step_per_sample();
    const int   ch   = channels;

    for (size_t i = 0; i < frames; i++) {
        // Equal-power fade-out: cos(t * π/2)
        const float gain = cosf(mix_pos_ * static_cast<float>(M_PI) / 2.0f);
        for (int c = 0; c < ch; c++)
            pcm[i * ch + c] *= gain;

        mix_pos_ = std::min(1.0f, mix_pos_ + step);
    }

    if (mix_pos_ >= 1.0f) {
        fading_.store(false);
        mix_pos_ = 0.0f;
        if (done_cb_) { done_cb_(); done_cb_ = nullptr; }
        return false;
    }
    return true;
}

bool DspCrossfader::apply_fade_in(float* pcm, size_t frames, int channels)
{
    if (!cfg_.enabled || !fading_.load()) return false;

    const float step = step_per_sample();
    const int   ch   = channels;

    for (size_t i = 0; i < frames; i++) {
        // Equal-power fade-in: sin(t * π/2)
        const float gain = sinf(mix_pos_ * static_cast<float>(M_PI) / 2.0f);
        for (int c = 0; c < ch; c++)
            pcm[i * ch + c] *= gain;

        mix_pos_ = std::min(1.0f, mix_pos_ + step);
    }

    if (mix_pos_ >= 1.0f) {
        fading_.store(false);
        mix_pos_ = 0.0f;
        return false;
    }
    return true;
}

bool DspCrossfader::process_mix(const float* buf_a, const float* buf_b, float* out,
                                 size_t frames, int channels)
{
    if (!cfg_.enabled || !fading_.load()) {
        const size_t n = frames * static_cast<size_t>(channels);
        for (size_t i = 0; i < n; i++) out[i] = buf_a[i];
        return false;
    }

    const float step = step_per_sample();
    const int   ch   = channels;

    for (size_t i = 0; i < frames; i++) {
        const float t      = mix_pos_;
        const float gain_a = cosf(t * static_cast<float>(M_PI) / 2.0f);
        const float gain_b = sinf(t * static_cast<float>(M_PI) / 2.0f);
        for (int c = 0; c < ch; c++) {
            size_t idx = i * static_cast<size_t>(ch) + static_cast<size_t>(c);
            out[idx] = buf_a[idx] * gain_a + buf_b[idx] * gain_b;
        }
        mix_pos_ = std::min(1.0f, mix_pos_ + step);
    }

    if (mix_pos_ >= 1.0f) {
        fading_.store(false);
        mix_pos_ = 0.0f;
        if (done_cb_) { done_cb_(); done_cb_ = nullptr; }
        return false;
    }
    return true;
}

} // namespace mc1dsp
