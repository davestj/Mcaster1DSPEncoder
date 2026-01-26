// mc1_db.h — Shared thread-safe MariaDB singleton for background C++ threads
//
// File:    src/linux/mc1_db.h
// Author:  Dave St. John <davestj@gmail.com>
// Date:    2026-02-24
// Purpose: We provide a single, mutex-protected MariaDB C API connection for
//          use by background threads (SystemHealth, ServerMonitor, EncoderSlot
//          event writes).  All write failures are non-fatal — they are logged
//          at WARN level and execution continues.
//
// Usage:
//   // During startup (after gAdminConfig is populated):
//   Mc1Db::instance().connect(gAdminConfig.db);
//
//   // Fire-and-forget INSERT / UPDATE (any thread):
//   Mc1Db::instance().exec("INSERT INTO mcaster1_metrics.stream_events "
//                          "(slot_id,mount,event_type) VALUES (1,'/rock','start')");
//
//   // Parameterised exec (values are mysql_real_escape_string'd):
//   Mc1Db::instance().execf("INSERT INTO foo (a,b) VALUES ('%s',%d)", str, num);
//
// CHANGELOG:
//   2026-02-24  v1.0.0  Initial implementation

#pragma once

#include <string>
#include <mutex>
#include <cstdarg>
#include <cstdio>

#include <mysql.h>
#include "../libmcaster1dspencoder/libmcaster1dspencoder.h"   // mc1DbConfig

// ---------------------------------------------------------------------------
// Mc1Db — singleton, thread-safe write-only MariaDB helper
// ---------------------------------------------------------------------------

class Mc1Db {
public:
    // We return the one global instance.
    static Mc1Db& instance() {
        static Mc1Db inst;
        return inst;
    }

    // We connect using credentials from gAdminConfig.db (populated by YAML loader).
    // safe to call multiple times — reconnects if already connected.
    bool connect(const mc1DbConfig& cfg);

    // We execute a raw SQL string (no parameters).  Returns true on success.
    // Thread-safe.  Reconnects once on lost-connection error (CR_SERVER_GONE_ERROR).
    bool exec(const std::string& sql);

    // We execute a printf-style formatted SQL string.
    // Thread-safe.
    bool execf(const char* fmt, ...);

    // We escape a string for safe embedding in SQL (wraps mysql_real_escape_string).
    // Returns the escaped string without surrounding quotes.
    std::string escape(const std::string& s);

    // We return true if currently connected.
    bool is_connected() const;

    // We fetch password_hash and is_active for the given username from
    // mcaster1_encoder.users.  Returns false if user not found or DB error.
    // out_hash and out_is_active are only set on true return.
    bool fetch_user_auth(const std::string& username,
                         std::string& out_hash,
                         bool&        out_is_active);

    void disconnect();

    // Non-copyable
    Mc1Db(const Mc1Db&)            = delete;
    Mc1Db& operator=(const Mc1Db&) = delete;

private:
    Mc1Db() = default;
    ~Mc1Db() { disconnect(); }

    bool do_connect();          // internal — called under lock
    bool check_reconnect();     // internal — called under lock when exec fails

    MYSQL*      mysql_   = nullptr;
    mc1DbConfig cfg_{};
    bool        cfg_set_ = false;
    mutable std::mutex mtx_;
};
