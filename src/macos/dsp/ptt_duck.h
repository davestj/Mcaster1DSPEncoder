// dsp/ptt_duck.h — Push-to-Talk audio ducking module
// Phase M8 — Mcaster1DSPEncoder macOS
//
// When PTT is active: duck main audio by configurable dB, mix in PTT mic audio.
// Uses smooth attack/release gain ramping to avoid clicks.
#pragma once

#include <atomic>
#include <cstddef>

namespace mc1dsp {

struct PttDuckConfig {
    bool  enabled       = false;
    float duck_level_db = -12.0f;  // main audio attenuation when PTT active (-30 to 0 dB)
    float attack_ms     = 5.0f;    // how fast ducking kicks in (ms)
    float release_ms    = 100.0f;  // how fast ducking recovers (ms)
    float mic_gain_db   = 0.0f;    // PTT mic mix level (-12 to +12 dB)
};

// ---------------------------------------------------------------------------
// DspPttDuck — main audio ducking + PTT mic mixing
// ---------------------------------------------------------------------------
class DspPttDuck {
public:
    DspPttDuck() { update_coeffs(); }

    void set_config(const PttDuckConfig& cfg)  { cfg_ = cfg; update_coeffs(); }
    const PttDuckConfig& config()        const { return cfg_; }

    void set_sample_rate(int sr) { sample_rate_ = sr; update_coeffs(); }
    void set_enabled(bool on)    { cfg_.enabled = on; }
    bool is_enabled()      const { return cfg_.enabled; }

    // Set PTT state from UI thread (atomic, real-time safe)
    void set_ptt_active(bool active) { ptt_active_.store(active, std::memory_order_relaxed); }
    bool is_ptt_active()       const { return ptt_active_.load(std::memory_order_relaxed); }

    // Feed PTT microphone PCM data (called before process, from same audio callback).
    // Stores a pointer — caller must keep buffer valid through process() call.
    void set_mic_buffer(const float* mic_pcm, size_t mic_frames, int mic_channels);

    // Clear mic buffer reference (call after process or when no mic data available)
    void clear_mic_buffer();

    // Process main audio in-place: duck main audio + mix in PTT mic when active
    void process(float* pcm, size_t frames, int channels);

    // Current duck attenuation in dB (for UI metering — atomic)
    float duck_reduction_db() const { return duck_reduction_db_.load(std::memory_order_relaxed); }

    // Reset gain state (call on discontinuity)
    void reset() { duck_gain_lin_ = 1.0f; duck_reduction_db_.store(0.0f, std::memory_order_relaxed); }

private:
    PttDuckConfig cfg_;
    int   sample_rate_       = 44100;

    std::atomic<bool> ptt_active_{false};
    float duck_gain_lin_     = 1.0f;    // current smoothed duck gain (1.0 = no duck)
    float attack_coeff_      = 0.0f;
    float release_coeff_     = 0.0f;
    float duck_target_lin_   = 0.25f;   // 10^(duck_level_db/20)
    float mic_gain_lin_      = 1.0f;    // 10^(mic_gain_db/20)

    // Mic buffer reference (set each callback, not owned)
    const float* mic_pcm_    = nullptr;
    size_t       mic_frames_ = 0;
    int          mic_ch_     = 0;

    std::atomic<float> duck_reduction_db_{0.0f};

    void update_coeffs();
};

} // namespace mc1dsp
