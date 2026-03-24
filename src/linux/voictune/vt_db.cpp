/*
 * Mcaster1 VoicTune — Database Client
 * voictune/vt_db.cpp
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "vt_db.h"
#include "vt_logger.h"
#include <cstring>
#include <cstdlib>

namespace mc1vt {

VtDb::~VtDb() {
    disconnect();
}

bool VtDb::connect(const std::string& host, int port,
                   const std::string& user, const std::string& password,
                   const std::string& db_name) {
    host_ = host; port_ = port; user_ = user;
    password_ = password; db_name_ = db_name;

    conn_ = mysql_init(nullptr);
    if (!conn_) { VT_ERR("mysql_init failed"); return false; }

    mysql_options(conn_, MYSQL_OPT_RECONNECT, "\x01");

    if (!mysql_real_connect(conn_, host_.c_str(), user_.c_str(),
                            password_.c_str(), db_name_.c_str(),
                            port_, nullptr, 0)) {
        VT_ERR("DB connect failed: " + std::string(mysql_error(conn_)));
        mysql_close(conn_); conn_ = nullptr;
        return false;
    }

    mysql_set_character_set(conn_, "utf8mb4");
    exec("SET foreign_key_checks=0, unique_checks=0, sql_mode=''");
    VT_INFO("Connected to database " + db_name_);
    return true;
}

bool VtDb::connect_defaults(const std::string& db_name) {
    db_name_ = db_name;
    conn_ = mysql_init(nullptr);
    if (!conn_) { VT_ERR("mysql_init failed"); return false; }

    /* Read ~/.my.cnf for credentials */
    mysql_options(conn_, MYSQL_READ_DEFAULT_FILE, "~/.my.cnf");
    mysql_options(conn_, MYSQL_OPT_RECONNECT, "\x01");

    if (!mysql_real_connect(conn_, nullptr, nullptr, nullptr,
                            db_name_.c_str(), 0, nullptr, 0)) {
        VT_ERR("DB connect (defaults) failed: " + std::string(mysql_error(conn_)));
        mysql_close(conn_); conn_ = nullptr;
        return false;
    }

    mysql_set_character_set(conn_, "utf8mb4");
    exec("SET foreign_key_checks=0, unique_checks=0, sql_mode=''");
    VT_INFO("Connected to database " + db_name_ + " via defaults");
    return true;
}

void VtDb::disconnect() {
    if (conn_) { mysql_close(conn_); conn_ = nullptr; }
}

bool VtDb::reconnect() {
    disconnect();
    if (!host_.empty())
        return connect(host_, port_, user_, password_, db_name_);
    return connect_defaults(db_name_);
}

bool VtDb::ensure_connected() {
    if (conn_ && mysql_ping(conn_) == 0) return true;
    VT_WARN("DB connection lost, reconnecting...");
    return reconnect();
}

MYSQL_RES* VtDb::query(const std::string& sql) {
    if (!ensure_connected()) return nullptr;
    if (mysql_query(conn_, sql.c_str()) != 0) {
        VT_ERR("SQL query error: " + std::string(mysql_error(conn_))
               + " [" + sql.substr(0, 200) + "]");
        return nullptr;
    }
    return mysql_store_result(conn_);
}

bool VtDb::exec(const std::string& sql) {
    if (!ensure_connected()) return false;
    if (mysql_query(conn_, sql.c_str()) != 0) {
        VT_ERR("SQL exec error: " + std::string(mysql_error(conn_))
               + " [" + sql.substr(0, 200) + "]");
        return false;
    }
    return true;
}

std::string VtDb::escape(const std::string& s) {
    if (!conn_) return s;
    std::string out(s.size() * 2 + 1, '\0');
    unsigned long len = mysql_real_escape_string(conn_, &out[0], s.c_str(), s.size());
    out.resize(len);
    return out;
}

std::string VtDb::last_error() const {
    return conn_ ? mysql_error(conn_) : "not connected";
}

/* ── Sessions ──────────────────────────────────────────────────────────── */

int VtDb::create_session(int user_id, const std::string& name) {
    std::string sql = "INSERT INTO sessions (user_id, session_name) VALUES ("
        + std::to_string(user_id) + ", '" + escape(name) + "')";
    if (!exec(sql)) return -1;
    return static_cast<int>(mysql_insert_id(conn_));
}

bool VtDb::end_session(int session_id) {
    std::string sql = "UPDATE sessions SET ended_at = NOW(), "
        "duration_sec = TIMESTAMPDIFF(SECOND, started_at, NOW()) "
        "WHERE id = " + std::to_string(session_id);
    return exec(sql);
}

DbSession VtDb::get_session(int id) {
    DbSession s;
    auto* res = query("SELECT id, session_name, user_id, started_at, ended_at, duration_sec, notes "
                      "FROM sessions WHERE id = " + std::to_string(id));
    if (!res) return s;
    MYSQL_ROW row = mysql_fetch_row(res);
    if (row) {
        s.id           = atoi(row[0]);
        s.session_name = row[1] ? row[1] : "";
        s.user_id      = atoi(row[2]);
        s.started_at   = row[3] ? row[3] : "";
        s.ended_at     = row[4] ? row[4] : "";
        s.duration_sec = row[5] ? atoi(row[5]) : 0;
        s.notes        = row[6] ? row[6] : "";
    }
    mysql_free_result(res);
    return s;
}

