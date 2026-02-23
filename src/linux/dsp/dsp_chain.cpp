// dsp/dsp_chain.cpp — Master DSP chain orchestrator
// Phase 4 — Mcaster1DSPEncoder Linux v1.3.0
#include "dsp_chain.h"

namespace mc1dsp {

void DspChain::configure(const DspChainConfig& cfg)
{
    cfg_ = cfg;

    eq_.set_sample_rate(cfg.sample_rate);
    eq_.set_enabled(cfg.eq_enabled);
    if (!cfg.eq_preset.empty())
        eq_apply_preset(eq_, cfg.eq_preset);

    agc_.set_sample_rate(cfg.sample_rate);
    agc_.set_enabled(cfg.agc_enabled);

    CrossfaderConfig xf;
    xf.duration_sec = cfg.crossfade_duration;
    xf.enabled      = cfg.crossfader_enabled;
    crossfader_.set_config(xf);
    crossfader_.set_sample_rate(cfg.sample_rate);
}

void DspChain::process(float* pcm, size_t frames)
{
    // EQ → AGC (crossfader applied externally by EncoderSlot at track boundaries)
    eq_.process(pcm, frames, cfg_.channels);
    agc_.process(pcm, frames, cfg_.channels);
}

} // namespace mc1dsp
