/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/transition_engine.cpp — A/B frame blending engine
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "transition_engine.h"
#include <algorithm>
#include <cstring>

namespace mc1 {

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
    /* Resize to match program dimensions if needed */
    size_t sz = static_cast<size_t>(stride) * static_cast<size_t>(h);
    if (frame_b_.size() != sz) frame_b_.resize(sz);
    std::memcpy(frame_b_.data(), bgra, sz);
    (void)w; (void)h; /* dimensions should match frame_a_ */
}

void TransitionEngine::begin()
{
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
        /* Instant cut — jump to 1.0 */
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

bool TransitionEngine::render(uint8_t* out_bgra, int w, int h, int stride)
{
    std::lock_guard<std::mutex> lock(mtx_);

    if (frame_a_.empty()) return false;

    size_t out_sz = static_cast<size_t>(stride) * static_cast<size_t>(h);

    if (!transitioning_ && progress_ < 1.0f) {
        /* Not transitioning — show program (A) */
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

    /* Mid-transition — blend */
    const uint8_t* a = frame_a_.data();
    const uint8_t* b = frame_b_.empty() ? frame_a_.data() : frame_b_.data();

    switch (type_) {
    case Type::CUT:
        std::memcpy(out_bgra, b, out_sz);
        break;
    case Type::CROSSFADE:
        blend_crossfade(out_bgra, a, b, w, h, stride, progress_);
        break;
    case Type::FADE_TO_BLACK:
        blend_fade_to_black(out_bgra, a, b, w, h, stride, progress_);
        break;
    case Type::WIPE_LEFT:
        blend_wipe_horizontal(out_bgra, a, b, w, h, stride, progress_, true);
        break;
    case Type::WIPE_RIGHT:
        blend_wipe_horizontal(out_bgra, a, b, w, h, stride, progress_, false);
        break;
    }

    return true;
}

void TransitionEngine::blend_crossfade(uint8_t* out, const uint8_t* a, const uint8_t* b,
                                        int w, int h, int stride, float t)
{
    float inv_t = 1.0f - t;
    for (int y = 0; y < h; ++y) {
        const uint8_t* ra = a + y * stride;
        const uint8_t* rb = b + y * stride;
        uint8_t* ro = out + y * stride;
        for (int x = 0; x < w * 4; ++x) {
            ro[x] = static_cast<uint8_t>(ra[x] * inv_t + rb[x] * t);
        }
    }
}

void TransitionEngine::blend_fade_to_black(uint8_t* out, const uint8_t* a, const uint8_t* b,
                                            int w, int h, int stride, float t)
{
    /* First half: A → black. Second half: black → B */
    if (t < 0.5f) {
        float fade = 1.0f - (t * 2.0f); /* 1.0 → 0.0 */
        for (int y = 0; y < h; ++y) {
            const uint8_t* ra = a + y * stride;
            uint8_t* ro = out + y * stride;
            for (int x = 0; x < w * 4; x += 4) {
                ro[x + 0] = static_cast<uint8_t>(ra[x + 0] * fade); /* B */
                ro[x + 1] = static_cast<uint8_t>(ra[x + 1] * fade); /* G */
                ro[x + 2] = static_cast<uint8_t>(ra[x + 2] * fade); /* R */
                ro[x + 3] = 255; /* A */
            }
        }
    } else {
        float fade = (t - 0.5f) * 2.0f; /* 0.0 → 1.0 */
        for (int y = 0; y < h; ++y) {
            const uint8_t* rb = b + y * stride;
            uint8_t* ro = out + y * stride;
            for (int x = 0; x < w * 4; x += 4) {
                ro[x + 0] = static_cast<uint8_t>(rb[x + 0] * fade); /* B */
                ro[x + 1] = static_cast<uint8_t>(rb[x + 1] * fade); /* G */
                ro[x + 2] = static_cast<uint8_t>(rb[x + 2] * fade); /* R */
                ro[x + 3] = 255; /* A */
            }
        }
    }
}

void TransitionEngine::blend_wipe_horizontal(uint8_t* out, const uint8_t* a, const uint8_t* b,
                                              int w, int h, int stride, float t,
                                              bool left_to_right)
{
    int split_x = static_cast<int>(t * w);

    for (int y = 0; y < h; ++y) {
        const uint8_t* ra = a + y * stride;
        const uint8_t* rb = b + y * stride;
        uint8_t* ro = out + y * stride;

        if (left_to_right) {
            /* B on left (already wiped), A on right (yet to wipe) */
            std::memcpy(ro, rb, split_x * 4);
            std::memcpy(ro + split_x * 4, ra + split_x * 4, (w - split_x) * 4);
        } else {
            /* A on left, B on right */
            int right_start = w - split_x;
            std::memcpy(ro, ra, right_start * 4);
            std::memcpy(ro + right_start * 4, rb + right_start * 4, split_x * 4);
        }
    }
}

} // namespace mc1
