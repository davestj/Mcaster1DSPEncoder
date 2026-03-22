/*
 * Mcaster1 VoicTune — HTTP/HTTPS API Server
 * voictune/vt_http_api.cpp
 *
 * cpp-httplib based HTTP server for VoicTune daemon.
 * Mirrors the http_api.cpp pattern from the encoder admin.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif

#include "vt_http_api.h"
#include "vt_logger.h"
#include "../external/include/httplib.h"
#include "../external/include/nlohmann/json.hpp"

#include <thread>
#include <vector>
#include <mutex>
#include <map>
#include <atomic>
#include <ctime>
#include <cstdlib>
#include <openssl/rand.h>

using json = nlohmann::json;

namespace mc1vt {

/* ══════════════════════════════════════════════════════════════════════════════
 * Session management (mirrors encoder admin pattern)
 * ══════════════════════════════════════════════════════════════════════════════ */

struct VtSession {
    time_t      expires;
    std::string username;
};

static std::map<std::string, VtSession> g_sessions;
static std::mutex g_session_mtx;
static VtConfig   g_vtcfg;

static std::string gen_token() {
    unsigned char buf[32];
    RAND_bytes(buf, sizeof(buf));
    char hex[65];
    for (int i = 0; i < 32; ++i) sprintf(hex + i * 2, "%02x", buf[i]);
    hex[64] = '\0';
    return std::string(hex);
}

static std::string session_create(const std::string& username) {
    std::lock_guard<std::mutex> lk(g_session_mtx);
    std::string token = gen_token();
    g_sessions[token] = {time(nullptr) + g_vtcfg.auth.session_timeout_sec, username};
    return token;
}

static bool session_valid(const std::string& token) {
    if (token.empty()) return false;
    std::lock_guard<std::mutex> lk(g_session_mtx);
    auto it = g_sessions.find(token);
    if (it == g_sessions.end()) return false;
    if (it->second.expires < time(nullptr)) {
        g_sessions.erase(it);
        return false;
    }
    return true;
}

static std::string cookie_get(const httplib::Request& req, const std::string& name) {
    auto it = req.headers.find("Cookie");
    while (it != req.headers.end() && it->first == "Cookie") {
        size_t pos = it->second.find(name + "=");
        if (pos != std::string::npos) {
            size_t start = pos + name.size() + 1;
            size_t end   = it->second.find(';', start);
            return it->second.substr(start, end == std::string::npos ? end : end - start);
        }
        ++it;
    }
    return "";
}

static bool request_is_authed(const httplib::Request& req) {
    if (session_valid(cookie_get(req, "mc1vt_session"))) return true;
    if (!g_vtcfg.auth.api_token.empty() &&
        req.get_header_value("X-API-Token") == g_vtcfg.auth.api_token) return true;
    /* We also accept the encoder admin session cookie for cross-daemon auth */
    if (session_valid(cookie_get(req, "mc1session"))) return true;
    return false;
}

