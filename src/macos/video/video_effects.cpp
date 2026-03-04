/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/video_effects.cpp — Video effects engine implementation
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "video_effects.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace mc1 {

// ===================================================================
// VideoEffectsChain
// ===================================================================

void VideoEffectsChain::process(uint8_t* bgra, int w, int h, int stride)
{
    if (bypass_) return;
    for (auto& fx : effects_) {
        if (fx && fx->enabled)
            fx->process(bgra, w, h, stride);
    }
}

void VideoEffectsChain::add_effect(std::unique_ptr<VideoEffect> fx)
{
    effects_.push_back(std::move(fx));
}

void VideoEffectsChain::remove_effect(int index)
{
    if (index >= 0 && index < static_cast<int>(effects_.size()))
        effects_.erase(effects_.begin() + index);
}

void VideoEffectsChain::set_bypass(bool bypass) { bypass_ = bypass; }
bool VideoEffectsChain::is_bypass() const       { return bypass_; }

int VideoEffectsChain::count() const
{
    return static_cast<int>(effects_.size());
}

VideoEffect* VideoEffectsChain::effect(int index)
{
    if (index >= 0 && index < static_cast<int>(effects_.size()))
        return effects_[index].get();
    return nullptr;
}

const std::vector<std::unique_ptr<VideoEffect>>& VideoEffectsChain::effects() const
{
    return effects_;
}

// ===================================================================
// Helper: clamp a float to uint8_t
// ===================================================================

static inline uint8_t clamp8(float v)
{
    return static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f));
}

// ===================================================================
// 1. FxBrightnessContrast
// ===================================================================

class FxBrightnessContrast : public VideoEffect {
public:
    FxBrightnessContrast()
        : brightness_(0.0f), contrast_(1.0f) {}

    std::string name() const override { return "Brightness/Contrast"; }

    void process(uint8_t* bgra, int w, int h, int stride) override
    {
        for (int y = 0; y < h; ++y) {
            uint8_t* row = bgra + y * stride;
            for (int x = 0; x < w; ++x) {
                uint8_t* px = row + x * 4;
                // BGRA layout: px[0]=B, px[1]=G, px[2]=R, px[3]=A
                px[0] = clamp8(contrast_ * (px[0] - 128.0f) + 128.0f + brightness_);
                px[1] = clamp8(contrast_ * (px[1] - 128.0f) + 128.0f + brightness_);
                px[2] = clamp8(contrast_ * (px[2] - 128.0f) + 128.0f + brightness_);
            }
        }
    }

    std::vector<VideoEffectParam> params() const override
    {
        return {
            {"brightness", brightness_, -100.0f, 100.0f, 0.0f},
            {"contrast",   contrast_,     0.5f,   2.0f,  1.0f}
        };
    }

    void set_param(const std::string& n, float v) override
    {
        if (n == "brightness") brightness_ = std::clamp(v, -100.0f, 100.0f);
        else if (n == "contrast") contrast_ = std::clamp(v, 0.5f, 2.0f);
    }

private:
    float brightness_;
    float contrast_;
};

// ===================================================================
// 2. FxSaturation
// ===================================================================

class FxSaturation : public VideoEffect {
public:
    FxSaturation()
        : saturation_(1.0f) {}

    std::string name() const override { return "Saturation"; }

    void process(uint8_t* bgra, int w, int h, int stride) override
    {
        for (int y = 0; y < h; ++y) {
            uint8_t* row = bgra + y * stride;
            for (int x = 0; x < w; ++x) {
                uint8_t* px = row + x * 4;
                float B = px[0], G = px[1], R = px[2];
                float Y = 0.299f * R + 0.587f * G + 0.114f * B;
                px[0] = clamp8(Y + saturation_ * (B - Y));
                px[1] = clamp8(Y + saturation_ * (G - Y));
                px[2] = clamp8(Y + saturation_ * (R - Y));
            }
        }
    }

    std::vector<VideoEffectParam> params() const override
    {
        return {
            {"saturation", saturation_, 0.0f, 3.0f, 1.0f}
        };
    }

    void set_param(const std::string& n, float v) override
    {
        if (n == "saturation") saturation_ = std::clamp(v, 0.0f, 3.0f);
    }

private:
    float saturation_;
};

