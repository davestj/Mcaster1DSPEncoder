// dsp/crossfader.h — Equal-power track crossfader
// Phase 4 — Mcaster1DSPEncoder Linux v1.3.0
//
// Provides two modes:
//  1. Dual-buffer mix: fade from buf_a → buf_b using equal-power curve
//  2. Single-buffer fade-out: fade current track to silence (track ending)
//  3. Single-buffer fade-in:  fade from silence into a new track
//
// Used by EncoderSlot at track boundaries for seamless transitions.
#pragma once

#include <cstddef>
#include <atomic>
#include <functional>

namespace mc1dsp {

struct CrossfaderConfig {
    float duration_sec  = 3.0f;   // crossfade duration in seconds
    bool  enabled       = true;
};

// ---------------------------------------------------------------------------
// DspCrossfader
// ---------------------------------------------------------------------------
class DspCrossfader {
public:
    using DoneCallback = std::function<void()>;

    DspCrossfader() = default;

    void set_config(const CrossfaderConfig& cfg) { cfg_ = cfg; }
    const CrossfaderConfig& config() const { return cfg_; }

    void set_sample_rate(int sr) { sample_rate_ = sr; }
    void set_enabled(bool on)    { cfg_.enabled = on; }
    bool is_enabled()      const { return cfg_.enabled; }

    // Begin a fade-out (current track ending). Optional callback fires when done.
    void begin_fade_out(DoneCallback cb = nullptr);

    // Begin a fade-in (new track just started).
    void begin_fade_in();

    bool is_fading()     const { return fading_.load(); }
    float mix_position() const { return mix_pos_; }

    // Apply fade-out in-place — returns true while fading
    bool apply_fade_out(float* pcm, size_t frames, int channels);

    // Apply fade-in in-place — returns true while fading
    bool apply_fade_in(float* pcm, size_t frames, int channels);

    // Full dual-buffer equal-power mix: buf_a fades out, buf_b fades in.
    // mix_pos drives both curves.  Returns true while fading.
    bool process_mix(const float* buf_a, const float* buf_b, float* out,
                     size_t frames, int channels);

    void reset();

private:
    CrossfaderConfig  cfg_;
    int               sample_rate_ = 44100;
    float             mix_pos_     = 0.0f;   // 0.0 → 1.0
    std::atomic<bool> fading_      {false};
    bool              fade_out_    = false;  // false = fade-in
    DoneCallback      done_cb_;

    float step_per_sample() const;
};

} // namespace mc1dsp
