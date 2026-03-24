/*
 * Mcaster1 VoicTune — Database Client
 * voictune/vt_db.h
 *
 * MariaDB connection wrapper for the mcaster1_voictune database.
 * CRUD for sessions, voice_profiles, analysis_snapshots, ai_interactions.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <mysql/mysql.h>

namespace mc1vt {

struct DbSession {
    int         id           = 0;
    std::string session_name;
    int         user_id      = 0;
    std::string started_at;
    std::string ended_at;
    int         duration_sec = 0;
    std::string notes;
};

struct DbVoiceProfile {
    int         id               = 0;
    int         user_id          = 0;
    std::string profile_name     = "Default";
    float       fundamental_hz   = 0.0f;
    std::string voice_type       = "unknown";
    float       avg_lufs         = -96.0f;
    float       avg_rms_db       = -96.0f;
    std::string eq_preset_json;
    std::string effects_chain_json;
    std::string analysis_json;
};

struct DbAnalysisSnapshot {
    int64_t     id           = 0;
    int         session_id   = 0;
    int64_t     timestamp_ms = 0;
    float       rms_db       = -96.0f;
    float       peak_db      = -96.0f;
    float       lufs         = -96.0f;
    float       pitch_hz     = 0.0f;
    std::string note_name;
    float       cents_off    = 0.0f;
    std::string spectrum_json;
};

struct DbAiInteraction {
    int64_t     id           = 0;
    int         user_id      = 0;
    std::string context;
    std::string prompt_text;
    std::string response_text;
    std::string model_used;
    int         latency_ms   = 0;
};

class VtDb {
public:
    VtDb() = default;
    ~VtDb();

    /* Connect using config values. Returns false on error. */
    bool connect(const std::string& host, int port,
                 const std::string& user, const std::string& password,
                 const std::string& db_name);

    /* Connect using ~/.my.cnf defaults + explicit db_name */
    bool connect_defaults(const std::string& db_name);

    void disconnect();
    bool is_connected() const { return conn_ != nullptr; }
    bool reconnect();

    /* Sessions */
    int  create_session(int user_id, const std::string& name);
    bool end_session(int session_id);
    DbSession get_session(int id);
    std::vector<DbSession> list_sessions(int user_id, int limit = 50);

    /* Voice Profiles */
    int  create_profile(int user_id, const std::string& name);
    bool update_profile(const DbVoiceProfile& p);
    DbVoiceProfile get_profile(int user_id, const std::string& name);
    std::vector<DbVoiceProfile> list_profiles(int user_id);

    /* Analysis Snapshots */
    bool insert_snapshot(const DbAnalysisSnapshot& s);
    bool insert_snapshots_batch(const std::vector<DbAnalysisSnapshot>& batch);
    std::vector<DbAnalysisSnapshot> get_snapshots(int session_id,
                                                   int64_t from_ms = 0,
                                                   int64_t to_ms = INT64_MAX,
                                                   int limit = 1000);

    /* AI Interactions */
    int  log_ai_interaction(const DbAiInteraction& ai);
    std::vector<DbAiInteraction> get_ai_history(int user_id, const std::string& context, int limit = 20);

    /* Last error message from MySQL */
    std::string last_error() const;

private:
    MYSQL* conn_ = nullptr;
    std::string host_, user_, password_, db_name_;
    int port_ = 3306;

    bool ensure_connected();
    MYSQL_RES* query(const std::string& sql);
    bool exec(const std::string& sql);
    std::string escape(const std::string& s);
};

} // namespace mc1vt
