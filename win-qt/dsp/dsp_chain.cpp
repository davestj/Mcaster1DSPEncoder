// dsp/dsp_chain.cpp — Master DSP chain orchestrator
// Phase M6 — Mcaster1DSPEncoder macOS v0.6.0
#include "dsp_chain.h"

namespace mc1dsp {

void DspChain::configure(const DspChainConfig& cfg)
{
    cfg_ = cfg;

    /* 10-band parametric EQ */
    eq_.set_sample_rate(cfg.sample_rate);
    eq_.set_enabled(cfg.eq_enabled && cfg.eq_mode == EqMode::PARAMETRIC_10);
    if (!cfg.eq_preset.empty())
        eq_apply_preset(eq_, cfg.eq_preset);

    /* 31-band graphic EQ */
    eq31_.set_sample_rate(cfg.sample_rate);
    eq31_.set_channels(cfg.channels);
    eq31_.set_enabled(cfg.eq_enabled && cfg.eq_mode == EqMode::GRAPHIC_31);
    if (!cfg.eq31_preset.empty())
        eq31_.apply_preset(cfg.eq31_preset);

    /* Phase M6: Sonic Enhancer */
    sonic_enhancer_.set_enabled(cfg.sonic_enabled);

    /* Phase M8: PTT ducking */
    ptt_duck_.set_sample_rate(cfg.sample_rate);
    ptt_duck_.set_enabled(cfg.ptt_duck_enabled);

    agc_.set_sample_rate(cfg.sample_rate);
    agc_.set_enabled(cfg.agc_enabled);

    /* Phase M8: DBX 286s voice processor */
    dbx_voice_.configure(dbx_voice_.config(), cfg.sample_rate);
    dbx_voice_.set_enabled(cfg.dbx_voice_enabled);

    CrossfaderConfig xf;
    xf.duration_sec = cfg.crossfade_duration;
    xf.enabled      = cfg.crossfader_enabled;
    crossfader_.set_config(xf);
    crossfader_.set_sample_rate(cfg.sample_rate);
}

void DspChain::process(float* pcm, size_t frames)
{
    // EQ → Sonic Enhancer → PTT Duck → AGC → DBX Voice
    // Only one EQ mode is active at a time
    if (cfg_.eq_mode == EqMode::GRAPHIC_31)
        eq31_.process(pcm, frames, cfg_.channels);
    else
        eq_.process(pcm, frames, cfg_.channels);

    sonic_enhancer_.process(pcm, frames, cfg_.channels);
    ptt_duck_.process(pcm, frames, cfg_.channels);
    agc_.process(pcm, frames, cfg_.channels);
    dbx_voice_.process(pcm, frames, cfg_.channels);
}

} // namespace mc1dsp
