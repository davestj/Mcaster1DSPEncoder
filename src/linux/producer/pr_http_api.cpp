/*
 * Mcaster1 Producer — HTTP/HTTPS API Server
 * producer/pr_http_api.cpp
 *
 * cpp-httplib based HTTP server for Producer daemon.
 * Mirrors the vt_http_api.cpp pattern from VoicTune.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif

#include "pr_http_api.h"
#include "pr_logger.h"
#include "pr_worker_pool.h"
#include "../external/include/httplib.h"
#include "../external/include/nlohmann/json.hpp"

#include <thread>
#include <mutex>
#include <map>
#include <atomic>
#include <ctime>
#include <chrono>
#include <openssl/rand.h>

using json = nlohmann::json;

namespace mc1pr {

/* ══════════════════════════════════════════════════════════════════════════════
 * Session management (mirrors VoicTune pattern)
 * ══════════════════════════════════════════════════════════════════════════════ */

struct PrSession {
    time_t      expires;
    std::string username;
};

static std::map<std::string, PrSession> g_sessions;
static std::mutex   g_session_mtx;
static PrConfig     g_prcfg;
static ProducerWorkerPool* g_pool = nullptr;
static std::chrono::steady_clock::time_point g_start_time;

void pr_set_worker_pool(ProducerWorkerPool* pool) {
    g_pool = pool;
}

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
    g_sessions[token] = {time(nullptr) + g_prcfg.auth.session_timeout_sec, username};
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
    /* Producer session cookie */
    if (session_valid(cookie_get(req, "mc1pr_session"))) return true;
    /* API token for inter-daemon calls */
    if (!g_prcfg.auth.api_token.empty() &&
        req.get_header_value("X-API-Token") == g_prcfg.auth.api_token) return true;
    /* Cross-daemon: accept encoder admin session cookie */
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

/* Helper: convert steady_clock::time_point to epoch seconds (0 if default) */
static int64_t tp_to_epoch(std::chrono::steady_clock::time_point tp) {
    if (tp == std::chrono::steady_clock::time_point{}) return 0;
    auto wall = std::chrono::system_clock::now() +
                std::chrono::duration_cast<std::chrono::system_clock::duration>(
                    tp - std::chrono::steady_clock::now());
    return std::chrono::duration_cast<std::chrono::seconds>(
        wall.time_since_epoch()).count();
}

