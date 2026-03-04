/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * global_config_manager.h — YAML persistence for global settings & DSP rack defaults
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_GLOBAL_CONFIG_MANAGER_H
#define MC1_GLOBAL_CONFIG_MANAGER_H

#include "config_types.h"

namespace mc1 {

/* ── DSP Rack Unit IDs ──────────────────────────────────────────────── */

enum class DspUnitId : int {
    AGC = 0,
    SONIC_ENHANCER,
    EQ10,
    EQ31,
    PTT_DUCK,
    DBX_VOICE,
    COUNT
};

/* ── Global DSP Rack Enable State ───────────────────────────────────── */

struct DspRackEnableState {
    bool eq10_enabled      = false;
    bool eq31_enabled      = false;
    bool sonic_enabled     = false;
    bool ptt_duck_enabled  = false;
    bool agc_enabled       = false;
    bool dbx_voice_enabled = false;
};

class GlobalConfigManager {
public:
    /* Returns ~/Library/Application Support/Mcaster1/Mcaster1 DSP Encoder/ */
    static std::string config_dir();

    /* ── Global application settings ─────────────────────────────────── */
    static GlobalConfig load_global();
    static std::string  save_global(const GlobalConfig& cfg);

    /* ── DSP effects rack defaults (per-unit YAML files) ─────────────── */
    static DspRackDefaults load_dsp_defaults();
    static std::string     save_dsp_defaults(const DspRackDefaults& dsp);

    /* Per-unit save/load — returns true on success */
    static bool save_dsp_unit(DspUnitId unit, const DspRackDefaults& dsp);
    static DspRackDefaults load_all_dsp_units();

    /* ── Rack enable state (dsp_rack_state.yaml) ─────────────────────── */
    static DspRackEnableState load_rack_enable_state();
    static bool save_rack_enable_state(const DspRackEnableState& state);

    /* Per-unit YAML filenames */
    static const char* unit_filename(DspUnitId unit);

private:
    static constexpr const char* kGlobalFilename  = "mcaster1dspencoder_global.yaml";
    static constexpr const char* kDspFilename      = "dsp_effects_rack.yaml"; /* legacy monolithic */
    static constexpr const char* kRackStateFilename = "dsp_rack_state.yaml";
};

} // namespace mc1

#endif // MC1_GLOBAL_CONFIG_MANAGER_H
