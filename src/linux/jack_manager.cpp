/*
 * Mcaster1DSPEncoder — JACK Audio Manager Implementation
 * jack_manager.cpp
 *
 * We manage JACK daemon lifecycle and virtual audio cable ports.
 * On headless servers we start jackd with the dummy driver.
 * On desktop Linux we auto-detect ALSA or PulseAudio.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "jack_manager.h"
#include <cstdlib>
#include <cstdio>
#include <fcntl.h>
#include <spawn.h>
#include <unistd.h>
#include <cstring>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

JackManager::~JackManager() {
    disconnect_client();
    stop_daemon();
}

/* ── Daemon lifecycle ──────────────────────────────────────────────────────── */

bool JackManager::start_daemon(const std::string& driver, int sample_rate, int buffer_size) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (daemon_started_) return true;

#ifdef HAVE_JACK
    driver_ = driver;

    /* Use posix_spawn instead of fork — safe from multi-threaded process */
    {
        /* Kill any existing jackd */
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
        posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);

        char sr_str[16], bs_str[16];
        snprintf(sr_str, sizeof(sr_str), "%d", sample_rate);
        snprintf(bs_str, sizeof(bs_str), "%d", buffer_size);

        const char* argv[] = {
            "jackd", "--no-realtime",
            "-d", driver.c_str(),
            "-r", sr_str,
            "-p", bs_str,
            nullptr
        };

        pid_t child_pid = 0;
        int rc = posix_spawnp(&child_pid, "jackd",
                              &fa, nullptr,
                              const_cast<char* const*>(argv),
                              environ);
        posix_spawn_file_actions_destroy(&fa);

        if (rc != 0) return false;
        daemon_pid_ = child_pid;
    }

    /* Wait for jackd to initialize */
    usleep(1500000); /* 1.5s */

    /* Verify it's still running */
    if (daemon_pid_ > 0 && kill(daemon_pid_, 0) == 0) {
        daemon_started_ = true;
        return true;
    }

    daemon_pid_ = 0;
    return false;
#else
    return false;
#endif
}

void JackManager::stop_daemon() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!daemon_started_) return;

    /* Use pkill for reliable stop — jackd may have been started via system() */
    system("pkill -x jackd 2>/dev/null");
    usleep(500000);
    system("pkill -9 -x jackd 2>/dev/null");
    daemon_pid_ = 0;
    daemon_started_ = false;
}

bool JackManager::is_daemon_running() const {
    if (!daemon_started_ || daemon_pid_ <= 0) return false;
    int status;
    return waitpid(daemon_pid_, &status, WNOHANG) == 0;
}

/* ── Client connection ─────────────────────────────────────────────────────── */

bool JackManager::connect_client() {
#ifdef HAVE_JACK
    std::lock_guard<std::mutex> lk(mtx_);
    if (client_) return true;

    jack_status_t status;
    client_ = jack_client_open("mc1encoder", JackNoStartServer, &status);
    if (!client_) return false;

    jack_activate(client_);
    return true;
#else
    return false;
#endif
}

void JackManager::disconnect_client() {
#ifdef HAVE_JACK
    std::lock_guard<std::mutex> lk(mtx_);
    if (!client_) return;

    /* We unregister all cable ports */
    for (auto& c : cables_) {
        if (c.capture)  jack_port_unregister(client_, c.capture);
        if (c.playback) jack_port_unregister(client_, c.playback);
    }
    cables_.clear();

    jack_deactivate(client_);
    jack_client_close(client_);
    client_ = nullptr;
#endif
}

bool JackManager::is_client_connected() const {
#ifdef HAVE_JACK
    return client_ != nullptr;
#else
    return false;
#endif
}

/* ── Virtual audio cable management ────────────────────────────────────────── */

