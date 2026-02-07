/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/video_effects.h — Video effects engine: per-frame BGRA pixel processing
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_VIDEO_EFFECTS_H
#define MC1_VIDEO_EFFECTS_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mc1 {

// ---------------------------------------------------------------------------
// VideoEffectParam — single named float parameter with range + default
// ---------------------------------------------------------------------------
struct VideoEffectParam {
    std::string name;
    float       value       = 0.0f;
    float       min_val     = 0.0f;
    float       max_val     = 1.0f;
    float       default_val = 0.0f;
};

// ---------------------------------------------------------------------------
// VideoEffect — abstract base class for a single video effect
// ---------------------------------------------------------------------------
class VideoEffect {
public:
    virtual ~VideoEffect() = default;

    virtual std::string name() const = 0;

    /// Process a BGRA frame in-place.  Byte order per pixel: [B, G, R, A].
    virtual void process(uint8_t* bgra, int w, int h, int stride) = 0;

    /// Return the list of tweakable parameters (with current values).
    virtual std::vector<VideoEffectParam> params() const = 0;

    /// Set a parameter by name.
    virtual void set_param(const std::string& name, float value) = 0;

    bool enabled = true;
};

// ---------------------------------------------------------------------------
// VideoEffectsChain — ordered chain of effects, processed sequentially
// ---------------------------------------------------------------------------
class VideoEffectsChain {
public:
    VideoEffectsChain() = default;

    /// Run every enabled effect in order on the BGRA frame.
    void process(uint8_t* bgra, int w, int h, int stride);

    void add_effect(std::unique_ptr<VideoEffect> fx);
    void remove_effect(int index);

    void set_bypass(bool bypass);
    bool is_bypass() const;

    int          count() const;
    VideoEffect* effect(int index);
    const std::vector<std::unique_ptr<VideoEffect>>& effects() const;

private:
    std::vector<std::unique_ptr<VideoEffect>> effects_;
    bool bypass_ = false;
};

/// Factory: create a video effect by type index (0-12).
std::unique_ptr<VideoEffect> create_video_effect(int type_index);

/// Number of available effect types.
constexpr int kVideoEffectTypeCount = 13;

/// Name of effect type by index.
const char* video_effect_type_name(int type_index);

} // namespace mc1

#endif // MC1_VIDEO_EFFECTS_H
