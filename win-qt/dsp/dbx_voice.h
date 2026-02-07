// dsp/dbx_voice.h — DBX 286s Voice Processor clone
// Phase M8 — Mcaster1DSPEncoder macOS
//
// 5-section voice channel strip modeled on the DBX 286s:
// 1. Expander/Gate — noise gate with adjustable ratio
// 2. Compressor — feed-forward with attack/release
// 3. De-Esser — bandpass sidechain sibilance reduction
// 4. LF Enhancer — low-shelf bass warmth
// 5. HF Detail — high-shelf presence/air boost
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <string>
#include "eq.h"  // for Biquad struct

namespace mc1dsp {

struct DbxVoiceConfig {
    bool enabled = false;

    // Section 1: Expander/Gate
    struct GateSection {
        float threshold_db = -50.0f;  // -80 to 0 dBFS
        float ratio        = 2.0f;    // 1:1 to 10:1 (expansion below threshold)
        float attack_ms    = 1.0f;    // 0.5 to 50 ms
        float release_ms   = 100.0f;  // 10 to 500 ms
        bool  enabled      = true;
    } gate;

    // Section 2: Compressor
    struct CompressorSection {
        float threshold_db = -20.0f;  // -60 to 0 dBFS
        float ratio        = 3.0f;    // 1:1 to 20:1
        float attack_ms    = 5.0f;    // 0.1 to 100 ms
        float release_ms   = 150.0f;  // 10 to 1000 ms
        bool  enabled      = true;
    } compressor;

    // Section 3: De-Esser
    struct DeEsserSection {
        float frequency_hz = 6000.0f;  // 3000 to 10000 Hz sidechain center
        float threshold_db = -20.0f;   // sidechain detection threshold
        float reduction_db = -6.0f;    // max reduction when sibilance detected
        float bandwidth_q  = 2.0f;     // sidechain bandpass Q
        bool  enabled      = true;
    } deesser;

    // Section 4: LF Enhancer (low-shelf bass boost)
    struct LfEnhancerSection {
        float frequency_hz = 120.0f;   // 50 to 250 Hz
        float amount_db    = 3.0f;     // 0 to 12 dB
        bool  enabled      = true;
    } lf_enhancer;

    // Section 5: HF Detail (high-shelf presence/air boost)
    struct HfDetailSection {
        float frequency_hz = 4000.0f;  // 2000 to 8000 Hz
        float amount_db    = 3.0f;     // 0 to 12 dB
        bool  enabled      = true;
    } hf_detail;
};

// ---------------------------------------------------------------------------
// DspDbxVoice — 5-section voice channel strip
// ---------------------------------------------------------------------------
class DspDbxVoice {
public:
    DspDbxVoice() = default;

    void configure(const DbxVoiceConfig& cfg, int sample_rate);
    void process(float* pcm, size_t frames, int channels);
    void reset();

    void set_config(const DbxVoiceConfig& cfg) { cfg_ = cfg; recalc(); }
    const DbxVoiceConfig& config() const { return cfg_; }

    void set_enabled(bool on) { cfg_.enabled = on; }
    bool is_enabled()   const { return cfg_.enabled; }

    // Atomic metering for UI (all positive dB of gain reduction)
    float gate_reduction_db()  const { return gate_gr_db_.load(std::memory_order_relaxed); }
    float comp_reduction_db()  const { return comp_gr_db_.load(std::memory_order_relaxed); }
    float deess_reduction_db() const { return deess_gr_db_.load(std::memory_order_relaxed); }

    // Presets
    static constexpr int kPresetCount = 4;
    static const char* preset_name(int index);
    static DbxVoiceConfig preset_config(int index);
    bool apply_preset(const std::string& name);

private:
    DbxVoiceConfig cfg_;
    int sample_rate_ = 44100;

    // Gate state
    float gate_gain_lin_     = 1.0f;
    float gate_attack_coeff_ = 0.0f;
    float gate_release_coeff_= 0.0f;

    // Compressor state
    float comp_gain_lin_     = 1.0f;
    float comp_attack_coeff_ = 0.0f;
    float comp_release_coeff_= 0.0f;

    // De-esser: bandpass sidechain + gain state
    std::array<Biquad, 2> deess_bp_;         // bandpass at ~6kHz, per channel
    float deess_gain_lin_      = 1.0f;
    float deess_attack_coeff_  = 0.0f;
    float deess_release_coeff_ = 0.0f;

    // LF Enhancer: low-shelf filter, per channel
    std::array<Biquad, 2> lf_shelf_;

    // HF Detail: high-shelf filter, per channel
    std::array<Biquad, 2> hf_shelf_;

    // Atomic metering (positive dB of gain reduction)
    std::atomic<float> gate_gr_db_{0.0f};
    std::atomic<float> comp_gr_db_{0.0f};
    std::atomic<float> deess_gr_db_{0.0f};

    void recalc();
    void calc_bandpass(Biquad& bq, float freq_hz, float q);
    void calc_low_shelf(Biquad& bq, float freq_hz, float gain_db, float q);
    void calc_high_shelf(Biquad& bq, float freq_hz, float gain_db, float q);
};

} // namespace mc1dsp
