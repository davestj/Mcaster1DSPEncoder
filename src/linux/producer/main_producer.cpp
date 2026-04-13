/*
 * Mcaster1 Producer — Heavy Workload Daemon
 * producer/main_producer.cpp
 *
 * Entry point for the mcaster1-producer daemon. Handles video encoding,
 * multi-track audio mixdown, forensic FFT analysis, and other CPU-intensive
 * tasks offloaded from the live audio streaming encoder.
 *
 * Ports: 8360 (HTTP), 8364 (HTTPS)
 * Logs:  /var/log/mcaster1/producer.log, producer_error.log
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "pr_config.h"
#include "pr_http_api.h"
#include "pr_logger.h"
#include "pr_worker_pool.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <unistd.h>
#include <getopt.h>
#include <string>
#include <atomic>

#define PR_VERSION "1.8.0-beta.1"
#define PR_BANNER  "Mcaster1 Producer v" PR_VERSION " — Heavy Workload Daemon"

static std::atomic<bool> g_shutdown{false};

static void signal_handler(int sig)
{
    fprintf(stderr, "\n[producer] Caught signal %d — shutting down\n", sig);
    g_shutdown.store(true);
    mc1pr::pr_http_stop();
}

static void print_usage(const char* progname)
{
    fprintf(stderr,
        PR_BANNER "\n\n"
        "Usage: %s [OPTIONS]\n\n"
        "Options:\n"
        "  -c, --config FILE      YAML config file (required)\n"
        "  -p, --port PORT        Override HTTP port (default: 8360)\n"
        "      --ssl-port PORT    Override HTTPS port (default: 8364)\n"
        "      --log-level N      Log level 1-5 (1=CRIT, 5=DEBUG)\n"
        "      --log-dir DIR      Log directory (default: /var/log/mcaster1)\n"
        "  -v, --verbose          Set log level to 5 (DEBUG)\n"
        "  -h, --help             Show this help\n"
        "  -V, --version          Show version\n"
        "\n"
        "Ports:\n"
        "  8360  HTTP  API (job management, status)\n"
        "  8364  HTTPS API (same routes, TLS)\n"
        "\n"
        "Job types:\n"
        "  video_encode   — ffmpeg transcode (WebM->MP4, resolution change, etc.)\n"
        "  audio_mixdown  — multi-track mix via ffmpeg amix\n"
        "  fft_analysis   — offline large FFT computation\n"
        "  thumbnail      — extract video frame\n"
        "  noise_reduce   — ffmpeg afftdn processing\n"
        "\n"
        "Example:\n"
        "  %s --config src/linux/config/mcaster1_producer.yaml -v\n"
        "\n",
        progname, progname);
}

int main(int argc, char* argv[])
{
    fprintf(stdout, PR_BANNER "\n");

    /* -- CLI option parsing ------------------------------------------------ */
    const char* config_file = nullptr;
    int cli_port      = 0;
    int cli_ssl_port  = 0;
    int cli_log_level = 0;
    const char* cli_log_dir = nullptr;
    bool cli_verbose  = false;

    static struct option long_opts[] = {
        {"config",    required_argument, nullptr, 'c'},
        {"port",      required_argument, nullptr, 'p'},
        {"ssl-port",  required_argument, nullptr, 'S'},
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
            case 'L': cli_log_level = atoi(optarg); break;
            case 'D': cli_log_dir   = optarg; break;
            case 'v': cli_verbose   = true; break;
            case 'V':
                fprintf(stdout, "mcaster1-producer %s\n", PR_VERSION);
                return 0;
            case 'h':
            default:
                print_usage(argv[0]);
                return (opt == 'h') ? 0 : 1;
        }
    }

    if (!config_file) {
        fprintf(stderr, "[producer] ERROR: --config is required\n");
        print_usage(argv[0]);
        return 1;
    }

    /* -- Load YAML config -------------------------------------------------- */
    mc1pr::PrConfig cfg;
    if (!mc1pr::pr_load_config(config_file, cfg)) {
        fprintf(stderr, "[producer] ERROR: Failed to load config from %s\n", config_file);
        return 1;
    }

    /* -- Apply CLI overrides (CLI wins over YAML) -------------------------- */
    if (cli_port > 0)      cfg.http.port     = cli_port;
    if (cli_ssl_port > 0)  cfg.http.ssl_port = cli_ssl_port;
    if (cli_log_dir)       cfg.log.dir        = cli_log_dir;
    if (cli_log_level > 0) cfg.log.level      = cli_log_level;
    if (cli_verbose)       cfg.log.level      = 5;

    /* -- Initialize logger ------------------------------------------------- */
    auto& logger = mc1pr::PrLogger::instance();
    logger.set_log_dir(cfg.log.dir);
    logger.set_level(cfg.log.level);

    PR_INFO(PR_BANNER);
    PR_INFO("Config loaded from: " + std::string(config_file));
    PR_INFO("HTTP port: " + std::to_string(cfg.http.port) +
            ", HTTPS port: " + std::to_string(cfg.http.ssl_port));
    PR_INFO("Log level: " + std::to_string(cfg.log.level) +
            ", Log dir: " + cfg.log.dir);
    PR_INFO("Worker threads: video=" + std::to_string(cfg.workers.video_threads) +
            " audio=" + std::to_string(cfg.workers.audio_threads) +
            " fft=" + std::to_string(cfg.workers.fft_threads));

    /* -- Signal handling --------------------------------------------------- */
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGHUP,  SIG_IGN);
    signal(SIGPIPE, SIG_IGN);

    /* -- Initialize worker pool -------------------------------------------- */
    mc1pr::ProducerWorkerPool worker_pool(
        cfg.workers.video_threads,
        cfg.workers.audio_threads,
        cfg.workers.fft_threads);

    mc1pr::pr_set_worker_pool(&worker_pool);

    /* -- Start HTTP server (blocks until stop) ----------------------------- */
    PR_INFO("Starting HTTP API server...");
    mc1pr::pr_http_start(cfg);

    /* -- Cleanup ----------------------------------------------------------- */
    PR_INFO("Producer daemon shutting down...");

    worker_pool.stop();
    mc1pr::pr_set_worker_pool(nullptr);

    PR_INFO("Clean shutdown complete.");
    fprintf(stdout, "[producer] Clean shutdown complete.\n");
    return 0;
}
