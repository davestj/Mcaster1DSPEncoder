/*
 * Mcaster1DSPEncoder — Encoder Binary Entry Point
 * main_encoder.cpp
 *
 * This is the audio worker process. It runs the AudioPipeline with all encoder
 * slots, DSP chains, codecs, and streaming clients. Exposes a lightweight HTTP
 * API on localhost for IPC with the admin binary.
 *
 * Started by the admin binary's ProcessSupervisor (or standalone for testing).
 * If this process crashes (codec segfault, etc.), the admin binary detects it
 * and auto-restarts it. The web UI stays up during the restart.
 *
 * Usage:
 *   mcaster1-dsp-encoder --config path/to/config.yaml [--ipc-port 8331] [--log-level 5]
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config_loader.h"
#include "audio_pipeline.h"
#include "encoder_ipc_api.h"
#include "mc1_logger.h"

#include "external/include/httplib.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <atomic>
#include <thread>

/* ── Globals ───────────────────────────────────────────────────────────────── */
/* g_pipeline is defined in audio_pipeline.cpp (extern in audio_pipeline.h) */
static std::atomic<bool> g_running{true};
static httplib::Server*  g_ipc_server = nullptr;

/* We catch SIGTERM/SIGINT for graceful shutdown */
static void signal_handler(int sig) {
    MC1_INFO("[Encoder] Signal " + std::to_string(sig) + " received — shutting down");
    g_running.store(false);
    if (g_ipc_server) g_ipc_server->stop();
}

/* ── Main ──────────────────────────────────────────────────────────────────── */
int main(int argc, char* argv[]) {
    /* We parse CLI args */
    std::string config_path;
    int ipc_port  = 8331;
    int log_level = 4;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--ipc-port" && i + 1 < argc) {
            ipc_port = std::atoi(argv[++i]);
        } else if (arg == "--log-level" && i + 1 < argc) {
            log_level = std::atoi(argv[++i]);
        } else if (arg == "-v") {
            log_level = 5;
        }
    }

    if (config_path.empty()) {
        fprintf(stderr, "Usage: mcaster1-dsp-encoder --config <path.yaml> [--ipc-port 8331] [-v]\n");
        return 1;
    }

    /* We init logging */
    auto& logger = Mc1Logger::instance();
    logger.init("/var/log/mcaster1", log_level, true);

    MC1_INFO("[Encoder] ═══════════════════════════════════════════════════════");
    MC1_INFO("[Encoder] Mcaster1DSPEncoder — Audio Worker Process");
    MC1_INFO("[Encoder] PID: " + std::to_string(getpid()));
    MC1_INFO("[Encoder] Config: " + config_path);
    MC1_INFO("[Encoder] IPC port: " + std::to_string(ipc_port));
    MC1_INFO("[Encoder] Log level: " + std::to_string(log_level));
    MC1_INFO("[Encoder] ═══════════════════════════════════════════════════════");

    /* We install signal handlers */
    signal(SIGTERM, signal_handler);
    signal(SIGINT,  signal_handler);
    signal(SIGPIPE, SIG_IGN);

    /* We load config from YAML — fills gAdminConfig (HTTP, DB, DNAS, log settings) */
    if (!mc1_load_config(config_path.c_str(), nullptr, nullptr)) {
        MC1_ERR("[Encoder] FATAL: Config load failed: " + config_path);
        return 2;
    }

    /* We create the audio pipeline */
    AudioPipeline pipeline;
    g_pipeline = &pipeline;

    /* We load encoder slots from the MySQL encoder_configs table */
    if (!mc1_load_slots_from_db(&pipeline)) {
        MC1_WARN("[Encoder] No active encoder slots loaded from DB — "
                 "check encoder_configs table and DB credentials in YAML");
    } else {
        MC1_INFO("[Encoder] Loaded " + std::to_string(pipeline.slot_count()) + " encoder slots from DB");

        /* We log detailed config for each slot for debugging */
        auto all = pipeline.all_stats();
        for (auto& s : all) {
            EncoderConfig cfg;
            if (pipeline.get_slot_config(s.slot_id, cfg)) {
                MC1_INFO("[Encoder] Slot " + std::to_string(s.slot_id) + ": " + cfg.name +
                         " codec=" + std::to_string(static_cast<int>(cfg.codec)) +
                         " bitrate=" + std::to_string(cfg.bitrate_kbps) + "kbps" +
                         " sr=" + std::to_string(cfg.sample_rate) + "Hz" +
                         " ch=" + std::to_string(cfg.channels) +
                         " input=" + std::to_string(static_cast<int>(cfg.input_type)) +
                         " eq=" + (cfg.dsp_eq_enabled ? "on" : "off") +
                         " agc=" + (cfg.dsp_agc_enabled ? "on" : "off") +
                         " xfade=" + (cfg.dsp_crossfade_enabled ? "on" : "off") +
                         " curve=" + std::to_string(cfg.dsp_crossfade_curve));
            }
        }
    }

    /* We start background services (DB connection, health monitors) */
    pipeline.start_background_services();

    /* We start auto-start slots */
    pipeline.start_auto_slots();

    /* We start the IPC HTTP server */
    httplib::Server ipc_svr;
    g_ipc_server = &ipc_svr;

    register_encoder_ipc_routes(ipc_svr, &pipeline);

    MC1_INFO("[Encoder] Starting IPC server on 127.0.0.1:" + std::to_string(ipc_port));

    /* We run IPC server in a thread so we can handle signals in main */
    std::thread ipc_thread([&ipc_svr, ipc_port]() {
        if (!ipc_svr.listen("127.0.0.1", ipc_port)) {
            MC1_ERR("[Encoder] IPC server failed to bind on port " + std::to_string(ipc_port));
        }
    });

    /* We wait for shutdown signal */
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    MC1_INFO("[Encoder] Shutting down...");
    ipc_svr.stop();
    if (ipc_thread.joinable()) ipc_thread.join();

    /* We stop all encoder slots */
    auto stats = pipeline.all_stats();
    for (auto& s : stats) {
        if (s.is_live) {
            MC1_INFO("[Encoder] Stopping slot " + std::to_string(s.slot_id));
            pipeline.stop_slot(s.slot_id);
        }
    }

    g_pipeline = nullptr;
    MC1_INFO("[Encoder] Clean shutdown complete");
    return 0;
}
