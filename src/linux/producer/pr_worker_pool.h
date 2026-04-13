/*
 * Mcaster1 Producer — Worker Pool with Job Tracking
 * producer/pr_worker_pool.h
 *
 * Three dedicated thread pools (video, audio, fft) with per-job
 * status tracking, progress reporting, and cancellation support.
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
#include <string>
#include <map>
#include <chrono>

namespace mc1pr {

/* ── Job types ─────────────────────────────────────────────────────────────── */

enum class JobType {
    VIDEO_ENCODE,
    AUDIO_MIXDOWN,
    FFT_ANALYSIS,
    THUMBNAIL,
    NOISE_REDUCE
};

enum class JobStatus {
    PENDING,
    RUNNING,
    COMPLETE,
    FAILED,
    CANCELLED
};

struct Job {
    int         id          = 0;
    JobType     type        = JobType::VIDEO_ENCODE;
    JobStatus   status      = JobStatus::PENDING;
    std::string params_json;            /* JSON string with job-specific params */
    int         progress    = 0;        /* 0-100 */
    std::string result_path;            /* output file path on completion */
    std::string error_msg;              /* set on failure */
    std::chrono::steady_clock::time_point submitted_at;
    std::chrono::steady_clock::time_point started_at;
    std::chrono::steady_clock::time_point completed_at;
};

/* ── Single thread pool ────────────────────────────────────────────────────── */

class WorkerPool {
public:
    using Task = std::function<void()>;

    explicit WorkerPool(int num_threads = 2);
    ~WorkerPool();

    void submit(Task task);
    void stop();

    int  queue_depth() const;
    int  active_count() const;
    int  thread_count() const { return (int)workers_.size(); }
    bool is_running() const { return running_.load(); }

private:
    std::vector<std::thread>  workers_;
    std::queue<Task>          tasks_;
    mutable std::mutex        mtx_;
    std::condition_variable   cv_;
    std::atomic<bool>         running_{true};
    std::atomic<int>          active_{0};

    void worker_loop();
};

/* ── Producer worker pool (three sub-pools + job registry) ─────────────────── */

class ProducerWorkerPool {
public:
    ProducerWorkerPool(int video_threads, int audio_threads, int fft_threads);
    ~ProducerWorkerPool();

    /* Submit a job. Returns the assigned job ID. */
    int submitJob(const Job& job);

    /* Query job status by ID. Returns false if not found. */
    bool getJobStatus(int jobId, Job& out) const;

    /* Get all jobs (active + completed). */
    std::vector<Job> getAllJobs() const;

    /* Cancel a pending or running job. Returns false if not found or already done. */
    bool cancelJob(int jobId);

    /* Pool statistics */
    int videoQueueDepth() const { return video_pool_.queue_depth(); }
    int audioQueueDepth() const { return audio_pool_.queue_depth(); }
    int fftQueueDepth()   const { return fft_pool_.queue_depth(); }
    int videoActive()     const { return video_pool_.active_count(); }
    int audioActive()     const { return audio_pool_.active_count(); }
    int fftActive()       const { return fft_pool_.active_count(); }
    int totalJobs()       const;
    int activeJobs()      const;

    void stop();

private:
    WorkerPool video_pool_;
    WorkerPool audio_pool_;
    WorkerPool fft_pool_;

    mutable std::mutex   jobs_mtx_;
    std::map<int, Job>   jobs_;
    std::atomic<int>     next_id_{1};

    void executeJob(int jobId);
    WorkerPool& poolForType(JobType type);
};

const char* jobTypeToString(JobType t);
const char* jobStatusToString(JobStatus s);
JobType jobTypeFromString(const std::string& s);

} // namespace mc1pr
