/*
 * Mcaster1 VoicTune — Voice Analysis & Coaching Daemon
 * voictune/main_voictune.cpp
 *
 * Entry point for the mcaster1-voictune daemon. Provides real-time voice
 * analysis (FFT, pitch detection, RMS/LUFS metering), voice coaching,
 * and Ollama AI integration for broadcast/podcast voice tuning.
 *
 * Ports: 8350 (HTTP), 8354 (HTTPS), 8355 (WebSocket for browser mic)
 * Logs:  /var/log/mcaster1/voictune.log, voictune_error.log
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "vt_config.h"
#include "vt_http_api.h"
#include "vt_logger.h"
#include "vt_audio_capture.h"
#include "vt_usb_monitor.h"
#include "vt_websocket.h"
#include "vt_db.h"
#include "vt_coach.h"
#include "vt_fft.h"
#include "vt_meters.h"
#include "vt_pitch.h"
#include "vt_worker_pool.h"
#include "vt_analysis_state.h"
#include "ollama_client.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <unistd.h>
#include <getopt.h>
#include <string>
#include <atomic>
#include <chrono>
#include <vector>
#include <mutex>
#include <cmath>
#include <algorithm>

#define VT_VERSION "2.0.0"
#define VT_BANNER  "Mcaster1 VoicTune v" VT_VERSION " — Voice Analysis & Coaching Daemon"

static std::atomic<bool> g_shutdown{false};

static void signal_handler(int sig)
{
    fprintf(stderr, "\n[voictune] Caught signal %d — shutting down\n", sig);
    g_shutdown.store(true);
    mc1vt::vt_http_stop();
}

static void print_usage(const char* progname)
{
    fprintf(stderr,
        VT_BANNER "\n\n"
        "Usage: %s [OPTIONS]\n\n"
        "Options:\n"
        "  -c, --config FILE      YAML config file (required)\n"
        "  -p, --port PORT        Override HTTP port (default: 8350)\n"
        "      --ssl-port PORT    Override HTTPS port (default: 8354)\n"
        "      --ws-port PORT     Override WebSocket port (default: 8355)\n"
        "      --log-level N      Log level 1-5 (1=CRIT, 5=DEBUG)\n"
        "      --log-dir DIR      Log directory (default: /var/log/mcaster1)\n"
        "  -v, --verbose          Set log level to 5 (DEBUG)\n"
        "  -h, --help             Show this help\n"
        "  -V, --version          Show version\n"
        "\n"
        "Ports:\n"
        "  8350  HTTP  API (admin, meters, status)\n"
        "  8354  HTTPS API (same routes, TLS)\n"
        "  8355  WebSocket (browser mic audio stream)\n"
        "\n"
        "Example:\n"
        "  %s --config src/linux/config/mcaster1_voictune.yaml -v\n"
        "\n",
        progname, progname);
}

int main(int argc, char* argv[])
{
    fprintf(stdout, VT_BANNER "\n");

    /* ── CLI option parsing ──────────────────────────────────────────── */
    const char* config_file = nullptr;
    int cli_port      = 0;
    int cli_ssl_port  = 0;
    int cli_ws_port   = 0;
    int cli_log_level = 0;
    const char* cli_log_dir = nullptr;
    bool cli_verbose  = false;

    static struct option long_opts[] = {
        {"config",    required_argument, nullptr, 'c'},
        {"port",      required_argument, nullptr, 'p'},
        {"ssl-port",  required_argument, nullptr, 'S'},
        {"ws-port",   required_argument, nullptr, 'W'},
        {"log-level", required_argument, nullptr, 'L'},
        {"log-dir",   required_argument, nullptr, 'D'},
        {"verbose",   no_argument,       nullptr, 'v'},
        {"help",      no_argument,       nullptr, 'h'},
        {"version",   no_argument,       nullptr, 'V'},
        {nullptr, 0, nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "c:p:vhV", long_opts, nullptr)) != -1) {
        switch (opt) {
            case 'c': config_file   = optarg; break;
            case 'p': cli_port      = atoi(optarg); break;
            case 'S': cli_ssl_port  = atoi(optarg); break;
            case 'W': cli_ws_port   = atoi(optarg); break;
            case 'L': cli_log_level = atoi(optarg); break;
            case 'D': cli_log_dir   = optarg; break;
            case 'v': cli_verbose   = true; break;
            case 'V':
                fprintf(stdout, "mcaster1-voictune %s\n", VT_VERSION);
                return 0;
            case 'h':
            default:
                print_usage(argv[0]);
                return (opt == 'h') ? 0 : 1;
        }
    }

    if (!config_file) {
        fprintf(stderr, "[voictune] ERROR: --config is required\n");
        print_usage(argv[0]);
        return 1;
    }

    /* ── Load YAML config ────────────────────────────────────────────── */
    mc1vt::VtConfig cfg;
    if (!mc1vt::vt_load_config(config_file, cfg)) {
        fprintf(stderr, "[voictune] ERROR: Failed to load config from %s\n", config_file);
        return 1;
    }

    /* ── Apply CLI overrides (CLI wins over YAML) ────────────────────── */
    if (cli_port > 0)      cfg.http.port     = cli_port;
    if (cli_ssl_port > 0)  cfg.http.ssl_port = cli_ssl_port;
    if (cli_ws_port > 0)   cfg.websocket.port = cli_ws_port;
    if (cli_log_dir)       cfg.log.dir        = cli_log_dir;
    if (cli_log_level > 0) cfg.log.level      = cli_log_level;
    if (cli_verbose)       cfg.log.level      = 5;

    /* ── Initialize logger ───────────────────────────────────────────── */
    auto& logger = mc1vt::VtLogger::instance();
    logger.set_log_dir(cfg.log.dir);
    logger.set_level(cfg.log.level);

    VT_INFO(VT_BANNER);
    VT_INFO("Config loaded from: " + std::string(config_file));
    VT_INFO("HTTP port: " + std::to_string(cfg.http.port) +
            ", HTTPS port: " + std::to_string(cfg.http.ssl_port) +
            ", WS port: " + std::to_string(cfg.websocket.port));
    VT_INFO("Log level: " + std::to_string(cfg.log.level) +
            ", Log dir: " + cfg.log.dir);
    VT_INFO("Ollama endpoint: " + cfg.ollama.endpoint +
            ", model: " + cfg.ollama.model);

    /* ── Signal handling ─────────────────────────────────────────────── */
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGHUP,  SIG_IGN);
    signal(SIGPIPE, SIG_IGN);

    /* ── Initialize subsystems ──────────────────────────────────────── */

    /* Audio capture (PortAudio) */
    mc1vt::VtAudioCapture audio_capture;
    if (audio_capture.init()) {
        VT_INFO("PortAudio initialized — " +
                std::to_string(audio_capture.list_devices().size()) + " devices");
    }

    /* USB/BT hotplug monitor */
    mc1vt::UsbAudioMonitor usb_monitor;
    if (cfg.audio.hotplug_enabled) {
        usb_monitor.start([&audio_capture]() {
            VT_INFO("Audio device change detected — re-enumerating");
            audio_capture.re_enumerate();
        }, cfg.audio.hotplug_settle_ms);
    }

    /* Database */
    mc1vt::VtDb db;
    if (!cfg.database.user.empty()) {
        db.connect(cfg.database.host, cfg.database.port,
                   cfg.database.user, cfg.database.password, cfg.database.db_name);
    } else {
        db.connect_defaults(cfg.database.db_name);
    }

    /* Voice coach (rule-based) */
    mc1vt::VoiceCoach coach;

    /* Ollama AI client */
    mc1vt::OllamaClient ollama(cfg.ollama.endpoint);
    if (ollama.is_available()) {
        VT_INFO("Ollama AI available at " + cfg.ollama.endpoint);
    } else {
        VT_WARN("Ollama not reachable at " + cfg.ollama.endpoint +
                " — AI features will degrade gracefully");
    }

    /* ── Analysis pipeline (VT-2) ──────────────────────────────────── */

    /* Shared analysis state (thread-safe bridge: workers → HTTP API) */
    mc1vt::AnalysisState analysis_state;

    /* DSP analysis engines */
    mc1vt::FftAnalyzer    fft_analyzer(cfg.analysis.fft_size, cfg.audio.sample_rate);
    mc1vt::AudioMeters    meters;
    mc1vt::PitchDetector  pitch_detector;

    /* Worker pool for off-loading analysis from audio callback */
    mc1vt::WorkerPool     worker_pool(cfg.worker_threads);

    /* DB snapshot batch buffer (guarded by batch_mtx) */
    std::mutex batch_mtx;
    std::vector<mc1vt::DbAnalysisSnapshot> snapshot_batch;
    static const int BATCH_SIZE = 10;

    /* Audio chunk processing lambda — submitted to worker pool.
     * Runs FFT, meters, pitch, coach; updates AnalysisState; batches DB writes. */
    auto process_audio_chunk = [&](mc1vt::AudioChunk chunk) {
        /* Downmix to mono if multi-channel */
        std::vector<float> mono;
        const float* mono_ptr = chunk.samples.data();
        int mono_frames = chunk.frames;

        if (chunk.channels > 1) {
            mono.resize(chunk.frames);
            for (int i = 0; i < chunk.frames; ++i) {
                float sum = 0.0f;
                for (int c = 0; c < chunk.channels; ++c) {
                    sum += chunk.samples[i * chunk.channels + c];
                }
                mono[i] = sum / (float)chunk.channels;
            }
            mono_ptr = mono.data();
        }

        /* 1. FFT analysis */
        fft_analyzer.analyze(mono_ptr, mono_frames);
        auto mag_db = fft_analyzer.get_magnitude_db();
        float peak_freq = fft_analyzer.peak_frequency_hz();
        float centroid  = fft_analyzer.spectral_centroid_hz();

        /* 2. RMS / peak / LUFS meters */
        meters.process(mono_ptr, mono_frames, chunk.sample_rate);

        /* 3. Pitch detection (with FFT peak hint) */
        auto pitch = pitch_detector.detect(mono_ptr, mono_frames, chunk.sample_rate, peak_freq);

        /* 4. Update shared state (atomics + brief mutex locks) */
        analysis_state.set_rms_db(meters.rms_db());
        analysis_state.set_peak_db(meters.peak_db());
        analysis_state.set_lufs(meters.lufs());
        analysis_state.set_peak_hold_db(meters.peak_hold_db());

        analysis_state.set_pitch_hz(pitch.frequency_hz);
        analysis_state.set_pitch_confidence(pitch.confidence);
        analysis_state.set_midi_note(pitch.midi_note);
        analysis_state.set_cents_off(pitch.cents_off);
        analysis_state.set_note_name(pitch.note_name);

        analysis_state.set_spectral_centroid(centroid);
        analysis_state.set_peak_frequency(peak_freq);
        analysis_state.set_spectrum(std::move(mag_db));

        /* Push waveform samples (downsample for display) */
        int wf_step = std::max(1, mono_frames / 128);
        for (int i = 0; i < mono_frames; i += wf_step) {
            analysis_state.push_waveform(&mono_ptr[i], 1);
        }

        analysis_state.increment_chunk_count();

        /* 5. Voice coaching */
        bool is_speech = meters.rms_db() > -50.0f;

        /* Compute spectral energy ratios for coach */
        float high_energy = 0.0f, low_energy = 0.0f, total_energy = 0.0f;
        auto spec = analysis_state.spectrum();
        float bin_res = fft_analyzer.bin_resolution_hz();
        for (int i = 0; i < (int)spec.size(); ++i) {
            float freq = i * bin_res;
            float lin = std::pow(10.0f, spec[i] / 20.0f);
            total_energy += lin;
            if (freq >= 4000.0f && freq <= 8000.0f) high_energy += lin;
            if (freq >= 80.0f && freq <= 300.0f) low_energy += lin;
        }
        float high_ratio = (total_energy > 1e-10f) ? high_energy / total_energy : 0.0f;
        float low_ratio  = (total_energy > 1e-10f) ? low_energy / total_energy : 0.0f;

        /* VT-4: Compute plosive energy (sub-80Hz transient energy ratio) */
        float sub80_energy = 0.0f;
        for (int i = 0; i < (int)spec.size(); ++i) {
            float freq = i * bin_res;
            if (freq > 80.0f) break;
            float lin = std::pow(10.0f, spec[i] / 20.0f);
            sub80_energy += lin;
        }
        float plosive_ratio = (total_energy > 1e-10f) ? sub80_energy / total_energy : 0.0f;

        /* VT-4: Noise floor estimate (use RMS during silence) */
        static float noise_floor_est = -96.0f;
        if (!is_speech) {
            float cur_rms = meters.rms_db();
            /* Exponential moving average for stability */
            if (noise_floor_est < -90.0f) {
                noise_floor_est = cur_rms;
            } else {
                noise_floor_est = noise_floor_est * 0.9f + cur_rms * 0.1f;
            }
        }

        mc1vt::VoiceSnapshot snap;
        snap.rms_db              = meters.rms_db();
        snap.peak_db             = meters.peak_db();
        snap.lufs                = meters.lufs();
        snap.pitch_hz            = pitch.frequency_hz;
        snap.cents_off           = pitch.cents_off;
        snap.note_name           = pitch.note_name;
        snap.spectral_centroid_hz = centroid;
        snap.high_freq_energy    = high_ratio;
        snap.low_freq_energy     = low_ratio;
        snap.is_speech           = is_speech;

        /* VT-4: Enhanced coaching fields */
        snap.dynamic_range_db    = meters.peak_db() - meters.rms_db();
        snap.plosive_energy      = plosive_ratio;
        snap.noise_floor_db      = noise_floor_est;

        auto tips = coach.analyze(snap);
        if (!tips.empty()) {
            analysis_state.set_tips(tips);
        }

        /* 6. DB snapshot batch (if session is active) */
        if (analysis_state.session_active() && db.is_connected()) {
            mc1vt::DbAnalysisSnapshot dbsnap;
            dbsnap.session_id   = analysis_state.session_id();
            dbsnap.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            dbsnap.rms_db       = meters.rms_db();
            dbsnap.peak_db      = meters.peak_db();
            dbsnap.lufs         = meters.lufs();
            dbsnap.pitch_hz     = pitch.frequency_hz;
            dbsnap.note_name    = pitch.note_name;
            dbsnap.cents_off    = pitch.cents_off;

            bool do_flush = false;
            {
                std::lock_guard<std::mutex> lk(batch_mtx);
                snapshot_batch.push_back(std::move(dbsnap));
                if ((int)snapshot_batch.size() >= BATCH_SIZE) {
                    do_flush = true;
                }
            }
            if (do_flush) {
                std::vector<mc1vt::DbAnalysisSnapshot> flush_batch;
                {
                    std::lock_guard<std::mutex> lk(batch_mtx);
                    flush_batch.swap(snapshot_batch);
                }
                db.insert_snapshots_batch(flush_batch);
            }
        }
    };

    /* Audio capture callback — pushes chunks to worker pool (never blocks) */
    auto audio_callback = [&](const mc1vt::AudioChunk& chunk) {
        if (!worker_pool.is_running()) return;
        /* Copy chunk into lambda capture for worker thread ownership */
        mc1vt::AudioChunk chunk_copy = chunk;
        worker_pool.submit([&process_audio_chunk, c = std::move(chunk_copy)]() mutable {
            process_audio_chunk(std::move(c));
        });
    };

    /* Start audio capture from configured device */
    analysis_state.set_analyzing(true);
    if (audio_capture.is_capturing()) {
        VT_INFO("Audio capture already running");
    } else {
        bool started = audio_capture.start(
            cfg.audio.input_device_index,
            cfg.audio.sample_rate,
            cfg.audio.channels,
            cfg.audio.buffer_frames,
            audio_callback);
        if (started) {
            VT_INFO("Audio capture pipeline started — chunks → worker pool → analysis");
        } else {
            VT_WARN("Audio capture failed to start — running in browser-mic-only mode");
        }
    }

    /* WebSocket server (browser mic audio → same analysis pipeline) */
    mc1vt::VtWebSocket websocket;
    mc1vt::WsAudioCallback ws_audio_cb = [&](const mc1vt::WsAudioFrame& frame) {
        if (!worker_pool.is_running()) return;
        /* Convert WsAudioFrame to AudioChunk for unified processing */
        mc1vt::AudioChunk chunk;
        chunk.samples     = frame.samples;
        chunk.channels    = frame.channels;
        chunk.sample_rate = frame.sample_rate;
        chunk.frames      = (int)frame.samples.size() / std::max(1, frame.channels);
        worker_pool.submit([&process_audio_chunk, c = std::move(chunk)]() mutable {
            process_audio_chunk(std::move(c));
        });
    };

    if (cfg.websocket.port > 0) {
        if (websocket.start(cfg.websocket.port, cfg.websocket.max_clients, ws_audio_cb)) {
            VT_INFO("WebSocket server started on port " + std::to_string(cfg.websocket.port));
        } else {
            VT_WARN("WebSocket server failed to start on port " + std::to_string(cfg.websocket.port));
        }
    }

    /* Register subsystem pointers with HTTP API */
    mc1vt::VtSubsystems sub;
    sub.audio_capture = &audio_capture;
    sub.usb_monitor   = &usb_monitor;
    sub.websocket     = &websocket;
    sub.db            = &db;
    sub.coach         = &coach;
    sub.ollama        = &ollama;
    sub.analysis      = &analysis_state;
    mc1vt::vt_set_subsystems(sub);

    /* ── Start HTTP server (blocks until stop) ───────────────────────── */
    VT_INFO("Starting HTTP API server...");
    mc1vt::vt_http_start(cfg);

    /* ── Cleanup ─────────────────────────────────────────────────────── */
    VT_INFO("VoicTune daemon shutting down...");

    analysis_state.set_analyzing(false);
    worker_pool.stop();
    websocket.stop();
    usb_monitor.stop();
    audio_capture.terminate();

    /* Flush remaining DB snapshots */
    {
        std::lock_guard<std::mutex> lk(batch_mtx);
        if (!snapshot_batch.empty() && db.is_connected()) {
            db.insert_snapshots_batch(snapshot_batch);
            snapshot_batch.clear();
        }
    }

    db.disconnect();

    VT_INFO("Clean shutdown complete.");
    fprintf(stdout, "[voictune] Clean shutdown complete.\n");
    return 0;
}
