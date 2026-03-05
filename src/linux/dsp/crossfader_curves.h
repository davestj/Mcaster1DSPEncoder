/*
 * Mcaster1DSPEncoder — DJ Crossfader Curve Mathematics
 * dsp/crossfader_curves.h
 *
 * Pure C++ crossfader curve mathematics — header-only, no dependencies beyond <cmath>.
 * All curves map crossfader position [0.0, 1.0] → gain pair {A, B}.
 *
 * position 0.0 = full Deck A, 1.0 = full Deck B, 0.5 = equal blend.
 *
 * Ported from Mcaster1AMP's dsp_mc1crossfader plugin (crossfader_curves.h).
 * 9 curve algorithms — production-proven in MC1AMP desktop player.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once
#include <cmath>
#include <algorithm>

namespace mc1xf {

/* ── Curve IDs ──────────────────────────────────────────────────────────────── */

enum class Curve : int {
    Linear          = 0,  // Simple linear taper — A=1-x, B=x
    ConstantPower   = 1,  // sin/cos taper (-3dB at center) — DJ industry standard
    SCurve          = 2,  // Cubic smoothstep — slow at extremes, fast in middle
    Exponential     = 3,  // Squared law (6dB/oct) — aggressive hard cut feel
    LogTaper        = 4,  // 1.5-power law — perceptually linear loudness
    BroadcastBlend  = 5,  // EBU-style equal loudness across the blend
    TransformCut    = 6,  // Instant cut at center — battle/scratch DJ style
    HardCut         = 7,  // Sharp 10% overlap — live radio hand-off
    PioneerStyle    = 8,  // Both decks open at extremes, fade only opposite deck

    COUNT           = 9
};

static const char* CURVE_NAMES[] = {
    "Linear",
    "Constant Power",
    "S-Curve",
    "Exponential",
    "Log Taper",
    "Broadcast Blend",
    "Transform Cut",
    "Hard Cut",
    "Pioneer Style",
};

static const char* CURVE_DESCRIPTIONS[] = {
    "Simple linear gain — A=1-x, B=x. Equal attenuation both sides.",
    "sin/cos taper — standard DJ crossfader. -3 dB at center, smooth blend.",
    "Cubic smooth-step — slow at extremes, fast through the center.",
    "Squared law (6 dB/octave) — more aggressive than constant power.",
    "1.5-power law — perceptually linear loudness (matches hearing curve).",
    "EBU broadcast blend — maintains equal loudness across the entire range.",
    "Instant cut at 50% — battle DJ and scratching. Both fully open until center.",
    "Sharp 10% overlap — clean live radio hand-off, no pop, no dropout.",
    "Pioneer DJM style — both decks fully open, only fades the opposite deck.",
};

/* ── Gain pair ──────────────────────────────────────────────────────────────── */

struct Gains {
    float a;  // Deck A gain [0.0, 1.0]
    float b;  // Deck B gain [0.0, 1.0]

    float a_db() const { return a > 1e-6f ? 20.0f * std::log10(a) : -96.0f; }
    float b_db() const { return b > 1e-6f ? 20.0f * std::log10(b) : -96.0f; }
};

/* ── Core computation ───────────────────────────────────────────────────────── */

inline Gains computeGains(float pos, Curve curve)
{
    pos = std::clamp(pos, 0.0f, 1.0f);
    static constexpr float kPi2 = 1.5707963267948966f;  // π/2

    float a, b;
    switch (curve) {

        case Curve::Linear:
            a = 1.0f - pos;
            b = pos;
            break;

        case Curve::ConstantPower:
            a = std::cos(pos * kPi2);
            b = std::sin(pos * kPi2);
            break;

        case Curve::SCurve: {
            const float t = pos * pos * (3.0f - 2.0f * pos);
            b = t;
            a = 1.0f - t;
        } break;

        case Curve::Exponential:
            a = (1.0f - pos) * (1.0f - pos);
            b = pos * pos;
            break;

        case Curve::LogTaper:
            a = std::pow(1.0f - pos, 1.5f);
            b = std::pow(pos, 1.5f);
            break;

        case Curve::BroadcastBlend:
            a = std::cos(pos * kPi2) * 0.7071f + (1.0f - pos) * 0.2929f;
            b = std::sin(pos * kPi2) * 0.7071f + pos * 0.2929f;
            break;

        case Curve::TransformCut:
            a = (pos < 0.5f) ? 1.0f : 0.0f;
            b = (pos >= 0.5f) ? 1.0f : 0.0f;
            break;

        case Curve::HardCut: {
            const float overlap = 0.10f;
            const float lo = 0.5f - overlap * 0.5f;
            const float hi = 0.5f + overlap * 0.5f;
            a = (pos < lo) ? 1.0f : (pos > hi) ? 0.0f
                : std::cos(((pos - lo) / overlap) * kPi2);
            b = (pos > hi) ? 1.0f : (pos < lo) ? 0.0f
                : std::sin(((pos - lo) / overlap) * kPi2);
        } break;

        case Curve::PioneerStyle:
            a = (pos < 0.5f) ? 1.0f : std::cos((pos - 0.5f) * kPi2 * 2.0f);
            b = (pos > 0.5f) ? 1.0f : std::cos((0.5f - pos) * kPi2 * 2.0f);
            break;

        default:
            a = 1.0f - pos;
            b = pos;
            break;
    }
    return {a, b};
}

/* We sample the A gain curve at n evenly-spaced points (for graph rendering) */
inline void sampleCurveA(Curve curve, float* out, int n)
{
    for (int i = 0; i < n; ++i)
        out[i] = computeGains(static_cast<float>(i) / (n - 1), curve).a;
}

inline void sampleCurveB(Curve curve, float* out, int n)
{
    for (int i = 0; i < n; ++i)
        out[i] = computeGains(static_cast<float>(i) / (n - 1), curve).b;
}

} // namespace mc1xf
