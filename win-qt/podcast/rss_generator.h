/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * podcast/rss_generator.h — RSS 2.0 + iTunes podcast feed generator
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_RSS_GENERATOR_H
#define MC1_RSS_GENERATOR_H

#include <string>
#include <vector>
#include <cstdint>
#include <ctime>

namespace mc1 {

struct PodcastConfig;  // forward declaration from config_types.h

/* Represents a single podcast episode */
struct Episode {
    std::string title;
    std::string description;
    std::string audio_file;       /* local filename (e.g. "episode-01.mp3") */
    std::string audio_url;        /* full URL for enclosure (if uploaded) */
    std::string mime_type = "audio/mpeg"; /* audio/mpeg, audio/ogg, audio/opus, audio/flac */
    int64_t     file_size = 0;    /* bytes */
    int         duration_sec = 0; /* total seconds */
    time_t      pub_date = 0;    /* publication timestamp */
    int         episode_number = 0;
};

class RssGenerator {
public:
    /*
     * Generate an RSS 2.0 feed and write it to the output directory.
     * Returns the full path of the generated feed.xml on success,
     * or an empty string on failure.
     */
    static std::string generate(const PodcastConfig &cfg,
                                const std::vector<Episode> &episodes);

    /*
     * Scan an episode directory for audio files and build Episode metadata.
     * Detects: .mp3, .ogg, .opus, .flac, .m4a, .aac
     */
    static std::vector<Episode> scanEpisodes(const std::string &episode_dir);

private:
    /* XML-escape a string (& < > " ') */
    static std::string xmlEscape(const std::string &s);

    /* Format duration_sec as HH:MM:SS */
    static std::string formatDuration(int seconds);

    /* Format time_t as RFC 2822 date */
    static std::string formatRfc2822(time_t t);

    /* Detect MIME type from file extension */
    static std::string mimeForExtension(const std::string &ext);
};

} // namespace mc1

#endif // MC1_RSS_GENERATOR_H
