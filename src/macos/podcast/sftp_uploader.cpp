/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * podcast/sftp_uploader.cpp — SFTP file upload client (optional libssh2)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "sftp_uploader.h"
#include "../config_types.h"

#ifdef HAVE_LIBSSH2
#include <libssh2.h>
#include <libssh2_sftp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fstream>
#endif

#include <cstring>

namespace mc1 {

bool SftpUploader::isAvailable()
{
#ifdef HAVE_LIBSSH2
    return true;
#else
    return false;
#endif
}

bool SftpUploader::testConnection(const PodcastConfig &cfg, std::string &error_out)
{
#ifdef HAVE_LIBSSH2
    if (cfg.sftp_host.empty()) {
        error_out = "SFTP host is empty";
        return false;
    }

    /* Resolve host */
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    std::string port_str = std::to_string(cfg.sftp_port);
    int rc = getaddrinfo(cfg.sftp_host.c_str(), port_str.c_str(), &hints, &res);
    if (rc != 0 || !res) {
        error_out = "Cannot resolve host: " + cfg.sftp_host;
        return false;
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(res);
        error_out = "Cannot create socket";
        return false;
    }

    rc = connect(sock, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (rc != 0) {
        close(sock);
        error_out = "Cannot connect to " + cfg.sftp_host + ":" + port_str;
        return false;
    }

    /* Init libssh2 session */
    LIBSSH2_SESSION *session = libssh2_session_init();
    if (!session) {
        close(sock);
        error_out = "libssh2_session_init failed";
        return false;
    }

    rc = libssh2_session_handshake(session, sock);
    if (rc) {
        error_out = "SSH handshake failed (rc=" + std::to_string(rc) + ")";
        libssh2_session_disconnect(session, "test");
        libssh2_session_free(session);
        close(sock);
        return false;
    }

    /* Authenticate */
    bool auth_ok = false;
    if (!cfg.sftp_key_path.empty()) {
        rc = libssh2_userauth_publickey_fromfile(session,
                cfg.sftp_username.c_str(),
                nullptr, /* public key — auto-derived from private */
                cfg.sftp_key_path.c_str(),
                cfg.sftp_password.c_str());
        auth_ok = (rc == 0);
    }
    if (!auth_ok && !cfg.sftp_password.empty()) {
        rc = libssh2_userauth_password(session,
                cfg.sftp_username.c_str(),
                cfg.sftp_password.c_str());
        auth_ok = (rc == 0);
    }

    if (!auth_ok) {
        error_out = "Authentication failed for user: " + cfg.sftp_username;
        libssh2_session_disconnect(session, "auth_failed");
        libssh2_session_free(session);
        close(sock);
        return false;
    }

    /* Open SFTP session to verify */
    LIBSSH2_SFTP *sftp = libssh2_sftp_init(session);
    if (!sftp) {
        error_out = "SFTP subsystem init failed";
        libssh2_session_disconnect(session, "sftp_init_failed");
        libssh2_session_free(session);
        close(sock);
        return false;
    }

    /* Success — clean up */
    libssh2_sftp_shutdown(sftp);
    libssh2_session_disconnect(session, "test_complete");
    libssh2_session_free(session);
    close(sock);
    return true;

#else
    error_out = "libssh2 not available — install libssh2 and rebuild";
    return false;
#endif
}

bool SftpUploader::uploadFile(const PodcastConfig &cfg,
                              const std::string &local_path,
                              const std::string &remote_filename,
                              std::string &error_out,
                              ProgressCallback progress)
{
#ifdef HAVE_LIBSSH2
    /* Read local file */
    std::ifstream fin(local_path, std::ios::binary | std::ios::ate);
    if (!fin) {
        error_out = "Cannot open local file: " + local_path;
        return false;
    }
    int64_t file_size = fin.tellg();
    fin.seekg(0);

    /* Connect */
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    std::string port_str = std::to_string(cfg.sftp_port);
    int rc = getaddrinfo(cfg.sftp_host.c_str(), port_str.c_str(), &hints, &res);
    if (rc != 0 || !res) {
        error_out = "Cannot resolve host: " + cfg.sftp_host;
        return false;
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    rc = connect(sock, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (rc != 0) {
        close(sock);
        error_out = "Cannot connect to " + cfg.sftp_host;
        return false;
    }

    LIBSSH2_SESSION *session = libssh2_session_init();
    rc = libssh2_session_handshake(session, sock);
    if (rc) {
        error_out = "SSH handshake failed";
        libssh2_session_free(session);
        close(sock);
        return false;
    }

    /* Auth */
    if (!cfg.sftp_key_path.empty()) {
        rc = libssh2_userauth_publickey_fromfile(session,
                cfg.sftp_username.c_str(), nullptr,
                cfg.sftp_key_path.c_str(), cfg.sftp_password.c_str());
    } else {
        rc = libssh2_userauth_password(session,
                cfg.sftp_username.c_str(), cfg.sftp_password.c_str());
    }
    if (rc) {
        error_out = "Auth failed";
        libssh2_session_disconnect(session, "auth_failed");
        libssh2_session_free(session);
        close(sock);
        return false;
    }

    LIBSSH2_SFTP *sftp = libssh2_sftp_init(session);
    if (!sftp) {
        error_out = "SFTP init failed";
        libssh2_session_disconnect(session, "");
        libssh2_session_free(session);
        close(sock);
        return false;
    }

    /* Build remote path */
    std::string remote_path = cfg.sftp_remote_dir;
    if (!remote_path.empty() && remote_path.back() != '/')
        remote_path += '/';
    remote_path += remote_filename;

    /* Open remote file for writing */
    LIBSSH2_SFTP_HANDLE *handle = libssh2_sftp_open(sftp, remote_path.c_str(),
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
        LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR |
        LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH);
    if (!handle) {
        error_out = "Cannot create remote file: " + remote_path;
        libssh2_sftp_shutdown(sftp);
        libssh2_session_disconnect(session, "");
        libssh2_session_free(session);
        close(sock);
        return false;
    }

    /* Upload in chunks */
    char buf[32768];
    int64_t sent = 0;
    while (fin) {
        fin.read(buf, sizeof(buf));
        auto n = fin.gcount();
        if (n <= 0) break;

        ssize_t written = 0;
        while (written < n) {
            auto w = libssh2_sftp_write(handle, buf + written, n - written);
            if (w < 0) {
                error_out = "SFTP write error";
                libssh2_sftp_close(handle);
                libssh2_sftp_shutdown(sftp);
                libssh2_session_disconnect(session, "");
                libssh2_session_free(session);
                close(sock);
                return false;
            }
            written += w;
        }
        sent += n;
        if (progress) progress(sent, file_size);
    }

    libssh2_sftp_close(handle);
    libssh2_sftp_shutdown(sftp);
    libssh2_session_disconnect(session, "upload_complete");
    libssh2_session_free(session);
    close(sock);
    return true;

#else
    (void)cfg; (void)local_path; (void)remote_filename; (void)progress;
    error_out = "libssh2 not available — install libssh2 and rebuild";
    return false;
#endif
}

bool SftpUploader::uploadRss(const PodcastConfig &cfg, std::string &error_out)
{
    std::string rss_path = cfg.rss_output_dir;
    if (rss_path.empty()) {
        error_out = "RSS output dir not set";
        return false;
    }
    if (rss_path.back() != '/') rss_path += '/';
    rss_path += "feed.xml";

    return uploadFile(cfg, rss_path, "feed.xml", error_out);
}

bool SftpUploader::uploadEpisode(const PodcastConfig &cfg,
                                 const std::string &local_audio_path,
                                 std::string &error_out,
                                 ProgressCallback progress)
{
    /* Extract filename from path */
    std::string filename = local_audio_path;
    auto slash = filename.rfind('/');
    if (slash != std::string::npos)
        filename = filename.substr(slash + 1);

    /* Upload the audio file */
    if (!uploadFile(cfg, local_audio_path, filename, error_out, progress))
        return false;

    /* Then upload the updated RSS feed */
    return uploadRss(cfg, error_out);
}

} // namespace mc1
