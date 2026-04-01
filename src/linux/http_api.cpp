// http_api.cpp — Embedded HTTP/HTTPS admin server for mcaster1-encoder (Linux)
//
// Dependencies (header-only, vendored in external/include/):
//   cpp-httplib  v0.18+  (MIT)  — HTTP/HTTPS server
//   nlohmann/json v3.11+ (MIT)  — JSON serialization
//
// Build requires: -lssl -lcrypto -lpthread
//
// Listener model:
//   Each mc1ListenSocket in gAdminConfig.sockets gets its own thread.
//   HTTP  listeners use httplib::Server.
//   HTTPS listeners use httplib::SSLServer (requires valid cert + key).
//
// Auth model:
//   Browser sessions  → cookie "mc1session=<32-byte-hex-token>"
//   Script/API access → "X-API-Token: <token>" request header

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>
#include <nlohmann/json.hpp>

#include "http_api.h"
#include "fastcgi_client.h"
#include "../platform.h"
#include "../libmcaster1dspencoder/libmcaster1dspencoder.h"

#ifndef MC1_HTTP_TEST_BUILD
#include "audio_pipeline.h"
#include "playlist_parser.h"
#include "dsp/dsp_chain.h"
#include "system_health.h"
#include "server_monitors.h"
#endif

#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/rsa.h>
#include <openssl/bn.h>
#include <openssl/evp.h>

#include "mc1_logger.h"
#include "mc1_db.h"
#include "voictune/ollama_client.h"
#include "voictune/ai_prompt_templates.h"

#include <crypt.h>
#include <string>
#include <map>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <thread>
#include <memory>
#include <random>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <functional>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <cstring>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

using json = nlohmann::json;

/* ── Internal state ───────────────────────────────────────────────────────── */

static std::string      g_webroot;
static FastCgiClient*   g_fcgi = nullptr;
static mc1vt::OllamaClient* g_ollama = nullptr;

/* ── Recording state (PC-1: Podcast Recording Studio) ────────────────────── */
struct RecordingState {
    bool        active            = false;
    int         slot_id           = 0;
    int         episode_id        = 0;
    int         show_id           = 0;
    std::string file_path;
    time_t      started_at        = 0;
    int         auto_split_minutes = 0;
    std::string format            = "mp3";
    std::string episode_title;
    std::string pre_roll;
    std::string post_roll;
    struct Marker {
        int64_t     timestamp_ms;
        std::string title;
        std::string marker_type;
        std::string url;
        std::string image_url;
        int         db_id = 0;
    };
    std::vector<Marker> markers;
};
static std::map<int, RecordingState> g_recordings;   // keyed by slot_id
static std::mutex                    g_rec_mtx;

// Per-listener context — keeps server alive and joinable
struct ListenerCtx {
    std::unique_ptr<httplib::Server> svr;
    std::thread                      th;
};
static std::vector<ListenerCtx> g_listeners;
static std::mutex               g_listeners_mtx;

/* ── Login rate limiter ───────────────────────────────────────────────────── */

struct LoginAttempt { int count; time_t window_start; };
static std::map<std::string, LoginAttempt> g_login_attempts;
static std::mutex                          g_login_rate_mtx;
static constexpr int    LOGIN_RATE_MAX_ATTEMPTS = 5;
static constexpr int    LOGIN_RATE_WINDOW_SECS  = 60;

static bool login_rate_check(const std::string& remote_addr)
{
    std::lock_guard<std::mutex> lk(g_login_rate_mtx);
    auto now = time(nullptr);
    auto it = g_login_attempts.find(remote_addr);
    if (it == g_login_attempts.end()) {
        g_login_attempts[remote_addr] = {1, now};
        return true;
    }
    if (now - it->second.window_start >= LOGIN_RATE_WINDOW_SECS) {
        it->second = {1, now};
        return true;
    }
    if (it->second.count >= LOGIN_RATE_MAX_ATTEMPTS) return false;
    it->second.count++;
    return true;
}

static void login_rate_reset(const std::string& remote_addr)
{
    std::lock_guard<std::mutex> lk(g_login_rate_mtx);
    g_login_attempts.erase(remote_addr);
}

/* ── Session store ────────────────────────────────────────────────────────── */

struct MC1Session { time_t expires; std::string username; };
static std::map<std::string, MC1Session> g_sessions;
static std::mutex                        g_session_mtx;

static std::string gen_token()
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    std::ostringstream ss;
    for (int i = 0; i < 4; i++)
        ss << std::hex << std::setw(16) << std::setfill('0') << dis(gen);
    return ss.str();
}

static bool session_valid(const std::string& tok)
{
    if (tok.empty()) return false;
    std::lock_guard<std::mutex> lk(g_session_mtx);
    auto it = g_sessions.find(tok);
    if (it == g_sessions.end()) return false;
    if (time(nullptr) >= it->second.expires) { g_sessions.erase(it); return false; }
    return true;
}

static std::string session_create(const std::string& username = "")
{
    std::string tok = gen_token();
    int ttl = gAdminConfig.session_timeout_secs > 0
                  ? gAdminConfig.session_timeout_secs : 3600;
    std::lock_guard<std::mutex> lk(g_session_mtx);
    g_sessions[tok] = { time(nullptr) + ttl, username };
    return tok;
}

static std::string session_get_username(const std::string& tok)
{
    if (tok.empty()) return {};
    std::lock_guard<std::mutex> lk(g_session_mtx);
    auto it = g_sessions.find(tok);
    if (it == g_sessions.end()) return {};
    return it->second.username;
}

static void session_delete(const std::string& tok)
{
    std::lock_guard<std::mutex> lk(g_session_mtx);
    g_sessions.erase(tok);
}

/* ── Cookie helpers ───────────────────────────────────────────────────────── */

static std::string cookie_get(const httplib::Request& req, const char* name)
{
    auto it = req.headers.find("Cookie");
    if (it == req.headers.end()) return {};
    std::string hdr = it->second;
    std::string key = std::string(name) + "=";
    size_t pos = hdr.find(key);
    if (pos == std::string::npos) return {};
    pos += key.size();
    size_t end = hdr.find(';', pos);
    std::string val = hdr.substr(pos, end == std::string::npos ? end : end - pos);
    // trim whitespace
    while (!val.empty() && (val.front() == ' ')) val.erase(val.begin());
    while (!val.empty() && (val.back()  == ' ')) val.pop_back();
    return val;
}

static bool request_is_authed(const httplib::Request& req)
{
    // 1. Session cookie
    if (session_valid(cookie_get(req, "mc1session"))) return true;
    // 2. X-API-Token header (for scripts/curl)
    if (gAdminConfig.admin_api_token[0] != '\0') {
        auto it = req.headers.find("X-API-Token");
        if (it != req.headers.end() && it->second == gAdminConfig.admin_api_token)
            return true;
    }
    return false;
}

// Guard helper: run handler if authed, else return 401 JSON or 302 /login
static void with_auth(const httplib::Request& req, httplib::Response& res,
                      std::function<void()> handler)
{
    if (!request_is_authed(req)) {
        auto it = req.headers.find("Accept");
        bool api = it != req.headers.end()
                   && it->second.find("application/json") != std::string::npos;
        // Also treat XHR / explicit API paths as JSON callers
        if (!api && req.path.rfind("/api/", 0) == 0) api = true;
        if (api) {
            res.status = 401;
            res.set_content(R"({"error":"Unauthorized","redirect":"/login"})",
                            "application/json");
        } else {
            res.status = 302;
            res.set_header("Location", "/login");
        }
        return;
    }
    handler();
}

/* ── MIME types — standard Linux /etc/mime.types mapping ─────────────────── */

static const char* mime_for(const std::string& path)
{
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string e = path.substr(dot);

    if (e == ".html" || e == ".htm") return "text/html; charset=utf-8";
    if (e == ".css")                 return "text/css; charset=utf-8";
    if (e == ".js"  || e == ".mjs") return "application/javascript; charset=utf-8";
    if (e == ".json")               return "application/json; charset=utf-8";
    if (e == ".svg")                return "image/svg+xml; charset=utf-8";
    if (e == ".xml")                return "application/xml; charset=utf-8";
    if (e == ".txt")                return "text/plain; charset=utf-8";
    if (e == ".png")                return "image/png";
    if (e == ".jpg" || e == ".jpeg")return "image/jpeg";
    if (e == ".gif")                return "image/gif";
    if (e == ".webp")               return "image/webp";
    if (e == ".ico")                return "image/x-icon";
    if (e == ".woff")               return "font/woff";
    if (e == ".woff2")              return "font/woff2";
    if (e == ".ttf")                return "font/ttf";
    if (e == ".otf")                return "font/otf";
    if (e == ".mp3")                return "audio/mpeg";
    if (e == ".ogg")                return "audio/ogg";
    if (e == ".opus")               return "audio/ogg; codecs=opus";
    if (e == ".flac")               return "audio/flac";
    if (e == ".aac")                return "audio/aac";
    if (e == ".m4a")                return "audio/mp4";
    if (e == ".wav")                return "audio/wav";
    if (e == ".m3u")                return "audio/x-mpegurl";
    if (e == ".m3u8")               return "application/vnd.apple.mpegurl";
    if (e == ".pls")                return "audio/x-scpls";
    if (e == ".xspf")               return "application/xspf+xml";
    return "application/octet-stream";
}

/* ── Static file helper ───────────────────────────────────────────────────── */

static void serve_file(const std::string& path, httplib::Response& res)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        res.status = 404;
        res.set_content("404 Not Found", "text/plain");
        return;
    }
    std::string body((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
    // Cache-control: short TTL for dashboard assets during dev
    res.set_header("Cache-Control", "no-cache");
    res.set_content(body, mime_for(path));
}

/* ── Credentials check ────────────────────────────────────────────────────── */

// bcrypt_verify: compares plaintext password against a $2y$/2b$/2a$ hash
// using the POSIX crypt_r() function (libxcrypt / libcrypt).
static bool bcrypt_verify(const std::string& password, const std::string& hash)
{
    if (hash.size() < 7 || hash[0] != '$') return false;
    struct crypt_data cd{};
    const char* result = crypt_r(password.c_str(), hash.c_str(), &cd);
    return result != nullptr && hash == result;
}

// try_login: checks YAML credentials first, then falls back to MySQL users table.
// Returns true on success and sets out_username to the authenticated username.
static bool try_login(const std::string& user, const std::string& pass,
                      std::string& out_username)
{
    // Layer 1: YAML admin credential (fast path, no DB round-trip)
    if (user == gAdminConfig.admin_username &&
        pass == gAdminConfig.admin_password) {
        out_username = user;
        return true;
    }

    // Layer 2: MySQL users table with bcrypt verification
    std::string stored_hash;
    bool is_active = false;
    if (!Mc1Db::instance().fetch_user_auth(user, stored_hash, is_active))
        return false;   // user not found or DB unavailable

    if (!is_active) return false;
    if (!bcrypt_verify(pass, stored_hash)) return false;

    out_username = user;
    return true;
}

/* ── API helpers ──────────────────────────────────────────────────────────── */

static std::string uptime_str(time_t started)
{
    long secs = (long)(time(nullptr) - started);
    long h = secs / 3600, m = (secs % 3600) / 60, s = secs % 60;
    char buf[32];
    snprintf(buf, sizeof(buf), "%02ld:%02ld:%02ld", h, m, s);
    return buf;
}

static time_t g_start_time = 0;

/* ── Route registration (works on both Server and SSLServer via base ref) ─── */

static void setup_routes(httplib::Server& svr)
{
    // ── Access logger (fires after every request) ──────────────────────────
    svr.set_logger([](const httplib::Request& req, const httplib::Response& res) {
        // Duration isn't available in httplib set_logger; approximate to 0
        std::string ua   = "-";
        std::string ref  = "-";
        auto ua_it  = req.headers.find("User-Agent");
        auto ref_it = req.headers.find("Referer");
        if (ua_it  != req.headers.end()) ua  = ua_it->second;
        if (ref_it != req.headers.end()) ref = ref_it->second;

        mc1log.access(req.remote_addr, req.method, req.path,
                      res.status, (long)res.body.size(),
                      0L, ref, ua);

        // At debug level, log API request/response bodies
        if (mc1log.level() >= MC1_LOG_DEBUG &&
            req.path.rfind("/api/", 0) == 0) {
            mc1log.api(req.method, req.path, res.status,
                       req.body.size()  > 0 ? req.body  : "",
                       res.body.size()  > 0 ? res.body  : "");
        }
    });

    // ── Root redirect ──────────────────────────────────────────────────────
    svr.Get("/", [](const httplib::Request& req, httplib::Response& res) {
        res.status = 302;
        res.set_header("Location", request_is_authed(req) ? "/dashboard" : "/login");
    });

    // ── Login page (no auth required) ─────────────────────────────────────
    svr.Get("/login", [](const httplib::Request&, httplib::Response& res) {
        serve_file(g_webroot + "/login.html", res);
    });

    // ── Dashboard — redirect to PHP page ──────────────────────────────────
    svr.Get("/dashboard", [](const httplib::Request& req, httplib::Response& res) {
        if (!request_is_authed(req)) {
            res.status = 302; res.set_header("Location", "/login"); return;
        }
        res.status = 302; res.set_header("Location", "/dashboard.php");
    });

    // ── Static assets (CSS/JS/fonts — no auth so login page can use them) ─
    svr.Get(R"(/(.+\.(css|js|mjs|ico|png|jpg|jpeg|gif|webp|svg|woff|woff2|ttf|otf)))",
        [](const httplib::Request& req, httplib::Response& res) {
            // Security: reject path traversal attempts (../ in the URL)
            std::string asset = req.matches[1].str();
            if (asset.find("..") != std::string::npos) {
                res.status = 400;
                res.set_content("400 Bad Request", "text/plain");
                return;
            }
            serve_file(g_webroot + "/" + asset, res);
        });

    // ── POST /api/v1/auth/login ────────────────────────────────────────────
    svr.Post("/api/v1/auth/login",
        [](const httplib::Request& req, httplib::Response& res) {
            // Rate limiting: max 5 attempts per IP per 60 seconds
            if (!login_rate_check(req.remote_addr)) {
                MC1_WARN("auth: rate limited login from " + req.remote_addr);
                res.status = 429;
                res.set_content(R"({"error":"Too many login attempts. Try again later."})",
                                "application/json");
                return;
            }
            json body;
            try { body = json::parse(req.body); }
            catch (...) {
                res.status = 400;
                res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                return;
            }
            std::string user = body.value("username", "");
            std::string pass = body.value("password", "");
            std::string authed_user;
            if (try_login(user, pass, authed_user)) {
                login_rate_reset(req.remote_addr);
                std::string tok = session_create(authed_user);
                int ttl = gAdminConfig.session_timeout_secs > 0
                              ? gAdminConfig.session_timeout_secs : 3600;
                bool is_https = req.has_header("X-Forwarded-Proto")
                    ? req.get_header_value("X-Forwarded-Proto") == "https"
                    : (req.get_header_value("Host").find("8344") != std::string::npos);
                std::string cookie = "mc1session=" + tok
                    + "; Path=/; HttpOnly; SameSite=Lax; Max-Age="
                    + std::to_string(ttl);
                if (is_https) cookie += "; Secure";
                res.set_header("Set-Cookie", cookie);
                MC1_INFO("auth: login ok user=" + authed_user);
                json r; r["ok"] = true; r["redirect"] = "/dashboard";
                res.set_content(r.dump(), "application/json");
            } else {
                MC1_WARN("auth: login failed for user=" + user);
                res.status = 401;
                res.set_content(R"({"error":"Invalid credentials"})",
                                "application/json");
            }
        });

    // ── POST /api/v1/auth/logout ───────────────────────────────────────────
    svr.Post("/api/v1/auth/logout",
        [](const httplib::Request& req, httplib::Response& res) {
            session_delete(cookie_get(req, "mc1session"));
            res.set_header("Set-Cookie", "mc1session=; Path=/; Max-Age=0");
            res.status = 302;
            res.set_header("Location", "/login");
        });

    // ── GET /api/v1/status ────────────────────────────────────────────────
    svr.Get("/api/v1/status",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                json j;
                j["version"]  = "1.8.0-beta.1";
                j["platform"] = "linux";
                j["uptime"]   = uptime_str(g_start_time);
                j["admin_server"] = "mcaster1-encoder";
                j["icy_version"]  = "2.2";

#ifndef MC1_HTTP_TEST_BUILD
                if (g_pipeline) {
                    auto all = g_pipeline->all_stats();
                    int live = 0;
                    for (auto& s : all)
                        if (s.state == EncoderSlot::State::LIVE) ++live;
                    j["encoders_connected"] = live;
                    j["encoders_total"]     = static_cast<int>(all.size());
                    j["master_volume"]      = g_pipeline->master_volume();
                } else {
                    j["encoders_connected"] = 0;
                    j["encoders_total"]     = 0;
                }
#else
                j["encoders_connected"] = 0;
                j["encoders_total"]     = 0;
#endif
                res.set_content(j.dump(2), "application/json");
            });
        });

    // ── GET /api/v1/encoders ──────────────────────────────────────────────
    svr.Get("/api/v1/encoders",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                json arr = json::array();
                if (g_pipeline) {
                    for (auto& s : g_pipeline->all_stats()) {
                        json e;
                        e["slot_id"]      = s.slot_id;
                        e["state"]        = s.state_str;
                        e["is_live"]      = s.is_live;
                        e["bytes_sent"]   = s.bytes_sent;
                        e["uptime_sec"]   = s.uptime_sec;
                        e["track_title"]  = s.current_title;
                        e["track_artist"] = s.current_artist;
                        e["position_ms"]  = s.position_ms;
                        e["duration_ms"]  = s.duration_ms;
                        e["track_index"]  = s.track_index;
                        e["track_count"]  = s.track_count;
                        e["volume"]       = s.volume;
                        e["last_error"]   = s.last_error;
                        arr.push_back(e);
                    }
                }
                res.set_content(arr.dump(2), "application/json");
#else
                res.set_content(json::array().dump(), "application/json");
#endif
            });
        });

    // ── POST /api/v1/encoders/{slot}/start ───────────────────────────────
    svr.Post(R"(/api/v1/encoders/(\d+)/start)",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                int slot_id = std::stoi(req.matches[1].str());
                if (g_pipeline && g_pipeline->start_slot(slot_id)) {
                    mc1log.encoder(slot_id, "START", "requested by " + req.remote_addr);
                    res.set_content(R"({"ok":true})", "application/json");
                } else {
                    MC1_WARN("start_slot(" + std::to_string(slot_id) + ") failed");
                    res.status = 400;
                    res.set_content(R"({"error":"Failed to start slot"})",
                                    "application/json");
                }
#else
                res.set_content(R"({"ok":false,"error":"no pipeline"})", "application/json");
#endif
            });
        });

    // ── POST /api/v1/encoders/{slot}/stop ────────────────────────────────
    svr.Post(R"(/api/v1/encoders/(\d+)/stop)",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                int slot_id = std::stoi(req.matches[1].str());
                if (g_pipeline) g_pipeline->stop_slot(slot_id);
                mc1log.encoder(slot_id, "STOP", "requested by " + req.remote_addr);
                res.set_content(R"({"ok":true})", "application/json");
#else
                res.set_content(R"({"ok":false})", "application/json");
#endif
            });
        });

    // ── POST /api/v1/encoders/{slot}/restart ─────────────────────────────
    svr.Post(R"(/api/v1/encoders/(\d+)/restart)",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                int slot_id = std::stoi(req.matches[1].str());
                if (g_pipeline) g_pipeline->restart_slot(slot_id);
                mc1log.encoder(slot_id, "RESTART", "requested by " + req.remote_addr);
                res.set_content(R"({"ok":true})", "application/json");
#else
                res.set_content(R"({"ok":false})", "application/json");
#endif
            });
        });

    // ── POST /api/v1/encoders/{slot}/wake ────────────────────────────────
    svr.Post(R"(/api/v1/encoders/(\d+)/wake)",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                int slot_id = std::stoi(req.matches[1].str());
                if (g_pipeline && g_pipeline->wake_slot(slot_id)) {
                    mc1log.encoder(slot_id, "WAKE", "requested by " + req.remote_addr);
                    res.set_content(R"({"ok":true})", "application/json");
                } else {
                    MC1_WARN("wake_slot(" + std::to_string(slot_id) + ") failed (not in SLEEP state?)");
                    res.status = 400;
                    res.set_content(R"({"error":"Slot not in SLEEP state or not found"})",
                                    "application/json");
                }
#else
                res.set_content(R"({"ok":false,"error":"no pipeline"})", "application/json");
#endif
            });
        });

    // ── GET /api/v1/encoders/{slot}/stats ────────────────────────────────
    svr.Get(R"(/api/v1/encoders/(\d+)/stats)",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                int slot_id = std::stoi(req.matches[1].str());
                json j;
                if (g_pipeline) {
                    auto s = g_pipeline->slot_stats(slot_id);
                    j["state"]        = s.state_str;
                    j["is_live"]      = s.is_live;
                    j["bytes_sent"]   = s.bytes_sent;
                    j["uptime_sec"]   = s.uptime_sec;
                    j["track_title"]  = s.current_title;
                    j["track_artist"] = s.current_artist;
                    j["position_ms"]  = s.position_ms;
                    j["duration_ms"]  = s.duration_ms;
                    j["track_index"]  = s.track_index;
                    j["track_count"]  = s.track_count;
                    j["volume"]       = s.volume;
                    j["last_error"]   = s.last_error;
                } else {
                    j["error"] = "no pipeline";
                }
                res.set_content(j.dump(2), "application/json");
#else
                res.set_content(R"({"error":"no pipeline"})", "application/json");
#endif
            });
        });

    // ── GET /api/v1/devices ───────────────────────────────────────────────
    svr.Get("/api/v1/devices",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                auto devs = AudioPipeline::list_devices();
                json arr = json::array();
                for (auto& d : devs) {
                    json e;
                    e["index"]              = d.index;
                    e["name"]               = d.name;
                    e["max_input_channels"] = d.max_input_channels;
                    e["default_sample_rate"]= d.default_sample_rate;
                    e["is_default_input"]   = d.is_default_input;
                    arr.push_back(e);
                }
                res.set_content(arr.dump(2), "application/json");
#else
                res.set_content(json::array().dump(), "application/json");
#endif
            });
        });

    // ── POST /api/v1/playlist/skip ────────────────────────────────────────
    svr.Post("/api/v1/playlist/skip",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                json body;
                try { body = json::parse(req.body); } catch (...) {}
                int slot_id = body.value("slot", 1);
                (void)slot_id;
#ifndef MC1_HTTP_TEST_BUILD
                if (g_pipeline) g_pipeline->skip_track(slot_id);
#endif
                res.set_content(R"({"ok":true})", "application/json");
            });
        });

    // ── POST /api/v1/playlist/load ────────────────────────────────────────
    svr.Post("/api/v1/playlist/load",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                json body;
                try { body = json::parse(req.body); }
                catch (...) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                    return;
                }
                int         slot_id = body.value("slot", 1);
                std::string path    = body.value("path", "");
                if (path.empty()) {
                    res.status = 400;
                    res.set_content(R"({"error":"path required"})", "application/json");
                    return;
                }
