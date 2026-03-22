/*
 * Mcaster1 VoicTune — Configuration
 * voictune/vt_config.h
 *
 * YAML config structure for the VoicTune daemon.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <string>
#include <vector>

namespace mc1vt {

struct VtConfig {
    /* HTTP/HTTPS server */
    struct Http {
        int         port          = 8350;
        int         ssl_port      = 8354;
        std::string bind          = "0.0.0.0";
        std::string ssl_cert;
        std::string ssl_key;
    } http;

    /* Audio capture */
    struct Audio {
        int         input_device_index  = -1;   /* -1 = system default */
        int         output_device_index = -1;   /* -1 = system default */
        int         sample_rate         = 48000;
        int         channels            = 1;
        int         buffer_frames       = 1024;
        std::string preferred_usb_id;           /* Sticky USB VID:PID */
        std::string preferred_bt_addr;          /* Sticky BT MAC */
        bool        hotplug_enabled     = true;
        int         hotplug_settle_ms   = 500;
        bool        monitor_output      = false;
    } audio;

    /* WebSocket server for browser mic */
    struct WebSocket {
        int port         = 8355;
        int max_clients  = 4;
    } websocket;

    /* FFT analysis */
    struct Analysis {
        int fft_size       = 4096;
        int hop_size       = 1024;
        int lufs_window_ms = 400;
    } analysis;

    /* Ollama AI */
    struct Ollama {
        std::string endpoint = "http://127.0.0.1:11434";
        std::string model    = "llama3.2";
        int         timeout_sec = 30;
    } ollama;

    /* Database */
    struct Database {
        std::string host    = "127.0.0.1";
        int         port    = 3306;
        std::string user;
        std::string password;
        std::string db_name = "mcaster1_voictune";
    } database;

    /* Logging */
    struct Log {
        std::string dir   = "/var/log/mcaster1";
        int         level = 4;  /* 1=CRIT..5=DEBUG */
    } log;

    /* Auth */
    struct Auth {
        std::string username = "admin";
        std::string password = "changeme";
        std::string api_token;
        int         session_timeout_sec = 3600;
    } auth;

    /* Worker pool */
    int worker_threads = 4;
};

/* We load VtConfig from a YAML file. Returns false on error. */
bool vt_load_config(const std::string& yaml_path, VtConfig& cfg);

} // namespace mc1vt