std::vector<DbSession> VtDb::list_sessions(int user_id, int limit) {
    std::vector<DbSession> out;
    auto* res = query("SELECT id, session_name, user_id, started_at, ended_at, duration_sec, notes "
                      "FROM sessions WHERE user_id = " + std::to_string(user_id)
                      + " ORDER BY started_at DESC LIMIT " + std::to_string(limit));
    if (!res) return out;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        DbSession s;
        s.id           = atoi(row[0]);
        s.session_name = row[1] ? row[1] : "";
        s.user_id      = atoi(row[2]);
        s.started_at   = row[3] ? row[3] : "";
        s.ended_at     = row[4] ? row[4] : "";
        s.duration_sec = row[5] ? atoi(row[5]) : 0;
        s.notes        = row[6] ? row[6] : "";
        out.push_back(std::move(s));
    }
    mysql_free_result(res);
    return out;
}

/* ── Voice Profiles ────────────────────────────────────────────────────── */

int VtDb::create_profile(int user_id, const std::string& name) {
    std::string sql = "INSERT INTO voice_profiles (user_id, profile_name) VALUES ("
        + std::to_string(user_id) + ", '" + escape(name) + "')";
    if (!exec(sql)) return -1;
    return static_cast<int>(mysql_insert_id(conn_));
}

bool VtDb::update_profile(const DbVoiceProfile& p) {
    std::string sql = "UPDATE voice_profiles SET "
        "fundamental_hz = " + std::to_string(p.fundamental_hz) + ", "
        "voice_type = '" + escape(p.voice_type) + "', "
        "avg_lufs = " + std::to_string(p.avg_lufs) + ", "
        "avg_rms_db = " + std::to_string(p.avg_rms_db);
    if (!p.eq_preset_json.empty())
        sql += ", eq_preset_json = '" + escape(p.eq_preset_json) + "'";
    if (!p.effects_chain_json.empty())
        sql += ", effects_chain_json = '" + escape(p.effects_chain_json) + "'";
    if (!p.analysis_json.empty())
        sql += ", analysis_json = '" + escape(p.analysis_json) + "'";
    sql += " WHERE id = " + std::to_string(p.id);
    return exec(sql);
}

DbVoiceProfile VtDb::get_profile(int user_id, const std::string& name) {
    DbVoiceProfile p;
    auto* res = query("SELECT id, user_id, profile_name, fundamental_hz, voice_type, "
                      "avg_lufs, avg_rms_db, eq_preset_json, effects_chain_json, analysis_json "
                      "FROM voice_profiles WHERE user_id = " + std::to_string(user_id)
                      + " AND profile_name = '" + escape(name) + "'");
    if (!res) return p;
    MYSQL_ROW row = mysql_fetch_row(res);
    if (row) {
        p.id                 = atoi(row[0]);
        p.user_id            = atoi(row[1]);
        p.profile_name       = row[2] ? row[2] : "";
        p.fundamental_hz     = row[3] ? atof(row[3]) : 0.0f;
        p.voice_type         = row[4] ? row[4] : "unknown";
        p.avg_lufs           = row[5] ? atof(row[5]) : -96.0f;
        p.avg_rms_db         = row[6] ? atof(row[6]) : -96.0f;
        p.eq_preset_json     = row[7] ? row[7] : "";
        p.effects_chain_json = row[8] ? row[8] : "";
        p.analysis_json      = row[9] ? row[9] : "";
    }
    mysql_free_result(res);
    return p;
}

std::vector<DbVoiceProfile> VtDb::list_profiles(int user_id) {
    std::vector<DbVoiceProfile> out;
    auto* res = query("SELECT id, user_id, profile_name, fundamental_hz, voice_type, "
                      "avg_lufs, avg_rms_db FROM voice_profiles "
                      "WHERE user_id = " + std::to_string(user_id)
                      + " ORDER BY profile_name");
    if (!res) return out;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        DbVoiceProfile p;
        p.id             = atoi(row[0]);
        p.user_id        = atoi(row[1]);
        p.profile_name   = row[2] ? row[2] : "";
        p.fundamental_hz = row[3] ? atof(row[3]) : 0.0f;
        p.voice_type     = row[4] ? row[4] : "unknown";
        p.avg_lufs       = row[5] ? atof(row[5]) : -96.0f;
        p.avg_rms_db     = row[6] ? atof(row[6]) : -96.0f;
        out.push_back(std::move(p));
    }
    mysql_free_result(res);
    return out;
}

/* ── Analysis Snapshots ────────────────────────────────────────────────── */

