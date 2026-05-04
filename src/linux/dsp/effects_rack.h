/*
 * Mcaster1DSPEncoder — Modular Effects Rack
 * dsp/effects_rack.h
 *
 * We define the DspUnit abstract base class for all modular DSP effects,
 * plus the EffectsRack container that chains them in user-defined order.
 * Each unit can be independently enabled/disabled and parameterized via JSON.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "eq.h"
#include "agc.h"
#include "effect_versions.h"
#include "plugin_loader.h"

#include <atomic>
#include <utility>
#include <vector>
#include <memory>
#include <mutex>
#include <string>
#include <cstddef>

/* We forward-declare nlohmann::json to avoid header pollution in DSP code */
#include "../external/include/nlohmann/json.hpp"
using json = nlohmann::json;

namespace mc1dsp {

/* ══════════════════════════════════════════════════════════════════════════════
 * MeterData — real-time metering snapshot returned by each DspUnit
 * All fields populated from std::atomic reads — no mutex needed.
 * ══════════════════════════════════════════════════════════════════════════════ */
struct MeterData {
    float input_db  = -96.0f;
    float output_db = -96.0f;
    float gain_reduction_db = 0.0f;   // compressor/limiter GR
    bool  gate_open = true;           // gate state
    std::vector<float> eq_response;   // per-band gain for EQ curve display
};

/* ══════════════════════════════════════════════════════════════════════════════
 * DspUnit — abstract base for all modular DSP effects
 * ══════════════════════════════════════════════════════════════════════════════ */
class DspUnit {
public:
    virtual ~DspUnit() = default;

    virtual void process(float* pcm, size_t frames, int channels) = 0;
    virtual void set_sample_rate(int sr) = 0;
    virtual void set_enabled(bool on) = 0;
    virtual bool is_enabled() const = 0;
    virtual const char* type_name() const = 0;
    virtual json get_params() const = 0;
    virtual void set_params(const json& j) = 0;
    virtual void reset() = 0;

    /* We return real-time meter data (all atomic reads, safe from any thread) */
    virtual MeterData get_meters() const { return {}; }

    /* We return version info from the central registry for this unit type */
    const EffectVersionInfo* version_info() const {
        return effect_version_by_type(type_name());
    }

    /* We return the branded display name, e.g. "Mcaster1 Compressor v1.1.0" */
    const char* brand_name() const {
        auto* vi = version_info();
        return vi ? vi->brand_name : type_name();
    }

