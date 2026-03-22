/*
 * Mcaster1 VoicTune — HTTP/HTTPS API Server
 * voictune/vt_http_api.h
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "vt_config.h"
#include <string>

namespace mc1vt {

/* We start the VoicTune HTTP/HTTPS server(s) on configured ports.
 * This blocks the calling threads — call from main after config load.
 * Returns when all servers have been stopped. */
void vt_http_start(const VtConfig& cfg);

/* We stop all running VoicTune HTTP servers gracefully. */
void vt_http_stop();

} // namespace mc1vt
