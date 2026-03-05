/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * dnas_slot_poller.h — Per-slot DNAS/Icecast2 stats polling
 *
 * Polls /admin/mcaster1stats (DNAS) or /admin/stats (Icecast2)
 * on each server that encoder slots are streaming to.
 * Deduplicates fetches by host:port — one HTTP GET per server per cycle.
 * Parses per-mount <source mount="/path"> blocks and distributes
 * listener counts to the corresponding encoder slots.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "config_types.h"
#include <string>
#include <cstdint>
#include <map>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>

namespace mc1 {

/* Per-mount stats parsed from DNAS XML */
struct MountStats {
    int         listeners      = 0;
    int         listener_peak  = 0;
    int         bitrate        = 0;
    int         out_kbps       = 0;
    int         connected_sec  = 0;
    std::string title;
    std::string server_name;
};

class DnasSlotPoller {
public:
    DnasSlotPoller();
    ~DnasSlotPoller();

    /* Register a slot for polling (call on add_slot / start_slot) */
    void register_slot(int slot_id, const StreamTarget& target);

    /* Unregister a slot (call on remove_slot / stop_slot) */
    void unregister_slot(int slot_id);

    /* Start/stop the background poll thread */
    void start(int interval_sec = 15);
    void stop();

    /* Thread-safe: get per-mount stats for a specific slot.
     * Returns a zero-initialized MountStats if not available. */
    MountStats get_mount_stats(int slot_id) const;

private:
    /* Registration info for one slot */
    struct SlotReg {
        int                     slot_id;
        std::string             host;
        uint16_t                port;
        std::string             mount;
        std::string             username;
        std::string             password;
        bool                    ssl;
        StreamTarget::Protocol  protocol;
    };

    /* Cache key: host + port */
    struct ServerKey {
        std::string host;
        uint16_t    port;
        bool operator<(const ServerKey& o) const {
            if (host != o.host) return host < o.host;
            return port < o.port;
        }
    };

    /* Cached server response */
    struct ServerCache {
        std::map<std::string, MountStats> mounts;  // mount path → stats
        time_t                            polled_at = 0;
    };

    mutable std::mutex          mtx_;
    std::map<int, SlotReg>      slots_;          // slot_id → registration
    std::map<int, MountStats>   slot_stats_;     // slot_id → latest stats
    std::map<ServerKey, ServerCache> server_cache_;

    std::thread                 thread_;
    std::atomic<bool>           stop_{false};
    int                         interval_sec_ = 15;

    void poll_loop();
    void poll_once();

    /* Choose the stats endpoint path based on protocol */
    static std::string stats_path(StreamTarget::Protocol proto);

    /* Parse XML body for per-mount blocks */
    static std::map<std::string, MountStats> parse_mounts(const std::string& body);

    /* Parse a single XML tag integer value from a block */
    static int parse_int(const std::string& block, const std::string& tag);

    /* Parse a single XML tag string value from a block */
    static std::string parse_str(const std::string& block, const std::string& tag);
};

} // namespace mc1
