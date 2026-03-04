/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * dsp/eq31.cpp — 31-band ISO 1/3-octave graphic EQ implementation
 *
 * All bands use RBJ peaking (bell) filters with Q = 4.318 (1/3-octave).
 * Dual-channel with independent L/R gain and filter state.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "eq31.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace mc1dsp {

/* ── Presets ───────────────────────────────────────────────────────────────
 * 8 built-in presets for common broadcast/music scenarios.
 * Gains are in dB for each of the 31 ISO 1/3-octave bands.
 * Band order: 20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160,
 *             200, 250, 315, 400, 500, 630, 800, 1k, 1.25k, 1.6k,
 *             2k, 2.5k, 3.15k, 4k, 5k, 6.3k, 8k, 10k, 12.5k, 16k, 20k
 */
static const DspEq31::Preset kPresets[] = {
    { "flat", "All bands at 0 dB",
      {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
    },
    { "broadcast_fm", "FM broadcast standard: warm bass, gentle presence, bright top",
      { 2, 2, 3, 3, 3, 3, 2, 2, 1, 1,
        0, 0,-1,-1, 0, 0, 1, 1, 2, 2,
        3, 3, 3, 3, 2, 2, 2, 2, 1, 1, 0 }
    },
    { "broadcast_am", "AM broadcast: reduced bass/treble, vocal-focused midrange",
      {-6,-5,-4,-3,-2,-1, 0, 1, 2, 2,
        3, 3, 4, 4, 4, 4, 4, 3, 3, 2,
        1, 0,-1,-2,-3,-4,-5,-6,-8,-10,-12 }
    },
    { "voice", "Optimized for spoken word: cut rumble, boost vocal clarity",
      {-10,-9,-8,-7,-5,-3,-1, 1, 2, 3,
        3, 4, 4, 4, 4, 4, 3, 3, 4, 4,
        3, 2, 1, 0,-1,-2,-3,-4,-5,-6,-8 }
    },
    { "music_warm", "Warm music: enhanced bass, smooth mids, gentle treble rolloff",
      { 4, 4, 5, 5, 4, 4, 3, 3, 2, 2,
        1, 1, 0, 0, 0, 0,-1,-1, 0, 0,
        0, 0, 0, 0,-1,-1,-2,-2,-3,-3,-4 }
    },
    { "music_bright", "Bright music: tight bass, forward presence, crisp highs",
      { 1, 1, 2, 2, 2, 2, 1, 1, 0, 0,
       -1,-1,-1, 0, 0, 0, 1, 1, 2, 3,
        3, 4, 4, 4, 4, 3, 3, 3, 2, 2, 1 }
    },
    { "loudness", "Loudness contour: Fletcher-Munson compensation for low volumes",
      { 8, 7, 6, 5, 4, 3, 2, 1, 0, 0,
       -1,-1,-1,-1, 0, 0, 0, 0, 1, 1,
        2, 2, 3, 3, 4, 4, 5, 5, 5, 4, 3 }
    },
    { "smiley_curve", "Classic V-shaped: boosted lows and highs, scooped mids",
      { 6, 5, 5, 4, 4, 3, 3, 2, 1, 0,
       -1,-2,-2,-3,-3,-3,-3,-2,-2,-1,
        0, 1, 2, 3, 3, 4, 4, 5, 5, 4, 3 }
    },
};

static constexpr int kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

const DspEq31::Preset* DspEq31::builtin_presets() { return kPresets; }
int DspEq31::builtin_preset_count() { return kPresetCount; }

/* ── Constructor ──────────────────────────────────────────────────────── */

DspEq31::DspEq31()
{
    /* Initialize all bands to flat (0 dB, enabled) */
    for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
        for (int b = 0; b < NUM_BANDS; ++b) {
            bands_[ch][b].gain_db = 0.0f;
            bands_[ch][b].enabled = true;
            recompute(ch, b);
        }
    }
}

void DspEq31::set_sample_rate(int sr)
{
    sample_rate_ = (sr > 0) ? sr : 44100;
    for (int ch = 0; ch < MAX_CHANNELS; ++ch)
        for (int b = 0; b < NUM_BANDS; ++b)
            recompute(ch, b);
    reset();
}

void DspEq31::set_channels(int ch)
{
    channels_ = std::clamp(ch, 1, 2);
}

void DspEq31::set_band(int channel, int band, float gain_db)
{
    if (channel < 0 || channel >= MAX_CHANNELS) return;
    if (band < 0 || band >= NUM_BANDS) return;

    gain_db = std::clamp(gain_db, GAIN_MIN, GAIN_MAX);
    bands_[channel][band].gain_db = gain_db;
    recompute(channel, band);
    filters_[channel][band].reset();

    /* If linked, mirror to other channel */
    if (linked_) {
        int other = 1 - channel;
        bands_[other][band].gain_db = gain_db;
        recompute(other, band);
        filters_[other][band].reset();
    }
}

