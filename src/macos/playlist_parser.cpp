// playlist_parser.cpp — M3U / PLS / XSPF / TXT playlist parser
// Phase 3 — Mcaster1DSPEncoder Linux v1.2.0
#include "playlist_parser.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// trim
// ---------------------------------------------------------------------------
std::string PlaylistParser::trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// ---------------------------------------------------------------------------
// resolve_path — make relative paths absolute using base_dir
// ---------------------------------------------------------------------------
std::string PlaylistParser::resolve_path(const std::string& entry_path,
                                         const std::string& base_dir)
{
    if (entry_path.empty()) return {};

    // URLs pass through untouched
    if (entry_path.rfind("http://",  0) == 0 ||
        entry_path.rfind("https://", 0) == 0 ||
        entry_path.rfind("rtmp://",  0) == 0 ||
        entry_path.rfind("file://",  0) == 0)
    {
        // Strip file:// prefix to absolute path
        if (entry_path.rfind("file://", 0) == 0) {
            return entry_path.substr(7);
        }
        return entry_path;
    }

    if (entry_path[0] == '/') return entry_path;  // already absolute

    if (!base_dir.empty()) {
        fs::path p = fs::path(base_dir) / entry_path;
        return p.lexically_normal().string();
    }

    return entry_path;
}

// ---------------------------------------------------------------------------
// parse — auto-detect format from file extension
// ---------------------------------------------------------------------------
std::vector<PlaylistEntry> PlaylistParser::parse(const std::string& file_path)
{
    std::ifstream f(file_path);
    if (!f.is_open()) return {};

    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    std::string base = fs::path(file_path).parent_path().string();

    // Detect by extension
    std::string ext;
    {
        auto p = file_path.rfind('.');
        if (p != std::string::npos) ext = file_path.substr(p);
        for (auto& c : ext) c = static_cast<char>(std::tolower(c));
    }

    if (ext == ".pls") return parse_pls(content, base);
    if (ext == ".xspf") return parse_xspf(content, base);
    if (ext == ".txt") return parse_txt(content, base);
    return parse_m3u(content, base);  // .m3u, .m3u8, default
}

// ---------------------------------------------------------------------------
// parse_string
// ---------------------------------------------------------------------------
std::vector<PlaylistEntry> PlaylistParser::parse_string(const std::string& content,
                                                         const std::string& hint_ext,
                                                         const std::string& base_dir)
{
    std::string ext = hint_ext;
    for (auto& c : ext) c = static_cast<char>(std::tolower(c));

    if (ext == ".pls") return parse_pls(content, base_dir);
    if (ext == ".xspf") return parse_xspf(content, base_dir);
    if (ext == ".txt") return parse_txt(content, base_dir);
    return parse_m3u(content, base_dir);
}

// ---------------------------------------------------------------------------
// parse_m3u — Extended M3U
// ---------------------------------------------------------------------------
std::vector<PlaylistEntry> PlaylistParser::parse_m3u(const std::string& content,
                                                      const std::string& base_dir)
{
    std::vector<PlaylistEntry> result;
    std::istringstream ss(content);
    std::string line;

    PlaylistEntry pending;
    bool has_extinf = false;

    while (std::getline(ss, line)) {
        line = trim(line);
        if (line.empty()) continue;

        if (line.rfind("#EXTM3U", 0) == 0) continue;  // header

        if (line.rfind("#EXTINF:", 0) == 0) {
            // #EXTINF:duration,title
            std::string info = line.substr(8);
            auto comma = info.find(',');
            if (comma != std::string::npos) {
                try { pending.duration_sec = std::stoi(info.substr(0, comma)); }
                catch (...) { pending.duration_sec = -1; }
                pending.title = trim(info.substr(comma + 1));
            } else {
                try { pending.duration_sec = std::stoi(info); }
                catch (...) { pending.duration_sec = -1; }
                pending.title.clear();
            }
            has_extinf = true;
            continue;
        }

        if (line[0] == '#') continue;  // other comment / directive

        // This is a path/URL
        pending.path = resolve_path(line, base_dir);
        if (!has_extinf) {
            pending.duration_sec = -1;
            pending.title.clear();
        }
        result.push_back(pending);
        pending = PlaylistEntry{};
        has_extinf = false;
    }

    return result;
}

