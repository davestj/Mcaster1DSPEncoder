/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/overlay_renderer.h — Video overlay renderer: text, image, lower-third,
 *                            news ticker, SRT subtitles over BGRA frames
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_OVERLAY_RENDERER_H
#define MC1_OVERLAY_RENDERER_H

#include <cstdint>
#include <string>
#include <vector>

namespace mc1 {

// ---------------------------------------------------------------------------
// TextOverlay — fixed-position bitmap text rendered onto the frame
// ---------------------------------------------------------------------------
struct TextOverlay {
    std::string text;
    int x = 10, y = 10;
    int font_size = 16;             // scale factor (1 = 8px base)
    uint32_t color = 0xFFFFFFFF;    // ARGB white
    uint32_t bg_color = 0x80000000; // semi-transparent black
    bool enabled = false;
};

// ---------------------------------------------------------------------------
// ImageOverlay — PNG logo / watermark composited onto the frame
// ---------------------------------------------------------------------------
struct ImageOverlay {
    std::string file_path;     // PNG file path
    int x = 0, y = 0;         // position (pixels from top-left)
    float opacity = 1.0f;
    int render_width = 0;      // 0 = original size
    int render_height = 0;
    bool enabled = false;
};

// ---------------------------------------------------------------------------
// LowerThird — broadcast-style name/title bar across the bottom
// ---------------------------------------------------------------------------
struct LowerThird {
    std::string line1;         // Main text (name/title)
    std::string line2;         // Subtitle
    uint32_t bar_color = 0xFF00D4AA;  // teal
    uint32_t text_color = 0xFFFFFFFF; // white
    int bar_height = 60;
    int duration_ms = 5000;
    bool enabled = false;
};

// ---------------------------------------------------------------------------
// NewsTicker — FOX News-style scrolling crawl at the bottom of the frame
// ---------------------------------------------------------------------------
struct NewsTicker {
    std::string text;          // Scrolling text content
    uint32_t bg_color = 0xCC1a1a2e;   // dark navy semi-transparent
    uint32_t text_color = 0xFFFFFFFF;
    uint32_t accent_color = 0xFF00D4AA; // teal accent bar
    int bar_height = 36;
    float scroll_speed = 2.0f; // pixels per frame
    bool enabled = false;
    // Runtime state
    float scroll_offset = 0.0f;
};

// ---------------------------------------------------------------------------
// SrtEntry — one parsed subtitle cue from an SRT file
// ---------------------------------------------------------------------------
struct SrtEntry {
    int index;
    int64_t start_ms;
    int64_t end_ms;
    std::string text;
};

// ---------------------------------------------------------------------------
// OverlayRenderer — composites all overlay types onto a BGRA video frame
// ---------------------------------------------------------------------------
class OverlayRenderer {
public:
    /// Master render: composites all enabled overlays onto the BGRA frame.
    void render(uint8_t* bgra, int w, int h, int stride);

    // --- Configuration setters (called from main/UI thread) ----------------
    void set_text_overlay(const TextOverlay& cfg);
    void set_image_overlay(const ImageOverlay& cfg);
    void set_lower_third(const LowerThird& cfg);
    void set_news_ticker(const NewsTicker& cfg);

    bool load_srt(const std::string& path);
    void set_srt_enabled(bool en);
    void set_playback_time(int64_t ms);

    /// Attempt to load an image file.  Returns true if path was stored.
    /// Actual BGRA pixel data must be provided via set_image_data() — the
    /// caller (e.g. MainWindow) uses QImage to decode the PNG and convert
    /// to BGRA, then passes the raw buffer here.
    bool load_image(const std::string& path);

    /// Set raw BGRA pixel data for the image overlay (decoded externally).
    void set_image_data(const uint8_t* bgra, int w, int h);

    // --- Accessors for UI --------------------------------------------------
    const TextOverlay&  text_overlay()  const { return text_; }
    const ImageOverlay& image_overlay() const { return image_; }
    const LowerThird&   lower_third()   const { return lower_third_; }
    const NewsTicker&   news_ticker()   const { return ticker_; }

    /// Returns the effective rendered rect of the image overlay in video coords.
    /// If render_width/render_height are 0, uses original image dimensions.
    struct ImageRect { int x, y, w, h; };
    ImageRect image_rect() const;

private:
    TextOverlay  text_;
    ImageOverlay image_;
    LowerThird   lower_third_;
    NewsTicker   ticker_;
    std::vector<SrtEntry> srt_entries_;
    bool    srt_enabled_     = false;
    int64_t current_time_ms_ = 0;

    // Cached image overlay (raw BGRA pixels)
    std::vector<uint8_t> logo_bgra_;
    int logo_w_ = 0, logo_h_ = 0;

    // --- Sub-renderers -----------------------------------------------------
    void render_text_block(uint8_t* bgra, int w, int h, int stride,
                           const std::string& text, int x, int y,
                           uint32_t color, uint32_t bg_color, int scale);
    void render_image_overlay(uint8_t* bgra, int w, int h, int stride);
    void render_lower_third(uint8_t* bgra, int w, int h, int stride);
    void render_news_ticker(uint8_t* bgra, int w, int h, int stride);
    void render_srt(uint8_t* bgra, int w, int h, int stride);

    static std::vector<SrtEntry> parse_srt(const std::string& path);
};

} // namespace mc1

#endif // MC1_OVERLAY_RENDERER_H
