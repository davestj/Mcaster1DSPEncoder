// server_monitors.cpp — ServerMonitor implementation
//
// File:    src/linux/server_monitors.cpp
// Author:  Dave St. John <davestj@gmail.com>
// Date:    2026-02-24
// Purpose: See server_monitors.h
//
// CHANGELOG:
//   2026-02-24  v1.0.0  Initial implementation

#include "server_monitors.h"
#include "mc1_db.h"
#include "mc1_logger.h"
#include "../libmcaster1dspencoder/libmcaster1dspencoder.h"

// We use httplib (header-only) and nlohmann::json (header-only) — both already
// in the Linux vendor include path: src/linux/external/include/
// CPPHTTPLIB_OPENSSL_SUPPORT is already defined via AM_CPPFLAGS; suppress redefinition.
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#  define CPPHTTPLIB_OPENSSL_SUPPORT
#endif
#include "external/include/httplib.h"
#include "external/include/nlohmann/json.hpp"

#include <mysql.h>
#include <regex>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <cstring>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// start / stop
// ---------------------------------------------------------------------------

void ServerMonitor::start()
{
    if (running_.load()) return;
    running_.store(true);
    thread_ = std::thread([this]() { run(); });
    MC1_INFO("ServerMonitor: started");
}

void ServerMonitor::stop()
{
    running_.store(false);
    if (thread_.joinable()) thread_.join();
}

// ---------------------------------------------------------------------------
// getAll / getById / getListenersByMount / pollNow
// ---------------------------------------------------------------------------

std::vector<ServerStatCache> ServerMonitor::getAll() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<ServerStatCache> out;
    for (const auto& [id, c] : cache_) out.push_back(c);
    return out;
}

std::optional<ServerStatCache> ServerMonitor::getById(int id) const
{
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = cache_.find(id);
    if (it == cache_.end()) return std::nullopt;
    return it->second;
}

int ServerMonitor::getListenersByMount(const std::string& mount) const
{
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto& [id, c] : cache_) {
        for (const auto& m : c.mounts) {
            if (m.mount == mount) return m.listeners;
        }
    }
    return 0;
}

void ServerMonitor::pollNow(int server_id)
{
    force_poll_id_.store(server_id);
}

// ---------------------------------------------------------------------------
// run — main polling loop
// ---------------------------------------------------------------------------

void ServerMonitor::run()
{
    // We load servers on first run.
    load_servers();
    load_our_mounts();
    last_server_reload_ = time(nullptr);

    while (running_.load()) {
        // We sleep in 500ms slices for responsiveness to stop() and pollNow().
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (!running_.load()) break;

        // We reload servers from DB every 60s to pick up additions/deletions.
        time_t now = time(nullptr);
        if (now - last_server_reload_ >= 60) {
            load_servers();
            load_our_mounts();
            last_server_reload_ = now;
        }

        // We handle a forced poll request.
        int forced = force_poll_id_.exchange(-2);
        bool force_all = (forced == -1);
        int  force_id  = (forced >= 0) ? forced : -1;

        // We iterate servers and poll those whose interval has elapsed (or forced).
        std::vector<ServerRecord> to_poll;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            for (const auto& srv : servers_) {
                if (!srv.is_active) continue;
                if (force_all || force_id == srv.id) {
                    to_poll.push_back(srv);
                    continue;
                }
                auto it = cache_.find(srv.id);
                time_t last = (it != cache_.end()) ? it->second.polled_at : 0;
                if (now - last >= srv.poll_interval_s)
                    to_poll.push_back(srv);
            }
        }

        for (const auto& srv : to_poll) {
            if (!running_.load()) break;
            poll_server(srv);
        }
    }
}

// ---------------------------------------------------------------------------
// load_servers — loads from mcaster1_encoder.streaming_servers
// ---------------------------------------------------------------------------