    /* We return the semantic version string, e.g. "1.1.0" */
    const char* version() const {
        auto* vi = version_info();
        return vi ? vi->version : "0.0.0";
    }
};

/* ══════════════════════════════════════════════════════════════════════════════
 * EqUnit — wraps existing DspEq as a DspUnit
 * ══════════════════════════════════════════════════════════════════════════════ */
class EqUnit : public DspUnit {
public:
    void process(float* pcm, size_t frames, int channels) override;
    void set_sample_rate(int sr) override { eq_.set_sample_rate(sr); }
    void set_enabled(bool on) override { eq_.set_enabled(on); }
    bool is_enabled() const override { return eq_.is_enabled(); }
    const char* type_name() const override { return "eq"; }
    json get_params() const override;
    void set_params(const json& j) override;
    void reset() override { eq_.reset(); }
    MeterData get_meters() const override;
    DspEq& eq() { return eq_; }
private:
    DspEq eq_;
    std::atomic<float> meter_input_db_{-96.0f};
    std::atomic<float> meter_output_db_{-96.0f};
};

/* ══════════════════════════════════════════════════════════════════════════════
 * AgcUnit — wraps existing DspAgc as a DspUnit
 * ══════════════════════════════════════════════════════════════════════════════ */
class AgcUnit : public DspUnit {
public:
    void process(float* pcm, size_t frames, int channels) override;
    void set_sample_rate(int sr) override { agc_.set_sample_rate(sr); }
    void set_enabled(bool on) override { agc_.set_enabled(on); }
    bool is_enabled() const override { return agc_.is_enabled(); }
    const char* type_name() const override { return "compressor"; }
    json get_params() const override;
    void set_params(const json& j) override;
    void reset() override { agc_.reset(); }
    MeterData get_meters() const override;
    DspAgc& agc() { return agc_; }
private:
    DspAgc agc_;
    std::atomic<float> meter_input_db_{-96.0f};
    std::atomic<float> meter_output_db_{-96.0f};
    std::atomic<float> meter_gr_db_{0.0f};
};

/* ══════════════════════════════════════════════════════════════════════════════
 * LimiterUnit — peak limiter (ceiling + ballistics, no ratio/threshold)
 * ══════════════════════════════════════════════════════════════════════════════ */
class LimiterUnit : public DspUnit {
public:
    struct Config {
        float ceiling_db = -1.0f;
        float attack_ms  = 0.5f;
        float release_ms = 50.0f;
        bool  enabled    = true;
    };
    void process(float* pcm, size_t frames, int channels) override;
    void set_sample_rate(int sr) override;
    void set_enabled(bool on) override { cfg_.enabled = on; }
    bool is_enabled() const override { return cfg_.enabled; }
    const char* type_name() const override { return "limiter"; }
    json get_params() const override;
    void set_params(const json& j) override;
    void reset() override { gain_ = 1.0f; }
    MeterData get_meters() const override;
    float gain_reduction_db() const { return gr_db_; }
private:
    Config cfg_;
    int    sample_rate_ = 44100;
    float  gain_        = 1.0f;
    float  attack_c_    = 0.0f;
    float  release_c_   = 0.0f;
    float  ceil_lin_    = 0.891f;
    float  gr_db_       = 0.0f;
    std::atomic<float> meter_input_db_{-96.0f};
    std::atomic<float> meter_output_db_{-96.0f};
    std::atomic<float> meter_gr_db_{0.0f};
    void   update_coeffs();
};

/* ══════════════════════════════════════════════════════════════════════════════
 * NoiseGateUnit — downward expander
 * ══════════════════════════════════════════════════════════════════════════════ */
class NoiseGateUnit : public DspUnit {
public:
    struct Config {
        float threshold_db = -50.0f;
        float range_db     = -80.0f;  // attenuation when gate is closed
        float attack_ms    = 1.0f;
        float hold_ms      = 50.0f;
        float release_ms   = 100.0f;
        bool  enabled      = true;
    };
    void process(float* pcm, size_t frames, int channels) override;
    void set_sample_rate(int sr) override;
    void set_enabled(bool on) override { cfg_.enabled = on; }
    bool is_enabled() const override { return cfg_.enabled; }
    const char* type_name() const override { return "noise_gate"; }
    json get_params() const override;
    void set_params(const json& j) override;
    void reset() override { gate_gain_ = 0.0f; hold_count_ = 0; }
    MeterData get_meters() const override;
    bool is_open() const { return gate_gain_ > 0.5f; }
private:
    Config cfg_;
    int    sample_rate_ = 44100;
    float  gate_gain_   = 0.0f;
    int    hold_count_  = 0;
    float  attack_c_    = 0.0f;
    float  release_c_   = 0.0f;
    int    hold_samples_ = 0;
    float  range_lin_    = 0.0001f;
    std::atomic<float> meter_input_db_{-96.0f};
    std::atomic<float> meter_output_db_{-96.0f};
    std::atomic<bool>  meter_gate_open_{false};
    void   update_coeffs();
};

/* ══════════════════════════════════════════════════════════════════════════════
 * ReverbUnit — Schroeder reverb (4 comb filters + 2 all-pass filters)
 * ══════════════════════════════════════════════════════════════════════════════ */
class ReverbUnit : public DspUnit {
public:
    struct Config {
        float mix          = 0.3f;    // dry/wet blend 0.0-1.0
        float decay        = 1.5f;    // reverb tail length in seconds 0.1-5.0
        float damping      = 0.5f;    // high-frequency absorption 0.0-1.0
        float room_size    = 0.7f;    // scales comb filter delays 0.1-1.0
        float pre_delay_ms = 20.0f;   // initial reflection delay 0-100ms
        bool  enabled      = false;
    };

    void process(float* pcm, size_t frames, int channels) override;
    void set_sample_rate(int sr) override;
    void set_enabled(bool on) override { cfg_.enabled = on; }
    bool is_enabled() const override { return cfg_.enabled; }
    const char* type_name() const override { return "reverb"; }
    json get_params() const override;
    void set_params(const json& j) override;
    void reset() override;
    MeterData get_meters() const override;

private:
    Config cfg_;
    int    sample_rate_  = 48000;
    bool   initialized_  = false;

