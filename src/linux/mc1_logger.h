// mc1_logger.h — Mcaster1 DSP Encoder logging subsystem
//
// Verbosity levels (set via --log-level N or config yaml log-level):
//   1 = CRITICAL  — fatal errors, crash conditions
//   2 = ERROR     — non-fatal errors
//   3 = WARN      — warnings, degraded operation
//   4 = INFO      — normal operational events (default)
//   5 = DEBUG     — verbose trace / stack info / raw data
//
// Log files (all under /var/log/mcaster1/ by default):
//   access.log   — HTTP request/response log (Apache combined format)
//   error.log    — application errors and warnings
//   encoder.log  — encoder slot events (start/stop/track change/DSP/etc.)
//   api.log      — C++ REST API calls (request body + response body at level 5)
//
// Thread-safe. Multiple writers via mutex.
// Rotation: external logrotate or mc1_logger_rotate() call.

#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <execinfo.h>
#endif

/* ── Log levels ─────────────────────────────────────────────────────────── */

enum Mc1LogLevel {
    MC1_LOG_CRITICAL = 1,
    MC1_LOG_ERROR    = 2,
    MC1_LOG_WARN     = 3,
    MC1_LOG_INFO     = 4,
    MC1_LOG_DEBUG    = 5,
};

/* ── Logger class ───────────────────────────────────────────────────────── */

class Mc1Logger {
public:
    // Singleton access
    static Mc1Logger& instance() {
        static Mc1Logger inst;
        return inst;
    }

    // Configure log directory + level. Called once at startup.
    void init(const std::string& log_dir, int level = MC1_LOG_INFO,
              bool also_stderr = true)
    {
        std::lock_guard<std::mutex> lk(mtx_);
        log_dir_    = log_dir;
        level_      = level;
        stderr_too_ = also_stderr;
        ensure_dir(log_dir_);
        open_files();
    }

    int  level()    const { return level_; }
    void set_level(int l) { std::lock_guard<std::mutex> lk(mtx_); level_ = l; }

    // ── Log helpers ──────────────────────────────────────────────────────

    void critical(const std::string& msg, const std::string& file = {},
                  int line = 0) { write(MC1_LOG_CRITICAL, "CRIT ", msg, file, line); }

    void error   (const std::string& msg, const std::string& file = {},
                  int line = 0) { write(MC1_LOG_ERROR,    "ERROR", msg, file, line); }

    void warn    (const std::string& msg, const std::string& file = {},
                  int line = 0) { write(MC1_LOG_WARN,     "WARN ", msg, file, line); }

    void info    (const std::string& msg, const std::string& file = {},
                  int line = 0) { write(MC1_LOG_INFO,     "INFO ", msg, file, line); }

    void debug   (const std::string& msg, const std::string& file = {},
                  int line = 0) { write(MC1_LOG_DEBUG,    "DEBUG", msg, file, line); }

    // ── Encoder event log ────────────────────────────────────────────────

    void encoder(int slot, const std::string& event,
                 const std::string& detail = {})
    {
        if (level_ < MC1_LOG_INFO) return;
        std::lock_guard<std::mutex> lk(mtx_);
        std::string line = ts_now() + " SLOT" + std::to_string(slot)
                         + " " + event;
        if (!detail.empty()) line += " | " + detail;
        append(enc_f_, line);
        if (stderr_too_ || level_ >= MC1_LOG_DEBUG)
            fprintf(stderr, "[encoder] slot%d %s %s\n",
                    slot, event.c_str(), detail.c_str());
    }

    // ── HTTP access log (Apache combined format) ─────────────────────────

    void access(const std::string& remote_addr,
                const std::string& method,
                const std::string& uri,
                int                status,
                long               bytes,
                long               duration_us,
                const std::string& referer = "-",
                const std::string& user_agent = "-")
    {
        if (level_ < MC1_LOG_INFO) return;
        std::lock_guard<std::mutex> lk(mtx_);

        // Apache combined log format + duration_ms extension
        char ts[64];
        time_t now = time(nullptr);
        struct tm tm_buf;
        strftime(ts, sizeof(ts), "%d/%b/%Y:%H:%M:%S +0000", gmtime_r(&now, &tm_buf));

        std::ostringstream ss;
        ss << remote_addr << " - - [" << ts << "] "
           << "\"" << method << " " << uri << " HTTP/1.1\" "
           << status << " " << bytes << " "
           << "\"" << referer << "\" "
           << "\"" << user_agent.substr(0, 200) << "\" "
           << (duration_us / 1000) << "ms";

        append(acc_f_, ss.str());

        if (level_ >= MC1_LOG_DEBUG) {
            fprintf(stderr, "[access] %s %s → %d (%ldms)\n",
                    method.c_str(), uri.c_str(), status, duration_us / 1000);
        }
    }

    // ── API log (request/response bodies at DEBUG level) ─────────────────

    void api(const std::string& method,
             const std::string& path,
             int                status,
             const std::string& req_body = {},
             const std::string& resp_body = {})
    {
        if (level_ < MC1_LOG_DEBUG) return;  // Only at level 5
        std::lock_guard<std::mutex> lk(mtx_);

        std::string line = ts_now() + " " + method + " " + path
                         + " → " + std::to_string(status);
        if (!req_body.empty())  line += "\n  REQ:  " + req_body.substr(0, 512);
        if (!resp_body.empty()) line += "\n  RESP: " + resp_body.substr(0, 512);

        append(api_f_, line);
    }

