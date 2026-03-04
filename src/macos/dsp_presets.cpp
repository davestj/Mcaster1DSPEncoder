/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * dsp_presets.cpp — Broadcast-grade AGC presets
 *
 * Professional broadcast AGC settings derived from industry-standard
 * processing chains (Orban Optimod, Omnia, Wheatstone AirAura concepts).
 *
 * Each preset is tuned for its target format with settings that sound
 * good out of the box for internet radio and terrestrial broadcast.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dsp_presets.h"
#include <algorithm>
#include <cctype>

namespace mc1 {

static std::string to_lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

/*
 * Preset parameter order in AgcConfig initializer:
 *   input_gain_db, gate_threshold_db,
 *   threshold_db, ratio, attack_ms, release_ms, knee_db, makeup_gain_db,
 *   limiter_db, enabled
 */

static const std::vector<AgcPreset> g_presets = {
    {
        "Broadcast Standard",
        "General-purpose broadcast AGC. Safe for all music and talk formats. "
        "Balanced compression with moderate attack/release. Industry default.",
        { 0.0f, -60.0f,  -18.0f, 4.0f, 10.0f, 200.0f, 6.0f, 4.0f, -1.0f, true }
    },
    {
        "Talk Radio",
        "Optimized for voice: fast attack controls plosives and sibilance, "
        "moderate release keeps speech natural. Tight gate removes studio noise.",
        { 0.0f, -50.0f,  -12.0f, 6.0f, 3.0f, 80.0f, 4.0f, 6.0f, -0.5f, true }
    },
    {
        "Classic Rock",
        "Medium compression with warm character. Longer attack lets guitar transients "
        "punch through. Wide knee for smooth gain transitions. Vintage broadcast feel.",
        { 0.0f, -60.0f,  -16.0f, 3.5f, 12.0f, 250.0f, 8.0f, 5.0f, -1.0f, true }
    },
    {
        "Country",
        "Light-medium compression preserving acoustic dynamics. Slow attack respects "
        "steel guitar and fiddle transients. Natural, unprocessed sound.",
        { 0.0f, -60.0f,  -18.0f, 2.5f, 20.0f, 300.0f, 8.0f, 3.0f, -1.0f, true }
    },
    {
        "Jazz / Blues",
        "Minimal compression for maximum dynamic range. Very slow attack/release "
        "preserves instrument separation and room ambience. Audiophile grade.",
        { 0.0f, -60.0f,  -24.0f, 2.0f, 25.0f, 400.0f, 10.0f, 2.0f, -1.5f, true }
    },
    {
        "Pop / Top 40",
        "Competitive loudness for chart music. Medium-fast attack with moderate "
        "release. Higher makeup gain for consistent perceived volume.",
        { 0.0f, -55.0f,  -15.0f, 4.5f, 6.0f, 120.0f, 6.0f, 7.0f, -0.5f, true }
    },
    {
        "EDM / Dance",
        "Heavy compression for maximum energy and loudness. Ultra-fast attack "
        "and release. Higher input gain drives harder into the compressor. "
        "Aggressive limiter ceiling for hot output.",
        { 2.0f, -50.0f,  -12.0f, 6.0f, 2.0f, 60.0f, 4.0f, 10.0f, -0.3f, true }
    },
    {
        "Hip Hop / R&B",
        "Punchy compression emphasizing bass weight and vocal presence. "
        "Fast attack tames peaks while moderate release retains groove.",
        { 1.0f, -52.0f,  -14.0f, 5.0f, 4.0f, 100.0f, 5.0f, 8.0f, -0.5f, true }
    },
    {
        "Classical",
        "Ultra-light compression for maximum dynamic fidelity. Very wide knee "
        "ensures imperceptible gain changes. Preserves concert hall dynamics.",
        { 0.0f, -60.0f,  -28.0f, 1.5f, 30.0f, 600.0f, 12.0f, 1.0f, -2.0f, true }
    },
    {
        "Podcast / Audiobook",
        "Natural voice leveling with gentle compression. Moderate gate removes "
        "room tone. Slow release prevents pumping between sentences.",
        { 0.0f, -48.0f,  -20.0f, 3.0f, 10.0f, 300.0f, 6.0f, 3.0f, -1.0f, true }
    },
    {
        "Sports / Live Events",
        "Maximum loudness for crowd noise and commentary. Ultra-fast attack/release "
        "with heavy ratio. Tight gate cuts ambient bleed. Hot limiter output.",
        { 2.0f, -45.0f,  -10.0f, 8.0f, 1.0f, 50.0f, 3.0f, 10.0f, -0.3f, true }
    },
    {
        "Smooth / Easy Listening",
        "Gentle, non-aggressive compression for relaxed formats. Wide knee and slow "
        "time constants. Minimal makeup gain preserves natural dynamics.",
        { 0.0f, -60.0f,  -22.0f, 2.0f, 20.0f, 350.0f, 10.0f, 2.0f, -1.5f, true }
    },
    {
        "Modern Rock",
        "Forward midrange with tight bass control. Medium compression with moderate "
        "attack allows pick transients. Higher makeup for presence.",
        { 1.0f, -55.0f,  -14.0f, 4.0f, 8.0f, 180.0f, 6.0f, 6.0f, -0.5f, true }
    }
};

const std::vector<AgcPreset>& agc_presets()
{
    return g_presets;
}

const AgcPreset* agc_preset_by_name(const std::string& name)
{
    std::string lower_name = to_lower(name);
    for (const auto& p : g_presets) {
        if (to_lower(p.name) == lower_name)
            return &p;
    }
    return nullptr;
}

} // namespace mc1