#ifndef MC1_HTTP_TEST_BUILD
                bool ok = g_pipeline ? g_pipeline->load_playlist(slot_id, path) : false;
                json r; r["ok"] = ok;
                if (!ok) r["error"] = "Failed to load playlist";
                res.set_content(r.dump(), "application/json");
#else
                res.set_content(R"({"ok":false})", "application/json");
#endif
            });
        });

    // ── PUT /api/v1/metadata ──────────────────────────────────────────────
    svr.Put("/api/v1/metadata",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                json j;
                try { j = json::parse(req.body); }
                catch (...) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid JSON"})",
                                    "application/json");
                    return;
                }
                std::string title  = j.value("title",  "");
                std::string artist = j.value("artist", "");
                int         slot   = j.value("slot", -1);  // -1 = all slots
                (void)slot;
#ifndef MC1_HTTP_TEST_BUILD
                if (g_pipeline) {
                    if (slot < 0) {
                        // Push to all live slots
                        auto all = g_pipeline->all_stats();
                        for (auto& s : all)
                            g_pipeline->push_metadata(s.slot_id, title, artist);
                    } else {
                        g_pipeline->push_metadata(slot, title, artist);
                    }
                }
#endif
                json r; r["ok"] = true;
                r["title"]  = title;
                r["artist"] = artist;
                res.set_content(r.dump(), "application/json");
            });
        });

    // ── PUT /api/v1/volume ────────────────────────────────────────────────
    svr.Put("/api/v1/volume",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                json j;
                try { j = json::parse(req.body); }
                catch (...) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid JSON"})",
                                    "application/json");
                    return;
                }
                float vol    = j.value("volume", 1.0f);
                int   slot   = j.value("slot", -1);  // -1 = master
                (void)slot;
                if (vol < 0.0f) vol = 0.0f;
                if (vol > 2.0f) vol = 2.0f;

#ifndef MC1_HTTP_TEST_BUILD
                if (g_pipeline) {
                    if (slot < 0)
                        g_pipeline->set_master_volume(vol);
                    else
                        g_pipeline->set_volume(slot, vol);
                }
#endif
                json r; r["ok"] = true; r["volume"] = vol;
                res.set_content(r.dump(), "application/json");
            });
        });

    // ── GET /api/v1/encoders/{slot}/dsp — get DSP configuration ──────────
    svr.Get(R"(/api/v1/encoders/(\d+)/dsp)",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                int slot_id = std::stoi(req.matches[1].str());
                json j;
                if (g_pipeline) {
                    EncoderConfig cfg;
                    if (g_pipeline->get_slot_config(slot_id, cfg)) {
                        j["slot_id"]            = slot_id;
                        j["eq_enabled"]         = cfg.dsp_eq_enabled;
                        j["agc_enabled"]        = cfg.dsp_agc_enabled;
                        j["crossfade_enabled"]  = cfg.dsp_crossfade_enabled;
                        j["crossfade_duration"] = cfg.dsp_crossfade_duration;
                        j["crossfade_curve"]    = cfg.dsp_crossfade_curve;
                        j["eq_preset"]          = cfg.dsp_eq_preset;
                        j["presets_available"]  = json::array(
                            {"flat","classic_rock","country","modern_rock",
                             "broadcast","spoken_word"});
                    } else {
                        res.status = 404;
                        j["error"] = "Slot not found";
                    }
                } else {
                    res.status = 503;
                    j["error"] = "No pipeline";
                }
                res.set_content(j.dump(2), "application/json");
#else
                res.set_content(R"({"error":"no pipeline"})", "application/json");
#endif
            });
        });

    // ── PUT /api/v1/encoders/{slot}/dsp — update DSP config live ─────────
    svr.Put(R"(/api/v1/encoders/(\d+)/dsp)",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                int slot_id = std::stoi(req.matches[1].str());
                json body;
                try { body = json::parse(req.body); }
                catch (...) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                    return;
                }
                EncoderConfig cfg;
                if (!g_pipeline || !g_pipeline->get_slot_config(slot_id, cfg)) {
                    res.status = 404;
                    res.set_content(R"({"error":"Slot not found"})", "application/json");
                    return;
                }
                if (body.contains("eq_enabled"))
                    cfg.dsp_eq_enabled        = body["eq_enabled"].get<bool>();
                if (body.contains("agc_enabled"))
                    cfg.dsp_agc_enabled       = body["agc_enabled"].get<bool>();
                if (body.contains("crossfade_enabled"))
                    cfg.dsp_crossfade_enabled = body["crossfade_enabled"].get<bool>();
                if (body.contains("crossfade_duration"))
                    cfg.dsp_crossfade_duration= body["crossfade_duration"].get<float>();
                if (body.contains("crossfade_curve"))
                    cfg.dsp_crossfade_curve   = std::clamp(body["crossfade_curve"].get<int>(), 0, 8);
                if (body.contains("eq_preset"))
                    cfg.dsp_eq_preset         = body["eq_preset"].get<std::string>();

                mc1dsp::DspChainConfig dsp_cfg;
                dsp_cfg.sample_rate        = cfg.sample_rate;
                dsp_cfg.channels           = cfg.channels;
                dsp_cfg.eq_enabled         = cfg.dsp_eq_enabled;
                dsp_cfg.agc_enabled        = cfg.dsp_agc_enabled;
                dsp_cfg.crossfader_enabled = cfg.dsp_crossfade_enabled;
                dsp_cfg.crossfade_duration = cfg.dsp_crossfade_duration;
                dsp_cfg.crossfade_curve    = cfg.dsp_crossfade_curve;
                dsp_cfg.eq_preset          = cfg.dsp_eq_preset;
                g_pipeline->reconfigure_dsp(slot_id, dsp_cfg);

                json r;
                r["ok"]               = true;
                r["slot_id"]          = slot_id;
                r["eq_enabled"]       = cfg.dsp_eq_enabled;
                r["agc_enabled"]      = cfg.dsp_agc_enabled;
                r["crossfade_enabled"]= cfg.dsp_crossfade_enabled;
                r["eq_preset"]        = cfg.dsp_eq_preset;
                res.set_content(r.dump(2), "application/json");
#else
                res.set_content(R"({"ok":false,"error":"no pipeline"})", "application/json");
#endif
            });
        });

    // ── POST /api/v1/encoders/{slot}/dsp/eq/preset — apply named EQ preset
    svr.Post(R"(/api/v1/encoders/(\d+)/dsp/eq/preset)",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                int slot_id = std::stoi(req.matches[1].str());
                json body;
                try { body = json::parse(req.body); }
                catch (...) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                    return;
                }
                std::string preset = body.value("preset", "flat");
                EncoderConfig cfg;
                if (!g_pipeline || !g_pipeline->get_slot_config(slot_id, cfg)) {
                    res.status = 404;
                    res.set_content(R"({"error":"Slot not found"})", "application/json");
                    return;
                }
                mc1dsp::DspChainConfig dsp_cfg;
                dsp_cfg.sample_rate        = cfg.sample_rate;
                dsp_cfg.channels           = cfg.channels;
                dsp_cfg.eq_enabled         = true;          // applying a preset enables EQ
                dsp_cfg.agc_enabled        = cfg.dsp_agc_enabled;
                dsp_cfg.crossfader_enabled = cfg.dsp_crossfade_enabled;
                dsp_cfg.crossfade_duration = cfg.dsp_crossfade_duration;
                dsp_cfg.eq_preset          = preset;
                g_pipeline->reconfigure_dsp(slot_id, dsp_cfg);

                json r; r["ok"] = true; r["slot_id"] = slot_id; r["preset"] = preset;
                res.set_content(r.dump(), "application/json");
#else
                res.set_content(R"({"ok":false})", "application/json");
#endif
            });
        });

    // ── GET /api/v1/effects/versions — full version registry for all effects ──
    svr.Get("/api/v1/effects/versions",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                json r; r["ok"] = true;
                r["effects"] = mc1dsp::EffectsRack::all_effect_versions();
                r["effect_count"] = mc1dsp::EFFECT_VERSION_COUNT;
                res.set_content(r.dump(2), "application/json");
            });
        });

    // ── GET /api/v1/crossfader/curves — list all 9 curve algorithms ──────────
    svr.Get("/api/v1/crossfader/curves",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                json j; j["ok"] = true;
                json curves = json::array();
                for (int i = 0; i < static_cast<int>(mc1xf::Curve::COUNT); ++i) {
                    json c;
                    c["id"]          = i;
                    c["name"]        = mc1xf::CURVE_NAMES[i];
                    c["description"] = mc1xf::CURVE_DESCRIPTIONS[i];
                    curves.push_back(c);
                }
                j["curves"] = curves;
                res.set_content(j.dump(2), "application/json");
            });
        });

    // ── GET /api/v1/crossfader/curves/{id}/sample — 100-point gain arrays ──
    svr.Get(R"(/api/v1/crossfader/curves/(\d+)/sample)",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                int id = std::stoi(req.matches[1].str());
                if (id < 0 || id >= static_cast<int>(mc1xf::Curve::COUNT)) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid curve id"})", "application/json");
                    return;
                }
                mc1xf::Curve cv = static_cast<mc1xf::Curve>(id);
                const int n = 100;
                float sa[n], sb[n];
                mc1xf::sampleCurveA(cv, sa, n);
                mc1xf::sampleCurveB(cv, sb, n);
                json j;
                j["ok"]   = true;
                j["id"]   = id;
                j["name"] = mc1xf::CURVE_NAMES[id];
                j["points"] = n;
                j["a"] = json::array();
                j["b"] = json::array();
                for (int i = 0; i < n; ++i) {
                    j["a"].push_back(std::round(sa[i] * 10000.0f) / 10000.0f);
                    j["b"].push_back(std::round(sb[i] * 10000.0f) / 10000.0f);
                }
                res.set_content(j.dump(), "application/json");
            });
        });

    // ── GET /api/v1/encoders/{slot}/crossfader — get crossfader state ───────
    svr.Get(R"(/api/v1/encoders/(\d+)/crossfader)",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                int slot_id = std::stoi(req.matches[1].str());
                json j; j["ok"] = true;
                EncoderConfig cfg;
                if (g_pipeline && g_pipeline->get_slot_config(slot_id, cfg)) {
                    j["slot_id"]       = slot_id;
                    j["curve"]         = cfg.dsp_crossfade_curve;
                    j["curve_name"]    = mc1xf::CURVE_NAMES[std::clamp(cfg.dsp_crossfade_curve, 0, 8)];
                    j["duration"]      = cfg.dsp_crossfade_duration;
                    j["enabled"]       = cfg.dsp_crossfade_enabled;
                } else {
                    res.status = 404;
                    j["ok"] = false; j["error"] = "Slot not found";
                }
                res.set_content(j.dump(2), "application/json");
#else
                res.set_content(R"({"ok":false})", "application/json");
#endif
            });
        });

    // ── PUT /api/v1/encoders/{slot}/crossfader — set crossfader config ──────
    svr.Put(R"(/api/v1/encoders/(\d+)/crossfader)",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                int slot_id = std::stoi(req.matches[1].str());
                json body;
                try { body = json::parse(req.body); }
                catch (...) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                    return;
                }
                EncoderConfig cfg;
                if (!g_pipeline || !g_pipeline->get_slot_config(slot_id, cfg)) {
                    res.status = 404;
                    res.set_content(R"({"error":"Slot not found"})", "application/json");
                    return;
                }
                if (body.contains("curve"))
                    cfg.dsp_crossfade_curve    = std::clamp(body["curve"].get<int>(), 0, 8);
                if (body.contains("duration"))
                    cfg.dsp_crossfade_duration = body["duration"].get<float>();
                if (body.contains("enabled"))
                    cfg.dsp_crossfade_enabled  = body["enabled"].get<bool>();

                mc1dsp::DspChainConfig dsp_cfg;
                dsp_cfg.sample_rate        = cfg.sample_rate;
                dsp_cfg.channels           = cfg.channels;
                dsp_cfg.eq_enabled         = cfg.dsp_eq_enabled;
                dsp_cfg.agc_enabled        = cfg.dsp_agc_enabled;
                dsp_cfg.crossfader_enabled = cfg.dsp_crossfade_enabled;
                dsp_cfg.crossfade_duration = cfg.dsp_crossfade_duration;
                dsp_cfg.crossfade_curve    = cfg.dsp_crossfade_curve;
                dsp_cfg.eq_preset          = cfg.dsp_eq_preset;
                g_pipeline->reconfigure_dsp(slot_id, dsp_cfg);

                json r;
                r["ok"]         = true;
                r["slot_id"]    = slot_id;
                r["curve"]      = cfg.dsp_crossfade_curve;
                r["curve_name"] = mc1xf::CURVE_NAMES[cfg.dsp_crossfade_curve];
                r["duration"]   = cfg.dsp_crossfade_duration;
                r["enabled"]    = cfg.dsp_crossfade_enabled;
                res.set_content(r.dump(2), "application/json");
#else
                res.set_content(R"({"ok":false})", "application/json");
#endif
            });
        });

    // ── GET /api/v1/encoders/{slot}/effects — per-slot effects config ──────
    svr.Get(R"(/api/v1/encoders/(\d+)/effects)",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                int slot_id = std::stoi(req.matches[1].str());
                json j; j["ok"] = true;
                EncoderConfig cfg;
                if (g_pipeline && g_pipeline->get_slot_config(slot_id, cfg)) {
                    j["slot_id"]      = slot_id;
                    const char* modes[] = {"global","bypass","custom"};
                    j["effects_mode"] = modes[static_cast<int>(cfg.effects_mode)];
                } else {
                    res.status = 404; j["ok"] = false; j["error"] = "Slot not found";
                }
                res.set_content(j.dump(2), "application/json");
#else
                res.set_content(R"({"ok":false})", "application/json");
#endif
            });
        });

    // ── PUT /api/v1/encoders/{slot}/effects — set per-slot effects mode ─────
    svr.Put(R"(/api/v1/encoders/(\d+)/effects)",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                int slot_id = std::stoi(req.matches[1].str());
                json body;
                try { body = json::parse(req.body); }
                catch (...) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                    return;
                }
                EncoderConfig cfg;
                if (!g_pipeline || !g_pipeline->get_slot_config(slot_id, cfg)) {
                    res.status = 404;
                    res.set_content(R"({"error":"Slot not found"})", "application/json");
                    return;
                }
                if (body.contains("effects_mode")) {
                    std::string mode = body["effects_mode"].get<std::string>();
                    if (mode == "global")  cfg.effects_mode = EncoderConfig::EffectsMode::GLOBAL;
                    else if (mode == "bypass")  cfg.effects_mode = EncoderConfig::EffectsMode::BYPASS;
                    else if (mode == "custom")  cfg.effects_mode = EncoderConfig::EffectsMode::CUSTOM;
                }
                json r; r["ok"] = true; r["slot_id"] = slot_id;
                const char* modes[] = {"global","bypass","custom"};
                r["effects_mode"] = modes[static_cast<int>(cfg.effects_mode)];
                res.set_content(r.dump(2), "application/json");
#else
                res.set_content(R"({"ok":false})", "application/json");
#endif
            });
        });

    // ── GET /api/v1/effects/unit-types — list available effect types ────────
    svr.Get("/api/v1/effects/unit-types",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                json j; j["ok"] = true;
                j["types"] = mc1dsp::EffectsRack::available_types();
                res.set_content(j.dump(2), "application/json");
            });
        });

    // ── GET /api/v1/effects/global — get global rack state ──────────────────
    svr.Get("/api/v1/effects/global",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                json j; j["ok"] = true;
                if (g_pipeline) {
                    j["rack"] = g_pipeline->global_effects_rack().to_json();
                } else {
                    j["rack"] = json::object();
                }
                res.set_content(j.dump(2), "application/json");
#else
                res.set_content(R"({"ok":false})", "application/json");
#endif
            });
        });

    // ── PUT /api/v1/effects/global — update global rack (bypass, unit params) ─
    svr.Put("/api/v1/effects/global",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                json body;
                try { body = json::parse(req.body); }
                catch (...) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                    return;
                }
                if (!g_pipeline) {
                    res.status = 503;
                    res.set_content(R"({"error":"No pipeline"})", "application/json");
                    return;
                }
                auto& rack = g_pipeline->global_effects_rack();
                if (body.contains("bypass"))
                    rack.set_bypass(body["bypass"].get<bool>());
                /* We allow updating a specific unit's params */
                if (body.contains("unit_id") && body.contains("params")) {
                    int uid = body["unit_id"].get<int>();
                    rack.set_unit_params(uid, body["params"]);
                }
                if (body.contains("unit_id") && body.contains("enabled")) {
                    int uid = body["unit_id"].get<int>();
                    rack.set_unit_enabled(uid, body["enabled"].get<bool>());
                }
                json r; r["ok"] = true; r["rack"] = rack.to_json();
                res.set_content(r.dump(2), "application/json");
