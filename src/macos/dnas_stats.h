// dnas_stats.h — Mcaster1DNAS XML stats fetcher
// Phase M4 — Mcaster1DSPEncoder macOS Qt 6 Build
//
// Fetches XML stats from a Mcaster1DNAS / Icecast2 server admin endpoint
// and parses listener count + basic stream info for display in the GUI.
#pragma once

#include "config_types.h"
#include <string>
#include <cstdint>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>

namespace mc1 {

// ---------------------------------------------------------------------------
// DnasStatsResult — parsed stats from one DNAS fetch
// ---------------------------------------------------------------------------
struct DnasStatsResult {
    bool        ok             = false;   // true if fetch + parse succeeded
    int         http_status    = 0;
    int         listeners      = 0;
    int         max_listeners  = 0;
    std::string raw_body;                 // raw XML/JSON body from server
    std::string source_label;             // "host:port/path"
    std::string error;                    // error message if !ok
};

// ---------------------------------------------------------------------------
// DnasStats — fetches DNAS/Icecast stats on a background timer
// ---------------------------------------------------------------------------
class DnasStats {
public:
    using Callback = std::function<void(const DnasStatsResult&)>;

    explicit DnasStats(const DnasConfig& cfg);
    ~DnasStats();

    // One-shot synchronous fetch (blocks until result or timeout)
    DnasStatsResult fetch();

    // Start/stop periodic polling (calls cb on every poll result)
    void start_polling(int interval_sec, Callback cb);
    void stop_polling();

    bool is_polling() const { return polling_.load(); }

    // Access last result
    DnasStatsResult last_result() const;

private:
    DnasConfig  cfg_;
    mutable std::mutex result_mutex_;
    DnasStatsResult    last_result_;

    // Polling thread
    std::thread         poll_thread_;
    std::atomic<bool>   polling_{false};
    std::atomic<bool>   poll_stop_{false};
    int                 poll_interval_sec_ = 15;
    Callback            poll_cb_;

    void poll_loop();

public:
    // Low-level HTTP GET via raw TCP (plain HTTP or SSL via SecureTransport).
    // Public so DnasSlotPoller can reuse for per-slot stats fetching.
    static bool http_get(const std::string& host, uint16_t port,
                         const std::string& path,
                         const std::string& username,
                         const std::string& password,
                         bool use_ssl,
                         int timeout_sec,
                         int& out_status,
                         std::string& out_body);

    // Parse listener count from DNAS XML body
    static int parse_listeners(const std::string& body);
    static int parse_max_listeners(const std::string& body);

private:
};

} // namespace mc1
