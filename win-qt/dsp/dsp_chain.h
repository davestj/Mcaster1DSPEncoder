// dsp/dsp_chain.h — Master DSP processing chain
// Phase M6 — Mcaster1DSPEncoder macOS v0.6.0
//
// Processing order per audio buffer:
//   [Input PCM float32] → EQ → Sonic Enhancer → PTT Duck → AGC → DBX Voice → [Encoder]
//   Crossfader is applied at track boundaries by EncoderSlot.
#pragma once

#include "eq.h"
#include "eq31.h"
#include "agc.h"
#include "crossfader.h"
#include "sonic_enhancer.h"
#include "ptt_duck.h"
#include "dbx_voice.h"

#include <cstddef>
#include <string>

namespace mc1dsp {

/* Which EQ engine to use */
enum class EqMode { PARAMETRIC_10, GRAPHIC_31 };

struct DspChainConfig {
    bool   eq_enabled         = false;
    bool   agc_enabled        = false;
    bool   crossfader_enabled = true;
    bool   sonic_enabled      = false;   // Phase M11.5: Sonic Enhancer enable flag
    bool   ptt_duck_enabled   = false;   // Phase M8: Push-to-Talk ducking
    bool   dbx_voice_enabled  = false;   // Phase M8: DBX 286s voice processor
    int    sample_rate        = 44100;
    int    channels           = 2;
    float  crossfade_duration = 3.0f;   // seconds
    // EQ preset applied on configure (empty = no preset)
    std::string eq_preset;
    // Phase M6: 31-band graphic EQ
    EqMode      eq_mode       = EqMode::PARAMETRIC_10;
    std::string eq31_preset;
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

    DspEq&              eq()              { return eq_; }
    DspEq31&            eq31()            { return eq31_; }
    DspAgc&             agc()             { return agc_; }
    DspCrossfader&      crossfader()      { return crossfader_; }
    DspSonicEnhancer&   sonic_enhancer()  { return sonic_enhancer_; }
    DspPttDuck&         ptt_duck()        { return ptt_duck_; }
    DspDbxVoice&        dbx_voice()       { return dbx_voice_; }

    const DspEq&              eq()              const { return eq_; }
    const DspEq31&            eq31()            const { return eq31_; }
    const DspAgc&             agc()             const { return agc_; }
    const DspCrossfader&      crossfader()      const { return crossfader_; }
    const DspSonicEnhancer&   sonic_enhancer()  const { return sonic_enhancer_; }
    const DspPttDuck&         ptt_duck()        const { return ptt_duck_; }
    const DspDbxVoice&        dbx_voice()       const { return dbx_voice_; }

    const DspChainConfig& config() const { return cfg_; }

private:
    DspChainConfig   cfg_;
    DspEq            eq_;
    DspEq31          eq31_;
    DspSonicEnhancer sonic_enhancer_;
    DspPttDuck       ptt_duck_;
    DspAgc           agc_;
    DspDbxVoice      dbx_voice_;
    DspCrossfader    crossfader_;
};

} // namespace mc1dsp