#else
                res.set_content(R"({"ok":false})", "application/json");
#endif
            });
        });

    // ── POST /api/v1/effects/global/units — add a unit to global rack ───────
    svr.Post("/api/v1/effects/global/units",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                json body;
                try { body = json::parse(req.body); }
                catch (...) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                    return;
                }
                if (!g_pipeline) {
                    res.status = 503;
                    res.set_content(R"({"error":"No pipeline"})", "application/json");
                    return;
                }
                std::string type = body.value("type", "");
                auto unit = mc1dsp::EffectsRack::create_unit(type);
                if (!unit) {
                    res.status = 400;
                    res.set_content(R"({"error":"Unknown unit type"})", "application/json");
                    return;
                }
                if (body.contains("params")) unit->set_params(body["params"]);
                if (body.contains("enabled")) unit->set_enabled(body["enabled"].get<bool>());
                int id = g_pipeline->global_effects_rack().add_unit(std::move(unit));
                json r; r["ok"] = true; r["unit_id"] = id; r["type"] = type;
                res.set_content(r.dump(), "application/json");
#else
                res.set_content(R"({"ok":false})", "application/json");
#endif
            });
        });

    // ── DELETE /api/v1/effects/global/units/{id} — remove a unit ────────────
    svr.Delete(R"(/api/v1/effects/global/units/(\d+))",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                int uid = std::stoi(req.matches[1].str());
                if (!g_pipeline) {
                    res.status = 503;
                    res.set_content(R"({"error":"No pipeline"})", "application/json");
                    return;
                }
                bool ok = g_pipeline->global_effects_rack().remove_unit(uid);
                json r; r["ok"] = ok;
                if (!ok) r["error"] = "Unit not found";
                res.set_content(r.dump(), "application/json");
#else
                res.set_content(R"({"ok":false})", "application/json");
#endif
            });
        });

    // ── PUT /api/v1/effects/global/reorder — reorder the chain ──────────────
    svr.Put("/api/v1/effects/global/reorder",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                json body;
                try { body = json::parse(req.body); }
                catch (...) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                    return;
                }
                if (!g_pipeline || !body.contains("order")) {
                    res.status = 400;
                    res.set_content(R"({"error":"order array required"})", "application/json");
                    return;
                }
                std::vector<int> order;
                for (const auto& v : body["order"]) order.push_back(v.get<int>());
                bool ok = g_pipeline->global_effects_rack().reorder(order);
                json r; r["ok"] = ok;
                if (!ok) r["error"] = "Reorder failed — check all IDs present";
                else r["rack"] = g_pipeline->global_effects_rack().to_json();
                res.set_content(r.dump(2), "application/json");
#else
                res.set_content(R"({"ok":false})", "application/json");
#endif
            });
        });

    // ── POST /api/v1/ptt/activate — activate push-to-talk ducking ─────────
    svr.Post("/api/v1/ptt/activate",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                if (g_pipeline) { g_pipeline->set_ptt(true); }
                json r; r["ok"] = true; r["ptt_active"] = true;
                res.set_content(r.dump(), "application/json");
#else
                res.set_content(R"({"ok":false})", "application/json");
#endif
            });
        });

    // ── POST /api/v1/ptt/deactivate — deactivate PTT ducking ────────────
    svr.Post("/api/v1/ptt/deactivate",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                if (g_pipeline) { g_pipeline->set_ptt(false); }
                json r; r["ok"] = true; r["ptt_active"] = false;
                res.set_content(r.dump(), "application/json");
#else
                res.set_content(R"({"ok":false})", "application/json");
#endif
            });
        });

    // ── GET /api/v1/ptt/status — current PTT state + duck level ─────────
    svr.Get("/api/v1/ptt/status",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                json r; r["ok"] = true;
                if (g_pipeline) {
                    auto& d = g_pipeline->ducker();
                    r["ptt_active"]     = d.is_ptt_active();
                    r["current_duck_db"]= d.current_duck_db();
                    r["config"]         = d.get_params();
                } else {
                    r["ptt_active"] = false;
                }
                res.set_content(r.dump(2), "application/json");
#else
                res.set_content(R"({"ok":false})", "application/json");
#endif
            });
        });

    // ── PUT /api/v1/ptt/config — configure ducker params ────────────────
    svr.Put("/api/v1/ptt/config",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                json body;
                try { body = json::parse(req.body); }
                catch (...) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                    return;
                }
                if (g_pipeline) {
                    g_pipeline->ducker().set_params(body);
                }
                json r; r["ok"] = true;
                if (g_pipeline) r["config"] = g_pipeline->ducker().get_params();
                res.set_content(r.dump(2), "application/json");
#else
                res.set_content(R"({"ok":false})", "application/json");
#endif
            });
        });

    // ── GET /api/v1/jack/status — JACK daemon status ────────────────────
    svr.Get("/api/v1/jack/status",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                json r; r["ok"] = true;
                if (g_pipeline) {
                    auto& jm = g_pipeline->jack_manager();
                    r["daemon_running"]    = jm.is_daemon_running();
                    r["client_connected"]  = jm.is_client_connected();
                    r["sample_rate"]       = jm.sample_rate();
                    r["buffer_size"]       = jm.buffer_size();
                    r["driver"]            = jm.driver();
                    auto cables = jm.list_cables();
                    r["cable_count"]       = (int)cables.size();
                } else {
                    r["daemon_running"] = false;
                }
                res.set_content(r.dump(2), "application/json");
#else
                res.set_content(R"({"ok":false})", "application/json");
#endif
            });
        });

    // ── POST /api/v1/jack/start — start JACK daemon ─────────────────────
    svr.Post("/api/v1/jack/start",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                json body;
                try { body = json::parse(req.body); } catch (...) { body = json::object(); }
                std::string driver = body.value("driver", "dummy");
                int sr = body.value("sample_rate", 44100);
                int bs = body.value("buffer_size", 1024);
                int cables = body.value("cables", 12);

                json r;
                if (!g_pipeline) { r["ok"] = false; r["error"] = "No pipeline"; }
                else {
                    auto& jm = g_pipeline->jack_manager();
                    bool ok = jm.start_daemon(driver, sr, bs);
                    if (ok) {
                        ok = jm.connect_client();
                        if (ok && cables > 0) jm.create_cables(cables);
                    }
                    r["ok"] = ok;
                    if (!ok) r["error"] = "Failed to start JACK daemon";
                    else {
                        r["sample_rate"]  = jm.sample_rate();
                        r["buffer_size"]  = jm.buffer_size();
                        r["cable_count"]  = (int)jm.list_cables().size();
                    }
                }
                res.set_content(r.dump(2), "application/json");
#else
                res.set_content(R"({"ok":false})", "application/json");
#endif
            });
        });

    // ── POST /api/v1/jack/stop — stop JACK daemon ───────────────────────
    svr.Post("/api/v1/jack/stop",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                if (g_pipeline) {
                    g_pipeline->jack_manager().disconnect_client();
                    g_pipeline->jack_manager().stop_daemon();
                }
                json r; r["ok"] = true;
                res.set_content(r.dump(), "application/json");
#else
                res.set_content(R"({"ok":false})", "application/json");
#endif
            });
        });

    // ── GET /api/v1/jack/ports — list all JACK ports ────────────────────
    svr.Get("/api/v1/jack/ports",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                json r; r["ok"] = true;
                json ports = json::array();
                if (g_pipeline) {
                    for (const auto& p : g_pipeline->jack_manager().list_ports()) {
                        ports.push_back({{"name", p.name}, {"is_input", p.is_input},
                                         {"is_output", p.is_output}, {"is_physical", p.is_physical}});
                    }
                }
                r["ports"] = ports;
                res.set_content(r.dump(2), "application/json");
#else
                res.set_content(R"({"ok":false})", "application/json");
#endif
            });
        });

    // ── GET /api/v1/jack/cables — list virtual audio cables ─────────────
    svr.Get("/api/v1/jack/cables",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                json r; r["ok"] = true;
                json cables = json::array();
                if (g_pipeline) {
                    for (const auto& c : g_pipeline->jack_manager().list_cables()) {
                        cables.push_back({{"id", c.id}, {"capture", c.capture_name}, {"playback", c.playback_name}});
                    }
                }
                r["cables"] = cables;
                res.set_content(r.dump(2), "application/json");
#else
                res.set_content(R"({"ok":false})", "application/json");
#endif
            });
        });

    // ── POST /api/v1/jack/cables — create a virtual cable ───────────────
    svr.Post("/api/v1/jack/cables",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                json r;
                if (g_pipeline) {
                    int id = g_pipeline->jack_manager().create_cable();
                    r["ok"] = (id > 0); r["cable_id"] = id;
                } else { r["ok"] = false; }
                res.set_content(r.dump(), "application/json");
#else
                res.set_content(R"({"ok":false})", "application/json");
#endif
            });
        });

    // ── DELETE /api/v1/jack/cables/{id} — destroy a virtual cable ────────
    svr.Delete(R"(/api/v1/jack/cables/(\d+))",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                int id = std::stoi(req.matches[1].str());
                json r;
                if (g_pipeline) {
                    r["ok"] = g_pipeline->jack_manager().destroy_cable(id);
                } else { r["ok"] = false; }
                res.set_content(r.dump(), "application/json");
#else
                res.set_content(R"({"ok":false})", "application/json");
#endif
            });
        });

    // ── POST /api/v1/jack/connect — connect two JACK ports ──────────────
    svr.Post("/api/v1/jack/connect",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                json body;
                try { body = json::parse(req.body); } catch (...) { body = json::object(); }
                std::string src = body.value("src", "");
                std::string dst = body.value("dst", "");
                json r;
                if (g_pipeline && !src.empty() && !dst.empty()) {
                    r["ok"] = g_pipeline->jack_manager().connect_ports(src, dst);
                } else { r["ok"] = false; r["error"] = "src and dst required"; }
                res.set_content(r.dump(), "application/json");
#else
                res.set_content(R"({"ok":false})", "application/json");
#endif
            });
        });

    // ── POST /api/v1/jack/disconnect — disconnect two JACK ports ────────
    svr.Post("/api/v1/jack/disconnect",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                json body;
                try { body = json::parse(req.body); } catch (...) { body = json::object(); }
                std::string src = body.value("src", "");
                std::string dst = body.value("dst", "");
                json r;
                if (g_pipeline && !src.empty() && !dst.empty()) {
                    r["ok"] = g_pipeline->jack_manager().disconnect_ports(src, dst);
                } else { r["ok"] = false; r["error"] = "src and dst required"; }
                res.set_content(r.dump(), "application/json");
#else
                res.set_content(R"({"ok":false})", "application/json");
#endif
            });
        });

    // ── GET /api/v1/dnas/stats — proxy live stats from Mcaster1DNAS server ─
    // We use gAdminConfig.dnas (populated from dnas: YAML section) so the
    // host, port, credentials, and stats URL are not hardcoded here.
    svr.Get("/api/v1/dnas/stats",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                const mc1DnasConfig& dnas = gAdminConfig.dnas;
                std::string source_label  = std::string(dnas.host) + ":"
                                          + std::to_string(dnas.port);

                httplib::SSLClient cli(dnas.host, dnas.port);
                cli.enable_server_certificate_verification(false);
                if (dnas.username[0])
                    cli.set_basic_auth(dnas.username, dnas.password);
                cli.set_connection_timeout(5);
                cli.set_read_timeout(10);

                const char* stats_path = dnas.stats_url[0]
                                       ? dnas.stats_url
                                       : "/admin/mcaster1stats";
                auto r = cli.Get(stats_path);
                if (!r) {
                    res.status = 502;
                    json e;
                    e["error"]  = "DNAS unreachable";
                    e["source"] = source_label;
                    res.set_content(e.dump(), "application/json");
                    return;
                }

                json j;
                j["ok"]           = (r->status == 200);
                j["http_status"]  = r->status;
                j["content_type"] = r->get_header_value("Content-Type");
                j["body"]         = r->body;
                j["source"]       = source_label + stats_path;
                res.set_content(j.dump(2), "application/json");
            });
        });

#ifndef MC1_HTTP_TEST_BUILD
    // ── GET /api/v1/system/health — latest HealthSnapshot as JSON ─────────
    svr.Get("/api/v1/system/health",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                const HealthSnapshot snap = SystemHealth::instance().getSnapshot();
                json j;
                j["ok"]           = (snap.sampled_at > 0);
                j["sampled_at"]   = (long long)snap.sampled_at;
                j["cpu_pct"]      = snap.cpu_pct;
                j["mem_used_mb"]  = snap.mem_used_mb;
                j["mem_total_mb"] = snap.mem_total_mb;
                j["mem_pct"]      = snap.mem_pct;
                j["net_in_kbps"]  = snap.net_in_kbps;
                j["net_out_kbps"] = snap.net_out_kbps;
                j["net_iface"]    = snap.net_iface;
                j["thread_count"] = snap.thread_count;
                json slots = json::array();
                for (const auto& sl : snap.slots) {
                    json s;
                    s["slot_id"]     = sl.slot_id;
                    s["state"]       = sl.state;
                    s["bytes_out"]   = sl.bytes_out;
                    s["out_kbps"]    = sl.out_kbps;
                    s["track_title"] = sl.track_title;
                    // Merge live listener count from ServerMonitor via slot's mount
                    std::string mount;
                    if (g_pipeline) {
                        EncoderConfig ec;
                        if (g_pipeline->get_slot_config(sl.slot_id, ec))
                            mount = ec.stream_target.mount;
                    }
                    s["listeners"] = ServerMonitor::instance().getListenersByMount(mount);
                    slots.push_back(s);
                }
                j["slots"] = slots;
                res.set_content(j.dump(2), "application/json");
            });
        });

    // ── GET /api/v1/system/health/history — last N snapshots ──────────────
    svr.Get("/api/v1/system/health/history",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                int n = 60;
                if (req.has_param("n")) {
                    try { n = std::stoi(req.get_param_value("n")); } catch (...) {}
                    if (n < 1)  n = 1;
                    if (n > 120) n = 120;
                }
                auto history = SystemHealth::instance().getHistory(n);
                json arr = json::array();
                for (const auto& snap : history) {
                    json j;
                    j["sampled_at"]   = (long long)snap.sampled_at;
                    j["cpu_pct"]      = snap.cpu_pct;
                    j["mem_used_mb"]  = snap.mem_used_mb;
                    j["mem_total_mb"] = snap.mem_total_mb;
                    j["mem_pct"]      = snap.mem_pct;
                    j["net_in_kbps"]  = snap.net_in_kbps;
                    j["net_out_kbps"] = snap.net_out_kbps;
                    j["thread_count"] = snap.thread_count;
                    arr.push_back(j);
                }
                json out;
                out["ok"]   = true;
                out["data"] = arr;
                res.set_content(out.dump(2), "application/json");
            });
        });

    // ── GET /api/v1/server_monitors/stats — all servers (or one) ──────────
    svr.Get("/api/v1/server_monitors/stats",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                json out;
                out["ok"] = true;
                if (req.has_param("id")) {
                    int id = 0;
                    try { id = std::stoi(req.get_param_value("id")); } catch (...) {}
                    auto srv = ServerMonitor::instance().getById(id);
                    if (!srv) {
                        res.status = 404;
                        out["ok"]    = false;
                        out["error"] = "server_id not found";
                        res.set_content(out.dump(), "application/json");
                        return;
                    }
                    json s;
                    s["server_id"]      = srv->server_id;
                    s["name"]           = srv->name;
                    s["server_type"]    = srv->server_type;
                    s["host"]           = srv->host;
                    s["port"]           = srv->port;
                    s["status"]         = srv->status;
                    s["listeners"]      = srv->listeners_total;
                    s["max_listeners"]  = srv->max_listeners;
                    s["sources_total"]  = srv->sources_total;
                    s["sources_online"] = srv->sources_online;
                    s["out_kbps"]       = srv->out_kbps;
                    s["uptime"]         = srv->uptime;
                    s["server_id_str"]  = srv->server_id_str;
                    s["polled_at"]      = (long long)srv->polled_at;
                    s["fetch_ms"]       = srv->fetch_ms;
                    s["error"]          = srv->error;
                    json mounts = json::array();
                    for (const auto& m : srv->mounts) {
                        json ms;
                        ms["mount"]      = m.mount;
                        ms["title"]      = m.title;
                        ms["codec"]      = m.codec;
                        ms["listeners"]  = m.listeners;
                        ms["peak"]       = m.peak;
                        ms["bitrate"]    = m.bitrate;
                        ms["out_kbps"]   = m.out_kbps;
                        ms["online"]     = m.online;
                        ms["ours"]       = m.ours;
                        mounts.push_back(ms);
                    }
                    s["mounts"] = mounts;
                    out["server"] = s;
                } else {
                    auto all = ServerMonitor::instance().getAll();
                    json arr = json::array();
                    for (const auto& srv : all) {
                        json s;
                        s["server_id"]      = srv.server_id;
                        s["name"]           = srv.name;
                        s["server_type"]    = srv.server_type;
                        s["host"]           = srv.host;
                        s["port"]           = srv.port;
                        s["status"]         = srv.status;
                        s["listeners"]      = srv.listeners_total;
                        s["max_listeners"]  = srv.max_listeners;
                        s["sources_total"]  = srv.sources_total;
                        s["sources_online"] = srv.sources_online;
                        s["out_kbps"]       = srv.out_kbps;
                        s["uptime"]         = srv.uptime;
                        s["polled_at"]      = (long long)srv.polled_at;
                        s["fetch_ms"]       = srv.fetch_ms;
                        s["status_str"]     = srv.server_id_str;
                        s["error"]          = srv.error;
                        arr.push_back(s);
                    }
                    out["servers"] = arr;
                    out["count"]   = (int)all.size();
                }
                res.set_content(out.dump(2), "application/json");
            });
        });

    // ── POST /api/v1/server_monitors/poll — force immediate re-poll ────────
    svr.Post("/api/v1/server_monitors/poll",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                int server_id = -1;  // -1 = all
                try {
                    auto body = json::parse(req.body);
                    if (body.contains("id") && body["id"].is_number_integer())
                        server_id = body["id"].get<int>();
                } catch (...) {}
                ServerMonitor::instance().pollNow(server_id);
                json out;
                out["ok"]       = true;
                out["polled"]   = (server_id == -1) ? "all" : std::to_string(server_id);
                res.set_content(out.dump(), "application/json");
            });
        });
