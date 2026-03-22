/*
 * Mcaster1 VoicTune — Voice Analysis & Coaching Daemon
 * voictune/main_voictune.cpp
 *
 * Entry point for the mcaster1-voictune daemon. Provides real-time voice
 * analysis (FFT, pitch detection, RMS/LUFS metering), voice coaching,
 * and Ollama AI integration for broadcast/podcast voice tuning.
 *
 * Ports: 8350 (HTTP), 8354 (HTTPS), 8355 (WebSocket for browser mic)
 * Logs:  /var/log/mcaster1/voictune.log, voictune_error.log
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "vt_config.h"
#include "vt_http_api.h"
#include "vt_logger.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <unistd.h>
#include <getopt.h>
#include <string>
#include <atomic>

#define VT_VERSION "1.0.0"
#define VT_BANNER  "Mcaster1 VoicTune v" VT_VERSION " — Voice Analysis & Coaching Daemon"

static std::atomic<bool> g_shutdown{false};

static void signal_handler(int sig)
{
    fprintf(stderr, "\n[voictune] Caught signal %d — shutting down\n", sig);
    g_shutdown.store(true);
    mc1vt::vt_http_stop();
}

static void print_usage(const char* progname)
{
    fprintf(stderr,
        VT_BANNER "\n\n"
        "Usage: %s [OPTIONS]\n\n"
        "Options:\n"
        "  -c, --config FILE      YAML config file (required)\n"
        "  -p, --port PORT        Override HTTP port (default: 8350)\n"
        "      --ssl-port PORT    Override HTTPS port (default: 8354)\n"
        "      --ws-port PORT     Override WebSocket port (default: 8355)\n"
        "      --log-level N      Log level 1-5 (1=CRIT, 5=DEBUG)\n"
        "      --log-dir DIR      Log directory (default: /var/log/mcaster1)\n"
        "  -v, --verbose          Set log level to 5 (DEBUG)\n"
        "  -h, --help             Show this help\n"
        "  -V, --version          Show version\n"
        "\n"
        "Ports:\n"
        "  8350  HTTP  API (admin, meters, status)\n"
        "  8354  HTTPS API (same routes, TLS)\n"
        "  8355  WebSocket (browser mic audio stream)\n"
        "\n"
        "Example:\n"
        "  %s --config src/linux/config/mcaster1_voictune.yaml -v\n"
        "\n",
        progname, progname);
}

int main(int argc, char* argv[])
{
    fprintf(stdout, VT_BANNER "\n");

    /* ── CLI option parsing ──────────────────────────────────────────── */
    const char* config_file = nullptr;
    int cli_port      = 0;
    int cli_ssl_port  = 0;
    int cli_ws_port   = 0;
    int cli_log_level = 0;
    const char* cli_log_dir = nullptr;
    bool cli_verbose  = false;

    static struct option long_opts[] = {
        {"config",    required_argument, nullptr, 'c'},
        {"port",      required_argument, nullptr, 'p'},
        {"ssl-port",  required_argument, nullptr, 'S'},
        {"ws-port",   required_argument, nullptr, 'W'},
        {"log-level", required_argument, nullptr, 'L'},
        {"log-dir",   required_argument, nullptr, 'D'},
        {"verbose",   no_argument,       nullptr, 'v'},
        {"help",      no_argument,       nullptr, 'h'},
        {"version",   no_argument,       nullptr, 'V'},
        {nullptr, 0, nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "c:p:vhV", long_opts, nullptr)) != -1) {
        switch (opt) {
            case 'c': config_file   = optarg; break;
            case 'p': cli_port      = atoi(optarg); break;
            case 'S': cli_ssl_port  = atoi(optarg); break;
            case 'W': cli_ws_port   = atoi(optarg); break;
            case 'L': cli_log_level = atoi(optarg); break;
            case 'D': cli_log_dir   = optarg; break;
            case 'v': cli_verbose   = true; break;
            case 'V':
                fprintf(stdout, "mcaster1-voictune %s\n", VT_VERSION);
                return 0;
            case 'h':
            default:
                print_usage(argv[0]);
                return (opt == 'h') ? 0 : 1;
        }
    }

    if (!config_file) {
        fprintf(stderr, "[voictune] ERROR: --config is required\n");
        print_usage(argv[0]);
        return 1;
    }

    /* ── Load YAML config ────────────────────────────────────────────── */
    mc1vt::VtConfig cfg;
    if (!mc1vt::vt_load_config(config_file, cfg)) {
        fprintf(stderr, "[voictune] ERROR: Failed to load config from %s\n", config_file);
        return 1;
    }

    /* ── Apply CLI overrides (CLI wins over YAML) ────────────────────── */
    if (cli_port > 0)      cfg.http.port     = cli_port;
    if (cli_ssl_port > 0)  cfg.http.ssl_port = cli_ssl_port;
    if (cli_ws_port > 0)   cfg.websocket.port = cli_ws_port;
    if (cli_log_dir)       cfg.log.dir        = cli_log_dir;
    if (cli_log_level > 0) cfg.log.level      = cli_log_level;
    if (cli_verbose)       cfg.log.level      = 5;

    /* ── Initialize logger ───────────────────────────────────────────── */
    auto& logger = mc1vt::VtLogger::instance();
    logger.set_log_dir(cfg.log.dir);
    logger.set_level(cfg.log.level);

    VT_INFO(VT_BANNER);
    VT_INFO("Config loaded from: " + std::string(config_file));
    VT_INFO("HTTP port: " + std::to_string(cfg.http.port) +
            ", HTTPS port: " + std::to_string(cfg.http.ssl_port) +
            ", WS port: " + std::to_string(cfg.websocket.port));
    VT_INFO("Log level: " + std::to_string(cfg.log.level) +
            ", Log dir: " + cfg.log.dir);
    VT_INFO("Ollama endpoint: " + cfg.ollama.endpoint +
            ", model: " + cfg.ollama.model);

    /* ── Signal handling ─────────────────────────────────────────────── */
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGHUP,  SIG_IGN);
    signal(SIGPIPE, SIG_IGN);

    /* ── Phase VT-2 will add here:
     *    - PortAudio initialization + USB/BT device monitor
     *    - FFT worker pool startup
     *    - WebSocket server startup
     * ──────────────────────────────────────────────────────────────── */

    /* ── Start HTTP server (blocks until stop) ───────────────────────── */
    VT_INFO("Starting HTTP API server...");
    mc1vt::vt_http_start(cfg);

    /* ── Cleanup ─────────────────────────────────────────────────────── */
    VT_INFO("VoicTune daemon shutting down...");

    /* Phase VT-2 will add: PortAudio cleanup, worker pool join, WS shutdown */

    VT_INFO("Clean shutdown complete.");
    fprintf(stdout, "[voictune] Clean shutdown complete.\n");
    return 0;
}
