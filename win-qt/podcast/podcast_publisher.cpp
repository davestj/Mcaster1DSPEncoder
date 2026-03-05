/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * podcast/podcast_publisher.cpp — Multi-destination podcast episode publisher
 *
 * Phase M8: Real REST API implementations via libcurl HttpClient.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "podcast_publisher.h"
#include "rss_generator.h"
#include "sftp_uploader.h"
#include "http_client.h"
#include "../config_types.h"

#include <cstdio>
#include <sstream>

/*
 * JSON helpers (minimal — no external dependency like nlohmann/json).
 * These parse simple flat JSON objects. For nested data we use string search.
 */
namespace {

/* Extract a string value from flat JSON: {"key":"value", ...} */
std::string json_str(const std::string& json, const std::string& key)
{
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return {};

    /* Find colon, skip whitespace */
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return {};
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;

    if (pos >= json.size() || json[pos] != '"') return {};
    ++pos; /* skip opening quote */

    std::string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            ++pos; /* skip escaped char */
        }
        result += json[pos++];
    }
    return result;
}

/* Extract an integer value from flat JSON: {"key": 123, ...} */
int json_int(const std::string& json, const std::string& key)
{
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return 0;

    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return 0;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;

    return std::atoi(json.c_str() + pos);
}

/* URL-encode a string (simple: encode everything except alphanumerics) */
std::string url_encode(const std::string& s)
{
    std::string result;
    result.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            result += static_cast<char>(c);
        } else {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", c);
            result += hex;
        }
    }
    return result;
}

/* Extract filename from path */
std::string filename_from_path(const std::string& path)
{
    auto slash = path.rfind('/');
    return (slash != std::string::npos) ? path.substr(slash + 1) : path;
}

/* Get file size */
int64_t file_size(const std::string& path)
{
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) return 0;
    fseek(fp, 0, SEEK_END);
    int64_t sz = ftell(fp);
    fclose(fp);
    return sz;
}

} // anonymous namespace

