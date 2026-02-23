// stream_client.cpp — Icecast2 / Shoutcast SOURCE client
// Phase 3 — Mcaster1DSPEncoder Linux v1.2.0
#include "stream_client.h"

#include <cstring>
#include <cstdio>
#include <ctime>
#include <cerrno>
#include <chrono>
#include <algorithm>

// POSIX sockets
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>

// Base64 encoding for HTTP Basic auth
static std::string base64_encode(const std::string& in)
{
    static const char* tab = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    for (size_t i = 0; i < in.size(); i += 3) {
        unsigned b0 = (unsigned char)in[i];
        unsigned b1 = (i + 1 < in.size()) ? (unsigned char)in[i + 1] : 0;
        unsigned b2 = (i + 2 < in.size()) ? (unsigned char)in[i + 2] : 0;
        out += tab[b0 >> 2];
        out += tab[((b0 & 3) << 4) | (b1 >> 4)];
        out += (i + 1 < in.size()) ? tab[((b1 & 0xf) << 2) | (b2 >> 6)] : '=';
        out += (i + 2 < in.size()) ? tab[b2 & 0x3f] : '=';
    }
    return out;
}

// ---------------------------------------------------------------------------
// StreamClient — constructor / destructor
// ---------------------------------------------------------------------------
StreamClient::StreamClient(const StreamTarget& target)
    : target_(target)
{
}

StreamClient::~StreamClient()
{
    disconnect();
}

// ---------------------------------------------------------------------------
// connect — start watchdog thread that manages the connection
// ---------------------------------------------------------------------------
bool StreamClient::connect()
{
    if (state_.load() != State::DISCONNECTED) return false;
    set_state(State::CONNECTING);

    watchdog_stop_.store(false);
    watchdog_thread_ = std::thread(&StreamClient::watchdog_loop, this);
    return true;
}

// ---------------------------------------------------------------------------
// disconnect
// ---------------------------------------------------------------------------
void StreamClient::disconnect()
{
    watchdog_stop_.store(true);
    if (watchdog_thread_.joinable()) watchdog_thread_.join();

    std::lock_guard<std::mutex> lk(sock_mutex_);
    do_disconnect_locked();
    set_state(State::STOPPED);
}

// ---------------------------------------------------------------------------
// write — send encoded audio data to the server
// ---------------------------------------------------------------------------
ssize_t StreamClient::write(const uint8_t* data, size_t len)
{
    std::lock_guard<std::mutex> lk(sock_mutex_);
    if (sock_fd_ < 0 || state_.load() != State::CONNECTED) return -1;

    ssize_t sent = tcp_write(data, len);
    if (sent > 0) {
        bytes_sent_.fetch_add(static_cast<uint64_t>(sent));
        bytes_since_meta_ += static_cast<size_t>(sent);
    }
    return sent;
}

// ---------------------------------------------------------------------------
// send_icy_metadata — inject ICY inline metadata block
// ---------------------------------------------------------------------------
bool StreamClient::send_icy_metadata(const std::string& title,
                                      const std::string& artist,
                                      const std::string& /*url*/)
{
    if (target_.icy_metaint <= 0) return false;

    std::lock_guard<std::mutex> lk(sock_mutex_);
    if (sock_fd_ < 0) return false;

    // Build StreamTitle string
    std::string stream_title;
    if (!artist.empty() && !title.empty())
        stream_title = artist + " - " + title;
    else if (!title.empty())
        stream_title = title;
    else
        stream_title = "Unknown";

    std::string meta_str = "StreamTitle='" + stream_title + "';";

    // Pad to multiple of 16 bytes
    size_t payload_len = meta_str.size();
    size_t block_len   = (payload_len + 15) / 16;  // number of 16-byte chunks
    size_t padded_len  = block_len * 16;

    std::vector<uint8_t> block;
    block.reserve(1 + padded_len);
    block.push_back(static_cast<uint8_t>(block_len));
    for (char c : meta_str) block.push_back(static_cast<uint8_t>(c));
    while (block.size() < 1 + padded_len) block.push_back(0);

    tcp_write(block.data(), block.size());
    bytes_since_meta_ = 0;
    return true;
}

