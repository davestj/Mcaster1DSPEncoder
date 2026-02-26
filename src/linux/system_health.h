// system_health.h — Background system health monitor for Mcaster1DSPEncoder
//
// File:    src/linux/system_health.h
// Author:  Dave St. John <davestj@gmail.com>
// Date:    2026-02-24
// Purpose: We sample CPU, memory, network, and thread metrics from /proc/
//          on a background POSIX thread (std::thread).  We keep a rolling
//          in-memory history buffer and write DB snapshots every 60 seconds.
//          We expose per-slot encoder health derived from AudioPipeline stats.
//
//          This runs in a SEPARATE thread from the encoder slots so DSP latency
//          is completely unaffected.
//
// API endpoints registered in http_api.cpp:
//   GET /api/v1/system/health            → latest HealthSnapshot as JSON
//   GET /api/v1/system/health/history    → last N snapshots (default 60)
//
// JSON response shape for /api/v1/system/health:
//   {
//     "ok": true,
//     "sampled_at": 1740422400,
//     "cpu_pct":    12.5,
//     "mem_used_mb": 1024,
//     "mem_total_mb": 4096,
//     "mem_pct":    25.0,
//     "net_in_kbps":  320,
//     "net_out_kbps": 512,
//     "net_iface":  "eth0",
//     "thread_count": 18,
//     "slots": [
//       { "slot_id": 1, "state": "live", "bytes_out": 12345678,
//         "out_kbps": 128, "track_title": "Song Name", "listeners": 5 }
//     ]
//   }
//
// CHANGELOG:
//   2026-02-24  v1.0.0  Initial implementation

#pragma once

#include <string>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <atomic>
#include <ctime>

// Forward declarations — we do not include the full audio_pipeline.h here
// to avoid circular deps; the .cpp includes it.
class AudioPipeline;

// ---------------------------------------------------------------------------
// SlotHealth — per-encoder-slot health snapshot
// ---------------------------------------------------------------------------

struct SlotHealth {
    int         slot_id    = 0;
    std::string state;          // "idle","live","reconnecting","error","sleep"
    int64_t     bytes_out  = 0; // cumulative bytes sent (from StreamClient)
    int         out_kbps   = 0; // computed rate over last sampling interval
    std::string track_title;
    int         listeners  = 0; // from ServerMonitor cross-ref (0 if unknown)
};

// ---------------------------------------------------------------------------
// HealthSnapshot — one point-in-time system health measurement
// ---------------------------------------------------------------------------

struct HealthSnapshot {
    time_t   sampled_at   = 0;
    float    cpu_pct      = 0.0f;   // 0.0 – 100.0
    int      mem_used_mb  = 0;
    int      mem_total_mb = 0;
    float    mem_pct      = 0.0f;
    int      net_in_kbps  = 0;      // RX on listening interface
    int      net_out_kbps = 0;      // TX on listening interface
    std::string net_iface;           // e.g. "eth0"
    int      thread_count = 0;
    std::vector<SlotHealth> slots;
};

// ---------------------------------------------------------------------------
// SystemHealth — singleton background sampler
// ---------------------------------------------------------------------------

class SystemHealth {
public:
    // We return the single global instance.
    static SystemHealth& instance() {
        static SystemHealth inst;
        return inst;
    }

    // We start the background sampling thread.
    //   interval_s  — seconds between samples (default 5)
    //   pipeline    — pointer to AudioPipeline for slot stats (may be null)
    void start(int interval_s = 5, AudioPipeline* pipeline = nullptr);

    // We signal the background thread to stop and join it.
    void stop();

    // We return a thread-safe copy of the latest snapshot.
    // Returns a zeroed snapshot if sampling hasn't run yet.
    HealthSnapshot getSnapshot() const;

    // We return up to n of the most recent snapshots (newest last).
    std::vector<HealthSnapshot> getHistory(int n = 60) const;

    // Non-copyable
    SystemHealth(const SystemHealth&)            = delete;
    SystemHealth& operator=(const SystemHealth&) = delete;

private:
    SystemHealth() = default;
    ~SystemHealth() { stop(); }

    // We run the sampling loop in this method (called from the background thread).
    void run();

    // ── /proc/ readers ──────────────────────────────────────────────────────
    // We read cpu jiffies from /proc/stat.
    struct CpuTicks { unsigned long user, nice, sys, idle, iowait, irq, softirq; };
    static CpuTicks read_cpu_ticks();

    // We compute CPU percentage between two tick snapshots.
    static float cpu_pct_from_ticks(const CpuTicks& t1, const CpuTicks& t2);

    // We read MemTotal and MemAvailable from /proc/meminfo.
    static void read_meminfo(int& used_mb, int& total_mb);

    // We read RX/TX bytes for a named interface from /proc/net/dev.
    struct NetBytes { uint64_t rx = 0, tx = 0; };
    static NetBytes read_net_bytes(const std::string& iface);

    // We auto-detect the primary non-loopback interface from /proc/net/dev.
    static std::string detect_net_iface(const std::string& bind_addr);

    // We read the process thread count from /proc/self/status.
    static int read_thread_count();

    // ── DB writes ───────────────────────────────────────────────────────────
    void write_to_db(const HealthSnapshot& snap);

    // ── State ───────────────────────────────────────────────────────────────
    int              interval_s_  = 5;
    AudioPipeline*   pipeline_    = nullptr;

    std::atomic<bool>       running_{ false };
    std::thread             thread_;
    mutable std::mutex      mtx_;

    // We keep the last MAX_HISTORY snapshots in a circular deque.
    static constexpr int MAX_HISTORY = 120;  // 10 min at 5s interval
    std::deque<HealthSnapshot> history_;

    // We track the last bytes-sent per slot to compute out_kbps.
    std::vector<int64_t> prev_bytes_;

    // We detect the network interface once at start.
    std::string net_iface_;
    time_t      last_db_write_ = 0;
};
