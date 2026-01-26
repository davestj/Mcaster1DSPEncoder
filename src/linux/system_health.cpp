// system_health.cpp — SystemHealth background monitor implementation
//
// File:    src/linux/system_health.cpp
// Author:  Dave St. John <davestj@gmail.com>
// Date:    2026-02-24
// Purpose: See system_health.h
//
// CHANGELOG:
//   2026-02-24  v1.0.0  Initial implementation

#include "system_health.h"
#include "audio_pipeline.h"
#include "encoder_slot.h"
#include "mc1_db.h"
#include "mc1_logger.h"
#include "../libmcaster1dspencoder/libmcaster1dspencoder.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cmath>

// ---------------------------------------------------------------------------
// start / stop
// ---------------------------------------------------------------------------

void SystemHealth::start(int interval_s, AudioPipeline* pipeline)
{
    if (running_.load()) return;
    interval_s_ = std::max(1, interval_s);
    pipeline_   = pipeline;
    net_iface_  = detect_net_iface(gAdminConfig.sockets[0].bind_address);
    running_.store(true);
    thread_ = std::thread([this]() { run(); });
    MC1_INFO("SystemHealth: started (interval=" + std::to_string(interval_s_) +
             "s iface=" + net_iface_ + ")");
}

void SystemHealth::stop()
{
    running_.store(false);
    if (thread_.joinable()) thread_.join();
}

// ---------------------------------------------------------------------------
// getSnapshot / getHistory
// ---------------------------------------------------------------------------

HealthSnapshot SystemHealth::getSnapshot() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (history_.empty()) return HealthSnapshot{};
    return history_.back();
}

std::vector<HealthSnapshot> SystemHealth::getHistory(int n) const
{
    std::lock_guard<std::mutex> lk(mtx_);
    int start = std::max(0, (int)history_.size() - n);
    return std::vector<HealthSnapshot>(history_.begin() + start, history_.end());
}

// ---------------------------------------------------------------------------
// run — the sampling loop
// ---------------------------------------------------------------------------

void SystemHealth::run()
{
    // We take an initial CPU tick reading before the loop so the first
    // delta is valid.
    CpuTicks prev_cpu = read_cpu_ticks();
    NetBytes prev_net = read_net_bytes(net_iface_);
    std::chrono::steady_clock::time_point prev_time = std::chrono::steady_clock::now();

    // We sleep in 200ms slices so stop() is responsive.
    const int slices_per_interval = interval_s_ * 5;  // 200ms × 5 = 1s; × interval_s_
    int       slice_count = 0;

    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (!running_.load()) break;

        ++slice_count;
        if (slice_count < slices_per_interval) continue;
        slice_count = 0;

        // ── CPU ─────────────────────────────────────────────────────────────
        CpuTicks curr_cpu = read_cpu_ticks();
        float cpu = cpu_pct_from_ticks(prev_cpu, curr_cpu);
        prev_cpu = curr_cpu;

        // ── Memory ──────────────────────────────────────────────────────────
        int mem_used = 0, mem_total = 0;
        read_meminfo(mem_used, mem_total);

        // ── Network ─────────────────────────────────────────────────────────
        NetBytes curr_net = read_net_bytes(net_iface_);
        auto     now_tp   = std::chrono::steady_clock::now();
        double   elapsed  = std::chrono::duration<double>(now_tp - prev_time).count();
        int in_kbps  = 0;
        int out_kbps = 0;
        if (elapsed > 0.0) {
            in_kbps  = (int)((double)(curr_net.rx - prev_net.rx) * 8.0 / elapsed / 1000.0);
            out_kbps = (int)((double)(curr_net.tx - prev_net.tx) * 8.0 / elapsed / 1000.0);
        }
        prev_net  = curr_net;
        prev_time = now_tp;
        if (in_kbps  < 0) in_kbps  = 0;
        if (out_kbps < 0) out_kbps = 0;

        // ── Threads ─────────────────────────────────────────────────────────
        int threads = read_thread_count();

        // ── Slot health ─────────────────────────────────────────────────────
        std::vector<SlotHealth> slots;
        if (pipeline_) {
            auto all_stats = pipeline_->all_stats();

            // Resize prev_bytes_ to match slot count
            if (prev_bytes_.size() < all_stats.size())
                prev_bytes_.resize(all_stats.size(), 0);

            for (size_t i = 0; i < all_stats.size(); ++i) {
                const auto& s = all_stats[i];
                SlotHealth sh;
                sh.slot_id    = s.slot_id;
                sh.state      = s.state_str;
                sh.bytes_out  = (int64_t)s.bytes_sent;
                sh.track_title = s.current_title;
                // We compute kbps as delta bytes / elapsed seconds
                int64_t delta = sh.bytes_out - prev_bytes_[i];
                if (delta >= 0 && elapsed > 0.0)
                    sh.out_kbps = (int)((double)delta * 8.0 / elapsed / 1000.0);
                prev_bytes_[i] = sh.bytes_out;
                // Listener count from ServerMonitor (included via forward decl in server_monitors)
                // We leave at 0 here; http_api.cpp merges it from ServerMonitor at response time.
                sh.listeners = 0;
                slots.push_back(sh);
            }
        }

        // ── Build snapshot ───────────────────────────────────────────────────
        HealthSnapshot snap;
        snap.sampled_at   = time(nullptr);
        snap.cpu_pct      = cpu;
        snap.mem_used_mb  = mem_used;
        snap.mem_total_mb = mem_total;
        snap.mem_pct      = (mem_total > 0)
                            ? (float)mem_used / (float)mem_total * 100.0f
                            : 0.0f;
        snap.net_in_kbps  = in_kbps;
        snap.net_out_kbps = out_kbps;
        snap.net_iface    = net_iface_;
        snap.thread_count = threads;
        snap.slots        = std::move(slots);

        {
            std::lock_guard<std::mutex> lk(mtx_);
            history_.push_back(snap);
            if ((int)history_.size() > MAX_HISTORY)
                history_.pop_front();
        }

        // ── DB write every 60s ───────────────────────────────────────────────
        if (snap.sampled_at - last_db_write_ >= 60) {
            write_to_db(snap);
            last_db_write_ = snap.sampled_at;
        }
    }
}