// ===================================================================
// 3. FxColorTemperature
// ===================================================================

class FxColorTemperature : public VideoEffect {
public:
    FxColorTemperature()
        : temperature_(0.0f) {}

    std::string name() const override { return "Color Temperature"; }

    void process(uint8_t* bgra, int w, int h, int stride) override
    {
        // temperature -100..+100: negative = cool (boost B, reduce R),
        //                         positive = warm (boost R, reduce B)
        float r_adj = temperature_ * 0.01f * 30.0f;   // up to +/-30
        float b_adj = -temperature_ * 0.01f * 30.0f;

        for (int y = 0; y < h; ++y) {
            uint8_t* row = bgra + y * stride;
            for (int x = 0; x < w; ++x) {
                uint8_t* px = row + x * 4;
                px[0] = clamp8(px[0] + b_adj);  // B
                px[2] = clamp8(px[2] + r_adj);  // R
            }
        }
    }

    std::vector<VideoEffectParam> params() const override
    {
        return {
            {"temperature", temperature_, -100.0f, 100.0f, 0.0f}
        };
    }

    void set_param(const std::string& n, float v) override
    {
        if (n == "temperature") temperature_ = std::clamp(v, -100.0f, 100.0f);
    }

private:
    float temperature_;
};

// ===================================================================
// 4. FxGaussianBlur — 3-pass box blur approximation
// ===================================================================

class FxGaussianBlur : public VideoEffect {
public:
    FxGaussianBlur()
        : radius_(1.0f) {}

    std::string name() const override { return "Gaussian Blur"; }

    void process(uint8_t* bgra, int w, int h, int stride) override
    {
        int r = static_cast<int>(radius_);
        if (r < 1) return;

        // Temporary buffer for ping-pong
        std::vector<uint8_t> tmp(static_cast<size_t>(h) * stride);

        // 3-pass box blur approximation of Gaussian
        for (int pass = 0; pass < 3; ++pass) {
            // Horizontal pass: bgra -> tmp
            box_blur_h(bgra, tmp.data(), w, h, stride, r);
            // Vertical pass: tmp -> bgra
            box_blur_v(tmp.data(), bgra, w, h, stride, r);
        }
    }

    std::vector<VideoEffectParam> params() const override
    {
        return {
            {"radius", radius_, 1.0f, 20.0f, 1.0f}
        };
    }

    void set_param(const std::string& n, float v) override
    {
        if (n == "radius") radius_ = std::clamp(v, 1.0f, 20.0f);
    }

private:
    float radius_;

    static void box_blur_h(const uint8_t* src, uint8_t* dst,
                            int w, int h, int stride, int r)
    {
        int box = r * 2 + 1;
        float inv = 1.0f / static_cast<float>(box);

        for (int y = 0; y < h; ++y) {
            const uint8_t* srow = src + y * stride;
            uint8_t*       drow = dst + y * stride;

            for (int ch = 0; ch < 3; ++ch) {
                // Initialize running sum for the first pixel
                float sum = 0.0f;
                for (int i = -r; i <= r; ++i) {
                    int sx = std::clamp(i, 0, w - 1);
                    sum += srow[sx * 4 + ch];
                }
                drow[ch] = clamp8(sum * inv);

                // Slide the window across the row
                for (int x = 1; x < w; ++x) {
                    int add_idx = std::min(x + r, w - 1);
                    int sub_idx = std::max(x - r - 1, 0);
                    sum += srow[add_idx * 4 + ch] - srow[sub_idx * 4 + ch];
                    drow[x * 4 + ch] = clamp8(sum * inv);
                }
            }
            // Copy alpha channel unchanged
            for (int x = 0; x < w; ++x)
                drow[x * 4 + 3] = srow[x * 4 + 3];
        }
    }

