// dsp/ptt_duck.cpp — Push-to-Talk audio ducking implementation
// Phase M8 — Mcaster1DSPEncoder macOS
//
// Algorithm: smooth gain ramp between 1.0 (no duck) and duck_target_lin_
// using the same time_const() formula as AGC. When PTT is active, main audio
// is attenuated and PTT mic audio is mixed in at configurable gain.
#include "ptt_duck.h"

#include <cmath>
#include <algorithm>
#include <cstring>

namespace mc1dsp {

// Same time constant as agc.cpp — gives -60 dB in time_ms milliseconds.
static float time_const(float time_ms, float sr)
{
    if (time_ms <= 0.0f || sr <= 0.0f) return 1.0f;
    return 1.0f - expf(-2.2f / (time_ms * sr / 1000.0f));
}

void DspPttDuck::update_coeffs()
{
    const float sr = static_cast<float>(sample_rate_);
    attack_coeff_    = time_const(cfg_.attack_ms,  sr);
    release_coeff_   = time_const(cfg_.release_ms, sr);
    duck_target_lin_ = powf(10.0f, cfg_.duck_level_db / 20.0f);
    mic_gain_lin_    = powf(10.0f, cfg_.mic_gain_db / 20.0f);
}

void DspPttDuck::set_mic_buffer(const float* mic_pcm, size_t mic_frames, int mic_channels,
                                int mic_sample_rate)
{
    /* Copy into internal buffer so the caller (under a mutex) can release
     * the lock before process() runs — no race with the PTT mic PortAudio thread. */
    if (!mic_pcm || mic_frames == 0 || mic_channels < 1) {
        clear_mic_buffer();
        return;
    }
    size_t n = mic_frames * static_cast<size_t>(mic_channels);
    if (n > kMicBufMax) n = kMicBufMax;
    std::memcpy(mic_copy_buf_, mic_pcm, n * sizeof(float));
    mic_pcm_         = mic_copy_buf_;
    mic_frames_      = (n / static_cast<size_t>(mic_channels));
    mic_ch_          = mic_channels;
    mic_sample_rate_ = mic_sample_rate;
}

void DspPttDuck::clear_mic_buffer()
{
    mic_pcm_    = nullptr;
    mic_frames_ = 0;
    mic_ch_     = 0;
}

void DspPttDuck::process(float* pcm, size_t frames, int channels)
{
    if (channels < 1 || channels > 2) return;

    const bool active = ptt_active_.load(std::memory_order_relaxed);

    // Fast path: effect disabled AND PTT not active — nothing to do
    if (!cfg_.enabled && !active) {
        clear_mic_buffer();
        return;
    }

    const int ch = channels;

    // Duck target: attenuate main audio only when effect is enabled AND ptt active
    const float target = (active && cfg_.enabled) ? duck_target_lin_ : 1.0f;

    // Determine whether mic needs resampling (sample rate mismatch).
    // mic_sample_rate_ == 0 means "same as encoder" — no resampling needed.
    const bool have_mic     = active && mic_pcm_ && mic_frames_ > 0;
    const bool need_resample = have_mic
        && mic_sample_rate_ > 0
        && mic_sample_rate_ != sample_rate_;
    const float resample_ratio = need_resample
        ? static_cast<float>(mic_sample_rate_) / static_cast<float>(sample_rate_)
        : 1.0f;

    for (size_t i = 0; i < frames; i++) {
        // Smooth duck gain ramp (only when effect is enabled)
        if (cfg_.enabled) {
            const float coeff = (target < duck_gain_lin_) ? attack_coeff_ : release_coeff_;
            duck_gain_lin_ += coeff * (target - duck_gain_lin_);
            for (int c = 0; c < ch; c++)
                pcm[i * ch + c] *= duck_gain_lin_;
        }

        // Mix in PTT mic audio — with linear interpolation resampling when needed.
        // Works regardless of whether the rack duck effect is enabled, so the mic
        // comes through even without the attenuation duck.
        if (have_mic) {
            if (need_resample) {
                // Linear interpolation: map encoder frame i → fractional mic position
                float  mic_pos = static_cast<float>(i) * resample_ratio;
                size_t mic_i0  = static_cast<size_t>(mic_pos);
                float  frac    = mic_pos - static_cast<float>(mic_i0);
                if (mic_i0 < mic_frames_) {
                    size_t mic_i1 = (mic_i0 + 1 < mic_frames_) ? mic_i0 + 1 : mic_i0;
                    for (int c = 0; c < ch; c++) {
                        int   mic_c = (c < mic_ch_) ? c : 0;
                        float s0    = mic_pcm_[mic_i0 * mic_ch_ + mic_c];
                        float s1    = mic_pcm_[mic_i1 * mic_ch_ + mic_c];
                        pcm[i * ch + c] += (s0 + frac * (s1 - s0)) * mic_gain_lin_;
                    }
                }
            } else {
                // No resampling — direct frame-for-frame mix
                if (i < mic_frames_) {
                    for (int c = 0; c < ch; c++) {
                        int mic_c = (c < mic_ch_) ? c : 0;
                        pcm[i * ch + c] += mic_pcm_[i * mic_ch_ + mic_c] * mic_gain_lin_;
                    }
                }
            }
        }
    }

    // Update metering (positive dB = how much main is being ducked)
    float reduction = (duck_gain_lin_ > 1e-9f)
        ? -20.0f * log10f(duck_gain_lin_)
        : 60.0f;
    duck_reduction_db_.store(reduction, std::memory_order_relaxed);

    clear_mic_buffer();
}

} // namespace mc1dsp
