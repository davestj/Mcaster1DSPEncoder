/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * video/transition_engine.h — Professional A/B frame blending engine
 *
 * Broadcast-grade video transitions with gamma-correct blending,
 * feathered edges, and 12 transition types.
 *
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
    enum class Type {
        CUT,
        CROSSFADE,         /* Smooth gamma-correct dissolve */
        FADE_TO_BLACK,     /* A → black → B */
        DIP_TO_WHITE,      /* A → white → B */
        WIPE_LEFT,         /* Feathered wipe left-to-right */
        WIPE_RIGHT,        /* Feathered wipe right-to-left */
        WIPE_UP,           /* Feathered wipe bottom-to-top */
        WIPE_DOWN,         /* Feathered wipe top-to-bottom */
        PUSH_LEFT,         /* B pushes A off-screen to the left */
        PUSH_RIGHT,        /* B pushes A off-screen to the right */
        IRIS_CIRCLE,       /* Circular reveal from center */
        DISSOLVE,          /* Random pixel dissolve pattern */
    };

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
    /* Transition blend functions */
    void blend_crossfade(uint8_t* out, const uint8_t* a, const uint8_t* b,
                         int w, int h, int stride, float t);
    void blend_dip_to_color(uint8_t* out, const uint8_t* a, const uint8_t* b,
                            int w, int h, int stride, float t,
                            uint8_t cr, uint8_t cg, uint8_t cb);
    void blend_wipe(uint8_t* out, const uint8_t* a, const uint8_t* b,
                    int w, int h, int stride, float t,
                    bool horizontal, bool forward);
    void blend_push(uint8_t* out, const uint8_t* a, const uint8_t* b,
                    int w, int h, int stride, float t, bool left);
    void blend_iris_circle(uint8_t* out, const uint8_t* a, const uint8_t* b,
                           int w, int h, int stride, float t);
    void blend_dissolve(uint8_t* out, const uint8_t* a, const uint8_t* b,
                        int w, int h, int stride, float t);

    /* sRGB gamma helpers for broadcast-quality blending */
    static float srgb_to_linear(uint8_t v);
    static uint8_t linear_to_srgb(float v);

    /* Easing for smooth transition feel */
    static float ease_in_out(float t);

    Type type_ = Type::CROSSFADE;
    float duration_sec_ = 1.0f;
    bool transitioning_ = false;
    float progress_ = 0.0f;
    double elapsed_ = 0.0;

    /* Feather width for wipe edges (pixels) */
    static constexpr int kFeatherWidth = 24;

    /* Double-buffered frame storage */
    mutable std::mutex mtx_;
    std::vector<uint8_t> frame_a_;  /* program */
    std::vector<uint8_t> frame_b_;  /* preview */
    int frame_w_ = 0;
    int frame_h_ = 0;
    int frame_stride_ = 0;

    /* Pre-computed dissolve pattern (seeded hash per pixel) */
    std::vector<float> dissolve_threshold_;
    int dissolve_w_ = 0;
    int dissolve_h_ = 0;
    void ensure_dissolve_pattern(int w, int h);
};

} // namespace mc1

#endif // MC1_TRANSITION_ENGINE_H
