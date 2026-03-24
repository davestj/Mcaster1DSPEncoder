/*
 * Mcaster1 VoicTune — WebSocket Server
 * voictune/vt_websocket.cpp
 *
 * Minimal RFC 6455 WebSocket server. Single-threaded accept loop
 * with per-client threads for I/O. Binary frames carry PCM float32
 * audio from browser AudioWorklet. Text frames carry JSON control msgs.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "vt_websocket.h"
#include "vt_logger.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

namespace mc1vt {

static const char* WS_MAGIC = "258EAFA5-E914-47DA-95CA-5AB9438B11F5";

VtWebSocket::VtWebSocket() = default;

VtWebSocket::~VtWebSocket() {
    stop();
}

bool VtWebSocket::start(int port, int max_clients,
                         WsAudioCallback audio_cb, WsEventCallback event_cb) {
    if (running_.load()) return true;

    max_clients_ = max_clients;
    audio_cb_    = std::move(audio_cb);
    event_cb_    = std::move(event_cb);

    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        VT_ERR("WebSocket socket() failed: " + std::string(strerror(errno)));
        return false;
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        VT_ERR("WebSocket bind() port " + std::to_string(port) + " failed: "
               + std::string(strerror(errno)));
        close(listen_fd_); listen_fd_ = -1;
        return false;
    }

    if (listen(listen_fd_, 8) < 0) {
        VT_ERR("WebSocket listen() failed: " + std::string(strerror(errno)));
        close(listen_fd_); listen_fd_ = -1;
        return false;
    }

    running_.store(true);
    accept_thread_ = std::thread(&VtWebSocket::accept_loop, this);
    VT_INFO("WebSocket server started on port " + std::to_string(port));
    return true;
}

void VtWebSocket::stop() {
    if (!running_.load()) return;
    running_.store(false);

    if (listen_fd_ >= 0) { shutdown(listen_fd_, SHUT_RDWR); close(listen_fd_); listen_fd_ = -1; }

    {
        std::lock_guard<std::mutex> lk(clients_mtx_);
        for (auto& [fd, _] : clients_) {
            shutdown(fd, SHUT_RDWR);
            close(fd);
        }
        clients_.clear();
    }

    if (accept_thread_.joinable()) accept_thread_.join();
    VT_INFO("WebSocket server stopped");
}

int VtWebSocket::client_count() const {
    std::lock_guard<std::mutex> lk(clients_mtx_);
    return static_cast<int>(clients_.size());
}

void VtWebSocket::broadcast(const std::string& json_msg) {
    std::lock_guard<std::mutex> lk(clients_mtx_);
    for (auto& [fd, c] : clients_) {
        if (c.handshake_done) send_text(fd, json_msg);
    }
}

void VtWebSocket::send(int client_fd, const std::string& json_msg) {
    std::lock_guard<std::mutex> lk(clients_mtx_);
    auto it = clients_.find(client_fd);
    if (it != clients_.end() && it->second.handshake_done)
        send_text(client_fd, json_msg);
}

void VtWebSocket::accept_loop() {
    while (running_.load()) {
        struct pollfd pfd{};
        pfd.fd = listen_fd_;
        pfd.events = POLLIN;
        int ret = poll(&pfd, 1, 500);
        if (ret <= 0) continue;

        struct sockaddr_in cli_addr{};
        socklen_t cli_len = sizeof(cli_addr);
        int fd = accept(listen_fd_, (struct sockaddr*)&cli_addr, &cli_len);
        if (fd < 0) continue;

        std::string addr = inet_ntoa(cli_addr.sin_addr);

        {
            std::lock_guard<std::mutex> lk(clients_mtx_);
            if ((int)clients_.size() >= max_clients_) {
                VT_WARN("WebSocket max clients reached, rejecting " + addr);
                close(fd);
                continue;
            }
        }

        /* Per-client thread */
        std::thread([this, fd, addr]() {
            handle_client(fd, addr);
        }).detach();
    }
}