int JackManager::create_cable() {
#ifdef HAVE_JACK
    std::lock_guard<std::mutex> lk(mtx_);
    if (!client_) return -1;

    int id = next_cable_id_++;
    char cap_name[64], play_name[64];
    snprintf(cap_name, sizeof(cap_name), "mc1_cable_%02d_capture", id);
    snprintf(play_name, sizeof(play_name), "mc1_cable_%02d_playback", id);

    jack_port_t* cap = jack_port_register(client_, cap_name,
        JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    jack_port_t* play = jack_port_register(client_, play_name,
        JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);

    if (!cap || !play) {
        if (cap)  jack_port_unregister(client_, cap);
        if (play) jack_port_unregister(client_, play);
        return -1;
    }

    cables_.push_back({id, cap, play});
    return id;
#else
    return -1;
#endif
}

bool JackManager::destroy_cable(int cable_id) {
#ifdef HAVE_JACK
    std::lock_guard<std::mutex> lk(mtx_);
    if (!client_) return false;

    for (auto it = cables_.begin(); it != cables_.end(); ++it) {
        if (it->id == cable_id) {
            if (it->capture)  jack_port_unregister(client_, it->capture);
            if (it->playback) jack_port_unregister(client_, it->playback);
            cables_.erase(it);
            return true;
        }
    }
    return false;
#else
    return false;
#endif
}

std::vector<JackManager::PortPair> JackManager::list_cables() const {
    std::vector<PortPair> result;
#ifdef HAVE_JACK
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto& c : cables_) {
        PortPair pp;
        pp.id = c.id;
        pp.capture_name  = c.capture  ? jack_port_name(c.capture)  : "";
        pp.playback_name = c.playback ? jack_port_name(c.playback) : "";
        result.push_back(pp);
    }
#endif
    return result;
}

void JackManager::create_cables(int count) {
    for (int i = 0; i < count; ++i) {
        create_cable();
    }
}

/* ── Port discovery ────────────────────────────────────────────────────────── */

std::vector<JackManager::PortInfo> JackManager::list_ports() const {
    std::vector<PortInfo> result;
#ifdef HAVE_JACK
    std::lock_guard<std::mutex> lk(mtx_);
    if (!client_) return result;

    const char** ports = jack_get_ports(client_, nullptr, nullptr, 0);
    if (!ports) return result;

    for (int i = 0; ports[i]; ++i) {
        jack_port_t* p = jack_port_by_name(client_, ports[i]);
        if (!p) continue;
        int flags = jack_port_flags(p);
        PortInfo pi;
        pi.name        = ports[i];
        pi.is_input    = (flags & JackPortIsInput) != 0;
        pi.is_output   = (flags & JackPortIsOutput) != 0;
        pi.is_physical = (flags & JackPortIsPhysical) != 0;
        result.push_back(pi);
    }
    jack_free(ports);
#endif
    return result;
}

std::vector<JackManager::Connection> JackManager::list_connections() const {
    std::vector<Connection> result;
#ifdef HAVE_JACK
    std::lock_guard<std::mutex> lk(mtx_);
    if (!client_) return result;

    const char** ports = jack_get_ports(client_, nullptr, nullptr, JackPortIsOutput);
    if (!ports) return result;

    for (int i = 0; ports[i]; ++i) {
        jack_port_t* p = jack_port_by_name(client_, ports[i]);
        if (!p) continue;
        const char** conns = jack_port_get_all_connections(client_, p);
        if (!conns) continue;
        for (int j = 0; conns[j]; ++j) {
            result.push_back({ports[i], conns[j]});
        }
        jack_free(conns);
    }
    jack_free(ports);
#endif
    return result;
}

bool JackManager::connect_ports(const std::string& src, const std::string& dst) {
#ifdef HAVE_JACK
    std::lock_guard<std::mutex> lk(mtx_);
    if (!client_) return false;
    return jack_connect(client_, src.c_str(), dst.c_str()) == 0;
#else
    return false;
#endif
}

bool JackManager::disconnect_ports(const std::string& src, const std::string& dst) {
#ifdef HAVE_JACK
    std::lock_guard<std::mutex> lk(mtx_);
    if (!client_) return false;
    return jack_disconnect(client_, src.c_str(), dst.c_str()) == 0;
#else
    return false;
#endif
}

/* ── Info ──────────────────────────────────────────────────────────────────── */

int JackManager::sample_rate() const {
#ifdef HAVE_JACK
    if (client_) return jack_get_sample_rate(client_);
#endif
    return 0;
}

int JackManager::buffer_size() const {
#ifdef HAVE_JACK
    if (client_) return jack_get_buffer_size(client_);
#endif
    return 0;
}