    /* Comb filter state */
    static constexpr int NUM_COMBS = 4;
    static constexpr int BASE_COMB_DELAYS[4] = {1557, 1617, 1491, 1422};
    struct CombFilter {
        std::vector<float> buffer;
        int  write_pos  = 0;
        float filter_state = 0.0f;  // one-pole LP state for damping
    };
    CombFilter combs_[4];
    float      comb_feedback_[4] = {};

    /* All-pass filter state */
    static constexpr int NUM_ALLPASS = 2;
    static constexpr int BASE_ALLPASS_DELAYS[2] = {225, 556};
    struct AllPassFilter {
        std::vector<float> buffer;
        int write_pos = 0;
    };
    AllPassFilter allpass_[NUM_ALLPASS];

    /* Pre-delay buffer */
    std::vector<float> pre_delay_buf_;
    int  pre_delay_write_ = 0;
    int  pre_delay_samples_ = 0;

    /* Metering */
    std::atomic<float> meter_input_db_{-96.0f};
    std::atomic<float> meter_output_db_{-96.0f};
    std::atomic<float> meter_wet_level_db_{-96.0f};

    void init_buffers();
    void update_feedback();
};

/* ══════════════════════════════════════════════════════════════════════════════
 * DelayUnit — feedback delay with lowpass filter and stereo spread
 * ══════════════════════════════════════════════════════════════════════════════ */
class DelayUnit : public DspUnit {
public:
    struct Config {
        float delay_ms      = 250.0f;  // delay time 10-2000ms
        float feedback      = 0.4f;    // feedback amount 0.0-0.95
        float mix           = 0.3f;    // dry/wet blend 0.0-1.0
        float filter_hz     = 3000.0f; // lowpass in feedback path 200-8000
        float stereo_spread = 0.0f;    // ping-pong offset L/R 0.0-1.0
        bool  enabled       = false;
    };

    void process(float* pcm, size_t frames, int channels) override;
    void set_sample_rate(int sr) override;
    void set_enabled(bool on) override { cfg_.enabled = on; }
    bool is_enabled() const override { return cfg_.enabled; }
    const char* type_name() const override { return "delay"; }
    json get_params() const override;
    void set_params(const json& j) override;
    void reset() override;
    MeterData get_meters() const override;

private:
    Config cfg_;
    int    sample_rate_  = 48000;
    bool   initialized_  = false;

    /* Circular delay buffer — sized for max delay (2000ms at 48kHz = 96000 samples per channel) */
    static constexpr int MAX_DELAY_SAMPLES = 96000;
    std::vector<float> delay_buf_l_;  // left channel delay line
    std::vector<float> delay_buf_r_;  // right channel delay line (or mono mirror)
    int write_pos_ = 0;

    /* One-pole lowpass filter state per channel for feedback path */
    float lp_state_l_ = 0.0f;
    float lp_state_r_ = 0.0f;
    float lp_coeff_   = 0.0f;       // computed from filter_hz

    /* Metering */
    std::atomic<float> meter_input_db_{-96.0f};
    std::atomic<float> meter_output_db_{-96.0f};
    std::atomic<float> meter_delay_level_db_{-96.0f};

    void init_buffers();
    void update_filter_coeff();
};

/* ══════════════════════════════════════════════════════════════════════════════
 * LoudnessUnit — EBU R128 / ATSC A/85 loudness compliance (ITU-R BS.1770-4)
 * ══════════════════════════════════════════════════════════════════════════════ */
class LoudnessUnit : public DspUnit {
public:
    struct Config {
        float target_lufs = -16.0f;    // Target integrated LUFS
        float target_tp   = -1.0f;     // True peak ceiling (dBTP)
        float lra_max     = 11.0f;     // Max loudness range (LU)
        std::string standard = "podcast"; // "ebu_r128","atsc_a85","podcast","spotify","youtube","custom"
        bool  enabled     = false;
    };

    void process(float* pcm, size_t frames, int channels) override;
    void set_sample_rate(int sr) override;
    void set_enabled(bool on) override { cfg_.enabled = on; }
    bool is_enabled() const override { return cfg_.enabled; }
    const char* type_name() const override { return "loudness"; }
    json get_params() const override;
    void set_params(const json& j) override;
    void reset() override;
    MeterData get_meters() const override;

