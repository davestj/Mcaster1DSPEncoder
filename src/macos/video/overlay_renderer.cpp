/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/overlay_renderer.cpp — Video overlay renderer implementation
 *
 * Renders text (built-in 8x8 bitmap font), image/logo compositing,
 * lower-third bars, scrolling news tickers, and SRT subtitles onto
 * BGRA video frames.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "overlay_renderer.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace mc1 {

// ===========================================================================
// Built-in 8x8 bitmap font — printable ASCII 32 (' ') through 126 ('~')
// Each entry is 8 bytes, one byte per row, MSB = leftmost pixel.
// Classic CP437 / VGA-style glyphs.
// ===========================================================================
static const uint8_t kFont8x8[95][8] = {
    // 32  ' '
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 33  '!'
    {0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x00},
    // 34  '"'
    {0x6C, 0x6C, 0x6C, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 35  '#'
    {0x6C, 0x6C, 0xFE, 0x6C, 0xFE, 0x6C, 0x6C, 0x00},
    // 36  '$'
    {0x18, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x18, 0x00},
    // 37  '%'
    {0x00, 0xC6, 0xCC, 0x18, 0x30, 0x66, 0xC6, 0x00},
    // 38  '&'
    {0x38, 0x6C, 0x38, 0x76, 0xDC, 0xCC, 0x76, 0x00},
    // 39  '''
    {0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 40  '('
    {0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00},
    // 41  ')'
    {0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00},
    // 42  '*'
    {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00},
    // 43  '+'
    {0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00},
    // 44  ','
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30},
    // 45  '-'
    {0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00},
    // 46  '.'
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00},
    // 47  '/'
    {0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x80, 0x00},
    // 48  '0'
    {0x7C, 0xCE, 0xDE, 0xF6, 0xE6, 0xC6, 0x7C, 0x00},
    // 49  '1'
    {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00},
    // 50  '2'
    {0x3C, 0x66, 0x06, 0x1C, 0x30, 0x66, 0x7E, 0x00},
    // 51  '3'
    {0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00},
    // 52  '4'
    {0x1C, 0x3C, 0x6C, 0xCC, 0xFE, 0x0C, 0x1E, 0x00},
    // 53  '5'
    {0x7E, 0x62, 0x60, 0x7C, 0x06, 0x66, 0x3C, 0x00},
    // 54  '6'
    {0x3C, 0x66, 0xC0, 0xFC, 0xC6, 0xC6, 0x7C, 0x00},
    // 55  '7'
    {0x7E, 0x66, 0x06, 0x0C, 0x18, 0x18, 0x18, 0x00},
    // 56  '8'
    {0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00},
    // 57  '9'
    {0x7C, 0xC6, 0xC6, 0x7E, 0x06, 0x0C, 0x78, 0x00},
    // 58  ':'
    {0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00},
    // 59  ';'
    {0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x30},
    // 60  '<'
    {0x0C, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0C, 0x00},
    // 61  '='
    {0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00},
    // 62  '>'
    {0x60, 0x30, 0x18, 0x0C, 0x18, 0x30, 0x60, 0x00},
    // 63  '?'
    {0x3C, 0x66, 0x06, 0x0C, 0x18, 0x00, 0x18, 0x00},
    // 64  '@'
    {0x7C, 0xC6, 0xDE, 0xDE, 0xDE, 0xC0, 0x7C, 0x00},
    // 65  'A'
    {0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00},
    // 66  'B'
    {0xFC, 0x66, 0x66, 0x7C, 0x66, 0x66, 0xFC, 0x00},
    // 67  'C'
    {0x3C, 0x66, 0xC0, 0xC0, 0xC0, 0x66, 0x3C, 0x00},
    // 68  'D'
    {0xF8, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0xF8, 0x00},
    // 69  'E'
    {0xFE, 0x62, 0x68, 0x78, 0x68, 0x62, 0xFE, 0x00},
    // 70  'F'
    {0xFE, 0x62, 0x68, 0x78, 0x68, 0x60, 0xF0, 0x00},
    // 71  'G'
    {0x3C, 0x66, 0xC0, 0xC0, 0xCE, 0x66, 0x3E, 0x00},
    // 72  'H'
    {0xC6, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0x00},
    // 73  'I'
    {0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00},
    // 74  'J'
    {0x1E, 0x0C, 0x0C, 0x0C, 0xCC, 0xCC, 0x78, 0x00},
    // 75  'K'
    {0xE6, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0xE6, 0x00},
    // 76  'L'
    {0xF0, 0x60, 0x60, 0x60, 0x62, 0x66, 0xFE, 0x00},
    // 77  'M'
    {0xC6, 0xEE, 0xFE, 0xFE, 0xD6, 0xC6, 0xC6, 0x00},
    // 78  'N'
    {0xC6, 0xE6, 0xF6, 0xDE, 0xCE, 0xC6, 0xC6, 0x00},
    // 79  'O'
    {0x38, 0x6C, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x00},
    // 80  'P'
    {0xFC, 0x66, 0x66, 0x7C, 0x60, 0x60, 0xF0, 0x00},
    // 81  'Q'
    {0x7C, 0xC6, 0xC6, 0xC6, 0xD6, 0x7C, 0x0E, 0x00},
    // 82  'R'
    {0xFC, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0xE6, 0x00},
    // 83  'S'
    {0x3C, 0x66, 0x30, 0x18, 0x0C, 0x66, 0x3C, 0x00},
    // 84  'T'
    {0x7E, 0x5A, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00},
    // 85  'U'
    {0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00},
    // 86  'V'
    {0xC6, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x10, 0x00},
    // 87  'W'
    {0xC6, 0xC6, 0xD6, 0xFE, 0xFE, 0xEE, 0xC6, 0x00},
    // 88  'X'
    {0xC6, 0x6C, 0x38, 0x38, 0x6C, 0xC6, 0xC6, 0x00},
    // 89  'Y'
    {0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x3C, 0x00},
    // 90  'Z'
    {0xFE, 0xC6, 0x8C, 0x18, 0x32, 0x66, 0xFE, 0x00},
    // 91  '['
    {0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0x00},
    // 92  '\'
    {0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x02, 0x00},
    // 93  ']'
    {0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0x00},
    // 94  '^'
    {0x10, 0x38, 0x6C, 0xC6, 0x00, 0x00, 0x00, 0x00},
    // 95  '_'
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},
    // 96  '`'
    {0x30, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 97  'a'
    {0x00, 0x00, 0x78, 0x0C, 0x7C, 0xCC, 0x76, 0x00},
    // 98  'b'
    {0xE0, 0x60, 0x7C, 0x66, 0x66, 0x66, 0xDC, 0x00},
    // 99  'c'
    {0x00, 0x00, 0x3C, 0x66, 0x60, 0x66, 0x3C, 0x00},
    // 100 'd'
    {0x1C, 0x0C, 0x7C, 0xCC, 0xCC, 0xCC, 0x76, 0x00},
    // 101 'e'
    {0x00, 0x00, 0x3C, 0x66, 0x7E, 0x60, 0x3C, 0x00},
    // 102 'f'
    {0x1C, 0x36, 0x30, 0x78, 0x30, 0x30, 0x78, 0x00},
    // 103 'g'
    {0x00, 0x00, 0x76, 0xCC, 0xCC, 0x7C, 0x0C, 0x78},
    // 104 'h'
    {0xE0, 0x60, 0x6C, 0x76, 0x66, 0x66, 0xE6, 0x00},
    // 105 'i'
    {0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00},
    // 106 'j'
    {0x06, 0x00, 0x0E, 0x06, 0x06, 0x66, 0x66, 0x3C},
    // 107 'k'
    {0xE0, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0xE6, 0x00},
    // 108 'l'
    {0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00},
    // 109 'm'
    {0x00, 0x00, 0xEC, 0xFE, 0xD6, 0xD6, 0xD6, 0x00},
    // 110 'n'
    {0x00, 0x00, 0xDC, 0x66, 0x66, 0x66, 0x66, 0x00},
    // 111 'o'
    {0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x3C, 0x00},
    // 112 'p'
    {0x00, 0x00, 0xDC, 0x66, 0x66, 0x7C, 0x60, 0xF0},
    // 113 'q'
    {0x00, 0x00, 0x76, 0xCC, 0xCC, 0x7C, 0x0C, 0x1E},
    // 114 'r'
    {0x00, 0x00, 0xDC, 0x76, 0x60, 0x60, 0xF0, 0x00},
    // 115 's'
    {0x00, 0x00, 0x3C, 0x60, 0x3C, 0x06, 0x7C, 0x00},
    // 116 't'
    {0x30, 0x30, 0x7C, 0x30, 0x30, 0x36, 0x1C, 0x00},
    // 117 'u'
    {0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0x76, 0x00},
    // 118 'v'
    {0x00, 0x00, 0xC6, 0xC6, 0x6C, 0x38, 0x10, 0x00},
    // 119 'w'
    {0x00, 0x00, 0xC6, 0xD6, 0xD6, 0xFE, 0x6C, 0x00},
    // 120 'x'
    {0x00, 0x00, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0x00},
    // 121 'y'
    {0x00, 0x00, 0xC6, 0xC6, 0x6C, 0x38, 0x30, 0x60},
    // 122 'z'
    {0x00, 0x00, 0x7E, 0x4C, 0x18, 0x32, 0x7E, 0x00},
    // 123 '{'
    {0x0E, 0x18, 0x18, 0x70, 0x18, 0x18, 0x0E, 0x00},
    // 124 '|'
    {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00},
    // 125 '}'
    {0x70, 0x18, 0x18, 0x0E, 0x18, 0x18, 0x70, 0x00},
    // 126 '~'
    {0x76, 0xDC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};

// ===========================================================================
// Helper: extract ARGB channels from a uint32_t color value
// ===========================================================================
static inline void argb_split(uint32_t argb, uint8_t& a, uint8_t& r,
                              uint8_t& g, uint8_t& b)
{
    a = static_cast<uint8_t>((argb >> 24) & 0xFF);
    r = static_cast<uint8_t>((argb >> 16) & 0xFF);
    g = static_cast<uint8_t>((argb >>  8) & 0xFF);
    b = static_cast<uint8_t>((argb      ) & 0xFF);
}

// ===========================================================================
// Helper: alpha-blend a single pixel (ARGB source) onto a BGRA destination
// ===========================================================================
static inline void blend_pixel(uint8_t* dst, uint8_t sa, uint8_t sr,
                               uint8_t sg, uint8_t sb)
{
    if (sa == 0) return;
    if (sa == 255) {
        dst[0] = sb;  // B
        dst[1] = sg;  // G
        dst[2] = sr;  // R
        dst[3] = 255; // A
        return;
    }
    // out = src * alpha + dst * (255 - alpha), integer math
    uint16_t inv = 255 - sa;
    dst[0] = static_cast<uint8_t>((sb * sa + dst[0] * inv) / 255);
    dst[1] = static_cast<uint8_t>((sg * sa + dst[1] * inv) / 255);
    dst[2] = static_cast<uint8_t>((sr * sa + dst[2] * inv) / 255);
    dst[3] = 255;
}

// ===========================================================================
// Helper: fill a rectangle with alpha-blended color (ARGB)
// ===========================================================================
static void fill_rect_blend(uint8_t* bgra, int fw, int fh, int stride,
                            int rx, int ry, int rw, int rh, uint32_t argb)
{
    uint8_t a, r, g, b;
    argb_split(argb, a, r, g, b);
    if (a == 0) return;

    int x0 = std::max(rx, 0);
    int y0 = std::max(ry, 0);
    int x1 = std::min(rx + rw, fw);
    int y1 = std::min(ry + rh, fh);

    for (int y = y0; y < y1; ++y) {
        uint8_t* row = bgra + y * stride;
        for (int x = x0; x < x1; ++x) {
            blend_pixel(row + x * 4, a, r, g, b);
        }
    }
}

// ===========================================================================
// render_text_block — draw a string using the 8x8 bitmap font
// ===========================================================================
void OverlayRenderer::render_text_block(uint8_t* bgra, int w, int h,
                                        int stride, const std::string& text,
                                        int x, int y, uint32_t color,
                                        uint32_t bg_color, int scale)
{
    if (text.empty() || scale < 1) return;

    const int char_w = 8 * scale;
    const int char_h = 8 * scale;

    // --- Measure text for background rectangle ---
    int max_cols = 0;
    int lines = 1;
    int cur_cols = 0;
    for (char ch : text) {
        if (ch == '\n') {
            if (cur_cols > max_cols) max_cols = cur_cols;
            cur_cols = 0;
            ++lines;
        } else {
            ++cur_cols;
        }
    }
    if (cur_cols > max_cols) max_cols = cur_cols;

    // Draw background rectangle if alpha > 0
    uint8_t bg_a = static_cast<uint8_t>((bg_color >> 24) & 0xFF);
    if (bg_a > 0) {
        int pad = scale;  // small padding
        fill_rect_blend(bgra, w, h, stride,
                        x - pad, y - pad,
                        max_cols * char_w + pad * 2,
                        lines * char_h + pad * 2,
                        bg_color);
    }

    // --- Extract foreground color ---
    uint8_t fa, fr, fg, fb;
    argb_split(color, fa, fr, fg, fb);
    if (fa == 0) return;

    // --- Draw characters ---
    int cx = x;
    int cy = y;

    for (char ch : text) {
        if (ch == '\n') {
            cx = x;
            cy += char_h;
            continue;
        }

        int idx = static_cast<int>(ch) - 32;
        if (idx < 0 || idx >= 95) {
            cx += char_w;
            continue;
        }

        const uint8_t* glyph = kFont8x8[idx];

        for (int row = 0; row < 8; ++row) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < 8; ++col) {
                if (!(bits & (0x80 >> col))) continue;

                // Draw scaled pixel block
                for (int sy = 0; sy < scale; ++sy) {
                    int py = cy + row * scale + sy;
                    if (py < 0 || py >= h) continue;

                    uint8_t* row_ptr = bgra + py * stride;
                    for (int sx = 0; sx < scale; ++sx) {
                        int px = cx + col * scale + sx;
                        if (px < 0 || px >= w) continue;
                        blend_pixel(row_ptr + px * 4, fa, fr, fg, fb);
                    }
                }
            }
        }

        cx += char_w;
    }
}

