/*
 * Mcaster1DSPEncoder — Win-Qt Build
 * event_log.cpp — Thread-safe event log implementation
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "event_log.h"

#include <cstdio>
#include <ctime>
#include <mutex>

namespace mc1 {

static std::mutex         g_handler_mutex;
static EventLogHandler    g_handler;

void set_event_log_handler(EventLogHandler h)
{
    std::lock_guard<std::mutex> lk(g_handler_mutex);
    g_handler = std::move(h);
}

void event_log(LogLevel level, const char* tag, const char* msg)
{
    // Timestamp for stderr output
    time_t now = time(nullptr);
    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &now);
#else
    localtime_r(&now, &tm_buf);
#endif
    char ts[16];
    strftime(ts, sizeof(ts), "%H:%M:%S", &tm_buf);

    fprintf(stderr, "[%s] [%s] %s  %s\n", ts, log_level_str(level), tag, msg);
    fflush(stderr);

    // Dispatch to registered handler (UI / Qt main thread)
    EventLogHandler h;
    {
        std::lock_guard<std::mutex> lk(g_handler_mutex);
        h = g_handler;
    }
    if (h) {
        h(level, tag, msg);
    }
}

} // namespace mc1