    /* Extended loudness meter data for the dedicated API endpoint */
    float integrated_lufs() const { return meter_integrated_lufs_.load(std::memory_order_relaxed); }
    float momentary_lufs()  const { return meter_momentary_lufs_.load(std::memory_order_relaxed); }
    float short_term_lufs() const { return meter_short_term_lufs_.load(std::memory_order_relaxed); }
    float true_peak_dbtp()  const { return meter_true_peak_.load(std::memory_order_relaxed); }
    float loudness_range()  const { return meter_lra_.load(std::memory_order_relaxed); }
    float gain_correction() const { return meter_gain_db_.load(std::memory_order_relaxed); }
    bool  is_compliant()    const;
    const Config& config()  const { return cfg_; }

private:
    Config cfg_;
    int    sample_rate_ = 48000;

    /* K-weighting biquad filter state (BS.1770-4)
     * Stage 1: pre-filter (high shelf +3.99 dB)
     * Stage 2: RLB weighting (high-pass ~38 Hz)
     * Per-channel state (up to 2 channels) */
    struct BiquadState {
        float x1 = 0, x2 = 0;   // input delay
        float y1 = 0, y2 = 0;   // output delay
    };
    struct BiquadCoeffs {
        float b0 = 1, b1 = 0, b2 = 0;
        float a1 = 0, a2 = 0;
    };
    BiquadCoeffs kw_stage1_;     // pre-filter
    BiquadCoeffs kw_stage2_;     // RLB
    BiquadState  kw_s1_[2];      // per-channel stage 1
    BiquadState  kw_s2_[2];      // per-channel stage 2

    /* Momentary loudness: 400ms overlapping blocks, 75% overlap (100ms hop) */
    static constexpr int MOMENTARY_BLOCKS = 4;  // 4 x 100ms = 400ms
    float  moment_block_sum_[4] = {};            // per-block channel-summed mean-sq
    int    moment_block_frames_[4] = {};
    int    moment_block_idx_   = 0;
    int    moment_hop_count_   = 0;
    int    moment_hop_frames_  = 0;              // 100ms in samples

    /* Short-term loudness: 3s window (30 x 100ms blocks) */
    static constexpr int SHORT_TERM_BLOCKS = 30;
    float  short_block_sum_[30] = {};
    int    short_block_frames_[30] = {};
    int    short_block_idx_    = 0;

    /* Integrated loudness: gated, overlapping 400ms blocks
     * BS.1770-4 two-pass gating:
     *   1. Absolute gate: -70 LUFS
     *   2. Relative gate: -10 LU below ungated average */
    static constexpr int MAX_GATE_BLOCKS = 7200;  // ~12 min at 100ms hop
    float  gate_blocks_[7200]  = {};
    int    gate_count_         = 0;
    float  integrated_lufs_    = -70.0f;

    /* Loudness range (LRA): BS.1770-4 short-term distribution percentiles */
    static constexpr int MAX_LRA_BLOCKS = 600;  // 10 min of 1s blocks
    float  lra_blocks_[600]    = {};
    int    lra_count_          = 0;
    float  loudness_range_     = 0.0f;

    /* LRA hop counter (counts 100ms hops, stores every 10th = 1s) */
    int    lra_hop_counter_    = 0;

    /* Auto-gain correction */
    float  gain_db_            = 0.0f;    // current target gain
    float  gain_smooth_db_     = 0.0f;    // smoothed gain (attack/release)
    float  gain_attack_c_      = 0.0f;    // per-sample smoothing coeff
    float  gain_release_c_     = 0.0f;

    /* True peak: 4x oversampled interpolation (FIR half-band filter) */
    float  tp_prev_[2]         = {};      // previous sample per channel
    float  true_peak_lin_      = 0.0f;

    /* Atomic meters for lock-free HTTP reads */
    std::atomic<float> meter_input_db_{-96.0f};
    std::atomic<float> meter_output_db_{-96.0f};
    std::atomic<float> meter_integrated_lufs_{-70.0f};
    std::atomic<float> meter_momentary_lufs_{-70.0f};
    std::atomic<float> meter_short_term_lufs_{-70.0f};
    std::atomic<float> meter_true_peak_{-70.0f};
    std::atomic<float> meter_gain_db_{0.0f};
    std::atomic<float> meter_lra_{0.0f};

