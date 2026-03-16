/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * video/transition_engine.cpp — Professional A/B frame blending engine
 *
 * Broadcast-grade video transitions with gamma-correct blending,
 * feathered edges, and 12 transition types.
 *
 * All operations on BGRA pixel data.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "transition_engine.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace mc1 {

/* ── sRGB linearization LUT ─────────────────────────────────────────────
 * Pre-computed 256-entry table: sRGB byte → linear float [0.0, 1.0].
 * Avoids per-pixel pow() calls during blending — critical for 30fps. */
static float g_srgb_lut[256];
static bool  g_lut_ready = false;

static void init_srgb_lut()
{
    if (g_lut_ready) return;
    for (int i = 0; i < 256; ++i) {
        float s = i / 255.0f;
        if (s <= 0.04045f)
            g_srgb_lut[i] = s / 12.92f;
        else
            g_srgb_lut[i] = std::pow((s + 0.055f) / 1.055f, 2.4f);
    }
    g_lut_ready = true;
}

/* ── Static helpers ──────────────────────────────────────────────────── */

float TransitionEngine::srgb_to_linear(uint8_t v)
{
    init_srgb_lut();
    return g_srgb_lut[v];
}

uint8_t TransitionEngine::linear_to_srgb(float v)
{
    if (v <= 0.0f) return 0;
    if (v >= 1.0f) return 255;
    float s;
    if (v <= 0.0031308f)
        s = v * 12.92f;
    else
        s = 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    return static_cast<uint8_t>(std::clamp(s * 255.0f + 0.5f, 0.0f, 255.0f));
}

