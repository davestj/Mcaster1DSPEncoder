/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * podcast/podcast_publisher.h — Multi-destination podcast episode publisher
 *
 * Publishes podcast recordings to:
 *   1. SFTP to web server (+ RSS XML upload)
 *   2. WordPress REST API (PowerPress / Seriously Simple Podcasting / Podlove)
 *   3. Buzzsprout API (token auth, simple POST)
 *   4. Transistor.fm API (API key, pre-signed upload URLs)
 *   5. Podbean API (OAuth 2.0, three-step upload)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_PODCAST_PUBLISHER_H
#define MC1_PODCAST_PUBLISHER_H

#include <functional>
#include <string>
#include <vector>

namespace mc1 {

struct PodcastConfig;
struct Episode;

/* Result of a publish attempt to one destination */
struct PublishResult {
    std::string destination;    /* "SFTP", "WordPress", "Buzzsprout", etc. */
    bool        success = false;
    std::string error;          /* error message if !success */
    std::string url;            /* published URL (if available) */
};

class PodcastPublisher {
public:
    /* Progress callback: (destination, bytes_sent, total_bytes) */
    using ProgressCallback = std::function<void(const std::string&, int64_t, int64_t)>;

    /*
     * Publish an episode to all enabled destinations in the PodcastConfig.
     * Returns a result for each attempted destination.
     */
    static std::vector<PublishResult> publishEpisode(
            const PodcastConfig &cfg,
            const Episode &episode,
            const std::string &local_audio_path,
            ProgressCallback progress = nullptr);

    /*
     * Publish just the RSS feed (after adding an episode locally).
     * Uploads feed.xml to SFTP if enabled.
     */
    static PublishResult publishRssFeed(const PodcastConfig &cfg);

    /* Individual destination publishers */
    static PublishResult publishToSftp(
            const PodcastConfig &cfg,
            const std::string &local_audio_path,
            ProgressCallback progress = nullptr);

    static PublishResult publishToWordPress(
            const PodcastConfig &cfg,
            const Episode &episode,
            const std::string &local_audio_path);

    static PublishResult publishToBuzzsprout(
            const PodcastConfig &cfg,
            const Episode &episode,
            const std::string &local_audio_path);

    static PublishResult publishToTransistor(
            const PodcastConfig &cfg,
            const Episode &episode,
            const std::string &local_audio_path);

    static PublishResult publishToPodbean(
            const PodcastConfig &cfg,
            const Episode &episode,
            const std::string &local_audio_path);
};

} // namespace mc1

#endif // MC1_PODCAST_PUBLISHER_H
