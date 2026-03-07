/*
 * Mcaster1DSPEncoder — Clockwheel Scheduler Implementation
 * clockwheel_scheduler.cpp
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "clockwheel_scheduler.h"
#include "audio_pipeline.h"
#include "mc1_logger.h"
#include "mc1_db.h"

#include <chrono>
#include <cstring>
#include <cstdlib>

void ClockwheelScheduler::start(AudioPipeline* pipeline) {
    if (running_.load()) return;
    pipeline_ = pipeline;
    running_.store(true);
    current_hour_ = -1;
    load_from_db();
    thread_ = std::thread(&ClockwheelScheduler::run, this);
    MC1_INFO("[Clockwheel] Scheduler started");
}

void ClockwheelScheduler::stop() {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
    MC1_INFO("[Clockwheel] Scheduler stopped");
}

void ClockwheelScheduler::run() {
    while (running_.load()) {
        check_hour_change();

        /* We reload from DB every 5 minutes */
        time_t now = time(nullptr);
        if (now - last_db_load_ >= 300) {
            load_from_db();
        }

        /* We sleep 1 second with responsive shutdown check */
        for (int i = 0; i < 10 && running_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void ClockwheelScheduler::load_from_db() {
    auto rows = Mc1Db::instance().query(
        "SELECT id, slot_id, hour, day_of_week, name, playlist_id, playlist_path, is_active "
        "FROM mcaster1_media.clock_hours WHERE is_active = 1 ORDER BY slot_id, hour");

    std::lock_guard<std::mutex> lk(mtx_);
    schedule_.clear();
    for (const auto& r : rows) {
        ClockHourEntry e;
        e.id            = std::atoi(r.at("id").c_str());
        e.slot_id       = std::atoi(r.at("slot_id").c_str());
        e.hour          = std::atoi(r.at("hour").c_str());
        auto dow_it = r.find("day_of_week");
        e.day_of_week   = (dow_it != r.end() && !dow_it->second.empty()) ? std::atoi(dow_it->second.c_str()) : -1;
        auto name_it = r.find("name");
        e.name          = (name_it != r.end()) ? name_it->second : "";
        auto pid_it = r.find("playlist_id");
        e.playlist_id   = (pid_it != r.end() && !pid_it->second.empty()) ? std::atoi(pid_it->second.c_str()) : 0;
        auto pp_it = r.find("playlist_path");
        e.playlist_path = (pp_it != r.end()) ? pp_it->second : "";
        e.is_active     = true;
        schedule_.push_back(e);
    }

    last_db_load_ = time(nullptr);
    MC1_DBG("[Clockwheel] Loaded " + std::to_string(schedule_.size()) + " schedule entries from DB");
}

void ClockwheelScheduler::check_hour_change() {
    time_t now = time(nullptr);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    int hour = tm_now.tm_hour;
    int dow  = tm_now.tm_wday;

    if (hour == current_hour_ && dow == current_dow_) return;

    MC1_INFO("[Clockwheel] Hour change: " + std::to_string(current_hour_) +
             " -> " + std::to_string(hour) + " (dow=" + std::to_string(dow) + ")");
    current_hour_ = hour;
    current_dow_  = dow;

    std::lock_guard<std::mutex> lk(mtx_);
    std::map<int, std::string> slot_playlists;

    for (const auto& e : schedule_) {
        if (e.hour != hour) continue;
        if (e.day_of_week >= 0 && e.day_of_week != dow) continue;
        if (e.playlist_path.empty()) continue;

        auto ov = overrides_.find(e.slot_id);
        if (ov != overrides_.end() && ov->second.active) {
            MC1_INFO("[Clockwheel] Slot " + std::to_string(e.slot_id) + " has override — skipping");
            continue;
        }
        slot_playlists[e.slot_id] = e.playlist_path;
    }

    for (auto& [slot_id, path] : slot_playlists) {
        MC1_INFO("[Clockwheel] Loading playlist for slot " + std::to_string(slot_id) +
                 " hour " + std::to_string(hour) + ": " + path);
        apply_playlist(slot_id, path);
    }
}

void ClockwheelScheduler::apply_playlist(int slot_id, const std::string& path) {
    if (!pipeline_) return;
    EncoderConfig cfg;
    if (!pipeline_->get_slot_config(slot_id, cfg)) {
        MC1_WARN("[Clockwheel] Slot " + std::to_string(slot_id) + " not found");
        return;
    }
    if (!cfg.clockwheel_enabled) {
        MC1_DBG("[Clockwheel] Slot " + std::to_string(slot_id) + " clockwheel disabled");
        return;
    }
    bool ok = pipeline_->load_playlist(slot_id, path);
    MC1_INFO("[Clockwheel] Playlist load " + std::string(ok ? "OK" : "FAILED") +
             ": slot " + std::to_string(slot_id) + " -> " + path);
}

std::vector<ClockHourEntry> ClockwheelScheduler::get_schedule(int slot_id) const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<ClockHourEntry> result;
    for (const auto& e : schedule_) {
        if (e.slot_id == slot_id) result.push_back(e);
    }
    return result;
}

std::vector<ClockHourEntry> ClockwheelScheduler::get_all_schedules() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return schedule_;
}

bool ClockwheelScheduler::save_assignment(int slot_id, int hour, int day_of_week,
                                           const std::string& playlist_path, int playlist_id,
                                           const std::string& name) {
    auto& db = Mc1Db::instance();
    std::string esc_name = db.escape(name);
    std::string esc_path = db.escape(playlist_path);
    char sql[1024];
    snprintf(sql, sizeof(sql),
        "INSERT INTO mcaster1_media.clock_hours (slot_id, hour, day_of_week, name, playlist_id, playlist_path, is_active) "
        "VALUES (%d, %d, %d, '%s', %d, '%s', 1) "
        "ON DUPLICATE KEY UPDATE name=VALUES(name), playlist_id=VALUES(playlist_id), "
        "playlist_path=VALUES(playlist_path), is_active=1",
        slot_id, hour, day_of_week, esc_name.c_str(), playlist_id, esc_path.c_str());

    bool ok = db.exec(sql);
    if (ok) load_from_db();
    return ok;
}

bool ClockwheelScheduler::delete_assignment(int id) {
    char sql[128];
    snprintf(sql, sizeof(sql), "DELETE FROM mcaster1_media.clock_hours WHERE id = %d", id);
    bool ok = Mc1Db::instance().exec(sql);
    if (ok) load_from_db();
    return ok;
}

void ClockwheelScheduler::set_override(int slot_id, const std::string& playlist_path) {
    std::lock_guard<std::mutex> lk(mtx_);
    overrides_[slot_id] = {playlist_path, time(nullptr), true};
    MC1_INFO("[Clockwheel] Override SET for slot " + std::to_string(slot_id) + ": " + playlist_path);
    if (pipeline_) pipeline_->load_playlist(slot_id, playlist_path);
}

void ClockwheelScheduler::clear_override(int slot_id) {
    std::lock_guard<std::mutex> lk(mtx_);
    overrides_.erase(slot_id);
    MC1_INFO("[Clockwheel] Override CLEARED for slot " + std::to_string(slot_id));
}

bool ClockwheelScheduler::has_override(int slot_id) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = overrides_.find(slot_id);
    return it != overrides_.end() && it->second.active;
}

void ClockwheelScheduler::reload_schedule() {
    load_from_db();
}