    static void box_blur_v(const uint8_t* src, uint8_t* dst,
                            int w, int h, int stride, int r)
    {
        int box = r * 2 + 1;
        float inv = 1.0f / static_cast<float>(box);

        for (int x = 0; x < w; ++x) {
            for (int ch = 0; ch < 3; ++ch) {
                // Initialize running sum for the first row
                float sum = 0.0f;
                for (int i = -r; i <= r; ++i) {
                    int sy = std::clamp(i, 0, h - 1);
                    sum += src[sy * stride + x * 4 + ch];
                }
                dst[x * 4 + ch] = clamp8(sum * inv);

                // Slide the window down the column
                for (int y = 1; y < h; ++y) {
                    int add_idx = std::min(y + r, h - 1);
                    int sub_idx = std::max(y - r - 1, 0);
                    sum += src[add_idx * stride + x * 4 + ch]
                         - src[sub_idx * stride + x * 4 + ch];
                    dst[y * stride + x * 4 + ch] = clamp8(sum * inv);
                }
            }
            // Copy alpha column unchanged
            for (int y = 0; y < h; ++y)
                dst[y * stride + x * 4 + 3] = src[y * stride + x * 4 + 3];
        }
    }
};

// ===================================================================
// 5. FxSharpen — unsharp mask
// ===================================================================

class FxSharpen : public VideoEffect {
public:
    FxSharpen()
        : strength_(1.0f) {}

    std::string name() const override { return "Sharpen"; }

    void process(uint8_t* bgra, int w, int h, int stride) override
    {
        if (strength_ <= 0.0f) return;

        // Make a copy, then box-blur the copy with radius 1
        size_t buf_size = static_cast<size_t>(h) * stride;
        std::vector<uint8_t> blurred(bgra, bgra + buf_size);

        box_blur(blurred.data(), w, h, stride, 1);

        // Unsharp mask: out = original + strength * (original - blurred)
        for (int y = 0; y < h; ++y) {
            uint8_t* row = bgra + y * stride;
            const uint8_t* brow = blurred.data() + y * stride;
            for (int x = 0; x < w; ++x) {
                for (int ch = 0; ch < 3; ++ch) {
                    float orig = row[x * 4 + ch];
                    float blur = brow[x * 4 + ch];
                    row[x * 4 + ch] = clamp8(orig + strength_ * (orig - blur));
                }
            }
        }
    }

    std::vector<VideoEffectParam> params() const override
    {
        return {
            {"strength", strength_, 0.0f, 5.0f, 1.0f}
        };
    }

    void set_param(const std::string& n, float v) override
    {
        if (n == "strength") strength_ = std::clamp(v, 0.0f, 5.0f);
    }

private:
    float strength_;

    // Simple single-pass box blur with radius r (horizontal + vertical)
    static void box_blur(uint8_t* data, int w, int h, int stride, int r)
    {
        int box = r * 2 + 1;
        float inv = 1.0f / static_cast<float>(box);
        std::vector<uint8_t> tmp(static_cast<size_t>(h) * stride);

        // Horizontal: data -> tmp
        for (int y = 0; y < h; ++y) {
            const uint8_t* srow = data + y * stride;
            uint8_t*       drow = tmp.data() + y * stride;
            for (int ch = 0; ch < 3; ++ch) {
                float sum = 0.0f;
                for (int i = -r; i <= r; ++i) {
                    int sx = std::clamp(i, 0, w - 1);
                    sum += srow[sx * 4 + ch];
                }
                drow[ch] = clamp8(sum * inv);
                for (int x = 1; x < w; ++x) {
                    sum += srow[std::min(x + r, w - 1) * 4 + ch]
                         - srow[std::max(x - r - 1, 0) * 4 + ch];
                    drow[x * 4 + ch] = clamp8(sum * inv);
                }
            }
            for (int x = 0; x < w; ++x)
                drow[x * 4 + 3] = srow[x * 4 + 3];
        }

        // Vertical: tmp -> data
        for (int x = 0; x < w; ++x) {
            for (int ch = 0; ch < 3; ++ch) {
                float sum = 0.0f;
                for (int i = -r; i <= r; ++i) {
                    int sy = std::clamp(i, 0, h - 1);
                    sum += tmp[sy * stride + x * 4 + ch];
                }
                data[x * 4 + ch] = clamp8(sum * inv);
                for (int y = 1; y < h; ++y) {
                    sum += tmp[std::min(y + r, h - 1) * stride + x * 4 + ch]
                         - tmp[std::max(y - r - 1, 0) * stride + x * 4 + ch];
                    data[y * stride + x * 4 + ch] = clamp8(sum * inv);
                }
            }
            for (int y = 0; y < h; ++y)
                data[y * stride + x * 4 + 3] = tmp[y * stride + x * 4 + 3];
        }
    }
};

