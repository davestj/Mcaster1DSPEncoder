/*
 * Mcaster1 DSP Encoder — Mcaster1AMP Plugin
 * dsp_mcaster1encoder.cpp — AMP Plugin SDK v2 DSP encoder + Icecast2 streamer
 *
 * Receives PCM audio from Mcaster1AMP's playback engine via amp_plugin_dsp_process(),
 * encodes to MP3 (LAME), and streams to Icecast2/Shoutcast via TCP.
 *
 * Part of the Mcaster1 broadcast ecosystem alongside Winamp, Foobar2000, and WaCCup plugins.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include "plugin_api.h"
#include "encoder_widget.h"

#include <lame/lame.h>
#include <yaml.h>

#include <atomic>
#include <thread>
#include <mutex>
#include <string>
#include <vector>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <functional>

// ═══════════════════════════════════════════════════════════════════════════════
//  Lock-free SPSC ring buffer (audio thread → encoder thread)
// ═══════════════════════════════════════════════════════════════════════════════

class SpscRingBuffer {
public:
    explicit SpscRingBuffer(size_t capacity)
        : buf_(capacity), capacity_(capacity) {}

    size_t available_read() const {
        size_t w = write_pos_.load(std::memory_order_acquire);
        size_t r = read_pos_.load(std::memory_order_relaxed);
        return (w >= r) ? (w - r) : (capacity_ - r + w);
    }

    size_t available_write() const {
        return capacity_ - 1 - available_read();
    }

    bool write(const float* data, size_t count) {
        if (available_write() < count) return false;
        size_t w = write_pos_.load(std::memory_order_relaxed);
        for (size_t i = 0; i < count; ++i)
            buf_[(w + i) % capacity_] = data[i];
        write_pos_.store((w + count) % capacity_, std::memory_order_release);
        return true;
    }

    bool read(float* data, size_t count) {
        if (available_read() < count) return false;
        size_t r = read_pos_.load(std::memory_order_relaxed);
        for (size_t i = 0; i < count; ++i)
            data[i] = buf_[(r + i) % capacity_];
        read_pos_.store((r + count) % capacity_, std::memory_order_release);
        return true;
    }

    void reset() {
        write_pos_.store(0, std::memory_order_relaxed);
        read_pos_.store(0, std::memory_order_relaxed);
    }

private:
    std::vector<float> buf_;
    size_t capacity_;
    std::atomic<size_t> write_pos_{0};
    std::atomic<size_t> read_pos_{0};
};

// ═══════════════════════════════════════════════════════════════════════════════
//  Encoder configuration
// ═══════════════════════════════════════════════════════════════════════════════

struct EncoderConfig {
    std::string host          = "localhost";
    uint16_t    port          = 8000;
    std::string mount         = "/stream";
    std::string username      = "source";
    std::string password      = "";
    std::string station_name  = "Mcaster1AMP Stream";
    std::string description   = "Powered by Mcaster1 DSP Encoder";
    std::string genre         = "Various";
    std::string url           = "https://mcaster1.com";
    int         bitrate       = 128;
    int         sample_rate   = 44100;
    int         channels      = 2;
};

// ═══════════════════════════════════════════════════════════════════════════════
//  Global state
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

// Plugin metadata
static const AmpPluginInfo kPluginInfo = {
    "MC1 DSP Encoder",
    "Mcaster1 Audio / David St. John",
    "Broadcast encoder — streams Mcaster1AMP playback to Icecast2/Shoutcast. "
    "MP3 encoding via LAME. Part of the Mcaster1 broadcast ecosystem.",
    "1.0.0",
    AMP_PLUGIN_DSP,
    AMP_PLUGIN_API_VERSION
};

// State
std::atomic<bool>     g_running{false};     // encoder thread alive
std::atomic<bool>     g_enabled{false};     // user wants to stream
std::atomic<bool>     g_connected{false};   // TCP connected to server
std::atomic<uint64_t> g_bytes_sent{0};      // total bytes streamed
std::atomic<int>      g_state{0};           // 0=idle, 1=connecting, 2=live, 3=error

// Configuration (protected by mutex for non-atomic access)
EncoderConfig   g_config;
std::mutex      g_config_mutex;

// Audio ring buffer (audio thread → encoder thread)
SpscRingBuffer* g_ring = nullptr;

// LAME encoder
lame_global_flags* g_lame = nullptr;

// TCP socket
SOCKET g_socket = INVALID_SOCKET;

// Encoder background thread
std::thread g_encoder_thread;

// Host context
std::string g_data_dir;
AmpLogCallback g_log = nullptr;

// UI widget
mc1enc::EncoderWidget* g_widget = nullptr;

// Ring buffer capacity: ~370ms of stereo 44.1kHz audio
static constexpr size_t kRingCapacity = 16384 * 2;

// LAME MP3 frame size
static constexpr int kMp3FrameSamples = 1152;

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
//  Utility: Base64 encode (for HTTP Basic auth)
// ═══════════════════════════════════════════════════════════════════════════════

static const char kB64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64_encode(const std::string& input) {
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(kB64Table[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6)
        out.push_back(kB64Table[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4)
        out.push_back('=');
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Utility: Host logging
// ═══════════════════════════════════════════════════════════════════════════════

static void plugin_log(int level, const char* fmt, ...) {
    if (!g_log) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    g_log(level, buf);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  YAML config load / save
// ═══════════════════════════════════════════════════════════════════════════════

static std::string config_path() {
    return g_data_dir + "/mcaster1encoder.yaml";
}

static void load_config() {
    FILE* fp = fopen(config_path().c_str(), "rb");
    if (!fp) return;

    yaml_parser_t parser;
    yaml_event_t  event;
    yaml_parser_initialize(&parser);
    yaml_parser_set_input_file(&parser, fp);

    std::string key;
    bool in_mapping = false;
    std::lock_guard<std::mutex> lock(g_config_mutex);

    while (true) {
        if (!yaml_parser_parse(&parser, &event)) break;
        if (event.type == YAML_STREAM_END_EVENT) { yaml_event_delete(&event); break; }

        if (event.type == YAML_MAPPING_START_EVENT) { in_mapping = true; }
        else if (event.type == YAML_MAPPING_END_EVENT) { in_mapping = false; }
        else if (event.type == YAML_SCALAR_EVENT && in_mapping) {
            const char* val = reinterpret_cast<const char*>(event.data.scalar.value);
            if (key.empty()) {
                key = val;
            } else {
                if      (key == "host")          g_config.host = val;
                else if (key == "port")          g_config.port = static_cast<uint16_t>(atoi(val));
                else if (key == "mount")         g_config.mount = val;
                else if (key == "username")      g_config.username = val;
                else if (key == "password")      g_config.password = val;
                else if (key == "station_name")  g_config.station_name = val;
                else if (key == "description")   g_config.description = val;
                else if (key == "genre")         g_config.genre = val;
                else if (key == "url")           g_config.url = val;
                else if (key == "bitrate")       g_config.bitrate = atoi(val);
                else if (key == "sample_rate")   g_config.sample_rate = atoi(val);
                else if (key == "channels")      g_config.channels = atoi(val);
                key.clear();
            }
        }
        yaml_event_delete(&event);
    }

    yaml_parser_delete(&parser);
    fclose(fp);
    plugin_log(4, "MC1 Encoder: config loaded from %s", config_path().c_str());
}

static void save_config() {
    FILE* fp = fopen(config_path().c_str(), "wb");
    if (!fp) return;

    std::lock_guard<std::mutex> lock(g_config_mutex);

    auto wy = [&](const char* k, const std::string& v) {
        fprintf(fp, "%s: \"%s\"\n", k, v.c_str());
    };
    auto wi = [&](const char* k, int v) {
        fprintf(fp, "%s: %d\n", k, v);
    };

    fprintf(fp, "# Mcaster1 DSP Encoder — Mcaster1AMP Plugin Config\n");
    fprintf(fp, "# Auto-generated — do not edit while streaming\n\n");
    wy("host",         g_config.host);
    wi("port",         g_config.port);
    wy("mount",        g_config.mount);
    wy("username",     g_config.username);
    wy("password",     g_config.password);
    wy("station_name", g_config.station_name);
    wy("description",  g_config.description);
    wy("genre",        g_config.genre);
    wy("url",          g_config.url);
    wi("bitrate",      g_config.bitrate);
    wi("sample_rate",  g_config.sample_rate);
    wi("channels",     g_config.channels);

    fclose(fp);
    plugin_log(5, "MC1 Encoder: config saved to %s", config_path().c_str());
}

// ═══════════════════════════════════════════════════════════════════════════════
//  LAME encoder init / teardown
// ═══════════════════════════════════════════════════════════════════════════════

static bool init_lame() {
    if (g_lame) { lame_close(g_lame); g_lame = nullptr; }

    g_lame = lame_init();
    if (!g_lame) return false;

    std::lock_guard<std::mutex> lock(g_config_mutex);
    lame_set_in_samplerate(g_lame, g_config.sample_rate);
    lame_set_out_samplerate(g_lame, g_config.sample_rate);
    lame_set_num_channels(g_lame, g_config.channels);
    lame_set_brate(g_lame, g_config.bitrate);
    lame_set_mode(g_lame, g_config.channels == 2 ? JOINT_STEREO : MONO);
    lame_set_quality(g_lame, 2);  // high quality
    lame_set_VBR(g_lame, vbr_off);

    if (lame_init_params(g_lame) < 0) {
        plugin_log(2, "MC1 Encoder: LAME init_params failed");
        lame_close(g_lame);
        g_lame = nullptr;
        return false;
    }

    plugin_log(4, "MC1 Encoder: LAME initialized — %d kbps, %d Hz, %d ch",
               g_config.bitrate, g_config.sample_rate, g_config.channels);
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Icecast2 TCP connection
// ═══════════════════════════════════════════════════════════════════════════════

static bool connect_to_server() {
    if (g_socket != INVALID_SOCKET) {
        closesocket(g_socket);
        g_socket = INVALID_SOCKET;
    }

    std::string host, mount, username, password, station_name, description, genre, url_str;
    uint16_t port;
    int bitrate;

    {
        std::lock_guard<std::mutex> lock(g_config_mutex);
        host         = g_config.host;
        port         = g_config.port;
        mount        = g_config.mount;
        username     = g_config.username;
        password     = g_config.password;
        station_name = g_config.station_name;
        description  = g_config.description;
        genre        = g_config.genre;
        url_str      = g_config.url;
        bitrate      = g_config.bitrate;
    }

    plugin_log(4, "MC1 Encoder: connecting to %s:%d%s", host.c_str(), port, mount.c_str());
    g_state.store(1);  // connecting

    // Resolve host
    struct addrinfo hints = {}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    std::string port_str = std::to_string(port);
    int rc = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result);
    if (rc != 0 || !result) {
        plugin_log(2, "MC1 Encoder: DNS resolve failed for %s: %d", host.c_str(), rc);
        g_state.store(3);
        return false;
    }

    // Create socket
    g_socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (g_socket == INVALID_SOCKET) {
        freeaddrinfo(result);
        plugin_log(2, "MC1 Encoder: socket() failed");
        g_state.store(3);
        return false;
    }

    // Set send timeout (5 seconds)
    DWORD timeout_ms = 5000;
    setsockopt(g_socket, SOL_SOCKET, SO_SNDTIMEO,
               reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
    setsockopt(g_socket, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));

    // Connect
    rc = ::connect(g_socket, result->ai_addr, static_cast<int>(result->ai_addrlen));
    freeaddrinfo(result);
    if (rc != 0) {
        plugin_log(2, "MC1 Encoder: connect() failed — server unreachable");
        closesocket(g_socket);
        g_socket = INVALID_SOCKET;
        g_state.store(3);
        return false;
    }

    // Send Icecast2 SOURCE request
    std::string auth = base64_encode(username + ":" + password);
    std::string req =
        "SOURCE " + mount + " HTTP/1.0\r\n"
        "Authorization: Basic " + auth + "\r\n"
        "Content-Type: audio/mpeg\r\n"
        "ice-name: " + station_name + "\r\n"
        "ice-description: " + description + "\r\n"
        "ice-genre: " + genre + "\r\n"
        "ice-url: " + url_str + "\r\n"
        "ice-bitrate: " + std::to_string(bitrate) + "\r\n"
        "ice-public: 0\r\n"
        "ice-audio-info: bitrate=" + std::to_string(bitrate) + "\r\n"
        "User-Agent: Mcaster1DSPEncoder/1.0 (Mcaster1AMP Plugin)\r\n"
        "\r\n";

    int sent = send(g_socket, req.c_str(), static_cast<int>(req.size()), 0);
    if (sent == SOCKET_ERROR) {
        plugin_log(2, "MC1 Encoder: failed to send SOURCE request");
        closesocket(g_socket);
        g_socket = INVALID_SOCKET;
        g_state.store(3);
        return false;
    }

    // Read server response
    char resp[1024] = {};
    int recv_len = recv(g_socket, resp, sizeof(resp) - 1, 0);
    if (recv_len <= 0 || strstr(resp, "200") == nullptr) {
        plugin_log(2, "MC1 Encoder: server rejected — %s",
                   recv_len > 0 ? resp : "no response");
        closesocket(g_socket);
        g_socket = INVALID_SOCKET;
        g_state.store(3);
        return false;
    }

    g_connected.store(true);
    g_bytes_sent.store(0);
    g_state.store(2);  // live
    plugin_log(4, "MC1 Encoder: LIVE — streaming to %s:%d%s",
               host.c_str(), port, mount.c_str());
    return true;
}

static void disconnect_from_server() {
    g_connected.store(false);
    if (g_socket != INVALID_SOCKET) {
        closesocket(g_socket);
        g_socket = INVALID_SOCKET;
    }
    g_state.store(0);  // idle
    plugin_log(4, "MC1 Encoder: disconnected");
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Encoder background thread
// ═══════════════════════════════════════════════════════════════════════════════

static void encoder_thread_func() {
    const int channels = 2;
    const int frame_floats = kMp3FrameSamples * channels;
    std::vector<float> pcm(frame_floats);
    std::vector<unsigned char> mp3_buf(frame_floats * 2);  // oversized for safety

    while (g_running.load(std::memory_order_relaxed)) {
        // Wait for user to enable streaming
        if (!g_enabled.load(std::memory_order_relaxed)) {
            if (g_connected.load(std::memory_order_relaxed))
                disconnect_from_server();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Connect if not connected
        if (!g_connected.load(std::memory_order_relaxed)) {
            if (!init_lame()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                continue;
            }
            if (!connect_to_server()) {
                // Retry after delay
                std::this_thread::sleep_for(std::chrono::milliseconds(5000));
                continue;
            }
            if (g_ring) g_ring->reset();
        }

        // Read a frame from the ring buffer
        if (!g_ring || g_ring->available_read() < static_cast<size_t>(frame_floats)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        g_ring->read(pcm.data(), frame_floats);

        // Encode to MP3
        if (!g_lame) continue;
        int mp3_bytes = lame_encode_buffer_interleaved_ieee_float(
            g_lame, pcm.data(), kMp3FrameSamples,
            mp3_buf.data(), static_cast<int>(mp3_buf.size()));

        if (mp3_bytes < 0) {
            plugin_log(3, "MC1 Encoder: LAME encode error %d", mp3_bytes);
            continue;
        }

        if (mp3_bytes > 0 && g_socket != INVALID_SOCKET) {
            int sent = send(g_socket,
                            reinterpret_cast<const char*>(mp3_buf.data()),
                            mp3_bytes, 0);
            if (sent == SOCKET_ERROR) {
                plugin_log(3, "MC1 Encoder: send() failed — disconnecting");
                disconnect_from_server();
                if (g_lame) { lame_close(g_lame); g_lame = nullptr; }
                continue;
            }
            g_bytes_sent.fetch_add(static_cast<uint64_t>(mp3_bytes),
                                   std::memory_order_relaxed);
        }
    }

    // Flush LAME
    if (g_lame && g_connected.load()) {
        int flush_bytes = lame_encode_flush(
            g_lame, mp3_buf.data(), static_cast<int>(mp3_buf.size()));
        if (flush_bytes > 0 && g_socket != INVALID_SOCKET) {
            send(g_socket, reinterpret_cast<const char*>(mp3_buf.data()),
                 flush_bytes, 0);
        }
    }

    disconnect_from_server();
    if (g_lame) { lame_close(g_lame); g_lame = nullptr; }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Public control API (called from UI widget)
// ═══════════════════════════════════════════════════════════════════════════════

void mc1enc_start_streaming() {
    g_enabled.store(true);
}

void mc1enc_stop_streaming() {
    g_enabled.store(false);
}

bool mc1enc_is_connected() {
    return g_connected.load();
}

int mc1enc_state() {
    return g_state.load();
}

uint64_t mc1enc_bytes_sent() {
    return g_bytes_sent.load();
}

void mc1enc_update_config(const EncoderConfig& cfg) {
    {
        std::lock_guard<std::mutex> lock(g_config_mutex);
        g_config = cfg;
    }
    save_config();
}

EncoderConfig mc1enc_get_config() {
    std::lock_guard<std::mutex> lock(g_config_mutex);
    return g_config;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  AMP Plugin SDK v2 — Required exports
// ═══════════════════════════════════════════════════════════════════════════════

extern "C" {

__declspec(dllexport)
const AmpPluginInfo* amp_plugin_info(void) {
    return &kPluginInfo;
}

__declspec(dllexport)
int amp_plugin_init(const AmpHostContext* ctx) {
    if (!ctx) return -1;

    g_log = ctx->log;
    g_data_dir = ctx->plugin_data_dir ? ctx->plugin_data_dir : ".";

    plugin_log(4, "MC1 Encoder: init — host v%s, sr=%d, ch=%d, buf=%d",
               ctx->app_version, ctx->sample_rate, ctx->channels, ctx->buffer_size);

    // Load config from YAML
    load_config();

    // Update sample rate and channels from host
    {
        std::lock_guard<std::mutex> lock(g_config_mutex);
        g_config.sample_rate = ctx->sample_rate;
        g_config.channels = ctx->channels;
    }

    // Create ring buffer
    g_ring = new SpscRingBuffer(kRingCapacity);

    // Start encoder background thread
    g_running.store(true);
    g_encoder_thread = std::thread(encoder_thread_func);

    plugin_log(4, "MC1 Encoder: initialized successfully");
    return 0;
}

__declspec(dllexport)
void amp_plugin_shutdown(void) {
    plugin_log(4, "MC1 Encoder: shutting down");

    // Save config before exit
    save_config();

    // Stop encoder thread
    g_enabled.store(false);
    g_running.store(false);
    if (g_encoder_thread.joinable())
        g_encoder_thread.join();

    // Clean up
    delete g_ring;
    g_ring = nullptr;

    g_widget = nullptr;  // host destroys the widget

    plugin_log(4, "MC1 Encoder: shutdown complete");
}

// ── DSP process (realtime audio thread) ─────────────────────────────────────

__declspec(dllexport)
void amp_plugin_dsp_process(float* samples, int frames, int channels) {
    // Pass audio through unmodified — this is a monitoring tap, not an effect.
    // Copy to ring buffer for the encoder thread.
    if (g_ring && g_enabled.load(std::memory_order_relaxed)) {
        const size_t count = static_cast<size_t>(frames * channels);
        g_ring->write(samples, count);  // drop if full (non-blocking)
    }
}

// ── Parameter API ───────────────────────────────────────────────────────────

static const AmpParamInfo kParams[] = {
    { "enabled",  "Stream Enabled", "",    0.0, 1.0, 0.0, 1, "Off|On" },
    { "bitrate",  "Bitrate",        "kbps", 32.0, 320.0, 128.0, 0, nullptr },
};
static constexpr int kParamCount = 2;

__declspec(dllexport)
int amp_plugin_get_param_count(void) {
    return kParamCount;
}

__declspec(dllexport)
const AmpParamInfo* amp_plugin_get_param_info(int idx) {
    return (idx >= 0 && idx < kParamCount) ? &kParams[idx] : nullptr;
}

__declspec(dllexport)
double amp_plugin_get_param(const char* name) {
    if (!name) return 0.0;
    if (strcmp(name, "enabled") == 0)
        return g_enabled.load() ? 1.0 : 0.0;
    if (strcmp(name, "bitrate") == 0) {
        std::lock_guard<std::mutex> lock(g_config_mutex);
        return static_cast<double>(g_config.bitrate);
    }
    return 0.0;
}

__declspec(dllexport)
void amp_plugin_set_param(const char* name, double value) {
    if (!name) return;
    if (strcmp(name, "enabled") == 0) {
        if (value >= 0.5)
            mc1enc_start_streaming();
        else
            mc1enc_stop_streaming();
    } else if (strcmp(name, "bitrate") == 0) {
        std::lock_guard<std::mutex> lock(g_config_mutex);
        g_config.bitrate = std::clamp(static_cast<int>(value), 32, 320);
    }
}

// ── Custom Qt UI ────────────────────────────────────────────────────────────

__declspec(dllexport)
void* amp_plugin_create_ui(void* parent_qwidget) {
    auto* parent = static_cast<QWidget*>(parent_qwidget);
    g_widget = new mc1enc::EncoderWidget(parent);
    return static_cast<void*>(g_widget);
}

__declspec(dllexport)
void amp_plugin_destroy_ui(void* ui_widget) {
    auto* w = static_cast<mc1enc::EncoderWidget*>(ui_widget);
    if (w) {
        g_widget = nullptr;
        delete w;
    }
}

} // extern "C"
