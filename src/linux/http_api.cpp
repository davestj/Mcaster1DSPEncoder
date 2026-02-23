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
#endif

#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/rsa.h>
#include <openssl/bn.h>
#include <openssl/evp.h>

#include <string>
#include <map>
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

// Per-listener context — keeps server alive and joinable
struct ListenerCtx {
    std::unique_ptr<httplib::Server> svr;
    std::thread                      th;
};
static std::vector<ListenerCtx> g_listeners;
static std::mutex               g_listeners_mtx;

/* ── Session store ────────────────────────────────────────────────────────── */

struct MC1Session { time_t expires; };
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

static std::string session_create()
{
    std::string tok = gen_token();
    int ttl = gAdminConfig.session_timeout_secs > 0
                  ? gAdminConfig.session_timeout_secs : 3600;
    std::lock_guard<std::mutex> lk(g_session_mtx);
    g_sessions[tok] = { time(nullptr) + ttl };
    return tok;
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

static bool check_creds(const std::string& user, const std::string& pass)
{
    // TODO: htpasswd file support (bcrypt via OpenSSL EVP)
    return user == gAdminConfig.admin_username
        && pass == gAdminConfig.admin_password;
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
    // ── Root redirect ──────────────────────────────────────────────────────
    svr.Get("/", [](const httplib::Request& req, httplib::Response& res) {
        res.status = 302;
        res.set_header("Location", request_is_authed(req) ? "/dashboard" : "/login");
    });

    // ── Login page (no auth required) ─────────────────────────────────────
    svr.Get("/login", [](const httplib::Request&, httplib::Response& res) {
        serve_file(g_webroot + "/login.html", res);
    });

    // ── Dashboard (auth required) ──────────────────────────────────────────
    svr.Get("/dashboard", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() { serve_file(g_webroot + "/index.html", res); });
    });

    // ── Static assets (CSS/JS/fonts — no auth so login page can use them) ─
    svr.Get(R"(/(.+\.(css|js|mjs|ico|png|jpg|jpeg|gif|webp|svg|woff|woff2|ttf|otf)))",
        [](const httplib::Request& req, httplib::Response& res) {
            serve_file(g_webroot + "/" + req.matches[1].str(), res);
        });

    // ── POST /api/v1/auth/login ────────────────────────────────────────────
    svr.Post("/api/v1/auth/login",
        [](const httplib::Request& req, httplib::Response& res) {
            json body;
            try { body = json::parse(req.body); }
            catch (...) {
                res.status = 400;
                res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                return;
            }
            std::string user = body.value("username", "");
            std::string pass = body.value("password", "");
            if (check_creds(user, pass)) {
                std::string tok = session_create();
                int ttl = gAdminConfig.session_timeout_secs > 0
                              ? gAdminConfig.session_timeout_secs : 3600;
                std::string cookie = "mc1session=" + tok
                    + "; Path=/; HttpOnly; SameSite=Strict; Max-Age="
                    + std::to_string(ttl);
                res.set_header("Set-Cookie", cookie);
                json r; r["ok"] = true; r["redirect"] = "/dashboard";
                res.set_content(r.dump(), "application/json");
            } else {
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
                j["version"]  = "1.2.0";
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
                    res.set_content(R"({"ok":true})", "application/json");
                } else {
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
                res.set_content(R"({"ok":true})", "application/json");
#else
                res.set_content(R"({"ok":false})", "application/json");
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
                            g_pipeline->push_metadata(s.track_index, title, artist);
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
                if (body.contains("eq_preset"))
                    cfg.dsp_eq_preset         = body["eq_preset"].get<std::string>();

                mc1dsp::DspChainConfig dsp_cfg;
                dsp_cfg.sample_rate        = cfg.sample_rate;
                dsp_cfg.channels           = cfg.channels;
                dsp_cfg.eq_enabled         = cfg.dsp_eq_enabled;
                dsp_cfg.agc_enabled        = cfg.dsp_agc_enabled;
                dsp_cfg.crossfader_enabled = cfg.dsp_crossfade_enabled;
                dsp_cfg.crossfade_duration = cfg.dsp_crossfade_duration;
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

    // ── GET /api/v1/dnas/stats — proxy live stats from Mcaster1DNAS server ─
    svr.Get("/api/v1/dnas/stats",
        [](const httplib::Request& req, httplib::Response& res) {
            with_auth(req, res, [&]() {
                // Fetch stats from DNAS admin endpoint via HTTPS
                httplib::SSLClient cli("dnas.mcaster1.com", 9443);
                cli.enable_server_certificate_verification(false);
                cli.set_basic_auth("djpulse", "#!3wrvNN57761");
                cli.set_connection_timeout(5);
                cli.set_read_timeout(10);

                auto r = cli.Get("/admin/mcaster1stats");
                if (!r) {
                    res.status = 502;
                    json e;
                    e["error"]  = "DNAS unreachable";
                    e["source"] = "dnas.mcaster1.com:9443";
                    res.set_content(e.dump(), "application/json");
                    return;
                }

                json j;
                j["ok"]           = (r->status == 200);
                j["http_status"]  = r->status;
                j["content_type"] = r->get_header_value("Content-Type");
                j["body"]         = r->body;
                j["source"]       = "dnas.mcaster1.com:9443/admin/mcaster1stats";
                res.set_content(j.dump(2), "application/json");
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

            FcgiResponse fr = g_fcgi->forward(
                req.method,
                script_filename, script_name,
                query_string, request_uri,
                content_type, req.body,
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
            for (auto& [k, v] : fr.headers)
                res.set_header(k.c_str(), v.c_str());
            res.set_content(fr.body, fr.content_type.c_str());
        });
    };

    // /app/foo.php  (top-level app pages)
    svr.Get(R"(/app/[^/]+\.php)",  handle_php);
    svr.Post(R"(/app/[^/]+\.php)", handle_php);

    // /app/api/foo.php  (JSON API endpoints)
    svr.Get(R"(/app/api/[^/]+\.php)",  handle_php);
    svr.Post(R"(/app/api/[^/]+\.php)", handle_php);

    // ── 404 catch-all ─────────────────────────────────────────────────────
    svr.set_error_handler([](const httplib::Request&, httplib::Response& res) {
        if (res.status == 404)
            res.set_content(R"({"error":"Not Found"})", "application/json");
    });
}

/* ── Public API: start ────────────────────────────────────────────────────── */

void http_api_start(const std::string& webroot)
{
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
