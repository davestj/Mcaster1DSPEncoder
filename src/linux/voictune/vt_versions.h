/*
 * Mcaster1 VoicTune — Component Version Registry
 * voictune/vt_versions.h
 *
 * Single source of truth for all VoicTune module versioning.
 * Every component is tracked with semantic versioning (MAJOR.MINOR.PATCH).
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

namespace mc1vt {

struct VtComponentVersion {
    const char* component_id;
    const char* brand_name;
    const char* version;
    int         ver_major;
    int         ver_minor;
    int         ver_patch;
    const char* pre_release;
    const char* release_date;
    const char* description;
    const char* changelog;
};

static const VtComponentVersion VT_VERSIONS[] = {

    {"voictune_daemon", "Mcaster1 VoicTune Daemon v1.8.0-beta.1", "1.8.0-beta.1",
     1, 8, 0, "beta.1", "2026-03-27",
     "Voice analysis and coaching daemon — HTTP/HTTPS API, auth, DB, WebSocket, USB hotplug, Ollama AI",
     "v1.8.0-beta.1: Full skeleton — 17 source files, PortAudio, FFT, pitch, meters, coach, WebSocket, USB hotplug, MariaDB, Ollama client."},

    {"vt_fft", "Mcaster1 VoicTune FFT Analyzer v1.8.0-beta.1", "1.8.0-beta.1",
     1, 8, 0, "beta.1", "2026-03-27",
     "Real-time FFT spectrum analysis with kiss_fft — Hann window, magnitude dB, parabolic peak interpolation",
     "v1.8.0-beta.1: kiss_fft wrapper, Hann window, magnitude spectrum, peak frequency, spectral centroid."},

    {"vt_meters", "Mcaster1 VoicTune Audio Meters v1.8.0-beta.1", "1.8.0-beta.1",
     1, 8, 0, "beta.1", "2026-03-27",
     "Thread-safe RMS, peak, and LUFS (ITU-R BS.1770-4) metering with atomic reads",
     "v1.8.0-beta.1: RMS dB, peak dB, peak hold, momentary LUFS (400ms window)."},

    {"vt_pitch", "Mcaster1 VoicTune Pitch Detector v1.8.0-beta.1", "1.8.0-beta.1",
     1, 8, 0, "beta.1", "2026-03-27",
     "Musical pitch detection — autocorrelation with FFT peak hint, note name + octave + cents deviation",
     "v1.8.0-beta.1: Autocorrelation fundamental, A4=440Hz equal temperament, MIDI note mapping."},

    {"vt_worker_pool", "Mcaster1 VoicTune Worker Pool v1.8.0-beta.1", "1.8.0-beta.1",
     1, 8, 0, "beta.1", "2026-03-27",
     "Fixed-size thread pool for parallel FFT/pitch/meter analysis — condition_variable task queue",
     "v1.8.0-beta.1: Configurable thread count, submit/stop, queue depth monitoring."},

    {"ollama_client", "Mcaster1 Ollama AI Client v1.8.0-beta.1", "1.8.0-beta.1",
     1, 8, 0, "beta.1", "2026-03-27",
     "HTTP client for Ollama LLM API — chat, generate, model listing, availability check",
     "v1.8.0-beta.1: Chat/generate/list_models/is_available, thread-safe, configurable timeout."},

    {"vt_db", "Mcaster1 VoicTune Database Client v1.8.0-beta.1", "1.8.0-beta.1",
     1, 8, 0, "beta.1", "2026-03-27",
     "MariaDB client for mcaster1_voictune — sessions, voice profiles, analysis snapshots, AI interactions",
     "v1.8.0-beta.1: Full CRUD for all 4 tables, batch snapshot insert, auto-reconnect."},

    {"vt_audio_capture", "Mcaster1 VoicTune Audio Capture v1.8.0-beta.1", "1.8.0-beta.1",
     1, 8, 0, "beta.1", "2026-03-27",
     "PortAudio device enumeration and mic capture — pushes audio chunks to analysis pipeline",
     "v1.8.0-beta.1: Device enum, start/stop capture, re-enumerate on hotplug."},

    {"vt_websocket", "Mcaster1 VoicTune WebSocket Server v1.8.0-beta.1", "1.8.0-beta.1",
     1, 8, 0, "beta.1", "2026-03-27",
     "RFC 6455 WebSocket server for browser mic audio — binary PCM frames, JSON control messages",
     "v1.8.0-beta.1: Handshake, binary/text frames, broadcast, per-client threads, ping/pong."},

    {"vt_usb_monitor", "Mcaster1 VoicTune USB Monitor v1.8.0-beta.1", "1.8.0-beta.1",
     1, 8, 0, "beta.1", "2026-03-27",
     "USB/BT audio device hotplug detection via inotify on /dev/snd/",
     "v1.8.0-beta.1: inotify watch, settle delay, PortAudio re-enumeration, USB/BT device flags."},

    {"vt_coach", "Mcaster1 VoicTune Voice Coach v1.8.0-beta.1", "1.8.0-beta.1",
     1, 8, 0, "beta.1", "2026-03-27",
     "Rule-based voice coaching — level, peak, sibilance, proximity, pitch drift, pacing analysis",
     "v1.8.0-beta.1: 6 coaching rules, configurable thresholds, tip rate limiting."},

};

static constexpr int VT_VERSION_COUNT = sizeof(VT_VERSIONS) / sizeof(VT_VERSIONS[0]);

} // namespace mc1vt