// ---------------------------------------------------------------------------
// last_error
// ---------------------------------------------------------------------------
std::string StreamClient::last_error() const
{
    std::lock_guard<std::mutex> lk(err_mutex_);
    return last_error_;
}

// ---------------------------------------------------------------------------
// watchdog_loop — connects, reconnects on failure
// ---------------------------------------------------------------------------
void StreamClient::watchdog_loop()
{
    while (!watchdog_stop_.load()) {
        if (!do_connect()) {
            ++retry_count_;
            if (target_.max_retries >= 0 && retry_count_ > target_.max_retries) {
                set_error("Max retries exceeded");
                set_state(State::STOPPED);
                return;
            }
            set_state(State::RECONNECTING);
            // Wait before retry
            for (int i = 0; i < target_.retry_interval_sec * 10 && !watchdog_stop_.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            set_state(State::CONNECTING);
        } else {
            retry_count_ = 0;
            set_state(State::CONNECTED);
            // Monitor connection — poll for disconnect by trying zero-len write
            while (!watchdog_stop_.load() && state_.load() == State::CONNECTED) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                // Connection health check: try MSG_PEEK
                {
                    std::lock_guard<std::mutex> lk(sock_mutex_);
                    if (sock_fd_ < 0) break;
                    char buf[1];
                    ssize_t r = ::recv(sock_fd_, buf, 1, MSG_PEEK | MSG_DONTWAIT);
                    if (r == 0) {
                        // Server closed connection
                        do_disconnect_locked();
                        set_error("Server disconnected");
                        break;
                    }
                }
            }
            if (!watchdog_stop_.load() && state_.load() != State::STOPPED) {
                set_state(State::RECONNECTING);
                for (int i = 0; i < target_.retry_interval_sec * 10 && !watchdog_stop_.load(); ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                set_state(State::CONNECTING);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// do_connect — blocking connect attempt
// ---------------------------------------------------------------------------
bool StreamClient::do_connect()
{
    fprintf(stderr, "[StreamClient] Connecting to %s:%d%s\n",
            target_.host.c_str(), target_.port, target_.mount.c_str());

    if (!tcp_connect(target_.host, target_.port)) return false;

    bool ok = false;
    if (target_.protocol == StreamTarget::Protocol::ICECAST2 ||
        target_.protocol == StreamTarget::Protocol::SHOUTCAST_V2) {
        ok = send_icecast2_headers();
    } else {
        ok = send_shoutcast_v1_headers();
    }

    if (ok) {
        connect_time_.store(static_cast<uint64_t>(std::time(nullptr)));
        fprintf(stderr, "[StreamClient] Connected.\n");
    } else {
        std::lock_guard<std::mutex> lk(sock_mutex_);
        do_disconnect_locked();
    }
    return ok;
}

// ---------------------------------------------------------------------------
// do_disconnect_locked — close socket (must hold sock_mutex_)
// ---------------------------------------------------------------------------
void StreamClient::do_disconnect_locked()
{
    if (sock_fd_ >= 0) {
        ::shutdown(sock_fd_, SHUT_RDWR);
        ::close(sock_fd_);
        sock_fd_ = -1;
    }
}

// ---------------------------------------------------------------------------
// send_icecast2_headers — HTTP PUT handshake
// ---------------------------------------------------------------------------
bool StreamClient::send_icecast2_headers()
{
    std::string cred = target_.username + ":" + target_.password;
    std::string b64  = base64_encode(cred);

    char req[4096];
    snprintf(req, sizeof(req),
        "PUT %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Authorization: Basic %s\r\n"
        "User-Agent: Mcaster1DSPEncoder/1.2.0\r\n"
        "Content-Type: %s\r\n"
        "Ice-Public: 1\r\n"
        "Ice-Name: %s\r\n"
        "Ice-Description: %s\r\n"
        "Ice-Genre: %s\r\n"
        "Ice-Url: %s\r\n"
        "Ice-Audio-Info: ice-samplerate=%d;ice-bitrate=%d;ice-channels=%d\r\n"
        "Icy-MetaData: %d\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Expect: 100-continue\r\n"
        "\r\n",
        target_.mount.c_str(),
        target_.host.c_str(), target_.port,
        b64.c_str(),
        target_.content_type.c_str(),
        target_.station_name.c_str(),
        target_.description.c_str(),
        target_.genre.c_str(),
        target_.url.c_str(),
        target_.sample_rate,
        target_.bitrate,
        target_.channels,
        (target_.icy_metaint > 0 ? 1 : 0));

    std::lock_guard<std::mutex> lk(sock_mutex_);
    if (tcp_write(reinterpret_cast<const uint8_t*>(req), strlen(req)) < 0) {
        set_error("Failed to send PUT headers");
        return false;
    }

    // Read 100-continue or 200 OK
    char resp[512] = {};
    ssize_t n = ::recv(sock_fd_, resp, sizeof(resp) - 1, 0);
    if (n <= 0) { set_error("No response from server"); return false; }

    // Accept 100 Continue or 200 OK
    std::string rs(resp, static_cast<size_t>(n));
    if (rs.find("100") == std::string::npos && rs.find("200") == std::string::npos) {
        set_error("Server rejected connection: " + rs.substr(0, 80));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// send_shoutcast_v1_headers — legacy SOURCE handshake
// ---------------------------------------------------------------------------
bool StreamClient::send_shoutcast_v1_headers()
{
    char req[2048];
    snprintf(req, sizeof(req),
        "SOURCE %s ICY/1.0\r\n"
        "icy-password: %s\r\n"
        "icy-name: %s\r\n"
        "icy-genre: %s\r\n"
        "icy-url: %s\r\n"
        "icy-pub: 1\r\n"
        "icy-br: %d\r\n"
        "content-type: %s\r\n"
        "\r\n",
        target_.mount.c_str(),
        target_.password.c_str(),
        target_.station_name.c_str(),
        target_.genre.c_str(),
        target_.url.c_str(),
        target_.bitrate,
        target_.content_type.c_str());

    std::lock_guard<std::mutex> lk(sock_mutex_);
    if (tcp_write(reinterpret_cast<const uint8_t*>(req), strlen(req)) < 0) {
        set_error("Failed to send SOURCE headers");
        return false;
    }

    char resp[256] = {};
    ssize_t n = ::recv(sock_fd_, resp, sizeof(resp) - 1, 0);
    if (n <= 0) { set_error("No response"); return false; }

    std::string rs(resp, static_cast<size_t>(n));
    if (rs.find("OK") == std::string::npos && rs.find("200") == std::string::npos) {
        set_error("Shoutcast rejected: " + rs.substr(0, 60));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// tcp_connect — create and connect a blocking TCP socket
// ---------------------------------------------------------------------------
bool StreamClient::tcp_connect(const std::string& host, uint16_t port)
{
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(port);
    int gai = ::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
    if (gai != 0) {
        set_error(std::string("getaddrinfo: ") + gai_strerror(gai));
        return false;
    }

    int fd = -1;
    for (auto* p = res; p; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(res);

    if (fd < 0) {
        set_error("Could not connect to " + host + ":" + port_str);
        return false;
    }

    std::lock_guard<std::mutex> lk(sock_mutex_);
    do_disconnect_locked();
    sock_fd_ = fd;
    return true;
}

// ---------------------------------------------------------------------------
// tcp_write — blocking write (must hold sock_mutex_ or be in single-thread context)
// ---------------------------------------------------------------------------
ssize_t StreamClient::tcp_write(const uint8_t* data, size_t len)
{
    if (sock_fd_ < 0) return -1;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(sock_fd_, data + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            set_error(std::string("send: ") + strerror(errno));
            do_disconnect_locked();
            return -1;
        }
        sent += static_cast<size_t>(n);
    }
    return static_cast<ssize_t>(sent);
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
void StreamClient::set_error(const std::string& msg)
{
    std::lock_guard<std::mutex> lk(err_mutex_);
    last_error_ = msg;
    fprintf(stderr, "[StreamClient] Error: %s\n", msg.c_str());
}

void StreamClient::set_state(State s)
{
    state_.store(s);
    if (state_cb_) state_cb_(s);
}
