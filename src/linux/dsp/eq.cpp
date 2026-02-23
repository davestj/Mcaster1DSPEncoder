// dsp/eq.cpp — 10-band parametric EQ implementation
// Phase 4 — Mcaster1DSPEncoder Linux v1.3.0
//
// Coefficient formulas from the RBJ Audio EQ Cookbook (Robert Bristow-Johnson):
//   https://www.musicdsp.org/files/Audio-EQ-Cookbook.txt
#include "eq.h"

#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace mc1dsp {

// Default 10-band layout for broadcast use
DspEq::DspEq()
{
    struct BandDef { EqBandType t; float f; float q; const char* label; };
    static constexpr BandDef kDefs[NUM_BANDS] = {
        { EqBandType::LOW_SHELF,    80.0f,  0.707f, "Low Shelf 80Hz"  },
        { EqBandType::PEAKING,     150.0f,  1.0f,   "150 Hz"          },
        { EqBandType::PEAKING,     400.0f,  1.0f,   "400 Hz"          },
        { EqBandType::PEAKING,     800.0f,  1.0f,   "800 Hz"          },
        { EqBandType::PEAKING,    1500.0f,  1.2f,   "1.5 kHz"         },
        { EqBandType::PEAKING,    3000.0f,  1.2f,   "3 kHz"           },
        { EqBandType::PEAKING,    5000.0f,  1.2f,   "5 kHz"           },
        { EqBandType::PEAKING,    8000.0f,  1.2f,   "8 kHz"           },
        { EqBandType::PEAKING,   12000.0f,  1.2f,   "12 kHz"          },
        { EqBandType::HIGH_SHELF, 16000.0f, 0.707f, "High Shelf 16kHz"},
    };

    for (int i = 0; i < NUM_BANDS; i++) {
        bands_[i].type    = kDefs[i].t;
        bands_[i].freq_hz = kDefs[i].f;
        bands_[i].q       = kDefs[i].q;
        bands_[i].gain_db = 0.0f;
        bands_[i].enabled = true;
        bands_[i].label   = kDefs[i].label;
        recompute(i);
    }
}

void DspEq::set_sample_rate(int sr)
{
    sample_rate_ = (sr > 0) ? sr : 44100;
    for (int i = 0; i < NUM_BANDS; i++) recompute(i);
    reset();
}

void DspEq::set_band(int index, const EqBandConfig& cfg)
{
    if (index < 0 || index >= NUM_BANDS) return;
    bands_[index] = cfg;
    recompute(index);
    filters_[index].reset();
}

void DspEq::reset()
{
    for (auto& f : filters_) f.reset();
}

void DspEq::process(float* pcm, size_t frames, int channels)
{
    if (!enabled_ || channels < 1 || channels > 2) return;

    for (int bi = 0; bi < NUM_BANDS; bi++) {
        if (!bands_[bi].enabled || bands_[bi].gain_db == 0.0f) continue;
        auto& filt = filters_[bi];
        const int ch = channels;
        for (size_t i = 0; i < frames; i++) {
            pcm[i * ch + 0] = filt.process(pcm[i * ch + 0], 0);
            if (ch == 2)
                pcm[i * ch + 1] = filt.process(pcm[i * ch + 1], 1);
        }
    }
}

