/*
 * Mcaster1DSPEncoder — Process Supervisor Implementation
 * process_supervisor.cpp
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "process_supervisor.h"
#include "mc1_logger.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <cerrno>
#include <fstream>
#include <chrono>

ProcessSupervisor::~ProcessSupervisor() {
    stop();
}

bool ProcessSupervisor::start() {
    if (state_.load() == ChildState::RUNNING) return true;

    if (!launch_child()) return false;

    /* We start the watchdog thread */
    watchdog_running_.store(true);
    watchdog_thread_ = std::thread(&ProcessSupervisor::watchdog_loop, this);
    return true;
}

void ProcessSupervisor::stop() {
    state_.store(ChildState::STOPPING);
    watchdog_running_.store(false);

    pid_t pid = child_pid_.load();
    if (pid > 0) {
        /* We send SIGTERM first for graceful shutdown */
        kill(pid, SIGTERM);

        /* We wait up to 10 seconds for graceful exit */
        for (int i = 0; i < 20; ++i) {
            int wstatus;
            pid_t result = waitpid(pid, &wstatus, WNOHANG);
            if (result != 0) break;
            usleep(500000);  // 500ms
        }

        /* We force kill if still alive */
        int wstatus;
        if (waitpid(pid, &wstatus, WNOHANG) == 0) {
            kill(pid, SIGKILL);
            waitpid(pid, &wstatus, 0);
        }
        child_pid_.store(0);
    }

    if (watchdog_thread_.joinable()) {
        watchdog_thread_.join();
    }

    state_.store(ChildState::STOPPED);
}

bool ProcessSupervisor::restart() {
    MC1_INFO("[Supervisor] Manual restart requested");
    stop();
    restart_count_ = 0;  // Reset counter on manual restart
    return start();
}

ProcessSupervisor::Status ProcessSupervisor::status() const {
    std::lock_guard<std::mutex> lk(mtx_);
    Status s;
    s.state           = state_.load();
    s.pid             = child_pid_.load();
    s.restart_count   = restart_count_;
    s.last_exit_code  = last_exit_code_;
    s.last_signal     = last_signal_;
    s.started_at      = started_at_;
    s.crashed_at      = crashed_at_;
    s.last_crash_reason = last_crash_reason_;
    return s;
}

/* ── Fork/exec the encoder child process ───────────────────────────────────── */

bool ProcessSupervisor::launch_child() {
    if (cfg_.binary_path.empty()) {
        MC1_ERR("[Supervisor] No binary path configured");
        return false;
    }

    MC1_INFO("[Supervisor] Launching encoder: " + cfg_.binary_path);
    for (const auto& a : cfg_.args) {
        MC1_INFO("[Supervisor]   arg: " + a);
    }

    pid_t pid = fork();
    if (pid < 0) {
        MC1_ERR("[Supervisor] fork() failed: " + std::string(strerror(errno)));
        return false;
    }

    if (pid == 0) {
        /* ── Child process ──────────────────────────────────────────────── */

        /* We build the argv array for execv */
        std::vector<const char*> argv;
        argv.push_back(cfg_.binary_path.c_str());
        for (const auto& a : cfg_.args) {
            argv.push_back(a.c_str());
        }
        argv.push_back(nullptr);

        /* We exec the encoder binary */
        execv(cfg_.binary_path.c_str(), const_cast<char* const*>(argv.data()));

        /* If we get here, exec failed */
        fprintf(stderr, "[Supervisor] execv failed: %s\n", strerror(errno));
        _exit(127);
    }

    /* ── Parent process ─────────────────────────────────────────────────── */
    {
        std::lock_guard<std::mutex> lk(mtx_);
        child_pid_.store(pid);
        started_at_ = time(nullptr);
        state_.store(ChildState::RUNNING);
    }

    MC1_INFO("[Supervisor] Encoder child started: PID=" + std::to_string(pid));
    return true;
}

/* ── Watchdog loop ─────────────────────────────────────────────────────────── */

