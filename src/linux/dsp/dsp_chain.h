// dsp/dsp_chain.h — Master DSP processing chain
// Phase 4 — Mcaster1DSPEncoder Linux v1.3.0
//
// Processing order per audio buffer:
//   [Input PCM float32] → EQ → AGC/Compressor → [Encoder]
//   Crossfader is applied at track boundaries by EncoderSlot.
#pragma once

#include "eq.h"
#include "agc.h"
#include "crossfader.h"

#include <cstddef>
#include <string>

namespace mc1dsp {

struct DspChainConfig {
    bool   eq_enabled         = false;
    bool   agc_enabled        = false;
    bool   crossfader_enabled = true;
    int    sample_rate        = 44100;
    int    channels           = 2;
    float  crossfade_duration = 3.0f;   // seconds
    // EQ preset applied on configure (empty = no preset)
    std::string eq_preset;
};

// ---------------------------------------------------------------------------
// DspChain — owns EQ, AGC, Crossfader; called from audio thread
// ---------------------------------------------------------------------------
class DspChain {
public:
    DspChain() = default;

    // (Re)configure: updates sample rate, enables/disables modules, applies EQ preset
    void configure(const DspChainConfig& cfg);

    // Process one PCM buffer in-place: EQ → AGC
    // Crossfader is NOT called here — use crossfader() directly from EncoderSlot
    void process(float* pcm, size_t frames);

    DspEq&         eq()         { return eq_; }
    DspAgc&        agc()        { return agc_; }
    DspCrossfader& crossfader() { return crossfader_; }

    const DspEq&         eq()         const { return eq_; }
    const DspAgc&        agc()        const { return agc_; }
    const DspCrossfader& crossfader() const { return crossfader_; }

    const DspChainConfig& config() const { return cfg_; }

private:
    DspChainConfig cfg_;
    DspEq          eq_;
    DspAgc         agc_;
    DspCrossfader  crossfader_;
};

} // namespace mc1dsp