#endif  // MC1_HTTP_TEST_BUILD

    // ── GET /api/v1/effects/meters — live meter data from effects rack ──────
    svr.Get("/api/v1/effects/meters",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                if (!g_pipeline) {
                    res.status = 503;
                    res.set_content(R"({"error":"No pipeline"})", "application/json");
                    return;
                }
                auto& rack = g_pipeline->global_effects_rack();
                auto meters = rack.get_all_meters();
                auto rack_json = rack.to_json();
                json arr = json::array();
                std::unordered_map<int, std::string> id_to_type;
                if (rack_json.contains("units") && rack_json["units"].is_array()) {
                    for (const auto& u : rack_json["units"]) {
                        if (u.contains("id") && u.contains("type"))
                            id_to_type[u["id"].get<int>()] = u["type"].get<std::string>();
                    }
                }
                for (const auto& [uid, md] : meters) {
                    json entry;
                    entry["id"]       = uid;
                    entry["type"]     = id_to_type.count(uid) ? id_to_type[uid] : "unknown";
                    entry["input_db"]  = std::round(md.input_db * 10.0f) / 10.0f;
                    entry["output_db"] = std::round(md.output_db * 10.0f) / 10.0f;
                    if (md.gain_reduction_db != 0.0f)
                        entry["gain_reduction_db"] = std::round(md.gain_reduction_db * 10.0f) / 10.0f;
                    if (!md.gate_open)
                        entry["gate_open"] = false;
                    if (!md.eq_response.empty()) {
                        json eq_arr = json::array();
                        for (float v : md.eq_response) eq_arr.push_back(std::round(v * 10000.0f) / 10000.0f);
                        entry["eq_response"] = eq_arr;
                    }
                    arr.push_back(entry);
                }
                res.set_content(arr.dump(), "application/json");
#else
                res.set_content("[]", "application/json");
#endif
            });
        });

    // ── GET /api/v1/effects/global/routing — get routing table ──────────────
    svr.Get("/api/v1/effects/global/routing",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                if (!g_pipeline) {
                    res.status = 503;
                    res.set_content(R"({"error":"Pipeline not running"})", "application/json");
                    return;
                }
                auto routing = g_pipeline->global_effects_rack().get_routing();
                json arr = json::array();
                for (const auto& r : routing) {
                    arr.push_back({
                        {"from", r.from_unit}, {"to", r.to_unit},
                        {"from_port", r.from_port}, {"to_port", r.to_port}
                    });
                }
                json result; result["ok"] = true; result["routing"] = arr;
                res.set_content(result.dump(2), "application/json");
#else
                res.set_content(R"({"ok":true,"routing":[]})", "application/json");
#endif
            });
        });

    // ── PUT /api/v1/effects/global/routing — set routing table ──────────────
    svr.Put("/api/v1/effects/global/routing",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
#ifndef MC1_HTTP_TEST_BUILD
                json body;
                try { body = json::parse(req.body); }
                catch (...) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                    return;
                }
                if (!g_pipeline || !body.contains("routing") || !body["routing"].is_array()) {
                    res.status = 400;
                    res.set_content(R"({"error":"routing array required"})", "application/json");
                    return;
                }
                std::vector<mc1dsp::RoutingEntry> routing;
                for (const auto& r : body["routing"]) {
                    mc1dsp::RoutingEntry entry;
                    entry.from_unit = r.value("from", "");
                    entry.to_unit   = r.value("to", "");
                    entry.from_port = r.value("from_port", 0);
                    entry.to_port   = r.value("to_port", 0);
                    routing.push_back(entry);
                }
                g_pipeline->global_effects_rack().set_routing(routing);
                auto result_routing = g_pipeline->global_effects_rack().get_routing();
                json arr = json::array();
                for (const auto& r : result_routing) {
                    arr.push_back({
                        {"from", r.from_unit}, {"to", r.to_unit},
                        {"from_port", r.from_port}, {"to_port", r.to_port}
                    });
                }
                json result; result["ok"] = true; result["routing"] = arr;
                result["rack"] = g_pipeline->global_effects_rack().to_json();
                res.set_content(result.dump(2), "application/json");
#else
                res.set_content(R"({"ok":true,"routing":[]})", "application/json");
#endif
            });
        });

    // ── GET /api/v1/effects/profiles — list all effects profiles ────────────
    svr.Get("/api/v1/effects/profiles",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                auto& db = Mc1Db::instance();
                std::string sql =
                    "SELECT id, user_id, profile_name, description, category, "
                    "effects_chain_json, is_public, use_count, created_at, updated_at "
                    "FROM mcaster1_encoder.mixer_custom_units "
                    "WHERE user_id=1 OR is_public=1";
                std::string cat = req.get_param_value("category");
                if (!cat.empty()) {
                    sql += " AND category='" + db.escape(cat) + "'";
                }
                sql += " ORDER BY use_count DESC, profile_name ASC";
                auto rows = db.query(sql);
                json arr = json::array();
                for (const auto& row : rows) {
                    json p;
                    p["id"]            = std::stoi(row.at("id"));
                    p["user_id"]       = std::stoi(row.at("user_id"));
                    p["profile_name"]  = row.at("profile_name");
                    p["description"]   = row.at("description");
                    p["category"]      = row.at("category");
                    try { p["effects_chain_json"] = json::parse(row.at("effects_chain_json")); }
                    catch (...) { p["effects_chain_json"] = json::array(); }
                    p["is_public"]     = row.at("is_public") == "1";
                    p["use_count"]     = std::stoi(row.at("use_count"));
                    p["created_at"]    = row.at("created_at");
                    p["updated_at"]    = row.at("updated_at");
                    arr.push_back(p);
                }
                json r; r["ok"] = true; r["profiles"] = arr;
                res.set_content(r.dump(2), "application/json");
            });
        });

    // ── GET /api/v1/effects/profiles/{id} — get single profile ──────────────
    svr.Get(R"(/api/v1/effects/profiles/(\d+))",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                int pid = std::stoi(req.matches[1].str());
                auto& db = Mc1Db::instance();
                auto rows = db.query(
                    "SELECT id, user_id, profile_name, description, category, "
                    "effects_chain_json, is_public, use_count, created_at, updated_at "
                    "FROM mcaster1_encoder.mixer_custom_units WHERE id=" + std::to_string(pid));
                if (rows.empty()) {
                    res.status = 404;
                    res.set_content(R"({"error":"Profile not found"})", "application/json");
                    return;
                }
                const auto& row = rows[0];
                json p;
                p["id"]            = std::stoi(row.at("id"));
                p["user_id"]       = std::stoi(row.at("user_id"));
                p["profile_name"]  = row.at("profile_name");
                p["description"]   = row.at("description");
                p["category"]      = row.at("category");
                try { p["effects_chain_json"] = json::parse(row.at("effects_chain_json")); }
                catch (...) { p["effects_chain_json"] = json::array(); }
                p["is_public"]     = row.at("is_public") == "1";
                p["use_count"]     = std::stoi(row.at("use_count"));
                p["created_at"]    = row.at("created_at");
                p["updated_at"]    = row.at("updated_at");
                json r; r["ok"] = true; r["profile"] = p;
                res.set_content(r.dump(2), "application/json");
            });
        });

    // ── POST /api/v1/effects/profiles — create new profile ──────────────────
    svr.Post("/api/v1/effects/profiles",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                json body;
                try { body = json::parse(req.body); }
                catch (...) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                    return;
                }
                auto& db = Mc1Db::instance();
                std::string name = body.value("profile_name", "");
                std::string desc = body.value("description", "");
                std::string cat  = body.value("category", "custom");
                std::string chain_str = body.contains("effects_chain_json")
                    ? body["effects_chain_json"].dump() : "[]";
                if (name.empty()) {
                    res.status = 400;
                    res.set_content(R"({"error":"profile_name required"})", "application/json");
                    return;
                }
                bool ok = db.execf(
                    "INSERT INTO mcaster1_encoder.mixer_custom_units "
                    "(user_id, profile_name, description, category, effects_chain_json, is_public) "
                    "VALUES (1, '%s', '%s', '%s', '%s', 0)",
                    db.escape(name).c_str(),
                    db.escape(desc).c_str(),
                    db.escape(cat).c_str(),
                    db.escape(chain_str).c_str());
                json r; r["ok"] = ok;
                if (!ok) r["error"] = "Insert failed (duplicate name?)";
                res.set_content(r.dump(), "application/json");
            });
        });

    // ── POST /api/v1/effects/profiles/save-current — save current rack as profile
    svr.Post("/api/v1/effects/profiles/save-current",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                json body;
                try { body = json::parse(req.body); }
                catch (...) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                    return;
                }
                std::string name = body.value("profile_name", "");
                std::string desc = body.value("description", "");
                std::string cat  = body.value("category", "custom");
                if (name.empty()) {
                    res.status = 400;
                    res.set_content(R"({"error":"profile_name required"})", "application/json");
                    return;
                }
#ifndef MC1_HTTP_TEST_BUILD
                if (!g_pipeline) {
                    res.status = 503;
                    res.set_content(R"({"error":"No pipeline"})", "application/json");
                    return;
                }
                auto rack_json = g_pipeline->global_effects_rack().to_json();
                json chain = json::array();
                if (rack_json.contains("units") && rack_json["units"].is_array()) {
                    for (const auto& u : rack_json["units"]) {
                        json entry;
                        entry["type"] = u.value("type", "unknown");
                        if (u.contains("params")) entry["params"] = u["params"];
                        chain.push_back(entry);
                    }
                }
                auto& db = Mc1Db::instance();
                std::string chain_str = chain.dump();
                bool ok = db.execf(
                    "INSERT INTO mcaster1_encoder.mixer_custom_units "
                    "(user_id, profile_name, description, category, effects_chain_json, is_public) "
                    "VALUES (1, '%s', '%s', '%s', '%s', 0) "
                    "ON DUPLICATE KEY UPDATE effects_chain_json='%s', description='%s', "
                    "category='%s', updated_at=NOW()",
                    db.escape(name).c_str(),
                    db.escape(desc).c_str(),
                    db.escape(cat).c_str(),
                    db.escape(chain_str).c_str(),
                    db.escape(chain_str).c_str(),
                    db.escape(desc).c_str(),
                    db.escape(cat).c_str());
                json r; r["ok"] = ok;
                r["effects_chain_json"] = chain;
                res.set_content(r.dump(2), "application/json");
#else
                res.set_content(R"({"ok":false})", "application/json");