// ===========================================================================
// render_image_overlay — alpha-composite cached logo onto the frame
//                        with optional scaling via render_width / render_height
// ===========================================================================
void OverlayRenderer::render_image_overlay(uint8_t* bgra, int w, int h,
                                           int stride)
{
    if (logo_bgra_.empty() || logo_w_ <= 0 || logo_h_ <= 0) return;

    float opacity = std::clamp(image_.opacity, 0.0f, 1.0f);
    if (opacity <= 0.0f) return;

    int ox = image_.x;
    int oy = image_.y;

    // Effective render dimensions (0 = original size)
    int rw = (image_.render_width  > 0) ? image_.render_width  : logo_w_;
    int rh = (image_.render_height > 0) ? image_.render_height : logo_h_;

    // Clip destination rect to frame bounds
    int dst_x0 = std::max(0, ox);
    int dst_y0 = std::max(0, oy);
    int dst_x1 = std::min(ox + rw, w);
    int dst_y1 = std::min(oy + rh, h);

    if (dst_x0 >= dst_x1 || dst_y0 >= dst_y1) return;

    int src_stride = logo_w_ * 4;

    // Scale factors: source pixels per destination pixel (fixed-point 16.16)
    uint32_t sx_step = (static_cast<uint32_t>(logo_w_) << 16) / static_cast<uint32_t>(rw);
    uint32_t sy_step = (static_cast<uint32_t>(logo_h_) << 16) / static_cast<uint32_t>(rh);

    for (int dy = dst_y0; dy < dst_y1; ++dy) {
        // Map destination Y to source Y (nearest-neighbor)
        uint32_t src_y_fp = static_cast<uint32_t>(dy - oy) * sy_step;
        int sy = static_cast<int>(src_y_fp >> 16);
        if (sy >= logo_h_) sy = logo_h_ - 1;

        const uint8_t* src_row = logo_bgra_.data() + sy * src_stride;
        uint8_t* dst_row = bgra + dy * stride;

        for (int dx = dst_x0; dx < dst_x1; ++dx) {
            // Map destination X to source X (nearest-neighbor)
            uint32_t src_x_fp = static_cast<uint32_t>(dx - ox) * sx_step;
            int sx = static_cast<int>(src_x_fp >> 16);
            if (sx >= logo_w_) sx = logo_w_ - 1;

            const uint8_t* sp = src_row + sx * 4;
            uint8_t* dp = dst_row + dx * 4;

            // Source is BGRA: sp[0]=B, sp[1]=G, sp[2]=R, sp[3]=A
            uint8_t sa = static_cast<uint8_t>(sp[3] * opacity);
            if (sa == 0) continue;

            if (sa == 255) {
                dp[0] = sp[0];
                dp[1] = sp[1];
                dp[2] = sp[2];
                dp[3] = 255;
            } else {
                uint16_t inv = 255 - sa;
                dp[0] = static_cast<uint8_t>((sp[0] * sa + dp[0] * inv) / 255);
                dp[1] = static_cast<uint8_t>((sp[1] * sa + dp[1] * inv) / 255);
                dp[2] = static_cast<uint8_t>((sp[2] * sa + dp[2] * inv) / 255);
                dp[3] = 255;
            }
        }
    }
}

