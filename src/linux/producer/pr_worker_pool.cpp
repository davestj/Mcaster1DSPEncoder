/*
 * Mcaster1 Producer — Worker Pool with Job Tracking
 * producer/pr_worker_pool.cpp
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "pr_worker_pool.h"
#include "pr_logger.h"
#include <algorithm>
#include <cstdlib>
#include <array>

namespace mc1pr {

/* ══════════════════════════════════════════════════════════════════════════════
 * WorkerPool (single pool)
 * ══════════════════════════════════════════════════════════════════════════════ */

WorkerPool::WorkerPool(int num_threads)
{
    for (int i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this]() { worker_loop(); });
    }
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

int WorkerPool::active_count() const
{
    return active_.load();
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
        active_.fetch_add(1);
        task();
        active_.fetch_sub(1);
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 * ProducerWorkerPool (three sub-pools + job registry)
 * ══════════════════════════════════════════════════════════════════════════════ */

ProducerWorkerPool::ProducerWorkerPool(int video_threads, int audio_threads, int fft_threads)
    : video_pool_(video_threads)
    , audio_pool_(audio_threads)
    , fft_pool_(fft_threads)
{
    PR_INFO("Producer worker pools started: video=" + std::to_string(video_threads) +
            " audio=" + std::to_string(audio_threads) +
            " fft=" + std::to_string(fft_threads));
}

ProducerWorkerPool::~ProducerWorkerPool()
{
    stop();
}

int ProducerWorkerPool::submitJob(const Job& job)
{
    int id = next_id_.fetch_add(1);
    {
        std::lock_guard<std::mutex> lk(jobs_mtx_);
        Job j = job;
        j.id = id;
        j.status = JobStatus::PENDING;
        j.progress = 0;
        j.submitted_at = std::chrono::steady_clock::now();
        jobs_[id] = j;
    }

    PR_INFO("Job " + std::to_string(id) + " submitted: " + jobTypeToString(job.type));

    poolForType(job.type).submit([this, id]() {
        executeJob(id);
    });

    return id;
}

bool ProducerWorkerPool::getJobStatus(int jobId, Job& out) const
{
    std::lock_guard<std::mutex> lk(jobs_mtx_);
    auto it = jobs_.find(jobId);
    if (it == jobs_.end()) return false;
    out = it->second;
    return true;
}

std::vector<Job> ProducerWorkerPool::getAllJobs() const
{
    std::lock_guard<std::mutex> lk(jobs_mtx_);
    std::vector<Job> result;
    result.reserve(jobs_.size());
    for (const auto& kv : jobs_) {
        result.push_back(kv.second);
    }
    return result;
}

bool ProducerWorkerPool::cancelJob(int jobId)
{
    std::lock_guard<std::mutex> lk(jobs_mtx_);
    auto it = jobs_.find(jobId);
    if (it == jobs_.end()) return false;
    if (it->second.status == JobStatus::COMPLETE ||
        it->second.status == JobStatus::FAILED ||
        it->second.status == JobStatus::CANCELLED) {
        return false;
    }
    it->second.status = JobStatus::CANCELLED;
    it->second.completed_at = std::chrono::steady_clock::now();
    PR_INFO("Job " + std::to_string(jobId) + " cancelled");
    return true;
}

int ProducerWorkerPool::totalJobs() const
{
    std::lock_guard<std::mutex> lk(jobs_mtx_);
    return (int)jobs_.size();
}

int ProducerWorkerPool::activeJobs() const
{
    std::lock_guard<std::mutex> lk(jobs_mtx_);
    int count = 0;
    for (const auto& kv : jobs_) {
        if (kv.second.status == JobStatus::PENDING ||
            kv.second.status == JobStatus::RUNNING) {
            ++count;
        }
    }
    return count;
}

void ProducerWorkerPool::stop()
{
    video_pool_.stop();
    audio_pool_.stop();
    fft_pool_.stop();
    PR_INFO("All producer worker pools stopped");
}

void ProducerWorkerPool::executeJob(int jobId)
{
    /* Check if cancelled before we even start */
    {
        std::lock_guard<std::mutex> lk(jobs_mtx_);
        auto it = jobs_.find(jobId);
        if (it == jobs_.end()) return;
        if (it->second.status == JobStatus::CANCELLED) return;
        it->second.status = JobStatus::RUNNING;
        it->second.started_at = std::chrono::steady_clock::now();
    }

    PR_INFO("Job " + std::to_string(jobId) + " running");

    /*
     * TODO: Implement actual job execution via ffmpeg subprocess.
     * For now, this is a skeleton that marks the job as complete.
     *
     * Future implementation:
     * - video_encode:  fork/exec ffmpeg with transcoding args
     * - audio_mixdown: fork/exec ffmpeg with amix filter
     * - fft_analysis:  run offline FFT computation
     * - thumbnail:     fork/exec ffmpeg -ss -frames:v 1
     * - noise_reduce:  fork/exec ffmpeg with afftdn filter
     *
     * Each job type would:
     * 1. Parse params_json for input/output paths and options
     * 2. Build the ffmpeg command line
     * 3. Fork/exec and monitor stderr for progress
     * 4. Update job.progress periodically
     * 5. Set result_path or error_msg on completion
     */

    /* Check cancellation again before marking complete */
    {
        std::lock_guard<std::mutex> lk(jobs_mtx_);
        auto it = jobs_.find(jobId);
        if (it == jobs_.end()) return;
        if (it->second.status == JobStatus::CANCELLED) return;
        it->second.status = JobStatus::COMPLETE;
        it->second.progress = 100;
        it->second.completed_at = std::chrono::steady_clock::now();
    }

    PR_INFO("Job " + std::to_string(jobId) + " complete");
}

WorkerPool& ProducerWorkerPool::poolForType(JobType type)
{
    switch (type) {
        case JobType::VIDEO_ENCODE:
        case JobType::THUMBNAIL:
            return video_pool_;
        case JobType::AUDIO_MIXDOWN:
        case JobType::NOISE_REDUCE:
            return audio_pool_;
        case JobType::FFT_ANALYSIS:
            return fft_pool_;
    }
    return video_pool_;  /* fallback */
}

/* ── String conversions ────────────────────────────────────────────────────── */

const char* jobTypeToString(JobType t)
{
    switch (t) {
        case JobType::VIDEO_ENCODE:  return "video_encode";
        case JobType::AUDIO_MIXDOWN: return "audio_mixdown";
        case JobType::FFT_ANALYSIS:  return "fft_analysis";
        case JobType::THUMBNAIL:     return "thumbnail";
        case JobType::NOISE_REDUCE:  return "noise_reduce";
    }
    return "unknown";
}

const char* jobStatusToString(JobStatus s)
{
    switch (s) {
        case JobStatus::PENDING:   return "pending";
        case JobStatus::RUNNING:   return "running";
        case JobStatus::COMPLETE:  return "complete";
        case JobStatus::FAILED:    return "failed";
        case JobStatus::CANCELLED: return "cancelled";
    }
    return "unknown";
}

JobType jobTypeFromString(const std::string& s)
{
    if (s == "video_encode")  return JobType::VIDEO_ENCODE;
    if (s == "audio_mixdown") return JobType::AUDIO_MIXDOWN;
    if (s == "fft_analysis")  return JobType::FFT_ANALYSIS;
    if (s == "thumbnail")     return JobType::THUMBNAIL;
    if (s == "noise_reduce")  return JobType::NOISE_REDUCE;
    return JobType::VIDEO_ENCODE;  /* default */
}

} // namespace mc1pr
