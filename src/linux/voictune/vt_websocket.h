/*
 * Mcaster1 VoicTune — WebSocket Server
 * voictune/vt_websocket.h
 *
 * RFC 6455 WebSocket server for browser mic audio streaming.
 * Receives PCM float32 binary frames from browser AudioWorklet.
 * Pushes to analysis pipeline via callback.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>
#include <thread>
#include <map>

namespace mc1vt {

struct WsClient {
    int         fd          = -1;
    std::string remote_addr;
    bool        handshake_done = false;
    int         sample_rate = 48000;
    int         channels    = 1;
};

/* Callback when browser audio arrives */
struct WsAudioFrame {
    int                client_fd;
    std::vector<float> samples;
    int                sample_rate;
    int                channels;
};

using WsAudioCallback  = std::function<void(const WsAudioFrame&)>;
using WsEventCallback  = std::function<void(int fd, const std::string& event, const std::string& data)>;

class VtWebSocket {
public:
    VtWebSocket();
    ~VtWebSocket();

    /* Start listening on port. Returns false on bind error. */
    bool start(int port, int max_clients, WsAudioCallback audio_cb,
               WsEventCallback event_cb = nullptr);
    void stop();

    bool is_running() const { return running_.load(); }
    int  client_count() const;

    /* Push event to all connected clients (JSON text frame) */
    void broadcast(const std::string& json_msg);

    /* Push event to a specific client */
    void send(int client_fd, const std::string& json_msg);

private:
    std::atomic<bool>  running_{false};
    std::thread        accept_thread_;
    int                listen_fd_    = -1;
    int                max_clients_  = 4;

    mutable std::mutex clients_mtx_;
    std::map<int, WsClient> clients_;

    WsAudioCallback    audio_cb_;
    WsEventCallback    event_cb_;

    void accept_loop();
    void handle_client(int fd, const std::string& addr);
    bool do_handshake(int fd, const std::string& request);
    void process_frame(int fd, const uint8_t* data, size_t len, uint8_t opcode);
    void remove_client(int fd);

    /* RFC 6455 frame helpers */
    static std::string compute_accept_key(const std::string& client_key);
    static bool        send_frame(int fd, uint8_t opcode, const void* data, size_t len);
    static bool        send_text(int fd, const std::string& msg);
    static bool        send_binary(int fd, const void* data, size_t len);
    static void        send_close(int fd, uint16_t code);
};

} // namespace mc1vt