// ===========================================================================
// image_rect — return the effective overlay rect in video coordinates
// ===========================================================================
OverlayRenderer::ImageRect OverlayRenderer::image_rect() const
{
    int rw = (image_.render_width  > 0) ? image_.render_width  : logo_w_;
    int rh = (image_.render_height > 0) ? image_.render_height : logo_h_;
    return { image_.x, image_.y, rw, rh };
}

// ===========================================================================
// render_lower_third — colored bar across the bottom with two text lines
// ===========================================================================
void OverlayRenderer::render_lower_third(uint8_t* bgra, int w, int h,
                                         int stride)
{
    int bar_h = lower_third_.bar_height;
    if (bar_h <= 0 || bar_h > h) return;

    int bar_y = h - bar_h;

    // Draw the bar background (alpha blended)
    fill_rect_blend(bgra, w, h, stride, 0, bar_y, w, bar_h,
                    lower_third_.bar_color);

    // Render line1 (larger, scale 3) with left padding
    int pad_x = 20;
    int line1_scale = 3;
    int line2_scale = 2;
    int line1_h = 8 * line1_scale;
    int line2_h = 8 * line2_scale;

    // Vertical layout: center both lines within the bar
    int total_text_h = line1_h + 4 + line2_h; // 4px gap
    int text_y = bar_y + (bar_h - total_text_h) / 2;
    if (text_y < bar_y) text_y = bar_y;

    if (!lower_third_.line1.empty()) {
        render_text_block(bgra, w, h, stride,
                          lower_third_.line1,
                          pad_x, text_y,
                          lower_third_.text_color,
                          0x00000000,  // no extra bg (bar is already drawn)
                          line1_scale);
    }

    if (!lower_third_.line2.empty()) {
        render_text_block(bgra, w, h, stride,
                          lower_third_.line2,
                          pad_x, text_y + line1_h + 4,
                          lower_third_.text_color,
                          0x00000000,
                          line2_scale);
    }
}

