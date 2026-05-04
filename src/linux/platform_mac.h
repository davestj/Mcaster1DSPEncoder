// platform_mac.h — macOS platform compatibility layer
//
// File:    src/linux/platform_mac.h
// Author:  Dave St. John <davestj@gmail.com>
// Date:    2026-03-29
// Purpose: Provides macOS alternatives for Linux-specific APIs used by
//          system_health.cpp and other modules. On Linux, these functions
//          read from /proc/ — on macOS we use sysctl, mach, and getifaddrs.
//
// Usage:   Included conditionally via #ifdef MC1_MACOS in source files that
//          need platform-specific system metrics.
//
// CHANGELOG:
//   2026-03-29  v1.0.0  Initial implementation for macOS build support

#pragma once

#ifdef MC1_MACOS

#include <mach/mach.h>
#include <mach/host_info.h>
#include <mach/mach_host.h>
#include <mach/processor_info.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <net/if_dl.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <unistd.h>
#include <pthread.h>

// ── CPU ticks (Mach host_statistics) ────────────────────────────────────────

struct MacCpuTicks {
    uint64_t user    = 0;
    uint64_t nice    = 0;  // not directly available on macOS, mapped from system
    uint64_t sys     = 0;
    uint64_t idle    = 0;
    uint64_t iowait  = 0;  // not available on macOS
    uint64_t irq     = 0;  // not available on macOS
    uint64_t softirq = 0;  // not available on macOS
};

inline MacCpuTicks mc1_read_cpu_ticks() {
    MacCpuTicks t{};
    host_cpu_load_info_data_t cpuinfo;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    kern_return_t kr = host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                                       (host_info_t)&cpuinfo, &count);
    if (kr == KERN_SUCCESS) {
        t.user = cpuinfo.cpu_ticks[CPU_STATE_USER];
        t.nice = cpuinfo.cpu_ticks[CPU_STATE_NICE];
        t.sys  = cpuinfo.cpu_ticks[CPU_STATE_SYSTEM];
        t.idle = cpuinfo.cpu_ticks[CPU_STATE_IDLE];
    }
    return t;
}

// ── Memory info (sysctl + vm_statistics) ────────────────────────────────────

inline uint64_t mc1_total_memory() {
    int mib[2] = {CTL_HW, HW_MEMSIZE};
    uint64_t mem = 0;
    size_t len = sizeof(mem);
    sysctl(mib, 2, &mem, &len, NULL, 0);
    return mem;
}

inline uint64_t mc1_available_memory() {
    // Use vm_statistics64 to get free + purgeable pages
    vm_statistics64_data_t vm_stat;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    kern_return_t kr = host_statistics64(mach_host_self(), HOST_VM_INFO64,
                                          (host_info64_t)&vm_stat, &count);
    if (kr != KERN_SUCCESS) return 0;

    // Page size
    vm_size_t page_size;
    host_page_size(mach_host_self(), &page_size);

    // Available = free + purgeable (approximate)
    uint64_t available = (uint64_t)(vm_stat.free_count + vm_stat.purgeable_count)
                         * (uint64_t)page_size;
    return available;
}

inline void mc1_read_meminfo(int& used_mb, int& total_mb) {
    uint64_t total = mc1_total_memory();
    uint64_t avail = mc1_available_memory();
    total_mb = (int)(total / (1024 * 1024));
    uint64_t used = (total > avail) ? (total - avail) : 0;
    used_mb = (int)(used / (1024 * 1024));
}

// ── Load average ────────────────────────────────────────────────────────────

inline double mc1_load_average() {
    double loadavg[1] = {0.0};
    getloadavg(loadavg, 1);
    return loadavg[0];
}

// ── Network bytes (getifaddrs) ──────────────────────────────────────────────

struct MacNetBytes {
    uint64_t rx = 0;
    uint64_t tx = 0;
};

inline MacNetBytes mc1_read_net_bytes(const std::string& iface) {
    MacNetBytes nb{};
    if (iface.empty()) return nb;

    struct ifaddrs* ifap = nullptr;
    if (getifaddrs(&ifap) != 0) return nb;

    for (struct ifaddrs* ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_LINK) continue;
        if (iface != ifa->ifa_name) continue;

        if (ifa->ifa_data) {
            struct if_data* if_data = (struct if_data*)ifa->ifa_data;
            nb.rx = if_data->ifi_ibytes;
            nb.tx = if_data->ifi_obytes;
        }
        break;
    }
    freeifaddrs(ifap);
    return nb;
}

inline std::string mc1_detect_net_iface(const std::string& /*bind_addr*/) {
    std::string best;
    uint64_t best_tx = 0;

    struct ifaddrs* ifap = nullptr;
    if (getifaddrs(&ifap) != 0) return "en0";

    for (struct ifaddrs* ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_LINK) continue;
        std::string name = ifa->ifa_name;
        if (name == "lo0") continue;

        if (ifa->ifa_data) {
            struct if_data* if_data = (struct if_data*)ifa->ifa_data;
            uint64_t tx = if_data->ifi_obytes;
            if (tx > best_tx) {
                best_tx = tx;
                best = name;
            }
        }
    }
    freeifaddrs(ifap);
    return best.empty() ? "en0" : best;
}

// ── Thread count (sysctl) ───────────────────────────────────────────────────

inline int mc1_read_thread_count() {
    // macOS doesn't expose thread count via sysctl easily.
    // Use task_info to get current process thread count.
    task_t task = mach_task_self();
    struct task_basic_info info;
    mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
    kern_return_t kr = task_info(task, TASK_BASIC_INFO,
                                 (task_info_t)&info, &count);
    // TASK_BASIC_INFO doesn't have thread count directly.
    // Use thread_info approach:
    thread_act_array_t threads;
    mach_msg_type_number_t thread_count = 0;
    kr = task_threads(task, &threads, &thread_count);
    if (kr == KERN_SUCCESS) {
        // Deallocate the thread list
        for (mach_msg_type_number_t i = 0; i < thread_count; i++) {
            mach_port_deallocate(task, threads[i]);
        }
        vm_deallocate(task, (vm_address_t)threads,
                      thread_count * sizeof(thread_act_t));
        return (int)thread_count;
    }
    return 0;
}

#endif // MC1_MACOS
