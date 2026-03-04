/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * dsp/sonic_enhancer.h — BBE Sonic Maximizer clone (3-band phase-corrective processor)
 *
 * Architecture:
 *   Input → 3-band Linkwitz-Riley crossover (LR4 24dB/oct)
 *         → Phase alignment (delay lines: low ~2.5ms, mid ~0.5ms)
 *         → Lo Contour (bass shelf boost)
 *         → Process (presence boost + harmonic exciter)
 *         → Recombine → Output gain
 *
 * Dual-channel: fully independent L/R processing.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "eq.h"  // Biquad struct
#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace mc1dsp {

struct SonicEnhancerConfig {
    float lo_contour   = 5.0f;   // 0–10 (bass definition / warmth)
    float process      = 5.0f;   // 0–10 (high-frequency presence / definition)
    float output_gain  = 0.0f;   // -12 to +6 dB (makeup gain)
    bool  enabled      = false;
};

struct SonicEnhancerPreset {
    const char*         name;
    const char*         description;
    SonicEnhancerConfig cfg;
};

class DspSonicEnhancer {
public:
    static constexpr int MAX_CHANNELS = 2;
    static constexpr float LO_CONTOUR_MIN = 0.0f;
    static constexpr float LO_CONTOUR_MAX = 10.0f;
    static constexpr float PROCESS_MIN    = 0.0f;
    static constexpr float PROCESS_MAX    = 10.0f;
    static constexpr float OUTPUT_MIN     = -12.0f;
    static constexpr float OUTPUT_MAX     = 6.0f;

    DspSonicEnhancer();

    void configure(const SonicEnhancerConfig& cfg, int sample_rate);
    void process(float* pcm, size_t frames, int channels);
    void reset();

    void set_enabled(bool on) { cfg_.enabled = on; }
    bool is_enabled() const { return cfg_.enabled; }

    const SonicEnhancerConfig& config() const { return cfg_; }

    /* Presets */
    static const SonicEnhancerPreset* builtin_presets();
    static int builtin_preset_count();
    bool apply_preset(const std::string& name);

private:
    SonicEnhancerConfig cfg_;
    int sample_rate_ = 44100;

    /* ── Linkwitz-Riley 4th-order crossover (2 cascaded Butterworth 2nd-order) ── */
    /* Crossover 1: ~150 Hz (LOW vs MID+HIGH) */
    /* Crossover 2: ~1.2 kHz (MID vs HIGH) */
    struct CrossoverState {
        /* LP = low-pass, HP = high-pass, each is 2 cascaded biquads = LR4 */
        Biquad lp1_a, lp1_b;  // Crossover 1: LP (extracts LOW)
        Biquad hp1_a, hp1_b;  // Crossover 1: HP (passes MID+HIGH)
        Biquad lp2_a, lp2_b;  // Crossover 2: LP (extracts MID from MID+HIGH)
        Biquad hp2_a, hp2_b;  // Crossover 2: HP (extracts HIGH from MID+HIGH)
    };
    std::array<CrossoverState, MAX_CHANNELS> xover_{};

    /* ── Phase alignment delay lines ── */
    struct DelayLine {
        std::vector<float> buffer;
        size_t write_pos = 0;
        size_t delay_samples = 0;

        void init(size_t max_delay) {
            buffer.assign(max_delay + 1, 0.0f);
            write_pos = 0;
        }
        void set_delay(size_t samples) { delay_samples = samples; }
        float process(float in) {
            buffer[write_pos] = in;
            size_t read = (write_pos + buffer.size() - delay_samples) % buffer.size();
            float out = buffer[read];
            write_pos = (write_pos + 1) % buffer.size();
            return out;
        }
        void reset() {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            write_pos = 0;
        }
    };
    std::array<DelayLine, MAX_CHANNELS> delay_lo_{};   // ~2.5ms for LOW band
    std::array<DelayLine, MAX_CHANNELS> delay_mid_{};   // ~0.5ms for MID band

    /* ── Lo Contour: low shelf boost at 80 Hz ── */
    std::array<Biquad, MAX_CHANNELS> lo_shelf_{};

    /* ── Process: presence shelf at 3 kHz ── */
    std::array<Biquad, MAX_CHANNELS> hi_shelf_{};

    /* Cached linear gain */
    float output_linear_ = 1.0f;

    /* Internal helpers */
    void compute_crossover(float freq, Biquad& lp_a, Biquad& lp_b,
                           Biquad& hp_a, Biquad& hp_b);
    void compute_low_shelf(float gain_db, float freq, Biquad& filt);
    void compute_high_shelf(float gain_db, float freq, Biquad& filt);
    static float soft_saturate(float x);
};

} // namespace mc1dsp
