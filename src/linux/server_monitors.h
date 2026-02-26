// server_monitors.h — Streaming server relay monitor for Mcaster1DSPEncoder
//
// File:    src/linux/server_monitors.h
// Author:  Dave St. John <davestj@gmail.com>
// Date:    2026-02-24
// Purpose: We poll each configured streaming server (Icecast2, Shoutcast v1/v2,
//          Steamcast, Mcaster1DNAS) on a background POSIX thread at user-defined
//          intervals.  We parse global stats + per-mount data + individual listener
//          lists.  We reconcile the listener list with the mcaster1_metrics.
//          listener_sessions DB table (new IPs → INSERT, gone IPs → UPDATE).
//          We also write one row per poll cycle to server_stat_history.
//
// API endpoints registered in http_api.cpp:
//   GET  /api/v1/server_monitors/stats        → all servers (cached)
//   GET  /api/v1/server_monitors/stats?id=N   → one server + per-mount breakdown
//   POST /api/v1/server_monitors/poll         → force immediate re-poll (body: {id:N} or {})
//
// CHANGELOG:
//   2026-02-24  v1.0.0  Initial implementation

#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <optional>
#include <mutex>
#include <thread>
#include <atomic>
#include <ctime>

// ---------------------------------------------------------------------------
// MountStat — per-source/mount statistics
// ---------------------------------------------------------------------------

struct MountStat {
    std::string mount;
    std::string title;
    std::string codec;          // "MP3", "Opus", "Vorbis", "AAC", "FLAC", etc.
    std::string server_name;
    int         listeners  = 0;
    int         peak       = 0;
    int         bitrate    = 0;  // kbps
    int         out_kbps   = 0;
    int         connected_s = 0;
    bool        online     = false;
    bool        ours       = false; // true if an encoder slot targets this mount
};

// ---------------------------------------------------------------------------
// ServerStatCache — cached stats for one configured streaming server
// ---------------------------------------------------------------------------

struct ServerStatCache {
    int         server_id       = 0;
    std::string name;
    std::string server_type;    // "icecast2","shoutcast1","shoutcast2","steamcast","mcaster1dnas"
    std::string host;
    int         port            = 8000;
    std::string status;         // "online","offline","unknown"
    int         listeners_total = 0;
    int         max_listeners   = 0;
    int         sources_total   = 0;
    int         sources_online  = 0;
    int         out_kbps        = 0;
    std::string uptime;
    std::string server_id_str;  // e.g. "Icecast 2.0.2"
    time_t      polled_at       = 0;
    int         fetch_ms        = 0;
    std::string error;
    std::vector<MountStat> mounts;
};

// ---------------------------------------------------------------------------
// ServerMonitor — singleton background poller
// ---------------------------------------------------------------------------

class ServerMonitor {
public:
    static ServerMonitor& instance() {
        static ServerMonitor inst;
        return inst;
    }

    // We start the background polling thread. connect to DB via Mc1Db::instance().
    void start();
    void stop();

    // We return a snapshot of all cached server stats (thread-safe copy).
    std::vector<ServerStatCache> getAll() const;

    // We return cached stats for one server by id.
    std::optional<ServerStatCache> getById(int id) const;

    // We return the total listener count for a server that has a slot targeting
    // the given mount path (used by SystemHealth for per-slot listener count).
    int getListenersByMount(const std::string& mount) const;

    // We request an immediate re-poll. -1 = all servers, otherwise specific id.
    void pollNow(int server_id = -1);

    // Non-copyable
    ServerMonitor(const ServerMonitor&)            = delete;
    ServerMonitor& operator=(const ServerMonitor&) = delete;

private:
    ServerMonitor() = default;
    ~ServerMonitor() { stop(); }

    // ── Server record loaded from DB ────────────────────────────────────────
    struct ServerRecord {
        int         id             = 0;
        std::string name;
        std::string server_type;
        std::string host;
        int         port           = 8000;
        bool        ssl_enabled    = false;
        std::string stat_username;
        std::string stat_password;
        std::string mount_point;
        int         poll_interval_s = 30;
        bool        is_active      = true;
    };

    void run();

    // We load server records from mcaster1_encoder.streaming_servers.
    void load_servers();

    // We load our mount points from mcaster1_encoder.encoder_configs.
    void load_our_mounts();

    // We poll one server synchronously and update the cache.
    void poll_server(const ServerRecord& srv);

    // ── Per-server-type fetch + parse ────────────────────────────────────────
    bool fetch_icecast2  (const ServerRecord& srv, ServerStatCache& out);
    bool fetch_shoutcast1(const ServerRecord& srv, ServerStatCache& out);
    bool fetch_shoutcast2(const ServerRecord& srv, ServerStatCache& out);
    bool fetch_mcaster1  (const ServerRecord& srv, ServerStatCache& out);

    // ── Per-listener list fetch ──────────────────────────────────────────────
    // We fetch the listener list for one mount and return a map<ip, user_agent>.
    std::map<std::string,std::string> fetch_listener_list_icecast(
        const ServerRecord& srv, const std::string& mount);
    std::map<std::string,std::string> fetch_listener_list_sc2(
        const ServerRecord& srv, int sid = 1);

    // ── Listener session reconciliation ─────────────────────────────────────
    void reconcile_listeners(int server_id, const std::string& mount,
                             const std::map<std::string,std::string>& current);

    // ── Helpers ─────────────────────────────────────────────────────────────
    // We classify codec from server_type MIME string.
    static std::string classify_codec(const std::string& server_type,
                                      const std::string& subtype = "");

    // We format uptime seconds into "Xd Xh" style string.
    static std::string format_uptime(int seconds);

    // We make an HTTP(S) GET and return body string. Returns "" on failure.
    std::string http_get(const ServerRecord& srv, const std::string& path,
                         int& ms_out, bool use_auth = true);

    // We extract a regex-captured group from a string.
    static std::string regex_first(const std::string& text, const std::string& pattern);
    static std::vector<std::string> regex_all(const std::string& text,
                                              const std::string& pattern);

    // ── DB writes ────────────────────────────────────────────────────────────
    void write_stat_history(int server_id, const ServerStatCache& c);
    void update_server_last_seen(int server_id, const std::string& status, int listeners);

    // ── State ───────────────────────────────────────────────────────────────
    std::atomic<bool>   running_{ false };
    std::thread         thread_;
    mutable std::mutex  mtx_;
    std::atomic<int>    force_poll_id_{ -2 };  // -2 = no force, -1 = all, >=0 = specific id

    std::vector<ServerRecord>       servers_;
    std::map<int, ServerStatCache>  cache_;     // server_id → cached stats
    std::set<std::string>           our_mounts_; // mount paths from encoder_configs

    // prev_ips_[server_id][mount] = set of IPs seen on last poll
    std::map<int, std::map<std::string, std::set<std::string>>> prev_ips_;
    time_t last_server_reload_ = 0;
};
