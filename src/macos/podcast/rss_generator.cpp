/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * podcast/rss_generator.cpp — RSS 2.0 + iTunes podcast feed generator
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "rss_generator.h"
#include "../config_types.h"

#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace mc1 {

std::string RssGenerator::xmlEscape(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
        case '&':  out += "&amp;";  break;
        case '<':  out += "&lt;";   break;
        case '>':  out += "&gt;";   break;
        case '"':  out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default:   out += c;        break;
        }
    }
    return out;
}

std::string RssGenerator::formatDuration(int seconds)
{
    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    int s = seconds % 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
    return buf;
}

std::string RssGenerator::formatRfc2822(time_t t)
{
    if (t == 0) t = time(nullptr);
    struct tm tm_buf;
    gmtime_r(&t, &tm_buf);
    char buf[64];
    strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S +0000", &tm_buf);
    return buf;
}

std::string RssGenerator::mimeForExtension(const std::string &ext)
{
    if (ext == ".mp3")  return "audio/mpeg";
    if (ext == ".ogg")  return "audio/ogg";
    if (ext == ".opus") return "audio/opus";
    if (ext == ".flac") return "audio/flac";
    if (ext == ".m4a")  return "audio/mp4";
    if (ext == ".aac")  return "audio/aac";
    return "audio/mpeg";
}

std::string RssGenerator::generate(const PodcastConfig &cfg,
                                   const std::vector<Episode> &episodes)
{
    if (cfg.rss_output_dir.empty()) return {};

    /* Ensure output dir exists */
    mkdir(cfg.rss_output_dir.c_str(), 0755);

    std::string path = cfg.rss_output_dir;
    if (path.back() != '/') path += '/';
    path += "feed.xml";

    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<rss xmlns:itunes=\"http://www.itunes.com/dtds/podcast-1.0.dtd\"\n"
        << "     xmlns:atom=\"http://www.w3.org/2005/Atom\"\n"
        << "     version=\"2.0\">\n"
        << "  <channel>\n";

    /* Channel metadata */
    xml << "    <title>" << xmlEscape(cfg.title) << "</title>\n";
    if (!cfg.website.empty())
        xml << "    <link>" << xmlEscape(cfg.website) << "</link>\n";
    xml << "    <description>" << xmlEscape(cfg.description) << "</description>\n";
    xml << "    <language>" << xmlEscape(cfg.language) << "</language>\n";

    if (!cfg.copyright.empty())
        xml << "    <copyright>" << xmlEscape(cfg.copyright) << "</copyright>\n";

    xml << "    <lastBuildDate>" << formatRfc2822(time(nullptr)) << "</lastBuildDate>\n";

    /* iTunes-specific */
    if (!cfg.author.empty())
        xml << "    <itunes:author>" << xmlEscape(cfg.author) << "</itunes:author>\n";
    if (!cfg.category.empty())
        xml << "    <itunes:category text=\"" << xmlEscape(cfg.category) << "\"/>\n";
    if (!cfg.cover_art_url.empty())
        xml << "    <itunes:image href=\"" << xmlEscape(cfg.cover_art_url) << "\"/>\n";
    xml << "    <itunes:explicit>false</itunes:explicit>\n";

    /* Episodes (most recent first) */
    auto sorted = episodes;
    std::sort(sorted.begin(), sorted.end(),
              [](const Episode &a, const Episode &b) { return a.pub_date > b.pub_date; });

    for (const auto &ep : sorted) {
        xml << "\n    <item>\n";
        xml << "      <title>" << xmlEscape(ep.title) << "</title>\n";
        if (!ep.description.empty())
            xml << "      <description>" << xmlEscape(ep.description) << "</description>\n";

        /* Enclosure — the actual audio file */
        std::string url = ep.audio_url.empty() ? ep.audio_file : ep.audio_url;
        xml << "      <enclosure url=\"" << xmlEscape(url)
            << "\" length=\"" << ep.file_size
            << "\" type=\"" << xmlEscape(ep.mime_type) << "\"/>\n";

        xml << "      <pubDate>" << formatRfc2822(ep.pub_date) << "</pubDate>\n";

        /* iTunes episode metadata */
        if (ep.duration_sec > 0)
            xml << "      <itunes:duration>" << formatDuration(ep.duration_sec)
                << "</itunes:duration>\n";
        if (ep.episode_number > 0)
            xml << "      <itunes:episode>" << ep.episode_number << "</itunes:episode>\n";

        xml << "    </item>\n";
    }

    xml << "  </channel>\n"
        << "</rss>\n";

    /* Write to file */
    std::ofstream out(path, std::ios::trunc);
    if (!out) return {};
    out << xml.str();
    out.close();

    return path;
}

std::vector<Episode> RssGenerator::scanEpisodes(const std::string &episode_dir)
{
    std::vector<Episode> episodes;
    if (episode_dir.empty()) return episodes;

    DIR *dir = opendir(episode_dir.c_str());
    if (!dir) return episodes;

    int ep_num = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != nullptr) {
        std::string name = ent->d_name;
        if (name.empty() || name[0] == '.') continue;

        /* Find extension */
        auto dot = name.rfind('.');
        if (dot == std::string::npos) continue;
        std::string ext = name.substr(dot);

        /* Filter audio files */
        if (ext != ".mp3" && ext != ".ogg" && ext != ".opus" &&
            ext != ".flac" && ext != ".m4a" && ext != ".aac")
            continue;

        std::string full_path = episode_dir;
        if (full_path.back() != '/') full_path += '/';
        full_path += name;

        /* Get file size and mtime */
        struct stat st;
        if (stat(full_path.c_str(), &st) != 0) continue;

        Episode ep;
        ep.audio_file = name;
        ep.file_size  = st.st_size;
        ep.pub_date   = st.st_mtime;
        ep.mime_type  = mimeForExtension(ext);
        ep.episode_number = ++ep_num;

        /* Use filename (without extension) as title */
        ep.title = name.substr(0, dot);
        /* Replace hyphens/underscores with spaces for readability */
        for (auto &c : ep.title) {
            if (c == '-' || c == '_') c = ' ';
        }

        episodes.push_back(std::move(ep));
    }

    closedir(dir);

    /* Sort by modification time */
    std::sort(episodes.begin(), episodes.end(),
              [](const Episode &a, const Episode &b) { return a.pub_date < b.pub_date; });

    /* Re-number after sorting */
    for (size_t i = 0; i < episodes.size(); ++i)
        episodes[i].episode_number = static_cast<int>(i + 1);

    return episodes;
}

} // namespace mc1