/* Helper: serialize a Job to JSON */
static json job_to_json(const Job& j) {
    json r;
    r["id"]           = j.id;
    r["type"]         = jobTypeToString(j.type);
    r["status"]       = jobStatusToString(j.status);
    r["progress"]     = j.progress;
    r["result_path"]  = j.result_path;
    r["error"]        = j.error_msg;
    r["submitted_at"] = tp_to_epoch(j.submitted_at);
    r["started_at"]   = tp_to_epoch(j.started_at);
    r["completed_at"] = tp_to_epoch(j.completed_at);
    /* Include raw params if present */
    if (!j.params_json.empty()) {
        try { r["params"] = json::parse(j.params_json); } catch (...) {
            r["params"] = j.params_json;
        }
    }
    return r;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Route registration
 * ══════════════════════════════════════════════════════════════════════════════ */

static void setup_routes(httplib::Server& svr)
{
    /* ── CORS: allow cross-port requests from admin UI (port 8344/8330) ── */
    svr.set_post_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        auto origin = req.get_header_value("Origin");
        if (!origin.empty()) {
            res.set_header("Access-Control-Allow-Origin", origin);
            res.set_header("Access-Control-Allow-Credentials", "true");
            res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type, X-API-Token");
        }
    });

    /* ── CORS preflight handler ──────────────────────────────────────────── */
    svr.Options(".*", [](const httplib::Request& req, httplib::Response& res) {
        auto origin = req.get_header_value("Origin");
        if (!origin.empty()) {
            res.set_header("Access-Control-Allow-Origin", origin);
            res.set_header("Access-Control-Allow-Credentials", "true");
            res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type, X-API-Token");
            res.set_header("Access-Control-Max-Age", "86400");
        }
        res.status = 204;
    });

    /* ── Health check (no auth) ──────────────────────────────────────────── */
    svr.Get("/api/v1/producer/health", [](const httplib::Request&, httplib::Response& res) {
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - g_start_time).count();
        json r;
        r["ok"]         = true;
        r["service"]    = "mcaster1-producer";
        r["version"]    = "2.0.1";
        r["uptime_sec"] = uptime;
        res.set_content(r.dump(), "application/json");
    });

    /* ── Auth: login ─────────────────────────────────────────────────────── */
    svr.Post("/api/v1/producer/auth/login", [](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); } catch (...) {
            res.status = 400;
            res.set_content(R"({"error":"Invalid JSON"})", "application/json");
            return;
        }
        std::string user = body.value("username", "");
        std::string pass = body.value("password", "");

        if (user == g_prcfg.auth.username && pass == g_prcfg.auth.password) {
            std::string token = session_create(user);
            json r;
            r["ok"]            = true;
            r["session_token"] = token;
            r["username"]      = user;
            res.set_header("Set-Cookie",
                "mc1pr_session=" + token + "; Path=/; HttpOnly; SameSite=Lax; Max-Age=" +
                std::to_string(g_prcfg.auth.session_timeout_sec));
            res.set_content(r.dump(), "application/json");
        } else {
            res.status = 401;
            res.set_content(R"({"error":"Invalid credentials"})", "application/json");
        }
    });

    /* ── Auth: logout ────────────────────────────────────────────────────── */
    svr.Post("/api/v1/producer/auth/logout", [](const httplib::Request& req, httplib::Response& res) {
        std::string token = cookie_get(req, "mc1pr_session");
        if (!token.empty()) {
            std::lock_guard<std::mutex> lk(g_session_mtx);
            g_sessions.erase(token);
        }
        res.set_header("Set-Cookie", "mc1pr_session=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0");
        res.set_content(R"({"ok":true})", "application/json");
    });

    /* ── Auth: replicate (internal, API-key only) ───────────────────────── */
    svr.Post("/api/v1/producer/auth/replicate", [](const httplib::Request& req, httplib::Response& res) {
        /* Only accept daemon API key — not user sessions */
        if (g_prcfg.auth.api_token.empty() ||
            req.get_header_value("X-API-Token") != g_prcfg.auth.api_token) {
            res.status = 403;
            res.set_content(R"({"error":"Forbidden — API key required"})", "application/json");
            return;
        }
        json body;
        try { body = json::parse(req.body); } catch (...) {
            res.status = 400;
            res.set_content(R"({"error":"Invalid JSON"})", "application/json");
            return;
        }
        std::string token    = body.value("token", "");
        std::string username = body.value("username", "");
        time_t      expires  = body.value("expires", (int64_t)0);
        if (token.empty() || username.empty() || expires == 0) {
            res.status = 400;
            res.set_content(R"({"error":"Missing token, username, or expires"})", "application/json");
            return;
        }
        {
            std::lock_guard<std::mutex> lk(g_session_mtx);
            g_sessions[token] = {expires, username};
        }
        PR_INFO("auth: replicated session for user=" + username);
        res.set_content(R"({"ok":true})", "application/json");
    });

    /* ── Auth: revoke (internal, API-key only) ──────────────────────────── */
    svr.Post("/api/v1/producer/auth/revoke", [](const httplib::Request& req, httplib::Response& res) {
        if (g_prcfg.auth.api_token.empty() ||
            req.get_header_value("X-API-Token") != g_prcfg.auth.api_token) {
            res.status = 403;
            res.set_content(R"({"error":"Forbidden — API key required"})", "application/json");
            return;
        }
        json body;
        try { body = json::parse(req.body); } catch (...) {
            res.status = 400;
            res.set_content(R"({"error":"Invalid JSON"})", "application/json");
            return;
        }
        std::string token = body.value("token", "");
        if (!token.empty()) {
            std::lock_guard<std::mutex> lk(g_session_mtx);
            g_sessions.erase(token);
            PR_INFO("auth: revoked replicated session");
        }
        res.set_content(R"({"ok":true})", "application/json");
    });

    /* ── Status (authed) ─────────────────────────────────────────────────── */
    svr.Get("/api/v1/producer/status", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - g_start_time).count();
            json r;
            r["ok"]         = true;
            r["service"]    = "mcaster1-producer";
            r["version"]    = "2.0.1";
            r["uptime_sec"] = uptime;

            if (g_pool) {
                r["workers"] = {
                    {"video_queue",  g_pool->videoQueueDepth()},
                    {"video_active", g_pool->videoActive()},
                    {"audio_queue",  g_pool->audioQueueDepth()},
                    {"audio_active", g_pool->audioActive()},
                    {"fft_queue",    g_pool->fftQueueDepth()},
                    {"fft_active",   g_pool->fftActive()}
                };
                r["total_jobs"]  = g_pool->totalJobs();
                r["active_jobs"] = g_pool->activeJobs();
            }

            res.set_content(r.dump(2), "application/json");
        });
    });

    /* ── Submit job (POST) ───────────────────────────────────────────────── */
    svr.Post("/api/v1/producer/jobs", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            if (!g_pool) {
                res.status = 503;
                res.set_content(R"({"error":"Worker pool not initialized"})", "application/json");
                return;
            }

            json body;
            try { body = json::parse(req.body); } catch (...) {
                res.status = 400;
                res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                return;
            }

            std::string type_str = body.value("type", "");
            if (type_str.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"Missing 'type' field"})", "application/json");
                return;
            }

            Job job;
            job.type = jobTypeFromString(type_str);
            if (body.contains("params")) {
                job.params_json = body["params"].dump();
            }

            int id = g_pool->submitJob(job);

            json r;
            r["ok"]     = true;
            r["job_id"] = id;
            r["type"]   = jobTypeToString(job.type);
            r["status"] = "pending";
            res.status = 201;
            res.set_content(r.dump(), "application/json");
        });
    });

    /* ── List jobs (GET) ─────────────────────────────────────────────────── */
    svr.Get("/api/v1/producer/jobs", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            json r;
            r["ok"] = true;

            if (g_pool) {
                auto jobs = g_pool->getAllJobs();
                json arr = json::array();
                for (const auto& j : jobs) {
                    arr.push_back(job_to_json(j));
                }
                r["jobs"]  = arr;
                r["count"] = (int)jobs.size();
            } else {
                r["jobs"]  = json::array();
                r["count"] = 0;
            }

            res.set_content(r.dump(2), "application/json");
        });
    });

    /* ── Get single job (GET /api/v1/producer/jobs/:id) ──────────────────── */
    svr.Get(R"(/api/v1/producer/jobs/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            int jobId = std::stoi(req.matches[1]);

            if (!g_pool) {
                res.status = 503;
                res.set_content(R"({"error":"Worker pool not initialized"})", "application/json");
                return;
            }

            Job job;
            if (!g_pool->getJobStatus(jobId, job)) {
                res.status = 404;
                res.set_content(R"({"error":"Job not found"})", "application/json");
                return;
            }

            json r = job_to_json(job);
            r["ok"] = true;
            res.set_content(r.dump(2), "application/json");
        });
    });

    /* ── Cancel job (DELETE /api/v1/producer/jobs/:id) ────────────────────── */
    svr.Delete(R"(/api/v1/producer/jobs/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            int jobId = std::stoi(req.matches[1]);

            if (!g_pool) {
                res.status = 503;
                res.set_content(R"({"error":"Worker pool not initialized"})", "application/json");
                return;
            }

            if (g_pool->cancelJob(jobId)) {
                json r;
                r["ok"]     = true;
                r["job_id"] = jobId;
                r["status"] = "cancelled";
                res.set_content(r.dump(), "application/json");
            } else {
                res.status = 404;
                res.set_content(R"({"error":"Job not found or already completed"})", "application/json");
            }
        });
    });
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Server lifecycle
 * ══════════════════════════════════════════════════════════════════════════════ */

