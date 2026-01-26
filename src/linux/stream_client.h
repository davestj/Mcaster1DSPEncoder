// stream_client.h — Icecast2 / Shoutcast SOURCE client
// Phase 4 — Mcaster1DSPEncoder Linux v1.3.0
#pragma once

#include <string>
#include <cstdint>
#include <cstddef>
#include <atomic>
#include <mutex>
#include <thread>
#include <functional>

// ---------------------------------------------------------------------------
// StreamTarget — connection parameters for one broadcast server
// ---------------------------------------------------------------------------
struct StreamTarget {
    enum class Protocol {
        ICECAST2,       // HTTP PUT to /mountpoint (Icecast 2.x, Mcaster1DNAS)
        SHOUTCAST_V1,   // Legacy SOURCE /mount HTTP/1.0 password-only
        SHOUTCAST_V2    // Ultravox extension
    };

    Protocol    protocol    = Protocol::ICECAST2;
    std::string host        = "localhost";
    uint16_t    port        = 8000;
    std::string mount       = "/stream";
    std::string username    = "source";
    std::string password;

    // Stream metadata sent in headers at connect time
    std::string station_name;
    std::string description;
    std::string genre;
    std::string url;
    std::string content_type = "audio/mpeg";  // MIME type for codec
    int         bitrate      = 128;
    int         sample_rate  = 44100;
    int         channels     = 2;

    // ICY2 extended station identity — sent as Ice-* headers at connect time
    // Leave empty to omit the header; Mcaster1DNAS / Icecast 2.4+ support these.
    std::string icy2_twitter;    // Twitter handle, e.g. "@Mcaster1Radio"
    std::string icy2_facebook;   // Facebook page URL or name
    std::string icy2_instagram;  // Instagram handle, e.g. "@mcaster1radio"
    std::string icy2_email;      // Station contact e-mail
    std::string icy2_language;   // IETF language tag, e.g. "en"
    std::string icy2_country;    // ISO 3166-1 alpha-2, e.g. "US"
    std::string icy2_city;       // City / region string, e.g. "Miami, FL"
    bool        icy2_is_public = true;  // 1 = list in public directories

    // Reconnect behaviour
    int         retry_interval_sec = 5;
    int         max_retries        = -1;  // -1 = retry forever
};

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
    // Sends: song= (combined), title=, artist=, icy-meta-track-title=, icy-meta-track-artist=,
    // and optionally album= / icy-meta-track-album= and icy-meta-track-artwork=.
    // Returns true if the server acknowledged with <return>1</return>.
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
