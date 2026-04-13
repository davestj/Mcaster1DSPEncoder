/*
 * Mcaster1 Producer — Logging Singleton
 * producer/pr_logger.h
 *
 * Mirrors vt_logger.h pattern for the Producer daemon.
 * Writes to /var/log/mcaster1/producer.log and producer_error.log.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <string>
#include <mutex>

namespace mc1pr {

class PrLogger {
public:
    static PrLogger& instance() {
        static PrLogger inst;
        return inst;
    }

    enum Level { CRITICAL = 1, ERROR = 2, WARN = 3, INFO = 4, DEBUG = 5 };

    void set_log_dir(const std::string& dir) {
        std::lock_guard<std::mutex> lk(mtx_);
        log_dir_ = dir;
        open_files();
    }

    void set_level(int level) { level_ = level; }
    int  level() const { return level_; }

    void log(int level, const char* fmt, ...) {
        if (level > level_) return;
        va_list args;
        va_start(args, fmt);
        write_log(level >= ERROR ? fp_err_ : fp_main_, level, fmt, args);
        va_end(args);
    }

    void info(const std::string& msg)  { log(INFO, "%s", msg.c_str()); }
    void warn(const std::string& msg)  { log(WARN, "%s", msg.c_str()); }
    void err(const std::string& msg)   { log(ERROR, "%s", msg.c_str()); }
    void dbg(const std::string& msg)   { log(DEBUG, "%s", msg.c_str()); }
    void crit(const std::string& msg)  { log(CRITICAL, "%s", msg.c_str()); }

private:
    PrLogger() = default;
    ~PrLogger() {
        if (fp_main_ && fp_main_ != stderr) fclose(fp_main_);
        if (fp_err_ && fp_err_ != stderr) fclose(fp_err_);
    }

    void open_files() {
        if (fp_main_ && fp_main_ != stderr) fclose(fp_main_);
        if (fp_err_ && fp_err_ != stderr) fclose(fp_err_);
        std::string main_path = log_dir_ + "/producer.log";
        std::string err_path  = log_dir_ + "/producer_error.log";
        fp_main_ = fopen(main_path.c_str(), "a");
        fp_err_  = fopen(err_path.c_str(), "a");
        if (!fp_main_) fp_main_ = stderr;
        if (!fp_err_)  fp_err_  = stderr;
    }

    void write_log(FILE* fp, int level, const char* fmt, va_list args) {
        if (!fp) fp = stderr;
        static const char* LEVEL_NAMES[] = {"", "CRIT", "ERR", "WARN", "INFO", "DBG"};
        char timebuf[32];
        time_t now = time(nullptr);
        struct tm tm_buf;
        localtime_r(&now, &tm_buf);
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm_buf);

        std::lock_guard<std::mutex> lk(mtx_);
        fprintf(fp, "[%s] [%s] ", timebuf, LEVEL_NAMES[level]);
        vfprintf(fp, fmt, args);
        fprintf(fp, "\n");
        fflush(fp);
    }

    std::mutex  mtx_;
    std::string log_dir_ = "/var/log/mcaster1";
    int         level_   = INFO;
    FILE*       fp_main_ = stderr;
    FILE*       fp_err_  = stderr;
};

} // namespace mc1pr

#define PR_INFO(msg)  mc1pr::PrLogger::instance().info(msg)
#define PR_WARN(msg)  mc1pr::PrLogger::instance().warn(msg)
#define PR_ERR(msg)   mc1pr::PrLogger::instance().err(msg)
#define PR_DBG(msg)   mc1pr::PrLogger::instance().dbg(msg)
#define PR_CRIT(msg)  mc1pr::PrLogger::instance().crit(msg)
