/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * dsp/sonic_enhancer.cpp — BBE Sonic Maximizer clone implementation
 *
 * BBE Sonic Maximizer architecture (3-band phase-corrective processor):
 *   1. 3-band Linkwitz-Riley 4th-order crossover at 150 Hz and 1.2 kHz
 *   2. Phase alignment via delay lines (low ~2.5ms, mid ~0.5ms)
 *   3. Lo Contour: low shelf boost (0-10 → 0-12 dB at 80 Hz)
 *   4. Process: high shelf boost + soft saturation (0-10 → 0-12 dB at 3 kHz)
 *   5. Recombine three bands
 *   6. Output gain (-12 to +6 dB)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "sonic_enhancer.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace mc1dsp {

/* ── Presets ───────────────────────────────────────────────────────────── */
static const SonicEnhancerPreset kPresets[] = {
    { "bypass", "All processing disabled",
      { 0.0f, 0.0f, 0.0f, false } },
    { "subtle_warmth", "Gentle bass warmth and high-frequency clarity",
      { 3.0f, 3.0f, 0.0f, true } },
    { "broadcast_standard", "Balanced broadcast enhancement",
      { 5.0f, 5.0f, 0.0f, true } },
    { "rock_pop", "Aggressive enhancement for rock and pop music",
      { 7.0f, 7.0f, 1.5f, true } },
    { "voice_clarity", "Optimized for speech intelligibility",
      { 2.0f, 8.0f, 0.0f, true } },
    { "maximum_impact", "Maximum bass and presence impact",
      { 10.0f, 10.0f, 3.0f, true } },
};

static constexpr int kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

const SonicEnhancerPreset* DspSonicEnhancer::builtin_presets() { return kPresets; }
int DspSonicEnhancer::builtin_preset_count() { return kPresetCount; }

/* ── Constructor ──────────────────────────────────────────────────────── */

DspSonicEnhancer::DspSonicEnhancer()
{
    /* Default: allocate delay lines for up to 48 kHz
     * 2.5ms @ 48kHz = 120 samples, 0.5ms @ 48kHz = 24 samples */
    for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
        delay_lo_[ch].init(256);   // enough for 2.5ms @ 96kHz
        delay_mid_[ch].init(64);   // enough for 0.5ms @ 96kHz
    }
}

void DspSonicEnhancer::configure(const SonicEnhancerConfig& cfg, int sample_rate)
{
    cfg_ = cfg;
    sample_rate_ = (sample_rate > 0) ? sample_rate : 44100;

    /* Clamp parameters */
    cfg_.lo_contour  = std::clamp(cfg_.lo_contour,  LO_CONTOUR_MIN, LO_CONTOUR_MAX);
    cfg_.process     = std::clamp(cfg_.process,      PROCESS_MIN,    PROCESS_MAX);
    cfg_.output_gain = std::clamp(cfg_.output_gain,  OUTPUT_MIN,     OUTPUT_MAX);

    /* Output gain: dB to linear */
    output_linear_ = std::pow(10.0f, cfg_.output_gain / 20.0f);

    /* Reinitialize delay lines for current sample rate */
    size_t lo_delay  = static_cast<size_t>(0.0025f * sample_rate_); // 2.5ms
    size_t mid_delay = static_cast<size_t>(0.0005f * sample_rate_); // 0.5ms

    for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
        delay_lo_[ch].init(lo_delay + 16);
        delay_lo_[ch].set_delay(lo_delay);
        delay_mid_[ch].init(mid_delay + 16);
        delay_mid_[ch].set_delay(mid_delay);
    }

    /* Compute crossover filters */
    for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
        auto& x = xover_[ch];
        /* Crossover 1: 150 Hz */
        compute_crossover(150.0f, x.lp1_a, x.lp1_b, x.hp1_a, x.hp1_b);
        /* Crossover 2: 1200 Hz */
        compute_crossover(1200.0f, x.lp2_a, x.lp2_b, x.hp2_a, x.hp2_b);
    }

    /* Lo Contour: 0-10 maps to 0-12 dB low shelf at 80 Hz */
    float lo_db = cfg_.lo_contour * 1.2f; // 0-10 → 0-12 dB
    for (int ch = 0; ch < MAX_CHANNELS; ++ch)
        compute_low_shelf(lo_db, 80.0f, lo_shelf_[ch]);

    /* Process: 0-10 maps to 0-12 dB high shelf at 3 kHz */
    float hi_db = cfg_.process * 1.2f; // 0-10 → 0-12 dB
    for (int ch = 0; ch < MAX_CHANNELS; ++ch)
        compute_high_shelf(hi_db, 3000.0f, hi_shelf_[ch]);
}