// ===========================================================================
// render_news_ticker — scrolling crawl bar at the very bottom of the frame
// ===========================================================================
void OverlayRenderer::render_news_ticker(uint8_t* bgra, int w, int h,
                                         int stride)
{
    int bar_h = ticker_.bar_height;
    if (bar_h <= 0 || bar_h > h) return;

    int bar_y = h - bar_h;

    // Draw background bar (alpha blended)
    fill_rect_blend(bgra, w, h, stride, 0, bar_y, w, bar_h,
                    ticker_.bg_color);

    // Draw teal accent stripe on top edge (3px)
    int accent_h = 3;
    fill_rect_blend(bgra, w, h, stride, 0, bar_y, w, accent_h,
                    ticker_.accent_color);

    // Render scrolling text
    if (!ticker_.text.empty()) {
        int text_scale = 2;
        int char_w = 8 * text_scale;
        int char_h = 8 * text_scale;
        int text_pixel_w = static_cast<int>(ticker_.text.size()) * char_w;

        // Vertical center of bar (below accent)
        int text_y = bar_y + (bar_h - char_h) / 2;
        if (text_y < bar_y + accent_h) text_y = bar_y + accent_h;

        // Horizontal position: start off-screen right, scroll left
        int text_x = w - static_cast<int>(ticker_.scroll_offset);

        render_text_block(bgra, w, h, stride,
                          ticker_.text,
                          text_x, text_y,
                          ticker_.text_color,
                          0x00000000,  // no extra bg
                          text_scale);

        // Advance scroll offset
        ticker_.scroll_offset += ticker_.scroll_speed;

        // Reset when fully scrolled off the left edge
        if (text_x + text_pixel_w < 0) {
            ticker_.scroll_offset = 0.0f;
        }
    }
}