// ---------------------------------------------------------------------------
// parse_pls — Winamp PLS format
// ---------------------------------------------------------------------------
std::vector<PlaylistEntry> PlaylistParser::parse_pls(const std::string& content,
                                                      const std::string& base_dir)
{
    // Key-value map first, then assemble entries
    std::map<int, PlaylistEntry> idx_map;
    std::istringstream ss(content);
    std::string line;

    while (std::getline(ss, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '[') continue;
        if (line.rfind("NumberOfEntries", 0) == 0) continue;
        if (line.rfind("Version", 0) == 0) continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        // Key format: File1, Title1, Length1
        std::string ktype;
        int         knum = 0;
        bool        have_num = false;

        // Find trailing digits
        size_t i = key.size();
        while (i > 0 && std::isdigit(key[i - 1])) --i;
        if (i < key.size()) {
            knum     = std::stoi(key.substr(i));
            ktype    = key.substr(0, i);
            have_num = true;
        }

        if (!have_num) continue;

        if (ktype == "File") {
            idx_map[knum].path = resolve_path(val, base_dir);
        } else if (ktype == "Title") {
            idx_map[knum].title = val;
        } else if (ktype == "Length") {
            try { idx_map[knum].duration_sec = std::stoi(val); }
            catch (...) { idx_map[knum].duration_sec = -1; }
        }
    }

    std::vector<PlaylistEntry> result;
    result.reserve(idx_map.size());
    for (auto& [k, v] : idx_map) {
        if (!v.path.empty()) result.push_back(v);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Minimal XML helpers for XSPF
// ---------------------------------------------------------------------------
std::string PlaylistParser::xml_text(const std::string& xml,
                                      const std::string& tag,
                                      size_t from)
{
    std::string open  = "<" + tag + ">";
    std::string close = "</" + tag + ">";
    auto a = xml.find(open, from);
    if (a == std::string::npos) return {};
    a += open.size();
    auto b = xml.find(close, a);
    if (b == std::string::npos) return {};
    return xml.substr(a, b - a);
}

std::vector<std::string> PlaylistParser::xml_elements(const std::string& xml,
                                                        const std::string& tag)
{
    std::vector<std::string> result;
    std::string open  = "<" + tag;
    std::string close = "</" + tag + ">";
    size_t pos = 0;
    while (true) {
        auto a = xml.find(open, pos);
        if (a == std::string::npos) break;
        // Skip to end of opening tag (may have attributes)
        auto end_open = xml.find('>', a);
        if (end_open == std::string::npos) break;
        // Self-closing?
        if (end_open > 0 && xml[end_open - 1] == '/') {
            pos = end_open + 1;
            continue;
        }
        auto b = xml.find(close, end_open);
        if (b == std::string::npos) break;
        result.push_back(xml.substr(end_open + 1, b - end_open - 1));
        pos = b + close.size();
    }
    return result;
}

// ---------------------------------------------------------------------------
// parse_xspf — XSPF (XML Shareable Playlist Format)
// ---------------------------------------------------------------------------
std::vector<PlaylistEntry> PlaylistParser::parse_xspf(const std::string& content,
                                                       const std::string& base_dir)
{
    std::vector<PlaylistEntry> result;
    auto tracks = xml_elements(content, "track");

    for (auto& t : tracks) {
        PlaylistEntry e;
        e.duration_sec = -1;

        std::string loc = xml_text(t, "location");
        if (loc.empty()) loc = xml_text(t, "identifier");
        e.path = resolve_path(trim(loc), base_dir);

        e.title = trim(xml_text(t, "title"));

        // Creator → artist (we store in title for now: "Artist - Title")
        std::string creator = trim(xml_text(t, "creator"));
        if (!creator.empty() && !e.title.empty()) {
            e.title = creator + " - " + e.title;
        } else if (!creator.empty()) {
            e.title = creator;
        }

        std::string dur = xml_text(t, "duration");
        if (!dur.empty()) {
            try {
                long ms = std::stol(trim(dur));
                e.duration_sec = static_cast<int>(ms / 1000);
            } catch (...) {}
        }

        if (!e.path.empty()) result.push_back(e);
    }
    return result;
}

// ---------------------------------------------------------------------------
// parse_txt — plain paths, one per line
// ---------------------------------------------------------------------------
std::vector<PlaylistEntry> PlaylistParser::parse_txt(const std::string& content,
                                                      const std::string& base_dir)
{
    std::vector<PlaylistEntry> result;
    std::istringstream ss(content);
    std::string line;

    while (std::getline(ss, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        PlaylistEntry e;
        e.path         = resolve_path(line, base_dir);
        e.duration_sec = -1;
        result.push_back(e);
    }
    return result;
}