static std::shared_ptr<httplib::Server> g_http_svr;
static std::shared_ptr<httplib::Server> g_https_svr;
static std::thread g_http_thread;
static std::thread g_https_thread;
static std::atomic<bool> g_running{false};

void pr_http_start(const PrConfig& cfg)
{
    g_prcfg      = cfg;
    g_running    = true;
    g_start_time = std::chrono::steady_clock::now();

    int listener_count = 0;

    /* Plain HTTP listener */
    {
        g_http_svr = std::make_shared<httplib::Server>();
        setup_routes(*g_http_svr);
        int port = cfg.http.port;
        std::string bind = cfg.http.bind;
        g_http_thread = std::thread([bind, port]() {
            PR_INFO("Producer HTTP listening on " + bind + ":" + std::to_string(port));
            if (!g_http_svr->listen(bind, port)) {
                PR_ERR("HTTP listen failed on port " + std::to_string(port));
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
                PR_INFO("Producer HTTPS listening on " + bind + ":" + std::to_string(ssl_port));
                if (!g_https_svr->listen(bind, ssl_port)) {
                    PR_ERR("HTTPS listen failed on port " + std::to_string(ssl_port));
                }
            });
            ++listener_count;
        } else {
            PR_WARN("SSL cert/key invalid — HTTPS disabled");
        }
    }

    PR_INFO("Producer HTTP API started — " + std::to_string(listener_count) + " listener(s)");

    /* Block until listeners finish (they run until stop() is called) */
    if (g_http_thread.joinable())  g_http_thread.join();
    if (g_https_thread.joinable()) g_https_thread.join();
}

void pr_http_stop()
{
    g_running = false;
    if (g_http_svr)  g_http_svr->stop();
    if (g_https_svr) g_https_svr->stop();
    PR_INFO("Producer HTTP API stopped");
}

} // namespace mc1pr
