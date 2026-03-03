/*
 * Mcaster1DSPEncoder — Encoder IPC API
 * encoder_ipc_api.h
 *
 * We register all encoder-specific HTTP routes on the IPC httplib server.
 * This runs inside the encoder binary on localhost:8331 (not public-facing).
 * The admin binary proxies external requests to these routes.
 *
 * Routes owned by the encoder:
 *   /api/v1/encoders/*     — slot control, DSP, stats
 *   /api/v1/effects/*      — effects rack on audio pipeline
 *   /api/v1/ptt/*          — push-to-talk ducking
 *   /api/v1/volume          — master/slot volume
 *   /api/v1/metadata        — ICY metadata push
 *   /api/v1/playlist/*     — playlist load/skip
 *   /api/v1/devices         — PortAudio device list
 *   /api/v1/encoder/status  — encoder process health
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "external/include/httplib.h"

class AudioPipeline;

/* We register all encoder IPC routes on the given httplib::Server.
 * No auth is required — IPC is localhost-only. */
void register_encoder_ipc_routes(httplib::Server& svr, AudioPipeline* pipeline);