#endif
            });
        });

    // ── POST /api/v1/effects/profiles/{id}/apply — apply a profile to the rack
    svr.Post(R"(/api/v1/effects/profiles/(\d+)/apply)",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                int pid = std::stoi(req.matches[1].str());
                auto& db = Mc1Db::instance();
                auto rows = db.query(
                    "SELECT effects_chain_json FROM mcaster1_encoder.mixer_custom_units WHERE id=" + std::to_string(pid));
                if (rows.empty()) {
                    res.status = 404;
                    res.set_content(R"({"error":"Profile not found"})", "application/json");
                    return;
                }
#ifndef MC1_HTTP_TEST_BUILD
                if (!g_pipeline) {
                    res.status = 503;
                    res.set_content(R"({"error":"No pipeline"})", "application/json");
                    return;
                }
                json chain;
                try { chain = json::parse(rows[0].at("effects_chain_json")); }
                catch (...) {
                    res.status = 500;
                    res.set_content(R"({"error":"Invalid chain JSON in DB"})", "application/json");
                    return;
                }
                auto& rack = g_pipeline->global_effects_rack();
                auto cur = rack.to_json();
                if (cur.contains("units") && cur["units"].is_array()) {
                    std::vector<int> ids;
                    for (const auto& u : cur["units"]) {
                        if (u.contains("id")) ids.push_back(u["id"].get<int>());
                    }
                    for (int uid : ids) rack.remove_unit(uid);
                }
                int added = 0;
                for (const auto& unit : chain) {
                    if (!unit.contains("type")) continue;
                    std::string type = unit["type"].get<std::string>();
                    auto new_unit = rack.create_unit(type);
                    if (new_unit) {
                        if (unit.contains("params")) {
                            new_unit->set_params(unit["params"]);
                        }
                        rack.add_unit(std::move(new_unit));
                        added++;
                    }
                }
                db.execf("UPDATE mcaster1_encoder.mixer_custom_units SET use_count=use_count+1 WHERE id=%d", pid);
                json r; r["ok"] = true; r["applied_count"] = added;
                r["rack"] = rack.to_json();
                res.set_content(r.dump(2), "application/json");
#else
                res.set_content(R"({"ok":false})", "application/json");
#endif
            });
        });

    // ── PUT /api/v1/effects/profiles/{id} — update a profile ────────────────
    svr.Put(R"(/api/v1/effects/profiles/(\d+))",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                int pid = std::stoi(req.matches[1].str());
                json body;
                try { body = json::parse(req.body); }
                catch (...) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                    return;
                }
                auto& db = Mc1Db::instance();
                auto check = db.query(
                    "SELECT user_id FROM mcaster1_encoder.mixer_custom_units WHERE id=" + std::to_string(pid));
                if (check.empty()) {
                    res.status = 404;
                    res.set_content(R"({"error":"Profile not found"})", "application/json");
                    return;
                }
                if (check[0].at("user_id") == "0") {
                    res.status = 403;
                    res.set_content(R"({"error":"Cannot modify built-in profiles"})", "application/json");
                    return;
                }
                std::string sets;
                if (body.contains("profile_name"))
                    sets += "profile_name='" + db.escape(body["profile_name"].get<std::string>()) + "',";
                if (body.contains("description"))
                    sets += "description='" + db.escape(body["description"].get<std::string>()) + "',";
                if (body.contains("category"))
                    sets += "category='" + db.escape(body["category"].get<std::string>()) + "',";
                if (body.contains("effects_chain_json"))
                    sets += "effects_chain_json='" + db.escape(body["effects_chain_json"].dump()) + "',";
                if (sets.empty()) {
                    res.set_content(R"({"ok":true})", "application/json");
                    return;
                }
                sets.pop_back();
                bool ok = db.execf("UPDATE mcaster1_encoder.mixer_custom_units SET %s WHERE id=%d",
                    sets.c_str(), pid);
                json r; r["ok"] = ok;
                res.set_content(r.dump(), "application/json");
            });
        });

    // ── DELETE /api/v1/effects/profiles/{id} — delete a profile ─────────────
    svr.Delete(R"(/api/v1/effects/profiles/(\d+))",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                int pid = std::stoi(req.matches[1].str());
                auto& db = Mc1Db::instance();
                auto check = db.query(
                    "SELECT user_id FROM mcaster1_encoder.mixer_custom_units WHERE id=" + std::to_string(pid));
                if (check.empty()) {
                    res.status = 404;
                    res.set_content(R"({"error":"Profile not found"})", "application/json");
                    return;
                }
                if (check[0].at("user_id") == "0") {
                    res.status = 403;
                    res.set_content(R"({"error":"Cannot delete built-in profiles"})", "application/json");
                    return;
                }
                bool ok = db.execf("DELETE FROM mcaster1_encoder.mixer_custom_units WHERE id=%d AND user_id=1", pid);
                json r; r["ok"] = ok;
                res.set_content(r.dump(), "application/json");
            });
        });

    // ── GET /api/v1/pedalboard/layout — get pedalboard layout ───────────────
    svr.Get("/api/v1/pedalboard/layout",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                std::string slot_id_str = req.get_param_value("slot_id");
                auto& db = Mc1Db::instance();
                std::string sql = "SELECT layout_json, cable_json FROM mcaster1_encoder.pedalboard_layouts WHERE user_id=1";
                if (!slot_id_str.empty() && slot_id_str != "null") {
                    sql += " AND slot_id=" + db.escape(slot_id_str);
                } else {
                    sql += " AND slot_id IS NULL";
                }
                sql += " ORDER BY updated_at DESC LIMIT 1";
                auto rows = db.query(sql);
                json r; r["ok"] = true;
                if (!rows.empty()) {
                    try { r["layout_json"] = json::parse(rows[0].at("layout_json")); }
                    catch (...) { r["layout_json"] = json::object(); }
                    try { r["cable_json"] = json::parse(rows[0].at("cable_json")); }
                    catch (...) { r["cable_json"] = json::array(); }
                } else {
                    r["layout_json"] = json::object();
                    r["cable_json"] = json::array();
                }
                res.set_content(r.dump(2), "application/json");
            });
        });

    // ── PUT /api/v1/pedalboard/layout — save pedalboard layout ──────────────
    svr.Put("/api/v1/pedalboard/layout",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                json body;
                try { body = json::parse(req.body); }
                catch (...) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                    return;
                }
                auto& db = Mc1Db::instance();
                std::string layout_str = body.contains("layout_json") ? body["layout_json"].dump() : "{}";
                std::string cable_str = body.contains("cable_json") ? body["cable_json"].dump() : "[]";
                std::string slot_clause;
                if (body.contains("slot_id") && !body["slot_id"].is_null()) {
                    slot_clause = body["slot_id"].dump();
                } else {
                    slot_clause = "NULL";
                }
                std::string check_sql = "SELECT id FROM mcaster1_encoder.pedalboard_layouts WHERE user_id=1 AND ";
                if (slot_clause == "NULL") {
                    check_sql += "slot_id IS NULL";
                } else {
                    check_sql += "slot_id=" + db.escape(slot_clause);
                }
                check_sql += " LIMIT 1";
                auto existing = db.query(check_sql);
                bool ok;
                if (!existing.empty()) {
                    ok = db.execf("UPDATE mcaster1_encoder.pedalboard_layouts SET layout_json='%s', cable_json='%s', updated_at=NOW() WHERE id=%s",
                        db.escape(layout_str).c_str(), db.escape(cable_str).c_str(), existing[0].at("id").c_str());
                } else {
                    ok = db.execf("INSERT INTO mcaster1_encoder.pedalboard_layouts (user_id, slot_id, layout_json, cable_json) VALUES (1, %s, '%s', '%s')",
                        slot_clause.c_str(), db.escape(layout_str).c_str(), db.escape(cable_str).c_str());
                }
                json r; r["ok"] = ok;
                res.set_content(r.dump(), "application/json");
            });
        });

    // ── GET /api/v1/mixer/config — get current mixer configuration ──────────
    svr.Get("/api/v1/mixer/config",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                auto& db = Mc1Db::instance();
                auto rows = db.query(
                    "SELECT id, config_name, skin, channel_json, master_volume "
                    "FROM mcaster1_encoder.mixer_configs "
                    "WHERE user_id=1 ORDER BY updated_at DESC LIMIT 1");
                json r; r["ok"] = true;
                if (!rows.empty()) {
                    r["config_id"]    = std::stoi(rows[0].at("id"));
                    r["config_name"]  = rows[0].at("config_name");
                    r["skin"]         = rows[0].at("skin");
                    r["master_volume"] = std::stof(rows[0].at("master_volume"));
                    try { r["channel_json"] = json::parse(rows[0].at("channel_json")); }
                    catch (...) { r["channel_json"] = json::array(); }
                } else {
                    r["config_name"]   = "Default";
                    r["skin"]          = "broadcast_dark";
                    r["master_volume"] = 1.0;
                    r["channel_json"]  = json::array();
                }
                res.set_content(r.dump(2), "application/json");
            });
        });

    // ── PUT /api/v1/mixer/config — save mixer configuration ─────────────────
    svr.Put("/api/v1/mixer/config",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                json body;
                try { body = json::parse(req.body); }
                catch (...) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                    return;
                }
                auto& db = Mc1Db::instance();
                std::string config_name  = body.value("config_name", "Default");
                std::string skin         = body.value("skin", "broadcast_dark");
                float master_vol         = body.value("master_volume", 1.0f);
                std::string channel_str  = body.contains("channel_json") ? body["channel_json"].dump() : "[]";

                auto existing = db.query(
                    "SELECT id FROM mcaster1_encoder.mixer_configs WHERE user_id=1 LIMIT 1");
                bool ok;
                if (!existing.empty()) {
                    ok = db.execf(
                        "UPDATE mcaster1_encoder.mixer_configs SET "
                        "config_name='%s', skin='%s', channel_json='%s', "
                        "master_volume=%f, updated_at=NOW() WHERE id=%s",
                        db.escape(config_name).c_str(),
                        db.escape(skin).c_str(),
                        db.escape(channel_str).c_str(),
                        master_vol,
                        existing[0].at("id").c_str());
                } else {
                    ok = db.execf(
                        "INSERT INTO mcaster1_encoder.mixer_configs "
                        "(user_id, config_name, skin, channel_json, master_volume) "
                        "VALUES (1, '%s', '%s', '%s', %f)",
                        db.escape(config_name).c_str(),
                        db.escape(skin).c_str(),
                        db.escape(channel_str).c_str(),
                        master_vol);
                }
                json r; r["ok"] = ok;
                res.set_content(r.dump(), "application/json");
            });
        });

    // ── GET /api/v1/mixer/presets — list all mixer presets ───────────────────
    svr.Get("/api/v1/mixer/presets",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                auto& db = Mc1Db::instance();
                auto rows = db.query(
                    "SELECT id, config_name, skin, master_volume, created_at, updated_at "
                    "FROM mcaster1_encoder.mixer_configs WHERE user_id=1 "
                    "ORDER BY updated_at DESC");
                json arr = json::array();
                for (const auto& row : rows) {
                    json p;
                    p["id"]            = std::stoi(row.at("id"));
                    p["config_name"]   = row.at("config_name");
                    p["skin"]          = row.at("skin");
                    p["master_volume"] = std::stof(row.at("master_volume"));
                    p["created_at"]    = row.at("created_at");
                    p["updated_at"]    = row.at("updated_at");
                    arr.push_back(p);
                }
                json r; r["ok"] = true; r["presets"] = arr;
                res.set_content(r.dump(2), "application/json");
            });
        });

    // ── GET /api/v1/mixer/presets/{id} — get single mixer preset ────────────
    svr.Get(R"(/api/v1/mixer/presets/(\d+))",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                int pid = std::stoi(req.matches[1].str());
                auto& db = Mc1Db::instance();
                auto rows = db.query(
                    "SELECT id, config_name, skin, channel_json, master_volume "
                    "FROM mcaster1_encoder.mixer_configs WHERE id=" + std::to_string(pid) + " AND user_id=1");
                if (rows.empty()) {
                    res.status = 404;
                    res.set_content(R"({"error":"Preset not found"})", "application/json");
                    return;
                }
                const auto& row = rows[0];
                json r; r["ok"] = true;
                r["config_id"]     = std::stoi(row.at("id"));
                r["config_name"]   = row.at("config_name");
                r["skin"]          = row.at("skin");
                r["master_volume"] = std::stof(row.at("master_volume"));
                try { r["channel_json"] = json::parse(row.at("channel_json")); }
                catch (...) { r["channel_json"] = json::array(); }
                res.set_content(r.dump(2), "application/json");
            });
        });

    // ── POST /api/v1/mixer/presets — create new mixer preset ────────────────
    svr.Post("/api/v1/mixer/presets",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                json body;
                try { body = json::parse(req.body); }
                catch (...) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                    return;
                }
                auto& db = Mc1Db::instance();
                std::string config_name = body.value("config_name", "");
                std::string skin        = body.value("skin", "broadcast_dark");
                float master_vol        = body.value("master_volume", 1.0f);
                std::string channel_str = body.contains("channel_json") ? body["channel_json"].dump() : "[]";
                if (config_name.empty()) {
                    res.status = 400;
                    res.set_content(R"({"error":"config_name required"})", "application/json");
                    return;
                }
                bool ok = db.execf(
                    "INSERT INTO mcaster1_encoder.mixer_configs "
                    "(user_id, config_name, skin, channel_json, master_volume) "
                    "VALUES (1, '%s', '%s', '%s', %f)",
                    db.escape(config_name).c_str(),
                    db.escape(skin).c_str(),
                    db.escape(channel_str).c_str(),
                    master_vol);
                json r; r["ok"] = ok;
                res.set_content(r.dump(), "application/json");
            });
        });

    // ── DELETE /api/v1/mixer/presets/{id} — delete a mixer preset ────────────
    svr.Delete(R"(/api/v1/mixer/presets/(\d+))",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                int pid = std::stoi(req.matches[1].str());
                auto& db = Mc1Db::instance();
                bool ok = db.execf(
                    "DELETE FROM mcaster1_encoder.mixer_configs WHERE id=%d AND user_id=1", pid);
                json r; r["ok"] = ok;
                res.set_content(r.dump(), "application/json");
            });
        });

    // ── GET /api/v1/ai/status — AI/Ollama availability ──────────────────────
    svr.Get("/api/v1/ai/status",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                json out;
                out["ok"] = true;
                out["ai_enabled"] = gAdminConfig.ollama.enabled ? true : false;
                if (g_ollama) {
                    bool avail = g_ollama->is_available();
                    out["available"] = avail;
                    out["endpoint"]  = g_ollama->endpoint();
                    out["model"]     = g_ollama->model();
                    out["timeout_sec"] = gAdminConfig.ollama.timeout_sec;
                    if (avail) {
                        auto models_resp = g_ollama->list_models();
                        if (models_resp.contains("models"))
                            out["models"] = models_resp["models"];
                        else
                            out["models"] = json::array();
                    }
                } else {
                    out["available"] = false;
                    if (!gAdminConfig.ollama.enabled)
                        out["error"] = "AI is disabled in configuration";
                    else
                        out["error"] = "Ollama client not initialized";
                }
                res.set_content(out.dump(2), "application/json");
            });
        });

    // ── GET /api/v1/ai/config — AI configuration ────────────────────────────
    svr.Get("/api/v1/ai/config",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                json out;
                out["ok"]          = true;
                out["enabled"]     = gAdminConfig.ollama.enabled ? true : false;
                out["endpoint"]    = std::string(gAdminConfig.ollama.endpoint);
                out["model"]       = std::string(gAdminConfig.ollama.model);
                out["timeout_sec"] = gAdminConfig.ollama.timeout_sec;

                json status;
                if (g_ollama) {
                    bool avail = g_ollama->is_available();
                    status["reachable"] = avail;
                    if (avail) {
                        auto models_resp = g_ollama->list_models();
                        if (models_resp.contains("models"))
                            status["models"] = models_resp["models"];
                        else
                            status["models"] = json::array();
                    }
                } else {
                    status["reachable"] = false;
                }
                out["status"] = status;
                res.set_content(out.dump(2), "application/json");
            });
        });

    // ── PUT /api/v1/ai/config — update AI configuration at runtime ──────────
    svr.Put("/api/v1/ai/config",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                json body;
                try { body = json::parse(req.body); } catch (...) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                    return;
                }

                if (body.contains("endpoint") && body["endpoint"].is_string()) {
                    std::string ep = body["endpoint"].get<std::string>();
                    memset(gAdminConfig.ollama.endpoint, 0, sizeof(gAdminConfig.ollama.endpoint));
                    strncpy(gAdminConfig.ollama.endpoint, ep.c_str(),
                            sizeof(gAdminConfig.ollama.endpoint) - 1);
                }
                if (body.contains("model") && body["model"].is_string()) {
                    std::string m = body["model"].get<std::string>();
                    memset(gAdminConfig.ollama.model, 0, sizeof(gAdminConfig.ollama.model));
                    strncpy(gAdminConfig.ollama.model, m.c_str(),
                            sizeof(gAdminConfig.ollama.model) - 1);
                }
                if (body.contains("timeout_sec") && body["timeout_sec"].is_number())
                    gAdminConfig.ollama.timeout_sec = body["timeout_sec"].get<int>();
                if (body.contains("enabled") && body["enabled"].is_boolean())
                    gAdminConfig.ollama.enabled = body["enabled"].get<bool>() ? 1 : 0;

                if (gAdminConfig.ollama.enabled) {
                    if (g_ollama) {
                        g_ollama->set_endpoint(gAdminConfig.ollama.endpoint);
                        g_ollama->set_model(gAdminConfig.ollama.model);
                        g_ollama->set_timeout(gAdminConfig.ollama.timeout_sec);
                    } else {
                        g_ollama = new mc1vt::OllamaClient(
                            gAdminConfig.ollama.endpoint,
                            gAdminConfig.ollama.model,
                            gAdminConfig.ollama.timeout_sec);
                    }
                    MC1_INFO("Ollama AI config updated — endpoint="
                             + std::string(gAdminConfig.ollama.endpoint)
                             + " model=" + std::string(gAdminConfig.ollama.model)
                             + " timeout=" + std::to_string(gAdminConfig.ollama.timeout_sec) + "s");
                } else {
                    delete g_ollama;
                    g_ollama = nullptr;
                    MC1_INFO("Ollama AI disabled at runtime via PUT /api/v1/ai/config");
                }

                json out;
                out["ok"]          = true;
                out["enabled"]     = gAdminConfig.ollama.enabled ? true : false;
                out["endpoint"]    = std::string(gAdminConfig.ollama.endpoint);
                out["model"]       = std::string(gAdminConfig.ollama.model);
                out["timeout_sec"] = gAdminConfig.ollama.timeout_sec;
                if (g_ollama)
                    out["reachable"] = g_ollama->is_available();
                else
                    out["reachable"] = false;
                res.set_content(out.dump(2), "application/json");
            });
        });

    // ── GET /api/v1/ai/models — list available AI models ────────────────────
    svr.Get("/api/v1/ai/models",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                json out;
                if (!gAdminConfig.ollama.enabled) {
                    out["ok"]         = false;
                    out["error"]      = "AI is disabled in configuration";
                    out["ai_enabled"] = false;
                    res.set_content(out.dump(2), "application/json");
                    return;
                }
                if (!g_ollama) {
                    out["ok"]    = false;
                    out["error"] = "Ollama client not initialized";
                    res.set_content(out.dump(2), "application/json");
                    return;
                }
                if (!g_ollama->is_available()) {
                    out["ok"]           = false;
                    out["error"]        = "Ollama not reachable at " + g_ollama->endpoint();
                    out["ai_available"] = false;
                    res.set_content(out.dump(2), "application/json");
                    return;
                }

                auto models_resp = g_ollama->list_models();
                out["ok"] = true;
                if (models_resp.contains("models"))
                    out["models"] = models_resp["models"];
                else
                    out["models"] = json::array();
                res.set_content(out.dump(2), "application/json");
            });
        });

    // ── POST /api/v1/ai/test — test AI connectivity ─────────────────────────
    svr.Post("/api/v1/ai/test",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                json out;
                out["ok"] = true;

                if (!gAdminConfig.ollama.enabled) {
                    out["ok"]         = false;
                    out["error"]      = "AI is disabled in configuration";
                    out["ai_enabled"] = false;
                    res.set_content(out.dump(2), "application/json");
                    return;
                }
                if (!g_ollama) {
                    out["ok"]         = false;
                    out["error"]      = "Ollama client not initialized";
                    out["ai_enabled"] = true;
                    res.set_content(out.dump(2), "application/json");
                    return;
                }

                auto t0 = std::chrono::steady_clock::now();
                bool reachable = g_ollama->is_available();
                auto t1 = std::chrono::steady_clock::now();
                int latency_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

                out["reachable"]   = reachable;
                out["latency_ms"]  = latency_ms;
                out["endpoint"]    = g_ollama->endpoint();

                if (reachable) {
                    auto models_resp = g_ollama->list_models();
                    if (models_resp.contains("models"))
                        out["models"] = models_resp["models"];
                    else
                        out["models"] = json::array();

                    json body;
                    try { body = json::parse(req.body); } catch (...) { body = json::object(); }
                    bool run_test = body.value("run_test", false);
                    if (run_test) {
                        auto t2 = std::chrono::steady_clock::now();
                        json test_result = g_ollama->generate("Say 'hello' in one word.");
                        auto t3 = std::chrono::steady_clock::now();
                        int test_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();
                        if (test_result.contains("error"))
                            out["test_error"] = test_result["error"];
                        else
                            out["test_response"] = test_result.value("response", "");
                        out["test_latency_ms"] = test_ms;
                    }
                } else {
                    out["error"] = "Ollama not reachable at " + g_ollama->endpoint();
                }
                res.set_content(out.dump(2), "application/json");
            });
        });

    // ── POST /api/v1/ai/chat — chat with AI ─────────────────────────────────
    svr.Post("/api/v1/ai/chat",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                if (!gAdminConfig.ollama.enabled || !g_ollama || !g_ollama->is_available()) {
                    json _e; _e["error"] = !gAdminConfig.ollama.enabled
                        ? "AI is disabled in configuration"
                        : (!g_ollama ? "Ollama client not initialized" : "Ollama not reachable");
                    _e["available"] = false;
                    res.set_content(_e.dump(), "application/json");
                    return;
                }

                json body;
                try { body = json::parse(req.body); } catch (...) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                    return;
                }

                json messages = body.value("messages", json::array());
                std::string model = body.value("model", "");
                if (messages.empty()) {
                    res.status = 400;
                    res.set_content(R"({"error":"messages array required"})", "application/json");
                    return;
                }

                auto t0 = std::chrono::steady_clock::now();
                json result = g_ollama->chat(messages, model);
                auto t1 = std::chrono::steady_clock::now();
                int latency_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

                json out;
                if (result.contains("error")) {
                    out["ok"]    = false;
                    out["error"] = result["error"];
                } else {
                    out["ok"]         = true;
                    out["response"]   = result;
                    out["model"]      = model.empty() ? g_ollama->model() : model;
                    out["latency_ms"] = latency_ms;
                }
                res.set_content(out.dump(2), "application/json");
            });
        });

    // ── POST /api/v1/ai/suggest-chain — AI suggests effects chain ───────────
    svr.Post("/api/v1/ai/suggest-chain",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                if (!gAdminConfig.ollama.enabled || !g_ollama || !g_ollama->is_available()) {
                    json _e; _e["error"] = "AI not available"; _e["available"] = false;
                    res.set_content(_e.dump(), "application/json");
                    return;
                }

                json body;
                try { body = json::parse(req.body); } catch (...) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                    return;
                }

                bool apply = body.value("apply", false);
                json prompt_body = body;
                prompt_body.erase("apply");
                std::string user_prompt = "Audio profile:\n" + prompt_body.dump(2);
                json messages = json::array();
                messages.push_back({{"role", "system"}, {"content", mc1vt::ai_prompts::CHAIN_SUGGEST_SYSTEM}});
                messages.push_back({{"role", "user"}, {"content", user_prompt}});

                auto t0 = std::chrono::steady_clock::now();
                json result = g_ollama->chat(messages);
                auto t1 = std::chrono::steady_clock::now();
                int latency_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

                json out;
                if (result.contains("error")) {
                    out["ok"]    = false;
                    out["error"] = result["error"];
                    res.set_content(out.dump(2), "application/json");
                    return;
                }

                std::string ai_text = result.contains("message")
                    ? result["message"].value("content", "") : "";
                out["ok"]         = true;
                out["response"]   = ai_text;
                out["model"]      = g_ollama->model();
                out["latency_ms"] = latency_ms;
                out["applied"]    = false;

                json chain_json;
                bool chain_parsed = false;
                try {
                    chain_json = json::parse(ai_text);
                    chain_parsed = true;
                } catch (...) {
                    auto j_start = ai_text.find('{');
                    auto j_end   = ai_text.rfind('}');
                    if (j_start != std::string::npos && j_end != std::string::npos && j_end > j_start) {
                        try {
                            chain_json = json::parse(ai_text.substr(j_start, j_end - j_start + 1));
                            chain_parsed = true;
                        } catch (...) {}
                    }
                }

                if (chain_parsed && chain_json.contains("chain")) {
                    out["suggested_chain"] = chain_json;
                    out["rationale"]       = chain_json.value("rationale", "");
                }

#ifndef MC1_HTTP_TEST_BUILD
                if (apply && chain_parsed && chain_json.contains("chain") && g_pipeline) {
                    auto& rack = g_pipeline->global_effects_rack();
                    rack.from_json({{"bypass", false}, {"units", json::array()}});
                    auto& chain_arr = chain_json["chain"];
                    int applied_count = 0;
                    for (auto& unit_j : chain_arr) {
                        std::string type = unit_j.value("type", "");
                        auto unit = mc1dsp::EffectsRack::create_unit(type);
                        if (unit) {
                            if (unit_j.contains("params"))
                                unit->set_params(unit_j["params"]);
                            unit->set_enabled(true);
                            rack.add_unit(std::move(unit));
                            applied_count++;
                        }
                    }
                    out["applied"]       = true;
                    out["applied_count"] = applied_count;
                    MC1_INFO("AI suggest-chain: applied " + std::to_string(applied_count) + " units to global rack");
                }
#endif

                res.set_content(out.dump(2), "application/json");
            });
        });

    // ── POST /api/v1/ai/natural-command — NLP command execution ─────────────
    svr.Post("/api/v1/ai/natural-command",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                if (!gAdminConfig.ollama.enabled || !g_ollama || !g_ollama->is_available()) {
                    json _e; _e["error"] = "AI not available"; _e["available"] = false;
                    res.set_content(_e.dump(), "application/json");
                    return;
                }

                json body;
                try { body = json::parse(req.body); } catch (...) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                    return;
                }

                std::string command = body.value("command", "");
                if (command.empty()) {
                    res.status = 400;
                    res.set_content(R"({"error":"command field required"})", "application/json");
                    return;
                }

                json messages = json::array();
                messages.push_back({{"role", "system"}, {"content", mc1vt::ai_prompts::NLP_COMMAND_SYSTEM}});
                messages.push_back({{"role", "user"}, {"content", command}});

                auto t0 = std::chrono::steady_clock::now();
                json result = g_ollama->chat(messages);
                auto t1 = std::chrono::steady_clock::now();
                int latency_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

                json out;
                if (result.contains("error")) {
                    out["ok"]    = false;
                    out["error"] = result["error"];
                    res.set_content(out.dump(2), "application/json");
                    return;
                }

                std::string ai_text = result.contains("message")
                    ? result["message"].value("content", "") : "";
                out["ok"]         = true;
                out["raw"]        = ai_text;
                out["model"]      = g_ollama->model();
                out["latency_ms"] = latency_ms;

                json parsed_action;
                bool parsed = false;
                try {
                    parsed_action = json::parse(ai_text);
                    parsed = true;
                } catch (...) {
                    auto j_start = ai_text.find('{');
                    auto j_end   = ai_text.rfind('}');
                    if (j_start != std::string::npos && j_end != std::string::npos && j_end > j_start) {
                        try {
                            parsed_action = json::parse(ai_text.substr(j_start, j_end - j_start + 1));
                            parsed = true;
                        } catch (...) {}
                    }
                }

                if (!parsed) {
                    out["parsed_action"] = nullptr;
                    out["executed"]      = false;
                    out["result"]        = "Could not parse AI response as action JSON";
                    res.set_content(out.dump(2), "application/json");
                    return;
                }

                out["parsed_action"] = parsed_action;
                std::string action = parsed_action.value("action", "");

