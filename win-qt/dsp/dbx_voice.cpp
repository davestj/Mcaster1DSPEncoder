// dsp/dbx_voice.cpp — DBX 286s Voice Processor implementation
// Phase M8 — Mcaster1DSPEncoder macOS
//
// Processing order: Gate → Compressor → De-Esser → LF Shelf → HF Shelf
// Each section is independently enable/disable-able.
#include "dbx_voice.h"

#include <cmath>
#include <cstring>
#include <algorithm>

namespace mc1dsp {

// Same time constant formula as agc.cpp
static float time_const(float time_ms, float sr)
{
    if (time_ms <= 0.0f || sr <= 0.0f) return 1.0f;
    return 1.0f - expf(-2.2f / (time_ms * sr / 1000.0f));
}

// ── Biquad coefficient calculators (RBJ Audio EQ Cookbook) ────────────────

void DspDbxVoice::calc_bandpass(Biquad& bq, float freq_hz, float q)
{
    const float sr    = static_cast<float>(sample_rate_);
    const float omega = 2.0f * static_cast<float>(M_PI) * freq_hz / sr;
    const float sin_w = sinf(omega);
    const float cos_w = cosf(omega);
    const float alpha = sin_w / (2.0f * q);

    const float b0 =  alpha;
    const float b1 =  0.0f;
    const float b2 = -alpha;
    const float a0 =  1.0f + alpha;
    const float a1 = -2.0f * cos_w;
    const float a2 =  1.0f - alpha;

    bq.b0 = b0 / a0;
    bq.b1 = b1 / a0;
    bq.b2 = b2 / a0;
    bq.a1 = a1 / a0;
    bq.a2 = a2 / a0;
}

void DspDbxVoice::calc_low_shelf(Biquad& bq, float freq_hz, float gain_db, float q)
{
    const float sr    = static_cast<float>(sample_rate_);
    const float omega = 2.0f * static_cast<float>(M_PI) * freq_hz / sr;
    const float sin_w = sinf(omega);
    const float cos_w = cosf(omega);
    const float A     = powf(10.0f, gain_db / 40.0f);
    const float sqrtA = sqrtf(A);
    const float alpha = sin_w / 2.0f * sqrtf((A + 1.0f/A) * (1.0f/q - 1.0f) + 2.0f);

    const float b0 =     A * ((A+1.0f) - (A-1.0f)*cos_w + 2.0f*sqrtA*alpha);
    const float b1 = 2.0f*A * ((A-1.0f) - (A+1.0f)*cos_w);
    const float b2 =     A * ((A+1.0f) - (A-1.0f)*cos_w - 2.0f*sqrtA*alpha);
    const float a0 =          (A+1.0f) + (A-1.0f)*cos_w + 2.0f*sqrtA*alpha;
    const float a1 = -2.0f *  ((A-1.0f) + (A+1.0f)*cos_w);
    const float a2 =          (A+1.0f) + (A-1.0f)*cos_w - 2.0f*sqrtA*alpha;

    bq.b0 = b0 / a0;
    bq.b1 = b1 / a0;
    bq.b2 = b2 / a0;
    bq.a1 = a1 / a0;
    bq.a2 = a2 / a0;
}

void DspDbxVoice::calc_high_shelf(Biquad& bq, float freq_hz, float gain_db, float q)
{
    const float sr    = static_cast<float>(sample_rate_);
    const float omega = 2.0f * static_cast<float>(M_PI) * freq_hz / sr;
    const float sin_w = sinf(omega);
    const float cos_w = cosf(omega);
    const float A     = powf(10.0f, gain_db / 40.0f);
    const float sqrtA = sqrtf(A);
    const float alpha = sin_w / 2.0f * sqrtf((A + 1.0f/A) * (1.0f/q - 1.0f) + 2.0f);

    const float b0 =     A * ((A+1.0f) + (A-1.0f)*cos_w + 2.0f*sqrtA*alpha);
    const float b1 = -2.0f*A * ((A-1.0f) + (A+1.0f)*cos_w);
    const float b2 =     A * ((A+1.0f) + (A-1.0f)*cos_w - 2.0f*sqrtA*alpha);
    const float a0 =          (A+1.0f) - (A-1.0f)*cos_w + 2.0f*sqrtA*alpha;
    const float a1 =  2.0f *  ((A-1.0f) - (A+1.0f)*cos_w);
    const float a2 =          (A+1.0f) - (A-1.0f)*cos_w - 2.0f*sqrtA*alpha;

    bq.b0 = b0 / a0;
    bq.b1 = b1 / a0;
    bq.b2 = b2 / a0;
    bq.a1 = a1 / a0;
    bq.a2 = a2 / a0;
}

// ── Coefficient recalculation ────────────────────────────────────────────

void DspDbxVoice::recalc()
{
    const float sr = static_cast<float>(sample_rate_);

    // Gate
    gate_attack_coeff_  = time_const(cfg_.gate.attack_ms,  sr);
    gate_release_coeff_ = time_const(cfg_.gate.release_ms, sr);

    // Compressor
    comp_attack_coeff_  = time_const(cfg_.compressor.attack_ms,  sr);
    comp_release_coeff_ = time_const(cfg_.compressor.release_ms, sr);

    // De-esser: ~3ms attack, ~50ms release (fixed fast values)
    deess_attack_coeff_  = time_const(3.0f,  sr);
    deess_release_coeff_ = time_const(50.0f, sr);

    // De-esser bandpass sidechain
    for (auto& bq : deess_bp_)
        calc_bandpass(bq, cfg_.deesser.frequency_hz, cfg_.deesser.bandwidth_q);

    // LF enhancer: low-shelf
    for (auto& bq : lf_shelf_)
        calc_low_shelf(bq, cfg_.lf_enhancer.frequency_hz, cfg_.lf_enhancer.amount_db, 0.707f);

    // HF detail: high-shelf
    for (auto& bq : hf_shelf_)
        calc_high_shelf(bq, cfg_.hf_detail.frequency_hz, cfg_.hf_detail.amount_db, 0.707f);
}

void DspDbxVoice::configure(const DbxVoiceConfig& cfg, int sample_rate)
{
    cfg_ = cfg;
    sample_rate_ = sample_rate;
    recalc();
}

void DspDbxVoice::reset()
{
    gate_gain_lin_  = 1.0f;
    comp_gain_lin_  = 1.0f;
    deess_gain_lin_ = 1.0f;

    for (auto& bq : deess_bp_)  bq.reset();
    for (auto& bq : lf_shelf_)  bq.reset();
    for (auto& bq : hf_shelf_)  bq.reset();

    gate_gr_db_.store(0.0f, std::memory_order_relaxed);
    comp_gr_db_.store(0.0f, std::memory_order_relaxed);
    deess_gr_db_.store(0.0f, std::memory_order_relaxed);
}

// ── Main processing ──────────────────────────────────────────────────────

void DspDbxVoice::process(float* pcm, size_t frames, int channels)
{
    if (!cfg_.enabled || channels < 1 || channels > 2) return;

    const int ch = channels;
    float max_gate_gr = 0.0f;
    float max_comp_gr = 0.0f;
    float max_deess_gr = 0.0f;

    for (size_t i = 0; i < frames; i++) {
        // Peak detect across channels
        float peak = 0.0f;
        for (int c = 0; c < ch; c++) {
            float s = fabsf(pcm[i * ch + c]);
            if (s > peak) peak = s;
        }
        const float peak_db = (peak > 1e-9f) ? 20.0f * log10f(peak) : -100.0f;

        // ── Section 1: Expander/Gate ──
        if (cfg_.gate.enabled) {
            float gate_target = 1.0f;
            if (peak_db < cfg_.gate.threshold_db) {
                // Downward expansion: reduce gain below threshold
                float below_db = cfg_.gate.threshold_db - peak_db;
                float reduction_db = below_db * (cfg_.gate.ratio - 1.0f);
                gate_target = powf(10.0f, -reduction_db / 20.0f);
                if (gate_target < 0.001f) gate_target = 0.001f;  // floor at -60 dB
            }
            float coeff = (gate_target < gate_gain_lin_)
                ? gate_attack_coeff_ : gate_release_coeff_;
            gate_gain_lin_ += coeff * (gate_target - gate_gain_lin_);

            for (int c = 0; c < ch; c++)
                pcm[i * ch + c] *= gate_gain_lin_;

            float gr = -20.0f * log10f(gate_gain_lin_ + 1e-9f);
            if (gr > max_gate_gr) max_gate_gr = gr;
        }

        // ── Section 2: Compressor ──
        if (cfg_.compressor.enabled) {
            // Re-measure peak after gate
            float c_peak = 0.0f;
            for (int c = 0; c < ch; c++) {
                float s = fabsf(pcm[i * ch + c]);
                if (s > c_peak) c_peak = s;
            }
            float c_peak_db = (c_peak > 1e-9f) ? 20.0f * log10f(c_peak) : -100.0f;

            float comp_target = 1.0f;
            if (c_peak_db > cfg_.compressor.threshold_db) {
                float over_db = c_peak_db - cfg_.compressor.threshold_db;
                float gain_db = -over_db * (1.0f - 1.0f / cfg_.compressor.ratio);
                comp_target = powf(10.0f, gain_db / 20.0f);
            }
            float coeff = (comp_target < comp_gain_lin_)
                ? comp_attack_coeff_ : comp_release_coeff_;
            comp_gain_lin_ += coeff * (comp_target - comp_gain_lin_);

            for (int c = 0; c < ch; c++)
                pcm[i * ch + c] *= comp_gain_lin_;

            float gr = -20.0f * log10f(comp_gain_lin_ + 1e-9f);
            if (gr > max_comp_gr) max_comp_gr = gr;
        }

        // ── Section 3: De-Esser ──
        if (cfg_.deesser.enabled) {
            // Sidechain: bandpass filter to isolate sibilant frequencies
            float sc_peak = 0.0f;
            for (int c = 0; c < ch; c++) {
                float sc = deess_bp_[c].process(pcm[i * ch + c], 0);
                float s = fabsf(sc);
                if (s > sc_peak) sc_peak = s;
            }
            float sc_db = (sc_peak > 1e-9f) ? 20.0f * log10f(sc_peak) : -100.0f;

            float deess_target = 1.0f;
            if (sc_db > cfg_.deesser.threshold_db) {
                // Proportional reduction based on how far above threshold
                float over_db = sc_db - cfg_.deesser.threshold_db;
                float reduction = std::min(over_db, -cfg_.deesser.reduction_db);
                deess_target = powf(10.0f, -reduction / 20.0f);
            }
            float coeff = (deess_target < deess_gain_lin_)
                ? deess_attack_coeff_ : deess_release_coeff_;
            deess_gain_lin_ += coeff * (deess_target - deess_gain_lin_);

            for (int c = 0; c < ch; c++)
                pcm[i * ch + c] *= deess_gain_lin_;

            float gr = -20.0f * log10f(deess_gain_lin_ + 1e-9f);
            if (gr > max_deess_gr) max_deess_gr = gr;
        }

        // ── Section 4: LF Enhancer (low-shelf) ──
        if (cfg_.lf_enhancer.enabled && cfg_.lf_enhancer.amount_db > 0.01f) {
            for (int c = 0; c < ch; c++)
                pcm[i * ch + c] = lf_shelf_[c].process(pcm[i * ch + c], 0);
        }

        // ── Section 5: HF Detail (high-shelf) ──
        if (cfg_.hf_detail.enabled && cfg_.hf_detail.amount_db > 0.01f) {
            for (int c = 0; c < ch; c++)
                pcm[i * ch + c] = hf_shelf_[c].process(pcm[i * ch + c], 0);
        }
    }

    // Update atomic meters
    gate_gr_db_.store(max_gate_gr, std::memory_order_relaxed);
    comp_gr_db_.store(max_comp_gr, std::memory_order_relaxed);
    deess_gr_db_.store(max_deess_gr, std::memory_order_relaxed);
}

// ── Presets ──────────────────────────────────────────────────────────────

static const char* kPresetNames[DspDbxVoice::kPresetCount] = {
    "Broadcast Voice",
    "Podcast Intimate",
    "Voice-Over Warm",
    "Raw/Flat"
};

const char* DspDbxVoice::preset_name(int index)
{
    if (index < 0 || index >= kPresetCount) return "Unknown";
    return kPresetNames[index];
}

DbxVoiceConfig DspDbxVoice::preset_config(int index)
{
    DbxVoiceConfig c;
    c.enabled = true;

    switch (index) {
    case 0: // Broadcast Voice
        c.gate.threshold_db = -45.0f; c.gate.ratio = 3.0f;
        c.gate.attack_ms = 1.0f;     c.gate.release_ms = 80.0f;
        c.compressor.threshold_db = -18.0f; c.compressor.ratio = 4.0f;
        c.compressor.attack_ms = 3.0f;      c.compressor.release_ms = 120.0f;
        c.deesser.frequency_hz = 6000.0f; c.deesser.threshold_db = -18.0f;
        c.deesser.reduction_db = -8.0f;
        c.lf_enhancer.frequency_hz = 120.0f; c.lf_enhancer.amount_db = 4.0f;
        c.hf_detail.frequency_hz = 4000.0f;  c.hf_detail.amount_db = 3.0f;
        break;

    case 1: // Podcast Intimate
        c.gate.threshold_db = -50.0f; c.gate.ratio = 2.0f;
        c.gate.attack_ms = 2.0f;     c.gate.release_ms = 120.0f;
        c.compressor.threshold_db = -15.0f; c.compressor.ratio = 3.0f;
        c.compressor.attack_ms = 5.0f;      c.compressor.release_ms = 200.0f;
        c.deesser.frequency_hz = 5000.0f; c.deesser.threshold_db = -22.0f;
        c.deesser.reduction_db = -5.0f;
        c.lf_enhancer.frequency_hz = 100.0f; c.lf_enhancer.amount_db = 5.0f;
        c.hf_detail.frequency_hz = 3500.0f;  c.hf_detail.amount_db = 2.0f;
        break;

    case 2: // Voice-Over Warm
        c.gate.threshold_db = -55.0f; c.gate.ratio = 2.0f;
        c.gate.attack_ms = 1.5f;     c.gate.release_ms = 150.0f;
        c.compressor.threshold_db = -20.0f; c.compressor.ratio = 3.0f;
        c.compressor.attack_ms = 4.0f;      c.compressor.release_ms = 180.0f;
        c.deesser.frequency_hz = 6000.0f; c.deesser.threshold_db = -20.0f;
        c.deesser.reduction_db = -6.0f;
        c.lf_enhancer.frequency_hz = 80.0f;  c.lf_enhancer.amount_db = 6.0f;
        c.hf_detail.frequency_hz = 5000.0f;  c.hf_detail.amount_db = 1.0f;
        break;

    case 3: // Raw/Flat
    default:
        c.gate.enabled = false;
        c.compressor.enabled = false;
        c.deesser.enabled = false;
        c.lf_enhancer.enabled = false;  c.lf_enhancer.amount_db = 0.0f;
        c.hf_detail.enabled = false;    c.hf_detail.amount_db = 0.0f;
        break;
    }
    return c;
}

bool DspDbxVoice::apply_preset(const std::string& name)
{
    for (int i = 0; i < kPresetCount; i++) {
        if (name == kPresetNames[i]) {
            configure(preset_config(i), sample_rate_);
            return true;
        }
    }
    return false;
}

} // namespace mc1dsp
