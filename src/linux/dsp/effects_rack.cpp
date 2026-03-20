/*
 * Mcaster1DSPEncoder — Modular Effects Rack Implementation
 * dsp/effects_rack.cpp
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "effects_rack.h"
#include <cmath>
#include <algorithm>

namespace mc1dsp {

/* ══════════════════════════════════════════════════════════════════════════════
 * EqUnit — JSON parameter interface
 * ══════════════════════════════════════════════════════════════════════════════ */

json EqUnit::get_params() const {
    json j;
    j["enabled"] = eq_.is_enabled();
    j["preset"]  = "";
    json bands = json::array();
    for (int i = 0; i < 10; ++i) {
        auto b = eq_.get_band(i);
        bands.push_back({{"freq_hz", b.freq_hz}, {"gain_db", b.gain_db}, {"q", b.q}});
    }
    j["bands"] = bands;
    return j;
}

void EqUnit::set_params(const json& j) {
    if (j.contains("enabled")) eq_.set_enabled(j["enabled"].get<bool>());
    if (j.contains("preset")) {
        std::string p = j["preset"].get<std::string>();
        if (!p.empty()) eq_apply_preset(eq_, p);
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 * AgcUnit — JSON parameter interface
 * ══════════════════════════════════════════════════════════════════════════════ */

json AgcUnit::get_params() const {
    const auto& c = agc_.config();
    return {
        {"enabled",        c.enabled},
        {"threshold_db",   c.threshold_db},
        {"ratio",          c.ratio},
        {"attack_ms",      c.attack_ms},
        {"release_ms",     c.release_ms},
        {"makeup_gain_db", c.makeup_gain_db},
        {"limiter_db",     c.limiter_db},
        {"gain_reduction_db", agc_.gain_reduction_db()}
    };
}

void AgcUnit::set_params(const json& j) {
    AgcConfig c = agc_.config();
    if (j.contains("enabled"))        c.enabled        = j["enabled"].get<bool>();
    if (j.contains("threshold_db"))   c.threshold_db   = j["threshold_db"].get<float>();
    if (j.contains("ratio"))          c.ratio          = j["ratio"].get<float>();
    if (j.contains("attack_ms"))      c.attack_ms      = j["attack_ms"].get<float>();
    if (j.contains("release_ms"))     c.release_ms     = j["release_ms"].get<float>();
    if (j.contains("makeup_gain_db")) c.makeup_gain_db = j["makeup_gain_db"].get<float>();
    if (j.contains("limiter_db"))     c.limiter_db     = j["limiter_db"].get<float>();
    agc_.set_config(c);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * LimiterUnit — peak limiter with attack/release ballistics
 * ══════════════════════════════════════════════════════════════════════════════ */

void LimiterUnit::set_sample_rate(int sr) {
    sample_rate_ = sr;
    update_coeffs();
}

void LimiterUnit::update_coeffs() {
    float sr = static_cast<float>(sample_rate_);
    attack_c_  = (cfg_.attack_ms > 0 && sr > 0)  ? 1.0f - std::exp(-2.2f / (cfg_.attack_ms * sr / 1000.0f))  : 1.0f;
    release_c_ = (cfg_.release_ms > 0 && sr > 0) ? 1.0f - std::exp(-2.2f / (cfg_.release_ms * sr / 1000.0f)) : 1.0f;
    ceil_lin_  = std::pow(10.0f, cfg_.ceiling_db / 20.0f);
}

void LimiterUnit::process(float* pcm, size_t frames, int channels) {
    if (!cfg_.enabled) return;
    for (size_t f = 0; f < frames; ++f) {
        /* We detect peak across channels */
        float peak = 0.0f;
        for (int c = 0; c < channels; ++c) {
            float s = std::fabs(pcm[f * channels + c]);
            if (s > peak) peak = s;
        }
        /* We compute desired gain */
        float desired = (peak > ceil_lin_) ? (ceil_lin_ / peak) : 1.0f;
        /* We apply ballistics */
        float coeff = (desired < gain_) ? attack_c_ : release_c_;
        gain_ += coeff * (desired - gain_);
        /* We apply gain and hard-clip */
        for (int c = 0; c < channels; ++c) {
            float& s = pcm[f * channels + c];
            s *= gain_;
            if (s > ceil_lin_) s = ceil_lin_;
            else if (s < -ceil_lin_) s = -ceil_lin_;
        }
    }
    gr_db_ = (gain_ < 1.0f && gain_ > 1e-6f) ? -20.0f * std::log10(gain_) : 0.0f;
}

json LimiterUnit::get_params() const {
    return {
        {"enabled", cfg_.enabled}, {"ceiling_db", cfg_.ceiling_db},
        {"attack_ms", cfg_.attack_ms}, {"release_ms", cfg_.release_ms},
        {"gain_reduction_db", gr_db_}
    };
}

void LimiterUnit::set_params(const json& j) {
    if (j.contains("enabled"))    cfg_.enabled    = j["enabled"].get<bool>();
    if (j.contains("ceiling_db")) cfg_.ceiling_db = j["ceiling_db"].get<float>();
    if (j.contains("attack_ms"))  cfg_.attack_ms  = j["attack_ms"].get<float>();
    if (j.contains("release_ms")) cfg_.release_ms = j["release_ms"].get<float>();
    update_coeffs();
}

/* ══════════════════════════════════════════════════════════════════════════════
 * NoiseGateUnit — downward expander
 * ══════════════════════════════════════════════════════════════════════════════ */

void NoiseGateUnit::set_sample_rate(int sr) {
    sample_rate_ = sr;
    update_coeffs();
}

void NoiseGateUnit::update_coeffs() {
    float sr = static_cast<float>(sample_rate_);
    attack_c_     = (cfg_.attack_ms > 0 && sr > 0) ? 1.0f - std::exp(-2.2f / (cfg_.attack_ms * sr / 1000.0f)) : 1.0f;
    release_c_    = (cfg_.release_ms > 0 && sr > 0) ? 1.0f - std::exp(-2.2f / (cfg_.release_ms * sr / 1000.0f)) : 1.0f;
    hold_samples_ = static_cast<int>(cfg_.hold_ms * sr / 1000.0f);
    range_lin_    = std::pow(10.0f, cfg_.range_db / 20.0f);
}

void NoiseGateUnit::process(float* pcm, size_t frames, int channels) {
    if (!cfg_.enabled) return;
    float thresh_lin = std::pow(10.0f, cfg_.threshold_db / 20.0f);

    for (size_t f = 0; f < frames; ++f) {
        /* We detect peak across channels */
        float peak = 0.0f;
        for (int c = 0; c < channels; ++c) {
            float s = std::fabs(pcm[f * channels + c]);
            if (s > peak) peak = s;
        }

        float target;
        if (peak >= thresh_lin) {
            /* We open the gate */
            target = 1.0f;
            hold_count_ = hold_samples_;
        } else if (hold_count_ > 0) {
            /* We hold the gate open */
            target = 1.0f;
            --hold_count_;
        } else {
            /* We close the gate (apply range attenuation) */
            target = range_lin_;
        }

        /* We apply ballistics */
        float coeff = (target > gate_gain_) ? attack_c_ : release_c_;
        gate_gain_ += coeff * (target - gate_gain_);

        /* We apply gate gain */
        for (int c = 0; c < channels; ++c) {
            pcm[f * channels + c] *= gate_gain_;
        }
    }
}

json NoiseGateUnit::get_params() const {
    return {
        {"enabled", cfg_.enabled}, {"threshold_db", cfg_.threshold_db},
        {"range_db", cfg_.range_db}, {"attack_ms", cfg_.attack_ms},
        {"hold_ms", cfg_.hold_ms}, {"release_ms", cfg_.release_ms},
        {"is_open", is_open()}
    };
}

void NoiseGateUnit::set_params(const json& j) {
    if (j.contains("enabled"))      cfg_.enabled      = j["enabled"].get<bool>();
    if (j.contains("threshold_db")) cfg_.threshold_db  = j["threshold_db"].get<float>();
    if (j.contains("range_db"))     cfg_.range_db      = j["range_db"].get<float>();
    if (j.contains("attack_ms"))    cfg_.attack_ms     = j["attack_ms"].get<float>();
    if (j.contains("hold_ms"))      cfg_.hold_ms       = j["hold_ms"].get<float>();
    if (j.contains("release_ms"))   cfg_.release_ms    = j["release_ms"].get<float>();
    update_coeffs();
}

/* ══════════════════════════════════════════════════════════════════════════════
 * EffectsRack — ordered chain management
 * ══════════════════════════════════════════════════════════════════════════════ */

int EffectsRack::add_unit(std::unique_ptr<DspUnit> unit) {
    std::lock_guard<std::mutex> lk(mtx_);
    int id = next_id_++;
    unit->set_sample_rate(sample_rate_);
    chain_.push_back({id, std::move(unit)});
    return id;
}

bool EffectsRack::remove_unit(int unit_id) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto it = chain_.begin(); it != chain_.end(); ++it) {
        if (it->id == unit_id) {
            chain_.erase(it);
            return true;
        }
    }
    return false;
}

bool EffectsRack::reorder(const std::vector<int>& new_order) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (new_order.size() != chain_.size()) return false;
    std::vector<Slot> reordered;
    reordered.reserve(chain_.size());
    for (int id : new_order) {
        bool found = false;
        for (auto& s : chain_) {
            if (s.id == id) {
                reordered.push_back({s.id, std::move(s.unit)});
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    chain_ = std::move(reordered);
    return true;
}

void EffectsRack::process(float* pcm, size_t frames, int channels) {
    if (bypass_) return;
    /* We iterate the chain without holding the mutex — structural changes are rare
     * and we accept the risk of a stale read over the cost of locking on every audio buffer */
    for (auto& s : chain_) {
        if (s.unit && s.unit->is_enabled()) {
            s.unit->process(pcm, frames, channels);
        }
    }
}

json EffectsRack::to_json() const {
    std::lock_guard<std::mutex> lk(mtx_);
    json j;
    j["bypass"] = bypass_;
    json units = json::array();
    for (const auto& s : chain_) {
        json u;
        u["id"]         = s.id;
        u["type"]       = s.unit->type_name();
        u["brand_name"] = s.unit->brand_name();
        u["version"]    = s.unit->version();
        u["enabled"]    = s.unit->is_enabled();
        u["params"]     = s.unit->get_params();
        /* We include full version metadata for traceability */
        auto* vi = s.unit->version_info();
        if (vi) {
            u["version_info"] = {
                {"version", vi->version},
                {"release_date", vi->release_date},
                {"is_stub", vi->is_stub}
            };
        }
        units.push_back(u);
    }
    j["units"] = units;
    return j;
}

void EffectsRack::from_json(const json& j) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (j.contains("bypass")) bypass_ = j["bypass"].get<bool>();
    if (j.contains("units") && j["units"].is_array()) {
        chain_.clear();
        next_id_ = 1;
        for (const auto& u : j["units"]) {
            std::string type = u.value("type", "");
            auto unit = create_unit(type);
            if (!unit) continue;
            unit->set_sample_rate(sample_rate_);
            if (u.contains("enabled")) unit->set_enabled(u["enabled"].get<bool>());
            if (u.contains("params"))  unit->set_params(u["params"]);
            int id = next_id_++;
            chain_.push_back({id, std::move(unit)});
        }
    }
}

void EffectsRack::set_sample_rate(int sr) {
    std::lock_guard<std::mutex> lk(mtx_);
    sample_rate_ = sr;
    for (auto& s : chain_) {
        s.unit->set_sample_rate(sr);
    }
}

int EffectsRack::unit_count() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return static_cast<int>(chain_.size());
}

json EffectsRack::get_unit_info(int unit_id) const {
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto& s : chain_) {
        if (s.id == unit_id) {
            json j = {
                {"id", s.id}, {"type", s.unit->type_name()},
                {"brand_name", s.unit->brand_name()}, {"version", s.unit->version()},
                {"enabled", s.unit->is_enabled()}, {"params", s.unit->get_params()}
            };
            auto* vi = s.unit->version_info();
            if (vi) {
                j["version_info"] = {
                    {"version", vi->version}, {"release_date", vi->release_date},
                    {"changelog", vi->changelog}, {"is_stub", vi->is_stub}
                };
            }
            return j;
        }
    }
    return {{"error", "Unit not found"}};
}

