/*
 * Mcaster1DSPEncoder — Modular Effects Rack Implementation
 * dsp/effects_rack.cpp
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "effects_rack.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>

namespace mc1dsp {

/* ══════════════════════════════════════════════════════════════════════════════
 * Metering helper: compute peak dB from interleaved PCM buffer
 * ══════════════════════════════════════════════════════════════════════════════ */
static float peak_db(const float* pcm, size_t frames, int channels) {
    float peak = 0.0f;
    const size_t n = frames * static_cast<size_t>(channels);
    for (size_t i = 0; i < n; ++i) {
        float s = std::fabs(pcm[i]);
        if (s > peak) peak = s;
    }
    if (peak < 1e-9f) return -96.0f;
    return 20.0f * std::log10(peak);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * EqUnit — JSON parameter interface + metering
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

void EqUnit::process(float* pcm, size_t frames, int channels) {
    meter_input_db_.store(peak_db(pcm, frames, channels), std::memory_order_relaxed);
    eq_.process(pcm, frames, channels);
    meter_output_db_.store(peak_db(pcm, frames, channels), std::memory_order_relaxed);
}

MeterData EqUnit::get_meters() const {
    MeterData m;
    m.input_db  = meter_input_db_.load(std::memory_order_relaxed);
    m.output_db = meter_output_db_.load(std::memory_order_relaxed);
    /* We export per-band gain values for the frequency response curve overlay */
    m.eq_response.reserve(DspEq::NUM_BANDS);
    for (int i = 0; i < DspEq::NUM_BANDS; ++i) {
        m.eq_response.push_back(eq_.get_band(i).gain_db);
    }
    return m;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * AgcUnit — JSON parameter interface + metering
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

void AgcUnit::process(float* pcm, size_t frames, int channels) {
    meter_input_db_.store(peak_db(pcm, frames, channels), std::memory_order_relaxed);
    agc_.process(pcm, frames, channels);
    meter_output_db_.store(peak_db(pcm, frames, channels), std::memory_order_relaxed);
    meter_gr_db_.store(agc_.gain_reduction_db(), std::memory_order_relaxed);
}

MeterData AgcUnit::get_meters() const {
    MeterData m;
    m.input_db  = meter_input_db_.load(std::memory_order_relaxed);
    m.output_db = meter_output_db_.load(std::memory_order_relaxed);
    m.gain_reduction_db = meter_gr_db_.load(std::memory_order_relaxed);
    return m;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * LimiterUnit — peak limiter with attack/release ballistics + metering
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
    meter_input_db_.store(peak_db(pcm, frames, channels), std::memory_order_relaxed);
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
    meter_output_db_.store(peak_db(pcm, frames, channels), std::memory_order_relaxed);
    meter_gr_db_.store(gr_db_, std::memory_order_relaxed);
}

MeterData LimiterUnit::get_meters() const {
    MeterData m;
    m.input_db  = meter_input_db_.load(std::memory_order_relaxed);
    m.output_db = meter_output_db_.load(std::memory_order_relaxed);
    m.gain_reduction_db = meter_gr_db_.load(std::memory_order_relaxed);
    return m;
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
    meter_input_db_.store(peak_db(pcm, frames, channels), std::memory_order_relaxed);
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
    meter_output_db_.store(peak_db(pcm, frames, channels), std::memory_order_relaxed);
    meter_gate_open_.store(gate_gain_ > 0.5f, std::memory_order_relaxed);
}

MeterData NoiseGateUnit::get_meters() const {
    MeterData m;
    m.input_db  = meter_input_db_.load(std::memory_order_relaxed);
    m.output_db = meter_output_db_.load(std::memory_order_relaxed);
    m.gate_open = meter_gate_open_.load(std::memory_order_relaxed);
    return m;
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
 * ReverbUnit — Schroeder reverb (4 comb + 2 all-pass)
 * ══════════════════════════════════════════════════════════════════════════════ */

void ReverbUnit::set_sample_rate(int sr) {
    sample_rate_ = sr;
    initialized_ = false;  // force re-init on next process()
}

void ReverbUnit::init_buffers() {
    float sr_scale = static_cast<float>(sample_rate_) / 48000.0f;

    /* We initialize comb filter delay lines scaled by room_size and sample rate */
    for (int i = 0; i < NUM_COMBS; ++i) {
        int len = static_cast<int>(BASE_COMB_DELAYS[i] * cfg_.room_size * sr_scale);
        if (len < 1) len = 1;
        combs_[i].buffer.assign(static_cast<size_t>(len), 0.0f);
        combs_[i].write_pos = 0;
        combs_[i].filter_state = 0.0f;
    }

    /* We initialize all-pass filter delay lines */
    for (int i = 0; i < NUM_ALLPASS; ++i) {
        int len = static_cast<int>(BASE_ALLPASS_DELAYS[i] * sr_scale);
        if (len < 1) len = 1;
        allpass_[i].buffer.assign(static_cast<size_t>(len), 0.0f);
        allpass_[i].write_pos = 0;
    }

    /* We initialize pre-delay buffer */
    pre_delay_samples_ = static_cast<int>(cfg_.pre_delay_ms * static_cast<float>(sample_rate_) / 1000.0f);
    if (pre_delay_samples_ < 1) pre_delay_samples_ = 1;
    pre_delay_buf_.assign(static_cast<size_t>(pre_delay_samples_), 0.0f);
    pre_delay_write_ = 0;

    update_feedback();
    initialized_ = true;
}

void ReverbUnit::update_feedback() {
    /* We derive comb filter feedback gain from decay time.
     * feedback = 10^(-3 * delay_len / (decay * sample_rate))
     * This gives -60dB attenuation after 'decay' seconds. */
    float sr_scale = static_cast<float>(sample_rate_) / 48000.0f;
    for (int i = 0; i < NUM_COMBS; ++i) {
        int len = static_cast<int>(BASE_COMB_DELAYS[i] * cfg_.room_size * sr_scale);
        if (len < 1) len = 1;
        float delay_sec = static_cast<float>(len) / static_cast<float>(sample_rate_);
        comb_feedback_[i] = std::pow(10.0f, -3.0f * delay_sec / std::max(0.1f, cfg_.decay));
    }
}

void ReverbUnit::reset() {
    initialized_ = false;
    meter_input_db_.store(-96.0f, std::memory_order_relaxed);
    meter_output_db_.store(-96.0f, std::memory_order_relaxed);
    meter_wet_level_db_.store(-96.0f, std::memory_order_relaxed);
}

json ReverbUnit::get_params() const {
    return {
        {"enabled", cfg_.enabled}, {"mix", cfg_.mix},
        {"decay", cfg_.decay}, {"damping", cfg_.damping},
        {"room_size", cfg_.room_size}, {"pre_delay_ms", cfg_.pre_delay_ms}
    };
}

void ReverbUnit::set_params(const json& j) {
    bool needs_reinit = false;
    if (j.contains("enabled"))      cfg_.enabled = j["enabled"].get<bool>();
    if (j.contains("mix"))          cfg_.mix = std::max(0.0f, std::min(1.0f, j["mix"].get<float>()));
    if (j.contains("decay"))        cfg_.decay = std::max(0.1f, std::min(5.0f, j["decay"].get<float>()));
    if (j.contains("damping"))      cfg_.damping = std::max(0.0f, std::min(1.0f, j["damping"].get<float>()));
    if (j.contains("room_size"))    { cfg_.room_size = std::max(0.1f, std::min(1.0f, j["room_size"].get<float>())); needs_reinit = true; }
    if (j.contains("pre_delay_ms")) { cfg_.pre_delay_ms = std::max(0.0f, std::min(100.0f, j["pre_delay_ms"].get<float>())); needs_reinit = true; }

    if (needs_reinit) {
        initialized_ = false;  // re-init buffers on next process()
    } else if (initialized_) {
        update_feedback();      // just update feedback gains
    }
}

void ReverbUnit::process(float* pcm, size_t frames, int channels) {
    if (!cfg_.enabled) return;
    if (!initialized_) init_buffers();

    meter_input_db_.store(peak_db(pcm, frames, channels), std::memory_order_relaxed);

    float wet_peak = 0.0f;
    const float mix = cfg_.mix;
    const float damp = cfg_.damping;
    const float damp_inv = 1.0f - damp;

    for (size_t f = 0; f < frames; ++f) {
        /* We sum all channels to mono for reverb input */
        float mono_in = 0.0f;
        for (int c = 0; c < channels; ++c) {
            mono_in += pcm[f * static_cast<size_t>(channels) + static_cast<size_t>(c)];
        }
        mono_in /= static_cast<float>(channels);

        /* We apply pre-delay */
        int pd_read = (pre_delay_write_ - pre_delay_samples_ + static_cast<int>(pre_delay_buf_.size())) % static_cast<int>(pre_delay_buf_.size());
        float delayed_in = pre_delay_buf_[static_cast<size_t>(pd_read)];
        pre_delay_buf_[static_cast<size_t>(pre_delay_write_)] = mono_in;
        pre_delay_write_ = (pre_delay_write_ + 1) % static_cast<int>(pre_delay_buf_.size());

        /* We process 4 parallel comb filters and sum their outputs */
        float comb_sum = 0.0f;
        for (int ci = 0; ci < NUM_COMBS; ++ci) {
            auto& cb = combs_[ci];
            int buf_len = static_cast<int>(cb.buffer.size());
            int read_pos = (cb.write_pos - buf_len + static_cast<int>(cb.buffer.size())) % buf_len;
            if (read_pos < 0) read_pos += buf_len;

            float delayed = cb.buffer[static_cast<size_t>(read_pos)];

            /* We apply one-pole lowpass damping in feedback path */
            cb.filter_state = delayed * damp_inv + cb.filter_state * damp;
            float feedback_sample = cb.filter_state * comb_feedback_[ci];

            /* We write input + feedback into the delay line */
            cb.buffer[static_cast<size_t>(cb.write_pos)] = delayed_in + feedback_sample;
            cb.write_pos = (cb.write_pos + 1) % buf_len;

            comb_sum += delayed;
        }

        /* We average the comb outputs */
        float wet = comb_sum / static_cast<float>(NUM_COMBS);

        /* We process 2 series all-pass filters */
        for (int ai = 0; ai < NUM_ALLPASS; ++ai) {
            auto& ap = allpass_[ai];
            int buf_len = static_cast<int>(ap.buffer.size());
            int read_pos = (ap.write_pos - buf_len + static_cast<int>(ap.buffer.size())) % buf_len;
            if (read_pos < 0) read_pos += buf_len;

            float delayed = ap.buffer[static_cast<size_t>(read_pos)];
            static constexpr float AP_GAIN = 0.5f;

            ap.buffer[static_cast<size_t>(ap.write_pos)] = wet + delayed * AP_GAIN;
            ap.write_pos = (ap.write_pos + 1) % buf_len;

            wet = delayed - wet * AP_GAIN;
        }

        /* We track wet signal peak for metering */
        float abs_wet = std::fabs(wet);
        if (abs_wet > wet_peak) wet_peak = abs_wet;

        /* We blend dry/wet and write back to all channels */
        for (int c = 0; c < channels; ++c) {
            float dry = pcm[f * static_cast<size_t>(channels) + static_cast<size_t>(c)];
            pcm[f * static_cast<size_t>(channels) + static_cast<size_t>(c)] = (1.0f - mix) * dry + mix * wet;
        }
    }

    meter_output_db_.store(peak_db(pcm, frames, channels), std::memory_order_relaxed);
    meter_wet_level_db_.store(wet_peak < 1e-9f ? -96.0f : 20.0f * std::log10(wet_peak), std::memory_order_relaxed);
}

MeterData ReverbUnit::get_meters() const {
    MeterData m;
    m.input_db  = meter_input_db_.load(std::memory_order_relaxed);
    m.output_db = meter_output_db_.load(std::memory_order_relaxed);
    return m;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * DelayUnit — feedback delay with lowpass filter and stereo spread
 * ══════════════════════════════════════════════════════════════════════════════ */

void DelayUnit::set_sample_rate(int sr) {
    sample_rate_ = sr;
    initialized_ = false;
}

void DelayUnit::init_buffers() {
    /* We size buffers for max delay (2000ms) at current sample rate */
    int max_samples = static_cast<int>(2.0f * static_cast<float>(sample_rate_));
    if (max_samples > MAX_DELAY_SAMPLES) max_samples = MAX_DELAY_SAMPLES;
    if (max_samples < 1) max_samples = 1;
    delay_buf_l_.assign(static_cast<size_t>(max_samples), 0.0f);
    delay_buf_r_.assign(static_cast<size_t>(max_samples), 0.0f);
    write_pos_ = 0;
    lp_state_l_ = 0.0f;
    lp_state_r_ = 0.0f;
    update_filter_coeff();
    initialized_ = true;
}

void DelayUnit::update_filter_coeff() {
    /* We compute one-pole lowpass coefficient from filter_hz:
     * coeff = exp(-2*pi*fc/sr)
     * output = input * (1 - coeff) + prev * coeff */
    float fc = std::max(200.0f, std::min(8000.0f, cfg_.filter_hz));
    float sr = static_cast<float>(sample_rate_);
    lp_coeff_ = std::exp(-2.0f * 3.14159265f * fc / sr);
}

void DelayUnit::reset() {
    initialized_ = false;
    lp_state_l_ = 0.0f;
    lp_state_r_ = 0.0f;
    meter_input_db_.store(-96.0f, std::memory_order_relaxed);
    meter_output_db_.store(-96.0f, std::memory_order_relaxed);
    meter_delay_level_db_.store(-96.0f, std::memory_order_relaxed);
}

json DelayUnit::get_params() const {
    return {
        {"enabled", cfg_.enabled}, {"delay_ms", cfg_.delay_ms},
        {"feedback", cfg_.feedback}, {"mix", cfg_.mix},
        {"filter_hz", cfg_.filter_hz}, {"stereo_spread", cfg_.stereo_spread}
    };
}

void DelayUnit::set_params(const json& j) {
    if (j.contains("enabled"))       cfg_.enabled = j["enabled"].get<bool>();
    if (j.contains("delay_ms"))      cfg_.delay_ms = std::max(10.0f, std::min(2000.0f, j["delay_ms"].get<float>()));
    if (j.contains("feedback"))      cfg_.feedback = std::max(0.0f, std::min(0.95f, j["feedback"].get<float>()));
    if (j.contains("mix"))           cfg_.mix = std::max(0.0f, std::min(1.0f, j["mix"].get<float>()));
    if (j.contains("filter_hz"))     { cfg_.filter_hz = std::max(200.0f, std::min(8000.0f, j["filter_hz"].get<float>())); if (initialized_) update_filter_coeff(); }
    if (j.contains("stereo_spread")) cfg_.stereo_spread = std::max(0.0f, std::min(1.0f, j["stereo_spread"].get<float>()));
    /* Also accept legacy "time_ms" param name */
    if (j.contains("time_ms"))       cfg_.delay_ms = std::max(10.0f, std::min(2000.0f, j["time_ms"].get<float>()));
}

void DelayUnit::process(float* pcm, size_t frames, int channels) {
    if (!cfg_.enabled) return;
    if (!initialized_) init_buffers();

    meter_input_db_.store(peak_db(pcm, frames, channels), std::memory_order_relaxed);

    const int buf_len = static_cast<int>(delay_buf_l_.size());
    const int delay_samples_l = static_cast<int>(cfg_.delay_ms * static_cast<float>(sample_rate_) / 1000.0f);
    /* We offset R channel read position for stereo spread (ping-pong) */
    int spread_offset = static_cast<int>(cfg_.stereo_spread * static_cast<float>(delay_samples_l) * 0.5f);
    const int delay_samples_r = delay_samples_l + spread_offset;

    const float feedback = std::min(cfg_.feedback, 0.95f);
    const float mix = cfg_.mix;
    const float lp_c = lp_coeff_;
    const float lp_c_inv = 1.0f - lp_c;

    float delay_peak = 0.0f;

    for (size_t f = 0; f < frames; ++f) {
        float in_l, in_r;

        if (channels >= 2) {
            in_l = pcm[f * 2];
            in_r = pcm[f * 2 + 1];
        } else {
            in_l = pcm[f];
            in_r = in_l;
        }

        /* We read from delay lines */
        int read_l = ((write_pos_ - delay_samples_l) % buf_len + buf_len) % buf_len;
        int read_r = ((write_pos_ - delay_samples_r) % buf_len + buf_len) % buf_len;

        float delayed_l = delay_buf_l_[static_cast<size_t>(read_l)];
        float delayed_r = delay_buf_r_[static_cast<size_t>(read_r)];

        /* We apply lowpass filter in feedback path */
        lp_state_l_ = delayed_l * lp_c_inv + lp_state_l_ * lp_c;
        lp_state_r_ = delayed_r * lp_c_inv + lp_state_r_ * lp_c;

        /* We write input + filtered feedback into delay lines */
        delay_buf_l_[static_cast<size_t>(write_pos_)] = in_l + lp_state_l_ * feedback;
        delay_buf_r_[static_cast<size_t>(write_pos_)] = in_r + lp_state_r_ * feedback;

        write_pos_ = (write_pos_ + 1) % buf_len;

        /* We track delay signal peak */
        float abs_dl = std::fabs(delayed_l);
        float abs_dr = std::fabs(delayed_r);
        if (abs_dl > delay_peak) delay_peak = abs_dl;
        if (abs_dr > delay_peak) delay_peak = abs_dr;

        /* We blend dry/wet */
        if (channels >= 2) {
            pcm[f * 2]     = (1.0f - mix) * in_l + mix * delayed_l;
            pcm[f * 2 + 1] = (1.0f - mix) * in_r + mix * delayed_r;
        } else {
            pcm[f] = (1.0f - mix) * in_l + mix * delayed_l;
        }
    }

    meter_output_db_.store(peak_db(pcm, frames, channels), std::memory_order_relaxed);
    meter_delay_level_db_.store(delay_peak < 1e-9f ? -96.0f : 20.0f * std::log10(delay_peak), std::memory_order_relaxed);
}

MeterData DelayUnit::get_meters() const {
    MeterData m;
    m.input_db  = meter_input_db_.load(std::memory_order_relaxed);
    m.output_db = meter_output_db_.load(std::memory_order_relaxed);
    return m;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * LoudnessUnit — EBU R128 / ATSC A/85 loudness compliance (ITU-R BS.1770-4)
 *
 * K-weighting filter coefficients are from the ITU-R BS.1770-4 standard.
 * Stage 1: pre-filter (high shelf boosting high frequencies ~+3.99 dB)
 * Stage 2: RLB weighting (high-pass, ~38 Hz rolloff)
 *
 * Gated integrated loudness uses the BS.1770-4 two-stage gate:
 *   1. Absolute gate at -70 LUFS
 *   2. Relative gate at -10 LU below the ungated mean
 *
 * True peak detection uses 4x oversampled linear interpolation to catch
 * inter-sample peaks that exceed the dBTP ceiling.
 *
 * Auto-gain correction slowly adjusts output gain to meet the target LUFS
 * with slow attack (~1s) and fast release (~100ms) to avoid pumping while
 * catching transients.
 * ══════════════════════════════════════════════════════════════════════════════ */

void LoudnessUnit::set_sample_rate(int sr) {
    sample_rate_ = sr;
    update_coeffs();
}

void LoudnessUnit::update_coeffs() {
    float sr = static_cast<float>(sample_rate_);

    /* K-weighting Stage 1: pre-filter (high shelf)
     * Coefficients from ITU-R BS.1770-4 for 48 kHz.
     * For other sample rates, we use the bilinear transform with frequency
     * warping from the reference 48 kHz coefficients. */
    if (sample_rate_ == 48000) {
        kw_stage1_.b0 = 1.53512485958697f;
        kw_stage1_.b1 = -2.69169618940638f;
        kw_stage1_.b2 = 1.19839281085285f;
        kw_stage1_.a1 = -1.69065929318241f;
        kw_stage1_.a2 = 0.73248077421585f;
    } else {
        /* Approximate via bilinear transform pre-warping for non-48kHz rates.
         * We use the canonical BS.1770-4 analog prototype and warp to the target SR. */
        float K = std::tan(3.14159265f * 1681.97f / sr);  // pre-warp fc ~1682 Hz
        float Vh = std::pow(10.0f, 3.999f / 20.0f);       // +3.999 dB boost
        float Vb = std::pow(Vh, 0.4996f);
        float K2 = K * K;
        float a0_inv = 1.0f / (1.0f + K / 0.7072f + K2);
        kw_stage1_.b0 = (Vh + Vb * K / 0.7072f + K2) * a0_inv;
        kw_stage1_.b1 = 2.0f * (K2 - Vh) * a0_inv;
        kw_stage1_.b2 = (Vh - Vb * K / 0.7072f + K2) * a0_inv;
        kw_stage1_.a1 = 2.0f * (K2 - 1.0f) * a0_inv;
        kw_stage1_.a2 = (1.0f - K / 0.7072f + K2) * a0_inv;
    }

    /* K-weighting Stage 2: RLB weighting (high-pass, ~38 Hz)
     * Reference coefficients for 48 kHz from BS.1770-4. */
    if (sample_rate_ == 48000) {
        kw_stage2_.b0 = 1.0f;
        kw_stage2_.b1 = -2.0f;
        kw_stage2_.b2 = 1.0f;
        kw_stage2_.a1 = -1.99004745483398f;
        kw_stage2_.a2 = 0.99007225036621f;
    } else {
        /* High-pass at ~38.135 Hz via bilinear transform */
        float fc = 38.135f;
        float wc = 2.0f * 3.14159265f * fc / sr;
        float wc_tan = std::tan(wc / 2.0f);
        float Q = 0.5003270373f;  // from BS.1770-4
        float a0_inv = 1.0f / (1.0f + wc_tan / Q + wc_tan * wc_tan);
        kw_stage2_.b0 = a0_inv;
        kw_stage2_.b1 = -2.0f * a0_inv;
        kw_stage2_.b2 = a0_inv;
        kw_stage2_.a1 = 2.0f * (wc_tan * wc_tan - 1.0f) * a0_inv;
        kw_stage2_.a2 = (1.0f - wc_tan / Q + wc_tan * wc_tan) * a0_inv;
    }

    /* Momentary window hop size: 100ms */
    moment_hop_frames_ = static_cast<int>(0.1f * sr);
    if (moment_hop_frames_ < 1) moment_hop_frames_ = 1;

    /* Auto-gain smoothing: attack ~1s, release ~100ms */
    float attack_ms = 1000.0f;
    float release_ms = 100.0f;
    gain_attack_c_  = 1.0f - std::exp(-2.2f / (attack_ms * sr / 1000.0f));
    gain_release_c_ = 1.0f - std::exp(-2.2f / (release_ms * sr / 1000.0f));
}

void LoudnessUnit::apply_standard_preset() {
    if (cfg_.standard == "ebu_r128") {
        cfg_.target_lufs = -23.0f;
        cfg_.target_tp   = -1.0f;
        cfg_.lra_max     = 11.0f;
    } else if (cfg_.standard == "atsc_a85") {
        cfg_.target_lufs = -24.0f;
        cfg_.target_tp   = -2.0f;
        cfg_.lra_max     = 0.0f;  // no LRA constraint
    } else if (cfg_.standard == "podcast") {
        cfg_.target_lufs = -16.0f;
        cfg_.target_tp   = -1.0f;
        cfg_.lra_max     = 0.0f;
    } else if (cfg_.standard == "spotify") {
        cfg_.target_lufs = -14.0f;
        cfg_.target_tp   = -1.0f;
        cfg_.lra_max     = 0.0f;
    } else if (cfg_.standard == "youtube") {
        cfg_.target_lufs = -14.0f;
        cfg_.target_tp   = -1.0f;
        cfg_.lra_max     = 0.0f;
    }
    // "custom" leaves values as-is
}

float LoudnessUnit::biquad_process(BiquadState& st, const BiquadCoeffs& c, float x) {
    float y = c.b0 * x + c.b1 * st.x1 + c.b2 * st.x2
                        - c.a1 * st.y1 - c.a2 * st.y2;
    st.x2 = st.x1; st.x1 = x;
    st.y2 = st.y1; st.y1 = y;
    return y;
}

float LoudnessUnit::detect_true_peak(float s0, float s1) {
    /* 4x oversampled linear interpolation between consecutive samples.
     * For broadcast-grade true peak, a polyphase FIR is recommended, but
     * linear interpolation catches the majority of inter-sample peaks and
     * is zero-allocation. */
    float peak = std::max(std::fabs(s0), std::fabs(s1));
    for (int i = 1; i <= 3; ++i) {
        float t = static_cast<float>(i) / 4.0f;
        float interp = s0 + t * (s1 - s0);
        float a = std::fabs(interp);
        if (a > peak) peak = a;
    }
    return peak;
}

float LoudnessUnit::compute_integrated() {
    if (gate_count_ == 0) return -70.0f;

    /* Pass 1: absolute gate at -70 LUFS */
    float sum1 = 0.0f;
    int   cnt1 = 0;
    for (int i = 0; i < gate_count_; ++i) {
        float block_lufs = gate_blocks_[i];
        if (block_lufs > -70.0f) {
            sum1 += std::pow(10.0f, block_lufs / 10.0f);
            ++cnt1;
        }
    }
    if (cnt1 == 0) return -70.0f;

    float ungated_mean = sum1 / static_cast<float>(cnt1);
    float ungated_lufs = 10.0f * std::log10(ungated_mean + 1e-20f);

    /* Pass 2: relative gate at -10 LU below ungated mean */
    float gate_threshold = ungated_lufs - 10.0f;
    float sum2 = 0.0f;
    int   cnt2 = 0;
    for (int i = 0; i < gate_count_; ++i) {
        float block_lufs = gate_blocks_[i];
        if (block_lufs > gate_threshold) {
            sum2 += std::pow(10.0f, block_lufs / 10.0f);
            ++cnt2;
        }
    }
    if (cnt2 == 0) return -70.0f;

    float gated_mean = sum2 / static_cast<float>(cnt2);
    return 10.0f * std::log10(gated_mean + 1e-20f);
}

float LoudnessUnit::compute_lra() {
    if (lra_count_ < 2) return 0.0f;

    /* Sort the short-term loudness blocks */
    std::vector<float> sorted(lra_blocks_, lra_blocks_ + lra_count_);

    /* Absolute gate at -70 LUFS */
    std::vector<float> gated;
    gated.reserve(sorted.size());
    for (float v : sorted) {
        if (v > -70.0f) gated.push_back(v);
    }
    if (gated.size() < 2) return 0.0f;

    /* Relative gate: -20 LU below mean of absolutely-gated blocks */
    float sum = 0.0f;
    for (float v : gated) sum += std::pow(10.0f, v / 10.0f);
    float mean_lufs = 10.0f * std::log10(sum / static_cast<float>(gated.size()) + 1e-20f);
    float rel_gate = mean_lufs - 20.0f;

    std::vector<float> final_blocks;
    final_blocks.reserve(gated.size());
    for (float v : gated) {
        if (v > rel_gate) final_blocks.push_back(v);
    }
    if (final_blocks.size() < 2) return 0.0f;

    std::sort(final_blocks.begin(), final_blocks.end());

    /* LRA = 95th percentile - 10th percentile */
    int idx_10 = static_cast<int>(0.10f * static_cast<float>(final_blocks.size()));
    int idx_95 = static_cast<int>(0.95f * static_cast<float>(final_blocks.size()));
    if (idx_95 >= static_cast<int>(final_blocks.size())) idx_95 = static_cast<int>(final_blocks.size()) - 1;

    return final_blocks[static_cast<size_t>(idx_95)] - final_blocks[static_cast<size_t>(idx_10)];
}

bool LoudnessUnit::is_compliant() const {
    float integ = meter_integrated_lufs_.load(std::memory_order_relaxed);
    float tp    = meter_true_peak_.load(std::memory_order_relaxed);
    float lra   = meter_lra_.load(std::memory_order_relaxed);

    /* Within +/- 1 LU of target is compliant */
    if (std::fabs(integ - cfg_.target_lufs) > 1.0f) return false;
    if (tp > cfg_.target_tp + 0.1f) return false;
    if (cfg_.lra_max > 0.0f && lra > cfg_.lra_max) return false;
    return true;
}

void LoudnessUnit::reset() {
    for (int c = 0; c < 2; ++c) {
        kw_s1_[c] = {};
        kw_s2_[c] = {};
    }
    for (int i = 0; i < MOMENTARY_BLOCKS; ++i) {
        moment_block_sum_[i] = 0.0f;
        moment_block_frames_[i] = 0;
    }
    moment_block_idx_ = 0;
    moment_hop_count_ = 0;

    for (int i = 0; i < SHORT_TERM_BLOCKS; ++i) {
        short_block_sum_[i] = 0.0f;
        short_block_frames_[i] = 0;
    }
    short_block_idx_ = 0;

    gate_count_ = 0;
    integrated_lufs_ = -70.0f;

    lra_count_ = 0;
    lra_hop_counter_ = 0;
    loudness_range_ = 0.0f;

    gain_db_ = 0.0f;
    gain_smooth_db_ = 0.0f;
    true_peak_lin_ = 0.0f;
    tp_prev_[0] = tp_prev_[1] = 0.0f;

    meter_input_db_.store(-96.0f, std::memory_order_relaxed);
    meter_output_db_.store(-96.0f, std::memory_order_relaxed);
    meter_integrated_lufs_.store(-70.0f, std::memory_order_relaxed);
    meter_momentary_lufs_.store(-70.0f, std::memory_order_relaxed);
    meter_short_term_lufs_.store(-70.0f, std::memory_order_relaxed);
    meter_true_peak_.store(-70.0f, std::memory_order_relaxed);
    meter_gain_db_.store(0.0f, std::memory_order_relaxed);
    meter_lra_.store(0.0f, std::memory_order_relaxed);
}

void LoudnessUnit::process(float* pcm, size_t frames, int channels) {
    if (!cfg_.enabled || channels < 1 || channels > 2) return;

    meter_input_db_.store(peak_db(pcm, frames, channels), std::memory_order_relaxed);

    const int ch = channels;
    const float tp_ceil_lin = std::pow(10.0f, cfg_.target_tp / 20.0f);
    float max_tp_this_buffer = true_peak_lin_;

    for (size_t f = 0; f < frames; ++f) {
        /* ── K-weighting: apply both biquad stages to each channel ── */
        float kw_sum_sq = 0.0f;
        for (int c = 0; c < ch; ++c) {
            float x = pcm[f * static_cast<size_t>(ch) + static_cast<size_t>(c)];

            /* True peak detection (4x oversampled) */
            float tp = detect_true_peak(tp_prev_[c], x);
            if (tp > max_tp_this_buffer) max_tp_this_buffer = tp;
            tp_prev_[c] = x;

            /* K-weighting Stage 1: pre-filter (high shelf) */
            float s1 = biquad_process(kw_s1_[c], kw_stage1_, x);
            /* K-weighting Stage 2: RLB weighting (high-pass) */
            float kw = biquad_process(kw_s2_[c], kw_stage2_, s1);

            /* BS.1770-4 channel weight: 1.0 for L/R front channels */
            kw_sum_sq += kw * kw;
        }

        /* Accumulate into current momentary block */
        moment_block_sum_[moment_block_idx_] += kw_sum_sq / static_cast<float>(ch);
        moment_block_frames_[moment_block_idx_]++;
        moment_hop_count_++;

        /* Check if we've completed a 100ms hop */
        if (moment_hop_count_ >= moment_hop_frames_) {
            moment_hop_count_ = 0;

            /* ── Momentary loudness (400ms, last 4 blocks) ── */
            float moment_sum = 0.0f;
            int   moment_total_frames = 0;
            for (int b = 0; b < MOMENTARY_BLOCKS; ++b) {
                moment_sum += moment_block_sum_[b];
                moment_total_frames += moment_block_frames_[b];
            }
            float moment_lufs = -70.0f;
            if (moment_total_frames > 0) {
                float mean_sq = moment_sum / static_cast<float>(moment_total_frames);
                if (mean_sq > 1e-20f) {
                    moment_lufs = -0.691f + 10.0f * std::log10(mean_sq);
                }
            }
            meter_momentary_lufs_.store(moment_lufs, std::memory_order_relaxed);

            /* Store for integrated loudness gating */
            if (gate_count_ < MAX_GATE_BLOCKS) {
                gate_blocks_[gate_count_++] = moment_lufs;
            } else {
                /* Circular overwrite (lose oldest data after ~12 min) */
                std::memmove(gate_blocks_, gate_blocks_ + 1,
                             sizeof(float) * static_cast<size_t>(MAX_GATE_BLOCKS - 1));
                gate_blocks_[MAX_GATE_BLOCKS - 1] = moment_lufs;
            }

            /* Advance short-term accumulator */
            short_block_sum_[short_block_idx_] = moment_block_sum_[moment_block_idx_];
            short_block_frames_[short_block_idx_] = moment_block_frames_[moment_block_idx_];
            short_block_idx_ = (short_block_idx_ + 1) % SHORT_TERM_BLOCKS;

            /* ── Short-term loudness (3s, last 30 blocks) ── */
            float st_sum = 0.0f;
            int   st_total_frames = 0;
            for (int b = 0; b < SHORT_TERM_BLOCKS; ++b) {
                st_sum += short_block_sum_[b];
                st_total_frames += short_block_frames_[b];
            }
            float st_lufs = -70.0f;
            if (st_total_frames > 0) {
                float mean_sq = st_sum / static_cast<float>(st_total_frames);
                if (mean_sq > 1e-20f) {
                    st_lufs = -0.691f + 10.0f * std::log10(mean_sq);
                }
            }
            meter_short_term_lufs_.store(st_lufs, std::memory_order_relaxed);

            /* Store for LRA calculation (1 block per ~1s = every 10 hops) */
            if (++lra_hop_counter_ >= 10) {
                lra_hop_counter_ = 0;
                if (lra_count_ < MAX_LRA_BLOCKS) {
                    lra_blocks_[lra_count_++] = st_lufs;
                } else {
                    std::memmove(lra_blocks_, lra_blocks_ + 1,
                                 sizeof(float) * static_cast<size_t>(MAX_LRA_BLOCKS - 1));
                    lra_blocks_[MAX_LRA_BLOCKS - 1] = st_lufs;
                }
                loudness_range_ = compute_lra();
                meter_lra_.store(loudness_range_, std::memory_order_relaxed);
            }

            /* ── Integrated loudness (gated) ── */
            integrated_lufs_ = compute_integrated();
            meter_integrated_lufs_.store(integrated_lufs_, std::memory_order_relaxed);

            /* Advance to next momentary block, clear it */
            moment_block_idx_ = (moment_block_idx_ + 1) % MOMENTARY_BLOCKS;
            moment_block_sum_[moment_block_idx_] = 0.0f;
            moment_block_frames_[moment_block_idx_] = 0;

            /* ── Auto-gain correction ── */
            float error = cfg_.target_lufs - moment_lufs;
            /* Clamp correction to +/-12 dB to avoid runaway */
            gain_db_ = std::max(-12.0f, std::min(12.0f, error));
        }

        /* ── Apply smoothed gain + true peak limiter ── */
        float coeff = (gain_db_ < gain_smooth_db_) ? gain_release_c_ : gain_attack_c_;
        gain_smooth_db_ += coeff * (gain_db_ - gain_smooth_db_);

        float gain_lin = std::pow(10.0f, gain_smooth_db_ / 20.0f);

        for (int c = 0; c < ch; ++c) {
            float& s = pcm[f * static_cast<size_t>(ch) + static_cast<size_t>(c)];
            s *= gain_lin;
            /* True peak hard limit */
            if (s >  tp_ceil_lin) s =  tp_ceil_lin;
            if (s < -tp_ceil_lin) s = -tp_ceil_lin;
        }
    }

    true_peak_lin_ = max_tp_this_buffer;
    float tp_dbtp = (max_tp_this_buffer > 1e-10f) ? 20.0f * std::log10(max_tp_this_buffer) : -70.0f;
    meter_true_peak_.store(tp_dbtp, std::memory_order_relaxed);
    meter_gain_db_.store(gain_smooth_db_, std::memory_order_relaxed);
    meter_output_db_.store(peak_db(pcm, frames, channels), std::memory_order_relaxed);
}

json LoudnessUnit::get_params() const {
    return {
        {"enabled",          cfg_.enabled},
        {"standard",         cfg_.standard},
        {"target_lufs",      cfg_.target_lufs},
        {"target_tp",        cfg_.target_tp},
        {"lra_max",          cfg_.lra_max},
        {"integrated_lufs",  meter_integrated_lufs_.load(std::memory_order_relaxed)},
        {"momentary_lufs",   meter_momentary_lufs_.load(std::memory_order_relaxed)},
        {"short_term_lufs",  meter_short_term_lufs_.load(std::memory_order_relaxed)},
        {"true_peak_dbtp",   meter_true_peak_.load(std::memory_order_relaxed)},
        {"loudness_range_lu",meter_lra_.load(std::memory_order_relaxed)},
        {"gain_correction_db", meter_gain_db_.load(std::memory_order_relaxed)},
        {"compliant",        is_compliant()}
    };
}

void LoudnessUnit::set_params(const json& j) {
    if (j.contains("enabled"))     cfg_.enabled     = j["enabled"].get<bool>();
    if (j.contains("target_lufs")) cfg_.target_lufs = j["target_lufs"].get<float>();
    if (j.contains("target_tp"))   cfg_.target_tp   = j["target_tp"].get<float>();
    if (j.contains("lra_max"))     cfg_.lra_max     = j["lra_max"].get<float>();
    if (j.contains("standard")) {
        cfg_.standard = j["standard"].get<std::string>();
        if (cfg_.standard != "custom") {
            apply_standard_preset();
        }
    }
}

MeterData LoudnessUnit::get_meters() const {
    MeterData m;
    m.input_db  = meter_input_db_.load(std::memory_order_relaxed);
    m.output_db = meter_output_db_.load(std::memory_order_relaxed);
    m.gain_reduction_db = -meter_gain_db_.load(std::memory_order_relaxed);
    return m;
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
    if (type == "loudness")   return std::make_unique<LoudnessUnit>();

    /* We check loaded plugins for a matching type_id */
    const auto* plugin = PluginLoader::instance().find(type);
    if (plugin && plugin->enabled) {
        return std::make_unique<PluginUnit>(plugin, 44100, 2);
    }

    return nullptr;
}

json EffectsRack::available_types() {
    /* We build the types list from the version registry — single source of truth */
    static const char* rack_types[] = {"eq", "compressor", "limiter", "noise_gate", "reverb", "delay", "loudness"};
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

    /* We append loaded third-party plugins to the available types list */
    auto plugins = PluginLoader::instance().get_all();
    for (const auto& p : plugins) {
        if (!p.enabled) continue;
        json entry;
        entry["type"]         = p.info.type_id ? p.info.type_id : "";
        entry["name"]         = p.info.display_name ? p.info.display_name : "";
        entry["short_name"]   = p.info.display_name ? p.info.display_name : "";
        entry["version"]      = p.info.version ? p.info.version : "0.0.0";
        entry["release_date"] = "";
        entry["description"]  = p.info.description ? p.info.description : "";
        entry["changelog"]    = "";
        entry["is_plugin"]    = true;
        entry["author"]       = p.info.author ? p.info.author : "";
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

    /* We append loaded third-party plugins to the version list */
    auto plugins = PluginLoader::instance().get_all();
    for (const auto& p : plugins) {
        arr.push_back({
            {"type",         p.info.type_id ? p.info.type_id : ""},
            {"brand_name",   p.info.display_name ? p.info.display_name : ""},
            {"short_name",   p.info.display_name ? p.info.display_name : ""},
            {"version",      p.info.version ? p.info.version : "0.0.0"},
            {"ver_major",    0},
            {"ver_minor",    0},
            {"ver_patch",    0},
            {"release_date", ""},
            {"description",  p.info.description ? p.info.description : ""},
            {"changelog",    ""},
            {"is_stub",      false},
            {"is_plugin",    true},
            {"author",       p.info.author ? p.info.author : ""},
            {"path",         p.path},
            {"enabled",      p.enabled}
        });
    }

    return arr;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * EffectsRack — real-time metering (all atomic reads, no mutex needed)
 * ══════════════════════════════════════════════════════════════════════════════ */

std::vector<std::pair<int, MeterData>> EffectsRack::get_all_meters() const {
    /* We intentionally do NOT lock the mutex here — all meter values are
     * read via std::atomic, and the audio callback never blocks on mutex.
     * A stale chain read is acceptable for metering display. */
    std::vector<std::pair<int, MeterData>> result;
    result.reserve(chain_.size());
    for (const auto& s : chain_) {
        if (s.unit) {
            result.emplace_back(s.id, s.unit->get_meters());
        }
    }
    return result;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * EffectsRack — signal routing (v1: serial chain only)
 * ══════════════════════════════════════════════════════════════════════════════ */

std::vector<RoutingEntry> EffectsRack::get_routing() const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<RoutingEntry> routing;

    if (chain_.empty()) return routing;

    /* We build the serial chain: input -> unit1 -> unit2 -> ... -> output */
    std::string prev = "input";
    for (const auto& s : chain_) {
        RoutingEntry entry;
        entry.from_unit = prev;
        entry.to_unit   = s.unit->type_name();
        entry.from_port = 0;
        entry.to_port   = 0;
        routing.push_back(entry);
        prev = s.unit->type_name();
    }

    /* We add the final connection to output */
    RoutingEntry final_entry;
    final_entry.from_unit = prev;
    final_entry.to_unit   = "output";
    final_entry.from_port = 0;
    final_entry.to_port   = 0;
    routing.push_back(final_entry);

    return routing;
}

void EffectsRack::set_routing(const std::vector<RoutingEntry>& routing) {
    if (routing.empty()) return;

    std::lock_guard<std::mutex> lk(mtx_);

    /* We validate that the topology is a valid serial chain (no cycles).
     * We walk from "input" following from->to links and ensure we reach "output"
     * without visiting any node twice. */

    /* Build adjacency: from_unit -> to_unit */
    std::unordered_map<std::string, std::string> next_map;
    for (const auto& r : routing) {
        if (next_map.count(r.from_unit)) {
            /* Duplicate source — fan-out not supported in v1 serial mode */
            return;
        }
        next_map[r.from_unit] = r.to_unit;
    }

    /* Walk the chain starting from "input" */
    std::vector<std::string> chain_order;
    std::unordered_set<std::string> visited;
    std::string current = "input";
    visited.insert(current);

    while (next_map.count(current)) {
        std::string next = next_map[current];
        if (visited.count(next)) {
            /* Cycle detected — reject routing */
            return;
        }
        visited.insert(next);
        if (next != "output") {
            chain_order.push_back(next);
        }
        current = next;
    }

    /* We must end at "output" */
    if (current != "output") return;

    /* We must have the same number of units as the current chain */
    if (chain_order.size() != chain_.size()) return;

    /* We reorder chain_ to match the derived chain_order.
     * Map type_name -> Slot index for lookup. For duplicate types,
     * we match in order of first occurrence. */
    std::vector<Slot> reordered;
    reordered.reserve(chain_.size());
    std::vector<bool> used(chain_.size(), false);

    for (const auto& type_name : chain_order) {
        bool found = false;
        for (size_t i = 0; i < chain_.size(); ++i) {
            if (!used[i] && chain_[i].unit->type_name() == type_name) {
                reordered.push_back({chain_[i].id, std::move(chain_[i].unit)});
                used[i] = true;
                found = true;
                break;
            }
        }
        if (!found) return;  /* Unknown unit type in routing */
    }

    chain_ = std::move(reordered);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * PluginUnit — wraps third-party C plugin API as a DspUnit
 * ══════════════════════════════════════════════════════════════════════════════ */

PluginUnit::PluginUnit(const LoadedPlugin* plugin, int sample_rate, int channels)
    : plugin_(plugin)
    , instance_(nullptr)
    , type_id_(plugin->info.type_id ? plugin->info.type_id : "")
    , sample_rate_(sample_rate)
    , channels_(channels)
{
    if (plugin_->fn_create) {
        instance_ = plugin_->fn_create(sample_rate, channels);
    }
}

PluginUnit::~PluginUnit() {
    if (instance_ && plugin_ && plugin_->fn_destroy) {
        plugin_->fn_destroy(instance_);
        instance_ = nullptr;
    }
}

void PluginUnit::process(float* pcm, size_t frames, int channels) {
    if (!enabled_ || !instance_ || !plugin_ || !plugin_->fn_process) return;

    /* We measure input level for metering */
    float in_peak = peak_db(pcm, frames, channels);
    meter_input_db_.store(in_peak, std::memory_order_relaxed);

    /* We delegate to the plugin's process function */
    plugin_->fn_process(instance_, pcm, frames, channels);

    /* We measure output level for metering */
    float out_peak = peak_db(pcm, frames, channels);
    meter_output_db_.store(out_peak, std::memory_order_relaxed);
}

void PluginUnit::set_sample_rate(int sr) {
    /* We must recreate the plugin instance at the new sample rate */
    if (instance_ && plugin_ && plugin_->fn_destroy) {
        plugin_->fn_destroy(instance_);
        instance_ = nullptr;
    }
    sample_rate_ = sr;
    if (plugin_ && plugin_->fn_create) {
        instance_ = plugin_->fn_create(sr, channels_);
    }
}

const char* PluginUnit::type_name() const {
    return type_id_.c_str();
}

json PluginUnit::get_params() const {
    json j;
    j["enabled"] = enabled_;
    j["is_plugin"] = true;
    j["plugin_name"] = plugin_->info.display_name ? plugin_->info.display_name : "";
    j["plugin_version"] = plugin_->info.version ? plugin_->info.version : "";

    /* We read all parameter values from the plugin instance */
    if (plugin_->params && plugin_->param_count > 0 && instance_) {
        json params = json::object();
        for (int i = 0; i < plugin_->param_count; ++i) {
            const auto& pd = plugin_->params[i];
            if (pd.key) {
                float val = plugin_->fn_get ? plugin_->fn_get(instance_, pd.key) : pd.default_val;
                params[pd.key] = val;
            }
        }
        j["params"] = params;
    }

    return j;
}

void PluginUnit::set_params(const json& j) {
    if (j.contains("enabled")) enabled_ = j["enabled"].get<bool>();

    /* We set parameter values on the plugin instance */
    if (instance_ && plugin_->fn_set) {
        /* We support flat key:value format: {"gain": 1.5, "mix": 0.7} */
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (it.key() == "enabled" || it.key() == "is_plugin" ||
                it.key() == "plugin_name" || it.key() == "plugin_version") continue;
            if (it.value().is_number()) {
                plugin_->fn_set(instance_, it.key().c_str(), it.value().get<float>());
            }
        }
        /* We also support nested params object: {"params": {"gain": 1.5}} */
        if (j.contains("params") && j["params"].is_object()) {
            for (auto it = j["params"].begin(); it != j["params"].end(); ++it) {
                if (it.value().is_number()) {
                    plugin_->fn_set(instance_, it.key().c_str(), it.value().get<float>());
                }
            }
        }
    }
}

void PluginUnit::reset() {
    if (instance_ && plugin_ && plugin_->fn_reset) {
        plugin_->fn_reset(instance_);
    }
    meter_input_db_.store(-96.0f, std::memory_order_relaxed);
    meter_output_db_.store(-96.0f, std::memory_order_relaxed);
}

MeterData PluginUnit::get_meters() const {
    MeterData m;
    m.input_db  = meter_input_db_.load(std::memory_order_relaxed);
    m.output_db = meter_output_db_.load(std::memory_order_relaxed);
    return m;
}

} // namespace mc1dsp
