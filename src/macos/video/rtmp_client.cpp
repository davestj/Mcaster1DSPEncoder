/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/rtmp_client.cpp — RTMP protocol client for YouTube Live / Twitch
 *
 * RTMP chunk format:
 *   Type 0 header: [1 byte fmt+csid] [3 bytes timestamp] [3 bytes msg length]
 *                  [1 byte msg type] [4 bytes msg stream id (little-endian!)]
 *   Type 3 header: [1 byte fmt+csid] (continuation chunk)
 *
 * Message types:
 *   1  = Set Chunk Size
 *   8  = Audio
 *   9  = Video
 *   20 = AMF0 Command
 *
 * RTMP gotchas:
 *   - Message stream ID is little-endian (unique in RTMP)
 *   - YouTube requires exact tcUrl match
 *   - Twitch requires specific flashVer string
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "rtmp_client.h"
#include "amf0.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <ctime>

namespace mc1 {

// ── Big-endian helpers ──────────────────────────────────────────────────

static void put_be24(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v >> 16);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v);
}

static void put_be32(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

static void put_le32(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

static uint32_t read_be32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

// ── Constructor / Destructor ────────────────────────────────────────────

RtmpClient::RtmpClient() = default;

RtmpClient::~RtmpClient()
{
    disconnect();
}

// ── Public API ──────────────────────────────────────────────────────────

bool RtmpClient::connect(const std::string& url, const std::string& stream_key)
{
    if (connected_.load()) disconnect();

    stream_key_ = stream_key;
    txn_counter_ = 1.0;
    chunk_size_ = 128;
    msg_stream_id_ = 0;

    /* Parse URL: rtmp://host:port/app */
    if (!parse_rtmp_url(url, host_, port_, app_)) {
        set_error("Invalid RTMP URL: " + url);
        return false;
    }
    tc_url_ = url;

    /* TCP connect */
    if (!tcp_connect(host_, port_)) return false;

    /* RTMP handshake */
    if (!do_handshake()) {
        tcp_close();
        return false;
    }

    /* Set chunk size to 4096 early for efficiency */
    if (!set_chunk_size(4096)) {
        tcp_close();
        return false;
    }

    /* AMF0 commands */
    if (!send_connect(app_, tc_url_)) { tcp_close(); return false; }

    /* Read _result for connect */
    double txn_id = 0;
    std::string cmd;
    if (!read_response(txn_id, cmd)) { tcp_close(); return false; }

    /* releaseStream + FCPublish */
    send_release_stream(stream_key_);
    send_fc_publish(stream_key_);

    /* createStream */
    if (!send_create_stream()) { tcp_close(); return false; }

    /* Read createStream _result — get msg_stream_id_ */
    if (!read_response(txn_id, cmd)) { tcp_close(); return false; }

    /* publish */
    if (!send_publish(stream_key_)) { tcp_close(); return false; }

    connected_ = true;
    return true;
}

void RtmpClient::disconnect()
{
    connected_ = false;
    tcp_close();
}

bool RtmpClient::send_metadata(int width, int height, int fps, int bitrate_kbps,
                                 const std::string& video_codec,
                                 const std::string& audio_codec)
{
    if (!connected_.load()) return false;

    Amf0Writer amf;
    amf.write_string("@setDataFrame");
    amf.write_string("onMetaData");

    amf.begin_ecma_array(8);
    amf.write_property("width",         static_cast<double>(width));
    amf.write_property("height",        static_cast<double>(height));
    amf.write_property("framerate",     static_cast<double>(fps));
    amf.write_property("videodatarate", static_cast<double>(bitrate_kbps));
    amf.write_property("videocodecid",  video_codec);
    amf.write_property("audiocodecid",  audio_codec);
    amf.write_property("audiodatarate", 128.0);
    amf.write_property("audiosamplerate", 44100.0);
    amf.end_object();

    /* Send as AMF0 Data message (type 18) on chunk stream 4, msg stream = published */
    return send_rtmp_message(4, 18, msg_stream_id_, 0,
                             amf.data().data(), amf.size());
}

bool RtmpClient::send_video(const uint8_t* data, size_t len, uint32_t timestamp_ms)
{
    if (!connected_.load()) return false;
    /* Video: msg type 9, chunk stream 6 */
    return send_rtmp_message(6, 9, msg_stream_id_, timestamp_ms, data, len);
}

bool RtmpClient::send_audio(const uint8_t* data, size_t len, uint32_t timestamp_ms)
{
    if (!connected_.load()) return false;
    /* Audio: msg type 8, chunk stream 4 */
    return send_rtmp_message(4, 8, msg_stream_id_, timestamp_ms, data, len);
}

std::string RtmpClient::last_error() const
{
    std::lock_guard<std::mutex> lk(err_mtx_);
    return last_error_;
}

// ── RTMP Handshake ──────────────────────────────────────────────────────

bool RtmpClient::do_handshake()
{
    /* C0: version byte (3) */
    uint8_t c0 = 0x03;
    if (!tcp_send(&c0, 1)) { set_error("Handshake: C0 send failed"); return false; }

    /* C1: 1536 bytes (4 bytes time + 4 bytes zero + 1528 random) */
    uint8_t c1[1536];
    std::memset(c1, 0, 1536);
    uint32_t now = static_cast<uint32_t>(std::time(nullptr));
    put_be32(c1, now);
    /* Fill random bytes */
    std::srand(now);
    for (int i = 8; i < 1536; ++i)
        c1[i] = static_cast<uint8_t>(std::rand() & 0xFF);
    if (!tcp_send(c1, 1536)) { set_error("Handshake: C1 send failed"); return false; }

    /* Read S0 + S1 (1 + 1536 = 1537 bytes) */
    uint8_t s0;
    if (!tcp_recv(&s0, 1)) { set_error("Handshake: S0 recv failed"); return false; }

    uint8_t s1[1536];
    if (!tcp_recv(s1, 1536)) { set_error("Handshake: S1 recv failed"); return false; }

    /* C2: echo S1 */
    if (!tcp_send(s1, 1536)) { set_error("Handshake: C2 send failed"); return false; }

    /* Read S2 (echo of C1) */
    uint8_t s2[1536];
    if (!tcp_recv(s2, 1536)) { set_error("Handshake: S2 recv failed"); return false; }

    return true;
}

// ── AMF0 Commands ───────────────────────────────────────────────────────

bool RtmpClient::send_connect(const std::string& app, const std::string& tc_url)
{
    Amf0Writer amf;
    amf.write_string("connect");
    amf.write_number(txn_counter_++);

    amf.begin_object();
    amf.write_property("app",       app);
    amf.write_property("type",      std::string("nonprivate"));
    amf.write_property("flashVer",  std::string("FMLE/3.0 (compatible; Mcaster1)"));
    amf.write_property("tcUrl",     tc_url);
    amf.end_object();

    /* Connect uses chunk stream 3, msg stream 0 */
    return send_rtmp_message(3, 20, 0, 0, amf.data().data(), amf.size());
}

bool RtmpClient::send_release_stream(const std::string& stream_key)
{
    Amf0Writer amf;
    amf.write_string("releaseStream");
    amf.write_number(txn_counter_++);
    amf.write_null();
    amf.write_string(stream_key);
    return send_rtmp_message(3, 20, 0, 0, amf.data().data(), amf.size());
}

bool RtmpClient::send_fc_publish(const std::string& stream_key)
{
    Amf0Writer amf;
    amf.write_string("FCPublish");
    amf.write_number(txn_counter_++);
    amf.write_null();
    amf.write_string(stream_key);
    return send_rtmp_message(3, 20, 0, 0, amf.data().data(), amf.size());
}

bool RtmpClient::send_create_stream()
{
    Amf0Writer amf;
    amf.write_string("createStream");
    amf.write_number(txn_counter_++);
    amf.write_null();
    return send_rtmp_message(3, 20, 0, 0, amf.data().data(), amf.size());
}

bool RtmpClient::send_publish(const std::string& stream_key)
{
    Amf0Writer amf;
    amf.write_string("publish");
    amf.write_number(0.0); /* non-transactional */
    amf.write_null();
    amf.write_string(stream_key);
    amf.write_string("live");
    return send_rtmp_message(3, 20, msg_stream_id_, 0, amf.data().data(), amf.size());
}

bool RtmpClient::set_chunk_size(uint32_t size)
{
    uint8_t payload[4];
    put_be32(payload, size);
    /* Set Chunk Size: msg type 1, chunk stream 2, msg stream 0 */
    if (!send_rtmp_message(2, 1, 0, 0, payload, 4)) return false;
    chunk_size_ = size;
    return true;
}

// ── Response reader ─────────────────────────────────────────────────────

bool RtmpClient::read_response(double& txn_id, std::string& cmd_name)
{
    /* Read RTMP chunks until we get a complete AMF0 command response.
     * This is a simplified reader that handles the common case. */

    /* Read up to 8KB of response data, looking for AMF0 command */
    uint8_t buf[8192];
    size_t total = 0;

    /* We may get multiple chunks (window ack, set chunk size, etc.)
     * before the actual _result response. Read with a timeout. */
    for (int attempts = 0; attempts < 20; ++attempts) {
        /* Read chunk header (basic header: 1 byte minimum) */
        uint8_t basic_hdr;
        if (!tcp_recv(&basic_hdr, 1)) return false;

        uint8_t fmt = (basic_hdr >> 6) & 0x03;
        uint8_t csid = basic_hdr & 0x3F;

        /* Handle extended chunk stream IDs */
        if (csid == 0) {
            uint8_t b;
            if (!tcp_recv(&b, 1)) return false;
            csid = b + 64;
        } else if (csid == 1) {
            uint8_t b[2];
            if (!tcp_recv(b, 2)) return false;
            csid = static_cast<uint8_t>(b[1] * 256 + b[0] + 64);
        }

        uint32_t timestamp = 0, msg_len = 0;
        uint8_t  msg_type = 0;
        uint32_t msg_sid [[maybe_unused]] = 0;

        if (fmt == 0) {
            /* Type 0: full header (11 bytes after basic) */
            uint8_t hdr[11];
            if (!tcp_recv(hdr, 11)) return false;
            timestamp = (static_cast<uint32_t>(hdr[0]) << 16) |
                        (static_cast<uint32_t>(hdr[1]) << 8) |
                        static_cast<uint32_t>(hdr[2]);
            msg_len = (static_cast<uint32_t>(hdr[3]) << 16) |
                      (static_cast<uint32_t>(hdr[4]) << 8) |
                      static_cast<uint32_t>(hdr[5]);
            msg_type = hdr[6];
            /* Message stream ID is little-endian! */
            msg_sid = static_cast<uint32_t>(hdr[7]) |
                      (static_cast<uint32_t>(hdr[8]) << 8) |
                      (static_cast<uint32_t>(hdr[9]) << 16) |
                      (static_cast<uint32_t>(hdr[10]) << 24);
        } else if (fmt == 1) {
            /* Type 1: 7 bytes */
            uint8_t hdr[7];
            if (!tcp_recv(hdr, 7)) return false;
            timestamp = (static_cast<uint32_t>(hdr[0]) << 16) |
                        (static_cast<uint32_t>(hdr[1]) << 8) |
                        static_cast<uint32_t>(hdr[2]);
            msg_len = (static_cast<uint32_t>(hdr[3]) << 16) |
                      (static_cast<uint32_t>(hdr[4]) << 8) |
                      static_cast<uint32_t>(hdr[5]);
            msg_type = hdr[6];
        } else if (fmt == 2) {
            /* Type 2: 3 bytes (timestamp delta only) */
            uint8_t hdr[3];
            if (!tcp_recv(hdr, 3)) return false;
        }
        /* fmt == 3: no header beyond basic */

        /* Extended timestamp */
        if (timestamp == 0xFFFFFF) {
            uint8_t ext[4];
            if (!tcp_recv(ext, 4)) return false;
            timestamp = read_be32(ext);
        }

        /* Read message payload */
        if (msg_len > 0 && msg_len < sizeof(buf)) {
            /* Read in chunks */
            size_t remaining = msg_len;
            size_t offset = 0;
            while (remaining > 0) {
                size_t to_read = std::min(remaining, static_cast<size_t>(chunk_size_));
                if (!tcp_recv(buf + offset, to_read)) return false;
                offset += to_read;
                remaining -= to_read;

                /* If more chunks remain, read type-3 continuation header */
                if (remaining > 0) {
                    uint8_t cont;
                    if (!tcp_recv(&cont, 1)) return false;
                    /* Should be type 3 header for same chunk stream */
                }
            }
            total = offset;
        } else if (msg_len == 0) {
            continue; /* Control message with no payload */
        } else {
            /* Message too large — skip */
            std::vector<uint8_t> skip_buf(msg_len);
            tcp_recv(skip_buf.data(), msg_len);
            continue;
        }

        /* Handle protocol control messages */
        if (msg_type == 1 && total >= 4) {
            /* Set Chunk Size from server */
            chunk_size_ = read_be32(buf);
            continue;
        }
        if (msg_type == 5 || msg_type == 6) {
            /* Window Ack Size / Set Peer Bandwidth — acknowledge and continue */
            continue;
        }

        /* AMF0 command (type 20) */
        if (msg_type == 20 && total > 3) {
            /* Parse command name */
            if (buf[0] == 0x02) { /* AMF0 string marker */
                uint16_t slen = (static_cast<uint16_t>(buf[1]) << 8) | buf[2];
                if (slen + 3 <= total) {
                    cmd_name.assign(reinterpret_cast<char*>(buf + 3), slen);
                }

                /* Parse transaction ID (should be next: 0x00 + 8 bytes double) */
                size_t pos = 3 + slen;
                if (pos + 9 <= total && buf[pos] == 0x00) {
                    uint64_t bits = 0;
                    for (int i = 0; i < 8; ++i)
                        bits = (bits << 8) | buf[pos + 1 + i];
                    std::memcpy(&txn_id, &bits, 8);
                    pos += 9;
                }

                /* For createStream _result, parse the stream ID from 4th AMF field */
                if (cmd_name == "_result" && pos + 9 <= total) {
                    /* Skip the properties object, find the number */
                    /* Look for a number marker (0x00) after the object */
                    for (size_t i = pos; i + 9 <= total; ++i) {
                        if (buf[i] == 0x00) {
                            uint64_t bits2 = 0;
                            for (int j = 0; j < 8; ++j)
                                bits2 = (bits2 << 8) | buf[i + 1 + j];
                            double val;
                            std::memcpy(&val, &bits2, 8);
                            if (val > 0 && val < 100) {
                                msg_stream_id_ = static_cast<uint32_t>(val);
                            }
                            break;
                        }
                    }
                }

                return true;
            }
        }

        /* onStatus or other messages — check for error */
        if (msg_type == 20 || msg_type == 18) {
            /* Non-command AMF message — skip */
            continue;
        }
    }

    set_error("Timed out waiting for RTMP response");
    return false;
}

// ── RTMP Chunk Writer ───────────────────────────────────────────────────

bool RtmpClient::send_rtmp_message(uint8_t chunk_stream_id, uint8_t msg_type_id,
                                    uint32_t msg_stream_id, uint32_t timestamp,
                                    const uint8_t* data, size_t len)
{
    std::lock_guard<std::mutex> lk(sock_mtx_);

    size_t offset = 0;
    bool first = true;

    while (offset < len) {
        size_t chunk_payload = std::min(static_cast<size_t>(chunk_size_), len - offset);

        if (first) {
            /* Type 0 header: 12 bytes */
            uint8_t hdr[12];
            hdr[0] = (0x00 << 6) | (chunk_stream_id & 0x3F); /* fmt=0 + csid */

            /* Timestamp (3 bytes, or 0xFFFFFF for extended) */
            if (timestamp < 0xFFFFFF) {
                put_be24(hdr + 1, timestamp);
            } else {
                put_be24(hdr + 1, 0xFFFFFF);
            }

            /* Message length (3 bytes) */
            put_be24(hdr + 4, static_cast<uint32_t>(len));

            /* Message type ID */
            hdr[7] = msg_type_id;

            /* Message stream ID (4 bytes, LITTLE ENDIAN!) */
            put_le32(hdr + 8, msg_stream_id);

            if (!tcp_send(hdr, 12)) return false;

            /* Extended timestamp if needed */
            if (timestamp >= 0xFFFFFF) {
                uint8_t ext[4];
                put_be32(ext, timestamp);
                if (!tcp_send(ext, 4)) return false;
            }
            first = false;
        } else {
            /* Type 3 header: 1 byte (continuation) */
            uint8_t hdr = (0x03 << 6) | (chunk_stream_id & 0x3F);
            if (!tcp_send(&hdr, 1)) return false;
        }

        if (!tcp_send(data + offset, chunk_payload)) return false;
        offset += chunk_payload;
    }

    return true;
}

// ── TCP Socket I/O ──────────────────────────────────────────────────────

bool RtmpClient::tcp_connect(const std::string& host, uint16_t port)
{
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(port);
    int rc = ::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
    if (rc != 0 || !res) {
        set_error("DNS resolution failed for " + host + ": " + gai_strerror(rc));
        return false;
    }

    sock_fd_ = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock_fd_ < 0) {
        set_error("Socket creation failed");
        ::freeaddrinfo(res);
        return false;
    }

    /* SO_NOSIGPIPE (macOS doesn't support MSG_NOSIGNAL) */
    int opt = 1;
    ::setsockopt(sock_fd_, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));

    /* TCP_NODELAY for low latency */
    ::setsockopt(sock_fd_, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    if (::connect(sock_fd_, res->ai_addr, res->ai_addrlen) < 0) {
        set_error("TCP connect to " + host + ":" + std::to_string(port) + " failed");
        ::close(sock_fd_);
        sock_fd_ = -1;
        ::freeaddrinfo(res);
        return false;
    }

    ::freeaddrinfo(res);
    return true;
}

bool RtmpClient::tcp_send(const uint8_t* data, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(sock_fd_, data + sent, len - sent, 0);
        if (n <= 0) {
            set_error("RTMP send failed");
            connected_ = false;
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool RtmpClient::tcp_recv(uint8_t* buf, size_t len)
{
    size_t received = 0;
    while (received < len) {
        ssize_t n = ::recv(sock_fd_, buf + received, len - received, 0);
        if (n <= 0) {
            set_error("RTMP recv failed");
            connected_ = false;
            return false;
        }
        received += static_cast<size_t>(n);
    }
    return true;
}

void RtmpClient::tcp_close()
{
    std::lock_guard<std::mutex> lk(sock_mtx_);
    if (sock_fd_ >= 0) {
        ::close(sock_fd_);
        sock_fd_ = -1;
    }
}

// ── URL Parsing ─────────────────────────────────────────────────────────

bool RtmpClient::parse_rtmp_url(const std::string& url,
                                 std::string& host, uint16_t& port,
                                 std::string& app)
{
    /* Expected: rtmp://host[:port]/app[/...] */
    const std::string prefix = "rtmp://";
    if (url.substr(0, prefix.size()) != prefix) return false;

    std::string rest = url.substr(prefix.size());

    /* Split host:port from path */
    auto slash = rest.find('/');
    std::string host_port = (slash != std::string::npos) ? rest.substr(0, slash) : rest;
    std::string path = (slash != std::string::npos) ? rest.substr(slash + 1) : "";

    /* Parse host and optional port */
    auto colon = host_port.find(':');
    if (colon != std::string::npos) {
        host = host_port.substr(0, colon);
        port = static_cast<uint16_t>(std::atoi(host_port.substr(colon + 1).c_str()));
    } else {
        host = host_port;
        port = 1935;
    }

    app = path;
    return !host.empty();
}

void RtmpClient::set_error(const std::string& msg)
{
    std::lock_guard<std::mutex> lk(err_mtx_);
    last_error_ = msg;
}

} // namespace mc1