#ifndef MC1_HTTP_TEST_BUILD
                bool executed = false;
                std::string exec_result;

                if (action == "eq_preset" || action == "set_eq_preset") {
                    int slot = parsed_action.value("slot", 1);
                    std::string preset = parsed_action.value("preset", "flat");
                    EncoderConfig cfg;
                    if (g_pipeline && g_pipeline->get_slot_config(slot, cfg)) {
                        mc1dsp::DspChainConfig dsp_cfg;
                        dsp_cfg.sample_rate        = cfg.sample_rate;
                        dsp_cfg.channels           = cfg.channels;
                        dsp_cfg.eq_enabled         = true;
                        dsp_cfg.agc_enabled        = cfg.dsp_agc_enabled;
                        dsp_cfg.crossfader_enabled = cfg.dsp_crossfade_enabled;
                        dsp_cfg.crossfade_duration = cfg.dsp_crossfade_duration;
                        dsp_cfg.eq_preset          = preset;
                        g_pipeline->reconfigure_dsp(slot, dsp_cfg);
                        executed = true;
                        exec_result = "EQ preset '" + preset + "' applied to slot " + std::to_string(slot);
                    } else {
                        exec_result = "Slot " + std::to_string(slot) + " not found";
                    }
                }
                else if (action == "volume" || action == "set_volume") {
                    int slot  = parsed_action.value("slot", -1);
                    float vol = parsed_action.value("level", 1.0f);
                    if (vol < 0.0f) vol = 0.0f;
                    if (vol > 2.0f) vol = 2.0f;
                    if (g_pipeline) {
                        if (slot < 0)
                            g_pipeline->set_master_volume(vol);
                        else
                            g_pipeline->set_volume(slot, vol);
                        executed = true;
                        exec_result = "Volume set to " + std::to_string(vol);
                    } else {
                        exec_result = "No pipeline available";
                    }
                }
                else if (action == "start" || action == "start_encoder") {
                    int slot = parsed_action.value("slot", 1);
                    if (g_pipeline && g_pipeline->start_slot(slot)) {
                        mc1log.encoder(slot, "START", "AI natural-command");
                        executed = true;
                        exec_result = "Encoder slot " + std::to_string(slot) + " started";
                    } else {
                        exec_result = "Failed to start slot " + std::to_string(slot);
                    }
                }
                else if (action == "stop" || action == "stop_encoder") {
                    int slot = parsed_action.value("slot", 1);
                    if (g_pipeline) {
                        g_pipeline->stop_slot(slot);
                        mc1log.encoder(slot, "STOP", "AI natural-command");
                        executed = true;
                        exec_result = "Encoder slot " + std::to_string(slot) + " stopped";
                    } else {
                        exec_result = "No pipeline available";
                    }
                }
                else if (action == "skip" || action == "skip_track") {
                    int slot = parsed_action.value("slot", 1);
                    if (g_pipeline) {
                        g_pipeline->skip_track(slot);
                        executed = true;
                        exec_result = "Skipped to next track on slot " + std::to_string(slot);
                    } else {
                        exec_result = "No pipeline available";
                    }
                }
                else if (action == "crossfade" || action == "set_crossfade") {
                    int slot = parsed_action.value("slot", 1);
                    EncoderConfig cfg;
                    if (g_pipeline && g_pipeline->get_slot_config(slot, cfg)) {
                        if (parsed_action.contains("duration_ms"))
                            cfg.dsp_crossfade_duration = parsed_action["duration_ms"].get<float>();
                        if (parsed_action.contains("curve"))
                            cfg.dsp_crossfade_curve = std::clamp(parsed_action["curve"].get<int>(), 0, 8);
                        cfg.dsp_crossfade_enabled = true;
                        mc1dsp::DspChainConfig dsp_cfg;
                        dsp_cfg.sample_rate        = cfg.sample_rate;
                        dsp_cfg.channels           = cfg.channels;
                        dsp_cfg.eq_enabled         = cfg.dsp_eq_enabled;
                        dsp_cfg.agc_enabled        = cfg.dsp_agc_enabled;
                        dsp_cfg.crossfader_enabled = cfg.dsp_crossfade_enabled;
                        dsp_cfg.crossfade_duration = cfg.dsp_crossfade_duration;
                        dsp_cfg.crossfade_curve    = cfg.dsp_crossfade_curve;
                        dsp_cfg.eq_preset          = cfg.dsp_eq_preset;
                        g_pipeline->reconfigure_dsp(slot, dsp_cfg);
                        executed = true;
                        exec_result = "Crossfade settings updated on slot " + std::to_string(slot);
                    } else {
                        exec_result = "Slot " + std::to_string(slot) + " not found";
                    }
                }
                else if (action == "load_playlist") {
                    int slot = parsed_action.value("slot", 1);
                    std::string path = parsed_action.value("path", "");
                    if (path.empty()) {
                        exec_result = "No playlist path specified";
                    } else if (g_pipeline && g_pipeline->load_playlist(slot, path)) {
                        executed = true;
                        exec_result = "Playlist loaded on slot " + std::to_string(slot);
                    } else {
                        exec_result = "Failed to load playlist";
                    }
                }
                else if (action == "clarify") {
                    executed = false;
                    exec_result = parsed_action.value("message", "Please clarify your command.");
                }
                else {
                    exec_result = "Unknown action: " + action;
                }

                out["executed"] = executed;
                out["result"]   = exec_result;
#else
                out["executed"] = false;
                out["result"]   = "Test build — no pipeline";
#endif
                res.set_content(out.dump(2), "application/json");
            });
        });

    // ── POST /api/v1/ai/troubleshoot — AI troubleshooting ───────────────────
    svr.Post("/api/v1/ai/troubleshoot",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                if (!gAdminConfig.ollama.enabled || !g_ollama || !g_ollama->is_available()) {
                    json _e; _e["error"] = "AI not available"; _e["available"] = false;
                    res.set_content(_e.dump(), "application/json");
                    return;
                }

                json body;
                try { body = json::parse(req.body); } catch (...) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                    return;
                }

                std::string symptoms = body.value("symptoms", "");
                if (symptoms.empty()) {
                    res.status = 400;
                    res.set_content(R"({"error":"symptoms field required"})", "application/json");
                    return;
                }

                std::string user_prompt = symptoms;
                if (body.contains("system_state") && body["system_state"].is_object()) {
                    user_prompt += "\n\nSystem state:\n" + body["system_state"].dump(2);
                }

                json messages = json::array();
                messages.push_back({{"role", "system"}, {"content", mc1vt::ai_prompts::TROUBLESHOOT_SYSTEM}});
                messages.push_back({{"role", "user"}, {"content", user_prompt}});

                auto t0 = std::chrono::steady_clock::now();
                json result = g_ollama->chat(messages);
                auto t1 = std::chrono::steady_clock::now();
                int latency_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

                json out;
                if (result.contains("error")) {
                    out["ok"]    = false;
                    out["error"] = result["error"];
                } else {
                    out["ok"]         = true;
                    out["response"]   = result.contains("message")
                        ? result["message"].value("content", "") : "";
                    out["model"]      = g_ollama->model();
                    out["latency_ms"] = latency_ms;
                }
                res.set_content(out.dump(2), "application/json");
            });
        });

    // ── POST /api/v1/ai/playlist/enhance — AI playlist reordering ───────────
    svr.Post("/api/v1/ai/playlist/enhance",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                if (!gAdminConfig.ollama.enabled || !g_ollama || !g_ollama->is_available()) {
                    json _e; _e["error"] = "AI not available"; _e["available"] = false;
                    res.set_content(_e.dump(), "application/json");
                    return;
                }

                json body;
                try { body = json::parse(req.body); } catch (...) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                    return;
                }

                auto tracks = body.value("playlist_tracks", json::array());
                std::string goal = body.value("goal", "energy_flow");
                if (tracks.empty()) {
                    res.status = 400;
                    res.set_content(R"({"error":"playlist_tracks array required"})", "application/json");
                    return;
                }

                std::string user_prompt = "Goal: " + goal + "\n\nTrack list (" +
                    std::to_string(tracks.size()) + " tracks):\n";
                for (size_t i = 0; i < tracks.size(); ++i) {
                    auto& t = tracks[i];
                    user_prompt += "[" + std::to_string(i) + "] ";
                    user_prompt += t.value("title", "Unknown") + " — " + t.value("artist", "Unknown");
                    if (t.contains("genre") && !t["genre"].get<std::string>().empty())
                        user_prompt += " | genre: " + t["genre"].get<std::string>();
                    if (t.contains("bpm") && t["bpm"].is_number() && t["bpm"].get<double>() > 0)
                        user_prompt += " | bpm: " + std::to_string((int)t["bpm"].get<double>());
                    if (t.contains("energy") && t["energy"].is_number())
                        user_prompt += " | energy: " + std::to_string(t["energy"].get<double>());
                    if (t.contains("duration_ms") && t["duration_ms"].is_number())
                        user_prompt += " | dur: " + std::to_string(t["duration_ms"].get<int>() / 1000) + "s";
                    user_prompt += "\n";
                }

                json messages = json::array();
                messages.push_back({{"role", "system"}, {"content", mc1vt::ai_prompts::PLAYLIST_ENHANCE_SYSTEM}});
                messages.push_back({{"role", "user"}, {"content", user_prompt}});

                auto t0 = std::chrono::steady_clock::now();
                json result = g_ollama->chat(messages);
                auto t1 = std::chrono::steady_clock::now();
                int latency_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

                json out;
                if (result.contains("error")) {
                    out["ok"]    = false;
                    out["error"] = result["error"];
                } else {
                    std::string ai_text = result.contains("message")
                        ? result["message"].value("content", "") : "";
                    out["ok"]         = true;
                    out["model"]      = g_ollama->model();
                    out["latency_ms"] = latency_ms;

                    json parsed;
                    bool parsed_ok = false;
                    try {
                        parsed = json::parse(ai_text);
                        parsed_ok = true;
                    } catch (...) {
                        auto j_start = ai_text.find('{');
                        auto j_end   = ai_text.rfind('}');
                        if (j_start != std::string::npos && j_end != std::string::npos && j_end > j_start) {
                            try {
                                parsed = json::parse(ai_text.substr(j_start, j_end - j_start + 1));
                                parsed_ok = true;
                            } catch (...) {}
                        }
                    }

                    if (parsed_ok && parsed.contains("reordered_indices")) {
                        out["reordered_indices"] = parsed["reordered_indices"];
                        out["rationale"]         = parsed.value("rationale", "");
                    } else {
                        out["reordered_indices"] = json::array();
                        out["rationale"]         = ai_text;
                    }
                }
                res.set_content(out.dump(2), "application/json");
            });
        });

    // ── POST /api/v1/ai/predict-deadair — AI dead air prediction ────────────
    svr.Post("/api/v1/ai/predict-deadair",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                if (!gAdminConfig.ollama.enabled || !g_ollama || !g_ollama->is_available()) {
                    json _e; _e["error"] = "AI not available"; _e["available"] = false;
                    res.set_content(_e.dump(), "application/json");
                    return;
                }

                json body;
                try { body = json::parse(req.body); } catch (...) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                    return;
                }

                int slot_id = body.value("slot_id", 0);
                auto events = body.value("recent_events", json::array());
                if (events.empty()) {
                    res.status = 400;
                    res.set_content(R"({"error":"recent_events array required"})", "application/json");
                    return;
                }

                std::string user_prompt = "Encoder slot: " + std::to_string(slot_id) +
                    "\n\nRecent events (" + std::to_string(events.size()) + "):\n" + events.dump(2);

                json messages = json::array();
                messages.push_back({{"role", "system"}, {"content", mc1vt::ai_prompts::DEADAIR_PREDICT_SYSTEM}});
                messages.push_back({{"role", "user"}, {"content", user_prompt}});

                auto t0 = std::chrono::steady_clock::now();
                json result = g_ollama->chat(messages);
                auto t1 = std::chrono::steady_clock::now();
                int latency_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

                json out;
                if (result.contains("error")) {
                    out["ok"]    = false;
                    out["error"] = result["error"];
                } else {
                    std::string ai_text = result.contains("message")
                        ? result["message"].value("content", "") : "";
                    out["ok"]         = true;
                    out["model"]      = g_ollama->model();
                    out["latency_ms"] = latency_ms;

                    json parsed;
                    bool parsed_ok = false;
                    try {
                        parsed = json::parse(ai_text);
                        parsed_ok = true;
                    } catch (...) {
                        auto j_start = ai_text.find('{');
                        auto j_end   = ai_text.rfind('}');
                        if (j_start != std::string::npos && j_end != std::string::npos && j_end > j_start) {
                            try {
                                parsed = json::parse(ai_text.substr(j_start, j_end - j_start + 1));
                                parsed_ok = true;
                            } catch (...) {}
                        }
                    }

                    if (parsed_ok) {
                        out["risk_level"]       = parsed.value("risk_level", "unknown");
                        out["prediction"]       = parsed.value("prediction", "");
                        out["suggested_actions"] = parsed.value("suggested_actions", json::array());
                    } else {
                        out["risk_level"]       = "unknown";
                        out["prediction"]       = ai_text;
                        out["suggested_actions"] = json::array();
                    }
                }
                res.set_content(out.dump(2), "application/json");
            });
        });

    // ── PC-1: Podcast Recording Studio API ─────────────────────────────────

    // POST /api/v1/recording/start — start recording on a slot
    svr.Post("/api/v1/recording/start",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                json body;
                try { body = json::parse(req.body); } catch (...) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                    return;
                }

                int slot_id = body.value("slot_id", 0);
                int show_id = body.value("show_id", 0);
                std::string episode_title = body.value("episode_title", "");
                std::string format        = body.value("format", "mp3");
                int auto_split_min        = body.value("auto_split_minutes", 0);
                std::string pre_roll      = body.value("pre_roll", "");
                std::string post_roll     = body.value("post_roll", "");

                if (slot_id < 1) {
                    res.status = 400;
                    res.set_content(R"({"error":"slot_id is required"})", "application/json");
                    return;
                }
                if (show_id < 1) {
                    res.status = 400;
                    res.set_content(R"({"error":"show_id is required"})", "application/json");
                    return;
                }
                if (episode_title.empty()) {
                    episode_title = "Recording " + std::to_string(slot_id) + " - " +
                        []() { char buf[32]; time_t t = time(nullptr); struct tm tm; localtime_r(&t, &tm);
                               strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm); return std::string(buf); }();
                }

                // Check if already recording on this slot
                {
                    std::lock_guard<std::mutex> lk(g_rec_mtx);
                    auto it = g_recordings.find(slot_id);
                    if (it != g_recordings.end() && it->second.active) {
                        res.status = 409;
                        json e; e["error"] = "Slot " + std::to_string(slot_id) + " is already recording";
                        res.set_content(e.dump(), "application/json");
                        return;
                    }
                }

#ifndef MC1_HTTP_TEST_BUILD
                // Verify slot exists
                if (!g_pipeline) {
                    res.status = 503;
                    res.set_content(R"({"error":"Pipeline not available"})", "application/json");
                    return;
                }
                auto stats = g_pipeline->slot_stats(slot_id);
                if (stats.slot_id == 0) {
                    res.status = 404;
                    json e; e["error"] = "Slot " + std::to_string(slot_id) + " not found";
                    res.set_content(e.dump(), "application/json");
                    return;
                }
