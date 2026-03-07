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

    /* We fork a child process to run jackd */
    pid_t pid = fork();
    if (pid < 0) return false;

    if (pid == 0) {
        /* We are the child — exec jackd */
        char sr_str[16], bs_str[16];
        snprintf(sr_str, sizeof(sr_str), "%d", sample_rate);
        snprintf(bs_str, sizeof(bs_str), "%d", buffer_size);

        execlp("jackd", "jackd", "--no-realtime",
               "-d", driver.c_str(),
               "-r", sr_str,
               "-p", bs_str,
               (char*)nullptr);
        /* If execlp fails, exit child */
        _exit(127);
    }

    /* We are the parent — wait a bit for jackd to initialize */
    daemon_pid_ = pid;
    usleep(1500000);  // 1.5 seconds

    /* We verify jackd is still running */
    int status;
    pid_t result = waitpid(pid, &status, WNOHANG);
    if (result != 0) {
        /* jackd exited already — startup failed */
        daemon_pid_ = 0;
        return false;
    }

    daemon_started_ = true;
    return true;
#else
    return false;
#endif
}

void JackManager::stop_daemon() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!daemon_started_) return;

    if (daemon_pid_ > 0) {
        kill(daemon_pid_, SIGTERM);
        usleep(500000);
        int status;
        if (waitpid(daemon_pid_, &status, WNOHANG) == 0) {
            kill(daemon_pid_, SIGKILL);
            waitpid(daemon_pid_, &status, 0);
        }
        daemon_pid_ = 0;
    }
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