void VtWebSocket::handle_client(int fd, const std::string& addr) {
    VT_INFO("WebSocket client connected: " + addr);

    {
        std::lock_guard<std::mutex> lk(clients_mtx_);
        WsClient c;
        c.fd = fd;
        c.remote_addr = addr;
        clients_[fd] = c;
    }

    if (event_cb_) event_cb_(fd, "connect", addr);

    /* Read HTTP upgrade request */
    char buf[4096];
    int n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) { remove_client(fd); return; }
    buf[n] = '\0';

    if (!do_handshake(fd, std::string(buf, n))) {
        VT_WARN("WebSocket handshake failed for " + addr);
        remove_client(fd);
        return;
    }

    {
        std::lock_guard<std::mutex> lk(clients_mtx_);
        if (clients_.count(fd)) clients_[fd].handshake_done = true;
    }

    /* Read frames */
    while (running_.load()) {
        struct pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;
        int ret = poll(&pfd, 1, 1000);
        if (ret <= 0) continue;
        if (pfd.revents & (POLLERR | POLLHUP)) break;

        uint8_t header[2];
        int r = recv(fd, header, 2, MSG_WAITALL);
        if (r != 2) break;

        bool     fin    = (header[0] & 0x80) != 0;
        uint8_t  opcode = header[0] & 0x0F;
        bool     masked = (header[1] & 0x80) != 0;
        uint64_t len    = header[1] & 0x7F;

        if (len == 126) {
            uint8_t ext[2];
            if (recv(fd, ext, 2, MSG_WAITALL) != 2) break;
            len = (uint64_t)ext[0] << 8 | ext[1];
        } else if (len == 127) {
            uint8_t ext[8];
            if (recv(fd, ext, 8, MSG_WAITALL) != 8) break;
            len = 0;
            for (int i = 0; i < 8; ++i) len = (len << 8) | ext[i];
        }

        uint8_t mask_key[4] = {};
        if (masked) {
            if (recv(fd, mask_key, 4, MSG_WAITALL) != 4) break;
        }

        if (len > 16 * 1024 * 1024) { /* 16MB limit */
            VT_WARN("WebSocket frame too large: " + std::to_string(len));
            break;
        }

        std::vector<uint8_t> payload(len);
        if (len > 0) {
            size_t got = 0;
            while (got < len) {
                int chunk = recv(fd, payload.data() + got, len - got, 0);
                if (chunk <= 0) break;
                got += chunk;
            }
            if (got < len) break;

            if (masked) {
                for (size_t i = 0; i < len; ++i)
                    payload[i] ^= mask_key[i & 3];
            }
        }

        if (opcode == 0x08) { /* Close */
            send_close(fd, 1000);
            break;
        }
        if (opcode == 0x09) { /* Ping → Pong */
            send_frame(fd, 0x0A, payload.data(), payload.size());
            continue;
        }

        if (fin) {
            process_frame(fd, payload.data(), payload.size(), opcode);
        }
    }

    remove_client(fd);
}

bool VtWebSocket::do_handshake(int fd, const std::string& request) {
    /* Extract Sec-WebSocket-Key */
    std::string key;
    std::istringstream iss(request);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.size() > 1 && line.back() == '\r') line.pop_back();
        if (line.substr(0, 19) == "Sec-WebSocket-Key: ") {
            key = line.substr(19);
            break;
        }
    }

    if (key.empty()) return false;

    std::string accept = compute_accept_key(key);

    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept + "\r\n"
        "\r\n";

    return ::send(fd, response.c_str(), response.size(), MSG_NOSIGNAL) > 0;
}