// ── RBJ biquad coefficient computation ───────────────────────────────────
void DspEq::recompute(int index)
{
    auto& cfg  = bands_[index];
    auto& filt = filters_[index];

    const float sr    = static_cast<float>(sample_rate_);
    const float omega = 2.0f * static_cast<float>(M_PI) * cfg.freq_hz / sr;
    const float sin_w = sinf(omega);
    const float cos_w = cosf(omega);
    const float A     = powf(10.0f, cfg.gain_db / 40.0f);
    const float sqrtA = sqrtf(A);

    float b0, b1, b2, a0, a1, a2, alpha;

    switch (cfg.type) {

    case EqBandType::PEAKING:
        alpha = sin_w / (2.0f * cfg.q);
        b0 =  1.0f + alpha * A;
        b1 = -2.0f * cos_w;
        b2 =  1.0f - alpha * A;
        a0 =  1.0f + alpha / A;
        a1 = -2.0f * cos_w;
        a2 =  1.0f - alpha / A;
        break;

    case EqBandType::LOW_SHELF:
        alpha = sin_w / 2.0f * sqrtf((A + 1.0f/A) * (1.0f/cfg.q - 1.0f) + 2.0f);
        b0 =     A * ((A+1.0f) - (A-1.0f)*cos_w + 2.0f*sqrtA*alpha);
        b1 = 2.0f*A * ((A-1.0f) - (A+1.0f)*cos_w);
        b2 =     A * ((A+1.0f) - (A-1.0f)*cos_w - 2.0f*sqrtA*alpha);
        a0 =          (A+1.0f) + (A-1.0f)*cos_w + 2.0f*sqrtA*alpha;
        a1 = -2.0f *  ((A-1.0f) + (A+1.0f)*cos_w);
        a2 =          (A+1.0f) + (A-1.0f)*cos_w - 2.0f*sqrtA*alpha;
        break;

    case EqBandType::HIGH_SHELF:
        alpha = sin_w / 2.0f * sqrtf((A + 1.0f/A) * (1.0f/cfg.q - 1.0f) + 2.0f);
        b0 =     A * ((A+1.0f) + (A-1.0f)*cos_w + 2.0f*sqrtA*alpha);
        b1 = -2.0f*A * ((A-1.0f) + (A+1.0f)*cos_w);
        b2 =     A * ((A+1.0f) + (A-1.0f)*cos_w - 2.0f*sqrtA*alpha);
        a0 =          (A+1.0f) - (A-1.0f)*cos_w + 2.0f*sqrtA*alpha;
        a1 =  2.0f *  ((A-1.0f) - (A+1.0f)*cos_w);
        a2 =          (A+1.0f) - (A-1.0f)*cos_w - 2.0f*sqrtA*alpha;
        break;

    default:
        // Identity
        filt.b0 = 1.0f; filt.b1 = 0.0f; filt.b2 = 0.0f;
        filt.a1 = 0.0f; filt.a2 = 0.0f;
        return;
    }

    // Normalize by a0
    filt.b0 = b0 / a0;
    filt.b1 = b1 / a0;
    filt.b2 = b2 / a0;
    filt.a1 = a1 / a0;
    filt.a2 = a2 / a0;
}

// ── Presets ────────────────────────────────────────────────────────────────

void eq_preset_flat(DspEq& eq)
{
    for (int i = 0; i < DspEq::NUM_BANDS; i++) {
        auto cfg = eq.get_band(i);
        cfg.gain_db = 0.0f;
        eq.set_band(i, cfg);
    }
}

void eq_preset_classic_rock(DspEq& eq)
{
    // Big bottom, guitar presence, air — Led Zeppelin to AC/DC
    static constexpr float g[DspEq::NUM_BANDS] = {
        +4.5f,  // 80 Hz  — kick drum punch, bass foundation
        +2.5f,  // 150 Hz — bass guitar warmth
         0.0f,  // 400 Hz — neutral mid
        -1.5f,  // 800 Hz — tighten muddy mid
        +2.0f,  // 1.5k   — guitar mid presence
        +4.5f,  // 3k     — electric guitar bite and attack
        +3.0f,  // 5k     — pick attack, string clarity
        +2.0f,  // 8k     — cymbals, hi-hat sparkle
        +2.5f,  // 12k    — brilliance
        +1.5f,  // 16k    — air and shimmer
    };
    for (int i = 0; i < DspEq::NUM_BANDS; i++) {
        auto cfg = eq.get_band(i);
        cfg.gain_db = g[i];
        eq.set_band(i, cfg);
    }
}

