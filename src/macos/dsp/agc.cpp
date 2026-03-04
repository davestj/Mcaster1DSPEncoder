// dsp/agc.cpp — AGC / compressor / hard limiter implementation
// Phase M5 — Mcaster1DSPEncoder macOS
//
// Algorithm: feedforward peak detection, noise gate, soft knee compression,
// gain smoothing with separate attack/release envelopes, makeup gain,
// hard limiter ceiling.
#include "agc.h"

#include <cmath>
#include <algorithm>

namespace mc1dsp {

// Time constant: 1 - exp(-2.2 / (time_ms * sample_rate / 1000))
// This gives -60 dB in time_ms milliseconds.
static float time_const(float time_ms, float sr)
{
    if (time_ms <= 0.0f || sr <= 0.0f) return 1.0f;
    return 1.0f - expf(-2.2f / (time_ms * sr / 1000.0f));
}

void DspAgc::update_coeffs()
{
    const float sr   = static_cast<float>(sample_rate_);
    attack_coeff_    = time_const(cfg_.attack_ms,  sr);
    release_coeff_   = time_const(cfg_.release_ms, sr);
    makeup_lin_      = powf(10.0f, cfg_.makeup_gain_db / 20.0f);
    input_gain_lin_  = powf(10.0f, cfg_.input_gain_db / 20.0f);
    limiter_ceil_lin_= powf(10.0f, cfg_.limiter_db / 20.0f);
}

// Compute ideal target gain for a given input peak level.
// Returns linear gain to apply (including makeup) before the hard limiter.
// Implements soft knee when knee_db > 0.
float DspAgc::compute_gain(float peak_lin) const
{
    if (peak_lin < 1e-9f) return makeup_lin_;

    const float peak_db = 20.0f * log10f(peak_lin);
    float gain_db = cfg_.makeup_gain_db;

    const float half_knee = cfg_.knee_db * 0.5f;

    if (cfg_.knee_db > 0.0f &&
        peak_db > (cfg_.threshold_db - half_knee) &&
        peak_db < (cfg_.threshold_db + half_knee)) {
        // Soft knee region: quadratic blend
        float x = peak_db - cfg_.threshold_db + half_knee;
        gain_db += (1.0f / cfg_.ratio - 1.0f) * x * x / (2.0f * cfg_.knee_db);
    } else if (peak_db >= cfg_.threshold_db + (cfg_.knee_db > 0.0f ? half_knee : 0.0f)) {
        // Above threshold (or above knee): full compression
        const float over_db = peak_db - cfg_.threshold_db;
        gain_db -= over_db * (1.0f - 1.0f / cfg_.ratio);
    }
    // else: below threshold — gain_db stays as makeup_gain_db only

    // Clamp so output doesn't push above limiter ceiling
    const float out_db = peak_db + gain_db;
    if (out_db > cfg_.limiter_db)
        return powf(10.0f, (cfg_.limiter_db - peak_db) / 20.0f);

    return powf(10.0f, gain_db / 20.0f);
}

void DspAgc::process(float* pcm, size_t frames, int channels)
{
    if (!cfg_.enabled || channels < 1 || channels > 2) return;

    const int ch = channels;
    float max_input_peak = 0.0f;

    for (size_t i = 0; i < frames; i++) {
        // Apply input gain
        if (input_gain_lin_ != 1.0f) {
            for (int c = 0; c < ch; c++)
                pcm[i * ch + c] *= input_gain_lin_;
        }

        // Peak detect across all channels for this sample frame
        float peak = 0.0f;
        for (int c = 0; c < ch; c++) {
            float s = fabsf(pcm[i * ch + c]);
            if (s > peak) peak = s;
        }
        if (peak > max_input_peak) max_input_peak = peak;

        // Noise gate: if signal is below gate threshold, mute
        float peak_db = (peak > 1e-9f) ? 20.0f * log10f(peak) : -100.0f;
        if (peak_db < cfg_.gate_threshold_db) {
            for (int c = 0; c < ch; c++)
                pcm[i * ch + c] = 0.0f;
            continue;
        }

        // Compute ideal gain (with soft knee)
        const float desired = compute_gain(peak);

        // Smooth with attack (fast) or release (slow) time constant
        const float coeff = (desired < gain_lin_) ? attack_coeff_ : release_coeff_;
        gain_lin_ += coeff * (desired - gain_lin_);

        // Apply gain + hard limiter ceiling
        for (int c = 0; c < ch; c++) {
            float s = pcm[i * ch + c] * gain_lin_;
            if (s >  limiter_ceil_lin_) s =  limiter_ceil_lin_;
            if (s < -limiter_ceil_lin_) s = -limiter_ceil_lin_;
            pcm[i * ch + c] = s;
        }
    }

    // Update metering values (atomic for UI thread reads)
    gain_reduction_db_.store(-20.0f * log10f(gain_lin_ + 1e-9f), std::memory_order_relaxed);
    float ip = (max_input_peak > 1e-9f) ? 20.0f * log10f(max_input_peak) : -100.0f;
    input_peak_db_.store(ip, std::memory_order_relaxed);
}

} // namespace mc1dsp
