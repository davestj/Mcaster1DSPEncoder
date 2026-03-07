/*
 * Mcaster1DSPEncoder — JACK Audio Manager
 * jack_manager.h
 *
 * We manage the JACK audio daemon lifecycle, virtual audio cable port pairs,
 * and port connections. Supports headless (dummy driver) and desktop (ALSA) modes.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#ifdef HAVE_JACK
#include <jack/jack.h>
#endif

#include <vector>
#include <string>
#include <mutex>
#include <atomic>

class JackManager {
public:
    struct PortPair {
        int         id;
        std::string capture_name;   // e.g. "mc1_cable_01_capture"
        std::string playback_name;  // e.g. "mc1_cable_01_playback"
    };

    struct PortInfo {
        std::string name;
        bool        is_input;
        bool        is_output;
        bool        is_physical;
    };

    struct Connection {
        std::string src;
        std::string dst;
    };

    JackManager() = default;
    ~JackManager();

    /* ── Daemon lifecycle ───────────────────────────────────────────────── */

    // We start the JACK daemon with the specified driver
    bool start_daemon(const std::string& driver = "dummy",
                      int sample_rate = 44100, int buffer_size = 1024);
    void stop_daemon();

    // We connect as a JACK client (call after daemon is running)
    bool connect_client();
    void disconnect_client();

    bool is_daemon_running() const;
    bool is_client_connected() const;

    /* ── Virtual audio cable management ─────────────────────────────────── */

    // We create a virtual audio cable pair (capture + playback ports)
    int  create_cable();
    bool destroy_cable(int cable_id);
    std::vector<PortPair> list_cables() const;

    // We create N cables at once
    void create_cables(int count);

    /* ── Port discovery + connections ───────────────────────────────────── */

    std::vector<PortInfo>   list_ports() const;
    std::vector<Connection> list_connections() const;
    bool connect_ports(const std::string& src, const std::string& dst);
    bool disconnect_ports(const std::string& src, const std::string& dst);

    /* ── Info ────────────────────────────────────────────────────────────── */

    int  sample_rate() const;
    int  buffer_size() const;
    std::string driver() const { return driver_; }

private:
#ifdef HAVE_JACK
    jack_client_t* client_ = nullptr;
    struct Cable {
        int          id;
        jack_port_t* capture  = nullptr;
        jack_port_t* playback = nullptr;
    };
    std::vector<Cable> cables_;
#endif
    mutable std::mutex mtx_;
    int  next_cable_id_ = 1;
    bool daemon_started_ = false;
    std::string driver_;
    pid_t daemon_pid_ = 0;
};