void eq_preset_country(DspEq& eq)
{
    // Warm low-mids, steel guitar twang, vocal intelligibility
    static constexpr float g[DspEq::NUM_BANDS] = {
        +2.5f,  // 80 Hz  — subtle kick, bass
        +3.5f,  // 150 Hz — acoustic guitar body, warmth
        +2.0f,  // 400 Hz — warm mid body
        -1.0f,  // 800 Hz — reduce box resonance
        +2.5f,  // 1.5k   — vocal clarity and presence
        +3.5f,  // 3k     — steel guitar twang, fiddle articulation
        +2.5f,  // 5k     — brightness, definition
        +2.0f,  // 8k     — sparkle, snare crack
        +1.5f,  // 12k    — air
        +1.0f,  // 16k    — open top end
    };
    for (int i = 0; i < DspEq::NUM_BANDS; i++) {
        auto cfg = eq.get_band(i);
        cfg.gain_db = g[i];
        eq.set_band(i, cfg);
    }
}

void eq_preset_modern_rock(DspEq& eq)
{
    // Punchy modern rock: tight bass, forward midrange, crisp highs
    static constexpr float g[DspEq::NUM_BANDS] = {
        +5.0f,  // 80 Hz  — sub bass slam
        +1.5f,  // 150 Hz — bass body (keep tight)
        -1.0f,  // 400 Hz — clear low-mid mud
        -2.0f,  // 800 Hz — cut the honk
        +3.0f,  // 1.5k   — forward midrange aggression
        +5.0f,  // 3k     — guitar crunch, vocal cut
        +3.5f,  // 5k     — presence, definition
        +2.5f,  // 8k     — cymbals, transient detail
        +2.0f,  // 12k    — top-end clarity
        +1.5f,  // 16k    — high-shelf air
    };
    for (int i = 0; i < DspEq::NUM_BANDS; i++) {
        auto cfg = eq.get_band(i);
        cfg.gain_db = g[i];
        eq.set_band(i, cfg);
    }
}

void eq_preset_broadcast(DspEq& eq)
{
    // Broadcast standard: gentle smile curve, wide listener appeal
    static constexpr float g[DspEq::NUM_BANDS] = {
        +3.0f,  // 80 Hz
        +1.5f,  // 150 Hz
         0.0f,  // 400 Hz
        -1.0f,  // 800 Hz
        +0.5f,  // 1.5k
        +1.5f,  // 3k
        +2.0f,  // 5k
        +2.5f,  // 8k
        +2.0f,  // 12k
        +1.5f,  // 16k
    };
    for (int i = 0; i < DspEq::NUM_BANDS; i++) {
        auto cfg = eq.get_band(i);
        cfg.gain_db = g[i];
        eq.set_band(i, cfg);
    }
}

void eq_preset_spoken_word(DspEq& eq)
{
    // DJ/podcast voice: cut rumble and sub, boost vocal intelligibility
    static constexpr float g[DspEq::NUM_BANDS] = {
        -8.0f,  // 80 Hz  — cut room rumble, handling noise
        +2.0f,  // 150 Hz — voice body and warmth
        +3.5f,  // 400 Hz — vocal presence
        +3.0f,  // 800 Hz — male voice fundamentals
        +4.0f,  // 1.5k   — intelligibility sweet spot
        +3.0f,  // 3k     — consonant clarity
        -1.5f,  // 5k     — reduce sibilance edge
        -2.5f,  // 8k     — reduce harshness
        -3.0f,  // 12k    — natural de-air
        -2.0f,  // 16k    — cut noise floor
    };
    for (int i = 0; i < DspEq::NUM_BANDS; i++) {
        auto cfg = eq.get_band(i);
        cfg.gain_db = g[i];
        eq.set_band(i, cfg);
    }
}

bool eq_apply_preset(DspEq& eq, const std::string& name)
{
    if (name == "flat")          { eq_preset_flat(eq);          return true; }
    if (name == "classic_rock")  { eq_preset_classic_rock(eq);  return true; }
    if (name == "country")       { eq_preset_country(eq);       return true; }
    if (name == "modern_rock")   { eq_preset_modern_rock(eq);   return true; }
    if (name == "broadcast")     { eq_preset_broadcast(eq);     return true; }
    if (name == "spoken_word")   { eq_preset_spoken_word(eq);   return true; }
    return false;
}

} // namespace mc1dsp
