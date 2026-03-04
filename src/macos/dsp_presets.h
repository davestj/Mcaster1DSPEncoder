/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * dsp_presets.h — Broadcast-grade AGC presets for audio processing
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_DSP_PRESETS_H
#define MC1_DSP_PRESETS_H

#include "dsp/agc.h"
#include <string>
#include <vector>

namespace mc1 {

struct AgcPreset {
    std::string      name;
    std::string      description;
    mc1dsp::AgcConfig config;
};

/* Returns the built-in broadcast-grade AGC presets */
const std::vector<AgcPreset>& agc_presets();

/* Find a preset by name (case-insensitive). Returns nullptr if not found. */
const AgcPreset* agc_preset_by_name(const std::string& name);

} // namespace mc1

#endif // MC1_DSP_PRESETS_H