// ===========================================================================
// render_srt — display current subtitle centered near the bottom
// ===========================================================================
void OverlayRenderer::render_srt(uint8_t* bgra, int w, int h, int stride)
{
    if (srt_entries_.empty()) return;

    // Binary search for the current subtitle
    const SrtEntry* active = nullptr;

    int lo = 0;
    int hi = static_cast<int>(srt_entries_.size()) - 1;

    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        const SrtEntry& e = srt_entries_[mid];

        if (current_time_ms_ < e.start_ms) {
            hi = mid - 1;
        } else if (current_time_ms_ > e.end_ms) {
            lo = mid + 1;
        } else {
            active = &e;
            break;
        }
    }

    if (!active || active->text.empty()) return;

    int text_scale = 2;
    int char_w = 8 * text_scale;
    int char_h = 8 * text_scale;

    // Measure text width (first line only for centering, multi-line uses max)
    int max_line_len = 0;
    int line_count = 1;
    int cur_len = 0;
    for (char ch : active->text) {
        if (ch == '\n') {
            if (cur_len > max_line_len) max_line_len = cur_len;
            cur_len = 0;
            ++line_count;
        } else {
            ++cur_len;
        }
    }
    if (cur_len > max_line_len) max_line_len = cur_len;

    int text_pixel_w = max_line_len * char_w;
    int text_pixel_h = line_count * char_h;

    // Position: centered horizontally, near bottom (above ticker if present)
    int text_x = (w - text_pixel_w) / 2;
    int bottom_margin = 80;  // room for ticker/lower-third
    if (ticker_.enabled) {
        bottom_margin += ticker_.bar_height;
    }
    int text_y = h - bottom_margin - text_pixel_h;
    if (text_y < 0) text_y = 0;

    // Semi-transparent black background behind subtitle
    render_text_block(bgra, w, h, stride,
                      active->text,
                      text_x, text_y,
                      0xFFFFFFFF,    // white text
                      0xA0000000,    // semi-transparent black bg
                      text_scale);
}

