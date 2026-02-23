// dsp/eq.h — 10-band parametric EQ (RBJ Audio EQ Cookbook biquad IIR)
// Phase 4 — Mcaster1DSPEncoder Linux v1.3.0
#pragma once

#include <array>
#include <cstddef>
#include <string>

namespace mc1dsp {

// ── Biquad filter (direct form I, 2-channel state) ────────────────────────
struct Biquad {
    // Normalized coefficients (a0 = 1 absorbed)
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float              a1 = 0.0f, a2 = 0.0f;

    // Delay-line state — separate per channel (0=L, 1=R)
    float x1[2] = {}, x2[2] = {};
    float y1[2] = {}, y2[2] = {};

    inline float process(float x, int ch)
    {
        float y = b0*x + b1*x1[ch] + b2*x2[ch] - a1*y1[ch] - a2*y2[ch];
        x2[ch] = x1[ch]; x1[ch] = x;
        y2[ch] = y1[ch]; y1[ch] = y;
        return y;
    }

    void reset()
    {
        for (int c = 0; c < 2; c++) x1[c] = x2[c] = y1[c] = y2[c] = 0.0f;
    }
};

// ── Band type ─────────────────────────────────────────────────────────────
enum class EqBandType { LOW_SHELF, PEAKING, HIGH_SHELF };

struct EqBandConfig {
    EqBandType  type    = EqBandType::PEAKING;
    float       freq_hz = 1000.0f;   // center / corner frequency (Hz)
    float       gain_db = 0.0f;      // ±24 dB
    float       q       = 1.0f;      // bandwidth / slope factor
    bool        enabled = true;
    std::string label;
};

// ── 10-band parametric EQ ─────────────────────────────────────────────────
class DspEq {
public:
    static constexpr int NUM_BANDS = 10;

    DspEq();

    void set_sample_rate(int sr);
    void set_band(int index, const EqBandConfig& cfg);
    const EqBandConfig& get_band(int index) const { return bands_[index]; }

    void set_enabled(bool on) { enabled_ = on; }
    bool is_enabled()   const { return enabled_; }

    // Process interleaved PCM in-place (1 or 2 channels)
    void process(float* pcm, size_t frames, int channels);

    void reset();

private:
    int  sample_rate_ = 44100;
    bool enabled_     = false;

    std::array<EqBandConfig, NUM_BANDS> bands_;
    std::array<Biquad,       NUM_BANDS> filters_;

    void recompute(int index);
};

// ── Named presets ─────────────────────────────────────────────────────────
void eq_preset_flat         (DspEq& eq);
void eq_preset_classic_rock (DspEq& eq);
void eq_preset_country       (DspEq& eq);
void eq_preset_modern_rock   (DspEq& eq);
void eq_preset_broadcast     (DspEq& eq);
void eq_preset_spoken_word   (DspEq& eq);

// Apply preset by name: "flat","classic_rock","country","modern_rock","broadcast","spoken_word"
bool eq_apply_preset(DspEq& eq, const std::string& name);

} // namespace mc1dsp
