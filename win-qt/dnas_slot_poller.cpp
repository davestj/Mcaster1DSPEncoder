/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * dnas_slot_poller.cpp — Per-slot DNAS/Icecast2 stats polling
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dnas_slot_poller.h"
#include "dnas_stats.h"

#include <cstdio>
#include <ctime>
#include <chrono>
#include <regex>

namespace mc1 {

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
DnasSlotPoller::DnasSlotPoller() = default;

DnasSlotPoller::~DnasSlotPoller()
{
    stop();
}

// ---------------------------------------------------------------------------
// register_slot / unregister_slot
// ---------------------------------------------------------------------------
void DnasSlotPoller::register_slot(int slot_id, const StreamTarget& target)
{
    std::lock_guard<std::mutex> lk(mtx_);
    SlotReg reg;
    reg.slot_id        = slot_id;
    reg.host           = target.host;
    reg.port           = target.port;
    reg.mount          = target.mount;
    reg.admin_username = target.admin_username;
    reg.admin_password = target.admin_password;
    reg.protocol       = target.protocol;

    /* Auto-detect SSL from port: common HTTPS ports for streaming servers */
    reg.ssl = (target.port == 443  || target.port == 8443 ||
               target.port == 9443 || target.port == 8243);

    slots_[slot_id] = reg;
}

void DnasSlotPoller::unregister_slot(int slot_id)
{
    std::lock_guard<std::mutex> lk(mtx_);
    slots_.erase(slot_id);
    slot_stats_.erase(slot_id);
}

// ---------------------------------------------------------------------------
// start / stop
// ---------------------------------------------------------------------------
void DnasSlotPoller::start(int interval_sec)
{
    stop();
    interval_sec_ = interval_sec;
    stop_.store(false);
    thread_ = std::thread(&DnasSlotPoller::poll_loop, this);
}

void DnasSlotPoller::stop()
{
    stop_.store(true);
    if (thread_.joinable()) thread_.join();
}

// ---------------------------------------------------------------------------
// get_mount_stats — thread-safe read
// ---------------------------------------------------------------------------
MountStats DnasSlotPoller::get_mount_stats(int slot_id) const
{
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = slot_stats_.find(slot_id);
    if (it != slot_stats_.end())
        return it->second;
    return {};
}

// ---------------------------------------------------------------------------
// poll_loop — background thread
// ---------------------------------------------------------------------------
void DnasSlotPoller::poll_loop()
{
    while (!stop_.load()) {
        poll_once();

        // Sleep in 100ms increments for responsive stop
        for (int i = 0; i < interval_sec_ * 10 && !stop_.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// ---------------------------------------------------------------------------
// poll_once — one poll cycle
// ---------------------------------------------------------------------------
void DnasSlotPoller::poll_once()
{
    // Snapshot registrations under lock
    std::map<int, SlotReg> regs;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        regs = slots_;
    }

    if (regs.empty()) return;

    // Group slots by server (host:port), pick admin credentials from first slot
    struct ServerGroup {
        std::string             host;
        uint16_t                port;
        std::string             admin_username;
        std::string             admin_password;
        bool                    ssl;
        StreamTarget::Protocol  protocol;
        std::vector<std::pair<int, std::string>> slot_mounts; // slot_id → mount
    };

    std::map<ServerKey, ServerGroup> groups;
    for (const auto& [sid, reg] : regs) {
        ServerKey key{reg.host, reg.port};
        auto& grp = groups[key];
        if (grp.slot_mounts.empty()) {
            // First slot for this server — set connection info
            grp.host           = reg.host;
            grp.port           = reg.port;
            grp.admin_username = reg.admin_username;
            grp.admin_password = reg.admin_password;
            grp.ssl            = reg.ssl;
            grp.protocol       = reg.protocol;
        }
        grp.slot_mounts.emplace_back(sid, reg.mount);
    }

    // Fetch once per unique server
    for (const auto& [key, grp] : groups) {
        std::string path = stats_path(grp.protocol);

        /* Skip servers with no admin credentials configured */
        if (grp.admin_password.empty()) {
            fprintf(stderr, "[DnasSlotPoller] Skipping %s:%d — no admin password set\n",
                    grp.host.c_str(), grp.port);
            continue;
        }

        int status = 0;
        std::string body;
        bool ok = DnasStats::http_get(grp.host, grp.port, path,
                                      grp.admin_username, grp.admin_password,
                                      grp.ssl, 10, status, body);

        if (!ok || status != 200) {
            fprintf(stderr, "[DnasSlotPoller] Failed to fetch stats from %s:%d%s (status=%d)\n",
                    grp.host.c_str(), grp.port, path.c_str(), status);
            continue;
        }

        // Parse per-mount XML blocks
        auto mounts = parse_mounts(body);

        // Also parse global listener count as fallback for single-mount servers
        int global_listeners = DnasStats::parse_listeners(body);

        // Distribute per-mount stats to the corresponding slots
        std::lock_guard<std::mutex> lk(mtx_);
        for (const auto& [sid, mount] : grp.slot_mounts) {
            auto mit = mounts.find(mount);
            if (mit != mounts.end()) {
                slot_stats_[sid] = mit->second;
            } else {
                // Fallback: if no per-mount block found but we have global listeners,
                // assign global count (single-mount server case)
                MountStats ms;
                ms.listeners = global_listeners;
                slot_stats_[sid] = ms;
            }
        }

        // Update server cache
        ServerCache& cache = server_cache_[key];
        cache.mounts = mounts;
        cache.polled_at = std::time(nullptr);
    }
}

// ---------------------------------------------------------------------------
// stats_path — choose admin endpoint by protocol
// ---------------------------------------------------------------------------
std::string DnasSlotPoller::stats_path(StreamTarget::Protocol proto)
{
    switch (proto) {
        case StreamTarget::Protocol::MCASTER1_DNAS:
            return "/admin/mcaster1stats.xml";
        case StreamTarget::Protocol::ICECAST2:
        case StreamTarget::Protocol::LIVE365:
            return "/admin/stats.xml";
        case StreamTarget::Protocol::SHOUTCAST_V1:
        case StreamTarget::Protocol::SHOUTCAST_V2:
            return "/statistics";
        case StreamTarget::Protocol::YOUTUBE:
        case StreamTarget::Protocol::TWITCH:
            return "";  /* RTMP-based — no stats endpoint */
    }
    return "/admin/stats.xml";
}

// ---------------------------------------------------------------------------
// parse_mounts — extract <source mount="/path">...</source> blocks
// ---------------------------------------------------------------------------
static uint64_t parse_uint64(const std::string& block, const std::string& tag)
{
    try {
        std::regex re("<" + tag + ">(\\d+)</" + tag + ">", std::regex::icase);
        std::smatch m;
        if (std::regex_search(block, m, re))
            return std::stoull(m[1].str());
    } catch (...) {}
    return 0;
}

std::map<std::string, MountStats> DnasSlotPoller::parse_mounts(const std::string& body)
{
    std::map<std::string, MountStats> result;

    try {
        // Match: <source mount="/path">...inner XML...</source>
        std::regex src_re(R"re(<source\s+mount="([^"]+)">([\s\S]*?)</source>)re",
                          std::regex::ECMAScript);

        auto it  = std::sregex_iterator(body.begin(), body.end(), src_re);
        auto end = std::sregex_iterator();

        for (; it != end; ++it) {
            std::string mount = (*it)[1].str();
            std::string block = (*it)[2].str();

            MountStats ms;
            /* Core stats (Icecast2 + DNAS) */
            ms.listeners            = parse_int(block, "listeners");
            ms.listener_peak        = parse_int(block, "listener_peak");
            ms.listener_connections = parse_int(block, "listener_connections");
            ms.bitrate              = parse_int(block, "bitrate");
            ms.incoming_kbps        = parse_int(block, "incoming_bitrate");
            ms.out_kbps             = parse_int(block, "outgoing_kbitrate");
            ms.connected_sec        = parse_int(block, "connected");
            ms.slow_listeners       = parse_int(block, "slow_listeners");
            ms.queue_size           = parse_int(block, "queue_size");
            ms.total_bytes_read     = parse_uint64(block, "total_bytes_read");
            ms.total_bytes_sent     = parse_uint64(block, "total_bytes_sent");

            ms.title            = parse_str(block, "title");
            ms.server_name      = parse_str(block, "server_name");
            ms.server_type      = parse_str(block, "server_type");
            ms.genre            = parse_str(block, "genre");
            ms.listen_url       = parse_str(block, "listenurl");
            ms.source_ip        = parse_str(block, "source_ip");
            ms.stream_start     = parse_str(block, "stream_start");
            ms.metadata_updated = parse_str(block, "metadata_updated");
            ms.user_agent       = parse_str(block, "user_agent");
            ms.max_listeners    = parse_str(block, "max_listeners");

            /* ICY 2.2 extended fields (Mcaster1 DNAS) */
            ms.icy2_track_title     = parse_str(block, "icy2-track-title");
            ms.icy2_show_title      = parse_str(block, "icy2-show-title");
            ms.icy2_show_season     = parse_str(block, "icy2-show-season");
            ms.icy2_show_episode    = parse_str(block, "icy2-show-episode");
            ms.icy2_station_slogan  = parse_str(block, "icy2-station-slogan");
            ms.icy2_playlist_name   = parse_str(block, "icy2-playlist-name");
            ms.icy2_share_url       = parse_str(block, "icy2-share-url");
            ms.icy2_chat_url        = parse_str(block, "icy2-chat-url");
            ms.icy2_tip_url         = parse_str(block, "icy2-tip-url");
            ms.icy2_request_url     = parse_str(block, "icy2-request-url");
            ms.icy2_dj_handle       = parse_str(block, "icy2-dj-handle");
            ms.icy2_station_logo    = parse_str(block, "icy2-station-logo");
            ms.icy2_station_id      = parse_str(block, "icy2-station-id");
            ms.icy2_podcast_rss     = parse_str(block, "icy2-podcast-rss");
            ms.icy2_podcast_host    = parse_str(block, "icy2-podcast-host");
            ms.icy2_podcast_episode = parse_str(block, "icy2-podcast-episode");
            ms.icy2_version         = parse_str(block, "icy2-version");
            ms.mount_type           = parse_str(block, "mount_type");

            /* Social media links */
            ms.icy2_social_twitter  = parse_str(block, "icy2-social-twitter");
            ms.icy2_social_facebook = parse_str(block, "icy2-social-facebook");
            ms.icy2_social_ig       = parse_str(block, "icy2-social-ig");
            ms.icy2_social_youtube  = parse_str(block, "icy2-social-youtube");
            ms.icy2_social_tiktok   = parse_str(block, "icy2-social-tiktok");
            ms.icy2_social_bluesky  = parse_str(block, "icy2-social-bluesky");

            /* Use icy2-track-title if <title> is empty */
            if (ms.title.empty() && !ms.icy2_track_title.empty())
                ms.title = ms.icy2_track_title;

            result[mount] = ms;
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "[DnasSlotPoller] XML parse error: %s\n", e.what());
    }

    return result;
}

// ---------------------------------------------------------------------------
// parse_int / parse_str — extract single XML tag values
// ---------------------------------------------------------------------------
int DnasSlotPoller::parse_int(const std::string& block, const std::string& tag)
{
    try {
        std::regex re("<" + tag + ">(\\d+)</" + tag + ">", std::regex::icase);
        std::smatch m;
        if (std::regex_search(block, m, re))
            return std::stoi(m[1].str());
    } catch (...) {}
    return 0;
}

std::string DnasSlotPoller::parse_str(const std::string& block, const std::string& tag)
{
    try {
        std::regex re("<" + tag + ">([^<]*)</" + tag + ">", std::regex::icase);
        std::smatch m;
        if (std::regex_search(block, m, re))
            return m[1].str();
    } catch (...) {}
    return "";
}

} // namespace mc1
