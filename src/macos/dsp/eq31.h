/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * dsp/eq31.h — 31-band ISO 1/3-octave graphic EQ (dual-channel)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "eq.h"  // reuses Biquad struct
#include <array>
#include <cstddef>
#include <string>

namespace mc1dsp {

/* ── 31-Band Graphic EQ ───────────────────────────────────────────────────
 * ISO 1/3-octave center frequencies from 20 Hz to 20 kHz.
 * Each band is a peaking (bell) biquad with Q = 4.318 (1/3-octave bandwidth).
 * Dual-channel: independent L/R gain + filter state per band.
 * Gain range: -15 to +15 dB per band.
 */
class DspEq31 {
public:
    static constexpr int NUM_BANDS = 31;
    static constexpr int MAX_CHANNELS = 2;
    static constexpr float Q_THIRD_OCTAVE = 4.318f;
    static constexpr float GAIN_MIN = -15.0f;
    static constexpr float GAIN_MAX =  15.0f;

    /* ISO 1/3-octave center frequencies (Hz) */
    static constexpr float CENTER_FREQS[NUM_BANDS] = {
        20.0f,    25.0f,    31.5f,   40.0f,    50.0f,
        63.0f,    80.0f,   100.0f,  125.0f,   160.0f,
       200.0f,   250.0f,   315.0f,  400.0f,   500.0f,
       630.0f,   800.0f,  1000.0f, 1250.0f,  1600.0f,
      2000.0f,  2500.0f,  3150.0f, 4000.0f,  5000.0f,
      6300.0f,  8000.0f, 10000.0f, 12500.0f, 16000.0f,
     20000.0f
    };

    struct BandConfig {
        float gain_db = 0.0f;
        bool  enabled = true;
    };

    DspEq31();

    void set_sample_rate(int sr);
    void set_channels(int ch);

    /* Per-channel band control */
    void set_band(int channel, int band, float gain_db);
    void set_band_both(int band, float gain_db);
    float get_band(int channel, int band) const;

    /* Link mode: when linked, setting L also sets R */
    void set_linked(bool linked) { linked_ = linked; }
    bool is_linked() const { return linked_; }

    void set_enabled(bool on) { enabled_ = on; }
    bool is_enabled() const { return enabled_; }

    /* Process interleaved PCM in-place */
    void process(float* pcm, size_t frames, int channels);

    void reset();

    /* Preset management */
    struct Preset {
        const char* name;
        const char* description;
        float gains[NUM_BANDS];
    };
    static const Preset* builtin_presets();
    static int            builtin_preset_count();
    bool apply_preset(const std::string& name);
    bool apply_preset_channel(const std::string& name, int channel);

private:
    int  sample_rate_ = 44100;
    int  channels_    = 2;
    bool enabled_     = false;
    bool linked_      = true;

    /* Per-channel band configs and filters */
    std::array<std::array<BandConfig, NUM_BANDS>, MAX_CHANNELS> bands_{};
    std::array<std::array<Biquad, NUM_BANDS>, MAX_CHANNELS>     filters_{};

    void recompute(int channel, int band);
};

} // namespace mc1dsp
