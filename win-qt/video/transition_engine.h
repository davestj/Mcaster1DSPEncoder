/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/transition_engine.h — A/B frame blending engine for video transitions
 *
 * Supports: Cut, Crossfade, Fade to Black, Wipe Left, Wipe Right.
 * All operations on BGRA pixel data.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_TRANSITION_ENGINE_H
#define MC1_TRANSITION_ENGINE_H

#include <cstdint>
#include <mutex>
#include <vector>

namespace mc1 {

class TransitionEngine {
public:
    enum class Type { CUT, CROSSFADE, FADE_TO_BLACK, WIPE_LEFT, WIPE_RIGHT };

    TransitionEngine() = default;

    void set_type(Type t) { type_ = t; }
    Type type() const { return type_; }

    void set_duration(float sec);
    float duration() const { return duration_sec_; }

    /* Push latest frames from program (A) and preview (B) sources */
    void push_program_frame(const uint8_t* bgra, int w, int h, int stride);
    void push_preview_frame(const uint8_t* bgra, int w, int h, int stride);

    /* Begin a transition from A → B */
    void begin();
    void cancel();
    bool is_transitioning() const { return transitioning_; }
    float progress() const { return progress_; }

    /* Advance transition clock; returns true while transition is active */
    bool tick(double elapsed_sec);

    /* Render the current output frame into out_bgra.
     * If not transitioning, copies program directly.
     * Returns true if a frame was written. */
    bool render(uint8_t* out_bgra, int w, int h, int stride);

private:
    void blend_crossfade(uint8_t* out, const uint8_t* a, const uint8_t* b,
                         int w, int h, int stride, float t);
    void blend_fade_to_black(uint8_t* out, const uint8_t* a, const uint8_t* b,
                             int w, int h, int stride, float t);
    void blend_wipe_horizontal(uint8_t* out, const uint8_t* a, const uint8_t* b,
                               int w, int h, int stride, float t, bool left_to_right);

    Type type_ = Type::CROSSFADE;
    float duration_sec_ = 1.0f;
    bool transitioning_ = false;
    float progress_ = 0.0f;
    double elapsed_ = 0.0;

    /* Double-buffered frame storage */
    mutable std::mutex mtx_;
    std::vector<uint8_t> frame_a_;  /* program */
    std::vector<uint8_t> frame_b_;  /* preview */
    int frame_w_ = 0;
    int frame_h_ = 0;
    int frame_stride_ = 0;
};

} // namespace mc1

#endif // MC1_TRANSITION_ENGINE_H
