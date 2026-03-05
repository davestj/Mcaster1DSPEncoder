// playlist_parser.h — Parse M3U / PLS / XSPF / plain-text playlists
// Phase 3 — Mcaster1DSPEncoder Linux v1.2.0
#pragma once

#include <string>
#include <vector>
#include <optional>

// ---------------------------------------------------------------------------
// PlaylistEntry — one track / stream URL from a playlist file
// ---------------------------------------------------------------------------
struct PlaylistEntry {
    std::string path;          // absolute file path or http(s):// stream URL
    std::string title;         // display title (may be empty)
    int         duration_sec;  // -1 if unknown

    bool is_url() const {
        return path.rfind("http://",  0) == 0 ||
               path.rfind("https://", 0) == 0 ||
               path.rfind("rtmp://",  0) == 0 ||
               path.rfind("rtsp://",  0) == 0;
    }
};

// ---------------------------------------------------------------------------
// PlaylistParser — detect format and parse
// ---------------------------------------------------------------------------
class PlaylistParser {
public:
    // Parse a playlist file.  Format is auto-detected from extension and
    // content.  Relative paths are resolved against the playlist's directory.
    // Returns empty vector on failure.
    static std::vector<PlaylistEntry> parse(const std::string& file_path);

    // Parse from an in-memory string (format hint via fake filename extension)
    static std::vector<PlaylistEntry> parse_string(const std::string& content,
                                                   const std::string& hint_ext = ".m3u",
                                                   const std::string& base_dir = "");

    // Individual format parsers (public for testing)
    static std::vector<PlaylistEntry> parse_m3u (const std::string& content, const std::string& base_dir);
    static std::vector<PlaylistEntry> parse_pls (const std::string& content, const std::string& base_dir);
    static std::vector<PlaylistEntry> parse_xspf(const std::string& content, const std::string& base_dir);
    static std::vector<PlaylistEntry> parse_txt (const std::string& content, const std::string& base_dir);

private:
    // Resolve a playlist-relative path to an absolute path
    static std::string resolve_path(const std::string& entry_path, const std::string& base_dir);

    // Minimal XML tag extraction for XSPF (no full XML library needed)
    static std::string xml_text(const std::string& xml, const std::string& tag, size_t from = 0);
    static std::vector<std::string> xml_elements(const std::string& xml, const std::string& tag);

    // Trim whitespace
    static std::string trim(const std::string& s);
};
