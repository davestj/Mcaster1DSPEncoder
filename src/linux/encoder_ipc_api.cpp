/*
 * Mcaster1DSPEncoder — Encoder IPC API Implementation
 * encoder_ipc_api.cpp
 *
 * All encoder-specific routes for the IPC server on localhost:8331.
 * No auth gate — IPC is trusted (admin already authenticated the user).
 * Verbose debug logging for every encoder operation.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "encoder_ipc_api.h"
#include "audio_pipeline.h"
#include "mc1_logger.h"
#include "dsp/crossfader_curves.h"
#include "dsp/effects_rack.h"
#include "dsp/sidechain_ducker.h"

#include "external/include/nlohmann/json.hpp"
using json = nlohmann::json;

#include <algorithm>
#include <string>

/* We keep a reference to the pipeline for all route handlers */
static AudioPipeline* g_enc_pipeline = nullptr;

/* ── Helper: state name for logging ────────────────────────────────────────── */
static const char* state_str(int state_int) {
    switch (state_int) {
        case 0: return "idle"; case 1: return "starting"; case 2: return "connecting";
        case 3: return "live"; case 4: return "reconnecting"; case 5: return "sleep";
        case 6: return "error"; case 7: return "stopping";
    }
    return "unknown";
}

void register_encoder_ipc_routes(httplib::Server& svr, AudioPipeline* pipeline) {
    g_enc_pipeline = pipeline;

    /* ── Health check ──────────────────────────────────────────────────────── */
    svr.Get("/api/v1/encoder/status", [](const httplib::Request&, httplib::Response& res) {
        json j;
        j["ok"] = true;
        j["process"] = "encoder";
        j["pid"] = getpid();
        if (g_enc_pipeline) {
            auto stats = g_enc_pipeline->all_stats();
            j["slot_count"] = (int)stats.size();
            int live = 0;
            for (auto& s : stats) if (s.is_live) live++;
            j["slots_live"] = live;
        }
        res.set_content(j.dump(2), "application/json");
    });

    /* ── GET /api/v1/encoders — all slot stats ─────────────────────────────── */
    svr.Get("/api/v1/encoders", [](const httplib::Request&, httplib::Response& res) {
        json j;
        if (!g_enc_pipeline) { j["ok"] = false; j["error"] = "No pipeline"; res.set_content(j.dump(), "application/json"); return; }
        auto stats = g_enc_pipeline->all_stats();
        j["ok"] = true;
        json arr = json::array();
        for (auto& s : stats) {
            json slot;
            slot["slot_id"]       = s.slot_id;
            slot["state"]         = s.state_str;
            slot["is_live"]       = s.is_live;
            slot["bytes_sent"]    = s.bytes_sent;
            slot["uptime_sec"]    = s.uptime_sec;
            slot["track_index"]   = s.track_index;
            slot["track_count"]   = s.track_count;
            slot["current_title"] = s.current_title;
            slot["current_artist"]= s.current_artist;
            slot["position_ms"]   = s.position_ms;
            slot["duration_ms"]   = s.duration_ms;
            slot["volume"]        = s.volume;
            slot["last_error"]    = s.last_error;
            arr.push_back(slot);
        }
        j["slots"] = arr;
        res.set_content(j.dump(2), "application/json");
    });

    /* ── GET /api/v1/encoders/{slot}/stats ─────────────────────────────────── */
    svr.Get(R"(/api/v1/encoders/(\d+)/stats)", [](const httplib::Request& req, httplib::Response& res) {
        int sid = std::stoi(req.matches[1].str());
        if (!g_enc_pipeline) { res.set_content(R"({"ok":false})", "application/json"); return; }
        auto s = g_enc_pipeline->slot_stats(sid);
        json j;
        j["ok"] = true; j["slot_id"] = sid;
        j["state"] = s.state_str; j["is_live"] = s.is_live;
        j["bytes_sent"] = s.bytes_sent; j["uptime_sec"] = s.uptime_sec;
        j["current_title"] = s.current_title; j["current_artist"] = s.current_artist;
        j["position_ms"] = s.position_ms; j["duration_ms"] = s.duration_ms;
        j["volume"] = s.volume; j["track_index"] = s.track_index; j["track_count"] = s.track_count;
        j["last_error"] = s.last_error;
        res.set_content(j.dump(2), "application/json");
    });

    /* ── POST /api/v1/encoders/{slot}/start ────────────────────────────────── */
    svr.Post(R"(/api/v1/encoders/(\d+)/start)", [](const httplib::Request& req, httplib::Response& res) {
        int sid = std::stoi(req.matches[1].str());
        MC1_INFO("[Encoder IPC] START slot " + std::to_string(sid));
        if (!g_enc_pipeline) { res.set_content(R"({"ok":false})", "application/json"); return; }

        /* We log the encoder config for debugging */
        EncoderConfig cfg;
        if (g_enc_pipeline->get_slot_config(sid, cfg)) {
            MC1_INFO("[Encoder IPC] Slot " + std::to_string(sid) + " config: " +
                     "codec=" + std::to_string(static_cast<int>(cfg.codec)) +
                     " bitrate=" + std::to_string(cfg.bitrate_kbps) +
                     " sr=" + std::to_string(cfg.sample_rate) +
                     " ch=" + std::to_string(cfg.channels) +
                     " input=" + std::to_string(static_cast<int>(cfg.input_type)));
        }

        bool ok = g_enc_pipeline->start_slot(sid);
        json j; j["ok"] = ok; j["slot_id"] = sid;
        if (!ok) {
            j["error"] = "Start failed — check encoder config and logs";
            MC1_ERR("[Encoder IPC] START FAILED slot " + std::to_string(sid));
        } else {
            MC1_INFO("[Encoder IPC] START OK slot " + std::to_string(sid));
        }
        res.set_content(j.dump(), "application/json");
    });

    /* ── POST /api/v1/encoders/{slot}/stop ─────────────────────────────────── */
    svr.Post(R"(/api/v1/encoders/(\d+)/stop)", [](const httplib::Request& req, httplib::Response& res) {
        int sid = std::stoi(req.matches[1].str());
        MC1_INFO("[Encoder IPC] STOP slot " + std::to_string(sid));
        bool ok = g_enc_pipeline ? g_enc_pipeline->stop_slot(sid) : false;
        json j; j["ok"] = ok; j["slot_id"] = sid;
        res.set_content(j.dump(), "application/json");
    });

    /* ── POST /api/v1/encoders/{slot}/restart ──────────────────────────────── */
    svr.Post(R"(/api/v1/encoders/(\d+)/restart)", [](const httplib::Request& req, httplib::Response& res) {
        int sid = std::stoi(req.matches[1].str());
        MC1_INFO("[Encoder IPC] RESTART slot " + std::to_string(sid));
        bool ok = g_enc_pipeline ? g_enc_pipeline->restart_slot(sid) : false;
        json j; j["ok"] = ok; j["slot_id"] = sid;
        res.set_content(j.dump(), "application/json");
    });

    /* ── POST /api/v1/encoders/{slot}/wake ─────────────────────────────────── */
    svr.Post(R"(/api/v1/encoders/(\d+)/wake)", [](const httplib::Request& req, httplib::Response& res) {
        int sid = std::stoi(req.matches[1].str());
        MC1_INFO("[Encoder IPC] WAKE slot " + std::to_string(sid));
        bool ok = g_enc_pipeline ? g_enc_pipeline->wake_slot(sid) : false;
        json j; j["ok"] = ok; j["slot_id"] = sid;
        res.set_content(j.dump(), "application/json");
    });

    /* ── GET /api/v1/encoders/{slot}/dsp — get DSP config ──────────────────── */
    svr.Get(R"(/api/v1/encoders/(\d+)/dsp)", [](const httplib::Request& req, httplib::Response& res) {
        int sid = std::stoi(req.matches[1].str());
        json j; j["ok"] = true;
        EncoderConfig cfg;
        if (g_enc_pipeline && g_enc_pipeline->get_slot_config(sid, cfg)) {
            j["slot_id"]            = sid;
            j["eq_enabled"]         = cfg.dsp_eq_enabled;
            j["agc_enabled"]        = cfg.dsp_agc_enabled;
            j["crossfade_enabled"]  = cfg.dsp_crossfade_enabled;
            j["crossfade_duration"] = cfg.dsp_crossfade_duration;
            j["crossfade_curve"]    = cfg.dsp_crossfade_curve;
            j["eq_preset"]          = cfg.dsp_eq_preset;
            j["presets_available"]   = json::array({"flat","classic_rock","country","modern_rock","broadcast","spoken_word"});
        } else { res.status = 404; j["ok"] = false; j["error"] = "Slot not found"; }
        res.set_content(j.dump(2), "application/json");
    });

    /* ── PUT /api/v1/encoders/{slot}/dsp — update DSP config live ──────────── */
    svr.Put(R"(/api/v1/encoders/(\d+)/dsp)", [](const httplib::Request& req, httplib::Response& res) {
        int sid = std::stoi(req.matches[1].str());
        json body;
        try { body = json::parse(req.body); } catch (...) {
            res.status = 400; res.set_content(R"({"error":"Invalid JSON"})", "application/json"); return;
        }
        EncoderConfig cfg;
        if (!g_enc_pipeline || !g_enc_pipeline->get_slot_config(sid, cfg)) {
            res.status = 404; res.set_content(R"({"error":"Slot not found"})", "application/json"); return;
        }

        MC1_DBG("[Encoder IPC] DSP update slot " + std::to_string(sid) + ": " + body.dump());

        if (body.contains("eq_enabled"))        cfg.dsp_eq_enabled        = body["eq_enabled"].get<bool>();
        if (body.contains("agc_enabled"))       cfg.dsp_agc_enabled       = body["agc_enabled"].get<bool>();
        if (body.contains("crossfade_enabled")) cfg.dsp_crossfade_enabled = body["crossfade_enabled"].get<bool>();
        if (body.contains("crossfade_duration")) cfg.dsp_crossfade_duration= body["crossfade_duration"].get<float>();
        if (body.contains("crossfade_curve"))   cfg.dsp_crossfade_curve   = std::clamp(body["crossfade_curve"].get<int>(), 0, 8);
        if (body.contains("eq_preset"))         cfg.dsp_eq_preset         = body["eq_preset"].get<std::string>();

        mc1dsp::DspChainConfig dsp_cfg;
        dsp_cfg.sample_rate        = cfg.sample_rate;
        dsp_cfg.channels           = cfg.channels;
        dsp_cfg.eq_enabled         = cfg.dsp_eq_enabled;
        dsp_cfg.agc_enabled        = cfg.dsp_agc_enabled;
        dsp_cfg.crossfader_enabled = cfg.dsp_crossfade_enabled;
        dsp_cfg.crossfade_duration = cfg.dsp_crossfade_duration;
        dsp_cfg.crossfade_curve    = cfg.dsp_crossfade_curve;
        dsp_cfg.eq_preset          = cfg.dsp_eq_preset;
        g_enc_pipeline->reconfigure_dsp(sid, dsp_cfg);

        json r; r["ok"] = true; r["slot_id"] = sid;
        r["eq_enabled"] = cfg.dsp_eq_enabled; r["agc_enabled"] = cfg.dsp_agc_enabled;
        r["crossfade_enabled"] = cfg.dsp_crossfade_enabled; r["crossfade_curve"] = cfg.dsp_crossfade_curve;
        r["eq_preset"] = cfg.dsp_eq_preset;
        res.set_content(r.dump(2), "application/json");
    });

    /* ── POST /api/v1/encoders/{slot}/dsp/eq/preset ────────────────────────── */
    svr.Post(R"(/api/v1/encoders/(\d+)/dsp/eq/preset)", [](const httplib::Request& req, httplib::Response& res) {
        int sid = std::stoi(req.matches[1].str());
        json body;
        try { body = json::parse(req.body); } catch (...) {
            res.status = 400; res.set_content(R"({"error":"Invalid JSON"})", "application/json"); return;
        }
        std::string preset = body.value("preset", "");
        EncoderConfig cfg;
        if (!g_enc_pipeline || !g_enc_pipeline->get_slot_config(sid, cfg)) {
            res.status = 404; res.set_content(R"({"error":"Slot not found"})", "application/json"); return;
        }
        MC1_INFO("[Encoder IPC] EQ preset '" + preset + "' → slot " + std::to_string(sid));
        mc1dsp::DspChainConfig dsp_cfg;
        dsp_cfg.sample_rate = cfg.sample_rate; dsp_cfg.channels = cfg.channels;
        dsp_cfg.eq_enabled = true; dsp_cfg.agc_enabled = cfg.dsp_agc_enabled;
        dsp_cfg.crossfader_enabled = cfg.dsp_crossfade_enabled;
        dsp_cfg.crossfade_duration = cfg.dsp_crossfade_duration;
        dsp_cfg.crossfade_curve    = cfg.dsp_crossfade_curve;
        dsp_cfg.eq_preset = preset;
        g_enc_pipeline->reconfigure_dsp(sid, dsp_cfg);
        json r; r["ok"] = true; r["slot_id"] = sid; r["preset"] = preset;
        res.set_content(r.dump(), "application/json");
    });

    /* ── Crossfader per-slot routes ────────────────────────────────────────── */
    svr.Get(R"(/api/v1/encoders/(\d+)/crossfader)", [](const httplib::Request& req, httplib::Response& res) {
        int sid = std::stoi(req.matches[1].str());
        json j; j["ok"] = true;
        EncoderConfig cfg;
        if (g_enc_pipeline && g_enc_pipeline->get_slot_config(sid, cfg)) {
            j["slot_id"] = sid; j["curve"] = cfg.dsp_crossfade_curve;
            j["curve_name"] = mc1xf::CURVE_NAMES[std::clamp(cfg.dsp_crossfade_curve, 0, 8)];
            j["duration"] = cfg.dsp_crossfade_duration; j["enabled"] = cfg.dsp_crossfade_enabled;
        } else { res.status = 404; j["ok"] = false; j["error"] = "Slot not found"; }
        res.set_content(j.dump(2), "application/json");
    });

    svr.Put(R"(/api/v1/encoders/(\d+)/crossfader)", [](const httplib::Request& req, httplib::Response& res) {
        int sid = std::stoi(req.matches[1].str());
        json body; try { body = json::parse(req.body); } catch (...) {
            res.status = 400; res.set_content(R"({"error":"Invalid JSON"})", "application/json"); return;
        }
        EncoderConfig cfg;
        if (!g_enc_pipeline || !g_enc_pipeline->get_slot_config(sid, cfg)) {
            res.status = 404; res.set_content(R"({"error":"Slot not found"})", "application/json"); return;
        }
        if (body.contains("curve"))    cfg.dsp_crossfade_curve    = std::clamp(body["curve"].get<int>(), 0, 8);
        if (body.contains("duration")) cfg.dsp_crossfade_duration = body["duration"].get<float>();
        if (body.contains("enabled"))  cfg.dsp_crossfade_enabled  = body["enabled"].get<bool>();
        MC1_DBG("[Encoder IPC] Crossfader slot " + std::to_string(sid) + " curve=" + std::to_string(cfg.dsp_crossfade_curve));
        mc1dsp::DspChainConfig dsp_cfg;
        dsp_cfg.sample_rate = cfg.sample_rate; dsp_cfg.channels = cfg.channels;
        dsp_cfg.eq_enabled = cfg.dsp_eq_enabled; dsp_cfg.agc_enabled = cfg.dsp_agc_enabled;
        dsp_cfg.crossfader_enabled = cfg.dsp_crossfade_enabled;
        dsp_cfg.crossfade_duration = cfg.dsp_crossfade_duration;
        dsp_cfg.crossfade_curve    = cfg.dsp_crossfade_curve;
        dsp_cfg.eq_preset = cfg.dsp_eq_preset;
        g_enc_pipeline->reconfigure_dsp(sid, dsp_cfg);
        json r; r["ok"] = true; r["slot_id"] = sid; r["curve"] = cfg.dsp_crossfade_curve;
        r["curve_name"] = mc1xf::CURVE_NAMES[cfg.dsp_crossfade_curve];
        res.set_content(r.dump(2), "application/json");
    });

    /* ── Per-slot effects mode ─────────────────────────────────────────────── */
    svr.Get(R"(/api/v1/encoders/(\d+)/effects)", [](const httplib::Request& req, httplib::Response& res) {
        int sid = std::stoi(req.matches[1].str());
        json j; j["ok"] = true;
        EncoderConfig cfg;
        if (g_enc_pipeline && g_enc_pipeline->get_slot_config(sid, cfg)) {
            const char* modes[] = {"global","bypass","custom"};
            j["slot_id"] = sid; j["effects_mode"] = modes[static_cast<int>(cfg.effects_mode)];
        } else { res.status = 404; j["ok"] = false; j["error"] = "Slot not found"; }
        res.set_content(j.dump(2), "application/json");
    });

    svr.Put(R"(/api/v1/encoders/(\d+)/effects)", [](const httplib::Request& req, httplib::Response& res) {
        int sid = std::stoi(req.matches[1].str());
        json body; try { body = json::parse(req.body); } catch (...) {
            res.status = 400; res.set_content(R"({"error":"Invalid JSON"})", "application/json"); return;
        }
        EncoderConfig cfg;
        if (!g_enc_pipeline || !g_enc_pipeline->get_slot_config(sid, cfg)) {
            res.status = 404; res.set_content(R"({"error":"Slot not found"})", "application/json"); return;
        }
        if (body.contains("effects_mode")) {
            std::string mode = body["effects_mode"].get<std::string>();
            if (mode == "global")  cfg.effects_mode = EncoderConfig::EffectsMode::GLOBAL;
            else if (mode == "bypass")  cfg.effects_mode = EncoderConfig::EffectsMode::BYPASS;
            else if (mode == "custom")  cfg.effects_mode = EncoderConfig::EffectsMode::CUSTOM;
            MC1_INFO("[Encoder IPC] Effects mode slot " + std::to_string(sid) + " → " + mode);
        }
        const char* modes[] = {"global","bypass","custom"};
        json r; r["ok"] = true; r["slot_id"] = sid; r["effects_mode"] = modes[static_cast<int>(cfg.effects_mode)];
        res.set_content(r.dump(2), "application/json");
    });

    /* ── Effects rack routes ───────────────────────────────────────────────── */
    svr.Get("/api/v1/effects/unit-types", [](const httplib::Request&, httplib::Response& res) {
        json j; j["ok"] = true; j["types"] = mc1dsp::EffectsRack::available_types();
        res.set_content(j.dump(2), "application/json");
    });

    svr.Get("/api/v1/effects/global", [](const httplib::Request&, httplib::Response& res) {
        json j; j["ok"] = true;
        j["rack"] = g_enc_pipeline ? g_enc_pipeline->global_effects_rack().to_json() : json::object();
        res.set_content(j.dump(2), "application/json");
    });

    svr.Put("/api/v1/effects/global", [](const httplib::Request& req, httplib::Response& res) {
        json body; try { body = json::parse(req.body); } catch (...) {
            res.status = 400; res.set_content(R"({"error":"Invalid JSON"})", "application/json"); return;
        }
        if (!g_enc_pipeline) { res.status = 503; res.set_content(R"({"error":"No pipeline"})", "application/json"); return; }
        auto& rack = g_enc_pipeline->global_effects_rack();
        if (body.contains("bypass")) rack.set_bypass(body["bypass"].get<bool>());
        if (body.contains("unit_id") && body.contains("params")) rack.set_unit_params(body["unit_id"].get<int>(), body["params"]);
        if (body.contains("unit_id") && body.contains("enabled")) rack.set_unit_enabled(body["unit_id"].get<int>(), body["enabled"].get<bool>());
        json r; r["ok"] = true; r["rack"] = rack.to_json();
        res.set_content(r.dump(2), "application/json");
    });

    svr.Post("/api/v1/effects/global/units", [](const httplib::Request& req, httplib::Response& res) {
        json body; try { body = json::parse(req.body); } catch (...) {
            res.status = 400; res.set_content(R"({"error":"Invalid JSON"})", "application/json"); return;
        }
        if (!g_enc_pipeline) { res.status = 503; res.set_content(R"({"error":"No pipeline"})", "application/json"); return; }
        std::string type = body.value("type", "");
        auto unit = mc1dsp::EffectsRack::create_unit(type);
        if (!unit) { res.status = 400; res.set_content(R"({"error":"Unknown unit type"})", "application/json"); return; }
        if (body.contains("params")) unit->set_params(body["params"]);
        if (body.contains("enabled")) unit->set_enabled(body["enabled"].get<bool>());
        int id = g_enc_pipeline->global_effects_rack().add_unit(std::move(unit));
        MC1_INFO("[Encoder IPC] Added effect: " + type + " id=" + std::to_string(id));
        json r; r["ok"] = true; r["unit_id"] = id; r["type"] = type;
        res.set_content(r.dump(), "application/json");
    });

    svr.Delete(R"(/api/v1/effects/global/units/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
        int uid = std::stoi(req.matches[1].str());
        if (!g_enc_pipeline) { res.status = 503; res.set_content(R"({"error":"No pipeline"})", "application/json"); return; }
        bool ok = g_enc_pipeline->global_effects_rack().remove_unit(uid);
        json r; r["ok"] = ok; if (!ok) r["error"] = "Unit not found";
        res.set_content(r.dump(), "application/json");
    });

    svr.Put("/api/v1/effects/global/reorder", [](const httplib::Request& req, httplib::Response& res) {
        json body; try { body = json::parse(req.body); } catch (...) {
            res.status = 400; res.set_content(R"({"error":"Invalid JSON"})", "application/json"); return;
        }
        if (!g_enc_pipeline || !body.contains("order")) {
            res.status = 400; res.set_content(R"({"error":"order array required"})", "application/json"); return;
        }
        std::vector<int> order;
        for (const auto& v : body["order"]) order.push_back(v.get<int>());
        bool ok = g_enc_pipeline->global_effects_rack().reorder(order);
        json r; r["ok"] = ok;
        if (ok) r["rack"] = g_enc_pipeline->global_effects_rack().to_json();
        else r["error"] = "Reorder failed";
        res.set_content(r.dump(2), "application/json");
    });

    /* ── PTT routes ────────────────────────────────────────────────────────── */
    svr.Post("/api/v1/ptt/activate", [](const httplib::Request&, httplib::Response& res) {
        if (g_enc_pipeline) g_enc_pipeline->set_ptt(true);
        MC1_INFO("[Encoder IPC] PTT ACTIVATED");
        res.set_content(R"({"ok":true,"ptt_active":true})", "application/json");
    });

    svr.Post("/api/v1/ptt/deactivate", [](const httplib::Request&, httplib::Response& res) {
        if (g_enc_pipeline) g_enc_pipeline->set_ptt(false);
        MC1_INFO("[Encoder IPC] PTT DEACTIVATED");
        res.set_content(R"({"ok":true,"ptt_active":false})", "application/json");
    });

    svr.Get("/api/v1/ptt/status", [](const httplib::Request&, httplib::Response& res) {
        json r; r["ok"] = true;
        if (g_enc_pipeline) {
            auto& d = g_enc_pipeline->ducker();
            r["ptt_active"] = d.is_ptt_active(); r["current_duck_db"] = d.current_duck_db();
            r["config"] = d.get_params();
        } else { r["ptt_active"] = false; }
        res.set_content(r.dump(2), "application/json");
    });

    svr.Put("/api/v1/ptt/config", [](const httplib::Request& req, httplib::Response& res) {
        json body; try { body = json::parse(req.body); } catch (...) {
            res.status = 400; res.set_content(R"({"error":"Invalid JSON"})", "application/json"); return;
        }
        if (g_enc_pipeline) g_enc_pipeline->ducker().set_params(body);
        MC1_DBG("[Encoder IPC] PTT config updated: " + body.dump());
        json r; r["ok"] = true;
        if (g_enc_pipeline) r["config"] = g_enc_pipeline->ducker().get_params();
        res.set_content(r.dump(2), "application/json");
    });

    /* ── Volume, metadata, playlist ────────────────────────────────────────── */
    svr.Put("/api/v1/volume", [](const httplib::Request& req, httplib::Response& res) {
        json body; try { body = json::parse(req.body); } catch (...) {
            res.status = 400; res.set_content(R"({"error":"Invalid JSON"})", "application/json"); return;
        }
        int slot = body.value("slot", -1);
        float vol = body.value("volume", 1.0f);
        MC1_DBG("[Encoder IPC] Volume slot=" + std::to_string(slot) + " vol=" + std::to_string(vol));
        if (g_enc_pipeline) {
            if (slot < 0) g_enc_pipeline->set_master_volume(vol);
            else g_enc_pipeline->set_volume(slot, vol);
        }
        json r; r["ok"] = true;
        res.set_content(r.dump(), "application/json");
    });

    svr.Put("/api/v1/metadata", [](const httplib::Request& req, httplib::Response& res) {
        json body; try { body = json::parse(req.body); } catch (...) {
            res.status = 400; res.set_content(R"({"error":"Invalid JSON"})", "application/json"); return;
        }
        int slot = body.value("slot", -1);
        std::string title = body.value("title", "");
        std::string artist = body.value("artist", "");
        MC1_DBG("[Encoder IPC] Metadata push slot=" + std::to_string(slot) + " title=" + title);
        bool ok = false;
        if (g_enc_pipeline) {
            if (slot >= 0) ok = g_enc_pipeline->push_metadata(slot, title, artist);
        }
        json r; r["ok"] = ok;
        res.set_content(r.dump(), "application/json");
    });

    svr.Post("/api/v1/playlist/load", [](const httplib::Request& req, httplib::Response& res) {
        json body; try { body = json::parse(req.body); } catch (...) {
            res.status = 400; res.set_content(R"({"error":"Invalid JSON"})", "application/json"); return;
        }
        int slot = body.value("slot", 1);
        std::string path = body.value("path", "");
        MC1_INFO("[Encoder IPC] Playlist load slot=" + std::to_string(slot) + " path=" + path);
        bool ok = g_enc_pipeline ? g_enc_pipeline->load_playlist(slot, path) : false;
        json r; r["ok"] = ok; r["slot_id"] = slot;
        res.set_content(r.dump(), "application/json");
    });

    svr.Post("/api/v1/playlist/skip", [](const httplib::Request& req, httplib::Response& res) {
        json body; try { body = json::parse(req.body); } catch (...) { body = json::object(); }
        int slot = body.value("slot", 1);
        MC1_INFO("[Encoder IPC] Skip track slot=" + std::to_string(slot));
        bool ok = g_enc_pipeline ? g_enc_pipeline->skip_track(slot) : false;
        json r; r["ok"] = ok;
        res.set_content(r.dump(), "application/json");
    });

    /* ── Devices ───────────────────────────────────────────────────────────── */
    svr.Get("/api/v1/devices", [](const httplib::Request&, httplib::Response& res) {
        auto devices = AudioPipeline::list_devices();
        json j; j["ok"] = true;
        json arr = json::array();
        for (auto& d : devices) {
            arr.push_back({{"index", d.index}, {"name", d.name},
                           {"max_input_channels", d.max_input_channels},
                           {"default_sample_rate", d.default_sample_rate}});
        }
        j["devices"] = arr;
        res.set_content(j.dump(2), "application/json");
    });

    MC1_INFO("[Encoder IPC] Registered " + std::to_string(25) + " routes on IPC server");
}