static void with_auth(const httplib::Request& req, httplib::Response& res, std::function<void()> fn) {
    if (!request_is_authed(req)) {
        res.status = 401;
        res.set_content(R"({"error":"Unauthorized"})", "application/json");
        return;
    }
    fn();
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Route registration
 * ══════════════════════════════════════════════════════════════════════════════ */

static void setup_routes(httplib::Server& svr)
{
    /* ── Health check (no auth) ──────────────────────────────────────────── */
    svr.Get("/api/v1/voictune/health", [](const httplib::Request&, httplib::Response& res) {
        json r;
        r["ok"]      = true;
        r["service"] = "mcaster1-voictune";
        r["version"] = "1.0.0";
        r["uptime_sec"] = 0; /* TODO: track startup time */
        res.set_content(r.dump(), "application/json");
    });

    /* ── Auth: login ─────────────────────────────────────────────────────── */
    svr.Post("/api/v1/voictune/auth/login", [](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); } catch (...) {
            res.status = 400;
            res.set_content(R"({"error":"Invalid JSON"})", "application/json");
            return;
        }
        std::string user = body.value("username", "");
        std::string pass = body.value("password", "");

        if (user == g_vtcfg.auth.username && pass == g_vtcfg.auth.password) {
            std::string token = session_create(user);
            json r;
            r["ok"]            = true;
            r["session_token"] = token;
            r["username"]      = user;
            res.set_header("Set-Cookie",
                "mc1vt_session=" + token + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=" +
                std::to_string(g_vtcfg.auth.session_timeout_sec));
            res.set_content(r.dump(), "application/json");
        } else {
            res.status = 401;
            res.set_content(R"({"error":"Invalid credentials"})", "application/json");
        }
    });

    /* ── Auth: logout ────────────────────────────────────────────────────── */
    svr.Post("/api/v1/voictune/auth/logout", [](const httplib::Request& req, httplib::Response& res) {
        std::string token = cookie_get(req, "mc1vt_session");
        if (!token.empty()) {
            std::lock_guard<std::mutex> lk(g_session_mtx);
            g_sessions.erase(token);
        }
        res.set_header("Set-Cookie", "mc1vt_session=; Path=/; Max-Age=0");
        res.set_content(R"({"ok":true})", "application/json");
    });

    /* ── Status ──────────────────────────────────────────────────────────── */
    svr.Get("/api/v1/voictune/status", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            json r;
            r["ok"]      = true;
            r["service"] = "mcaster1-voictune";
            r["version"] = "1.0.0";
            r["audio"]   = {
                {"input_device_index", g_vtcfg.audio.input_device_index},
                {"sample_rate", g_vtcfg.audio.sample_rate},
                {"channels", g_vtcfg.audio.channels},
                {"hotplug_enabled", g_vtcfg.audio.hotplug_enabled}
            };
            r["websocket_port"] = g_vtcfg.websocket.port;
            r["ollama_endpoint"] = g_vtcfg.ollama.endpoint;
            res.set_content(r.dump(2), "application/json");
        });
    });

    /* ── Device enumeration (placeholder — VT-2 fills in PortAudio) ──── */
    svr.Get("/api/v1/voictune/devices", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            json r;
            r["ok"]      = true;
            r["inputs"]  = json::array();
            r["outputs"] = json::array();
            r["message"] = "Device enumeration available after VT-2 phase (PortAudio integration)";
            res.set_content(r.dump(2), "application/json");
        });
    });

    /* ── Meters (placeholder — VT-2 fills in FFT/RMS) ────────────────── */
    svr.Get("/api/v1/voictune/meters", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            json r;
            r["ok"]      = true;
            r["rms_db"]  = -96.0;
            r["peak_db"] = -96.0;
            r["lufs"]    = -96.0;
            r["pitch_hz"]= 0.0;
            r["note"]    = "";
            r["cents"]   = 0.0;
            r["message"] = "Live meters available after VT-2 phase (FFT analysis)";
            res.set_content(r.dump(2), "application/json");
        });
    });

    /* ── Spectrum (placeholder) ──────────────────────────────────────── */
    svr.Get("/api/v1/voictune/spectrum", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            json r;
            r["ok"]   = true;
            r["bins"]  = json::array();
            r["message"] = "Spectrum data available after VT-2 phase";
            res.set_content(r.dump(), "application/json");
        });
    });

    /* ── AI status (placeholder — AI-1 fills in Ollama) ──────────────── */
    svr.Get("/api/v1/ai/status", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            json r;
            r["ok"]        = true;
            r["available"] = false;
            r["endpoint"]  = g_vtcfg.ollama.endpoint;
            r["model"]     = g_vtcfg.ollama.model;
            r["message"]   = "Ollama integration available after AI-1 phase";
            res.set_content(r.dump(2), "application/json");
        });
    });

    VT_INFO("VoicTune routes registered");
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Server lifecycle
 * ══════════════════════════════════════════════════════════════════════════════ */

/* We use shared_ptr for stable references across threads */
static std::shared_ptr<httplib::Server> g_http_svr;
static std::shared_ptr<httplib::Server> g_https_svr;
static std::thread g_http_thread;
static std::thread g_https_thread;
static std::atomic<bool> g_running{false};

void vt_http_start(const VtConfig& cfg)
{
    g_vtcfg  = cfg;
    g_running = true;

    auto& log = VtLogger::instance();
    log.set_log_dir(cfg.log.dir);
    log.set_level(cfg.log.level);

    int listener_count = 0;

    /* Plain HTTP listener */
    {
        g_http_svr = std::make_shared<httplib::Server>();
        setup_routes(*g_http_svr);
        int port = cfg.http.port;
        std::string bind = cfg.http.bind;
        g_http_thread = std::thread([bind, port]() {
            VT_INFO("VoicTune HTTP listening on " + bind + ":" + std::to_string(port));
            if (!g_http_svr->listen(bind, port)) {
                VT_ERR("HTTP listen failed on port " + std::to_string(port));
            }
        });
        ++listener_count;
    }

    /* HTTPS listener (if cert configured) */
    if (!cfg.http.ssl_cert.empty() && !cfg.http.ssl_key.empty()) {
        auto ssl_svr = std::make_shared<httplib::SSLServer>(
            cfg.http.ssl_cert.c_str(), cfg.http.ssl_key.c_str());
        if (ssl_svr->is_valid()) {
            setup_routes(*ssl_svr);
            g_https_svr = ssl_svr;
            int ssl_port = cfg.http.ssl_port;
            std::string bind = cfg.http.bind;
            g_https_thread = std::thread([bind, ssl_port]() {
                VT_INFO("VoicTune HTTPS listening on " + bind + ":" + std::to_string(ssl_port));
                if (!g_https_svr->listen(bind, ssl_port)) {
                    VT_ERR("HTTPS listen failed on port " + std::to_string(ssl_port));
                }
            });
            ++listener_count;
        } else {
            VT_WARN("SSL cert/key invalid — HTTPS disabled");
        }
    }

    VT_INFO("VoicTune HTTP API started — " + std::to_string(listener_count) + " listener(s)");

    /* We block until listeners finish (they run until stop() is called) */
    if (g_http_thread.joinable())  g_http_thread.join();
    if (g_https_thread.joinable()) g_https_thread.join();
}

void vt_http_stop()
{
    g_running = false;
    if (g_http_svr)  g_http_svr->stop();
    if (g_https_svr) g_https_svr->stop();
    VT_INFO("VoicTune HTTP API stopped");
}

} // namespace mc1vt