float TransitionEngine::ease_in_out(float t)
{
    /* Cubic Hermite smoothstep — natural acceleration/deceleration */
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/* ── Frame push ──────────────────────────────────────────────────────── */

void TransitionEngine::set_duration(float sec)
{
    duration_sec_ = std::clamp(sec, 0.1f, 5.0f);
}

void TransitionEngine::push_program_frame(const uint8_t* bgra, int w, int h, int stride)
{
    std::lock_guard<std::mutex> lock(mtx_);
    frame_w_ = w;
    frame_h_ = h;
    frame_stride_ = stride;
    size_t sz = static_cast<size_t>(stride) * static_cast<size_t>(h);
    if (frame_a_.size() != sz) frame_a_.resize(sz);
    std::memcpy(frame_a_.data(), bgra, sz);
}

void TransitionEngine::push_preview_frame(const uint8_t* bgra, int w, int h, int stride)
{
    std::lock_guard<std::mutex> lock(mtx_);
    size_t sz = static_cast<size_t>(stride) * static_cast<size_t>(h);
    if (frame_b_.size() != sz) frame_b_.resize(sz);
    std::memcpy(frame_b_.data(), bgra, sz);
    (void)w; (void)h;
}

/* ── Transition lifecycle ────────────────────────────────────────────── */

void TransitionEngine::begin()
{
    init_srgb_lut();
    transitioning_ = true;
    progress_ = 0.0f;
    elapsed_ = 0.0;
}

void TransitionEngine::cancel()
{
    transitioning_ = false;
    progress_ = 0.0f;
    elapsed_ = 0.0;
}

bool TransitionEngine::tick(double elapsed_sec)
{
    if (!transitioning_) return false;

    if (type_ == Type::CUT) {
        progress_ = 1.0f;
        transitioning_ = false;
        return false;
    }

    elapsed_ += elapsed_sec;
    progress_ = std::clamp(static_cast<float>(elapsed_ / duration_sec_), 0.0f, 1.0f);

    if (progress_ >= 1.0f) {
        transitioning_ = false;
        return false;
    }
    return true;
}

/* ── Render dispatch ─────────────────────────────────────────────────── */

bool TransitionEngine::render(uint8_t* out_bgra, int w, int h, int stride)
{
    std::lock_guard<std::mutex> lock(mtx_);

    if (frame_a_.empty()) return false;

    size_t out_sz = static_cast<size_t>(stride) * static_cast<size_t>(h);

    /* Not transitioning — show program (A) */
    if (!transitioning_ && progress_ < 1.0f) {
        if (frame_a_.size() >= out_sz) {
            std::memcpy(out_bgra, frame_a_.data(), out_sz);
            return true;
        }
        return false;
    }

    /* Transition complete — show preview (B) as new program */
    if (progress_ >= 1.0f && !transitioning_) {
        if (!frame_b_.empty() && frame_b_.size() >= out_sz) {
            std::memcpy(out_bgra, frame_b_.data(), out_sz);
            return true;
        }
        return false;
    }

    /* Mid-transition — apply easing and blend */
    const uint8_t* a = frame_a_.data();
    const uint8_t* b = frame_b_.empty() ? frame_a_.data() : frame_b_.data();
    float t = ease_in_out(progress_);

    switch (type_) {
    case Type::CUT:
        std::memcpy(out_bgra, b, out_sz);
        break;
    case Type::CROSSFADE:
        blend_crossfade(out_bgra, a, b, w, h, stride, t);
        break;
    case Type::FADE_TO_BLACK:
        blend_dip_to_color(out_bgra, a, b, w, h, stride, t, 0, 0, 0);
        break;
    case Type::DIP_TO_WHITE:
        blend_dip_to_color(out_bgra, a, b, w, h, stride, t, 255, 255, 255);
        break;
    case Type::WIPE_LEFT:
        blend_wipe(out_bgra, a, b, w, h, stride, t, true, true);
        break;
    case Type::WIPE_RIGHT:
        blend_wipe(out_bgra, a, b, w, h, stride, t, true, false);
        break;
    case Type::WIPE_UP:
        blend_wipe(out_bgra, a, b, w, h, stride, t, false, true);
        break;
    case Type::WIPE_DOWN:
        blend_wipe(out_bgra, a, b, w, h, stride, t, false, false);
        break;
    case Type::PUSH_LEFT:
        blend_push(out_bgra, a, b, w, h, stride, t, true);
        break;
    case Type::PUSH_RIGHT:
        blend_push(out_bgra, a, b, w, h, stride, t, false);
        break;
    case Type::IRIS_CIRCLE:
        blend_iris_circle(out_bgra, a, b, w, h, stride, t);
        break;
    case Type::DISSOLVE:
        blend_dissolve(out_bgra, a, b, w, h, stride, t);
        break;
    }

    return true;
}

/* ── Gamma-correct crossfade ─────────────────────────────────────────
 * Linearize sRGB → blend in linear light → re-encode to sRGB.
 * Eliminates the grainy/dark midpoint artifact of naive sRGB lerp. */

void TransitionEngine::blend_crossfade(uint8_t* out, const uint8_t* a, const uint8_t* b,
                                       int w, int h, int stride, float t)
{
    float inv_t = 1.0f - t;

    for (int y = 0; y < h; ++y) {
        const uint8_t* ra = a + y * stride;
        const uint8_t* rb = b + y * stride;
        uint8_t* ro = out + y * stride;

        for (int x = 0; x < w * 4; x += 4) {
            /* Linearize B, G, R channels */
            float lb = g_srgb_lut[ra[x + 0]] * inv_t + g_srgb_lut[rb[x + 0]] * t;
            float lg = g_srgb_lut[ra[x + 1]] * inv_t + g_srgb_lut[rb[x + 1]] * t;
            float lr = g_srgb_lut[ra[x + 2]] * inv_t + g_srgb_lut[rb[x + 2]] * t;

            /* Re-encode to sRGB */
            ro[x + 0] = linear_to_srgb(lb);
            ro[x + 1] = linear_to_srgb(lg);
            ro[x + 2] = linear_to_srgb(lr);
            ro[x + 3] = 255; /* alpha */
        }
    }
}

/* ── Dip to color (fade-to-black / dip-to-white) ────────────────────
 * First half: A → solid color. Second half: solid color → B.
 * Gamma-correct blending with the dip color. */

void TransitionEngine::blend_dip_to_color(uint8_t* out, const uint8_t* a, const uint8_t* b,
                                          int w, int h, int stride, float t,
                                          uint8_t cr, uint8_t cg, uint8_t cb)
{
    float color_b = g_srgb_lut[cb]; /* BGRA order: B=0, G=1, R=2 */
    float color_g = g_srgb_lut[cg];
    float color_r = g_srgb_lut[cr];

    const uint8_t* src;
    float blend;

    if (t < 0.5f) {
        /* A → color: blend = 0.0 (all A) → 1.0 (all color) */
        src = a;
        blend = t * 2.0f;
    } else {
        /* color → B: blend = 1.0 (all color) → 0.0 (all B) */
        src = b;
        blend = (1.0f - t) * 2.0f;
    }

    float inv_blend = 1.0f - blend;

    for (int y = 0; y < h; ++y) {
        const uint8_t* rs = src + y * stride;
        uint8_t* ro = out + y * stride;

        for (int x = 0; x < w * 4; x += 4) {
            float lb = g_srgb_lut[rs[x + 0]] * inv_blend + color_b * blend;
            float lg = g_srgb_lut[rs[x + 1]] * inv_blend + color_g * blend;
            float lr = g_srgb_lut[rs[x + 2]] * inv_blend + color_r * blend;

            ro[x + 0] = linear_to_srgb(lb);
            ro[x + 1] = linear_to_srgb(lg);
            ro[x + 2] = linear_to_srgb(lr);
            ro[x + 3] = 255;
        }
    }
}

/* ── Feathered wipe ─────────────────────────────────────────────────
 * Wipe with a soft gradient feather zone (kFeatherWidth pixels).
 * The feather blends A and B at the transition edge for a smooth reveal.
 *
 * horizontal=true: wipe along X axis. forward=true: left-to-right.
 * horizontal=false: wipe along Y axis. forward=true: bottom-to-top. */

void TransitionEngine::blend_wipe(uint8_t* out, const uint8_t* a, const uint8_t* b,
                                  int w, int h, int stride, float t,
                                  bool horizontal, bool forward)
{
    int dim = horizontal ? w : h;
    float edge = t * static_cast<float>(dim);
    float half_feather = static_cast<float>(kFeatherWidth) * 0.5f;

    for (int y = 0; y < h; ++y) {
        const uint8_t* ra = a + y * stride;
        const uint8_t* rb = b + y * stride;
        uint8_t* ro = out + y * stride;

        for (int x = 0; x < w; ++x) {
            int pos = horizontal ? x : y;
            if (!forward) pos = dim - 1 - pos;

            float dist = static_cast<float>(pos) - edge;
            /* alpha: -half_feather → 0.0 (fully B), +half_feather → 1.0 (fully A) */
            float alpha;
            if (half_feather > 0.5f)
                alpha = std::clamp((dist + half_feather) / (2.0f * half_feather), 0.0f, 1.0f);
            else
                alpha = (dist >= 0.0f) ? 1.0f : 0.0f;

            /* Invert: we want B to replace A as t grows */
            float mix_b = 1.0f - alpha;
            float mix_a = alpha;

            int px = x * 4;
            if (mix_b <= 0.0f) {
                /* Fully A — no blend needed */
                ro[px + 0] = ra[px + 0];
                ro[px + 1] = ra[px + 1];
                ro[px + 2] = ra[px + 2];
                ro[px + 3] = 255;
            } else if (mix_a <= 0.0f) {
                /* Fully B — no blend needed */
                ro[px + 0] = rb[px + 0];
                ro[px + 1] = rb[px + 1];
                ro[px + 2] = rb[px + 2];
                ro[px + 3] = 255;
            } else {
                /* Feather zone — gamma-correct blend */
                float lb = g_srgb_lut[ra[px + 0]] * mix_a + g_srgb_lut[rb[px + 0]] * mix_b;
                float lg = g_srgb_lut[ra[px + 1]] * mix_a + g_srgb_lut[rb[px + 1]] * mix_b;
                float lr = g_srgb_lut[ra[px + 2]] * mix_a + g_srgb_lut[rb[px + 2]] * mix_b;
                ro[px + 0] = linear_to_srgb(lb);
                ro[px + 1] = linear_to_srgb(lg);
                ro[px + 2] = linear_to_srgb(lr);
                ro[px + 3] = 255;
            }
        }
    }
}

/* ── Push transition ────────────────────────────────────────────────
 * B pushes A off-screen. Both frames shift simultaneously:
 * left=true: A slides left, B enters from right.
 * left=false: A slides right, B enters from left. */

void TransitionEngine::blend_push(uint8_t* out, const uint8_t* a, const uint8_t* b,
                                  int w, int h, int stride, float t, bool left)
{
    int shift = static_cast<int>(t * w);

    for (int y = 0; y < h; ++y) {
        const uint8_t* ra = a + y * stride;
        const uint8_t* rb = b + y * stride;
        uint8_t* ro = out + y * stride;

        for (int x = 0; x < w; ++x) {
            int src_x;
            const uint8_t* src;

            if (left) {
                /* A slides left: pixel at x comes from A at (x + shift) */
                src_x = x + shift;
                if (src_x < w) {
                    src = ra;
                } else {
                    /* B enters from right: pixel from B at (src_x - w) */
                    src_x -= w;
                    src = rb;
                }
            } else {
                /* A slides right: pixel at x comes from A at (x - shift) */
                src_x = x - shift;
                if (src_x >= 0) {
                    src = ra;
                } else {
                    /* B enters from left: pixel from B at (w + src_x) */
                    src_x += w;
                    src = rb;
                }
            }

            src_x = std::clamp(src_x, 0, w - 1);
            int px_out = x * 4;
            int px_src = src_x * 4;
            ro[px_out + 0] = src[px_src + 0];
            ro[px_out + 1] = src[px_src + 1];
            ro[px_out + 2] = src[px_src + 2];
            ro[px_out + 3] = 255;
        }
    }
}

/* ── Iris circle ────────────────────────────────────────────────────
 * Circular reveal expanding from center with feathered edge.
 * At t=0 all A. At t=1 all B. The circle radius grows from 0
 * to the corner distance (full frame diagonal / 2). */

void TransitionEngine::blend_iris_circle(uint8_t* out, const uint8_t* a, const uint8_t* b,
                                         int w, int h, int stride, float t)
{
    float cx = w * 0.5f;
    float cy = h * 0.5f;
    /* Max radius = distance from center to corner */
    float max_r = std::sqrt(cx * cx + cy * cy);
    float radius = t * max_r;
    float feather = static_cast<float>(kFeatherWidth);

    for (int y = 0; y < h; ++y) {
        const uint8_t* ra = a + y * stride;
        const uint8_t* rb = b + y * stride;
        uint8_t* ro = out + y * stride;

        float dy = static_cast<float>(y) - cy;
        float dy2 = dy * dy;

        for (int x = 0; x < w; ++x) {
            float dx = static_cast<float>(x) - cx;
            float dist = std::sqrt(dx * dx + dy2);

            /* mix_b: 0 = fully A, 1 = fully B */
            float mix_b;
            if (feather > 0.5f) {
                mix_b = std::clamp((radius - dist + feather * 0.5f) / feather, 0.0f, 1.0f);
            } else {
                mix_b = (dist <= radius) ? 1.0f : 0.0f;
            }

            int px = x * 4;
            if (mix_b <= 0.0f) {
                ro[px + 0] = ra[px + 0];
                ro[px + 1] = ra[px + 1];
                ro[px + 2] = ra[px + 2];
            } else if (mix_b >= 1.0f) {
                ro[px + 0] = rb[px + 0];
                ro[px + 1] = rb[px + 1];
                ro[px + 2] = rb[px + 2];
            } else {
                float mix_a = 1.0f - mix_b;
                float lb = g_srgb_lut[ra[px + 0]] * mix_a + g_srgb_lut[rb[px + 0]] * mix_b;
                float lg = g_srgb_lut[ra[px + 1]] * mix_a + g_srgb_lut[rb[px + 1]] * mix_b;
                float lr = g_srgb_lut[ra[px + 2]] * mix_a + g_srgb_lut[rb[px + 2]] * mix_b;
                ro[px + 0] = linear_to_srgb(lb);
                ro[px + 1] = linear_to_srgb(lg);
                ro[px + 2] = linear_to_srgb(lr);
            }
            ro[px + 3] = 255;
        }
    }
}

/* ── Dissolve pattern ───────────────────────────────────────────────
 * Pre-computed per-pixel random threshold. As t sweeps 0→1,
 * pixels whose threshold < t flip from A to B. The hash function
 * produces an organic, non-repeating pattern. */

void TransitionEngine::ensure_dissolve_pattern(int w, int h)
{
    if (dissolve_w_ == w && dissolve_h_ == h && !dissolve_threshold_.empty())
        return;

    dissolve_w_ = w;
    dissolve_h_ = h;
    dissolve_threshold_.resize(static_cast<size_t>(w) * static_cast<size_t>(h));

    /* FNV-1a inspired hash for even distribution */
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint32_t hash = 2166136261u;
            hash ^= static_cast<uint32_t>(x);
            hash *= 16777619u;
            hash ^= static_cast<uint32_t>(y);
            hash *= 16777619u;
            hash ^= static_cast<uint32_t>(x * 31 + y * 37);
            hash *= 16777619u;
            dissolve_threshold_[y * w + x] = static_cast<float>(hash & 0xFFFFu) / 65535.0f;
        }
    }
}

void TransitionEngine::blend_dissolve(uint8_t* out, const uint8_t* a, const uint8_t* b,
                                      int w, int h, int stride, float t)
{
    ensure_dissolve_pattern(w, h);

    for (int y = 0; y < h; ++y) {
        const uint8_t* ra = a + y * stride;
        const uint8_t* rb = b + y * stride;
        uint8_t* ro = out + y * stride;
        const float* thr = &dissolve_threshold_[y * w];

        for (int x = 0; x < w; ++x) {
            int px = x * 4;
            if (thr[x] < t) {
                /* This pixel has dissolved — show B */
                ro[px + 0] = rb[px + 0];
                ro[px + 1] = rb[px + 1];
                ro[px + 2] = rb[px + 2];
            } else {
                /* Not yet dissolved — show A */
                ro[px + 0] = ra[px + 0];
                ro[px + 1] = ra[px + 1];
                ro[px + 2] = ra[px + 2];
            }
            ro[px + 3] = 255;
        }
    }
}

} // namespace mc1