#endif

                // Create podcast_episode record in DB
                auto& db = Mc1Db::instance();
                std::string esc_title = db.escape(episode_title);
                std::string esc_format = db.escape(format);

                // Build tags JSON with pre/post roll paths
                json tags_json;
                if (!pre_roll.empty())  tags_json["pre_roll"]  = pre_roll;
                if (!post_roll.empty()) tags_json["post_roll"] = post_roll;
                std::string tags_str = tags_json.empty() ? "" : tags_json.dump();
                std::string esc_tags = db.escape(tags_str);

                // Generate recording file path
                char ts_buf[64];
                time_t now = time(nullptr);
                struct tm tm_now;
                localtime_r(&now, &tm_now);
                strftime(ts_buf, sizeof(ts_buf), "%Y%m%d_%H%M%S", &tm_now);
                std::string safe_title;
                for (char c : episode_title) {
                    if (std::isalnum(c) || c == '-' || c == '_') safe_title += c;
                    else if (c == ' ') safe_title += '_';
                }
                if (safe_title.size() > 60) safe_title.resize(60);
                std::string archive_dir = "/var/www/mcaster1.com/Mcaster1DSPEncoder/archives";
                std::string file_name = "rec_slot" + std::to_string(slot_id) + "_" + ts_buf + "_" + safe_title + "." + format;
                std::string file_path = archive_dir + "/" + file_name;

                // Ensure archive directory exists
                mkdir(archive_dir.c_str(), 0755);

                // Insert episode record
                char sql[2048];
                snprintf(sql, sizeof(sql),
                    "INSERT INTO mcaster1_media.podcast_episodes "
                    "(show_id, title, description, file_path, format, bitrate_kbps, "
                    " slot_id, tags, is_published, recording_started_at) "
                    "VALUES (%d, '%s', '', '%s', '%s', 128, %d, '%s', 0, NOW())",
                    show_id, esc_title.c_str(), db.escape(file_path).c_str(),
                    esc_format.c_str(), slot_id, esc_tags.c_str());

                if (!db.exec(sql)) {
                    res.status = 500;
                    res.set_content(R"({"error":"Failed to create episode record"})", "application/json");
                    return;
                }

                // Get the episode id
                auto rows = db.query("SELECT LAST_INSERT_ID() AS id");
                int episode_id = 0;
                if (!rows.empty()) episode_id = std::stoi(rows[0]["id"]);

                // Store recording state
                {
                    std::lock_guard<std::mutex> lk(g_rec_mtx);
                    RecordingState& rs = g_recordings[slot_id];
                    rs.active             = true;
                    rs.slot_id            = slot_id;
                    rs.episode_id         = episode_id;
                    rs.show_id            = show_id;
                    rs.file_path          = file_path;
                    rs.started_at         = now;
                    rs.auto_split_minutes = auto_split_min;
                    rs.format             = format;
                    rs.episode_title      = episode_title;
                    rs.pre_roll           = pre_roll;
                    rs.post_roll          = post_roll;
                    rs.markers.clear();
                }

                MC1_INFO("Recording started: slot=" + std::to_string(slot_id) +
                         " episode_id=" + std::to_string(episode_id) +
                         " file=" + file_path);

                json out;
                out["ok"]           = true;
                out["recording_id"] = slot_id;
                out["episode_id"]   = episode_id;
                out["file_path"]    = file_path;
                res.set_content(out.dump(2), "application/json");
            });
        });

    // POST /api/v1/recording/stop — stop recording on a slot
    svr.Post("/api/v1/recording/stop",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                json body;
                try { body = json::parse(req.body); } catch (...) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                    return;
                }

                int slot_id = body.value("slot_id", 0);
                if (slot_id < 1) {
                    res.status = 400;
                    res.set_content(R"({"error":"slot_id is required"})", "application/json");
                    return;
                }

                int episode_id = 0;
                time_t started_at = 0;
                std::string file_path;
                {
                    std::lock_guard<std::mutex> lk(g_rec_mtx);
                    auto it = g_recordings.find(slot_id);
                    if (it == g_recordings.end() || !it->second.active) {
                        res.status = 404;
                        json e; e["error"] = "No active recording on slot " + std::to_string(slot_id);
                        res.set_content(e.dump(), "application/json");
                        return;
                    }
                    episode_id = it->second.episode_id;
                    started_at = it->second.started_at;
                    file_path  = it->second.file_path;
                    it->second.active = false;
                }

                time_t now = time(nullptr);
                int duration_sec = (int)(now - started_at);

                // Get file size if file exists
                int64_t file_size = 0;
                struct stat st;
                if (stat(file_path.c_str(), &st) == 0) {
                    file_size = st.st_size;
                }

                // Update episode record in DB
                auto& db = Mc1Db::instance();
                char sql[1024];
                snprintf(sql, sizeof(sql),
                    "UPDATE mcaster1_media.podcast_episodes "
                    "SET duration_sec = %d, file_size_bytes = %lld, recording_ended_at = NOW() "
                    "WHERE id = %d",
                    duration_sec, (long long)file_size, episode_id);
                db.exec(sql);

                MC1_INFO("Recording stopped: slot=" + std::to_string(slot_id) +
                         " episode_id=" + std::to_string(episode_id) +
                         " duration=" + std::to_string(duration_sec) + "s");

                json out;
                out["ok"]              = true;
                out["episode_id"]      = episode_id;
                out["duration_sec"]    = duration_sec;
                out["file_size_bytes"] = file_size;
                res.set_content(out.dump(2), "application/json");
            });
        });

    // POST /api/v1/recording/marker — add chapter marker to recording
    svr.Post("/api/v1/recording/marker",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                json body;
                try { body = json::parse(req.body); } catch (...) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                    return;
                }

                int slot_id = body.value("slot_id", 0);
                std::string marker_type = body.value("marker_type", "chapter");
                std::string title       = body.value("title", "");
                std::string url         = body.value("url", "");
                std::string image_url   = body.value("image_url", "");

                if (slot_id < 1) {
                    res.status = 400;
                    res.set_content(R"({"error":"slot_id is required"})", "application/json");
                    return;
                }

                int episode_id = 0;
                int64_t timestamp_ms = 0;
                {
                    std::lock_guard<std::mutex> lk(g_rec_mtx);
                    auto it = g_recordings.find(slot_id);
                    if (it == g_recordings.end() || !it->second.active) {
                        res.status = 404;
                        json e; e["error"] = "No active recording on slot " + std::to_string(slot_id);
                        res.set_content(e.dump(), "application/json");
                        return;
                    }
                    episode_id   = it->second.episode_id;
                    timestamp_ms = (int64_t)(time(nullptr) - it->second.started_at) * 1000LL;

                    // If title is empty, generate a default
                    if (title.empty()) {
                        title = "Marker " + std::to_string(it->second.markers.size() + 1);
                    }
                }

                // Insert into DB
                auto& db = Mc1Db::instance();
                char sql[2048];
                snprintf(sql, sizeof(sql),
                    "INSERT INTO mcaster1_media.episode_markers "
                    "(episode_id, marker_type, timestamp_ms, title, url, image_url) "
                    "VALUES (%d, '%s', %lld, '%s', '%s', '%s')",
                    episode_id, db.escape(marker_type).c_str(),
                    (long long)timestamp_ms, db.escape(title).c_str(),
                    db.escape(url).c_str(), db.escape(image_url).c_str());
                db.exec(sql);

                // Get marker id
                auto rows = db.query("SELECT LAST_INSERT_ID() AS id");
                int marker_id = 0;
                if (!rows.empty()) marker_id = std::stoi(rows[0]["id"]);

                // Store in memory
                {
                    std::lock_guard<std::mutex> lk(g_rec_mtx);
                    auto it = g_recordings.find(slot_id);
                    if (it != g_recordings.end()) {
                        RecordingState::Marker m;
                        m.timestamp_ms = timestamp_ms;
                        m.title        = title;
                        m.marker_type  = marker_type;
                        m.url          = url;
                        m.image_url    = image_url;
                        m.db_id        = marker_id;
                        it->second.markers.push_back(m);
                    }
                }

                MC1_INFO("Marker added: slot=" + std::to_string(slot_id) +
                         " ts=" + std::to_string(timestamp_ms) + "ms title=" + title);

                json out;
                out["ok"]           = true;
                out["marker_id"]    = marker_id;
                out["timestamp_ms"] = timestamp_ms;
                res.set_content(out.dump(2), "application/json");
            });
        });

    // GET /api/v1/recording/status — recording state for all slots
    svr.Get("/api/v1/recording/status",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                json slots_arr = json::array();
                {
                    std::lock_guard<std::mutex> lk(g_rec_mtx);
                    for (auto& [sid, rs] : g_recordings) {
                        json s;
                        s["slot_id"]    = sid;
                        s["recording"]  = rs.active;
                        s["episode_id"] = rs.episode_id;
                        s["show_id"]    = rs.show_id;
                        s["file_path"]  = rs.file_path;
                        s["format"]     = rs.format;
                        s["episode_title"] = rs.episode_title;
                        s["auto_split_minutes"] = rs.auto_split_minutes;

                        if (rs.active && rs.started_at > 0) {
                            s["duration_sec"] = (int)(time(nullptr) - rs.started_at);
                        } else {
                            s["duration_sec"] = 0;
                        }

                        json markers = json::array();
                        for (auto& m : rs.markers) {
                            json mj;
                            mj["id"]           = m.db_id;
                            mj["timestamp_ms"] = m.timestamp_ms;
                            mj["title"]        = m.title;
                            mj["marker_type"]  = m.marker_type;
                            markers.push_back(mj);
                        }
                        s["markers"] = markers;
                        slots_arr.push_back(s);
                    }
                }

                // Also include slots that have no recording state yet
#ifndef MC1_HTTP_TEST_BUILD
                if (g_pipeline) {
                    auto all = g_pipeline->all_stats();
                    for (auto& st : all) {
                        bool found = false;
                        for (auto& s : slots_arr) {
                            if (s["slot_id"] == st.slot_id) { found = true; break; }
                        }
                        if (!found) {
                            json s;
                            s["slot_id"]      = st.slot_id;
                            s["recording"]    = false;
                            s["episode_id"]   = 0;
                            s["show_id"]      = 0;
                            s["file_path"]    = "";
                            s["format"]       = "";
                            s["episode_title"] = "";
                            s["duration_sec"] = 0;
                            s["auto_split_minutes"] = 0;
                            s["markers"]      = json::array();
                            slots_arr.push_back(s);
                        }
                    }
                }
#endif

                json out;
                out["ok"]    = true;
                out["slots"] = slots_arr;
                res.set_content(out.dump(2), "application/json");
            });
        });

    // POST /api/v1/recording/split — split recording into new file/episode
    svr.Post("/api/v1/recording/split",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                json body;
                try { body = json::parse(req.body); } catch (...) {
                    res.status = 400;
                    res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                    return;
                }

                int slot_id = body.value("slot_id", 0);
                if (slot_id < 1) {
                    res.status = 400;
                    res.set_content(R"({"error":"slot_id is required"})", "application/json");
                    return;
                }

                int old_episode_id = 0;
                int show_id        = 0;
                time_t old_start   = 0;
                std::string old_file;
                std::string format;
                std::string ep_title;
                int auto_split_min = 0;
                std::string pre_roll;
                std::string post_roll;
                {
                    std::lock_guard<std::mutex> lk(g_rec_mtx);
                    auto it = g_recordings.find(slot_id);
                    if (it == g_recordings.end() || !it->second.active) {
                        res.status = 404;
                        json e; e["error"] = "No active recording on slot " + std::to_string(slot_id);
                        res.set_content(e.dump(), "application/json");
                        return;
                    }
                    old_episode_id = it->second.episode_id;
                    show_id        = it->second.show_id;
                    old_start      = it->second.started_at;
                    old_file       = it->second.file_path;
                    format         = it->second.format;
                    ep_title       = it->second.episode_title;
                    auto_split_min = it->second.auto_split_minutes;
                    pre_roll       = it->second.pre_roll;
                    post_roll      = it->second.post_roll;
                }

                auto& db = Mc1Db::instance();
                time_t now = time(nullptr);

                // Finalize old episode
                int old_duration = (int)(now - old_start);
                int64_t old_file_size = 0;
                struct stat st;
                if (stat(old_file.c_str(), &st) == 0) old_file_size = st.st_size;

                char sql[1024];
                snprintf(sql, sizeof(sql),
                    "UPDATE mcaster1_media.podcast_episodes "
                    "SET duration_sec = %d, file_size_bytes = %lld, recording_ended_at = NOW() "
                    "WHERE id = %d",
                    old_duration, (long long)old_file_size, old_episode_id);
                db.exec(sql);

                // Create new episode record
                char ts_buf[64];
                struct tm tm_now;
                localtime_r(&now, &tm_now);
                strftime(ts_buf, sizeof(ts_buf), "%Y%m%d_%H%M%S", &tm_now);

                std::string new_title = ep_title + " (cont.)";
                std::string safe_title;
                for (char c : new_title) {
                    if (std::isalnum(c) || c == '-' || c == '_') safe_title += c;
                    else if (c == ' ') safe_title += '_';
                }
                if (safe_title.size() > 60) safe_title.resize(60);
                std::string archive_dir = "/var/www/mcaster1.com/Mcaster1DSPEncoder/archives";
                std::string new_file = archive_dir + "/rec_slot" + std::to_string(slot_id) + "_" + ts_buf + "_" + safe_title + "." + format;

                json tags_json;
                if (!pre_roll.empty())  tags_json["pre_roll"]  = pre_roll;
                if (!post_roll.empty()) tags_json["post_roll"] = post_roll;
                tags_json["split_from_episode"] = old_episode_id;
                std::string esc_tags = db.escape(tags_json.dump());

                char sql2[2048];
                snprintf(sql2, sizeof(sql2),
                    "INSERT INTO mcaster1_media.podcast_episodes "
                    "(show_id, title, description, file_path, format, bitrate_kbps, "
                    " slot_id, tags, is_published, recording_started_at) "
                    "VALUES (%d, '%s', '', '%s', '%s', 128, %d, '%s', 0, NOW())",
                    show_id, db.escape(new_title).c_str(), db.escape(new_file).c_str(),
                    db.escape(format).c_str(), slot_id, esc_tags.c_str());
                db.exec(sql2);

                auto rows = db.query("SELECT LAST_INSERT_ID() AS id");
                int new_episode_id = 0;
                if (!rows.empty()) new_episode_id = std::stoi(rows[0]["id"]);

                // Update recording state
                {
                    std::lock_guard<std::mutex> lk(g_rec_mtx);
                    auto it = g_recordings.find(slot_id);
                    if (it != g_recordings.end()) {
                        it->second.episode_id    = new_episode_id;
                        it->second.file_path     = new_file;
                        it->second.started_at    = now;
                        it->second.episode_title = new_title;
                        it->second.markers.clear();
                    }
                }

                MC1_INFO("Recording split: slot=" + std::to_string(slot_id) +
                         " old_ep=" + std::to_string(old_episode_id) +
                         " new_ep=" + std::to_string(new_episode_id));

                json out;
                out["ok"]             = true;
                out["old_episode_id"] = old_episode_id;
                out["new_episode_id"] = new_episode_id;
                out["new_file_path"]  = new_file;
                res.set_content(out.dump(2), "application/json");
            });
        });

    // ── PHP app routes — FastCGI bridge to php-fpm ─────────────────────────
    // Matches /app/foo.php and /app/api/foo.php but NOT /app/inc/*.php
    // Auth is enforced by with_auth(); php-fpm receives X-MC1-AUTHENTICATED:1
    auto handle_php = [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            if (!g_fcgi) {
                res.status = 503;
                res.set_content("FastCGI client not available", "text/plain");
                return;
            }

            // Security: reject path traversal in PHP script path
            if (req.path.find("..") != std::string::npos) {
                res.status = 400;
                res.set_content("400 Bad Request", "text/plain");
                return;
            }

            // Absolute path to .php file on disk
            std::string script_name     = req.path;
            std::string script_filename = g_webroot + script_name;

            // Reconstruct query string from parsed params
            std::string query_string;
            for (auto it = req.params.begin(); it != req.params.end(); ++it) {
                if (!query_string.empty()) query_string += "&";
                // Simple percent-encoding of value
                for (unsigned char c : it->first)  query_string += static_cast<char>(c);
                query_string += "=";
                for (unsigned char c : it->second) {
                    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                        query_string += static_cast<char>(c);
                    } else {
                        char hex[4];
                        snprintf(hex, sizeof(hex), "%%%02X", c);
                        query_string += hex;
                    }
                }
            }

            std::string request_uri = script_name;
            if (!query_string.empty()) request_uri += "?" + query_string;

            // Gather content-type from request headers
            std::string content_type;
            {
                auto ct = req.headers.find("Content-Type");
                if (ct != req.headers.end()) content_type = ct->second;
            }

            // Forward request HTTP headers as HTTP_* FastCGI params
            std::map<std::string, std::string> extra;
            extra["HTTP_X_MC1_AUTHENTICATED"] = "1";
            // Pass the authenticated username so PHP auto_login maps to the right DB user
            std::string mc1_user = session_get_username(cookie_get(req, "mc1session"));
            if (!mc1_user.empty())
                extra["HTTP_X_MC1_USER"] = mc1_user;
            for (auto& [hname, hval] : req.headers) {
                std::string key = hname;
                std::transform(key.begin(), key.end(), key.begin(),
                    [](unsigned char c) { return std::toupper(c); });
                std::replace(key.begin(), key.end(), '-', '_');
                // These are set separately as standard CGI vars
                if (key == "CONTENT_TYPE" || key == "CONTENT_LENGTH") continue;
                // Don't forward the Authorization header to PHP
                if (key == "AUTHORIZATION") continue;
                extra["HTTP_" + key] = hval;
            }

            std::string remote_addr = req.remote_addr;
            if (remote_addr.empty()) remote_addr = "127.0.0.1";

            int server_port = (gAdminConfig.num_sockets > 0)
                              ? gAdminConfig.sockets[0].port : 8330;

            // cpp-httplib parses multipart/form-data into req.files and leaves
            // req.body empty. PHP-FPM needs the raw multipart bytes on STDIN to
            // populate $_FILES and $_POST. Re-serialize from req.files here.
            std::string fcgi_body;
            std::string fcgi_ct;
            if (req.is_multipart_form_data()) {
                char bnd_buf[48];
                auto ns = std::chrono::steady_clock::now().time_since_epoch().count();
                snprintf(bnd_buf, sizeof(bnd_buf), "Mc1Bnd%lld", (long long)ns);
                std::string boundary(bnd_buf);
                for (auto& [fname, mfd] : req.files) {
                    fcgi_body += "--";
                    fcgi_body += boundary;
                    fcgi_body += "\r\nContent-Disposition: form-data; name=\"";
                    fcgi_body += mfd.name;
                    fcgi_body += "\"";
                    if (!mfd.filename.empty()) {
                        fcgi_body += "; filename=\"";
                        fcgi_body += mfd.filename;
                        fcgi_body += "\"";
                    }
                    fcgi_body += "\r\n";
                    if (!mfd.content_type.empty()) {
                        fcgi_body += "Content-Type: ";
                        fcgi_body += mfd.content_type;
                        fcgi_body += "\r\n";
                    }
                    fcgi_body += "\r\n";
                    fcgi_body += mfd.content;
                    fcgi_body += "\r\n";
                }
                fcgi_body += "--";
                fcgi_body += boundary;
                fcgi_body += "--\r\n";
                fcgi_ct = "multipart/form-data; boundary=" + boundary;
            } else {
                fcgi_body = req.body;
                fcgi_ct   = content_type;
            }

            FcgiResponse fr = g_fcgi->forward(
                req.method,
                script_filename, script_name,
                query_string, request_uri,
                fcgi_ct, fcgi_body,
                g_webroot,
                remote_addr, "localhost", server_port,
                extra
            );

            if (!fr.ok) {
                res.status = 502;
                res.set_content("502 Bad Gateway\n" + fr.error, "text/plain");
                return;
            }

            res.status = fr.status;

            /* We detect 206 responses where PHP has already set Content-Range.
             * httplib's apply_ranges() would add a SECOND Content-Range using
             * res.body.size() (the partial length) as the total, which is wrong.
             * We fix this by stripping PHP's Content-Range, using a content_provider
             * with the real file size so httplib computes a single correct header. */
            auto cr_it = fr.headers.find("Content-Range");
            if (fr.status == 206 && cr_it != fr.headers.end()) {
                long long range_start = 0, range_end = 0, file_size = 0;
                if (std::sscanf(cr_it->second.c_str(), "bytes %lld-%lld/%lld",
                                &range_start, &range_end, &file_size) == 3
                    && file_size > 0) {
                    fr.headers.erase("Content-Range"); /* httplib will regenerate correctly */
                    for (auto& [k, v] : fr.headers)
                        res.set_header(k.c_str(), v.c_str());
                    auto data = std::move(fr.body);
                    res.set_content_provider(
                        (size_t)file_size, fr.content_type.c_str(),
                        [data = std::move(data), range_start]
                        (size_t offset, size_t length, httplib::DataSink &sink) {
                            /* We serve only the bytes PHP gave us; offset is the
                             * file-absolute byte position requested by httplib. */
                            size_t data_idx = (offset >= (size_t)range_start)
                                              ? (offset - (size_t)range_start) : 0;
                            if (data_idx < data.size()) {
                                size_t avail = data.size() - data_idx;
                                sink.write(data.data() + data_idx,
                                           std::min(length, avail));
                            }
                            return true;
                        });
                    return;
                }
            }

            /* Normal (non-range) PHP response — forward headers and body as-is. */
            for (auto& [k, v] : fr.headers)
                res.set_header(k.c_str(), v.c_str());
            res.set_content(fr.body, fr.content_type.c_str());
        });
    };

    // ── Public remote guest join page — NO auth required ───────────────────
    // GET /join/{code} → forward to remote-guest.php?code={code}
    svr.Get(R"(/join/([A-Za-z0-9]{4,32}))", [](const httplib::Request& req, httplib::Response& res) {
        if (!g_fcgi) {
            res.status = 503;
            res.set_content("FastCGI client not available", "text/plain");
            return;
        }
        std::string code = req.matches[1];
        std::string script_name     = "/remote-guest.php";
        std::string script_filename = g_webroot + script_name;
        std::string query_string    = "code=" + code;
        std::string request_uri     = script_name + "?" + query_string;

        std::map<std::string, std::string> extra;
        extra["HTTP_X_MC1_AUTHENTICATED"] = "1";

        std::string remote_addr = req.remote_addr;
        if (remote_addr.empty()) remote_addr = "127.0.0.1";
        int server_port = (gAdminConfig.num_sockets > 0)
                          ? gAdminConfig.sockets[0].port : 8330;

        FcgiResponse fr = g_fcgi->forward(
            "GET", script_filename, script_name,
            query_string, request_uri,
            "", "",
            g_webroot,
            remote_addr, "localhost", server_port,
            extra
        );

        if (!fr.ok) {
            res.status = 502;
            res.set_content("502 Bad Gateway", "text/plain");
            return;
        }
        res.status = fr.status;
        for (auto& [k, v] : fr.headers)
            res.set_header(k.c_str(), v.c_str());
        res.set_content(fr.body, fr.content_type.empty()
            ? "text/html; charset=UTF-8" : fr.content_type.c_str());
    });

    // ── Public remote API — NO auth for guest actions ───────────────────────
    // POST /api/v1/remote/guest → forward to remote.php (no auth check)
    // Guest actions are validated by session_code + participant_id inside PHP
    svr.Post("/api/v1/remote/guest", [](const httplib::Request& req, httplib::Response& res) {
        if (!g_fcgi) {
            res.status = 503;
            res.set_content("FastCGI client not available", "text/plain");
            return;
        }
        std::string script_name     = "/app/api/remote.php";
        std::string script_filename = g_webroot + script_name;
        std::string request_uri     = script_name;

        std::string content_type;
        {
            auto ct = req.headers.find("Content-Type");
            if (ct != req.headers.end()) content_type = ct->second;
        }

        std::map<std::string, std::string> extra;
        extra["HTTP_X_MC1_AUTHENTICATED"] = "1";

        std::string remote_addr = req.remote_addr;
        if (remote_addr.empty()) remote_addr = "127.0.0.1";
        int server_port = (gAdminConfig.num_sockets > 0)
                          ? gAdminConfig.sockets[0].port : 8330;

        FcgiResponse fr = g_fcgi->forward(
            "POST", script_filename, script_name,
            "", request_uri,
            content_type, req.body,
            g_webroot,
            remote_addr, "localhost", server_port,
            extra
        );

        if (!fr.ok) {
            res.status = 502;
            res.set_content("502 Bad Gateway", "text/plain");
            return;
        }
        res.status = fr.status;
        for (auto& [k, v] : fr.headers)
            res.set_header(k.c_str(), v.c_str());
        res.set_content(fr.body, fr.content_type.empty()
            ? "application/json" : fr.content_type.c_str());
    });

    // ── Public podcast RSS feed — NO auth required ────────────────────────
    // GET /podcast/{show_id}/feed.xml → forward to podcast_feed.php?show_id=N
    svr.Get(R"(/podcast/(\d+)/feed\.xml)", [](const httplib::Request& req, httplib::Response& res) {
        if (!g_fcgi) {
            res.status = 503;
            res.set_content("FastCGI client not available", "text/plain");
            return;
        }
        std::string show_id = req.matches[1];
        std::string script_name     = "/podcast_feed.php";
        std::string script_filename = g_webroot + script_name;
        std::string query_string    = "show_id=" + show_id;
        std::string request_uri     = script_name + "?" + query_string;

        std::map<std::string, std::string> extra;
        // Public endpoint — we still set X-MC1-AUTHENTICATED so the PHP
        // file boots normally, but podcast_feed.php does not check auth
        extra["HTTP_X_MC1_AUTHENTICATED"] = "1";

        std::string remote_addr = req.remote_addr;
        if (remote_addr.empty()) remote_addr = "127.0.0.1";
        int server_port = (gAdminConfig.num_sockets > 0)
                          ? gAdminConfig.sockets[0].port : 8330;

        FcgiResponse fr = g_fcgi->forward(
            "GET", script_filename, script_name,
            query_string, request_uri,
            "", "",
            g_webroot,
            remote_addr, "localhost", server_port,
            extra
        );

        if (!fr.ok) {
            res.status = 502;
            res.set_content("502 Bad Gateway", "text/plain");
            return;
        }
        res.status = fr.status;
        for (auto& [k, v] : fr.headers)
            res.set_header(k.c_str(), v.c_str());
        res.set_content(fr.body, fr.content_type.empty()
            ? "application/rss+xml; charset=UTF-8" : fr.content_type.c_str());
    });

    // ── Public podcast episode page — NO auth required ────────────────────
    // GET /shows/{id}/episodes/{eid} → forward to podcast-episode-page.php
    // MUST be registered before /shows/{id} (more specific route first)
    svr.Get(R"(/shows/(\d+)/episodes/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
        if (!g_fcgi) {
            res.status = 503;
            res.set_content("FastCGI client not available", "text/plain");
            return;
        }
        std::string show_id    = req.matches[1];
        std::string episode_id = req.matches[2];
        std::string script_name     = "/podcast-episode-page.php";
        std::string script_filename = g_webroot + script_name;
        std::string query_string    = "show_id=" + show_id + "&episode_id=" + episode_id;
        std::string request_uri     = script_name + "?" + query_string;

        std::map<std::string, std::string> extra;
        extra["HTTP_X_MC1_AUTHENTICATED"] = "1";

        std::string remote_addr = req.remote_addr;
        if (remote_addr.empty()) remote_addr = "127.0.0.1";
        int server_port = (gAdminConfig.num_sockets > 0)
                          ? gAdminConfig.sockets[0].port : 8330;

        FcgiResponse fr = g_fcgi->forward(
            "GET", script_filename, script_name,
            query_string, request_uri,
            "", "",
            g_webroot,
            remote_addr, "localhost", server_port,
            extra
        );

        if (!fr.ok) {
            res.status = 502;
            res.set_content("502 Bad Gateway", "text/plain");
            return;
        }
        res.status = fr.status;
        for (auto& [k, v] : fr.headers)
            res.set_header(k.c_str(), v.c_str());
        res.set_content(fr.body, fr.content_type.empty()
            ? "text/html; charset=UTF-8" : fr.content_type.c_str());
    });

    // ── Public podcast show page — NO auth required ─────────────────────
    // GET /shows/{id} → forward to podcast-site.php?show_id=N
    svr.Get(R"(/shows/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
        if (!g_fcgi) {
            res.status = 503;
            res.set_content("FastCGI client not available", "text/plain");
            return;
        }
        std::string show_id = req.matches[1];
        std::string script_name     = "/podcast-site.php";
        std::string script_filename = g_webroot + script_name;
        std::string query_string    = "show_id=" + show_id;
        std::string request_uri     = script_name + "?" + query_string;

        std::map<std::string, std::string> extra;
        extra["HTTP_X_MC1_AUTHENTICATED"] = "1";

        std::string remote_addr = req.remote_addr;
        if (remote_addr.empty()) remote_addr = "127.0.0.1";
        int server_port = (gAdminConfig.num_sockets > 0)
                          ? gAdminConfig.sockets[0].port : 8330;

        FcgiResponse fr = g_fcgi->forward(
            "GET", script_filename, script_name,
            query_string, request_uri,
            "", "",
            g_webroot,
            remote_addr, "localhost", server_port,
            extra
        );

        if (!fr.ok) {
            res.status = 502;
            res.set_content("502 Bad Gateway", "text/plain");
            return;
        }
        res.status = fr.status;
        for (auto& [k, v] : fr.headers)
            res.set_header(k.c_str(), v.c_str());
        res.set_content(fr.body, fr.content_type.empty()
            ? "text/html; charset=UTF-8" : fr.content_type.c_str());
    });

    // ── VoicTune / AI proxy — browser calls same-origin, we forward to VoicTune daemon ──

    // Generic proxy helper: forwards request to VoicTune on localhost:8350,
    // authenticating with the daemon API key from config.
    auto vt_proxy = [](const httplib::Request& req, httplib::Response& res,
                       const std::string& method, const std::string& target_path)
    {
        httplib::Client cli("127.0.0.1", 8350);
        cli.set_connection_timeout(5, 0);
        cli.set_read_timeout(60, 0);   // AI/Ollama calls can be slow
        cli.set_write_timeout(5, 0);

        // Build headers: forward everything except Cookie and Host
        httplib::Headers hdrs;
        for (auto& [k, v] : req.headers) {
            std::string lk = k;
            std::transform(lk.begin(), lk.end(), lk.begin(), ::tolower);
            if (lk == "cookie" || lk == "host" || lk == "connection") continue;
            hdrs.emplace(k, v);
        }

        // Staple the daemon API key
        if (gAdminConfig.daemon_keys.voictune_key[0] != '\0')
            hdrs.emplace("X-API-Token", gAdminConfig.daemon_keys.voictune_key);

        httplib::Result result;
        std::string ct = req.get_header_value("Content-Type");
        if (method == "GET") {
            // Forward query string
            std::string full = target_path;
            if (!req.params.empty()) {
                full += "?";
                bool first = true;
                for (auto& [pk, pv] : req.params) {
                    if (!first) full += "&";
                    full += pk + "=" + pv;
                    first = false;
                }
            }
            result = cli.Get(full, hdrs);
        } else if (method == "PUT") {
            result = cli.Put(target_path, hdrs, req.body,
                             ct.empty() ? "application/json" : ct);
        } else {
            result = cli.Post(target_path, hdrs, req.body,
                              ct.empty() ? "application/json" : ct);
        }

        if (!result) {
            res.status = 502;
            json err;
            err["error"] = "VoicTune daemon unreachable (127.0.0.1:8350)";
            res.set_content(err.dump(), "application/json");
            return;
        }

        res.status = result->status;
        // Forward content-type
        auto rct = result->get_header_value("Content-Type");
        res.set_content(result->body, rct.empty() ? "application/json" : rct);
    };

    // GET /api/v1/proxy/voictune/* → forward to VoicTune /api/v1/voictune/*
    svr.Get(R"(/api/v1/proxy/voictune/(.*))",
        [vt_proxy](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                std::string sub = req.matches[1];
                vt_proxy(req, res, "GET", "/api/v1/voictune/" + sub);
            });
        });

    // POST /api/v1/proxy/voictune/* → forward to VoicTune /api/v1/voictune/*
    svr.Post(R"(/api/v1/proxy/voictune/(.*))",
        [vt_proxy](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                std::string sub = req.matches[1];
                vt_proxy(req, res, "POST", "/api/v1/voictune/" + sub);
            });
        });

    // PUT /api/v1/proxy/voictune/* → forward to VoicTune /api/v1/voictune/*
    svr.Put(R"(/api/v1/proxy/voictune/(.*))",
        [vt_proxy](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                std::string sub = req.matches[1];
                vt_proxy(req, res, "PUT", "/api/v1/voictune/" + sub);
            });
        });

    // GET /api/v1/proxy/ai/* → forward to VoicTune /api/v1/ai/*
    svr.Get(R"(/api/v1/proxy/ai/(.*))",
        [vt_proxy](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                std::string sub = req.matches[1];
                vt_proxy(req, res, "GET", "/api/v1/ai/" + sub);
            });
        });

    // POST /api/v1/proxy/ai/* → forward to VoicTune /api/v1/ai/*
    svr.Post(R"(/api/v1/proxy/ai/(.*))",
        [vt_proxy](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                std::string sub = req.matches[1];
                vt_proxy(req, res, "POST", "/api/v1/ai/" + sub);
            });
        });

    // Block /app/inc/ — includes must never be served directly
    svr.Get(R"(/app/inc/.*)", [](const httplib::Request&, httplib::Response& res) {
        res.status = 403;
        res.set_content(R"({"error":"Forbidden"})", "application/json");
    });

    // /app/api/foo.php  (JSON API endpoints — most-specific first)
    svr.Get(R"(/app/api/[^/]+\.php)",  handle_php);
    svr.Post(R"(/app/api/[^/]+\.php)", handle_php);
    svr.Put(R"(/app/api/[^/]+\.php)",  handle_php);
    svr.Delete(R"(/app/api/[^/]+\.php)", handle_php);

    // /app/foo.php  (legacy app pages — backward compat)
    svr.Get(R"(/app/[^/]+\.php)",  handle_php);
    svr.Post(R"(/app/[^/]+\.php)", handle_php);

    // Root-level PHP pages: /dashboard.php, /media.php, etc.
    svr.Get(R"(/[a-zA-Z0-9_\-]+\.php)",  handle_php);
    svr.Post(R"(/[a-zA-Z0-9_\-]+\.php)", handle_php);

    // ── 404 catch-all ─────────────────────────────────────────────────────
    svr.set_error_handler([](const httplib::Request&, httplib::Response& res) {
        if (res.status == 404)
            res.set_content(R"({"error":"Not Found"})", "application/json");
    });
}

