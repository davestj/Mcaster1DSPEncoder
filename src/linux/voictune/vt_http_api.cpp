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
#include "vt_audio_capture.h"
#include "vt_usb_monitor.h"
#include "vt_websocket.h"
#include "vt_db.h"
#include "vt_coach.h"
#include "vt_analysis_state.h"
#include "ollama_client.h"
#include "ai_prompt_templates.h"
#include "../external/include/httplib.h"
#include "../external/include/nlohmann/json.hpp"

#include <thread>
#include <vector>
#include <mutex>
#include <map>
#include <atomic>
#include <algorithm>
#include <ctime>
#include <cstdlib>
#include <chrono>
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
static VtSubsystems g_sub;
static std::chrono::steady_clock::time_point g_start_time;

void vt_set_subsystems(const VtSubsystems& sub) {
    g_sub = sub;
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
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - g_start_time).count();
        json r;
        r["ok"]         = true;
        r["service"]    = "mcaster1-voictune";
        r["version"]    = "2.0.1";
        r["uptime_sec"] = uptime;
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

    /* ── Auth: replicate (internal, API-key only) ───────────────────────── */
    svr.Post("/api/v1/voictune/auth/replicate", [](const httplib::Request& req, httplib::Response& res) {
        /* Only accept daemon API key — not user sessions */
        if (g_vtcfg.auth.api_token.empty() ||
            req.get_header_value("X-API-Token") != g_vtcfg.auth.api_token) {
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
        VT_INFO("auth: replicated session for user=" + username);
        res.set_content(R"({"ok":true})", "application/json");
    });

    /* ── Auth: revoke (internal, API-key only) ──────────────────────────── */
    svr.Post("/api/v1/voictune/auth/revoke", [](const httplib::Request& req, httplib::Response& res) {
        if (g_vtcfg.auth.api_token.empty() ||
            req.get_header_value("X-API-Token") != g_vtcfg.auth.api_token) {
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
            VT_INFO("auth: revoked replicated session");
        }
        res.set_content(R"({"ok":true})", "application/json");
    });

    /* ── Status ──────────────────────────────────────────────────────────── */
    svr.Get("/api/v1/voictune/status", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            json r;
            r["ok"]      = true;
            r["service"] = "mcaster1-voictune";
            r["version"] = "2.0.1";
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

    /* ── Device enumeration ──────────────────────────────────────────── */
    svr.Get("/api/v1/voictune/devices", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            json r;
            r["ok"] = true;

            if (g_sub.audio_capture) {
                auto devs = g_sub.audio_capture->list_devices();
                json inputs = json::array(), outputs = json::array();
                for (const auto& d : devs) {
                    json dj;
                    dj["index"]        = d.index;
                    dj["name"]         = d.name;
                    dj["sample_rate"]  = d.default_sample_rate;
                    if (d.max_input_ch > 0) {
                        dj["channels"]       = d.max_input_ch;
                        dj["is_default"]     = d.is_default_input;
                        dj["is_usb"]         = false;
                        dj["is_bluetooth"]   = false;
                        inputs.push_back(dj);
                    }
                    if (d.max_output_ch > 0) {
                        json oj = dj;
                        oj["channels"]     = d.max_output_ch;
                        oj["is_default"]   = d.is_default_output;
                        outputs.push_back(oj);
                    }
                }

                /* Enrich with USB/BT flags from usb_monitor */
                if (g_sub.usb_monitor) {
                    auto usb_devs = g_sub.usb_monitor->list_usb_devices();
                    for (auto& indev : inputs) {
                        int idx = indev["index"].get<int>();
                        for (const auto& ud : usb_devs) {
                            if (ud.pa_device_index == idx) {
                                indev["is_usb"]       = ud.is_usb;
                                indev["is_bluetooth"]  = ud.is_bluetooth;
                                if (!ud.usb_id.empty())
                                    indev["usb_id"] = ud.usb_id;
                                break;
                            }
                        }
                    }
                }

                r["inputs"]  = inputs;
                r["outputs"] = outputs;
            } else {
                r["inputs"]  = json::array();
                r["outputs"] = json::array();
                r["message"] = "PortAudio not initialized — start audio capture first";
            }

            r["capturing"]    = g_sub.audio_capture ? g_sub.audio_capture->is_capturing() : false;
            r["active_device"] = g_sub.audio_capture ? g_sub.audio_capture->active_device() : -1;
            r["ws_clients"]    = g_sub.websocket ? g_sub.websocket->client_count() : 0;

            res.set_content(r.dump(2), "application/json");
        });
    });

    /* ── Meters (live — reads atomics from AnalysisState) ──────────── */
    svr.Get("/api/v1/voictune/meters", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            json r;
            r["ok"] = true;
            if (g_sub.analysis) {
                r["rms_db"]       = g_sub.analysis->rms_db();
                r["peak_db"]      = g_sub.analysis->peak_db();
                r["lufs"]         = g_sub.analysis->lufs();
                r["peak_hold_db"] = g_sub.analysis->peak_hold_db();
                r["pitch_hz"]     = g_sub.analysis->pitch_hz();
                r["note"]         = g_sub.analysis->note_name();
                r["midi_note"]    = g_sub.analysis->midi_note();
                r["cents"]        = g_sub.analysis->cents_off();
                r["confidence"]   = g_sub.analysis->pitch_confidence();
                r["spectral_centroid_hz"] = g_sub.analysis->spectral_centroid();
                r["peak_frequency_hz"]    = g_sub.analysis->peak_frequency();
                r["analyzing"]    = g_sub.analysis->analyzing();
                r["chunks"]       = g_sub.analysis->chunk_count();
            } else {
                r["rms_db"]  = -96.0;
                r["peak_db"] = -96.0;
                r["lufs"]    = -96.0;
                r["pitch_hz"]= 0.0;
                r["note"]    = "";
                r["cents"]   = 0.0;
                r["message"] = "Analysis state not initialized";
            }
            res.set_content(r.dump(2), "application/json");
        });
    });

    /* ── Spectrum (live — mutex lock, copy magnitude array) ─────────── */
    svr.Get("/api/v1/voictune/spectrum", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            json r;
            r["ok"] = true;
            if (g_sub.analysis) {
                auto bins = g_sub.analysis->spectrum();
                r["bins"]  = bins;
                r["count"] = (int)bins.size();
                r["peak_frequency_hz"]    = g_sub.analysis->peak_frequency();
                r["spectral_centroid_hz"] = g_sub.analysis->spectral_centroid();
            } else {
                r["bins"]  = json::array();
                r["count"] = 0;
                r["message"] = "Analysis state not initialized";
            }
            res.set_content(r.dump(), "application/json");
        });
    });

    /* ── Waveform (live — mutex lock, ring buffer copy) ──────────── */
    svr.Get("/api/v1/voictune/waveform", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            json r;
            r["ok"] = true;
            if (g_sub.analysis) {
                auto wf = g_sub.analysis->waveform();
                /* Downsample to ~256 points for efficient JSON transport */
                int src_size = (int)wf.size();
                int target = 256;
                int step = std::max(1, src_size / target);
                json arr = json::array();
                for (int i = 0; i < src_size; i += step) {
                    arr.push_back(wf[i]);
                }
                r["samples"] = arr;
                r["count"]   = (int)arr.size();
                r["full_size"] = src_size;
            } else {
                r["samples"] = json::array();
                r["count"]   = 0;
                r["message"] = "Analysis state not initialized";
            }
            res.set_content(r.dump(), "application/json");
        });
    });

    /* ── Device switch (PUT) ─────────────────────────────────────────── */
    svr.Put("/api/v1/voictune/device", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            json body;
            try { body = json::parse(req.body); } catch (...) {
                res.status = 400;
                res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                return;
            }
            int dev_index  = body.value("device_index", -1);
            int sr         = body.value("sample_rate", g_vtcfg.audio.sample_rate);
            int ch         = body.value("channels", g_vtcfg.audio.channels);
            int buf_frames = body.value("buffer_frames", g_vtcfg.audio.buffer_frames);

            if (!g_sub.audio_capture) {
                res.status = 503;
                res.set_content(R"({"error":"Audio capture not initialized"})", "application/json");
                return;
            }

            /* Stop current capture */
            g_sub.audio_capture->stop();
            VT_INFO("Device switch requested: index=" + std::to_string(dev_index));

            /* Re-enumerate in case of hotplug */
            g_sub.audio_capture->re_enumerate();

            /* The callback is wired in main — we store the current callback
             * by re-starting with the same callback the system was using.
             * main_voictune sets the callback before HTTP starts, so we just
             * need to restart capture. The callback_ member persists. */
            /* Note: We cannot access the internal callback from here.
             * Instead, we update the config and let main handle restart.
             * For now, we just flag the device index change. */
            g_vtcfg.audio.input_device_index = dev_index;
            g_vtcfg.audio.sample_rate = sr;
            g_vtcfg.audio.channels = ch;
            g_vtcfg.audio.buffer_frames = buf_frames;

            json r;
            r["ok"] = true;
            r["message"] = "Device stopped. Reconfigure and restart capture via /session/start";
            r["device_index"] = dev_index;
            res.set_content(r.dump(2), "application/json");
        });
    });

    /* ── Session start ───────────────────────────────────────────────── */
    svr.Post("/api/v1/voictune/session/start", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            json body;
            try { body = json::parse(req.body); } catch (...) {
                body = json::object();
            }
            std::string session_name = body.value("name", "VoicTune Session");

            if (!g_sub.analysis) {
                res.status = 503;
                res.set_content(R"({"error":"Analysis state not initialized"})", "application/json");
                return;
            }

            if (g_sub.analysis->session_active()) {
                res.status = 409;
                json r;
                r["error"] = "Session already active";
                r["session_id"] = g_sub.analysis->session_id();
                res.set_content(r.dump(), "application/json");
                return;
            }

            int session_id = 0;
            if (g_sub.db && g_sub.db->is_connected()) {
                session_id = g_sub.db->create_session(1 /* user_id */, session_name);
            }

            g_sub.analysis->set_session_id(session_id);
            g_sub.analysis->set_session_active(true);

            /* Reset coach state for new session */
            if (g_sub.coach) g_sub.coach->reset();

            json r;
            r["ok"]         = true;
            r["session_id"] = session_id;
            r["name"]       = session_name;
            VT_INFO("Session started: id=" + std::to_string(session_id) + " name=" + session_name);
            res.set_content(r.dump(2), "application/json");
        });
    });

    /* ── Session stop ────────────────────────────────────────────────── */
    svr.Post("/api/v1/voictune/session/stop", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            if (!g_sub.analysis) {
                res.status = 503;
                res.set_content(R"({"error":"Analysis state not initialized"})", "application/json");
                return;
            }

            int sid = g_sub.analysis->session_id();
            g_sub.analysis->set_session_active(false);
            g_sub.analysis->set_session_id(0);

            if (g_sub.db && g_sub.db->is_connected() && sid > 0) {
                g_sub.db->end_session(sid);
            }

            json r;
            r["ok"]         = true;
            r["session_id"] = sid;
            r["message"]    = "Session ended";
            VT_INFO("Session stopped: id=" + std::to_string(sid));
            res.set_content(r.dump(2), "application/json");
        });
    });

    /* ── Coaching tips ───────────────────────────────────────────────── */
    svr.Get("/api/v1/voictune/coaching/tips", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            json r;
            r["ok"] = true;
            if (g_sub.analysis) {
                auto tips = g_sub.analysis->tips();
                json arr = json::array();
                for (const auto& t : tips) {
                    json tj;
                    tj["severity"]   = (t.severity == CoachSeverity::INFO) ? "info" :
                                       (t.severity == CoachSeverity::SUGGESTION) ? "suggestion" :
                                       (t.severity == CoachSeverity::WARNING) ? "warning" : "critical";
                    tj["category"]   = t.category;
                    tj["message"]    = t.message;
                    tj["suggestion"] = t.suggestion;
                    tj["confidence"] = t.confidence;
                    arr.push_back(tj);
                }
                r["tips"]  = arr;
                r["count"] = (int)arr.size();
            } else {
                r["tips"]  = json::array();
                r["count"] = 0;
                r["message"] = "Analysis state not initialized";
            }
            res.set_content(r.dump(2), "application/json");
        });
    });

    /* ── AI status (live Ollama availability check) ──────────────────── */
    svr.Get("/api/v1/ai/status", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            json r;
            r["ok"]       = true;
            r["endpoint"] = g_vtcfg.ollama.endpoint;
            r["model"]    = g_vtcfg.ollama.model;

            if (g_sub.ollama) {
                bool avail = g_sub.ollama->is_available();
                r["available"] = avail;
                if (avail) {
                    auto models_resp = g_sub.ollama->list_models();
                    if (models_resp.contains("models"))
                        r["models"] = models_resp["models"];
                    else
                        r["models"] = json::array();
                }
            } else {
                r["available"] = false;
                r["message"]   = "Ollama client not initialized";
            }

            res.set_content(r.dump(2), "application/json");
        });
    });

    /* ── AI chat — generic pass-through to Ollama ──────────────────────── */
    svr.Post("/api/v1/ai/chat", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            if (!g_sub.ollama) {
                res.set_content(R"({"error":"Ollama client not initialized","available":false})",
                                "application/json");
                return;
            }
            if (!g_sub.ollama->is_available()) {
                res.set_content(R"({"error":"Ollama not available. Start with: ollama serve","available":false})",
                                "application/json");
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
            json result = g_sub.ollama->chat(messages, model);
            auto t1 = std::chrono::steady_clock::now();
            int latency_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

            /* Log to DB if available */
            if (g_sub.db && !result.contains("error")) {
                DbAiInteraction ai;
                ai.user_id       = 1;
                ai.context       = "chat";
                ai.prompt_text   = messages.back().value("content", "");
                ai.model_used    = model.empty() ? g_sub.ollama->model() : model;
                ai.latency_ms    = latency_ms;
                if (result.contains("message") && result["message"].contains("content"))
                    ai.response_text = result["message"]["content"].get<std::string>();
                g_sub.db->log_ai_interaction(ai);
            }

            json out;
            if (result.contains("error")) {
                out["ok"]    = false;
                out["error"] = result["error"];
            } else {
                out["ok"]         = true;
                out["response"]   = result;
                out["model"]      = model.empty() ? g_sub.ollama->model() : model;
                out["latency_ms"] = latency_ms;
            }
            res.set_content(out.dump(2), "application/json");
        });
    });

    /* ── AI coaching — voice analysis with system prompt ─────────────── */
    svr.Post("/api/v1/ai/coaching", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            if (!g_sub.ollama) {
                res.set_content(R"({"error":"Ollama client not initialized","available":false})",
                                "application/json");
                return;
            }
            if (!g_sub.ollama->is_available()) {
                res.set_content(R"({"error":"Ollama not available. Start with: ollama serve","available":false})",
                                "application/json");
                return;
            }

            json body;
            try { body = json::parse(req.body); } catch (...) {
                res.status = 400;
                res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                return;
            }

            std::string user_message = body.value("message", "");
            if (user_message.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"message field required"})", "application/json");
                return;
            }

            /* Inject voice_data into the user message context */
            std::string context_str = user_message;
            if (body.contains("voice_data") && body["voice_data"].is_object()) {
                context_str += "\n\nCurrent voice metrics:\n" + body["voice_data"].dump(2);
            }

            json messages = json::array();
            messages.push_back({{"role", "system"}, {"content", ai_prompts::COACHING_SYSTEM}});
            messages.push_back({{"role", "user"}, {"content", context_str}});

            auto t0 = std::chrono::steady_clock::now();
            json result = g_sub.ollama->chat(messages);
            auto t1 = std::chrono::steady_clock::now();
            int latency_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

            /* Log to DB */
            if (g_sub.db && !result.contains("error")) {
                DbAiInteraction ai;
                ai.user_id       = 1;
                ai.context       = "coaching";
                ai.prompt_text   = context_str;
                ai.model_used    = g_sub.ollama->model();
                ai.latency_ms    = latency_ms;
                if (result.contains("message") && result["message"].contains("content"))
                    ai.response_text = result["message"]["content"].get<std::string>();
                g_sub.db->log_ai_interaction(ai);
            }

            json out;
            if (result.contains("error")) {
                out["ok"]    = false;
                out["error"] = result["error"];
            } else {
                out["ok"]         = true;
                out["response"]   = result.contains("message") ? result["message"]["content"] : "";
                out["model"]      = g_sub.ollama->model();
                out["latency_ms"] = latency_ms;
            }
            res.set_content(out.dump(2), "application/json");
        });
    });

    /* ── AI suggest-eq — EQ settings from voice analysis ────────────── */
    svr.Post("/api/v1/ai/suggest-eq", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            if (!g_sub.ollama) {
                res.set_content(R"({"error":"Ollama client not initialized","available":false})",
                                "application/json");
                return;
            }
            if (!g_sub.ollama->is_available()) {
                res.set_content(R"({"error":"Ollama not available. Start with: ollama serve","available":false})",
                                "application/json");
                return;
            }

            json body;
            try { body = json::parse(req.body); } catch (...) {
                res.status = 400;
                res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                return;
            }

            /* Build user prompt from voice data */
            std::string user_prompt = "Voice analysis data:\n" + body.dump(2);

            json messages = json::array();
            messages.push_back({{"role", "system"}, {"content", ai_prompts::EQ_SUGGEST_SYSTEM}});
            messages.push_back({{"role", "user"}, {"content", user_prompt}});

            auto t0 = std::chrono::steady_clock::now();
            json result = g_sub.ollama->chat(messages);
            auto t1 = std::chrono::steady_clock::now();
            int latency_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

            if (g_sub.db && !result.contains("error")) {
                DbAiInteraction ai;
                ai.user_id       = 1;
                ai.context       = "eq_suggest";
                ai.prompt_text   = user_prompt;
                ai.model_used    = g_sub.ollama->model();
                ai.latency_ms    = latency_ms;
                if (result.contains("message") && result["message"].contains("content"))
                    ai.response_text = result["message"]["content"].get<std::string>();
                g_sub.db->log_ai_interaction(ai);
            }

            json out;
            if (result.contains("error")) {
                out["ok"]    = false;
                out["error"] = result["error"];
            } else {
                out["ok"]         = true;
                out["response"]   = result.contains("message") ? result["message"]["content"] : "";
                out["model"]      = g_sub.ollama->model();
                out["latency_ms"] = latency_ms;
            }
            res.set_content(out.dump(2), "application/json");
        });
    });

    /* ── AI suggest-chain — effects chain recommendation ────────────── */
    svr.Post("/api/v1/ai/suggest-chain", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            if (!g_sub.ollama) {
                res.set_content(R"({"error":"Ollama client not initialized","available":false})",
                                "application/json");
                return;
            }
            if (!g_sub.ollama->is_available()) {
                res.set_content(R"({"error":"Ollama not available. Start with: ollama serve","available":false})",
                                "application/json");
                return;
            }

            json body;
            try { body = json::parse(req.body); } catch (...) {
                res.status = 400;
                res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                return;
            }

            std::string user_prompt = "Voice/use-case profile:\n" + body.dump(2);

            json messages = json::array();
            messages.push_back({{"role", "system"}, {"content", ai_prompts::CHAIN_SUGGEST_SYSTEM}});
            messages.push_back({{"role", "user"}, {"content", user_prompt}});

            auto t0 = std::chrono::steady_clock::now();
            json result = g_sub.ollama->chat(messages);
            auto t1 = std::chrono::steady_clock::now();
            int latency_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

            if (g_sub.db && !result.contains("error")) {
                DbAiInteraction ai;
                ai.user_id       = 1;
                ai.context       = "chain_suggest";
                ai.prompt_text   = user_prompt;
                ai.model_used    = g_sub.ollama->model();
                ai.latency_ms    = latency_ms;
                if (result.contains("message") && result["message"].contains("content"))
                    ai.response_text = result["message"]["content"].get<std::string>();
                g_sub.db->log_ai_interaction(ai);
            }

            json out;
            if (result.contains("error")) {
                out["ok"]    = false;
                out["error"] = result["error"];
            } else {
                out["ok"]         = true;
                out["response"]   = result.contains("message") ? result["message"]["content"] : "";
                out["model"]      = g_sub.ollama->model();
                out["latency_ms"] = latency_ms;
            }
            res.set_content(out.dump(2), "application/json");
        });
    });

    /* ── AI history — query past AI interactions from DB ─────────────── */
    svr.Get("/api/v1/ai/history", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            if (!g_sub.db) {
                res.set_content(R"({"error":"Database not available"})", "application/json");
                return;
            }

            std::string context;
            int limit = 20;
            if (req.has_param("context"))
                context = req.get_param_value("context");
            if (req.has_param("limit")) {
                try { limit = std::stoi(req.get_param_value("limit")); } catch (...) {}
                if (limit < 1) limit = 1;
                if (limit > 100) limit = 100;
            }

            auto history = g_sub.db->get_ai_history(1, context, limit);
            json arr = json::array();
            for (const auto& h : history) {
                json item;
                item["id"]            = h.id;
                item["user_id"]       = h.user_id;
                item["context"]       = h.context;
                item["prompt_text"]   = h.prompt_text;
                item["response_text"] = h.response_text;
                item["model_used"]    = h.model_used;
                item["latency_ms"]    = h.latency_ms;
                arr.push_back(item);
            }

            json out;
            out["ok"]      = true;
            out["history"] = arr;
            out["count"]   = (int)arr.size();
            res.set_content(out.dump(2), "application/json");
        });
    });

    /* ══════════════════════════════════════════════════════════════════════
     * Phase AI-2: Advanced AI Coaching Endpoints
     * ══════════════════════════════════════════════════════════════════════ */

    /* Helper: build a metrics context string from AnalysisState */
    auto build_metrics_context = []() -> std::string {
        if (!g_sub.analysis) return "(no analysis data available)";
        std::string ctx;
        ctx += "- RMS: " + std::to_string(g_sub.analysis->rms_db()) + " dB\n";
        ctx += "- Peak: " + std::to_string(g_sub.analysis->peak_db()) + " dB\n";
        ctx += "- LUFS: " + std::to_string(g_sub.analysis->lufs()) + " (target: -16)\n";
        ctx += "- Pitch: " + std::to_string(g_sub.analysis->pitch_hz()) + " Hz";
        std::string note = g_sub.analysis->note_name();
        if (!note.empty())
            ctx += " (" + note + ", " + std::to_string(g_sub.analysis->cents_off()) + " cents off)";
        ctx += "\n";
        ctx += "- Spectral centroid: " + std::to_string(g_sub.analysis->spectral_centroid()) + " Hz\n";
        ctx += "- Peak frequency: " + std::to_string(g_sub.analysis->peak_frequency()) + " Hz\n";
        ctx += "- Pitch confidence: " + std::to_string(g_sub.analysis->pitch_confidence()) + "\n";
        return ctx;
    };

    /* Helper: classify voice type from fundamental frequency */
    auto classify_voice_type = [](float fundamental_hz) -> std::string {
        if (fundamental_hz <= 0.0f) return "unknown";
        if (fundamental_hz < 130.0f) return "bass";
        if (fundamental_hz < 185.0f) return "baritone";
        if (fundamental_hz < 265.0f) return "tenor";
        if (fundamental_hz < 375.0f) return "alto";
        return "soprano";
    };

    /* Helper: get coaching tips as comma-separated string */
    auto tips_string = []() -> std::string {
        if (!g_sub.analysis) return "";
        auto tips = g_sub.analysis->tips();
        std::string result;
        for (size_t i = 0; i < tips.size(); ++i) {
            if (i > 0) result += ", ";
            result += tips[i].message;
        }
        return result;
    };

    /* ── AI analyze — deep voice analysis with live metrics ────────────── */
    svr.Post("/api/v1/voictune/ai/analyze", [build_metrics_context, classify_voice_type, tips_string](
        const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            if (!g_sub.ollama) {
                res.set_content(R"({"error":"Ollama client not initialized","available":false})",
                                "application/json");
                return;
            }
            if (!g_sub.ollama->is_available()) {
                res.set_content(R"({"error":"Ollama not available. Start with: ollama serve","available":false})",
                                "application/json");
                return;
            }

            /* Gather all available data */
            std::string metrics_ctx = build_metrics_context();
            std::string active_tips = tips_string();
            float fundamental = g_sub.analysis ? g_sub.analysis->pitch_hz() : 0.0f;
            std::string voice_type = classify_voice_type(fundamental);

            /* Check for voice profile in DB */
            std::string profile_info;
            if (g_sub.db && g_sub.db->is_connected()) {
                auto prof = g_sub.db->get_profile(1, "Default");
                if (prof.id > 0) {
                    profile_info = "Voice profile: " + prof.profile_name +
                                   " (type: " + prof.voice_type +
                                   ", fundamental: " + std::to_string(prof.fundamental_hz) + " Hz" +
                                   ", avg LUFS: " + std::to_string(prof.avg_lufs) + ")";
                }
            }

            /* Build the detailed analysis prompt */
            std::string user_prompt = "Perform a deep voice analysis based on the following live data.\n\n";
            user_prompt += "Current voice metrics:\n" + metrics_ctx + "\n";
            if (!active_tips.empty())
                user_prompt += "Active coaching issues: " + active_tips + "\n\n";
            if (!profile_info.empty())
                user_prompt += profile_info + "\n\n";
            user_prompt += "Voice classification: " + voice_type + "\n\n";
            user_prompt += "Please provide:\n"
                           "1. A detailed analysis of the voice characteristics\n"
                           "2. Specific suggestions for improvement (as a JSON array of strings)\n"
                           "3. A voice classification summary\n"
                           "Format your response as JSON: {\"analysis\": \"...\", \"suggestions\": [...], \"voice_classification\": \"...\"}";

            json messages = json::array();
            messages.push_back({{"role", "system"}, {"content", ai_prompts::COACHING_SYSTEM}});
            messages.push_back({{"role", "user"}, {"content", user_prompt}});

            auto t0 = std::chrono::steady_clock::now();
            json result = g_sub.ollama->chat(messages);
            auto t1 = std::chrono::steady_clock::now();
            int latency_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

            /* Log to DB */
            if (g_sub.db && !result.contains("error")) {
                DbAiInteraction ai;
                ai.user_id       = 1;
                ai.context       = "coaching";
                ai.prompt_text   = user_prompt;
                ai.model_used    = g_sub.ollama->model();
                ai.latency_ms    = latency_ms;
                if (result.contains("message") && result["message"].contains("content"))
                    ai.response_text = result["message"]["content"].get<std::string>();
                g_sub.db->log_ai_interaction(ai);
            }

            json out;
            if (result.contains("error")) {
                out["ok"]    = false;
                out["error"] = result["error"];
            } else {
                std::string response_text;
                if (result.contains("message") && result["message"].contains("content"))
                    response_text = result["message"]["content"].get<std::string>();

                out["ok"]         = true;
                out["latency_ms"] = latency_ms;
                out["voice_classification"] = voice_type;

                /* Try to parse structured JSON from AI response */
                try {
                    json parsed = json::parse(response_text);
                    out["analysis"]    = parsed.value("analysis", response_text);
                    out["suggestions"] = parsed.value("suggestions", json::array());
                    if (parsed.contains("voice_classification"))
                        out["voice_classification"] = parsed["voice_classification"];
                } catch (...) {
                    /* AI returned free text — wrap it */
                    out["analysis"]    = response_text;
                    out["suggestions"] = json::array();
                }
            }
            res.set_content(out.dump(2), "application/json");
        });
    });

    /* ── AI coach — interactive chat with conversation memory ─────────── */
    svr.Post("/api/v1/voictune/ai/coach", [build_metrics_context, classify_voice_type, tips_string](
        const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            if (!g_sub.ollama) {
                res.set_content(R"({"error":"Ollama client not initialized","available":false})",
                                "application/json");
                return;
            }
            if (!g_sub.ollama->is_available()) {
                res.set_content(R"({"error":"Ollama not available. Start with: ollama serve","available":false})",
                                "application/json");
                return;
            }

            json body;
            try { body = json::parse(req.body); } catch (...) {
                res.status = 400;
                res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                return;
            }

            std::string user_message = body.value("message", "");
            if (user_message.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"message field required"})", "application/json");
                return;
            }

            /* Build system prompt with injected live metrics */
            std::string metrics_ctx = build_metrics_context();
            float fundamental = g_sub.analysis ? g_sub.analysis->pitch_hz() : 0.0f;
            std::string voice_type = classify_voice_type(fundamental);
            std::string active_tips = tips_string();

            std::string system_prompt = std::string(ai_prompts::COACHING_SYSTEM) +
                "\n\nCurrent voice metrics:\n" + metrics_ctx +
                "- Voice type: " + voice_type + "\n";
            if (!active_tips.empty())
                system_prompt += "- Active issues: " + active_tips + "\n";

            /* Build messages array: system + conversation history + user message */
            json messages = json::array();
            messages.push_back({{"role", "system"}, {"content", system_prompt}});

            /* Load last 5 AI interactions for conversation context */
            if (g_sub.db && g_sub.db->is_connected()) {
                auto history = g_sub.db->get_ai_history(1, "coaching_chat", 5);
                /* History comes newest-first; reverse for chronological order */
                for (int i = (int)history.size() - 1; i >= 0; --i) {
                    const auto& h = history[i];
                    if (!h.prompt_text.empty())
                        messages.push_back({{"role", "user"}, {"content", h.prompt_text}});
                    if (!h.response_text.empty())
                        messages.push_back({{"role", "assistant"}, {"content", h.response_text}});
                }
            }

            messages.push_back({{"role", "user"}, {"content", user_message}});

            auto t0 = std::chrono::steady_clock::now();
            json result = g_sub.ollama->chat(messages);
            auto t1 = std::chrono::steady_clock::now();
            int latency_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

            /* Log to DB */
            if (g_sub.db && !result.contains("error")) {
                DbAiInteraction ai;
                ai.user_id       = 1;
                ai.context       = "coaching_chat";
                ai.prompt_text   = user_message;
                ai.model_used    = g_sub.ollama->model();
                ai.latency_ms    = latency_ms;
                if (result.contains("message") && result["message"].contains("content"))
                    ai.response_text = result["message"]["content"].get<std::string>();
                g_sub.db->log_ai_interaction(ai);
            }

            json out;
            if (result.contains("error")) {
                out["ok"]    = false;
                out["error"] = result["error"];
            } else {
                out["ok"]         = true;
                out["response"]   = result.contains("message") && result["message"].contains("content")
                                    ? result["message"]["content"].get<std::string>() : "";
                out["model"]      = g_sub.ollama->model();
                out["latency_ms"] = latency_ms;
            }
            res.set_content(out.dump(2), "application/json");
        });
    });

    /* ── AI suggest-eq — smart EQ with live analysis data ─────────────── */
    svr.Post("/api/v1/voictune/ai/suggest-eq", [build_metrics_context, classify_voice_type](
        const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            if (!g_sub.ollama) {
                res.set_content(R"({"error":"Ollama client not initialized","available":false})",
                                "application/json");
                return;
            }
            if (!g_sub.ollama->is_available()) {
                res.set_content(R"({"error":"Ollama not available. Start with: ollama serve","available":false})",
                                "application/json");
                return;
            }

            /* Build prompt from live analysis data + optional voice profile */
            std::string user_prompt = "Voice analysis snapshot:\n" + build_metrics_context();
            float fundamental = g_sub.analysis ? g_sub.analysis->pitch_hz() : 0.0f;
            user_prompt += "Voice type: " + classify_voice_type(fundamental) + "\n";

            /* Inject voice profile if available */
            if (g_sub.db && g_sub.db->is_connected()) {
                auto prof = g_sub.db->get_profile(1, "Default");
                if (prof.id > 0) {
                    user_prompt += "Calibrated fundamental: " + std::to_string(prof.fundamental_hz) + " Hz\n";
                    user_prompt += "Average LUFS: " + std::to_string(prof.avg_lufs) + "\n";
                    user_prompt += "Average RMS: " + std::to_string(prof.avg_rms_db) + " dB\n";
                }
            }

            user_prompt += "\nPlease suggest a 10-band parametric EQ preset optimized for this voice.";

            json messages = json::array();
            messages.push_back({{"role", "system"}, {"content", ai_prompts::EQ_SUGGEST_SYSTEM}});
            messages.push_back({{"role", "user"}, {"content", user_prompt}});

            auto t0 = std::chrono::steady_clock::now();
            json result = g_sub.ollama->chat(messages);
            auto t1 = std::chrono::steady_clock::now();
            int latency_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

            /* Log to DB */
            if (g_sub.db && !result.contains("error")) {
                DbAiInteraction ai;
                ai.user_id       = 1;
                ai.context       = "eq_suggest";
                ai.prompt_text   = user_prompt;
                ai.model_used    = g_sub.ollama->model();
                ai.latency_ms    = latency_ms;
                if (result.contains("message") && result["message"].contains("content"))
                    ai.response_text = result["message"]["content"].get<std::string>();
                g_sub.db->log_ai_interaction(ai);
            }

            json out;
            if (result.contains("error")) {
                out["ok"]    = false;
                out["error"] = result["error"];
            } else {
                std::string response_text;
                if (result.contains("message") && result["message"].contains("content"))
                    response_text = result["message"]["content"].get<std::string>();

                out["ok"]         = true;
                out["latency_ms"] = latency_ms;

                /* Try to parse structured EQ JSON from AI response */
                try {
                    json parsed = json::parse(response_text);
                    out["bands"]     = parsed.value("bands", json::array());
                    out["rationale"] = parsed.value("rationale", "");
                } catch (...) {
                    /* AI returned free text — wrap it */
                    out["bands"]     = json::array();
                    out["rationale"] = response_text;
                }
            }
            res.set_content(out.dump(2), "application/json");
        });
    });

    /* ── POST /api/v1/ai/content/analyze — content analysis for show notes */
    svr.Post("/api/v1/ai/content/analyze", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            if (!g_sub.ollama) {
                res.set_content(R"({"error":"Ollama client not initialized","available":false})",
                                "application/json");
                return;
            }
            if (!g_sub.ollama->is_available()) {
                res.set_content(R"({"error":"Ollama not available. Start with: ollama serve","available":false})",
                                "application/json");
                return;
            }

            json body;
            try { body = json::parse(req.body); } catch (...) {
                res.status = 400;
                res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                return;
            }

            std::string transcript = body.value("transcript", "");
            int session_id = body.value("session_id", 0);

            /* Build user prompt from transcript or DB session data */
            std::string user_prompt;
            if (!transcript.empty()) {
                user_prompt = "Analyze this broadcast/podcast transcript:\n\n" + transcript;
            } else if (session_id > 0 && g_sub.db && g_sub.db->is_connected()) {
                /* Pull analysis snapshots from DB for the session */
                auto snapshots = g_sub.db->get_snapshots(session_id);

                if (snapshots.empty()) {
                    res.status = 404;
                    res.set_content(R"({"error":"No analysis data found for session"})", "application/json");
                    return;
                }

                /* Compute speech stats from snapshots */
                float total_rms = 0, total_pitch = 0, total_lufs = 0;
                int speech_count = 0, silence_count = 0;
                float min_pitch = 9999, max_pitch = 0;
                for (auto& snap : snapshots) {
                    if (snap.rms_db > -60.0f) {
                        speech_count++;
                        total_rms += snap.rms_db;
                        total_lufs += snap.lufs;
                        if (snap.pitch_hz > 50.0f) {
                            total_pitch += snap.pitch_hz;
                            if (snap.pitch_hz < min_pitch) min_pitch = snap.pitch_hz;
                            if (snap.pitch_hz > max_pitch) max_pitch = snap.pitch_hz;
                        }
                    } else {
                        silence_count++;
                    }
                }

                int total = (int)snapshots.size();
                float avg_rms = speech_count > 0 ? total_rms / speech_count : -96.0f;
                float avg_pitch = speech_count > 0 ? total_pitch / speech_count : 0.0f;
                float avg_lufs = speech_count > 0 ? total_lufs / speech_count : -96.0f;
                float silence_ratio = total > 0 ? (float)silence_count / total : 0.0f;
                float pitch_variance = max_pitch > 0 ? max_pitch - min_pitch : 0.0f;

                user_prompt = "Analyze this broadcast session (session_id=" + std::to_string(session_id) + ").\n\n";
                user_prompt += "Speech statistics from " + std::to_string(total) + " analysis snapshots:\n";
                user_prompt += "- Speech frames: " + std::to_string(speech_count) + "/" + std::to_string(total) + "\n";
                user_prompt += "- Silence ratio: " + std::to_string(silence_ratio * 100.0f) + "%\n";
                user_prompt += "- Average RMS: " + std::to_string(avg_rms) + " dB\n";
                user_prompt += "- Average LUFS: " + std::to_string(avg_lufs) + "\n";
                user_prompt += "- Average pitch: " + std::to_string(avg_pitch) + " Hz\n";
                user_prompt += "- Pitch range: " + std::to_string(min_pitch) + " - " + std::to_string(max_pitch) + " Hz\n";
                user_prompt += "- Pitch variance: " + std::to_string(pitch_variance) + " Hz\n";
                user_prompt += "\nProvide a content analysis summary based on these vocal characteristics.";
            } else {
                res.status = 400;
                res.set_content(R"({"error":"transcript or session_id required"})", "application/json");
                return;
            }

            json messages = json::array();
            messages.push_back({{"role", "system"}, {"content", ai_prompts::CONTENT_ANALYSIS_SYSTEM}});
            messages.push_back({{"role", "user"}, {"content", user_prompt}});

            auto t0 = std::chrono::steady_clock::now();
            json result = g_sub.ollama->chat(messages);
            auto t1 = std::chrono::steady_clock::now();
            int latency_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

            /* Log to DB */
            if (g_sub.db && !result.contains("error")) {
                DbAiInteraction ai;
                ai.user_id       = 1;
                ai.context       = "content";
                ai.prompt_text   = user_prompt.substr(0, 512);
                ai.model_used    = g_sub.ollama->model();
                ai.latency_ms    = latency_ms;
                if (result.contains("message") && result["message"].contains("content"))
                    ai.response_text = result["message"]["content"].get<std::string>();
                g_sub.db->log_ai_interaction(ai);
            }

            json out;
            if (result.contains("error")) {
                out["ok"]    = false;
                out["error"] = result["error"];
            } else {
                std::string response_text;
                if (result.contains("message") && result["message"].contains("content"))
                    response_text = result["message"]["content"].get<std::string>();

                out["ok"]         = true;
                out["latency_ms"] = latency_ms;

                /* Try to parse structured JSON from AI response */
                json parsed;
                bool parsed_ok = false;
                try {
                    parsed = json::parse(response_text);
                    parsed_ok = true;
                } catch (...) {
                    auto j_start = response_text.find('{');
                    auto j_end   = response_text.rfind('}');
                    if (j_start != std::string::npos && j_end != std::string::npos && j_end > j_start) {
                        try {
                            parsed = json::parse(response_text.substr(j_start, j_end - j_start + 1));
                            parsed_ok = true;
                        } catch (...) {}
                    }
                }

                if (parsed_ok) {
                    out["summary"]          = parsed.value("summary", "");
                    out["topics"]           = parsed.value("topics", json::array());
                    out["tags"]             = parsed.value("tags", json::array());
                    out["title_suggestion"] = parsed.value("title_suggestion", "");
                    out["filler_words"]     = parsed.value("filler_words", json::object());
                    out["pace_analysis"]    = parsed.value("pace_analysis", json::object());
                } else {
                    out["summary"]          = response_text;
                    out["topics"]           = json::array();
                    out["tags"]             = json::array();
                    out["title_suggestion"] = "";
                    out["filler_words"]     = json::object();
                    out["pace_analysis"]    = json::object();
                }
            }
            res.set_content(out.dump(2), "application/json");
        });
    });

    /* ── Coaching summary — session broadcast quality score (VT-4) ────── */
    svr.Get("/api/v1/voictune/coaching/summary", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            json out;
            out["ok"] = true;

            if (!g_sub.coach || !g_sub.analysis) {
                res.status = 503;
                res.set_content(R"({"error":"Coaching subsystem not initialized"})", "application/json");
                return;
            }

            auto& coach = *g_sub.coach;
            auto counts = coach.tip_counts();
            int total_f = coach.total_frames();
            int speech_f = coach.speech_frames();

            /* Session duration: frames / ~10Hz analysis rate */
            float session_sec = (total_f > 0) ? (float)total_f / 10.0f : 0.0f;
            float speech_ratio = (total_f > 0) ? (float)speech_f / (float)total_f : 0.0f;
            float avg_lufs_val = coach.avg_lufs();
            float avg_rms = g_sub.analysis->rms_db();
            float avg_dr = coach.avg_dynamic_range();
            int est_wpm = coach.estimated_wpm();

            out["session_duration_sec"] = (int)session_sec;
            out["avg_lufs"] = avg_lufs_val;
            out["avg_rms_db"] = avg_rms;
            out["dynamic_range_db"] = avg_dr;
            out["speech_ratio"] = speech_ratio;
            out["estimated_wpm"] = est_wpm;

            json tc;
            tc["level"]         = counts.level;
            tc["pitch"]         = counts.pitch;
            tc["sibilance"]     = counts.sibilance;
            tc["plosive"]       = counts.plosive;
            tc["room_noise"]    = counts.room_noise;
            tc["dynamic_range"] = counts.dynamic_range;
            tc["wpm"]           = counts.wpm;
            tc["proximity"]     = counts.proximity;
            tc["pacing"]        = counts.pacing;
            out["tip_counts"] = tc;

            /* Calculate overall broadcast quality score (0-100) */
            int score = 0;

            /* LUFS within target range: 30 points
             * Full 30 if within +/-3 dB of -16 LUFS, scaled down beyond that */
            {
                float target = -16.0f;
                float diff = std::abs(avg_lufs_val - target);
                if (diff <= 3.0f) {
                    score += 30;
                } else if (diff <= 9.0f) {
                    score += (int)(30.0f * (1.0f - (diff - 3.0f) / 6.0f));
                }
                /* else 0 points */
            }

            /* No clipping: 20 points
             * Full 20 if no level warnings with severity WARNING/CRITICAL about clipping */
            {
                float peak_hold = g_sub.analysis->peak_hold_db();
                if (peak_hold < -1.0f) {
                    score += 20;
                } else if (peak_hold < 0.0f) {
                    score += 10;
                }
                /* else clipping occurred, 0 points */
            }

            /* Good dynamic range (6-20 dB): 15 points */
            {
                if (avg_dr >= 6.0f && avg_dr <= 20.0f) {
                    score += 15;
                } else if (avg_dr > 3.0f && avg_dr < 6.0f) {
                    score += (int)(15.0f * (avg_dr - 3.0f) / 3.0f);
                } else if (avg_dr > 20.0f && avg_dr < 30.0f) {
                    score += (int)(15.0f * (1.0f - (avg_dr - 20.0f) / 10.0f));
                }
            }

            /* No plosives: 10 points */
            {
                if (counts.plosive == 0) {
                    score += 10;
                } else if (counts.plosive <= 2) {
                    score += 5;
                }
            }

            /* No sibilance: 10 points */
            {
                if (counts.sibilance == 0) {
                    score += 10;
                } else if (counts.sibilance <= 2) {
                    score += 5;
                }
            }

            /* Good pacing (140-170 WPM): 15 points */
            {
                if (est_wpm >= 140 && est_wpm <= 170) {
                    score += 15;
                } else if (est_wpm > 0) {
                    int wpm_diff = 0;
                    if (est_wpm < 140) wpm_diff = 140 - est_wpm;
                    else wpm_diff = est_wpm - 170;
                    int wpm_score = std::max(0, 15 - wpm_diff / 2);
                    score += wpm_score;
                }
                /* est_wpm == 0 means insufficient data, give 0 */
            }

            score = std::clamp(score, 0, 100);
            out["overall_score"] = score;

            res.set_content(out.dump(2), "application/json");
        });
    });

    /* ── Coaching profile — voice type classification ─────────────────── */
    svr.Get("/api/v1/voictune/coaching/profile", [classify_voice_type](
        const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            json out;
            out["ok"] = true;

            float fundamental = g_sub.analysis ? g_sub.analysis->pitch_hz() : 0.0f;
            float lufs_val    = g_sub.analysis ? g_sub.analysis->lufs() : -96.0f;
            float rms_val     = g_sub.analysis ? g_sub.analysis->rms_db() : -96.0f;
            float peak_val    = g_sub.analysis ? g_sub.analysis->peak_db() : -96.0f;
            float dynamic_range = peak_val - rms_val;

            out["fundamental_hz"]   = fundamental;
            out["voice_type"]       = classify_voice_type(fundamental);
            out["avg_lufs"]         = lufs_val;
            out["avg_rms_db"]       = rms_val;
            out["dynamic_range_db"] = dynamic_range;

            /* Try to load saved profile from DB */
            std::string profile_name = "Default";
            if (g_sub.db && g_sub.db->is_connected()) {
                auto prof = g_sub.db->get_profile(1, "Default");
                if (prof.id > 0) {
                    profile_name = prof.profile_name;
                    /* Return saved values alongside live values */
                    out["saved_fundamental_hz"] = prof.fundamental_hz;
                    out["saved_voice_type"]     = prof.voice_type;
                    out["saved_avg_lufs"]       = prof.avg_lufs;
                    out["saved_avg_rms_db"]     = prof.avg_rms_db;
                } else {
                    /* Create default profile */
                    int pid = g_sub.db->create_profile(1, "Default");
                    if (pid > 0) {
                        DbVoiceProfile p;
                        p.id             = pid;
                        p.user_id        = 1;
                        p.profile_name   = "Default";
                        p.fundamental_hz = fundamental;
                        p.voice_type     = classify_voice_type(fundamental);
                        p.avg_lufs       = lufs_val;
                        p.avg_rms_db     = rms_val;
                        g_sub.db->update_profile(p);
                    }
                }
            }
            out["profile_name"] = profile_name;

            res.set_content(out.dump(2), "application/json");
        });
    });

    /* ── Coaching calibrate — snapshot current metrics into voice profile ── */
    svr.Post("/api/v1/voictune/coaching/calibrate", [classify_voice_type](
        const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            if (!g_sub.analysis) {
                res.status = 503;
                res.set_content(R"({"error":"Analysis state not initialized"})", "application/json");
                return;
            }

            json body;
            try { body = json::parse(req.body); } catch (...) {
                body = json::object();
            }
            std::string profile_name = body.value("profile_name", "Default");

            /* Read current analysis state (represents recent running averages) */
            float fundamental = g_sub.analysis->pitch_hz();
            float lufs_val    = g_sub.analysis->lufs();
            float rms_val     = g_sub.analysis->rms_db();
            float centroid    = g_sub.analysis->spectral_centroid();
            std::string voice_type = classify_voice_type(fundamental);

            json profile_json;
            profile_json["ok"]             = true;
            profile_json["voice_type"]     = voice_type;
            profile_json["fundamental_hz"] = fundamental;
            profile_json["avg_lufs"]       = lufs_val;
            profile_json["avg_rms_db"]     = rms_val;
            profile_json["spectral_centroid_hz"] = centroid;
            profile_json["profile_name"]   = profile_name;

            /* Save to DB */
            if (g_sub.db && g_sub.db->is_connected()) {
                auto existing = g_sub.db->get_profile(1, profile_name);
                DbVoiceProfile p;
                if (existing.id > 0) {
                    p.id = existing.id;
                } else {
                    p.id = g_sub.db->create_profile(1, profile_name);
                }
                p.user_id        = 1;
                p.profile_name   = profile_name;
                p.fundamental_hz = fundamental;
                p.voice_type     = voice_type;
                p.avg_lufs       = lufs_val;
                p.avg_rms_db     = rms_val;
                p.analysis_json  = json({
                    {"spectral_centroid_hz", centroid},
                    {"peak_frequency_hz", g_sub.analysis->peak_frequency()},
                    {"pitch_confidence", g_sub.analysis->pitch_confidence()},
                    {"calibrated_at", std::time(nullptr)}
                }).dump();
                g_sub.db->update_profile(p);

                profile_json["profile_id"] = p.id;
                profile_json["saved"]      = true;
            } else {
                profile_json["saved"]   = false;
                profile_json["message"] = "Database not available — profile not persisted";
            }

            VT_INFO("Voice calibration: " + profile_name + " type=" + voice_type +
                     " fundamental=" + std::to_string(fundamental) + "Hz" +
                     " LUFS=" + std::to_string(lufs_val));

            json out;
            out["ok"]      = true;
            out["profile"] = profile_json;
            res.set_content(out.dump(2), "application/json");
        });
    });

    /* ══════════════════════════════════════════════════════════════════════════
     * Podcast AI tools (Phase PC-6)
     * ══════════════════════════════════════════════════════════════════════════ */

    /* ── POST /api/v1/ai/podcast/show-notes — generate show notes from transcript ── */
    svr.Post("/api/v1/ai/podcast/show-notes", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            if (!g_sub.ollama) {
                res.set_content(R"({"error":"Ollama client not initialized","available":false})",
                                "application/json");
                return;
            }
            if (!g_sub.ollama->is_available()) {
                res.set_content(R"({"error":"Ollama not available. Start with: ollama serve","available":false})",
                                "application/json");
                return;
            }

            json body;
            try { body = json::parse(req.body); } catch (...) {
                res.status = 400;
                res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                return;
            }

            std::string transcript = body.value("transcript", "");
            std::string episode_title = body.value("episode_title", "");
            if (transcript.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"transcript field required"})", "application/json");
                return;
            }

            std::string user_prompt = "Generate show notes for this podcast episode";
            if (!episode_title.empty())
                user_prompt += " titled \"" + episode_title + "\"";
            user_prompt += ":\n\n" + transcript;

            json messages = json::array();
            messages.push_back({{"role", "system"}, {"content", ai_prompts::SHOW_NOTES_SYSTEM}});
            messages.push_back({{"role", "user"}, {"content", user_prompt}});

            auto t0 = std::chrono::steady_clock::now();
            json result = g_sub.ollama->chat(messages);
            auto t1 = std::chrono::steady_clock::now();
            int latency_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

            /* Log to DB */
            if (g_sub.db && !result.contains("error")) {
                DbAiInteraction ai;
                ai.user_id       = 1;
                ai.context       = "podcast_show_notes";
                ai.prompt_text   = user_prompt.substr(0, 512);
                ai.model_used    = g_sub.ollama->model();
                ai.latency_ms    = latency_ms;
                if (result.contains("message") && result["message"].contains("content"))
                    ai.response_text = result["message"]["content"].get<std::string>();
                g_sub.db->log_ai_interaction(ai);
            }

            json out;
            if (result.contains("error")) {
                out["ok"]    = false;
                out["error"] = result["error"];
            } else {
                std::string response_text;
                if (result.contains("message") && result["message"].contains("content"))
                    response_text = result["message"]["content"].get<std::string>();
                out["ok"]         = true;
                out["show_notes"] = response_text;
                out["latency_ms"] = latency_ms;
            }
            res.set_content(out.dump(2), "application/json");
        });
    });

    /* ── POST /api/v1/ai/podcast/suggest-chapters — suggest chapters from transcript ── */
    svr.Post("/api/v1/ai/podcast/suggest-chapters", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            if (!g_sub.ollama) {
                res.set_content(R"({"error":"Ollama client not initialized","available":false})",
                                "application/json");
                return;
            }
            if (!g_sub.ollama->is_available()) {
                res.set_content(R"({"error":"Ollama not available. Start with: ollama serve","available":false})",
                                "application/json");
                return;
            }

            json body;
            try { body = json::parse(req.body); } catch (...) {
                res.status = 400;
                res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                return;
            }

            std::string transcript = body.value("transcript", "");
            if (transcript.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"transcript field required"})", "application/json");
                return;
            }

            std::string user_prompt = "Suggest chapter markers for this podcast transcript:\n\n" + transcript;

            json messages = json::array();
            messages.push_back({{"role", "system"}, {"content", ai_prompts::CHAPTER_SUGGEST_SYSTEM}});
            messages.push_back({{"role", "user"}, {"content", user_prompt}});

            auto t0 = std::chrono::steady_clock::now();
            json result = g_sub.ollama->chat(messages);
            auto t1 = std::chrono::steady_clock::now();
            int latency_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

            /* Log to DB */
            if (g_sub.db && !result.contains("error")) {
                DbAiInteraction ai;
                ai.user_id       = 1;
                ai.context       = "podcast_chapters";
                ai.prompt_text   = user_prompt.substr(0, 512);
                ai.model_used    = g_sub.ollama->model();
                ai.latency_ms    = latency_ms;
                if (result.contains("message") && result["message"].contains("content"))
                    ai.response_text = result["message"]["content"].get<std::string>();
                g_sub.db->log_ai_interaction(ai);
            }

            json out;
            if (result.contains("error")) {
                out["ok"]    = false;
                out["error"] = result["error"];
            } else {
                std::string response_text;
                if (result.contains("message") && result["message"].contains("content"))
                    response_text = result["message"]["content"].get<std::string>();

                out["ok"]         = true;
                out["latency_ms"] = latency_ms;

                /* Try to parse structured JSON array from AI response */
                json chapters = json::array();
                bool parsed_ok = false;
                try {
                    chapters = json::parse(response_text);
                    if (chapters.is_array()) parsed_ok = true;
                } catch (...) {
                    /* Try extracting JSON array from surrounding text */
                    auto j_start = response_text.find('[');
                    auto j_end   = response_text.rfind(']');
                    if (j_start != std::string::npos && j_end != std::string::npos && j_end > j_start) {
                        try {
                            chapters = json::parse(response_text.substr(j_start, j_end - j_start + 1));
                            if (chapters.is_array()) parsed_ok = true;
                        } catch (...) {}
                    }
                }

                if (parsed_ok) {
                    out["chapters"] = chapters;
                } else {
                    out["chapters"]     = json::array();
                    out["raw_response"] = response_text;
                }
            }
            res.set_content(out.dump(2), "application/json");
        });
    });

    /* ── POST /api/v1/ai/podcast/extract-clips — identify best moments for social clips ── */
    svr.Post("/api/v1/ai/podcast/extract-clips", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            if (!g_sub.ollama) {
                res.set_content(R"({"error":"Ollama client not initialized","available":false})",
                                "application/json");
                return;
            }
            if (!g_sub.ollama->is_available()) {
                res.set_content(R"({"error":"Ollama not available. Start with: ollama serve","available":false})",
                                "application/json");
                return;
            }

            json body;
            try { body = json::parse(req.body); } catch (...) {
                res.status = 400;
                res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                return;
            }

            std::string transcript = body.value("transcript", "");
            if (transcript.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"transcript field required"})", "application/json");
                return;
            }

            std::string user_prompt = "Identify the best moments for social media clips from this podcast transcript:\n\n" + transcript;

            json messages = json::array();
            messages.push_back({{"role", "system"}, {"content", ai_prompts::CLIP_EXTRACT_SYSTEM}});
            messages.push_back({{"role", "user"}, {"content", user_prompt}});

            auto t0 = std::chrono::steady_clock::now();
            json result = g_sub.ollama->chat(messages);
            auto t1 = std::chrono::steady_clock::now();
            int latency_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

            /* Log to DB */
            if (g_sub.db && !result.contains("error")) {
                DbAiInteraction ai;
                ai.user_id       = 1;
                ai.context       = "podcast_clips";
                ai.prompt_text   = user_prompt.substr(0, 512);
                ai.model_used    = g_sub.ollama->model();
                ai.latency_ms    = latency_ms;
                if (result.contains("message") && result["message"].contains("content"))
                    ai.response_text = result["message"]["content"].get<std::string>();
                g_sub.db->log_ai_interaction(ai);
            }

            json out;
            if (result.contains("error")) {
                out["ok"]    = false;
                out["error"] = result["error"];
            } else {
                std::string response_text;
                if (result.contains("message") && result["message"].contains("content"))
                    response_text = result["message"]["content"].get<std::string>();

                out["ok"]         = true;
                out["latency_ms"] = latency_ms;

                /* Try to parse structured JSON array from AI response */
                json clips = json::array();
                bool parsed_ok = false;
                try {
                    clips = json::parse(response_text);
                    if (clips.is_array()) parsed_ok = true;
                } catch (...) {
                    auto j_start = response_text.find('[');
                    auto j_end   = response_text.rfind(']');
                    if (j_start != std::string::npos && j_end != std::string::npos && j_end > j_start) {
                        try {
                            clips = json::parse(response_text.substr(j_start, j_end - j_start + 1));
                            if (clips.is_array()) parsed_ok = true;
                        } catch (...) {}
                    }
                }

                if (parsed_ok) {
                    out["clips"] = clips;
                } else {
                    out["clips"]        = json::array();
                    out["raw_response"] = response_text;
                }
            }
            res.set_content(out.dump(2), "application/json");
        });
    });

    /* ── POST /api/v1/ai/podcast/seo-optimize — optimize episode metadata for SEO ── */
    svr.Post("/api/v1/ai/podcast/seo-optimize", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            if (!g_sub.ollama) {
                res.set_content(R"({"error":"Ollama client not initialized","available":false})",
                                "application/json");
                return;
            }
            if (!g_sub.ollama->is_available()) {
                res.set_content(R"({"error":"Ollama not available. Start with: ollama serve","available":false})",
                                "application/json");
                return;
            }

            json body;
            try { body = json::parse(req.body); } catch (...) {
                res.status = 400;
                res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                return;
            }

            std::string title       = body.value("title", "");
            std::string description = body.value("description", "");
            if (title.empty() && description.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"title or description required"})", "application/json");
                return;
            }

            std::string user_prompt = "Optimize this podcast episode for SEO:\n";
            if (!title.empty())       user_prompt += "Title: " + title + "\n";
            if (!description.empty()) user_prompt += "Description: " + description + "\n";

            json messages = json::array();
            messages.push_back({{"role", "system"}, {"content", ai_prompts::SEO_OPTIMIZE_SYSTEM}});
            messages.push_back({{"role", "user"}, {"content", user_prompt}});

            auto t0 = std::chrono::steady_clock::now();
            json result = g_sub.ollama->chat(messages);
            auto t1 = std::chrono::steady_clock::now();
            int latency_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

            /* Log to DB */
            if (g_sub.db && !result.contains("error")) {
                DbAiInteraction ai;
                ai.user_id       = 1;
                ai.context       = "podcast_seo";
                ai.prompt_text   = user_prompt.substr(0, 512);
                ai.model_used    = g_sub.ollama->model();
                ai.latency_ms    = latency_ms;
                if (result.contains("message") && result["message"].contains("content"))
                    ai.response_text = result["message"]["content"].get<std::string>();
                g_sub.db->log_ai_interaction(ai);
            }

            json out;
            if (result.contains("error")) {
                out["ok"]    = false;
                out["error"] = result["error"];
            } else {
                std::string response_text;
                if (result.contains("message") && result["message"].contains("content"))
                    response_text = result["message"]["content"].get<std::string>();

                out["ok"]         = true;
                out["latency_ms"] = latency_ms;

                /* Try to parse structured JSON from AI response */
                json parsed;
                bool parsed_ok = false;
                try {
                    parsed = json::parse(response_text);
                    parsed_ok = true;
                } catch (...) {
                    auto j_start = response_text.find('{');
                    auto j_end   = response_text.rfind('}');
                    if (j_start != std::string::npos && j_end != std::string::npos && j_end > j_start) {
                        try {
                            parsed = json::parse(response_text.substr(j_start, j_end - j_start + 1));
                            parsed_ok = true;
                        } catch (...) {}
                    }
                }

                if (parsed_ok) {
                    out["title"]          = parsed.value("title", "");
                    out["description"]    = parsed.value("description", "");
                    out["tags"]           = parsed.value("tags", json::array());
                    out["social_caption"] = parsed.value("social_caption", "");
                } else {
                    out["title"]          = "";
                    out["description"]    = response_text;
                    out["tags"]           = json::array();
                    out["social_caption"] = "";
                }
            }
            res.set_content(out.dump(2), "application/json");
        });
    });

    /* ── POST /api/v1/ai/podcast/transcribe — transcribe episode audio ── */
    svr.Post("/api/v1/ai/podcast/transcribe", [](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            json body;
            try { body = json::parse(req.body); } catch (...) {
                res.status = 400;
                res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                return;
            }

            std::string file_path = body.value("file_path", "");
            int episode_id = body.value("episode_id", 0);

            /* If episode_id provided but no file_path, look up the file from DB */
            if (file_path.empty() && episode_id > 0 && g_sub.db && g_sub.db->is_connected()) {
                /* We query the episode file path from the database */
                /* For now, we require file_path to be specified directly */
                res.status = 400;
                res.set_content("{\"error\":\"file_path required (episode_id lookup not yet implemented)\"}",
                                "application/json");
                return;
            }

            if (file_path.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"file_path or episode_id required"})", "application/json");
                return;
            }

            /* Security: reject path traversal and null bytes (SAST fix 2026-03-29) */
            if (file_path.find("..") != std::string::npos || file_path.find('\0') != std::string::npos) {
                res.status = 400;
                res.set_content(R"({"ok":false,"error":"Invalid file path"})", "application/json");
                return;
            }

            /* Check if the file exists */
            {
                FILE* f = fopen(file_path.c_str(), "r");
                if (!f) {
                    res.status = 404;
                    json err;
                    err["ok"]    = false;
                    err["error"] = "Audio file not found";
                    res.set_content(err.dump(), "application/json");
                    return;
                }
                fclose(f);
            }

            /* Check if whisper CLI is available */
            int whisper_check = system("which whisper > /dev/null 2>&1");
            if (whisper_check != 0) {
                json out;
                out["ok"]     = false;
                out["error"]  = "Whisper not installed. Install with: pip install openai-whisper";
                out["method"] = "unavailable";
                res.set_content(out.dump(2), "application/json");
                return;
            }

            /* Run whisper transcription */
            auto t0 = std::chrono::steady_clock::now();

            std::string tmp_dir = "/tmp/mc1vt_whisper_" + std::to_string(std::time(nullptr));

            /* Security: shell-escape file_path to prevent command injection (SAST fix 2026-03-29) */
            auto vt_shell_esc = [](const std::string& s) -> std::string {
                std::string out = "'";
                for (char c : s) {
                    if (c == '\'') out += "'\\''";
                    else           out += c;
                }
                out += "'";
                return out;
            };

            std::string cmd = "mkdir -p " + tmp_dir + " && whisper --model base --output_format txt --output_dir " +
                              tmp_dir + " " + vt_shell_esc(file_path) + " 2>&1";

            VT_INFO("Running Whisper transcription: " + file_path);
            FILE* pipe = popen(cmd.c_str(), "r");
            if (!pipe) {
                res.status = 500;
                res.set_content(R"({"ok":false,"error":"Failed to run whisper command"})", "application/json");
                return;
            }

            char buffer[4096];
            std::string whisper_output;
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                whisper_output += buffer;
            }
            int exit_code = pclose(pipe);

            auto t1 = std::chrono::steady_clock::now();
            int latency_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

            json out;
            if (exit_code != 0) {
                out["ok"]     = false;
                out["error"]  = "Whisper failed: " + whisper_output;
                out["method"] = "whisper";
            } else {
                /* Read the transcript output file */
                /* Whisper creates a .txt file with the same basename in the output dir */
                std::string basename = file_path.substr(file_path.find_last_of('/') + 1);
                auto dot_pos = basename.find_last_of('.');
                if (dot_pos != std::string::npos) basename = basename.substr(0, dot_pos);
                std::string txt_path = tmp_dir + "/" + basename + ".txt";

                std::string transcript;
                FILE* txt_file = fopen(txt_path.c_str(), "r");
                if (txt_file) {
                    char tbuf[4096];
                    while (fgets(tbuf, sizeof(tbuf), txt_file) != nullptr) {
                        transcript += tbuf;
                    }
                    fclose(txt_file);
                }

                out["ok"]         = true;
                out["transcript"] = transcript;
                out["method"]     = "whisper";
                out["latency_ms"] = latency_ms;

                VT_INFO("Whisper transcription complete: " + std::to_string(transcript.size()) + " chars in " +
                         std::to_string(latency_ms) + "ms");

                /* Log to DB */
                if (g_sub.db) {
                    DbAiInteraction ai;
                    ai.user_id       = 1;
                    ai.context       = "podcast_transcribe";
                    ai.prompt_text   = "whisper: " + file_path;
                    ai.model_used    = "whisper-base";
                    ai.latency_ms    = latency_ms;
                    ai.response_text = transcript.substr(0, 1024);
                    g_sub.db->log_ai_interaction(ai);
                }
            }

            /* Cleanup temp dir */
            std::string cleanup = "rm -rf " + tmp_dir;
            system(cleanup.c_str());

            res.set_content(out.dump(2), "application/json");
        });
    });

    /* ══════════════════════════════════════════════════════════════════════════
     * Advanced Voice Processing Endpoints (Phase AVT)
     * All processing via ffmpeg subprocess — not real-time DSP.
     * Original files are always preserved; output gets a suffix.
     * ══════════════════════════════════════════════════════════════════════════ */

    /* Helper: shell-escape a string (equivalent to PHP escapeshellarg) */
    auto shell_escape = [](const std::string& s) -> std::string {
        std::string out = "'";
        for (char c : s) {
            if (c == '\'') out += "'\\''";
            else           out += c;
        }
        out += "'";
        return out;
    };

    /* Helper: run an ffmpeg command and return {ok, output_path, stderr} */
    auto run_ffmpeg = [shell_escape](const std::string& input_path, const std::string& suffix,
                         const std::string& filter_chain, bool preview_only) -> json {
        json result;

        /* Verify input exists */
        {
            FILE* f = fopen(input_path.c_str(), "r");
            if (!f) {
                result["ok"]    = false;
                result["error"] = "Input file not found: " + input_path;
                return result;
            }
            fclose(f);
        }

        /* Build output path: /path/to/file_suffix.wav */
        std::string ext = ".wav";
        auto dot_pos = input_path.find_last_of('.');
        std::string base = (dot_pos != std::string::npos)
            ? input_path.substr(0, dot_pos) : input_path;
        std::string orig_ext = (dot_pos != std::string::npos)
            ? input_path.substr(dot_pos) : ".wav";
        std::string output_path = base + suffix + orig_ext;

        /* Build ffmpeg command with shell-escaped arguments */
        std::string cmd = "ffmpeg -y -i " + shell_escape(input_path);
        if (preview_only) {
            cmd += " -t 10";  /* First 10 seconds only */
            output_path = base + suffix + "_preview" + orig_ext;
        }
        cmd += " -af " + shell_escape(filter_chain);
        cmd += " " + shell_escape(output_path);
        cmd += " 2>&1";

        VT_INFO("ffmpeg voice process: " + cmd);

        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            result["ok"]    = false;
            result["error"] = "Failed to execute ffmpeg";
            return result;
        }

        char buffer[4096];
        std::string ffmpeg_output;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            ffmpeg_output += buffer;
        }
        int exit_code = pclose(pipe);

        if (exit_code != 0) {
            result["ok"]     = false;
            result["error"]  = "ffmpeg failed (exit " + std::to_string(exit_code) + "): " +
                               ffmpeg_output.substr(0, 512);
        } else {
            result["ok"]              = true;
            result["file_path_processed"] = output_path;
        }
        return result;
    };

    /* ── POST /api/v1/voictune/process/de-breath ─────────────────────── */
    svr.Post("/api/v1/voictune/process/de-breath",
        [run_ffmpeg](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            json body;
            try { body = json::parse(req.body); } catch (...) {
                res.status = 400;
                res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                return;
            }

            std::string file_path = body.value("file_path", "");
            if (file_path.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"file_path required"})", "application/json");
                return;
            }

            float threshold_db     = body.value("threshold_db", -30.0f);
            float max_reduction_db = body.value("max_reduction_db", -20.0f);
            int   min_breath_ms    = body.value("min_breath_ms", 100);
            bool  preview          = body.value("preview", false);

            /* Clamp parameters */
            threshold_db     = std::clamp(threshold_db, -60.0f, -10.0f);
            max_reduction_db = std::clamp(max_reduction_db, -40.0f, 0.0f);
            min_breath_ms    = std::clamp(min_breath_ms, 50, 800);

            /* De-breath algorithm via ffmpeg:
             * Use silencedetect to find quiet gaps, then apply volume reduction.
             * We use a compander filter to duck signals below the threshold. */
            float attack_s  = (float)min_breath_ms / 1000.0f;
            float release_s = attack_s * 0.5f;
            float ratio = std::abs(max_reduction_db) / 10.0f;
            if (ratio < 1.0f) ratio = 2.0f;

            /* Compander: attack|release, soft-knee points, gain, initial volume, delay */
            std::string filter = "compand=attacks=" + std::to_string(attack_s) +
                ":decays=" + std::to_string(release_s) +
                ":points=-90/-90|" + std::to_string(threshold_db) + "/" +
                std::to_string(threshold_db + max_reduction_db) +
                "|0/0:soft-knee=6:gain=0";

            auto t0 = std::chrono::steady_clock::now();
            json result = run_ffmpeg(file_path, "_debreath", filter, preview);
            auto t1 = std::chrono::steady_clock::now();
            int latency_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

            json out;
            out["ok"]         = result.value("ok", false);
            out["latency_ms"] = latency_ms;
            if (result.value("ok", false)) {
                out["file_path_processed"] = result["file_path_processed"];
                VT_INFO("De-breath complete: " + file_path + " -> " +
                         result["file_path_processed"].get<std::string>() +
                         " (" + std::to_string(latency_ms) + "ms)");
            } else {
                out["error"] = result.value("error", "Unknown error");
            }
            res.set_content(out.dump(2), "application/json");
        });
    });

    /* ── POST /api/v1/voictune/process/voice-change ──────────────────── */
    svr.Post("/api/v1/voictune/process/voice-change",
        [run_ffmpeg](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            json body;
            try { body = json::parse(req.body); } catch (...) {
                res.status = 400;
                res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                return;
            }

            std::string file_path = body.value("file_path", "");
            std::string effect    = body.value("effect", "");
            float intensity       = body.value("intensity", 0.5f);
            bool  preview         = body.value("preview", false);

            if (file_path.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"file_path required"})", "application/json");
                return;
            }
            if (effect.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"effect required: deeper|higher|robot|whisper|radio|telephone|chipmunk|darth_vader\"}", "application/json");
                return;
            }

            intensity = std::clamp(intensity, 0.0f, 1.0f);

            /* Build filter chain based on effect type */
            std::string filter;
            std::string suffix = "_fx_" + effect;

            if (effect == "deeper") {
                float rate_factor = 1.0f - (0.25f * intensity);  /* 0.75-1.0 */
                float tempo_fix   = 1.0f / rate_factor;
                filter = "asetrate=44100*" + std::to_string(rate_factor) +
                         ",aresample=44100,atempo=" + std::to_string(tempo_fix);
            } else if (effect == "higher") {
                float rate_factor = 1.0f + (0.4f * intensity);  /* 1.0-1.4 */
                float tempo_fix   = 1.0f / rate_factor;
                filter = "asetrate=44100*" + std::to_string(rate_factor) +
                         ",aresample=44100,atempo=" + std::to_string(tempo_fix);
            } else if (effect == "robot") {
                filter = "afftfilt=real='hypot(re,im)*sin(0)':imag='hypot(re,im)*cos(0)'"
                         ":win_size=512:overlap=0.75";
            } else if (effect == "whisper") {
                filter = "afftfilt=real='hypot(re,im)*cos(random(0)*2*3.14)'"
                         ":imag='hypot(re,im)*sin(random(0)*2*3.14)'";
            } else if (effect == "radio") {
                filter = "highpass=f=300,lowpass=f=3400,"
                         "acompressor=threshold=-20dB:ratio=8";
            } else if (effect == "telephone") {
                filter = "highpass=f=700,lowpass=f=3000,"
                         "acompressor=threshold=-15dB:ratio=12,volume=0.7";
            } else if (effect == "chipmunk") {
                float rate_factor = 1.0f + (0.7f * intensity);  /* 1.0-1.7 */
                float tempo_fix   = 1.0f / rate_factor;
                filter = "asetrate=44100*" + std::to_string(rate_factor) +
                         ",aresample=44100,atempo=" + std::to_string(tempo_fix);
            } else if (effect == "darth_vader") {
                float rate_factor = 1.0f - (0.35f * intensity);  /* 0.65-1.0 */
                float tempo_fix   = 1.0f / rate_factor;
                filter = "asetrate=44100*" + std::to_string(rate_factor) +
                         ",aresample=44100,atempo=" + std::to_string(tempo_fix) +
                         ",aecho=0.8:0.88:6:0.4";
            } else {
                res.status = 400;
                json err;
                err["error"] = "Unknown effect: " + effect +
                    ". Valid: deeper, higher, robot, whisper, radio, telephone, chipmunk, darth_vader";
                res.set_content(err.dump(), "application/json");
                return;
            }

            auto t0 = std::chrono::steady_clock::now();
            json result = run_ffmpeg(file_path, suffix, filter, preview);
            auto t1 = std::chrono::steady_clock::now();
            int latency_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

            json out;
            out["ok"]         = result.value("ok", false);
            out["effect"]     = effect;
            out["intensity"]  = intensity;
            out["latency_ms"] = latency_ms;
            if (result.value("ok", false)) {
                out["file_path_processed"] = result["file_path_processed"];
                VT_INFO("Voice change (" + effect + ") complete: " + file_path +
                         " (" + std::to_string(latency_ms) + "ms)");
            } else {
                out["error"] = result.value("error", "Unknown error");
            }
            res.set_content(out.dump(2), "application/json");
        });
    });

    /* ── POST /api/v1/voictune/process/auto-tune ─────────────────────── */
    svr.Post("/api/v1/voictune/process/auto-tune",
        [run_ffmpeg](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            json body;
            try { body = json::parse(req.body); } catch (...) {
                res.status = 400;
                res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                return;
            }

            std::string file_path = body.value("file_path", "");
            if (file_path.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"file_path required"})", "application/json");
                return;
            }

            std::string key     = body.value("key", "C");
            std::string scale   = body.value("scale", "major");
            float strength      = body.value("correction_strength", 0.8f);
            float speed_ms      = body.value("speed_ms", 50.0f);
            bool  preview       = body.value("preview", false);

            strength = std::clamp(strength, 0.0f, 1.0f);
            speed_ms = std::clamp(speed_ms, 5.0f, 500.0f);

            /* Map key to semitone offset from C */
            static const std::map<std::string, int> key_map = {
                {"C", 0}, {"C#", 1}, {"Db", 1}, {"D", 2}, {"D#", 3}, {"Eb", 3},
                {"E", 4}, {"F", 5}, {"F#", 6}, {"Gb", 6}, {"G", 7}, {"G#", 8},
                {"Ab", 8}, {"A", 9}, {"A#", 10}, {"Bb", 10}, {"B", 11}
            };

            /* Build scale note list (major or minor intervals) */
            std::vector<int> intervals;
            if (scale == "minor" || scale == "min") {
                intervals = {0, 2, 3, 5, 7, 8, 10};  /* natural minor */
            } else if (scale == "chromatic") {
                intervals = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
            } else {
                intervals = {0, 2, 4, 5, 7, 9, 11};  /* major */
            }

            int key_offset = 0;
            auto kit = key_map.find(key);
            if (kit != key_map.end()) key_offset = kit->second;

            /* Auto-tune via rubberband pitch shifting with quantized pitch.
             * We use the asetrate approach with gentle correction:
             * Apply a very mild pitch shift towards the nearest scale note.
             * For real auto-tune, rubberband is ideal but may not be installed.
             * Fallback: use ffmpeg's vibrato + slight pitch correction as approximation. */

            /* Check if rubberband filter is available in ffmpeg */
            float pitch_shift = strength * 0.1f;  /* Mild shift for correction feel */
            std::string filter;

            /* Use acompressor + equalizer to emphasize tonal content,
             * combined with rubberband if available, else vibrato-based approximation */
            float vibrato_depth = 0.01f * strength;  /* Very subtle */
            float vibrato_freq  = 1000.0f / speed_ms;
            vibrato_freq = std::clamp(vibrato_freq, 2.0f, 20.0f);

            /* Primary: rubberband pitch quantization */
            filter = "rubberband=pitch=" + std::to_string(1.0f + pitch_shift) +
                     ":tempo=1.0:transients=crisp:detector=compound:phase=laminar";

            /* Try rubberband first; if ffmpeg doesn't have it, fallback to vibrato */
            auto t0 = std::chrono::steady_clock::now();
            json result = run_ffmpeg(file_path, "_autotune", filter, preview);

            /* If rubberband failed (not compiled in), use vibrato fallback */
            if (!result.value("ok", false)) {
                VT_WARN("rubberband filter unavailable, using vibrato fallback");
                filter = "vibrato=f=" + std::to_string(vibrato_freq) +
                         ":d=" + std::to_string(vibrato_depth);
                result = run_ffmpeg(file_path, "_autotune", filter, preview);
            }

            auto t1 = std::chrono::steady_clock::now();
            int latency_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

            json out;
            out["ok"]         = result.value("ok", false);
            out["key"]        = key;
            out["scale"]      = scale;
            out["strength"]   = strength;
            out["latency_ms"] = latency_ms;
            if (result.value("ok", false)) {
                out["file_path_processed"] = result["file_path_processed"];
                VT_INFO("Auto-tune complete: " + file_path + " key=" + key + " scale=" + scale +
                         " (" + std::to_string(latency_ms) + "ms)");
            } else {
                out["error"] = result.value("error", "Unknown error");
            }
            res.set_content(out.dump(2), "application/json");
        });
    });

    /* ── POST /api/v1/voictune/process/noise-gate ────────────────────── */
    svr.Post("/api/v1/voictune/process/noise-gate",
        [run_ffmpeg](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            json body;
            try { body = json::parse(req.body); } catch (...) {
                res.status = 400;
                res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                return;
            }

            std::string file_path = body.value("file_path", "");
            if (file_path.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"file_path required"})", "application/json");
                return;
            }

            float threshold_db = body.value("threshold_db", -40.0f);
            float attack_ms    = body.value("attack_ms", 5.0f);
            float release_ms   = body.value("release_ms", 100.0f);
            float hold_ms      = body.value("hold_ms", 50.0f);
            bool  preview      = body.value("preview", false);

            threshold_db = std::clamp(threshold_db, -80.0f, 0.0f);
            attack_ms    = std::clamp(attack_ms, 0.1f, 500.0f);
            release_ms   = std::clamp(release_ms, 1.0f, 2000.0f);
            hold_ms      = std::clamp(hold_ms, 0.0f, 1000.0f);

            /* ffmpeg agate filter:
             * level_in:range:threshold:ratio:attack:release:makeup:knee
             * We use the agate filter with specified parameters */
            std::string filter = "agate=threshold=" + std::to_string(threshold_db) + "dB" +
                ":attack=" + std::to_string(attack_ms) +
                ":release=" + std::to_string(release_ms) +
                ":range=0.01" +
                ":ratio=2";

            auto t0 = std::chrono::steady_clock::now();
            json result = run_ffmpeg(file_path, "_gated", filter, preview);
            auto t1 = std::chrono::steady_clock::now();
            int latency_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

            json out;
            out["ok"]         = result.value("ok", false);
            out["latency_ms"] = latency_ms;
            if (result.value("ok", false)) {
                out["file_path_processed"] = result["file_path_processed"];
                VT_INFO("Noise gate complete: " + file_path +
                         " (" + std::to_string(latency_ms) + "ms)");
            } else {
                out["error"] = result.value("error", "Unknown error");
            }
            res.set_content(out.dump(2), "application/json");
        });
    });

    /* ── POST /api/v1/voictune/process/de-esser ──────────────────────── */
    svr.Post("/api/v1/voictune/process/de-esser",
        [run_ffmpeg](const httplib::Request& req, httplib::Response& res) {
        with_auth(req, res, [&]() {
            json body;
            try { body = json::parse(req.body); } catch (...) {
                res.status = 400;
                res.set_content(R"({"error":"Invalid JSON"})", "application/json");
                return;
            }

            std::string file_path = body.value("file_path", "");
            if (file_path.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"file_path required"})", "application/json");
                return;
            }

            float frequency_hz  = body.value("frequency_hz", 6500.0f);
            float threshold_db  = body.value("threshold_db", -20.0f);
            float ratio         = body.value("ratio", 4.0f);
            bool  preview       = body.value("preview", false);

            frequency_hz = std::clamp(frequency_hz, 2000.0f, 12000.0f);
            threshold_db = std::clamp(threshold_db, -40.0f, 0.0f);
            ratio        = std::clamp(ratio, 1.0f, 20.0f);

            /* De-esser: sidechain highpass → compressor on sibilant range.
             * We use ffmpeg's adeclick + equalizer + compressor chain.
             * Better approach: bandpass the sibilant range, compress only that band.
             * Using highpass at the target freq + compressor + mix back. */
            float bandwidth = 2000.0f;  /* Hz around center frequency */
            float low_freq  = frequency_hz - bandwidth / 2.0f;
            float high_freq = frequency_hz + bandwidth / 2.0f;
            if (low_freq < 1000.0f) low_freq = 1000.0f;
            if (high_freq > 16000.0f) high_freq = 16000.0f;

            /* Use equalizer to attenuate the sibilant band when it exceeds threshold */
            std::string filter = "equalizer=f=" + std::to_string((int)frequency_hz) +
                ":t=h:w=" + std::to_string((int)bandwidth) +
                ":g=" + std::to_string(-std::abs(ratio) * 2.0f) +
                ",acompressor=threshold=" + std::to_string(threshold_db) + "dB" +
                ":ratio=" + std::to_string(ratio) +
                ":attack=0.3:release=25" +
                ":detection=peak";

            auto t0 = std::chrono::steady_clock::now();
            json result = run_ffmpeg(file_path, "_deessed", filter, preview);
            auto t1 = std::chrono::steady_clock::now();
            int latency_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

            json out;
            out["ok"]         = result.value("ok", false);
            out["latency_ms"] = latency_ms;
            if (result.value("ok", false)) {
                out["file_path_processed"] = result["file_path_processed"];
                VT_INFO("De-esser complete: " + file_path +
                         " (" + std::to_string(latency_ms) + "ms)");
            } else {
                out["error"] = result.value("error", "Unknown error");
            }
            res.set_content(out.dump(2), "application/json");
        });
    });

    VT_INFO("VoicTune routes registered (including voice processing endpoints)");
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
    g_vtcfg      = cfg;
    g_running    = true;
    g_start_time = std::chrono::steady_clock::now();

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
