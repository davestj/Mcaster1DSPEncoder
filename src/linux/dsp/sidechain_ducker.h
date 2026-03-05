/*
 * Mcaster1DSPEncoder — Sidechain Ducker (PTT / Push-to-Talk)
 * dsp/sidechain_ducker.h
 *
 * We implement a sidechain compressor for push-to-talk ducking.
 * When PTT is active, we reduce the music level by a configurable amount.
 * The fade shape uses crossfader_curves.h algorithms for smooth transitions.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "effects_rack.h"
#include "crossfader_curves.h"
#include <atomic>
#include <cstddef>

namespace mc1dsp {

struct DuckerConfig {
    float duck_amount_db  = -15.0f;  // how much to reduce music when PTT active
    float attack_ms       = 50.0f;   // how fast to duck
    float release_ms      = 500.0f;  // how fast to restore
    float threshold_db    = -40.0f;  // mic signal level that triggers ducking (if sidechain fed)
    int   fade_curve      = 2;       // mc1xf::Curve for duck shape (default S-Curve)
    bool  enabled         = true;
};

class SidechainDucker : public DspUnit {
public:
    SidechainDucker() { update_coeffs(); }

    /* ── DspUnit interface ──────────────────────────────────────────────── */
    void process(float* pcm, size_t frames, int channels) override;
    void set_sample_rate(int sr) override;
    void set_enabled(bool on) override { cfg_.enabled = on; }
    bool is_enabled() const override { return cfg_.enabled; }
    const char* type_name() const override { return "ducker"; }
    json get_params() const override;
    void set_params(const json& j) override;
    void reset() override { duck_gain_ = 1.0f; }

    /* ── PTT control ────────────────────────────────────────────────────── */
    void set_ptt_active(bool on) { ptt_active_.store(on); }
    bool is_ptt_active() const   { return ptt_active_.load(); }

    /* We feed sidechain mic PCM for level detection (optional — PTT alone works without it) */
    void feed_sidechain(const float* mic_pcm, size_t frames, int channels);

    /* We return current duck amount in dB for UI metering */
    float current_duck_db() const { return duck_db_; }

    /* We configure the ducker */
    void set_config(const DuckerConfig& cfg) { cfg_ = cfg; update_coeffs(); }
    const DuckerConfig& config() const { return cfg_; }

private:
    DuckerConfig       cfg_;
    std::atomic<bool>  ptt_active_{false};
    int                sample_rate_ = 44100;
    float              duck_gain_   = 1.0f;   // current smoothed duck gain (1.0 = no duck)
    float              duck_db_     = 0.0f;   // current duck in dB for metering
    float              target_lin_  = 1.0f;   // target gain when ducking
    float              attack_c_    = 0.0f;
    float              release_c_   = 0.0f;
    float              sc_level_    = 0.0f;   // sidechain detected level

    void update_coeffs();
};

} // namespace mc1dsp
