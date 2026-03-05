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
};

/* ══════════════════════════════════════════════════════════════════════════════
 * EqUnit — wraps existing DspEq as a DspUnit
 * ══════════════════════════════════════════════════════════════════════════════ */
class EqUnit : public DspUnit {
public:
    void process(float* pcm, size_t frames, int channels) override { eq_.process(pcm, frames, channels); }
    void set_sample_rate(int sr) override { eq_.set_sample_rate(sr); }
    void set_enabled(bool on) override { eq_.set_enabled(on); }
    bool is_enabled() const override { return eq_.is_enabled(); }
    const char* type_name() const override { return "eq"; }
    json get_params() const override;
    void set_params(const json& j) override;
    void reset() override { eq_.reset(); }
    DspEq& eq() { return eq_; }
private:
    DspEq eq_;
};

/* ══════════════════════════════════════════════════════════════════════════════
 * AgcUnit — wraps existing DspAgc as a DspUnit
 * ══════════════════════════════════════════════════════════════════════════════ */
class AgcUnit : public DspUnit {
public:
    void process(float* pcm, size_t frames, int channels) override { agc_.process(pcm, frames, channels); }
    void set_sample_rate(int sr) override { agc_.set_sample_rate(sr); }
    void set_enabled(bool on) override { agc_.set_enabled(on); }
    bool is_enabled() const override { return agc_.is_enabled(); }
    const char* type_name() const override { return "compressor"; }
    json get_params() const override;
    void set_params(const json& j) override;
    void reset() override { agc_.reset(); }
    DspAgc& agc() { return agc_; }
private:
    DspAgc agc_;
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
    float gain_reduction_db() const { return gr_db_; }
private:
    Config cfg_;
    int    sample_rate_ = 44100;
    float  gain_        = 1.0f;
    float  attack_c_    = 0.0f;
    float  release_c_   = 0.0f;
    float  ceil_lin_    = 0.891f;
    float  gr_db_       = 0.0f;
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
    void   update_coeffs();
};

/* ══════════════════════════════════════════════════════════════════════════════
 * ReverbUnit — placeholder stub (pass-through)
 * ══════════════════════════════════════════════════════════════════════════════ */
class ReverbUnit : public DspUnit {
public:
    void process(float* pcm, size_t frames, int channels) override { (void)pcm; (void)frames; (void)channels; }
    void set_sample_rate(int sr) override { (void)sr; }
    void set_enabled(bool on) override { enabled_ = on; }
    bool is_enabled() const override { return enabled_; }
    const char* type_name() const override { return "reverb"; }
    json get_params() const override { return {{"enabled", enabled_}, {"mix", mix_}}; }
    void set_params(const json& j) override { if (j.contains("enabled")) enabled_ = j["enabled"].get<bool>(); if (j.contains("mix")) mix_ = j["mix"].get<float>(); }
    void reset() override {}
private:
    bool  enabled_ = false;
    float mix_     = 0.3f;
};

/* ══════════════════════════════════════════════════════════════════════════════
 * DelayUnit — placeholder stub (pass-through)
 * ══════════════════════════════════════════════════════════════════════════════ */
class DelayUnit : public DspUnit {
public:
    void process(float* pcm, size_t frames, int channels) override { (void)pcm; (void)frames; (void)channels; }
    void set_sample_rate(int sr) override { (void)sr; }
    void set_enabled(bool on) override { enabled_ = on; }
    bool is_enabled() const override { return enabled_; }
    const char* type_name() const override { return "delay"; }
    json get_params() const override { return {{"enabled", enabled_}, {"time_ms", time_ms_}, {"feedback", feedback_}}; }
    void set_params(const json& j) override { if (j.contains("enabled")) enabled_ = j["enabled"].get<bool>(); if (j.contains("time_ms")) time_ms_ = j["time_ms"].get<float>(); if (j.contains("feedback")) feedback_ = j["feedback"].get<float>(); }
    void reset() override {}
private:
    bool  enabled_  = false;
    float time_ms_  = 250.0f;
    float feedback_ = 0.3f;
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

    /* We return available unit types with default params */
    static json available_types();

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