void ProcessSupervisor::watchdog_loop() {
    while (watchdog_running_.load()) {
        pid_t pid = child_pid_.load();

        if (pid > 0 && state_.load() == ChildState::RUNNING) {
            int wstatus;
            pid_t result = waitpid(pid, &wstatus, WNOHANG);

            if (result > 0) {
                /* Child has exited */
                on_child_died(wstatus);
            } else if (result < 0 && errno == ECHILD) {
                /* No such child — it's gone */
                on_child_died(0);
            }
        }

        /* We sleep for the watchdog interval */
        for (int i = 0; i < cfg_.watchdog_interval_sec * 10 && watchdog_running_.load(); ++i) {
            usleep(100000);  // 100ms granularity for responsive shutdown
        }
    }
}

/* ── Handle child death ────────────────────────────────────────────────────── */

void ProcessSupervisor::on_child_died(int wstatus) {
    int exit_code = 0;
    int sig       = 0;
    std::string reason;

    if (WIFEXITED(wstatus)) {
        exit_code = WEXITSTATUS(wstatus);
        reason = "Exited with code " + std::to_string(exit_code);
    } else if (WIFSIGNALED(wstatus)) {
        sig = WTERMSIG(wstatus);
        reason = "Killed by signal " + std::to_string(sig);
        /* We decode common crash signals */
        switch (sig) {
            case SIGSEGV: reason += " (SIGSEGV — segmentation fault, likely codec crash)"; break;
            case SIGABRT: reason += " (SIGABRT — assertion failure or abort())"; break;
            case SIGFPE:  reason += " (SIGFPE — floating point exception)"; break;
            case SIGBUS:  reason += " (SIGBUS — bus error, alignment fault)"; break;
            case SIGILL:  reason += " (SIGILL — illegal instruction)"; break;
            default: break;
        }
#ifdef WCOREDUMP
        if (WCOREDUMP(wstatus)) reason += " [core dumped]";
#endif
    } else {
        reason = "Unknown termination";
    }

    {
        std::lock_guard<std::mutex> lk(mtx_);
        last_exit_code_    = exit_code;
        last_signal_       = sig;
        crashed_at_        = time(nullptr);
        last_crash_reason_ = reason;
        child_pid_.store(0);
    }

    MC1_ERR("[Supervisor] Encoder died: " + reason);
    log_crash(reason, exit_code, sig);

    /* We notify the callback (for UI notification) */
    Status st = status();
    if (crash_cb_) {
        crash_cb_(st);
    }

    /* We check if we should auto-restart */
    if (state_.load() == ChildState::STOPPING) {
        state_.store(ChildState::STOPPED);
        return;
    }

    {
        std::lock_guard<std::mutex> lk(mtx_);
        restart_count_++;
    }

    if (cfg_.max_restarts > 0 && restart_count_ >= cfg_.max_restarts) {
        MC1_ERR("[Supervisor] Max restarts (" + std::to_string(cfg_.max_restarts) +
                ") exceeded — encoder will NOT be restarted");
        state_.store(ChildState::FAILED);
        return;
    }

    /* We wait before restarting */
    state_.store(ChildState::RESTARTING);
    MC1_INFO("[Supervisor] Restarting encoder in " + std::to_string(cfg_.restart_delay_sec) + "s" +
             " (restart #" + std::to_string(restart_count_) + ")");

    for (int i = 0; i < cfg_.restart_delay_sec * 10 && watchdog_running_.load(); ++i) {
        usleep(100000);
    }

    if (!watchdog_running_.load()) return;

    if (launch_child()) {
        MC1_INFO("[Supervisor] Encoder restarted successfully");
    } else {
        MC1_ERR("[Supervisor] Encoder restart FAILED");
        state_.store(ChildState::FAILED);
    }
}

/* ── Crash logging ─────────────────────────────────────────────────────────── */

void ProcessSupervisor::log_crash(const std::string& reason, int exit_code, int signal) {
    std::string path = cfg_.crash_log_path;
    if (path.empty()) path = "/var/log/mcaster1/encoder_crash.log";

    FILE* f = fopen(path.c_str(), "a");
    if (!f) return;

    time_t now = time(nullptr);
    char ts[64];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));

    fprintf(f, "[%s] CRASH: %s | exit=%d signal=%d restarts=%d\n",
            ts, reason.c_str(), exit_code, signal, restart_count_);
    fclose(f);
}