// ===================================================================
// 6. FxGrayscale
// ===================================================================

class FxGrayscale : public VideoEffect {
public:
    std::string name() const override { return "Grayscale"; }

    void process(uint8_t* bgra, int w, int h, int stride) override
    {
        for (int y = 0; y < h; ++y) {
            uint8_t* row = bgra + y * stride;
            for (int x = 0; x < w; ++x) {
                uint8_t* px = row + x * 4;
                float Y = 0.299f * px[2] + 0.587f * px[1] + 0.114f * px[0];
                uint8_t grey = clamp8(Y);
                px[0] = grey; // B
                px[1] = grey; // G
                px[2] = grey; // R
            }
        }
    }

    std::vector<VideoEffectParam> params() const override { return {}; }
    void set_param(const std::string&, float) override {}
};

// ===================================================================
// 7. FxSepia
// ===================================================================

class FxSepia : public VideoEffect {
public:
    FxSepia()
        : intensity_(1.0f) {}

    std::string name() const override { return "Sepia"; }

    void process(uint8_t* bgra, int w, int h, int stride) override
    {
        for (int y = 0; y < h; ++y) {
            uint8_t* row = bgra + y * stride;
            for (int x = 0; x < w; ++x) {
                uint8_t* px = row + x * 4;
                float B = px[0], G = px[1], R = px[2];
                float Y = 0.299f * R + 0.587f * G + 0.114f * B;

                // Sepia tint
                float sR = Y + 112.0f * intensity_;
                float sG = Y +  66.0f * intensity_;
                float sB = Y +  20.0f * intensity_;

                // Blend with original based on intensity
                px[2] = clamp8(R + intensity_ * (sR - R));  // R
                px[1] = clamp8(G + intensity_ * (sG - G));  // G
                px[0] = clamp8(B + intensity_ * (sB - B));  // B
            }
        }
    }

    std::vector<VideoEffectParam> params() const override
    {
        return {
            {"intensity", intensity_, 0.0f, 1.0f, 1.0f}
        };
    }

    void set_param(const std::string& n, float v) override
    {
        if (n == "intensity") intensity_ = std::clamp(v, 0.0f, 1.0f);
    }

private:
    float intensity_;
};

// ===================================================================
// 8. FxInvert
// ===================================================================

class FxInvert : public VideoEffect {
public:
    std::string name() const override { return "Invert"; }

    void process(uint8_t* bgra, int w, int h, int stride) override
    {
        for (int y = 0; y < h; ++y) {
            uint8_t* row = bgra + y * stride;
            for (int x = 0; x < w; ++x) {
                uint8_t* px = row + x * 4;
                px[0] = 255 - px[0]; // B
                px[1] = 255 - px[1]; // G
                px[2] = 255 - px[2]; // R
                // Alpha unchanged
            }
        }
    }

    std::vector<VideoEffectParam> params() const override { return {}; }
    void set_param(const std::string&, float) override {}
};

// ===================================================================
// 9. FxChromaKey
// ===================================================================

class FxChromaKey : public VideoEffect {
public:
    FxChromaKey()
        : key_hue_(120.0f), tolerance_(30.0f), spill_(50.0f) {}

    std::string name() const override { return "Chroma Key"; }