void ServerMonitor::load_servers()
{
    // We query directly via MariaDB C API using the already-connected Mc1Db.
    // Note: Mc1Db::instance().exec() is fire-and-forget; for SELECT we need
    // the raw MYSQL* pointer.  We access it via a helper query pattern.
    // We embed the SELECT in execf with a PROCEDURE-style approach isn't possible
    // with exec().  Instead we use mysql_query directly via a function we add below.

    // Since Mc1Db doesn't expose a SELECT method (by design — write-only for safety),
    // we use a SEPARATE, short-lived MYSQL* for reads.  We reuse gAdminConfig.db creds.
    MYSQL* mysql = mysql_init(nullptr);
    if (!mysql) return;
    my_bool rc = 1;
    mysql_options(mysql, MYSQL_OPT_RECONNECT, &rc);
    unsigned int to = 5;
    mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, &to);

    const mc1DbConfig& db = gAdminConfig.db;
    if (!mysql_real_connect(mysql, db.host, db.user, db.password,
                            db.db_encoder, (unsigned int)db.port, nullptr, 0)) {
        MC1_WARN("ServerMonitor: DB connect failed: " + std::string(mysql_error(mysql)));
        mysql_close(mysql);
        return;
    }
    mysql_set_character_set(mysql, "utf8mb4");

    const char* sql =
        "SELECT id,name,server_type,host,port,ssl_enabled,stat_username,stat_password,"
        "mount_point,poll_interval_s,is_active FROM streaming_servers ORDER BY display_order,id";

    if (mysql_query(mysql, sql) != 0) {
        MC1_WARN("ServerMonitor: query failed: " + std::string(mysql_error(mysql)));
        mysql_close(mysql);
        return;
    }

    MYSQL_RES* result = mysql_store_result(mysql);
    if (!result) { mysql_close(mysql); return; }

    std::vector<ServerRecord> loaded;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result)) != nullptr) {
        ServerRecord s;
        s.id              = row[0] ? atoi(row[0]) : 0;
        s.name            = row[1] ? row[1] : "";
        s.server_type     = row[2] ? row[2] : "icecast2";
        s.host            = row[3] ? row[3] : "";
        s.port            = row[4] ? atoi(row[4]) : 8000;
        s.ssl_enabled     = row[5] && atoi(row[5]) == 1;
        s.stat_username   = row[6] ? row[6] : "";
        s.stat_password   = row[7] ? row[7] : "";
        s.mount_point     = row[8] ? row[8] : "";
        s.poll_interval_s = row[9] ? std::max(10, atoi(row[9])) : 30;
        s.is_active       = !row[10] || atoi(row[10]) == 1;
        if (s.id > 0 && !s.host.empty()) loaded.push_back(s);
    }
    mysql_free_result(result);
    mysql_close(mysql);

    std::lock_guard<std::mutex> lk(mtx_);
    servers_ = std::move(loaded);
    MC1_INFO("ServerMonitor: loaded " + std::to_string(servers_.size()) + " server(s)");
}

// ---------------------------------------------------------------------------
// load_our_mounts — loads encoder slot mount paths from encoder_configs
// ---------------------------------------------------------------------------

void ServerMonitor::load_our_mounts()
{
    MYSQL* mysql = mysql_init(nullptr);
    if (!mysql) return;
    my_bool rc = 1;
    mysql_options(mysql, MYSQL_OPT_RECONNECT, &rc);
    unsigned int to = 5;
    mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, &to);

    const mc1DbConfig& db = gAdminConfig.db;
    if (!mysql_real_connect(mysql, db.host, db.user, db.password,
                            db.db_encoder, (unsigned int)db.port, nullptr, 0)) {
        mysql_close(mysql);
        return;
    }

    if (mysql_query(mysql, "SELECT server_mount FROM encoder_configs WHERE server_mount IS NOT NULL") != 0) {
        mysql_close(mysql);
        return;
    }
    MYSQL_RES* result = mysql_store_result(mysql);
    if (!result) { mysql_close(mysql); return; }

    std::set<std::string> mounts;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result)) != nullptr) {
        if (row[0] && row[0][0]) {
            std::string m = row[0];
            if (m[0] != '/') m = "/" + m;
            mounts.insert(m);
        }
    }
    mysql_free_result(result);
    mysql_close(mysql);

    std::lock_guard<std::mutex> lk(mtx_);
    our_mounts_ = std::move(mounts);
}

// ---------------------------------------------------------------------------
// poll_server — polls one server and updates cache
// ---------------------------------------------------------------------------

