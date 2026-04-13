/*
 * Mcaster1 Producer — Configuration
 * producer/pr_config.h
 *
 * YAML config structure for the Producer daemon.
 * Handles video encoding, audio mixdown, forensic FFT, and
 * other heavy workloads offloaded from the live encoder.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <string>

namespace mc1pr {

struct PrConfig {
    /* HTTP/HTTPS server */
    struct Http {
        int         port     = 8360;
        int         ssl_port = 8364;
        std::string bind     = "0.0.0.0";
        std::string ssl_cert;
        std::string ssl_key;
    } http;

    /* Worker thread pools */
    struct Workers {
        int video_threads = 2;
        int audio_threads = 2;
        int fft_threads   = 1;
    } workers;

    /* Auth */
    struct Auth {
        std::string username = "admin";
        std::string password = "changeme";
        std::string api_token;
        int         session_timeout_sec = 3600;
    } auth;

    /* Logging */
    struct Log {
        std::string dir   = "/var/log/mcaster1";
        int         level = 4;  /* 1=CRIT..5=DEBUG */
    } log;
};

/* Load PrConfig from a YAML file. Returns false on error. */
bool pr_load_config(const std::string& yaml_path, PrConfig& cfg);

} // namespace mc1pr