    void process(uint8_t* bgra, int w, int h, int stride) override
    {
        for (int y = 0; y < h; ++y) {
            uint8_t* row = bgra + y * stride;
            for (int x = 0; x < w; ++x) {
                uint8_t* px = row + x * 4;
                float B = px[0], G = px[1], R = px[2];

                // RGB -> HSV
                float r = R / 255.0f, g = G / 255.0f, b = B / 255.0f;
                float cmax = std::max({r, g, b});
                float cmin = std::min({r, g, b});
                float delta = cmax - cmin;

                float hue = 0.0f;
                if (delta > 1e-5f) {
                    if (cmax == r)      hue = 60.0f * std::fmod((g - b) / delta, 6.0f);
                    else if (cmax == g) hue = 60.0f * ((b - r) / delta + 2.0f);
                    else                hue = 60.0f * ((r - g) / delta + 4.0f);
                    if (hue < 0.0f) hue += 360.0f;
                }

                float sat = (cmax > 1e-5f) ? (delta / cmax) : 0.0f;

                // Hue distance (circular)
                float hue_diff = std::abs(hue - key_hue_);
                if (hue_diff > 180.0f) hue_diff = 360.0f - hue_diff;

                // Keying: reduce alpha where hue is near key_hue and
                // saturation is high enough to be chromatic
                float tol = tolerance_ * 0.01f * 180.0f;  // tolerance in degrees
                if (hue_diff < tol && sat > 0.15f) {
                    float alpha_factor = hue_diff / tol;
                    alpha_factor = std::clamp(alpha_factor, 0.0f, 1.0f);
                    px[3] = static_cast<uint8_t>(px[3] * alpha_factor);

                    // Spill suppression: reduce the key color channel contribution
                    float spill_factor = spill_ * 0.01f * (1.0f - alpha_factor);
                    if (key_hue_ >= 60.0f && key_hue_ <= 180.0f) {
                        // Key is green-ish: suppress green
                        px[1] = clamp8(G - spill_factor * (G - std::min(R, B)));
                    } else if (key_hue_ >= 180.0f && key_hue_ <= 300.0f) {
                        // Key is blue-ish: suppress blue
                        px[0] = clamp8(B - spill_factor * (B - std::min(R, G)));
                    } else {
                        // Key is red-ish: suppress red
                        px[2] = clamp8(R - spill_factor * (R - std::min(G, B)));
                    }
                }
            }
        }
    }

    std::vector<VideoEffectParam> params() const override
    {
        return {
            {"key_hue",   key_hue_,   0.0f, 360.0f, 120.0f},
            {"tolerance", tolerance_, 0.0f, 100.0f,  30.0f},
            {"spill",     spill_,    0.0f, 100.0f,  50.0f}
        };
    }

    void set_param(const std::string& n, float v) override
    {
        if (n == "key_hue")        key_hue_   = std::clamp(v, 0.0f, 360.0f);
        else if (n == "tolerance") tolerance_ = std::clamp(v, 0.0f, 100.0f);
        else if (n == "spill")     spill_     = std::clamp(v, 0.0f, 100.0f);
    }

private:
    float key_hue_;
    float tolerance_;
    float spill_;
};

// ===================================================================
// 10. FxVignette
// ===================================================================

class FxVignette : public VideoEffect {
public:
    FxVignette()
        : radius_(1.2f), softness_(0.5f) {}

    std::string name() const override { return "Vignette"; }

    void process(uint8_t* bgra, int w, int h, int stride) override
    {
        float cx = w * 0.5f;
        float cy = h * 0.5f;
        // Normalize so that the corner distance is ~1.0
        float diag = std::sqrt(cx * cx + cy * cy);
        if (diag < 1.0f) return;

        float edge_inner = radius_ - softness_;
        float edge_outer = radius_;

        for (int y = 0; y < h; ++y) {
            uint8_t* row = bgra + y * stride;
            float dy = (y - cy) / diag;
            for (int x = 0; x < w; ++x) {
                float dx = (x - cx) / diag;
                float dist = std::sqrt(dx * dx + dy * dy);

                // smoothstep(edge_inner, edge_outer, dist)
                float t = (dist - edge_inner) / (edge_outer - edge_inner + 1e-6f);
                t = std::clamp(t, 0.0f, 1.0f);
                float factor = t * t * (3.0f - 2.0f * t); // Hermite smoothstep

                float darken = 1.0f - factor;

                uint8_t* px = row + x * 4;
                px[0] = clamp8(px[0] * darken); // B
                px[1] = clamp8(px[1] * darken); // G
                px[2] = clamp8(px[2] * darken); // R
            }
        }
    }

    std::vector<VideoEffectParam> params() const override
    {
        return {
            {"radius",   radius_,   0.5f, 2.0f, 1.2f},
            {"softness", softness_, 0.1f, 1.0f, 0.5f}
        };
    }