void DspEq31::set_band_both(int band, float gain_db)
{
    if (band < 0 || band >= NUM_BANDS) return;
    gain_db = std::clamp(gain_db, GAIN_MIN, GAIN_MAX);

    for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
        bands_[ch][band].gain_db = gain_db;
        recompute(ch, band);
        filters_[ch][band].reset();
    }
}

float DspEq31::get_band(int channel, int band) const
{
    if (channel < 0 || channel >= MAX_CHANNELS) return 0.0f;
    if (band < 0 || band >= NUM_BANDS) return 0.0f;
    return bands_[channel][band].gain_db;
}

void DspEq31::reset()
{
    for (auto& ch : filters_)
        for (auto& f : ch)
            f.reset();
}

/* ── Processing ───────────────────────────────────────────────────────── */

void DspEq31::process(float* pcm, size_t frames, int channels)
{
    if (!enabled_ || channels < 1 || channels > 2) return;

    /* Process each band as a cascade: run all frames through band 0,
     * then all frames through band 1, etc. This keeps each biquad's
     * state coherent across the buffer. */
    for (int bi = 0; bi < NUM_BANDS; ++bi) {
        /* Check if any channel has non-zero gain for this band */
        bool any_active = false;
        for (int ch = 0; ch < channels; ++ch) {
            if (bands_[ch][bi].enabled && bands_[ch][bi].gain_db != 0.0f) {
                any_active = true;
                break;
            }
        }
        if (!any_active) continue;

        if (channels == 1) {
            /* Mono: only L channel filter */
            if (!bands_[0][bi].enabled || bands_[0][bi].gain_db == 0.0f) continue;
            auto& filt = filters_[0][bi];
            for (size_t i = 0; i < frames; ++i)
                pcm[i] = filt.process(pcm[i], 0);
        } else {
            /* Stereo: independent L/R filters */
            auto& filt_l = filters_[0][bi];
            auto& filt_r = filters_[1][bi];
            bool active_l = bands_[0][bi].enabled && bands_[0][bi].gain_db != 0.0f;
            bool active_r = bands_[1][bi].enabled && bands_[1][bi].gain_db != 0.0f;

            for (size_t i = 0; i < frames; ++i) {
                if (active_l)
                    pcm[i * 2 + 0] = filt_l.process(pcm[i * 2 + 0], 0);
                if (active_r)
                    pcm[i * 2 + 1] = filt_r.process(pcm[i * 2 + 1], 0);
            }
        }
    }
}

/* ── Biquad coefficient computation (RBJ peaking filter) ─────────────── */

void DspEq31::recompute(int channel, int band)
{
    auto& cfg  = bands_[channel][band];
    auto& filt = filters_[channel][band];

    if (!cfg.enabled || cfg.gain_db == 0.0f) {
        /* Identity filter */
        filt.b0 = 1.0f; filt.b1 = 0.0f; filt.b2 = 0.0f;
        filt.a1 = 0.0f; filt.a2 = 0.0f;
        return;
    }

    const float sr    = static_cast<float>(sample_rate_);
    const float f0    = CENTER_FREQS[band];
    const float omega = 2.0f * static_cast<float>(M_PI) * f0 / sr;
    const float sin_w = sinf(omega);
    const float cos_w = cosf(omega);
    const float A     = powf(10.0f, cfg.gain_db / 40.0f);
    const float alpha = sin_w / (2.0f * Q_THIRD_OCTAVE);

    /* RBJ peaking EQ */
    float b0 =  1.0f + alpha * A;
    float b1 = -2.0f * cos_w;
    float b2 =  1.0f - alpha * A;
    float a0 =  1.0f + alpha / A;
    float a1 = -2.0f * cos_w;
    float a2 =  1.0f - alpha / A;

    /* Normalize by a0 */
    filt.b0 = b0 / a0;
    filt.b1 = b1 / a0;
    filt.b2 = b2 / a0;
    filt.a1 = a1 / a0;
    filt.a2 = a2 / a0;
}

/* ── Preset application ───────────────────────────────────────────────── */

bool DspEq31::apply_preset(const std::string& name)
{
    for (int p = 0; p < kPresetCount; ++p) {
        if (name == kPresets[p].name) {
            for (int b = 0; b < NUM_BANDS; ++b)
                set_band_both(b, kPresets[p].gains[b]);
            return true;
        }
    }
    return false;
}

bool DspEq31::apply_preset_channel(const std::string& name, int channel)
{
    if (channel < 0 || channel >= MAX_CHANNELS) return false;
    for (int p = 0; p < kPresetCount; ++p) {
        if (name == kPresets[p].name) {
            bool was_linked = linked_;
            linked_ = false; /* Temporarily unlink */
            for (int b = 0; b < NUM_BANDS; ++b)
                set_band(channel, b, kPresets[p].gains[b]);
            linked_ = was_linked;
            return true;
        }
    }
    return false;
}

} // namespace mc1dsp
