/*
 * Mcaster1 Producer — HTTP/HTTPS API Server
 * producer/pr_http_api.h
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "pr_config.h"
#include <string>

namespace mc1pr {

/* Forward declaration */
class ProducerWorkerPool;

/* Set the worker pool pointer before calling pr_http_start. */
void pr_set_worker_pool(ProducerWorkerPool* pool);

/* Start the Producer HTTP/HTTPS server(s) on configured ports.
 * This blocks the calling thread — call from main after config load.
 * Returns when all servers have been stopped. */
void pr_http_start(const PrConfig& cfg);

/* Stop all running Producer HTTP servers gracefully. */
void pr_http_stop();

} // namespace mc1pr
