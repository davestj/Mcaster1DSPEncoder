/*
 * Mcaster1 VoicTune — Thread Worker Pool
 * voictune/vt_worker_pool.cpp
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "vt_worker_pool.h"
#include "vt_logger.h"

namespace mc1vt {

WorkerPool::WorkerPool(int num_threads)
{
    for (int i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this]() { worker_loop(); });
    }
    VT_INFO("Worker pool started: " + std::to_string(num_threads) + " threads");
}

WorkerPool::~WorkerPool()
{
    stop();
}

void WorkerPool::submit(Task task)
{
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!running_.load()) return;
        tasks_.push(std::move(task));
    }
    cv_.notify_one();
}

void WorkerPool::stop()
{
    running_.store(false);
    cv_.notify_all();
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
    workers_.clear();
}

int WorkerPool::queue_depth() const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return (int)tasks_.size();
}

void WorkerPool::worker_loop()
{
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk, [this]() { return !running_.load() || !tasks_.empty(); });
            if (!running_.load() && tasks_.empty()) return;
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        task();
    }
}

} // namespace mc1vt