void ServerMonitor::poll_server(const ServerRecord& srv)
{
    ServerStatCache c;
    c.server_id   = srv.id;
    c.name        = srv.name;
    c.server_type = srv.server_type;
    c.host        = srv.host;
    c.port        = srv.port;
    c.status      = "offline";

    bool ok = false;
    if (srv.server_type == "icecast2" || srv.server_type == "steamcast")
        ok = fetch_icecast2(srv, c);
    else if (srv.server_type == "shoutcast1")
        ok = fetch_shoutcast1(srv, c);
    else if (srv.server_type == "shoutcast2")
        ok = fetch_shoutcast2(srv, c);
    else if (srv.server_type == "mcaster1dnas")
        ok = fetch_mcaster1(srv, c);

    c.polled_at = time(nullptr);

    // We mark "ours" mounts.
    for (auto& m : c.mounts) {
        m.ours = (our_mounts_.count(m.mount) > 0);
    }

    // We fetch per-listener lists for online mounts (Icecast/DNAS/Shoutcast2).
    if (ok && c.status == "online") {
        if (srv.server_type == "icecast2" || srv.server_type == "steamcast" ||
            srv.server_type == "mcaster1dnas") {
            for (const auto& m : c.mounts) {
                if (m.online) {
                    auto listeners = fetch_listener_list_icecast(srv, m.mount);
                    reconcile_listeners(srv.id, m.mount, listeners);
                }
            }
        } else if (srv.server_type == "shoutcast2") {
            auto listeners = fetch_listener_list_sc2(srv);
            reconcile_listeners(srv.id, srv.mount_point, listeners);
        }
    }

    // We update DB.
    if (ok) {
        write_stat_history(srv.id, c);
        update_server_last_seen(srv.id, c.status, c.listeners_total);
    } else {
        update_server_last_seen(srv.id, "offline", 0);
    }

    // We store in cache.
    {
        std::lock_guard<std::mutex> lk(mtx_);
        cache_[srv.id] = c;
    }
}

// ---------------------------------------------------------------------------
// fetch_icecast2 — polls /status-json.xsl (JSON)
// ---------------------------------------------------------------------------