    void set_param(const std::string& n, float v) override
    {
        if (n == "radius")        radius_   = std::clamp(v, 0.5f, 2.0f);
        else if (n == "softness") softness_ = std::clamp(v, 0.1f, 1.0f);
    }

private:
    float radius_;
    float softness_;
};

// ===================================================================
// 11. FxEdgeDetect — Sobel operator
// ===================================================================

class FxEdgeDetect : public VideoEffect {
public:
    FxEdgeDetect()
        : threshold_(128.0f) {}

    std::string name() const override { return "Edge Detect"; }

    void process(uint8_t* bgra, int w, int h, int stride) override
    {
        if (w < 3 || h < 3) return;

        // Convert to grayscale in temp buffer
        std::vector<float> grey(static_cast<size_t>(w) * h);
        for (int y = 0; y < h; ++y) {
            const uint8_t* row = bgra + y * stride;
            for (int x = 0; x < w; ++x) {
                const uint8_t* px = row + x * 4;
                grey[y * w + x] = 0.299f * px[2] + 0.587f * px[1] + 0.114f * px[0];
            }
        }

        // Sobel kernels applied, write result to frame
        for (int y = 0; y < h; ++y) {
            uint8_t* row = bgra + y * stride;
            for (int x = 0; x < w; ++x) {
                float mag = 0.0f;
                if (x > 0 && x < w - 1 && y > 0 && y < h - 1) {
                    // Sobel X
                    float gx = -grey[(y-1)*w + (x-1)] + grey[(y-1)*w + (x+1)]
                             - 2.0f * grey[y*w + (x-1)] + 2.0f * grey[y*w + (x+1)]
                             - grey[(y+1)*w + (x-1)] + grey[(y+1)*w + (x+1)];
                    // Sobel Y
                    float gy = -grey[(y-1)*w + (x-1)] - 2.0f * grey[(y-1)*w + x] - grey[(y-1)*w + (x+1)]
                             + grey[(y+1)*w + (x-1)] + 2.0f * grey[(y+1)*w + x] + grey[(y+1)*w + (x+1)];
                    mag = std::sqrt(gx * gx + gy * gy);
                }

                uint8_t edge = (mag >= threshold_) ? 255 : 0;
                uint8_t* px = row + x * 4;
                px[0] = edge; // B
                px[1] = edge; // G
                px[2] = edge; // R
                // Alpha unchanged
            }
        }
    }

    std::vector<VideoEffectParam> params() const override
    {
        return {
            {"threshold", threshold_, 0.0f, 255.0f, 128.0f}
        };
    }

    void set_param(const std::string& n, float v) override
    {
        if (n == "threshold") threshold_ = std::clamp(v, 0.0f, 255.0f);
    }

private:
    float threshold_;
};

// ===================================================================
// 12. FxPixelize
// ===================================================================

class FxPixelize : public VideoEffect {
public:
    FxPixelize()
        : block_size_(8.0f) {}

    std::string name() const override { return "Pixelize"; }

    void process(uint8_t* bgra, int w, int h, int stride) override
    {
        int bs = static_cast<int>(block_size_);
        if (bs < 2) return;

        for (int by = 0; by < h; by += bs) {
            for (int bx = 0; bx < w; bx += bs) {
                int bw = std::min(bs, w - bx);
                int bh = std::min(bs, h - by);
                int count = bw * bh;

                // Accumulate averages for the block
                float sumB = 0.0f, sumG = 0.0f, sumR = 0.0f;
                for (int dy = 0; dy < bh; ++dy) {
                    const uint8_t* row = bgra + (by + dy) * stride;
                    for (int dx = 0; dx < bw; ++dx) {
                        const uint8_t* px = row + (bx + dx) * 4;
                        sumB += px[0];
                        sumG += px[1];
                        sumR += px[2];
                    }
                }

                uint8_t avgB = clamp8(sumB / count);
                uint8_t avgG = clamp8(sumG / count);
                uint8_t avgR = clamp8(sumR / count);

                // Fill the block with the average color
                for (int dy = 0; dy < bh; ++dy) {
                    uint8_t* row = bgra + (by + dy) * stride;
                    for (int dx = 0; dx < bw; ++dx) {
                        uint8_t* px = row + (bx + dx) * 4;
                        px[0] = avgB;
                        px[1] = avgG;
                        px[2] = avgR;
                    }
                }
            }
        }
    }