void DspSonicEnhancer::reset()
{
    for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
        auto& x = xover_[ch];
        x.lp1_a.reset(); x.lp1_b.reset();
        x.hp1_a.reset(); x.hp1_b.reset();
        x.lp2_a.reset(); x.lp2_b.reset();
        x.hp2_a.reset(); x.hp2_b.reset();
        delay_lo_[ch].reset();
        delay_mid_[ch].reset();
        lo_shelf_[ch].reset();
        hi_shelf_[ch].reset();
    }
}

/* ── Main processing ─────────────────────────────────────────────────── */

void DspSonicEnhancer::process(float* pcm, size_t frames, int channels)
{
    if (!cfg_.enabled || channels < 1 || channels > 2) return;

    for (size_t i = 0; i < frames; ++i) {
        for (int ch = 0; ch < channels; ++ch) {
            float in = pcm[i * channels + ch];
            auto& x = xover_[ch];

            /* ── Step 1: 3-band crossover split ── */
            /* Crossover 1 (150 Hz): extract LOW, pass MID+HIGH */
            float lo1  = x.lp1_a.process(in, 0);
                  lo1  = x.lp1_b.process(lo1, 0);
            float hi1  = x.hp1_a.process(in, 0);
                  hi1  = x.hp1_b.process(hi1, 0);

            /* Crossover 2 (1.2 kHz): split MID+HIGH into MID and HIGH */
            float mid  = x.lp2_a.process(hi1, 0);
                  mid  = x.lp2_b.process(mid, 0);
            float high = x.hp2_a.process(hi1, 0);
                  high = x.hp2_b.process(high, 0);

            /* ── Step 2: Phase alignment delay ── */
            /* Delay LOW by ~2.5ms, MID by ~0.5ms, HIGH arrives first */
            float lo_delayed  = delay_lo_[ch].process(lo1);
            float mid_delayed = delay_mid_[ch].process(mid);

            /* ── Step 3: Lo Contour (bass enhancement) ── */
            float lo_enhanced = lo_shelf_[ch].process(lo_delayed, 0);

            /* ── Step 4: Process (presence + harmonic exciter) ── */
            float hi_enhanced = hi_shelf_[ch].process(high, 0);
            /* Soft saturation on HIGH band for harmonic richness */
            if (cfg_.process > 0.5f) {
                float drive = 1.0f + cfg_.process * 0.15f; // subtle drive
                hi_enhanced = soft_saturate(hi_enhanced * drive) / drive;
            }

            /* ── Step 5: Recombine bands ── */
            float out = lo_enhanced + mid_delayed + hi_enhanced;

            /* ── Step 6: Output gain ── */
            out *= output_linear_;

            pcm[i * channels + ch] = out;
        }
    }
}

/* ── Linkwitz-Riley 4th-order crossover computation ───────────────────
 * LR4 = two cascaded Butterworth 2nd-order filters.
 * Both LP and HP outputs have flat magnitude sum and zero phase offset
 * at the crossover frequency.
 */
void DspSonicEnhancer::compute_crossover(float freq,
                                          Biquad& lp_a, Biquad& lp_b,
                                          Biquad& hp_a, Biquad& hp_b)
{
    const float sr = static_cast<float>(sample_rate_);
    const float w0 = 2.0f * static_cast<float>(M_PI) * freq / sr;
    const float cos_w = cosf(w0);
    const float sin_w = sinf(w0);
    /* Butterworth Q = 0.7071 (1/sqrt(2)) */
    const float alpha = sin_w / (2.0f * 0.7071067811865476f);

    /* 2nd-order Butterworth low-pass */
    {
        float a0 = 1.0f + alpha;
        float b0 = (1.0f - cos_w) / 2.0f;
        float b1 =  1.0f - cos_w;
        float b2 = (1.0f - cos_w) / 2.0f;
        float a1 = -2.0f * cos_w;
        float a2 =  1.0f - alpha;

        lp_a.b0 = b0/a0; lp_a.b1 = b1/a0; lp_a.b2 = b2/a0;
        lp_a.a1 = a1/a0; lp_a.a2 = a2/a0;
        /* Second stage: same coefficients for LR4 */
        lp_b.b0 = lp_a.b0; lp_b.b1 = lp_a.b1; lp_b.b2 = lp_a.b2;
        lp_b.a1 = lp_a.a1; lp_b.a2 = lp_a.a2;
    }

    /* 2nd-order Butterworth high-pass */
    {
        float a0 = 1.0f + alpha;
        float b0 = (1.0f + cos_w) / 2.0f;
        float b1 = -(1.0f + cos_w);
        float b2 = (1.0f + cos_w) / 2.0f;
        float a1 = -2.0f * cos_w;
        float a2 =  1.0f - alpha;

        hp_a.b0 = b0/a0; hp_a.b1 = b1/a0; hp_a.b2 = b2/a0;
        hp_a.a1 = a1/a0; hp_a.a2 = a2/a0;
        hp_b.b0 = hp_a.b0; hp_b.b1 = hp_a.b1; hp_b.b2 = hp_a.b2;
        hp_b.a1 = hp_a.a1; hp_b.a2 = hp_a.a2;
    }

    lp_a.reset(); lp_b.reset();
    hp_a.reset(); hp_b.reset();
}