    // ── Rotate (reopen files) ─────────────────────────────────────────────

    void rotate() {
        std::lock_guard<std::mutex> lk(mtx_);
        close_files();
        open_files();
    }

    // ── Stack trace (level 5 only) ────────────────────────────────────────
    // On Linux with -rdynamic or addr2line, prints a basic backtrace.
    // Writes to error.log.

    void stack_trace(const std::string& ctx = {}) {
        if (level_ < MC1_LOG_DEBUG) return;
#ifdef __linux__
        void* addrs[32];
        int   n = ::backtrace(addrs, 32);
        char** syms = ::backtrace_symbols(addrs, n);
        std::ostringstream ss;
        ss << "--- STACK TRACE";
        if (!ctx.empty()) ss << " [" << ctx << "]";
        ss << " ---\n";
        for (int i = 0; i < n; i++) {
            if (syms) ss << "  " << i << ": " << syms[i] << "\n";
            else      ss << "  " << i << ": " << addrs[i] << "\n";
        }
        ss << "--- END STACK TRACE ---";
        if (syms) free(syms);
        {
            std::lock_guard<std::mutex> lk(mtx_);
            append(err_f_, ss.str());
        }
        fprintf(stderr, "%s\n", ss.str().c_str());
#endif
    }

    // ── Convenience: log to error.log + stderr ────────────────────────────

    void log_exception(const std::exception& e, const std::string& ctx = {}) {
        std::string msg = "Exception";
        if (!ctx.empty()) msg += " [" + ctx + "]";
        msg += ": ";
        msg += e.what();
        error(msg);
        if (level_ >= MC1_LOG_DEBUG) stack_trace(ctx);
    }

private:
    Mc1Logger() = default;
    ~Mc1Logger() { close_files(); }
    Mc1Logger(const Mc1Logger&) = delete;
    Mc1Logger& operator=(const Mc1Logger&) = delete;

    std::mutex  mtx_;
    std::string log_dir_    = "/var/log/mcaster1";
    int         level_      = MC1_LOG_INFO;
    bool        stderr_too_ = true;

    FILE* acc_f_ = nullptr;   // access.log
    FILE* err_f_ = nullptr;   // error.log
    FILE* enc_f_ = nullptr;   // encoder.log
    FILE* api_f_ = nullptr;   // api.log

    static std::string ts_now() {
        char ts[32];
        time_t now = time(nullptr);
        struct tm tm_buf;
        strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", gmtime_r(&now, &tm_buf));
        return ts;
    }

    static void ensure_dir(const std::string& dir) {
        struct stat st;
        if (::stat(dir.c_str(), &st) != 0)
            ::mkdir(dir.c_str(), 0755);
    }

    void open_files() {
        acc_f_ = fopen((log_dir_ + "/access.log").c_str(), "a");
        err_f_ = fopen((log_dir_ + "/error.log").c_str(),  "a");
        enc_f_ = fopen((log_dir_ + "/encoder.log").c_str(), "a");
        if (level_ >= MC1_LOG_DEBUG)
            api_f_ = fopen((log_dir_ + "/api.log").c_str(), "a");
    }

    void close_files() {
        if (acc_f_) { fclose(acc_f_); acc_f_ = nullptr; }
        if (err_f_) { fclose(err_f_); err_f_ = nullptr; }
        if (enc_f_) { fclose(enc_f_); enc_f_ = nullptr; }
        if (api_f_) { fclose(api_f_); api_f_ = nullptr; }
    }

    void append(FILE*& f, const std::string& line) {
        if (!f) return;
        fprintf(f, "%s\n", line.c_str());
        fflush(f);
    }

    void write(int req_level, const char* tag,
               const std::string& msg,
               const std::string& src_file, int src_line)
    {
        if (req_level > level_) return;
        std::lock_guard<std::mutex> lk(mtx_);

        std::string entry = ts_now();
        entry += " ["; entry += tag; entry += "] ";
        entry += msg;

        if (level_ >= MC1_LOG_DEBUG && !src_file.empty()) {
            // Strip path prefix for readability
            size_t sl = src_file.rfind('/');
            entry += " (";
            entry += (sl == std::string::npos ? src_file : src_file.substr(sl + 1));
            entry += ":";
            entry += std::to_string(src_line);
            entry += ")";
        }

        append(err_f_, entry);

        if (stderr_too_) {
            fprintf(stderr, "%s\n", entry.c_str());
        }
    }
};

/* ── Global logger accessor + convenience macros ─────────────────────────── */

#define mc1log   (Mc1Logger::instance())

#define MC1_CRIT(msg)  mc1log.critical((msg), __FILE__, __LINE__)
#define MC1_ERR(msg)   mc1log.error   ((msg), __FILE__, __LINE__)
#define MC1_WARN(msg)  mc1log.warn    ((msg), __FILE__, __LINE__)
#define MC1_INFO(msg)  mc1log.info    ((msg), __FILE__, __LINE__)
#define MC1_DBG(msg)   mc1log.debug   ((msg), __FILE__, __LINE__)

// Stream-style helper for building log messages inline
#define MC1_LOG(level, ...) do { \
    std::ostringstream _mc1_ss; \
    _mc1_ss << __VA_ARGS__; \
    mc1log.level(_mc1_ss.str(), __FILE__, __LINE__); \
} while(0)
