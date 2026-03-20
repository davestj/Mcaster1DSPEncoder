/*
 * Mcaster1DSPEncoder — Effect Version Registry
 * dsp/effect_versions.h
 *
 * Single source of truth for all DSP effect branding, versioning, and
 * release metadata. Every effect in the Mcaster1 effects rack is tracked
 * here with semantic versioning (MAJOR.MINOR.PATCH).
 *
 * Version bumps:
 *   MAJOR — Breaking change to params/API or fundamental algorithm rewrite
 *   MINOR — New features, config options exposed to user, enhancements
 *   PATCH — Bug fixes, performance improvements, internal refactors
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <string>

namespace mc1dsp {

/* ══════════════════════════════════════════════════════════════════════════════
 * Effect version entry — one per DSP unit type
 * ══════════════════════════════════════════════════════════════════════════════ */
struct EffectVersionInfo {
    const char* type_id;       // internal type key ("eq", "compressor", etc.)
    const char* brand_name;    // display name: "Mcaster1 <Name> vX.Y.Z"
    const char* short_name;    // short display name without version
    const char* version;       // semantic version "X.Y.Z"
    int         ver_major;
    int         ver_minor;
    int         ver_patch;
    const char* release_date;  // ISO 8601 "YYYY-MM-DD"
    const char* description;   // what this effect does
    const char* changelog;     // last change summary
    bool        is_stub;       // true if pass-through placeholder
};

/* ══════════════════════════════════════════════════════════════════════════════
 * VERSION REGISTRY — edit this table when releasing, patching, or enhancing
 * any effect. This is the canonical record for traceability.
 *
 * Naming convention: "Mcaster1 <EffectName> v<MAJOR>.<MINOR>.<PATCH>"
 * ══════════════════════════════════════════════════════════════════════════════ */