/* ── Public API: start ────────────────────────────────────────────────────── */

void http_api_start(const std::string& webroot)
{
    // Initialise logger — log_dir from config, default /var/log/mcaster1
    {
        std::string log_dir = "/var/log/mcaster1";
        if (gAdminConfig.log_dir[0] != '\0') log_dir = gAdminConfig.log_dir;
        int  lv = (gAdminConfig.log_level > 0) ? gAdminConfig.log_level : MC1_LOG_INFO;
        mc1log.init(log_dir, lv, /*also_stderr=*/true);
        MC1_INFO("mcaster1-encoder starting — log_level=" + std::to_string(lv)
                 + " log_dir=" + log_dir);
    }

    // Resolve webroot to absolute path so SCRIPT_FILENAME is correct for php-fpm
    {
        char resolved[PATH_MAX];
        if (realpath(webroot.c_str(), resolved))
            g_webroot = resolved;
        else
            g_webroot = webroot;
    }
    g_start_time = time(nullptr);

    // Initialise FastCGI client (one instance, thread-safe per-request)
    if (!g_fcgi)
        g_fcgi = new FastCgiClient("/run/php/php8.2-fpm-mc1.sock");

    // Initialise Ollama AI client (Phase AI-1) — use config values
    if (!g_ollama && gAdminConfig.ollama.enabled) {
        g_ollama = new mc1vt::OllamaClient(
            gAdminConfig.ollama.endpoint,
            gAdminConfig.ollama.model,
            gAdminConfig.ollama.timeout_sec);
        MC1_INFO("Ollama AI client initialized — endpoint="
                 + std::string(gAdminConfig.ollama.endpoint)
                 + " model=" + std::string(gAdminConfig.ollama.model)
                 + " timeout=" + std::to_string(gAdminConfig.ollama.timeout_sec) + "s");
    } else if (!gAdminConfig.ollama.enabled) {
        MC1_INFO("Ollama AI disabled in configuration");
    }

    if (!gAdminConfig.enabled || gAdminConfig.num_sockets == 0) {
        fprintf(stderr, "[http] Admin server disabled or no sockets configured.\n");
        return;
    }

    std::lock_guard<std::mutex> lk(g_listeners_mtx);
    g_listeners.clear();

    for (int i = 0; i < gAdminConfig.num_sockets; i++) {
        const mc1ListenSocket& sock = gAdminConfig.sockets[i];
        if (sock.port <= 0) continue;

        if (sock.ssl_enabled) {
            // Resolve cert/key: per-socket overrides global ssl-config
            const char* cert = (sock.ssl_cert[0]) ? sock.ssl_cert
                                                   : gAdminConfig.ssl_cert;
            const char* key  = (sock.ssl_key[0])  ? sock.ssl_key
                                                   : gAdminConfig.ssl_key;
            if (!cert[0] || !key[0]) {
                fprintf(stderr,
                    "[http] HTTPS socket :%d skipped — no cert/key configured.\n"
                    "       Generate with: mcaster1-encoder --ssl-gencert ...\n",
                    sock.port);
                continue;
            }

            // Capture by value for thread safety
            std::string bind_addr = sock.bind_address;
            int         port      = sock.port;
            std::string s_cert    = cert;
            std::string s_key     = key;

            ListenerCtx ctx;
            ctx.svr = std::make_unique<httplib::SSLServer>(s_cert.c_str(),
                                                            s_key.c_str());
            if (!ctx.svr->is_valid()) {
                fprintf(stderr, "[http] HTTPS server invalid — bad cert/key?\n"
                                "       cert: %s\n       key:  %s\n",
                        cert, key);
                continue;
            }
            setup_routes(*ctx.svr);
            ctx.th = std::thread([&svr = *ctx.svr, bind_addr, port]() {
                fprintf(stdout, "[http] HTTPS listening on %s:%d\n",
                        bind_addr.c_str(), port);
                svr.listen(bind_addr.c_str(), port);
            });
            g_listeners.push_back(std::move(ctx));

        } else {
            std::string bind_addr = sock.bind_address;
            int         port      = sock.port;

            ListenerCtx ctx;
            ctx.svr = std::make_unique<httplib::Server>();
            setup_routes(*ctx.svr);
            ctx.th = std::thread([&svr = *ctx.svr, bind_addr, port]() {
                fprintf(stdout, "[http] HTTP  listening on %s:%d\n",
                        bind_addr.c_str(), port);
                svr.listen(bind_addr.c_str(), port);
            });
            g_listeners.push_back(std::move(ctx));
        }
    }
}

/* ── Public API: stop ─────────────────────────────────────────────────────── */

void http_api_stop()
{
    std::lock_guard<std::mutex> lk(g_listeners_mtx);
    for (auto& ctx : g_listeners) {
        if (ctx.svr) ctx.svr->stop();
        if (ctx.th.joinable()) ctx.th.join();
    }
    g_listeners.clear();

    delete g_fcgi;
    g_fcgi = nullptr;

    delete g_ollama;
    g_ollama = nullptr;
}

/* ── SSL cert / CSR generation ────────────────────────────────────────────── */

static int parse_subject(const char* subj, X509_NAME* name)
{
    // Parse /C=US/ST=FL/L=Miami/O=Org/CN=host into X509_NAME entries
    std::string s(subj);
    size_t pos = 0;
    while (pos < s.size()) {
        if (s[pos] == '/') pos++;
        size_t eq  = s.find('=', pos);
        size_t end = s.find('/', eq);
        if (eq == std::string::npos) break;
        std::string key = s.substr(pos, eq - pos);
        std::string val = s.substr(eq + 1, end == std::string::npos ? end : end - eq - 1);
        X509_NAME_add_entry_by_txt(name, key.c_str(),
                                   MBSTRING_ASC,
                                   (const unsigned char*)val.c_str(),
                                   -1, -1, 0);
        pos = (end == std::string::npos) ? s.size() : end;
    }
    return 1;
}

int http_api_gencert(const char* gentype, const char* subj,
                     const char* savepath, int keysize, int days,
                     const char* basename, const char* config_path)
{
    // Ensure output directory exists
#ifdef _WIN32
    _mkdir(savepath);
#else
    mkdir(savepath, 0755);
#endif

    std::string base    = std::string(savepath) + "/" + basename;
    std::string keyfile = base + ".key";
    std::string crtfile = base + ".crt";
    std::string csrfile = base + ".csr";

    fprintf(stdout, "[ssl] Generating %d-bit RSA key...\n", keysize);

    // Generate RSA key
    EVP_PKEY* pkey = EVP_RSA_gen((unsigned int)keysize);
    if (!pkey) {
        fprintf(stderr, "[ssl] EVP_RSA_gen failed.\n");
        return 1;
    }

    // Write private key (mode 0600)
    FILE* kf = fopen(keyfile.c_str(), "wb");
    if (!kf) {
        fprintf(stderr, "[ssl] Cannot write key: %s (%s)\n",
                keyfile.c_str(), strerror(errno));
        EVP_PKEY_free(pkey);
        return 1;
    }
    PEM_write_PrivateKey(kf, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    fclose(kf);
    chmod(keyfile.c_str(), 0600);
    fprintf(stdout, "[ssl] Key written  : %s (mode 0600)\n", keyfile.c_str());

    bool is_selfsigned = (std::string(gentype) != "csr");

    if (is_selfsigned) {
        // Self-signed X.509 certificate
        X509* x509 = X509_new();
        X509_set_version(x509, 2); // v3
        ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
        X509_gmtime_adj(X509_get_notBefore(x509), 0);
        X509_gmtime_adj(X509_get_notAfter(x509),  (long)60 * 60 * 24 * days);
        X509_set_pubkey(x509, pkey);

        X509_NAME* name = X509_get_subject_name(x509);
        parse_subject(subj, name);
        X509_set_issuer_name(x509, name); // self-signed: issuer == subject

        X509_sign(x509, pkey, EVP_sha256());

        FILE* cf = fopen(crtfile.c_str(), "wb");
        if (!cf) {
            fprintf(stderr, "[ssl] Cannot write cert: %s\n", crtfile.c_str());
            X509_free(x509); EVP_PKEY_free(pkey);
            return 1;
        }
        PEM_write_X509(cf, x509);
        fclose(cf);
        chmod(crtfile.c_str(), 0644);
        fprintf(stdout, "[ssl] Cert written : %s (mode 0644, valid %d days)\n",
                crtfile.c_str(), days);
        X509_free(x509);

        if (config_path) {
            fprintf(stdout, "[ssl] TODO: auto-patch %s (Phase 3 YAML writer)\n",
                    config_path);
        }
        fprintf(stdout, "[ssl] Done. Start with:\n");
        fprintf(stdout, "       mcaster1-encoder --ssl-cert %s --ssl-key %s\n",
                crtfile.c_str(), keyfile.c_str());

    } else {
        // Certificate Signing Request
        X509_REQ* req = X509_REQ_new();
        X509_REQ_set_version(req, 0);
        X509_REQ_set_pubkey(req, pkey);

        X509_NAME* name = X509_REQ_get_subject_name(req);
        parse_subject(subj, name);
        X509_REQ_sign(req, pkey, EVP_sha256());

        FILE* cf = fopen(csrfile.c_str(), "wb");
        if (!cf) {
            fprintf(stderr, "[ssl] Cannot write CSR: %s\n", csrfile.c_str());
            X509_REQ_free(req); EVP_PKEY_free(pkey);
            return 1;
        }
        PEM_write_X509_REQ(cf, req);
        fclose(cf);
        chmod(csrfile.c_str(), 0644);
        fprintf(stdout, "[ssl] CSR written  : %s (mode 0644)\n", csrfile.c_str());
        fprintf(stdout, "[ssl] Submit %s to your CA.\n", csrfile.c_str());
        fprintf(stdout, "[ssl] Once you receive the signed cert, configure:\n");
        fprintf(stdout, "       http-admin.ssl-config.cert: <path/to/signed.crt>\n");
        X509_REQ_free(req);
    }

    EVP_PKEY_free(pkey);
    return 0;
}