// ===========================================================================
// parse_srt — parse a standard SRT subtitle file
// ===========================================================================
std::vector<SrtEntry> OverlayRenderer::parse_srt(const std::string& path)
{
    std::vector<SrtEntry> entries;
    std::ifstream ifs(path);
    if (!ifs.is_open()) return entries;

    // Helper: parse "HH:MM:SS,mmm" to milliseconds
    auto parse_ts = [](const std::string& ts) -> int64_t {
        int hh = 0, mm = 0, ss = 0, ms = 0;
        // Accept both ',' and '.' as decimal separator
        if (std::sscanf(ts.c_str(), "%d:%d:%d,%d", &hh, &mm, &ss, &ms) != 4) {
            if (std::sscanf(ts.c_str(), "%d:%d:%d.%d", &hh, &mm, &ss, &ms) != 4) {
                return -1;
            }
        }
        return static_cast<int64_t>(hh) * 3600000 +
               static_cast<int64_t>(mm) * 60000 +
               static_cast<int64_t>(ss) * 1000 +
               static_cast<int64_t>(ms);
    };

    std::string line;
    while (std::getline(ifs, line)) {
        // Strip trailing \r
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // Skip blank lines
        if (line.empty()) continue;

        // Try to read index number
        SrtEntry entry{};
        try {
            entry.index = std::stoi(line);
        } catch (...) {
            continue;
        }

        // Read timestamp line: "HH:MM:SS,mmm --> HH:MM:SS,mmm"
        if (!std::getline(ifs, line)) break;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        auto arrow_pos = line.find("-->");
        if (arrow_pos == std::string::npos) continue;

        std::string start_str = line.substr(0, arrow_pos);
        std::string end_str = line.substr(arrow_pos + 3);

        // Trim whitespace
        auto trim = [](std::string& s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
                s.erase(s.begin());
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t' ||
                                  s.back() == '\r'))
                s.pop_back();
        };
        trim(start_str);
        trim(end_str);

        entry.start_ms = parse_ts(start_str);
        entry.end_ms = parse_ts(end_str);
        if (entry.start_ms < 0 || entry.end_ms < 0) continue;

        // Read text lines until blank line or EOF
        std::string text_buf;
        while (std::getline(ifs, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) break;
            if (!text_buf.empty()) text_buf += '\n';
            text_buf += line;
        }

        entry.text = std::move(text_buf);
        if (!entry.text.empty()) {
            entries.push_back(std::move(entry));
        }
    }

    return entries;
}