static const EffectVersionInfo EFFECT_VERSIONS[] = {

    /* ── Parametric EQ ────────────────────────────────────────────────────── */
    {
        /* type_id      */ "eq",
        /* brand_name   */ "Mcaster1 Parametric EQ v1.1.0",
        /* short_name   */ "Mcaster1 Parametric EQ",
        /* version      */ "1.1.0",
        /* ver_major    */ 1,
        /* ver_minor    */ 1,
        /* ver_patch    */ 0,
        /* release_date */ "2026-03-27",
        /* description  */ "10-band parametric equalizer with RBJ biquad IIR filters. "
                           "Supports peaking, low shelf, and high shelf filter types per band.",
        /* changelog    */ "v1.1.0: Mcaster1 branding + version tracking. "
                           "v1.0.0: Initial release with 10-band RBJ biquad, preset system.",
        /* is_stub      */ false
    },

    /* ── Compressor / AGC ─────────────────────────────────────────────────── */
    {
        /* type_id      */ "compressor",
        /* brand_name   */ "Mcaster1 Compressor v1.1.0",
        /* short_name   */ "Mcaster1 Compressor",
        /* version      */ "1.1.0",
        /* ver_major    */ 1,
        /* ver_minor    */ 1,
        /* ver_patch    */ 0,
        /* release_date */ "2026-03-27",
        /* description  */ "Feedforward peak compressor with soft-knee, configurable "
                           "ratio/threshold/attack/release, makeup gain, and hard limiter ceiling.",
        /* changelog    */ "v1.1.0: Mcaster1 branding + version tracking. "
                           "v1.0.0: Initial release with AGC, soft-knee, hard limiter.",
        /* is_stub      */ false
    },

    /* ── Peak Limiter ─────────────────────────────────────────────────────── */
    {
        /* type_id      */ "limiter",
        /* brand_name   */ "Mcaster1 Peak Limiter v1.1.0",
        /* short_name   */ "Mcaster1 Peak Limiter",
        /* version      */ "1.1.0",
        /* ver_major    */ 1,
        /* ver_minor    */ 1,
        /* ver_patch    */ 0,
        /* release_date */ "2026-03-27",
        /* description  */ "Brick-wall peak limiter with configurable ceiling, "
                           "attack/release ballistics, and gain reduction metering.",
        /* changelog    */ "v1.1.0: Mcaster1 branding + version tracking. "
                           "v1.0.0: Initial release with ceiling + ballistics.",
        /* is_stub      */ false
    },

    /* ── Noise Gate ───────────────────────────────────────────────────────── */
    {
        /* type_id      */ "noise_gate",
        /* brand_name   */ "Mcaster1 Noise Gate v1.1.0",
        /* short_name   */ "Mcaster1 Noise Gate",
        /* version      */ "1.1.0",
        /* ver_major    */ 1,
        /* ver_minor    */ 1,
        /* ver_patch    */ 0,
        /* release_date */ "2026-03-27",
        /* description  */ "Downward expander with configurable threshold, range, "
                           "attack/hold/release, and gate-open state metering.",
        /* changelog    */ "v1.1.0: Mcaster1 branding + version tracking. "
                           "v1.0.0: Initial release with hold + range controls.",
        /* is_stub      */ false
    },

    /* ── Reverb ───────────────────────────────────────────────────────────── */
    {
        /* type_id      */ "reverb",
        /* brand_name   */ "Mcaster1 Reverb v0.1.0",
        /* short_name   */ "Mcaster1 Reverb",
        /* version      */ "0.1.0",
        /* ver_major    */ 0,
        /* ver_minor    */ 1,
        /* ver_patch    */ 0,
        /* release_date */ "2026-03-27",
        /* description  */ "Reverb effect — pass-through stub awaiting algorithm implementation. "
                           "Wet/dry mix parameter exposed but not yet applied to audio.",
        /* changelog    */ "v0.1.0: Stub release with mix parameter. Algorithm pending.",
        /* is_stub      */ true
    },

    /* ── Delay ────────────────────────────────────────────────────────────── */
    {
        /* type_id      */ "delay",
        /* brand_name   */ "Mcaster1 Delay v0.1.0",
        /* short_name   */ "Mcaster1 Delay",
        /* version      */ "0.1.0",
        /* ver_major    */ 0,
        /* ver_minor    */ 1,
        /* ver_patch    */ 0,
        /* release_date */ "2026-03-27",
        /* description  */ "Delay/echo effect — pass-through stub awaiting algorithm implementation. "
                           "Time and feedback parameters exposed but not yet applied to audio.",
        /* changelog    */ "v0.1.0: Stub release with time_ms + feedback params. Algorithm pending.",
        /* is_stub      */ true
    },

    /* ── Sidechain Ducker (PTT) ───────────────────────────────────────────── */
    {
        /* type_id      */ "ducker",
        /* brand_name   */ "Mcaster1 Sidechain Ducker v1.1.0",
        /* short_name   */ "Mcaster1 Sidechain Ducker",
        /* version      */ "1.1.0",
        /* ver_major    */ 1,
        /* ver_minor    */ 1,
        /* ver_patch    */ 0,
        /* release_date */ "2026-03-27",
        /* description  */ "Push-to-talk sidechain compressor. Ducks music bus when PTT "
                           "is active or sidechain mic level exceeds threshold. Uses "
                           "crossfader curve algorithms for smooth fade transitions.",
        /* changelog    */ "v1.1.0: Mcaster1 branding + version tracking. "
                           "v1.0.0: Initial release with PTT + sidechain input + curve-based fades.",
        /* is_stub      */ false
    },

    /* ── Dead Air Detector ────────────────────────────────────────────────── */
    {
        /* type_id      */ "dead_air",
        /* brand_name   */ "Mcaster1 Dead Air Detector v1.1.0",
        /* short_name   */ "Mcaster1 Dead Air Detector",
        /* version      */ "1.1.0",
        /* ver_major    */ 1,
        /* ver_minor    */ 1,
        /* ver_patch    */ 0,
        /* release_date */ "2026-03-27",
        /* description  */ "Monitors audio for extended silence. Triggers track skip after "
                           "configurable timeout, then loads fallback playlist if silence persists.",
        /* changelog    */ "v1.1.0: Mcaster1 branding + version tracking. "
                           "v1.0.0: Initial release with RMS detection, skip + fallback triggers.",
        /* is_stub      */ false
    },

    /* ── DJ Crossfader ────────────────────────────────────────────────────── */
    {
        /* type_id      */ "crossfader",
        /* brand_name   */ "Mcaster1 DJ Crossfader v1.1.0",
        /* short_name   */ "Mcaster1 DJ Crossfader",
        /* version      */ "1.1.0",
        /* ver_major    */ 1,
        /* ver_minor    */ 1,
        /* ver_patch    */ 0,
        /* release_date */ "2026-03-27",
        /* description  */ "9-curve dual-deck crossfader with position-to-gain mapping. "
                           "Algorithms: Linear, Constant Power, S-Curve, Exponential, "
                           "Log Taper, Broadcast Blend (EBU), Transform Cut, Hard Cut, "
                           "Dual Open. Ported from MC1AMP.",
        /* changelog    */ "v1.1.0: Mcaster1 branding + version tracking. "
                           "v1.0.0: Initial port from MC1AMP with 9 curve algorithms.",
        /* is_stub      */ false
    },

    /* ── Track Crossfader ─────────────────────────────────────────────────── */
    {
        /* type_id      */ "track_crossfader",
        /* brand_name   */ "Mcaster1 Track Crossfader v1.1.0",
        /* short_name   */ "Mcaster1 Track Crossfader",
        /* version      */ "1.1.0",
        /* ver_major    */ 1,
        /* ver_minor    */ 1,
        /* ver_patch    */ 0,
        /* release_date */ "2026-03-27",
        /* description  */ "Equal-power track-to-track crossfader for seamless transitions "
                           "during playlist playback. Configurable duration in seconds.",
        /* changelog    */ "v1.1.0: Mcaster1 branding + version tracking. "
                           "v1.0.0: Initial release with equal-power curve.",
        /* is_stub      */ false
    },

};

static constexpr int EFFECT_VERSION_COUNT = sizeof(EFFECT_VERSIONS) / sizeof(EFFECT_VERSIONS[0]);

/* ══════════════════════════════════════════════════════════════════════════════
 * Lookup helpers
 * ══════════════════════════════════════════════════════════════════════════════ */

/* We look up version info by type_id. Returns nullptr if not found. */
static inline const EffectVersionInfo* effect_version_by_type(const char* type_id) {
    for (int i = 0; i < EFFECT_VERSION_COUNT; ++i) {
        /* We compare C-strings directly (small table, no performance concern) */
        const char* a = EFFECT_VERSIONS[i].type_id;
        const char* b = type_id;
        while (*a && *a == *b) { ++a; ++b; }
        if (*a == '\0' && *b == '\0') return &EFFECT_VERSIONS[i];
    }
    return nullptr;
}

/* We look up version info by type_id (std::string overload). */
static inline const EffectVersionInfo* effect_version_by_type(const std::string& type_id) {
    return effect_version_by_type(type_id.c_str());
}

} // namespace mc1dsp
