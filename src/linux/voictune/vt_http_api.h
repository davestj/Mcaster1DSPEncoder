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
#include <memory>

namespace mc1vt {

/* Forward declarations for subsystem pointers */
class VtAudioCapture;
class UsbAudioMonitor;
class VtWebSocket;
class VtDb;
class VoiceCoach;
class OllamaClient;
class AnalysisState;

/* Set subsystem pointers before calling vt_http_start.
 * These are used by API endpoints to return real data.
 * All pointers are optional — endpoints degrade gracefully. */
struct VtSubsystems {
    VtAudioCapture*  audio_capture  = nullptr;
    UsbAudioMonitor* usb_monitor    = nullptr;
    VtWebSocket*     websocket      = nullptr;
    VtDb*            db             = nullptr;
    VoiceCoach*      coach          = nullptr;
    OllamaClient*    ollama         = nullptr;
    AnalysisState*   analysis       = nullptr;
};

void vt_set_subsystems(const VtSubsystems& sub);

/* Start the VoicTune HTTP/HTTPS server(s) on configured ports.
 * This blocks the calling thread — call from main after config load.
 * Returns when all servers have been stopped. */
void vt_http_start(const VtConfig& cfg);

/* Stop all running VoicTune HTTP servers gracefully. */
void vt_http_stop();

} // namespace mc1vt