void VtWebSocket::process_frame(int fd, const uint8_t* data, size_t len, uint8_t opcode) {
    if (opcode == 0x02 && audio_cb_) {
        /* Binary frame: PCM float32 audio */
        size_t num_samples = len / sizeof(float);
        if (num_samples == 0) return;

        WsAudioFrame frame;
        frame.client_fd   = fd;
        frame.samples.resize(num_samples);
        memcpy(frame.samples.data(), data, num_samples * sizeof(float));

        {
            std::lock_guard<std::mutex> lk(clients_mtx_);
            auto it = clients_.find(fd);
            if (it != clients_.end()) {
                frame.sample_rate = it->second.sample_rate;
                frame.channels    = it->second.channels;
            } else {
                frame.sample_rate = 48000;
                frame.channels    = 1;
            }
        }

        audio_cb_(frame);
    } else if (opcode == 0x01) {
        /* Text frame: JSON control message */
        std::string msg(reinterpret_cast<const char*>(data), len);
        if (event_cb_) event_cb_(fd, "message", msg);

        /* Parse sample_rate/channels config if present */
        /* Simple JSON field extraction (no full parser needed for config msgs) */
        auto extract_int = [&](const std::string& key) -> int {
            auto pos = msg.find("\"" + key + "\"");
            if (pos == std::string::npos) return -1;
            pos = msg.find(':', pos);
            if (pos == std::string::npos) return -1;
            return atoi(msg.c_str() + pos + 1);
        };

        int sr = extract_int("sample_rate");
        int ch = extract_int("channels");
        if (sr > 0 || ch > 0) {
            std::lock_guard<std::mutex> lk(clients_mtx_);
            auto it = clients_.find(fd);
            if (it != clients_.end()) {
                if (sr > 0) it->second.sample_rate = sr;
                if (ch > 0) it->second.channels    = ch;
            }
        }
    }
}

void VtWebSocket::remove_client(int fd) {
    {
        std::lock_guard<std::mutex> lk(clients_mtx_);
        clients_.erase(fd);
    }
    shutdown(fd, SHUT_RDWR);
    close(fd);
    if (event_cb_) event_cb_(fd, "disconnect", "");
    VT_INFO("WebSocket client disconnected (fd=" + std::to_string(fd) + ")");
}

/* ── RFC 6455 Helpers ──────────────────────────────────────────────────── */

std::string VtWebSocket::compute_accept_key(const std::string& client_key) {
    std::string concat = client_key + WS_MAGIC;

    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(concat.c_str()), concat.size(), hash);

    /* Base64 encode */
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, mem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, hash, SHA_DIGEST_LENGTH);
    BIO_flush(b64);

    BUF_MEM* bptr;
    BIO_get_mem_ptr(b64, &bptr);
    std::string result(bptr->data, bptr->length);
    BIO_free_all(b64);
    return result;
}

bool VtWebSocket::send_frame(int fd, uint8_t opcode, const void* data, size_t len) {
    std::vector<uint8_t> frame;
    frame.push_back(0x80 | opcode); /* FIN + opcode */

    if (len < 126) {
        frame.push_back(static_cast<uint8_t>(len));
    } else if (len < 65536) {
        frame.push_back(126);
        frame.push_back((len >> 8) & 0xFF);
        frame.push_back(len & 0xFF);
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i)
            frame.push_back((len >> (8 * i)) & 0xFF);
    }

    if (data && len > 0) {
        frame.insert(frame.end(),
                     static_cast<const uint8_t*>(data),
                     static_cast<const uint8_t*>(data) + len);
    }

    ssize_t sent = ::send(fd, frame.data(), frame.size(), MSG_NOSIGNAL);
    return sent == static_cast<ssize_t>(frame.size());
}

bool VtWebSocket::send_text(int fd, const std::string& msg) {
    return send_frame(fd, 0x01, msg.data(), msg.size());
}

bool VtWebSocket::send_binary(int fd, const void* data, size_t len) {
    return send_frame(fd, 0x02, data, len);
}

void VtWebSocket::send_close(int fd, uint16_t code) {
    uint8_t payload[2] = { static_cast<uint8_t>(code >> 8), static_cast<uint8_t>(code & 0xFF) };
    send_frame(fd, 0x08, payload, 2);
}

} // namespace mc1vt
