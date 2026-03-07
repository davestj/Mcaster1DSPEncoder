/*
 * Mcaster1DSPEncoder — Clockwheel Scheduler
 * clockwheel_scheduler.h
 *
 * Background thread that reads clock_hours from the DB and switches playlists
 * on encoder slots at hour boundaries. Supports per-slot schedules and manual
 * overrides. Same thread pattern as ServerMonitor (1s sleep loop).
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <atomic>
#include <ctime>

class AudioPipeline;

struct ClockHourEntry {
    int         id            = 0;
    int         slot_id       = 0;
    int         hour          = 0;    // 0-23
    int         day_of_week   = -1;   // -1 = every day, 0=Sun, 6=Sat
    std::string name;
    int         playlist_id   = 0;
    std::string playlist_path;
    bool        is_active     = true;
};

struct SlotOverride {
    std::string playlist_path;
    time_t      set_at = 0;
    bool        active = false;
};

class ClockwheelScheduler {
public:
    static ClockwheelScheduler& instance() {
        static ClockwheelScheduler inst;
        return inst;
    }

    /* We start the scheduler background thread */
    void start(AudioPipeline* pipeline);

    /* We stop the scheduler */
    void stop();

    bool is_running() const { return running_.load(); }

    /* We get the current schedule for a slot (all 24 hours) */
    std::vector<ClockHourEntry> get_schedule(int slot_id) const;

    /* We get all schedules across all slots */
    std::vector<ClockHourEntry> get_all_schedules() const;

    /* We save a clock hour assignment */
    bool save_assignment(int slot_id, int hour, int day_of_week,
                         const std::string& playlist_path, int playlist_id = 0,
                         const std::string& name = "");

    /* We delete a clock hour assignment */
    bool delete_assignment(int id);

    /* We set a manual override for a slot (takes priority over schedule) */
    void set_override(int slot_id, const std::string& playlist_path);

    /* We clear the override for a slot (resume schedule) */
    void clear_override(int slot_id);

    /* We check if a slot has an active override */
    bool has_override(int slot_id) const;

    /* We get the current hour being served per slot */
    int current_hour() const { return current_hour_; }

    /* We force-reload the schedule from DB */
    void reload_schedule();

    ClockwheelScheduler(const ClockwheelScheduler&) = delete;
    ClockwheelScheduler& operator=(const ClockwheelScheduler&) = delete;

private:
    ClockwheelScheduler() = default;
    ~ClockwheelScheduler() { stop(); }

    void run();
    void load_from_db();
    void check_hour_change();
    void apply_playlist(int slot_id, const std::string& path);

    AudioPipeline*          pipeline_ = nullptr;
    std::atomic<bool>       running_{false};
    std::thread             thread_;
    mutable std::mutex      mtx_;

    /* We cache the schedule keyed by (slot_id, hour) */
    std::vector<ClockHourEntry> schedule_;
    std::map<int, SlotOverride> overrides_;  // slot_id → override

    int    current_hour_ = -1;
    int    current_dow_  = -1;  // day of week
    time_t last_db_load_ = 0;
};
