/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * video/rtmp_client.h — RTMP protocol client for YouTube Live / Twitch
 *
 * Implements a minimal RTMP publisher:
 *   1. TCP connect to host:1935
 *   2. C0/C1/C2 handshake
 *   3. AMF0 commands: connect → createStream → publish
 *   4. Set chunk size to 4096
 *   5. @setDataFrame metadata
 *   6. send_video() — RTMP msg type 9, chunk stream 6
 *   7. send_audio() — RTMP msg type 8, chunk stream 4
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_RTMP_CLIENT_H
#define MC1_RTMP_CLIENT_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>

namespace mc1 {

class RtmpClient {
public:
    RtmpClient();
    ~RtmpClient();

    /* Connect to RTMP server and perform handshake + publish.
     * url: "rtmp://host:port/app" (e.g. "rtmp://a.rtmp.youtube.com/live2")
     * stream_key: the stream key (e.g. "xxxx-xxxx-xxxx-xxxx") */
    bool connect(const std::string& url, const std::string& stream_key);

    /* Disconnect from the server */
    void disconnect();

    /* Send @setDataFrame metadata (width, height, fps, bitrate, codecs) */
    bool send_metadata(int width, int height, int fps, int bitrate_kbps,
                       const std::string& video_codec = "avc1",
                       const std::string& audio_codec = "mp4a");

    /* Send video data (FLV video tag body: 1-byte tag header + data)
     * data should be an FLV video tag body (AVC format). */
    bool send_video(const uint8_t* data, size_t len, uint32_t timestamp_ms);

    /* Send audio data (FLV audio tag body) */
    bool send_audio(const uint8_t* data, size_t len, uint32_t timestamp_ms);

    bool is_connected() const { return connected_.load(); }
    std::string last_error() const;

private:
    /* RTMP handshake */
    bool do_handshake();

    /* AMF0 command helpers */
    bool send_connect(const std::string& app, const std::string& tc_url);
    bool send_release_stream(const std::string& stream_key);
    bool send_fc_publish(const std::string& stream_key);
    bool send_create_stream();
    bool send_publish(const std::string& stream_key);
    bool set_chunk_size(uint32_t size);

    /* Wait for and parse an AMF0 response */
    bool read_response(double& txn_id, std::string& cmd_name);

    /* RTMP chunk writing */
    bool send_rtmp_message(uint8_t chunk_stream_id, uint8_t msg_type_id,
                           uint32_t msg_stream_id, uint32_t timestamp,
                           const uint8_t* data, size_t len);

    /* Low-level socket I/O */
    bool tcp_connect(const std::string& host, uint16_t port);
    bool tcp_send(const uint8_t* data, size_t len);
    bool tcp_recv(uint8_t* buf, size_t len);
    void tcp_close();

    /* URL parsing */
    bool parse_rtmp_url(const std::string& url,
                        std::string& host, uint16_t& port, std::string& app);

    int         sock_fd_ = -1;
    std::mutex  sock_mtx_;
    std::atomic<bool> connected_{false};
    std::string last_error_;
    mutable std::mutex err_mtx_;

    std::string host_;
    uint16_t    port_ = 1935;
    std::string app_;
    std::string stream_key_;
    std::string tc_url_;

    uint32_t chunk_size_     = 128;  /* default RTMP chunk size */
    uint32_t msg_stream_id_  = 0;    /* assigned by server in createStream response */
    double   txn_counter_    = 1.0;  /* AMF0 transaction ID counter */

    void set_error(const std::string& msg);
};

} // namespace mc1

#endif // MC1_RTMP_CLIENT_H
