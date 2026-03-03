/*
 * Mcaster1DSPEncoder — Process Supervisor
 * process_supervisor.h
 *
 * We manage the encoder child process lifecycle: fork/exec, watchdog, restart on crash.
 * The admin binary uses this to supervise the encoder binary for fault isolation.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <ctime>
#include <sys/types.h>

class ProcessSupervisor {
public:
    struct Config {
        std::string binary_path;          // Path to encoder binary
        std::vector<std::string> args;    // Command line args (--config, --ipc-port, etc.)
        int    restart_delay_sec = 5;     // Seconds to wait before restart after crash
        int    max_restarts      = 0;     // 0 = unlimited
        int    watchdog_interval_sec = 2; // How often to check child is alive
        std::string crash_log_path;       // Where to log crash events
    };

    enum class ChildState {
        STOPPED,       // Not running, not started
        RUNNING,       // Child is alive
        CRASHED,       // Child died unexpectedly, pending restart
        RESTARTING,    // Waiting restart_delay before re-launch
        FAILED,        // Max restarts exceeded
        STOPPING       // Graceful shutdown requested
    };

    struct Status {
        ChildState state        = ChildState::STOPPED;
        pid_t      pid          = 0;
        int        restart_count = 0;
        int        last_exit_code = 0;
        int        last_signal   = 0;
        time_t     started_at   = 0;
        time_t     crashed_at   = 0;
        std::string last_crash_reason;
    };

    using CrashCallback = std::function<void(const Status&)>;

    ProcessSupervisor() = default;
    ~ProcessSupervisor();

    /* We configure the supervisor (must call before start) */
    void configure(const Config& cfg) { cfg_ = cfg; }

    /* We set a callback that fires on child crash (for logging/UI notification) */
    void set_crash_callback(CrashCallback cb) { crash_cb_ = cb; }

    /* We start the child process and begin watchdog monitoring */
    bool start();

    /* We stop the child process gracefully (SIGTERM, then SIGKILL after timeout) */
    void stop();

    /* We restart the child process (stop + start) */
    bool restart();

    /* We get the current supervisor status (thread-safe) */
    Status status() const;

    /* We check if the child is currently alive */
    bool is_child_alive() const { return state_.load() == ChildState::RUNNING; }

    /* We get the child PID (0 if not running) */
    pid_t child_pid() const { return child_pid_.load(); }

private:
    Config       cfg_;
    CrashCallback crash_cb_;

    std::atomic<ChildState> state_{ChildState::STOPPED};
    std::atomic<pid_t>      child_pid_{0};
    std::atomic<bool>       watchdog_running_{false};
    std::thread             watchdog_thread_;
    mutable std::mutex      mtx_;

    int    restart_count_ = 0;
    int    last_exit_code_ = 0;
    int    last_signal_    = 0;
    time_t started_at_    = 0;
    time_t crashed_at_    = 0;
    std::string last_crash_reason_;

    /* We fork/exec the child process */
    bool launch_child();

    /* We run the watchdog loop (checks child health every N seconds) */
    void watchdog_loop();

    /* We handle a child death event */
    void on_child_died(int wstatus);

    /* We log a crash event to the crash log file */
    void log_crash(const std::string& reason, int exit_code, int signal);
};