// ===========================================================================
// Public interface
// ===========================================================================

void OverlayRenderer::render(uint8_t* bgra, int w, int h, int stride)
{
    if (!bgra || w <= 0 || h <= 0 || stride <= 0) return;

    // Render in order: image first (background), then overlays on top
    if (image_.enabled) {
        render_image_overlay(bgra, w, h, stride);
    }

    if (text_.enabled && !text_.text.empty()) {
        int scale = std::max(1, text_.font_size / 8);
        render_text_block(bgra, w, h, stride,
                          text_.text, text_.x, text_.y,
                          text_.color, text_.bg_color, scale);
    }

    if (lower_third_.enabled) {
        render_lower_third(bgra, w, h, stride);
    }

    if (ticker_.enabled) {
        render_news_ticker(bgra, w, h, stride);
    }

    if (srt_enabled_) {
        render_srt(bgra, w, h, stride);
    }
}

void OverlayRenderer::set_text_overlay(const TextOverlay& cfg)
{
    text_ = cfg;
}

void OverlayRenderer::set_image_overlay(const ImageOverlay& cfg)
{
    image_ = cfg;
}

void OverlayRenderer::set_lower_third(const LowerThird& cfg)
{
    lower_third_ = cfg;
}

void OverlayRenderer::set_news_ticker(const NewsTicker& cfg)
{
    ticker_ = cfg;
}

bool OverlayRenderer::load_srt(const std::string& path)
{
    auto entries = parse_srt(path);
    if (entries.empty()) return false;

    srt_entries_ = std::move(entries);
    return true;
}

void OverlayRenderer::set_srt_enabled(bool en)
{
    srt_enabled_ = en;
}

void OverlayRenderer::set_playback_time(int64_t ms)
{
    current_time_ms_ = ms;
}

bool OverlayRenderer::load_image(const std::string& path)
{
    // Store the path — actual BGRA data will be provided via set_image_data()
    // by the caller (e.g. MainWindow) after decoding with QImage.
    image_.file_path = path;
    return !path.empty();
}

void OverlayRenderer::set_image_data(const uint8_t* bgra, int w, int h)
{
    if (!bgra || w <= 0 || h <= 0) {
        logo_bgra_.clear();
        logo_w_ = 0;
        logo_h_ = 0;
        return;
    }

    size_t sz = static_cast<size_t>(w) * h * 4;
    logo_bgra_.resize(sz);
    std::memcpy(logo_bgra_.data(), bgra, sz);
    logo_w_ = w;
    logo_h_ = h;
}

} // namespace mc1
