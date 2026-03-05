// dsp/agc.h — Automatic Gain Control / compressor / hard limiter
// Mcaster1DSPEncoder macOS — ported from Linux v1.3.0
#pragma once

#include <atomic>
#include <cstddef>

namespace mc1dsp {

struct AgcConfig {
    // Input stage
    float input_gain_db     =   0.0f;   // Pre-compression input gain (dB), -24 to +24
    float gate_threshold_db = -60.0f;   // Noise gate threshold (dBFS), -80 to 0

    // Compression
    float threshold_db   = -18.0f;      // Gain reduction starts here (dBFS)
    float ratio          =   4.0f;      // Compression ratio (e.g. 4.0 = 4:1)
    float attack_ms      =  10.0f;      // How fast gain reduces when signal gets loud (ms)
    float release_ms     = 200.0f;      // How fast gain recovers when signal drops (ms)
    float knee_db        =   6.0f;      // Soft knee width in dB (0 = hard knee)
    float makeup_gain_db =   0.0f;      // Post-compression makeup gain (dB)

    // Limiter
    float limiter_db     =  -1.0f;      // Hard limiter ceiling (dBFS) — clip prevention

    // Enable flag
    bool  enabled        = false;
};

// ---------------------------------------------------------------------------
// DspAgc — feedforward peak compressor + hard limiter
// ---------------------------------------------------------------------------
class DspAgc {
public:
    DspAgc() { update_coeffs(); }

    void set_config(const AgcConfig& cfg)   { cfg_ = cfg; update_coeffs(); }
    const AgcConfig& config()         const { return cfg_; }

    void set_sample_rate(int sr)  { sample_rate_ = sr; update_coeffs(); }
    void set_enabled(bool on)     { cfg_.enabled = on; }
    bool is_enabled()       const { return cfg_.enabled; }

    // Process interleaved PCM in-place (1 or 2 channels)
    void process(float* pcm, size_t frames, int channels);

    // Current gain reduction in dB (for metering, read from UI thread — atomic for thread safety)
    float gain_reduction_db() const { return gain_reduction_db_.load(std::memory_order_relaxed); }

    // Current input peak in dB (for UI metering — atomic for thread safety)
    float input_peak_db() const { return input_peak_db_.load(std::memory_order_relaxed); }

    // Reset gain state (call on discontinuity / startup)
    void reset()  { gain_lin_ = 1.0f; gain_reduction_db_.store(0.0f, std::memory_order_relaxed); }

private:
    AgcConfig cfg_;
    int       sample_rate_       = 44100;
    float     gain_lin_          = 1.0f;    // current smoothed gain
    float     attack_coeff_      = 0.0f;
    float     release_coeff_     = 0.0f;
    float     makeup_lin_        = 1.0f;
    float     input_gain_lin_    = 1.0f;    // pre-compression gain
    float     limiter_ceil_lin_  = 0.891f;  // -1 dBFS
    std::atomic<float> gain_reduction_db_{0.0f};
    std::atomic<float> input_peak_db_{-100.0f};  // for UI metering

    void  update_coeffs();
    float compute_gain(float peak_lin) const;
};

} // namespace mc1dsp
