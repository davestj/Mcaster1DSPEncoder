// mc1_db.cpp — Mc1Db singleton implementation
//
// File:    src/linux/mc1_db.cpp
// Author:  Dave St. John <davestj@gmail.com>
// Date:    2026-02-24
// Purpose: See mc1_db.h
//
// CHANGELOG:
//   2026-02-24  v1.0.0  Initial implementation

#include "mc1_db.h"
#include "mc1_logger.h"

#include <cstring>
#include <cstdio>
#include <cerrno>

// ---------------------------------------------------------------------------
// connect
// ---------------------------------------------------------------------------

bool Mc1Db::connect(const mc1DbConfig& cfg)
{
    std::lock_guard<std::mutex> lk(mtx_);
    cfg_     = cfg;
    cfg_set_ = true;
    if (mysql_) {
        mysql_close(mysql_);
        mysql_ = nullptr;
    }
    return do_connect();
}

// ---------------------------------------------------------------------------
// do_connect — must be called under lock
// ---------------------------------------------------------------------------

bool Mc1Db::do_connect()
{
    mysql_ = mysql_init(nullptr);
    if (!mysql_) {
        MC1_ERR("Mc1Db: mysql_init() failed (OOM)");
        return false;
    }

    // We set auto-reconnect at the C level as a secondary safeguard.
    my_bool reconnect = 1;
    mysql_options(mysql_, MYSQL_OPT_RECONNECT, &reconnect);

    // We set a 5-second connect timeout so a dead DB host doesn't block.
    unsigned int timeout = 5;
    mysql_options(mysql_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    mysql_options(mysql_, MYSQL_OPT_READ_TIMEOUT,    &timeout);
    mysql_options(mysql_, MYSQL_OPT_WRITE_TIMEOUT,   &timeout);

    // We use the first configured database (db_encoder) for the connection;
    // all queries fully-qualify the database (e.g. mcaster1_metrics.foo).
    const char* dbname = cfg_.db_encoder[0] ? cfg_.db_encoder : "mcaster1_encoder";

    if (!mysql_real_connect(mysql_,
                            cfg_.host,
                            cfg_.user,
                            cfg_.password,
                            dbname,
                            (unsigned int)cfg_.port,
                            nullptr,   // socket
                            0))        // flags
    {
        MC1_ERR("Mc1Db: connect to " + std::string(cfg_.host) + " failed: " +
                std::string(mysql_error(mysql_)));
        mysql_close(mysql_);
        mysql_ = nullptr;
        return false;
    }

    // We set UTF-8 charset for all queries.
    mysql_set_character_set(mysql_, "utf8mb4");

    MC1_INFO("Mc1Db: connected to " + std::string(cfg_.host) + ":" +
             std::to_string(cfg_.port) + " db=" + std::string(dbname));
    return true;
}

// ---------------------------------------------------------------------------
// check_reconnect — called under lock after a failed query
// ---------------------------------------------------------------------------

bool Mc1Db::check_reconnect()
{
    if (!mysql_) return false;
    unsigned int err = mysql_errno(mysql_);
    // CR_SERVER_GONE_ERROR=2006, CR_SERVER_LOST=2013, CR_CONNECTION_ERROR=2002
    if (err == 2006 || err == 2013 || err == 2002 || err == 2003) {
        MC1_WARN("Mc1Db: lost connection (errno=" + std::to_string(err) +
                 "), attempting reconnect");
        mysql_close(mysql_);
        mysql_ = nullptr;
        return do_connect();
    }
    return false;
}

// ---------------------------------------------------------------------------
// exec
// ---------------------------------------------------------------------------

bool Mc1Db::exec(const std::string& sql)
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (!mysql_) {
        // We try a lazy connect if config was set but connection not yet made.
        if (cfg_set_) {
            if (!do_connect()) return false;
        } else {
            return false;
        }
    }

    if (mysql_real_query(mysql_, sql.c_str(), (unsigned long)sql.size()) != 0) {
        // We attempt one reconnect on connection-lost errors.
        if (check_reconnect()) {
            if (mysql_real_query(mysql_, sql.c_str(), (unsigned long)sql.size()) == 0)
                return true;
        }
        MC1_WARN("Mc1Db: query failed: " + std::string(mysql_error(mysql_)) +
                 " SQL=" + sql.substr(0, 120));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// execf
// ---------------------------------------------------------------------------

bool Mc1Db::execf(const char* fmt, ...)
{
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0 || n >= (int)sizeof(buf)) {
        MC1_WARN("Mc1Db::execf: SQL buffer overflow, skipping write");
        return false;
    }
    return exec(std::string(buf, n));
}

// ---------------------------------------------------------------------------
// escape
// ---------------------------------------------------------------------------

std::string Mc1Db::escape(const std::string& s)
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (!mysql_) return s;  // If not connected, return as-is (caller handles)
    std::string out(s.size() * 2 + 1, '\0');
    unsigned long len = mysql_real_escape_string(
        mysql_,
        &out[0],
        s.c_str(),
        (unsigned long)s.size());
    out.resize(len);
    return out;
}

// ---------------------------------------------------------------------------
// fetch_user_auth
// ---------------------------------------------------------------------------

bool Mc1Db::fetch_user_auth(const std::string& username,
                             std::string& out_hash,
                             bool&        out_is_active)
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (!mysql_) {
        if (cfg_set_) { if (!do_connect()) return false; }
        else return false;
    }

    // Escape username in-place (lock already held, so call mysql_ directly)
    std::string esc(username.size() * 2 + 1, '\0');
    unsigned long esc_len = mysql_real_escape_string(
        mysql_, &esc[0], username.c_str(), (unsigned long)username.size());
    esc.resize(esc_len);

    std::string sql =
        "SELECT password_hash, is_active "
        "FROM mcaster1_encoder.users "
        "WHERE username='" + esc + "' AND is_active=1 LIMIT 1";

    if (mysql_real_query(mysql_, sql.c_str(), (unsigned long)sql.size()) != 0) {
        // Try one reconnect
        if (check_reconnect() &&
            mysql_real_query(mysql_, sql.c_str(), (unsigned long)sql.size()) != 0)
            return false;
    }

    MYSQL_RES* result = mysql_store_result(mysql_);
    if (!result) return false;

    MYSQL_ROW row = mysql_fetch_row(result);
    if (!row || !row[0]) {
        mysql_free_result(result);
        return false;
    }

    out_hash      = row[0];
    out_is_active = (row[1] && std::string(row[1]) == "1");
    mysql_free_result(result);
    return true;
}

// ---------------------------------------------------------------------------
// is_connected
// ---------------------------------------------------------------------------

bool Mc1Db::is_connected() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return mysql_ != nullptr;
}

// ---------------------------------------------------------------------------
// disconnect
// ---------------------------------------------------------------------------

void Mc1Db::disconnect()
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (mysql_) {
        mysql_close(mysql_);
        mysql_ = nullptr;
    }
}