    void update_coeffs();
    void apply_standard_preset();
    float biquad_process(BiquadState& st, const BiquadCoeffs& c, float x);
    float compute_integrated();
    float compute_lra();
    float detect_true_peak(float s0, float s1);
};

/* ══════════════════════════════════════════════════════════════════════════════
 * PluginUnit — wraps a third-party C plugin as a DspUnit
 *
 * We hold a pointer to the LoadedPlugin (owned by PluginLoader singleton)
 * and manage one plugin instance via the C API. The plugin's process()
 * function is called directly on the audio thread.
 * ══════════════════════════════════════════════════════════════════════════════ */
class PluginUnit : public DspUnit {
public:
    PluginUnit(const LoadedPlugin* plugin, int sample_rate, int channels);
    ~PluginUnit() override;

    void process(float* pcm, size_t frames, int channels) override;
    void set_sample_rate(int sr) override;
    void set_enabled(bool on) override { enabled_ = on; }
    bool is_enabled() const override { return enabled_; }
    const char* type_name() const override;
    json get_params() const override;
    void set_params(const json& j) override;
    void reset() override;
    MeterData get_meters() const override;

private:
    const LoadedPlugin*   plugin_;         /* owned by PluginLoader — do not free */
    mc1_plugin_handle_t   instance_;       /* plugin-managed instance */
    std::string           type_id_;        /* cached for type_name() return */
    bool                  enabled_ = true;
    int                   sample_rate_;
    int                   channels_;
    std::atomic<float>    meter_input_db_{-96.0f};
    std::atomic<float>    meter_output_db_{-96.0f};
};

/* ══════════════════════════════════════════════════════════════════════════════
 * RoutingEntry — signal routing metadata (v1: serial only — cable order = chain order)
 * ══════════════════════════════════════════════════════════════════════════════ */
struct RoutingEntry {
    std::string from_unit;  // unit type_id or "input"
    std::string to_unit;    // unit type_id or "output"
    int from_port = 0;      // 0 = default output
    int to_port   = 0;      // 0 = default input
};

/* ══════════════════════════════════════════════════════════════════════════════
 * EffectsRack — ordered chain of DspUnit instances
 * ══════════════════════════════════════════════════════════════════════════════ */
class EffectsRack {
public:
    EffectsRack() = default;

    /* We add a unit to the end of the chain, returning its ID */
    int add_unit(std::unique_ptr<DspUnit> unit);

    /* We remove a unit by ID */
    bool remove_unit(int unit_id);

    /* We reorder the chain by specifying the new order of IDs */
    bool reorder(const std::vector<int>& new_order);

    /* We process audio through all enabled units in chain order */
    void process(float* pcm, size_t frames, int channels);

    /* We serialize/deserialize the rack state for API + DB persistence */
    json to_json() const;
    void from_json(const json& j);

    void set_sample_rate(int sr);
    void set_bypass(bool bypass) { bypass_ = bypass; }
    bool is_bypass() const { return bypass_; }

    int unit_count() const;

    /* We return info about a specific unit */
    json get_unit_info(int unit_id) const;

    /* We update params for a specific unit */
    bool set_unit_params(int unit_id, const json& params);

    /* We toggle enable for a specific unit */
    bool set_unit_enabled(int unit_id, bool on);

    /* We create a DspUnit by type name */
    static std::unique_ptr<DspUnit> create_unit(const std::string& type);

    /* We return available unit types with version info from registry */
    static json available_types();

    /* We return the full version registry for all effects (rack + non-rack) */
    static json all_effect_versions();

    /* We return real-time meter data for all units (atomic reads, no mutex) */
    std::vector<std::pair<int, MeterData>> get_all_meters() const;

    /* Signal routing metadata (v1: serial only — cable order = chain order) */
    std::vector<RoutingEntry> get_routing() const;
    void set_routing(const std::vector<RoutingEntry>& routing);

private:
    struct Slot {
        int id;
        std::unique_ptr<DspUnit> unit;
    };
    std::vector<Slot> chain_;
    mutable std::mutex mtx_;
    int  next_id_     = 1;
    bool bypass_      = false;
    int  sample_rate_ = 44100;
};

} // namespace mc1dsp
