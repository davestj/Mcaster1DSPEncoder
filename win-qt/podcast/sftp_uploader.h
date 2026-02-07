/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * podcast/sftp_uploader.h — SFTP file upload client (optional libssh2)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_SFTP_UPLOADER_H
#define MC1_SFTP_UPLOADER_H

#include <functional>
#include <string>
#include <cstdint>

namespace mc1 {

struct PodcastConfig;  // forward

class SftpUploader {
public:
    /* Progress callback: (bytes_sent, total_bytes) */
    using ProgressCallback = std::function<void(int64_t, int64_t)>;

    /* Returns true if libssh2 was compiled in */
    static bool isAvailable();

    /* Test SSH/SFTP connection to the host */
    static bool testConnection(const PodcastConfig &cfg, std::string &error_out);

    /* Upload a single file. Returns true on success. */
    static bool uploadFile(const PodcastConfig &cfg,
                           const std::string &local_path,
                           const std::string &remote_filename,
                           std::string &error_out,
                           ProgressCallback progress = nullptr);

    /* Upload the RSS feed.xml to the remote dir */
    static bool uploadRss(const PodcastConfig &cfg, std::string &error_out);

    /* Upload an episode audio file + update RSS */
    static bool uploadEpisode(const PodcastConfig &cfg,
                              const std::string &local_audio_path,
                              std::string &error_out,
                              ProgressCallback progress = nullptr);
};

} // namespace mc1

#endif // MC1_SFTP_UPLOADER_H
