/*
 * Mcaster1 VoicTune — Thread Worker Pool
 * voictune/vt_worker_pool.h
 *
 * Fixed-size thread pool for parallel FFT analysis. Audio callback pushes
 * chunks to the queue; workers process FFT + pitch + meters without blocking.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

namespace mc1vt {

class WorkerPool {
public:
    using Task = std::function<void()>;

    explicit WorkerPool(int num_threads = 4);
    ~WorkerPool();

    void submit(Task task);
    void stop();

    int  queue_depth() const;
    int  thread_count() const { return (int)workers_.size(); }
    bool is_running() const { return running_.load(); }

private:
    std::vector<std::thread>  workers_;
    std::queue<Task>          tasks_;
    mutable std::mutex        mtx_;
    std::condition_variable   cv_;
    std::atomic<bool>         running_{true};

    void worker_loop();
};

} // namespace mc1vt