bool EffectsRack::set_unit_params(int unit_id, const json& params) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& s : chain_) {
        if (s.id == unit_id) {
            s.unit->set_params(params);
            return true;
        }
    }
    return false;
}

bool EffectsRack::set_unit_enabled(int unit_id, bool on) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& s : chain_) {
        if (s.id == unit_id) {
            s.unit->set_enabled(on);
            return true;
        }
    }
    return false;
}

std::unique_ptr<DspUnit> EffectsRack::create_unit(const std::string& type) {
    if (type == "eq")         return std::make_unique<EqUnit>();
    if (type == "compressor") return std::make_unique<AgcUnit>();
    if (type == "limiter")    return std::make_unique<LimiterUnit>();
    if (type == "noise_gate") return std::make_unique<NoiseGateUnit>();
    if (type == "reverb")     return std::make_unique<ReverbUnit>();
    if (type == "delay")      return std::make_unique<DelayUnit>();
    return nullptr;
}

json EffectsRack::available_types() {
    /* We build the types list from the version registry — single source of truth */
    static const char* rack_types[] = {"eq", "compressor", "limiter", "noise_gate", "reverb", "delay"};
    json arr = json::array();
    for (const char* tid : rack_types) {
        auto* vi = effect_version_by_type(tid);
        if (!vi) continue;
        json entry;
        entry["type"]         = vi->type_id;
        entry["name"]         = vi->brand_name;
        entry["short_name"]   = vi->short_name;
        entry["version"]      = vi->version;
        entry["release_date"] = vi->release_date;
        entry["description"]  = vi->description;
        entry["changelog"]    = vi->changelog;
        if (vi->is_stub) entry["stub"] = true;
        arr.push_back(entry);
    }
    return arr;
}

json EffectsRack::all_effect_versions() {
    /* We return the full version registry — all effects including non-rack units */
    json arr = json::array();
    for (int i = 0; i < EFFECT_VERSION_COUNT; ++i) {
        const auto& vi = EFFECT_VERSIONS[i];
        arr.push_back({
            {"type",         vi.type_id},
            {"brand_name",   vi.brand_name},
            {"short_name",   vi.short_name},
            {"version",      vi.version},
            {"ver_major",    vi.ver_major},
            {"ver_minor",    vi.ver_minor},
            {"ver_patch",    vi.ver_patch},
            {"release_date", vi.release_date},
            {"description",  vi.description},
            {"changelog",    vi.changelog},
            {"is_stub",      vi.is_stub}
        });
    }
    return arr;
}

} // namespace mc1dsp