namespace mc1 {

std::vector<PublishResult> PodcastPublisher::publishEpisode(
        const PodcastConfig &cfg,
        const Episode &episode,
        const std::string &local_audio_path,
        ProgressCallback progress)
{
    std::vector<PublishResult> results;

    /* 1. SFTP upload (if enabled) */
    if (cfg.sftp_enabled) {
        results.push_back(publishToSftp(cfg, local_audio_path, progress));
    }

    /* 2. WordPress REST API (if configured) */
    if (cfg.wordpress.enabled) {
        results.push_back(publishToWordPress(cfg, episode, local_audio_path));
    }

    /* 3. Buzzsprout API (if configured) */
    if (cfg.buzzsprout.enabled) {
        results.push_back(publishToBuzzsprout(cfg, episode, local_audio_path));
    }

    /* 4. Transistor.fm API (if configured) */
    if (cfg.transistor.enabled) {
        results.push_back(publishToTransistor(cfg, episode, local_audio_path));
    }

    /* 5. Podbean API (if configured) */
    if (cfg.podbean.enabled) {
        results.push_back(publishToPodbean(cfg, episode, local_audio_path));
    }

    return results;
}

PublishResult PodcastPublisher::publishRssFeed(const PodcastConfig &cfg)
{
    PublishResult result;
    result.destination = "RSS Feed";

    if (!cfg.sftp_enabled) {
        result.success = true;
        result.url = cfg.rss_output_dir + "/feed.xml";
        return result;
    }

    std::string err;
    if (SftpUploader::uploadRss(cfg, err)) {
        result.success = true;
        result.url = cfg.sftp_remote_dir + "/feed.xml";
    } else {
        result.error = err;
    }
    return result;
}

PublishResult PodcastPublisher::publishToSftp(
        const PodcastConfig &cfg,
        const std::string &local_audio_path,
        ProgressCallback progress)
{
    PublishResult result;
    result.destination = "SFTP";

    auto sftp_progress = [&progress](int64_t sent, int64_t total) {
        if (progress) progress("SFTP", sent, total);
    };

    std::string err;
    if (SftpUploader::uploadEpisode(cfg, local_audio_path, err, sftp_progress)) {
        result.success = true;
        std::string filename = filename_from_path(local_audio_path);
        result.url = cfg.sftp_remote_dir + "/" + filename;
    } else {
        result.error = err;
    }
    return result;
}

/* ── WordPress REST API ──────────────────────────────────────────────────── */

PublishResult PodcastPublisher::publishToWordPress(
        const PodcastConfig &cfg,
        const Episode &episode,
        const std::string &local_audio_path)
{
    PublishResult result;
    result.destination = "WordPress";

    if (cfg.wordpress.site_url.empty()) {
        result.error = "WordPress site URL not configured";
        return result;
    }

#ifdef HAVE_LIBCURL
    if (!HttpClient::isAvailable()) {
        result.error = "libcurl not available";
        return result;
    }

    std::string base_url = cfg.wordpress.site_url;
    /* Strip trailing slash */
    while (!base_url.empty() && base_url.back() == '/') base_url.pop_back();

    std::string auth = HttpClient::basic_auth_header(
        cfg.wordpress.username, cfg.wordpress.app_password);

    /* Step 1: Upload audio file as media attachment */
    std::string filename = filename_from_path(local_audio_path);
    std::string media_url = base_url + "/wp-json/wp/v2/media";

    HttpClient::Headers media_headers;
    media_headers["Authorization"] = auth;
    media_headers["Content-Disposition"] =
        "attachment; filename=\"" + filename + "\"";

    auto media_resp = HttpClient::put_file(
        media_url, local_audio_path, episode.mime_type, media_headers);

    if (!media_resp.ok()) {
        result.error = "Media upload failed: " +
            (media_resp.error.empty()
                ? "HTTP " + std::to_string(media_resp.status_code) + " — " + media_resp.body
                : media_resp.error);
        return result;
    }

    /* Parse media ID from response */
    int media_id = json_int(media_resp.body, "id");
    std::string source_url = json_str(media_resp.body, "source_url");

    if (media_id <= 0) {
        result.error = "Failed to parse media ID from WordPress response";
        return result;
    }

    /* Step 2: Create post with audio attachment */
    std::string post_type = cfg.wordpress.post_type.empty()
        ? "posts" : cfg.wordpress.post_type + "s"; /* "post" -> "posts" */
    if (post_type == "posts" || post_type == "podcasts") {
        /* standard pluralization */
    }

    std::string post_url = base_url + "/wp-json/wp/v2/" + post_type;

    /* Build JSON body */
    std::ostringstream json;
    json << "{"
         << "\"title\":\"" << episode.title << "\","
         << "\"content\":\"" << episode.description << "\","
         << "\"status\":\"publish\","
         << "\"featured_media\":" << media_id
         << "}";

    HttpClient::Headers post_headers;
    post_headers["Authorization"] = auth;

    auto post_resp = HttpClient::post_json(post_url, json.str(), post_headers);

    if (!post_resp.ok()) {
        result.error = "Post creation failed: HTTP " +
            std::to_string(post_resp.status_code);
        return result;
    }

    result.success = true;
    result.url = json_str(post_resp.body, "link");
    if (result.url.empty()) result.url = source_url;

#else
    (void)episode;
    (void)local_audio_path;
    result.error = "libcurl not compiled in — install via: brew install curl";
#endif

    return result;
}

/* ── Buzzsprout API ──────────────────────────────────────────────────────── */

PublishResult PodcastPublisher::publishToBuzzsprout(
        const PodcastConfig &cfg,
        const Episode &episode,
        const std::string &local_audio_path)
{
    PublishResult result;
    result.destination = "Buzzsprout";

    if (cfg.buzzsprout.api_token.empty() || cfg.buzzsprout.podcast_id.empty()) {
        result.error = "Buzzsprout API token or podcast ID not configured";
        return result;
    }

#ifdef HAVE_LIBCURL
    if (!HttpClient::isAvailable()) {
        result.error = "libcurl not available";
        return result;
    }

    /* Single multipart POST with audio file + metadata */
    std::string url = "https://www.buzzsprout.com/api/" +
        cfg.buzzsprout.podcast_id + "/episodes.json";

    HttpClient::Headers headers;
    headers["Authorization"] = "Token token=" + cfg.buzzsprout.api_token;

    std::vector<HttpClient::MultipartField> fields;
    fields.push_back({"title", episode.title, {}, {}, {}});
    fields.push_back({"description", episode.description, {}, {}, {}});
    fields.push_back({"private", "false", {}, {}, {}});

    /* Audio file */
    std::string filename = filename_from_path(local_audio_path);
    fields.push_back({"audio_file", {}, local_audio_path,
                       episode.mime_type, filename});

    auto resp = HttpClient::post_multipart(url, fields, headers);

    if (!resp.ok()) {
        result.error = "Buzzsprout upload failed: " +
            (resp.error.empty()
                ? "HTTP " + std::to_string(resp.status_code) + " — " + resp.body
                : resp.error);
        return result;
    }

    result.success = true;
    result.url = json_str(resp.body, "audio_url");

#else
    (void)episode;
    (void)local_audio_path;
    result.error = "libcurl not compiled in — install via: brew install curl";
#endif

    return result;
}

/* ── Transistor.fm API ───────────────────────────────────────────────────── */

PublishResult PodcastPublisher::publishToTransistor(
        const PodcastConfig &cfg,
        const Episode &episode,
        const std::string &local_audio_path)
{
    PublishResult result;
    result.destination = "Transistor.fm";

    if (cfg.transistor.api_key.empty() || cfg.transistor.show_id.empty()) {
        result.error = "Transistor API key or show ID not configured";
        return result;
    }

#ifdef HAVE_LIBCURL
    if (!HttpClient::isAvailable()) {
        result.error = "libcurl not available";
        return result;
    }

    std::string filename = filename_from_path(local_audio_path);

    HttpClient::Headers api_headers;
    api_headers["x-api-key"] = cfg.transistor.api_key;

    /* Step 1: Authorize upload → get presigned S3 URL */
    std::string auth_url = "https://api.transistor.fm/v1/episodes/authorize_upload";
    std::string auth_body = "{\"filename\":\"" + filename + "\"}";

    auto auth_resp = HttpClient::post_json(auth_url, auth_body, api_headers);

    if (!auth_resp.ok()) {
        result.error = "Transistor authorize_upload failed: " +
            (auth_resp.error.empty()
                ? "HTTP " + std::to_string(auth_resp.status_code)
                : auth_resp.error);
        return result;
    }

    /* Parse upload_url from nested JSON response */
    std::string upload_url = json_str(auth_resp.body, "upload_url");
    std::string audio_url  = json_str(auth_resp.body, "content_url");

    if (upload_url.empty()) {
        result.error = "Failed to parse upload_url from Transistor response";
        return result;
    }

    /* Step 2: PUT audio file to S3 presigned URL */
    auto put_resp = HttpClient::put_file(
        upload_url, local_audio_path, episode.mime_type);

    if (!put_resp.ok()) {
        result.error = "S3 upload failed: " +
            (put_resp.error.empty()
                ? "HTTP " + std::to_string(put_resp.status_code)
                : put_resp.error);
        return result;
    }

    /* Step 3: Create episode with audio_url reference */
    std::string ep_url = "https://api.transistor.fm/v1/episodes";

    std::ostringstream ep_json;
    ep_json << "{"
            << "\"episode\":{"
            << "\"show_id\":\"" << cfg.transistor.show_id << "\","
            << "\"title\":\"" << episode.title << "\","
            << "\"description\":\"" << episode.description << "\","
            << "\"audio_url\":\"" << (audio_url.empty() ? upload_url : audio_url) << "\""
            << "}}";

    auto ep_resp = HttpClient::post_json(ep_url, ep_json.str(), api_headers);

    if (!ep_resp.ok()) {
        result.error = "Episode creation failed: HTTP " +
            std::to_string(ep_resp.status_code);
        return result;
    }

    result.success = true;
    result.url = json_str(ep_resp.body, "share_url");

#else
    (void)episode;
    (void)local_audio_path;
    result.error = "libcurl not compiled in — install via: brew install curl";
#endif

    return result;
}

/* ── Podbean API (OAuth 2.0) ─────────────────────────────────────────────── */

PublishResult PodcastPublisher::publishToPodbean(
        const PodcastConfig &cfg,
        const Episode &episode,
        const std::string &local_audio_path)
{
    PublishResult result;
    result.destination = "Podbean";

    if (cfg.podbean.client_id.empty() || cfg.podbean.client_secret.empty()) {
        result.error = "Podbean client ID or secret not configured";
        return result;
    }

#ifdef HAVE_LIBCURL
    if (!HttpClient::isAvailable()) {
        result.error = "libcurl not available";
        return result;
    }

    std::string filename = filename_from_path(local_audio_path);
    int64_t fsize = file_size(local_audio_path);

    /* Step 1: OAuth token (client_credentials grant) */
    std::string token_url = "https://api.podbean.com/v1/oauth/token";
    std::string token_body =
        "grant_type=client_credentials"
        "&client_id=" + url_encode(cfg.podbean.client_id) +
        "&client_secret=" + url_encode(cfg.podbean.client_secret);

    auto token_resp = HttpClient::post_form(token_url, token_body);

    if (!token_resp.ok()) {
        result.error = "Podbean OAuth failed: " +
            (token_resp.error.empty()
                ? "HTTP " + std::to_string(token_resp.status_code)
                : token_resp.error);
        return result;
    }

    std::string access_token = json_str(token_resp.body, "access_token");
    if (access_token.empty()) {
        result.error = "Failed to parse access_token from Podbean";
        return result;
    }

    HttpClient::Headers bearer;
    bearer["Authorization"] = "Bearer " + access_token;

    /* Step 2: Upload authorize → get presigned URL + file_key */
    std::string auth_url = "https://api.podbean.com/v1/files/uploadAuthorize";
    std::string auth_body =
        "filename=" + url_encode(filename) +
        "&filesize=" + std::to_string(fsize) +
        "&content_type=" + url_encode(episode.mime_type);

    auto auth_resp = HttpClient::post_form(auth_url, auth_body, bearer);

    if (!auth_resp.ok()) {
        result.error = "Podbean uploadAuthorize failed: " +
            (auth_resp.error.empty()
                ? "HTTP " + std::to_string(auth_resp.status_code)
                : auth_resp.error);
        return result;
    }

    std::string presigned_url = json_str(auth_resp.body, "presigned_url");
    std::string file_key      = json_str(auth_resp.body, "file_key");

    if (presigned_url.empty() || file_key.empty()) {
        result.error = "Failed to parse presigned_url/file_key from Podbean";
        return result;
    }

    /* Step 3: PUT audio file to presigned S3 URL */
    auto put_resp = HttpClient::put_file(
        presigned_url, local_audio_path, episode.mime_type);

    if (!put_resp.ok()) {
        result.error = "Podbean S3 upload failed: " +
            (put_resp.error.empty()
                ? "HTTP " + std::to_string(put_resp.status_code)
                : put_resp.error);
        return result;
    }

    /* Step 4: Create episode */
    std::string ep_url = "https://api.podbean.com/v1/episodes";
    std::string ep_body =
        "title=" + url_encode(episode.title) +
        "&content=" + url_encode(episode.description) +
        "&status=publish"
        "&type=public"
        "&media_key=" + url_encode(file_key);

    auto ep_resp = HttpClient::post_form(ep_url, ep_body, bearer);

    if (!ep_resp.ok()) {
        result.error = "Podbean episode creation failed: HTTP " +
            std::to_string(ep_resp.status_code);
        return result;
    }

    result.success = true;
    result.url = json_str(ep_resp.body, "permalink_url");

#else
    (void)episode;
    (void)local_audio_path;
    result.error = "libcurl not compiled in — install via: brew install curl";
#endif

    return result;
}

} // namespace mc1