bool ServerMonitor::fetch_icecast2(const ServerRecord& srv, ServerStatCache& out)
{
    int ms = 0;
    // We try JSON endpoint first (modern Icecast2 / Steamcast)
    std::string body = http_get(srv, "/status-json.xsl", ms, false);
    out.fetch_ms = ms;

    if (!body.empty()) {
        try {
            json j = json::parse(body);
            auto& ic = j.at("icestats");

            out.server_id_str = ic.value("server_id", "");
            out.status        = "online";

            // Global listener count
            if (ic.contains("listeners"))
                out.listeners_total = ic["listeners"].get<int>();
            if (ic.contains("limits") && ic["limits"].is_object())
                out.max_listeners = ic["limits"].value("clients", 0);

            // Parse sources
            if (ic.contains("source")) {
                auto& src = ic["source"];
                auto parse_source = [&](const json& s) {
                    MountStat m;
                    // Extract mount path from listenurl
                    std::string url = s.value("listenurl", "");
                    size_t slash = url.rfind('/');
                    m.mount = (slash != std::string::npos) ? url.substr(slash) : url;
                    if (m.mount.empty()) m.mount = "/unknown";

                    m.server_name = s.value("server_name", "");
                    m.listeners   = s.value("listeners", 0);
                    m.peak        = s.value("listener_peak", 0);
                    m.bitrate     = s.value("bitrate", 0);
                    m.out_kbps    = s.value("stream_kbps", 0);
                    m.title       = s.value("title", "");
                    m.codec       = classify_codec(s.value("server_type",""), s.value("subtype",""));
                    m.online      = true;
                    m.connected_s = 0;
                    out.mounts.push_back(m);
                    out.sources_total++;
                    out.sources_online++;
                    out.out_kbps += m.out_kbps;
                };

                if (src.is_array()) {
                    for (auto& s : src) parse_source(s);
                } else if (src.is_object()) {
                    parse_source(src);
                }
            }
            return true;
        } catch (...) {
            // JSON parse failed — fall through to regex fallback
        }
    }

    // We fall back to a plain listener count via XML regex.
    body = http_get(srv, "/admin/stats.xml", ms, true);
    out.fetch_ms = ms;
    if (!body.empty()) {
        std::string cnt = regex_first(body, R"(<listeners>(\d+)</listeners>)");
        if (!cnt.empty()) {
            out.listeners_total = std::stoi(cnt);
            out.status = "online";
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// fetch_shoutcast1 — polls /7.html (CSV)
// ---------------------------------------------------------------------------

bool ServerMonitor::fetch_shoutcast1(const ServerRecord& srv, ServerStatCache& out)
{
    int ms = 0;
    std::string body = http_get(srv, "/7.html", ms, false);
    out.fetch_ms = ms;
    if (body.empty()) return false;
    // CSV: CurrentListeners,Status,PeakListeners,MaxListeners,BitRate,Uptime,...
    std::istringstream ss(body);
    std::string token;
    int field = 0;
    while (std::getline(ss, token, ',') && field < 5) {
        try {
            switch (field) {
                case 0: out.listeners_total = std::stoi(token); break;
                case 2: /* peak — not stored globally */         break;
                case 3: out.max_listeners   = std::stoi(token); break;
                case 4: { MountStat m; m.bitrate = std::stoi(token);
                          m.mount = srv.mount_point.empty() ? "/" : srv.mount_point;
                          m.listeners = out.listeners_total;
                          m.online = true;
                          out.mounts.push_back(m);
                          out.sources_total = out.sources_online = 1;
                          break; }
            }
        } catch (...) {}
        ++field;
    }
    if (field >= 1) { out.status = "online"; return true; }
    return false;
}

// ---------------------------------------------------------------------------
// fetch_shoutcast2 — polls /statistics?sid=1 (XML)
// ---------------------------------------------------------------------------

bool ServerMonitor::fetch_shoutcast2(const ServerRecord& srv, ServerStatCache& out)
{
    int ms = 0;
    std::string body = http_get(srv, "/statistics?sid=1", ms, false);
    out.fetch_ms = ms;
    if (body.empty()) return false;

    auto rx = [&](const std::string& tag) -> int {
        std::string v = regex_first(body, "<" + tag + R"(>(\d+)</)" + tag + ">");
        return v.empty() ? 0 : std::stoi(v);
    };
    auto rxs = [&](const std::string& tag) -> std::string {
        return regex_first(body, "<" + tag + ">([^<]+)</" + tag + ">");
    };

    out.listeners_total = rx("CURRENTLISTENERS");
    out.max_listeners   = rx("MAXLISTENERS");

    MountStat m;
    m.mount     = srv.mount_point.empty() ? "/stream" : srv.mount_point;
    m.listeners = out.listeners_total;
    m.peak      = rx("PEAKLISTENERS");
    m.bitrate   = rx("BITRATE");
    m.out_kbps  = m.bitrate;
    m.server_name = rxs("SERVERTITLE");
    m.title     = rxs("SONGTITLE");
    m.online    = !rxs("STREAMSTATUS").empty();
    m.codec     = "MP3";  // Shoutcast default
    out.mounts.push_back(m);
    out.sources_total = out.sources_online = 1;

    out.status = "online";
    return true;
}

// ---------------------------------------------------------------------------
// fetch_mcaster1 — polls /admin/mcaster1stats (XML)
// ---------------------------------------------------------------------------

bool ServerMonitor::fetch_mcaster1(const ServerRecord& srv, ServerStatCache& out)
{
    int ms = 0;
    std::string body = http_get(srv, "/admin/mcaster1stats", ms, true);
    out.fetch_ms = ms;
    if (body.empty()) return false;

    // Global fields
    auto rxg = [&](const std::string& tag) -> std::string {
        return regex_first(body, "<" + tag + ">([^<]+)</" + tag + ">");
    };

    std::string lst = rxg("listeners");
    std::string mxl = rxg("max_listeners");
    std::string okbps = rxg("outgoing_kbitrate");
    std::string src_count = rxg("sources");
    std::string srv_start = rxg("server_start");

    out.listeners_total = lst.empty()    ? 0 : std::stoi(lst);
    out.max_listeners   = mxl.empty()    ? 0 : std::stoi(mxl);
    out.out_kbps        = okbps.empty()  ? 0 : std::stoi(okbps);
    out.sources_total   = src_count.empty() ? 0 : std::stoi(src_count);

    // We compute uptime from server_start timestamp.
    if (!srv_start.empty()) {
        // Format: "23/Feb/2026:15:38:48 -0800"
        struct tm tm_s{};
        char mon_buf[4] = {};
        sscanf(srv_start.c_str(), "%d/%3s/%d:%d:%d:%d",
               &tm_s.tm_mday, mon_buf, &tm_s.tm_year,
               &tm_s.tm_hour, &tm_s.tm_min, &tm_s.tm_sec);
        // Normalise year
        if (tm_s.tm_year > 1900) tm_s.tm_year -= 1900;
        // Month from 3-letter abbreviation
        static const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                        "Jul","Aug","Sep","Oct","Nov","Dec"};
        for (int i = 0; i < 12; ++i) {
            if (strncmp(mon_buf, months[i], 3) == 0) { tm_s.tm_mon = i; break; }
        }
        tm_s.tm_isdst = -1;
        time_t start = mktime(&tm_s);
        int uptime_s = (int)(time(nullptr) - start);
        if (uptime_s > 0) out.uptime = format_uptime(uptime_s);
    }

    // Per-source blocks: <source mount="/mount">...</source>
    std::regex src_re(R"re(<source\s+mount="([^"]+)">([\s\S]*?)</source>)re",
                       std::regex::ECMAScript);
    auto it  = std::sregex_iterator(body.begin(), body.end(), src_re);
    auto end = std::sregex_iterator();
    for (; it != end; ++it) {
        MountStat m;
        m.mount = (*it)[1].str();
        std::string blk = (*it)[2].str();

        auto rxi = [&](const std::string& tag) -> int {
            std::string v = regex_first(blk, "<" + tag + R"(>(\d+)</)" + tag + ">");
            return v.empty() ? 0 : std::stoi(v);
        };
        auto rxs2 = [&](const std::string& tag) -> std::string {
            return regex_first(blk, "<" + tag + ">([^<]*)</" + tag + ">");
        };

        m.listeners   = rxi("listeners");
        m.peak        = rxi("listener_peak");
        m.bitrate     = rxi("bitrate");
        m.out_kbps    = rxi("outgoing_kbitrate");
        m.connected_s = rxi("connected");
        m.title       = rxs2("title");
        m.server_name = rxs2("server_name");
        m.online      = (m.out_kbps > 0 || m.connected_s > 0);
        m.codec       = classify_codec(rxs2("server_type"), "");

        out.mounts.push_back(m);
        if (m.online) out.sources_online++;
    }

    out.status = "online";
    out.server_id_str = "Mcaster1DNAS";
    return true;
}

// ---------------------------------------------------------------------------
// fetch_listener_list_icecast — /admin/listclients?mount=X
// Returns map<ip, user_agent>
// ---------------------------------------------------------------------------

std::map<std::string,std::string>
ServerMonitor::fetch_listener_list_icecast(const ServerRecord& srv,
                                            const std::string& mount)
{
    std::map<std::string,std::string> result;
    int ms = 0;
    std::string path = "/admin/listclients?mount=" + mount;
    std::string body = http_get(srv, path, ms, true);
    if (body.empty()) return result;

    // XML format: <listener><IP>x.x.x.x</IP><Connected>N</Connected><UserAgent>...</UserAgent></listener>
    std::regex re(R"(<listener>[\s\S]*?<IP>([^<]+)</IP>[\s\S]*?<UserAgent>([^<]*)</UserAgent>[\s\S]*?</listener>)",
                  std::regex::ECMAScript);
    auto it  = std::sregex_iterator(body.begin(), body.end(), re);
    auto end = std::sregex_iterator();
    for (; it != end; ++it) {
        std::string ip = (*it)[1].str();
        std::string ua = (*it)[2].str();
        if (!ip.empty()) result[ip] = ua;
    }
    return result;
}

// ---------------------------------------------------------------------------
// fetch_listener_list_sc2 — /admin/streamadmin.cgi?mode=viewxml&sid=N
// ---------------------------------------------------------------------------

std::map<std::string,std::string>
ServerMonitor::fetch_listener_list_sc2(const ServerRecord& srv, int sid)
{
    std::map<std::string,std::string> result;
    int ms = 0;
    std::string path = "/admin/streamadmin.cgi?mode=viewxml&sid=" + std::to_string(sid);
    std::string body = http_get(srv, path, ms, true);
    if (body.empty()) return result;

    // XML: <LISTENER><HOSTNAME>ip</HOSTNAME><CONNECTTIME>N</CONNECTTIME><USERAGENT>ua</USERAGENT></LISTENER>
    std::regex re(R"(<LISTENER>[\s\S]*?<HOSTNAME>([^<]+)</HOSTNAME>[\s\S]*?<USERAGENT>([^<]*)</USERAGENT>[\s\S]*?</LISTENER>)",
                  std::regex::ECMAScript | std::regex::icase);
    auto it  = std::sregex_iterator(body.begin(), body.end(), re);
    auto end = std::sregex_iterator();
    for (; it != end; ++it) {
        std::string ip = (*it)[1].str();
        std::string ua = (*it)[2].str();
        if (!ip.empty()) result[ip] = ua;
    }
    return result;
}

// ---------------------------------------------------------------------------
// reconcile_listeners
// ---------------------------------------------------------------------------

void ServerMonitor::reconcile_listeners(int server_id,
                                         const std::string& mount,
                                         const std::map<std::string,std::string>& current)
{
    std::set<std::string> current_ips;
    for (const auto& [ip, ua] : current) current_ips.insert(ip);

    std::set<std::string>& prev = prev_ips_[server_id][mount];
    std::string mnt_esc = Mc1Db::instance().escape(mount);

    // We INSERT rows for new listeners.
    for (const auto& [ip, ua] : current) {
        if (prev.count(ip)) continue;  // already tracked
        std::string ip_esc = Mc1Db::instance().escape(ip);
        std::string ua_esc = Mc1Db::instance().escape(ua.substr(0, 499));
        Mc1Db::instance().execf(
            "INSERT INTO mcaster1_metrics.listener_sessions "
            "(stream_mount,client_ip,user_agent,connected_at) "
            "VALUES ('%s','%s','%s',NOW())",
            mnt_esc.c_str(), ip_esc.c_str(), ua_esc.c_str());
    }

    // We UPDATE rows for listeners who have disconnected.
    for (const auto& ip : prev) {
        if (current_ips.count(ip)) continue;  // still connected
        std::string ip_esc = Mc1Db::instance().escape(ip);
        Mc1Db::instance().execf(
            "UPDATE mcaster1_metrics.listener_sessions "
            "SET disconnected_at=NOW(), "
            "duration_sec=TIMESTAMPDIFF(SECOND,connected_at,NOW()), "
            "bytes_sent=0 "
            "WHERE client_ip='%s' AND stream_mount='%s' AND disconnected_at IS NULL "
            "ORDER BY connected_at DESC LIMIT 1",
            ip_esc.c_str(), mnt_esc.c_str());
    }

    prev = current_ips;
}

// ---------------------------------------------------------------------------
// write_stat_history / update_server_last_seen
// ---------------------------------------------------------------------------

void ServerMonitor::write_stat_history(int server_id, const ServerStatCache& c)
{
    Mc1Db::instance().execf(
        "INSERT INTO mcaster1_metrics.server_stat_history "
        "(server_id,sampled_at,listeners,out_kbps,sources_online,status) "
        "VALUES (%d,NOW(),%d,%d,%d,'%s')",
        server_id, c.listeners_total, c.out_kbps,
        c.sources_online, c.status.c_str());
}

void ServerMonitor::update_server_last_seen(int server_id,
                                             const std::string& status,
                                             int listeners)
{
    Mc1Db::instance().execf(
        "UPDATE mcaster1_encoder.streaming_servers "
        "SET last_checked=NOW(),last_status='%s',last_listeners=%d "
        "WHERE id=%d",
        status.c_str(), listeners, server_id);
}

// ---------------------------------------------------------------------------
// http_get — makes an HTTP(S) GET, returns body string
// ---------------------------------------------------------------------------

std::string ServerMonitor::http_get(const ServerRecord& srv,
                                     const std::string& path,
                                     int& ms_out,
                                     bool use_auth)
{
    auto t0 = std::chrono::steady_clock::now();

    std::string body;
    try {
        if (srv.ssl_enabled) {
            httplib::SSLClient cli(srv.host, srv.port);
            cli.enable_server_certificate_verification(false);
            cli.set_connection_timeout(5);
            cli.set_read_timeout(10);
            if (use_auth && !srv.stat_username.empty())
                cli.set_basic_auth(srv.stat_username, srv.stat_password);
            auto r = cli.Get(path.c_str());
            if (r && r->status == 200) body = r->body;
        } else {
            httplib::Client cli(srv.host, srv.port);
            cli.set_connection_timeout(5);
            cli.set_read_timeout(10);
            if (use_auth && !srv.stat_username.empty())
                cli.set_basic_auth(srv.stat_username, srv.stat_password);
            auto r = cli.Get(path.c_str());
            if (r && r->status == 200) body = r->body;
        }
    } catch (const std::exception& e) {
        MC1_WARN("ServerMonitor: http_get failed for " + srv.host + path +
                 ": " + std::string(e.what()));
    }

    ms_out = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    return body;
}

// ---------------------------------------------------------------------------
// regex_first / regex_all
// ---------------------------------------------------------------------------

std::string ServerMonitor::regex_first(const std::string& text,
                                        const std::string& pattern)
{
    try {
        std::regex re(pattern, std::regex::ECMAScript | std::regex::icase);
        std::smatch m;
        if (std::regex_search(text, m, re) && m.size() > 1)
            return m[1].str();
    } catch (...) {}
    return "";
}

std::vector<std::string> ServerMonitor::regex_all(const std::string& text,
                                                    const std::string& pattern)
{
    std::vector<std::string> results;
    try {
        std::regex re(pattern, std::regex::ECMAScript | std::regex::icase);
        auto it  = std::sregex_iterator(text.begin(), text.end(), re);
        auto end = std::sregex_iterator();
        for (; it != end; ++it)
            if ((*it).size() > 1) results.push_back((*it)[1].str());
    } catch (...) {}
    return results;
}

// ---------------------------------------------------------------------------
// classify_codec
// ---------------------------------------------------------------------------

std::string ServerMonitor::classify_codec(const std::string& server_type,
                                           const std::string& subtype)
{
    auto up = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), ::toupper);
        return s;
    };
    std::string st = up(server_type);
    std::string sub = up(subtype);

    if (st.find("OPUS")  != std::string::npos || sub.find("OPUS") != std::string::npos) return "Opus";
    if (st.find("FLAC")  != std::string::npos || sub.find("FLAC") != std::string::npos) return "FLAC";
    if (st.find("AACP")  != std::string::npos || st.find("AAC+") != std::string::npos)  return "AAC+";
    if (st.find("AAC")   != std::string::npos)  return "AAC";
    if (st.find("VORBIS")!= std::string::npos || sub.find("VORBIS") != std::string::npos) return "Vorbis";
    if (st.find("OGG")   != std::string::npos) {
        if (sub.find("OPUS") != std::string::npos)   return "Opus";
        if (sub.find("VORBIS") != std::string::npos) return "Vorbis";
        if (sub.find("FLAC") != std::string::npos)   return "FLAC";
        return "Ogg";
    }
    if (st.find("MPEG")  != std::string::npos || st.find("MP3") != std::string::npos)   return "MP3";
    return "MP3";  // sensible default
}

// ---------------------------------------------------------------------------
// format_uptime
// ---------------------------------------------------------------------------

std::string ServerMonitor::format_uptime(int seconds)
{
    if (seconds <= 0) return "—";
    int days  = seconds / 86400;
    int hours = (seconds % 86400) / 3600;
    int mins  = (seconds % 3600)  / 60;
    char buf[32];
    if      (days  > 0) snprintf(buf, sizeof(buf), "%dd %dh",  days, hours);
    else if (hours > 0) snprintf(buf, sizeof(buf), "%dh %dm",  hours, mins);
    else                snprintf(buf, sizeof(buf), "%dm %ds",  mins, seconds % 60);
    return buf;
}
