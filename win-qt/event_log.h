/*
 * Mcaster1DSPEncoder — Win-Qt Build
 * event_log.h — Thread-safe application event logging
 *
 * Pure C++ header (no Qt dependencies) — safe to include from encoder_slot.cpp,
 * stream_client.cpp, and any other non-Qt translation unit.
 *
 * Usage (from any thread):
 *   mc1::event_log(mc1::LogLevel::INFO, "[Encoder 1]", "Connected to server");
 *   mc1::event_log(mc1::LogLevel::ERROR, "[Stream]", "Auth failed: 401");
 *
 * The Qt main window registers a handler (via set_event_log_handler) that posts
 * entries to the Event Log tab via a queued connection.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <functional>
#include <string>
#include <mutex>
#include <cstdio>

namespace mc1 {

enum class LogLevel {
    DEBUG   = 0,
    INFO    = 1,
    WARN    = 2,
    LOG_ERROR   = 3,   // LOG_ERROR to avoid Windows macro conflict
    CONNECT = 4,
    AUTH    = 5,
    ICY_META = 6
};

inline const char* log_level_str(LogLevel l) {
    switch (l) {
        case LogLevel::DEBUG:    return "DEBUG";
        case LogLevel::INFO:     return "INFO ";
        case LogLevel::WARN:     return "WARN ";
        case LogLevel::LOG_ERROR:    return "ERROR";
        case LogLevel::CONNECT:  return "CONN ";
        case LogLevel::AUTH:     return "AUTH ";
        case LogLevel::ICY_META: return "ICY  ";
    }
    return "?    ";
}

using EventLogHandler = std::function<void(LogLevel, const char* tag, const char* msg)>;

// Register a handler to receive log entries in addition to stderr.
// Thread-safe. The handler will be called from the logging thread — use
// QMetaObject::invokeMethod(Qt::QueuedConnection) inside the handler to
// marshal to the Qt main thread.
void set_event_log_handler(EventLogHandler h);

// Log an entry. Safe to call from any thread.
// Always writes to stderr; also calls the registered handler if any.
void event_log(LogLevel level, const char* tag, const char* msg);

// Convenience overloads accepting std::string
inline void event_log(LogLevel level, const std::string& tag, const std::string& msg) {
    event_log(level, tag.c_str(), msg.c_str());
}

// Shorthand helpers
inline void log_debug(const char* tag, const char* msg)   { event_log(LogLevel::DEBUG,    tag, msg); }
inline void log_info (const char* tag, const char* msg)   { event_log(LogLevel::INFO,     tag, msg); }
inline void log_warn (const char* tag, const char* msg)   { event_log(LogLevel::WARN,     tag, msg); }
inline void log_error(const char* tag, const char* msg)   { event_log(LogLevel::LOG_ERROR,    tag, msg); }
inline void log_connect(const char* tag, const char* msg) { event_log(LogLevel::CONNECT,  tag, msg); }
inline void log_auth (const char* tag, const char* msg)   { event_log(LogLevel::AUTH,     tag, msg); }
inline void log_icy  (const char* tag, const char* msg)   { event_log(LogLevel::ICY_META, tag, msg); }

inline void log_debug(const std::string& tag, const std::string& msg)   { event_log(LogLevel::DEBUG,    tag, msg); }
inline void log_info (const std::string& tag, const std::string& msg)   { event_log(LogLevel::INFO,     tag, msg); }
inline void log_warn (const std::string& tag, const std::string& msg)   { event_log(LogLevel::WARN,     tag, msg); }
inline void log_error(const std::string& tag, const std::string& msg)   { event_log(LogLevel::LOG_ERROR,    tag, msg); }
inline void log_connect(const std::string& tag, const std::string& msg) { event_log(LogLevel::CONNECT,  tag, msg); }
inline void log_auth (const std::string& tag, const std::string& msg)   { event_log(LogLevel::AUTH,     tag, msg); }
inline void log_icy  (const std::string& tag, const std::string& msg)   { event_log(LogLevel::ICY_META, tag, msg); }

} // namespace mc1