/* ── Low shelf filter (RBJ) ──────────────────────────────────────────── */
void DspSonicEnhancer::compute_low_shelf(float gain_db, float freq, Biquad& filt)
{
    if (gain_db < 0.01f) {
        filt.b0 = 1.0f; filt.b1 = 0.0f; filt.b2 = 0.0f;
        filt.a1 = 0.0f; filt.a2 = 0.0f;
        return;
    }

    const float sr = static_cast<float>(sample_rate_);
    const float A  = powf(10.0f, gain_db / 40.0f);
    const float w0 = 2.0f * static_cast<float>(M_PI) * freq / sr;
    const float cos_w = cosf(w0);
    const float sin_w = sinf(w0);
    const float sqrtA = sqrtf(A);
    const float alpha = sin_w / 2.0f * sqrtf((A + 1.0f/A) * (1.0f/0.707f - 1.0f) + 2.0f);

    float b0 =     A * ((A+1.0f) - (A-1.0f)*cos_w + 2.0f*sqrtA*alpha);
    float b1 = 2.0f*A * ((A-1.0f) - (A+1.0f)*cos_w);
    float b2 =     A * ((A+1.0f) - (A-1.0f)*cos_w - 2.0f*sqrtA*alpha);
    float a0 =          (A+1.0f) + (A-1.0f)*cos_w + 2.0f*sqrtA*alpha;
    float a1 = -2.0f *  ((A-1.0f) + (A+1.0f)*cos_w);
    float a2 =          (A+1.0f) + (A-1.0f)*cos_w - 2.0f*sqrtA*alpha;

    filt.b0 = b0/a0; filt.b1 = b1/a0; filt.b2 = b2/a0;
    filt.a1 = a1/a0; filt.a2 = a2/a0;
    filt.reset();
}

/* ── High shelf filter (RBJ) ─────────────────────────────────────────── */
void DspSonicEnhancer::compute_high_shelf(float gain_db, float freq, Biquad& filt)
{
    if (gain_db < 0.01f) {
        filt.b0 = 1.0f; filt.b1 = 0.0f; filt.b2 = 0.0f;
        filt.a1 = 0.0f; filt.a2 = 0.0f;
        return;
    }

    const float sr = static_cast<float>(sample_rate_);
    const float A  = powf(10.0f, gain_db / 40.0f);
    const float w0 = 2.0f * static_cast<float>(M_PI) * freq / sr;
    const float cos_w = cosf(w0);
    const float sin_w = sinf(w0);
    const float sqrtA = sqrtf(A);
    const float alpha = sin_w / 2.0f * sqrtf((A + 1.0f/A) * (1.0f/0.707f - 1.0f) + 2.0f);

    float b0 =     A * ((A+1.0f) + (A-1.0f)*cos_w + 2.0f*sqrtA*alpha);
    float b1 = -2.0f*A * ((A-1.0f) + (A+1.0f)*cos_w);
    float b2 =     A * ((A+1.0f) + (A-1.0f)*cos_w - 2.0f*sqrtA*alpha);
    float a0 =          (A+1.0f) - (A-1.0f)*cos_w + 2.0f*sqrtA*alpha;
    float a1 =  2.0f *  ((A-1.0f) - (A+1.0f)*cos_w);
    float a2 =          (A+1.0f) - (A-1.0f)*cos_w - 2.0f*sqrtA*alpha;

    filt.b0 = b0/a0; filt.b1 = b1/a0; filt.b2 = b2/a0;
    filt.a1 = a1/a0; filt.a2 = a2/a0;
    filt.reset();
}

/* ── Soft saturation (tanh waveshaper) ────────────────────────────────── */
float DspSonicEnhancer::soft_saturate(float x)
{
    /* Fast tanh approximation (Pade 3/3) */
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* ── Preset application ───────────────────────────────────────────────── */
bool DspSonicEnhancer::apply_preset(const std::string& name)
{
    for (int i = 0; i < kPresetCount; ++i) {
        if (name == kPresets[i].name) {
            configure(kPresets[i].cfg, sample_rate_);
            return true;
        }
    }
    return false;
}

} // namespace mc1dsp