    std::vector<VideoEffectParam> params() const override
    {
        return {
            {"block_size", block_size_, 2.0f, 64.0f, 8.0f}
        };
    }

    void set_param(const std::string& n, float v) override
    {
        if (n == "block_size") block_size_ = std::clamp(v, 2.0f, 64.0f);
    }

private:
    float block_size_;
};

// ===================================================================
// 13. FxFlipMirror
// ===================================================================

class FxFlipMirror : public VideoEffect {
public:
    FxFlipMirror()
        : flip_h_(0.0f), flip_v_(0.0f) {}

    std::string name() const override { return "Flip/Mirror"; }

    void process(uint8_t* bgra, int w, int h, int stride) override
    {
        bool do_h = (flip_h_ >= 0.5f);
        bool do_v = (flip_v_ >= 0.5f);
        if (!do_h && !do_v) return;

        // Vertical flip: swap rows top-to-bottom
        if (do_v) {
            std::vector<uint8_t> row_buf(static_cast<size_t>(w) * 4);
            for (int y = 0; y < h / 2; ++y) {
                uint8_t* top = bgra + y * stride;
                uint8_t* bot = bgra + (h - 1 - y) * stride;
                std::memcpy(row_buf.data(), top, static_cast<size_t>(w) * 4);
                std::memcpy(top, bot, static_cast<size_t>(w) * 4);
                std::memcpy(bot, row_buf.data(), static_cast<size_t>(w) * 4);
            }
        }

        // Horizontal flip: swap columns within each row
        if (do_h) {
            for (int y = 0; y < h; ++y) {
                uint8_t* row = bgra + y * stride;
                for (int x = 0; x < w / 2; ++x) {
                    uint8_t* left  = row + x * 4;
                    uint8_t* right = row + (w - 1 - x) * 4;
                    // Swap 4 bytes (BGRA)
                    uint8_t tmp[4];
                    std::memcpy(tmp, left, 4);
                    std::memcpy(left, right, 4);
                    std::memcpy(right, tmp, 4);
                }
            }
        }
    }

    std::vector<VideoEffectParam> params() const override
    {
        return {
            {"flip_h", flip_h_, 0.0f, 1.0f, 0.0f},
            {"flip_v", flip_v_, 0.0f, 1.0f, 0.0f}
        };
    }

    void set_param(const std::string& n, float v) override
    {
        if (n == "flip_h")      flip_h_ = std::clamp(v, 0.0f, 1.0f);
        else if (n == "flip_v") flip_v_ = std::clamp(v, 0.0f, 1.0f);
    }

private:
    float flip_h_;
    float flip_v_;
};

// ===================================================================
// Factory functions
// ===================================================================

std::unique_ptr<VideoEffect> create_video_effect(int type_index)
{
    switch (type_index) {
    case  0: return std::make_unique<FxBrightnessContrast>();
    case  1: return std::make_unique<FxSaturation>();
    case  2: return std::make_unique<FxColorTemperature>();
    case  3: return std::make_unique<FxGaussianBlur>();
    case  4: return std::make_unique<FxSharpen>();
    case  5: return std::make_unique<FxGrayscale>();
    case  6: return std::make_unique<FxSepia>();
    case  7: return std::make_unique<FxInvert>();
    case  8: return std::make_unique<FxChromaKey>();
    case  9: return std::make_unique<FxVignette>();
    case 10: return std::make_unique<FxEdgeDetect>();
    case 11: return std::make_unique<FxPixelize>();
    case 12: return std::make_unique<FxFlipMirror>();
    default: return nullptr;
    }
}

static const char* s_effect_type_names[kVideoEffectTypeCount] = {
    "Brightness/Contrast",
    "Saturation",
    "Color Temperature",
    "Gaussian Blur",
    "Sharpen",
    "Grayscale",
    "Sepia",
    "Invert",
    "Chroma Key",
    "Vignette",
    "Edge Detect",
    "Pixelize",
    "Flip/Mirror"
};

const char* video_effect_type_name(int type_index)
{
    if (type_index >= 0 && type_index < kVideoEffectTypeCount)
        return s_effect_type_names[type_index];
    return "Unknown";
}

} // namespace mc1
