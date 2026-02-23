// dsp/agc.h — Automatic Gain Control / compressor / hard limiter
// Phase 4 — Mcaster1DSPEncoder Linux v1.3.0
#pragma once

#include <cstddef>

namespace mc1dsp {

struct AgcConfig {
    // Compression threshold in dBFS (gain reduction starts here)
    float threshold_db   = -18.0f;
    // Compression ratio (e.g. 4.0 = 4:1 — for every 4dB over threshold, output rises 1dB)
    float ratio          =   4.0f;
    // Attack time: how fast gain reduces when signal gets loud (ms)
    float attack_ms      =  10.0f;
    // Release time: how fast gain recovers when signal drops (ms)
    float release_ms     = 200.0f;
    // Post-compression makeup gain (dB)
    float makeup_gain_db =   0.0f;
    // Hard limiter ceiling (dBFS) — absolute clip prevention
    float limiter_db     =  -1.0f;
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

    // Current gain reduction in dB (for metering, read from UI thread)
    float gain_reduction_db() const { return gain_reduction_db_; }

    // Reset gain state (call on discontinuity / startup)
    void reset()  { gain_lin_ = 1.0f; gain_reduction_db_ = 0.0f; }

private:
    AgcConfig cfg_;
    int       sample_rate_       = 44100;
    float     gain_lin_          = 1.0f;    // current smoothed gain
    float     attack_coeff_      = 0.0f;
    float     release_coeff_     = 0.0f;
    float     makeup_lin_        = 1.0f;
    float     limiter_ceil_lin_  = 0.891f;  // -1 dBFS
    float     gain_reduction_db_ = 0.0f;

    void  update_coeffs();
    float compute_gain(float peak_lin) const;
};

} // namespace mc1dsp