// ---------------------------------------------------------------------------
// write_to_db
// ---------------------------------------------------------------------------

void SystemHealth::write_to_db(const HealthSnapshot& snap)
{
    Mc1Db::instance().execf(
        "INSERT INTO mcaster1_metrics.system_health_snapshots "
        "(sampled_at,cpu_pct,mem_used_mb,mem_total_mb,net_in_kbps,net_out_kbps,thread_count) "
        "VALUES (FROM_UNIXTIME(%ld),%.2f,%d,%d,%d,%d,%d)",
        (long)snap.sampled_at,
        (double)snap.cpu_pct,
        snap.mem_used_mb, snap.mem_total_mb,
        snap.net_in_kbps, snap.net_out_kbps,
        snap.thread_count);

    // Per-slot
    for (const auto& sl : snap.slots) {
        std::string title_esc = Mc1Db::instance().escape(sl.track_title);
        Mc1Db::instance().execf(
            "INSERT INTO mcaster1_metrics.encoder_health_snapshots "
            "(slot_id,sampled_at,state,bytes_out,out_kbps,track_title,listeners) "
            "VALUES (%d,FROM_UNIXTIME(%ld),'%s',%lld,%d,'%s',%d)",
            sl.slot_id, (long)snap.sampled_at,
            sl.state.c_str(),
            (long long)sl.bytes_out,
            sl.out_kbps,
            title_esc.c_str(),
            sl.listeners);
    }

    // We prune rows older than 7 days once per hour (cheap but not every 60s)
    static time_t last_prune = 0;
    if (snap.sampled_at - last_prune >= 3600) {
        Mc1Db::instance().exec(
            "DELETE FROM mcaster1_metrics.system_health_snapshots "
            "WHERE sampled_at < DATE_SUB(NOW(), INTERVAL 7 DAY)");
        Mc1Db::instance().exec(
            "DELETE FROM mcaster1_metrics.encoder_health_snapshots "
            "WHERE sampled_at < DATE_SUB(NOW(), INTERVAL 7 DAY)");
        last_prune = snap.sampled_at;
    }
}

// ---------------------------------------------------------------------------
// /proc/ readers
// ---------------------------------------------------------------------------

SystemHealth::CpuTicks SystemHealth::read_cpu_ticks()
{
    CpuTicks t{};
    std::ifstream f("/proc/stat");
    if (!f) return t;
    std::string label;
    f >> label;  // "cpu"
    f >> t.user >> t.nice >> t.sys >> t.idle >> t.iowait >> t.irq >> t.softirq;
    return t;
}

