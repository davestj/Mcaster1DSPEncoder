// dsp/ptt_duck.cpp — Push-to-Talk audio ducking implementation
// Phase M8 — Mcaster1DSPEncoder macOS
//
// Algorithm: smooth gain ramp between 1.0 (no duck) and duck_target_lin_
// using the same time_const() formula as AGC. When PTT is active, main audio
// is attenuated and PTT mic audio is mixed in at configurable gain.
#include "ptt_duck.h"

#include <cmath>
#include <algorithm>

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

void DspPttDuck::set_mic_buffer(const float* mic_pcm, size_t mic_frames, int mic_channels)
{
    mic_pcm_    = mic_pcm;
    mic_frames_ = mic_frames;
    mic_ch_     = mic_channels;
}

void DspPttDuck::clear_mic_buffer()
{
    mic_pcm_    = nullptr;
    mic_frames_ = 0;
    mic_ch_     = 0;
}

void DspPttDuck::process(float* pcm, size_t frames, int channels)
{
    if (!cfg_.enabled || channels < 1 || channels > 2) return;

    const bool active = ptt_active_.load(std::memory_order_relaxed);
    const float target = active ? duck_target_lin_ : 1.0f;
    const int ch = channels;

    for (size_t i = 0; i < frames; i++) {
        // Smooth gain ramp toward target
        const float coeff = (target < duck_gain_lin_) ? attack_coeff_ : release_coeff_;
        duck_gain_lin_ += coeff * (target - duck_gain_lin_);

        // Apply duck gain to main audio
        for (int c = 0; c < ch; c++)
            pcm[i * ch + c] *= duck_gain_lin_;

        // Mix in PTT mic audio when active and mic buffer is available
        if (active && mic_pcm_ && i < mic_frames_) {
            for (int c = 0; c < ch; c++) {
                // If mic is mono but main is stereo, use mic[0] for both channels
                int mic_c = (c < mic_ch_) ? c : 0;
                pcm[i * ch + c] += mic_pcm_[i * mic_ch_ + mic_c] * mic_gain_lin_;
            }
        }
    }

    // Update metering (show how much we're ducking, in positive dB)
    float reduction = (duck_gain_lin_ > 1e-9f)
        ? -20.0f * log10f(duck_gain_lin_)
        : 60.0f;
    duck_reduction_db_.store(reduction, std::memory_order_relaxed);

    // Clear mic buffer reference after processing
    clear_mic_buffer();
}

} // namespace mc1dsp