bool VtDb::insert_snapshot(const DbAnalysisSnapshot& s) {
    std::string sql = "INSERT INTO analysis_snapshots "
        "(session_id, timestamp_ms, rms_db, peak_db, lufs, pitch_hz, note_name, cents_off, spectrum_json) "
        "VALUES (" + std::to_string(s.session_id) + ", "
        + std::to_string(s.timestamp_ms) + ", "
        + std::to_string(s.rms_db) + ", "
        + std::to_string(s.peak_db) + ", "
        + std::to_string(s.lufs) + ", "
        + std::to_string(s.pitch_hz) + ", "
        "'" + escape(s.note_name) + "', "
        + std::to_string(s.cents_off) + ", "
        "'" + escape(s.spectrum_json) + "')";
    return exec(sql);
}

bool VtDb::insert_snapshots_batch(const std::vector<DbAnalysisSnapshot>& batch) {
    if (batch.empty()) return true;
    std::string sql = "INSERT INTO analysis_snapshots "
        "(session_id, timestamp_ms, rms_db, peak_db, lufs, pitch_hz, note_name, cents_off, spectrum_json) VALUES ";
    for (size_t i = 0; i < batch.size(); ++i) {
        const auto& s = batch[i];
        if (i > 0) sql += ",";
        sql += "(" + std::to_string(s.session_id) + ","
             + std::to_string(s.timestamp_ms) + ","
             + std::to_string(s.rms_db) + ","
             + std::to_string(s.peak_db) + ","
             + std::to_string(s.lufs) + ","
             + std::to_string(s.pitch_hz) + ","
             "'" + escape(s.note_name) + "',"
             + std::to_string(s.cents_off) + ","
             "'" + escape(s.spectrum_json) + "')";
    }
    return exec(sql);
}

std::vector<DbAnalysisSnapshot> VtDb::get_snapshots(int session_id,
                                                     int64_t from_ms,
                                                     int64_t to_ms,
                                                     int limit) {
    std::vector<DbAnalysisSnapshot> out;
    std::string sql = "SELECT id, session_id, timestamp_ms, rms_db, peak_db, lufs, "
        "pitch_hz, note_name, cents_off, spectrum_json FROM analysis_snapshots "
        "WHERE session_id = " + std::to_string(session_id);
    if (from_ms > 0) sql += " AND timestamp_ms >= " + std::to_string(from_ms);
    if (to_ms < INT64_MAX) sql += " AND timestamp_ms <= " + std::to_string(to_ms);
    sql += " ORDER BY timestamp_ms LIMIT " + std::to_string(limit);

    auto* res = query(sql);
    if (!res) return out;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        DbAnalysisSnapshot s;
        s.id           = row[0] ? atoll(row[0]) : 0;
        s.session_id   = row[1] ? atoi(row[1]) : 0;
        s.timestamp_ms = row[2] ? atoll(row[2]) : 0;
        s.rms_db       = row[3] ? atof(row[3]) : -96.0f;
        s.peak_db      = row[4] ? atof(row[4]) : -96.0f;
        s.lufs         = row[5] ? atof(row[5]) : -96.0f;
        s.pitch_hz     = row[6] ? atof(row[6]) : 0.0f;
        s.note_name    = row[7] ? row[7] : "";
        s.cents_off    = row[8] ? atof(row[8]) : 0.0f;
        s.spectrum_json = row[9] ? row[9] : "";
        out.push_back(std::move(s));
    }
    mysql_free_result(res);
    return out;
}

/* ── AI Interactions ───────────────────────────────────────────────────── */

int VtDb::log_ai_interaction(const DbAiInteraction& ai) {
    std::string sql = "INSERT INTO ai_interactions "
        "(user_id, context, prompt_text, response_text, model_used, latency_ms) VALUES ("
        + std::to_string(ai.user_id) + ", "
        "'" + escape(ai.context) + "', "
        "'" + escape(ai.prompt_text) + "', "
        "'" + escape(ai.response_text) + "', "
        "'" + escape(ai.model_used) + "', "
        + std::to_string(ai.latency_ms) + ")";
    if (!exec(sql)) return -1;
    return static_cast<int>(mysql_insert_id(conn_));
}

std::vector<DbAiInteraction> VtDb::get_ai_history(int user_id,
                                                    const std::string& context,
                                                    int limit) {
    std::vector<DbAiInteraction> out;
    std::string sql = "SELECT id, user_id, context, prompt_text, response_text, model_used, latency_ms "
        "FROM ai_interactions WHERE user_id = " + std::to_string(user_id);
    if (!context.empty())
        sql += " AND context = '" + escape(context) + "'";
    sql += " ORDER BY created_at DESC LIMIT " + std::to_string(limit);

    auto* res = query(sql);
    if (!res) return out;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        DbAiInteraction ai;
        ai.id            = row[0] ? atoll(row[0]) : 0;
        ai.user_id       = row[1] ? atoi(row[1]) : 0;
        ai.context       = row[2] ? row[2] : "";
        ai.prompt_text   = row[3] ? row[3] : "";
        ai.response_text = row[4] ? row[4] : "";
        ai.model_used    = row[5] ? row[5] : "";
        ai.latency_ms    = row[6] ? atoi(row[6]) : 0;
        out.push_back(std::move(ai));
    }
    mysql_free_result(res);
    return out;
}

} // namespace mc1vt