float SystemHealth::cpu_pct_from_ticks(const CpuTicks& t1, const CpuTicks& t2)
{
    unsigned long u1 = t1.user + t1.nice + t1.sys + t1.irq + t1.softirq;
    unsigned long u2 = t2.user + t2.nice + t2.sys + t2.irq + t2.softirq;
    unsigned long i1 = t1.idle + t1.iowait;
    unsigned long i2 = t2.idle + t2.iowait;
    unsigned long du = (u2 > u1) ? (u2 - u1) : 0;
    unsigned long di = (i2 > i1) ? (i2 - i1) : 0;
    unsigned long total = du + di;
    if (total == 0) return 0.0f;
    return (float)du / (float)total * 100.0f;
}

void SystemHealth::read_meminfo(int& used_mb, int& total_mb)
{
    used_mb = total_mb = 0;
    std::ifstream f("/proc/meminfo");
    if (!f) return;
    long mem_total = 0, mem_available = 0;
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("MemTotal:", 0) == 0)
            sscanf(line.c_str(), "MemTotal: %ld kB", &mem_total);
        else if (line.rfind("MemAvailable:", 0) == 0)
            sscanf(line.c_str(), "MemAvailable: %ld kB", &mem_available);
        if (mem_total && mem_available) break;
    }
    total_mb = (int)(mem_total / 1024);
    long used_kb = mem_total - mem_available;
    used_mb = (int)(used_kb / 1024);
    if (used_mb < 0) used_mb = 0;
}

SystemHealth::NetBytes SystemHealth::read_net_bytes(const std::string& iface)
{
    NetBytes nb{};
    if (iface.empty()) return nb;
    std::ifstream f("/proc/net/dev");
    if (!f) return nb;
    std::string line;
    // Skip 2 header lines
    std::getline(f, line);
    std::getline(f, line);
    while (std::getline(f, line)) {
        // Format: "  eth0: rx_bytes ... tx_bytes ..."
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        // Trim whitespace
        size_t s = name.find_first_not_of(" \t");
        if (s != std::string::npos) name = name.substr(s);
        if (name != iface) continue;
        // Parse RX bytes (first field after colon) and TX bytes (9th field)
        std::istringstream ss(line.substr(colon + 1));
        uint64_t rx, rx_pkts, rx_err, rx_drop, rx_fifo, rx_frame, rx_compr, rx_mcast;
        uint64_t tx, tx_pkts, tx_err, tx_drop, tx_fifo, tx_colls, tx_carrier, tx_compr;
        ss >> rx >> rx_pkts >> rx_err >> rx_drop >> rx_fifo >> rx_frame >> rx_compr >> rx_mcast;
        ss >> tx;
        nb.rx = rx;
        nb.tx = tx;
        break;
    }
    return nb;
}

std::string SystemHealth::detect_net_iface(const std::string& bind_addr)
{
    // If bound to a specific IP, we try to find the matching interface.
    // Otherwise (0.0.0.0 / empty) we pick the first non-loopback with traffic.
    std::ifstream f("/proc/net/dev");
    if (!f) return "eth0";  // safe fallback

    std::string best;
    uint64_t    best_tx = 0;
    std::string line;
    std::getline(f, line);  // skip header 1
    std::getline(f, line);  // skip header 2

    while (std::getline(f, line)) {
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        size_t s = name.find_first_not_of(" \t");
        if (s != std::string::npos) name = name.substr(s);
        if (name == "lo") continue;  // skip loopback

        std::istringstream ss(line.substr(colon + 1));
        uint64_t rx, rx_pkts, rx_err, rx_drop, rx_fifo, rx_frame, rx_compr, rx_mcast;
        uint64_t tx;
        if (!(ss >> rx >> rx_pkts >> rx_err >> rx_drop >> rx_fifo >> rx_frame >> rx_compr >> rx_mcast >> tx))
            continue;
        if (tx > best_tx) {
            best_tx = tx;
            best    = name;
        }
    }
    return best.empty() ? "eth0" : best;
}

int SystemHealth::read_thread_count()
{
    std::ifstream f("/proc/self/status");
    if (!f) return 0;
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("Threads:", 0) == 0) {
            int n = 0;
            sscanf(line.c_str(), "Threads: %d", &n);
            return n;
        }
    }
    return 0;
}
