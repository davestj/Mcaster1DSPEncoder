// stream_client.h — Icecast2 / Shoutcast SOURCE client
// Phase M4 — Mcaster1DSPEncoder macOS Qt 6 Build
//
// Ported from src/linux/stream_client.h.
// Manages TCP connection to one broadcast server with watchdog reconnect.
//
// Uses mc1::StreamTarget from config_types.h (no duplicate struct).
#pragma once

#include "config_types.h"

#include <string>
#include <cstdint>
#include <cstddef>
#include <atomic>
#include <mutex>
#include <thread>
#include <functional>

namespace mc1 {

// ---------------------------------------------------------------------------
// StreamClient — manages a TCP connection to one broadcast server
// ---------------------------------------------------------------------------
class StreamClient {
public:
    enum class State {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        RECONNECTING,
        STOPPED
    };

    using StateCallback = std::function<void(State)>;

    explicit StreamClient(const StreamTarget& target);
    ~StreamClient();

    // Connect (non-blocking — starts background watchdog thread)
    bool connect();

    // Disconnect and stop the watchdog thread
    void disconnect();

    // Write encoded audio data.  Returns bytes written, or -1 on error.
    // If the socket has dropped, the watchdog will reconnect automatically.
    ssize_t write(const uint8_t* data, size_t len);

    // Push track metadata to the server via HTTP GET /admin/metadata
    // (Icecast2 / Mcaster1DNAS admin endpoint — separate TCP connection per call).
    // For Shoutcast v1, uses /admin.cgi instead.
    bool send_admin_metadata(const std::string& title,
                             const std::string& artist  = "",
                             const std::string& album   = "",
                             const std::string& artwork = "");

    State       state()         const { return state_.load(); }
    bool        is_connected()  const { return state_.load() == State::CONNECTED; }
    uint64_t    bytes_sent()    const { return bytes_sent_.load(); }
    uint64_t    connect_time()  const { return connect_time_.load(); }  // unix epoch
    std::string last_error()    const;

    void set_state_callback(StateCallback cb) { state_cb_ = cb; }

private:
    StreamTarget    target_;
    std::atomic<State> state_{State::DISCONNECTED};
    int             sock_fd_     = -1;
    mutable std::mutex sock_mutex_;
    mutable std::mutex err_mutex_;
    std::string     last_error_;

    std::atomic<uint64_t> bytes_sent_{0};
    std::atomic<uint64_t> connect_time_{0};
    int             retry_count_  = 0;

    StateCallback   state_cb_;

    // Watchdog thread manages reconnect loop
    std::thread     watchdog_thread_;
    std::atomic<bool> watchdog_stop_{false};

    bool    do_connect();               // blocking connect attempt
    void    do_disconnect_locked();     // must hold sock_mutex_
    bool    send_icecast2_headers();
    bool    send_shoutcast_v1_headers();
    void    watchdog_loop();

    // Low-level socket helpers
    bool    tcp_connect(const std::string& host, uint16_t port);
    ssize_t tcp_write(const uint8_t* data, size_t len);

    // URL-encode a string for HTTP query parameters (RFC 3986 percent-encoding)
    static std::string url_encode(const std::string& in);

    void    set_error(const std::string& msg);
    void    set_state(State s);
};

} // namespace mc1
