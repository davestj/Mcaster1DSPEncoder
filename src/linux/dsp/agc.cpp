// dsp/agc.cpp — AGC / compressor / hard limiter implementation
// Phase 4 — Mcaster1DSPEncoder Linux v1.3.0
//
// Algorithm: feedforward peak detection, gain smoothing with separate
// attack/release envelopes, makeup gain, hard limiter ceiling.
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
    limiter_ceil_lin_= powf(10.0f, cfg_.limiter_db / 20.0f);
}

// Compute ideal target gain for a given input peak level.
// Returns linear gain to apply (including makeup) before the hard limiter.
float DspAgc::compute_gain(float peak_lin) const
{
    if (peak_lin < 1e-9f) return makeup_lin_;

    const float peak_db = 20.0f * log10f(peak_lin);

    if (peak_db <= cfg_.threshold_db)
        return makeup_lin_;  // below threshold: unity + makeup

    // Compression curve: gain_db = -(over * (1 - 1/ratio))
    const float over_db  = peak_db - cfg_.threshold_db;
    const float gain_db  = cfg_.makeup_gain_db
                         - over_db * (1.0f - 1.0f / cfg_.ratio);

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

    for (size_t i = 0; i < frames; i++) {
        // Peak detect across all channels for this sample frame
        float peak = 0.0f;
        for (int c = 0; c < ch; c++) {
            float s = fabsf(pcm[i * ch + c]);
            if (s > peak) peak = s;
        }

        // Compute ideal gain
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

    // Update metering value (gain reduction relative to unity)
    gain_reduction_db_ = -20.0f * log10f(gain_lin_ + 1e-9f);
}

} // namespace mc1dsp
