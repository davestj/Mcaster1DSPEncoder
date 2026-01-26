// stream_client.cpp — Icecast2 / Shoutcast SOURCE client
// Phase 4 — Mcaster1DSPEncoder Linux v1.3.0
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
    if (sent > 0)
        bytes_sent_.fetch_add(static_cast<uint64_t>(sent));
    return sent;
}

// ---------------------------------------------------------------------------
// url_encode — RFC 3986 percent-encoding for HTTP query parameters
// ---------------------------------------------------------------------------
std::string StreamClient::url_encode(const std::string& in)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(in.size() * 3);
    for (unsigned char c : in) {
        // Unreserved: ALPHA / DIGIT / "-" / "." / "_" / "~"
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '.' || c == '_' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[(c >> 4) & 0xF];
            out += hex[c & 0xF];
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// send_admin_metadata — update stream title via HTTP GET /admin/metadata
//
// Opens a new TCP connection to the DNAS/Icecast server and sends the full
// set of metadata parameters supported by Mcaster1DNAS:
//
//   Icecast2 / Mcaster1DNAS:
//     GET /admin/metadata?pass=<pass>&mode=updinfo&mount=<mount>
//          &song=<artist - title>        ← ICY1/Shoutcast compat (combined)
//          &title=<title>                ← Separate title
//          &artist=<artist>              ← Separate artist
//          &icy-meta-track-title=<title> ← ICY2.2 track title
//          &icy-meta-track-artist=<art>  ← ICY2.2 track artist
//          [&icy-meta-track-album=<alb>] ← ICY2.2 album (if provided)
//          [&icy-meta-track-artwork=<u>] ← ICY2.2 artwork URL (if provided)
//
//   Shoutcast v1:
//     GET /admin.cgi?pass=<pass>&mode=updinfo&song=<combined>
//
// Mcaster1DNAS stores all icy-meta-* fields in the stats system for delivery
// to web players and API consumers. The combined song= is for legacy ICY1
// clients and Shoutcast compatibility.
// ---------------------------------------------------------------------------
bool StreamClient::send_admin_metadata(const std::string& title,
                                        const std::string& artist,
                                        const std::string& album,
                                        const std::string& artwork)
{
    // Build the combined "Artist - Title" song string (ICY1 / Shoutcast compat)
    std::string song;
    if (!artist.empty() && !title.empty())
        song = artist + " - " + title;
    else if (!title.empty())
        song = title;
    else
        song = "Unknown";

    std::string enc_song    = url_encode(song);
    std::string enc_title   = url_encode(title);
    std::string enc_artist  = url_encode(artist);
    std::string enc_album   = url_encode(album);
    std::string enc_artwork = url_encode(artwork);
    std::string enc_mount   = url_encode(target_.mount);
    std::string enc_pass    = url_encode(target_.password);

    // Build Basic auth header
    std::string cred = target_.username + ":" + target_.password;
    std::string b64  = base64_encode(cred);

    std::string req;
    if (target_.protocol == StreamTarget::Protocol::SHOUTCAST_V1) {
        // Shoutcast v1: combined song only
        req = "GET /admin.cgi?pass=" + enc_pass +
              "&mode=updinfo&song=" + enc_song +
              " HTTP/1.0\r\n"
              "User-Agent: Mcaster1DSPEncoder/1.3.0\r\n"
              "\r\n";
    } else {
        // Icecast2 / Mcaster1DNAS: full ICY1 + ICY2.2 parameter set
        std::string query =
              "pass="    + enc_pass   +
              "&mode=updinfo"         +
              "&mount="  + enc_mount  +
              "&song="   + enc_song   +   // ICY1 compat combined string
              "&title="  + enc_title  +   // separate title
              "&artist=" + enc_artist +   // separate artist
              "&icy-meta-track-title="  + enc_title  +  // ICY2.2
              "&icy-meta-track-artist=" + enc_artist;   // ICY2.2

        if (!album.empty())
            query += "&icy-meta-track-album=" + enc_album;
        if (!artwork.empty())
            query += "&icy-meta-track-artwork=" + enc_artwork;

        req = "GET /admin/metadata?" + query + " HTTP/1.0\r\n"
              "Host: " + target_.host + ":" + std::to_string(target_.port) + "\r\n"
              "Authorization: Basic " + b64 + "\r\n"
              "User-Agent: Mcaster1DSPEncoder/1.3.0\r\n"
              "\r\n";
    }

    // Open a fresh TCP connection for the metadata update (separate from audio stream)
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    std::string port_str = std::to_string(target_.port);
    if (::getaddrinfo(target_.host.c_str(), port_str.c_str(), &hints, &res) != 0)
        return false;

    int fd = -1;
    for (auto* p = res; p; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(res);
    if (fd < 0) return false;

    // Send the request
    size_t sent = 0;
    const auto* rdata = reinterpret_cast<const uint8_t*>(req.c_str());
    size_t rlen = req.size();
    while (sent < rlen) {
        ssize_t n = ::send(fd, rdata + sent, rlen - sent, MSG_NOSIGNAL);
        if (n <= 0) { ::close(fd); return false; }
        sent += static_cast<size_t>(n);
    }

    // Read response — check for <return>1</return> (DNAS success indicator)
    char resp[512] = {};
    ssize_t rn = ::recv(fd, resp, sizeof(resp) - 1, 0);
    ::close(fd);

    bool accepted = (rn > 0 && std::string(resp, static_cast<size_t>(rn)).find("<return>1</return>") != std::string::npos);
    fprintf(stderr, "[StreamClient] Metadata update: %s%s\n",
            song.c_str(), accepted ? "" : " (no ACK from server)");
    return accepted;
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
// send_icecast2_headers — HTTP PUT handshake with ICY2 extended headers
// ---------------------------------------------------------------------------
bool StreamClient::send_icecast2_headers()
{
    std::string cred = target_.username + ":" + target_.password;
    std::string b64  = base64_encode(cred);

    // Mandatory headers
    char fixed[2048];
    snprintf(fixed, sizeof(fixed),
        "PUT %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Authorization: Basic %s\r\n"
        "User-Agent: Mcaster1DSPEncoder/1.3.0\r\n"
        "Content-Type: %s\r\n"
        "Ice-Public: %d\r\n"
        "Ice-Name: %s\r\n"
        "Ice-Description: %s\r\n"
        "Ice-Genre: %s\r\n"
        "Ice-Url: %s\r\n"
        "Ice-Audio-Info: ice-samplerate=%d;ice-bitrate=%d;ice-channels=%d\r\n",
        target_.mount.c_str(),
        target_.host.c_str(), target_.port,
        b64.c_str(),
        target_.content_type.c_str(),
        (target_.icy2_is_public ? 1 : 0),
        target_.station_name.c_str(),
        target_.description.c_str(),
        target_.genre.c_str(),
        target_.url.c_str(),
        target_.sample_rate,
        target_.bitrate,
        target_.channels);

    std::string req(fixed);

    // ICY2 extended social / identity headers — emitted only when set
    if (!target_.icy2_twitter.empty())
        req += "Icy-Twitter: "   + target_.icy2_twitter   + "\r\n";
    if (!target_.icy2_facebook.empty())
        req += "Icy-Facebook: "  + target_.icy2_facebook  + "\r\n";
    if (!target_.icy2_instagram.empty())
        req += "Icy-Instagram: " + target_.icy2_instagram + "\r\n";
    if (!target_.icy2_email.empty())
        req += "Icy-Email: "     + target_.icy2_email     + "\r\n";
    if (!target_.icy2_language.empty())
        req += "Icy-Language: "  + target_.icy2_language  + "\r\n";
    if (!target_.icy2_country.empty())
        req += "Icy-Country: "   + target_.icy2_country   + "\r\n";
    if (!target_.icy2_city.empty())
        req += "Icy-City: "      + target_.icy2_city      + "\r\n";

    // Terminate headers
    req += "Transfer-Encoding: chunked\r\n"
           "Expect: 100-continue\r\n"
           "\r\n";

    std::lock_guard<std::mutex> lk(sock_mutex_);
    if (tcp_write(reinterpret_cast<const uint8_t*>(req.c_str()), req.size()) < 0) {
        set_error("Failed to send PUT headers");
        return false;
    }

    // Read 100-continue or 200 OK
    char resp[512] = {};
    ssize_t n = ::recv(sock_fd_, resp, sizeof(resp) - 1, 0);
    if (n <= 0) { set_error("No response from server"); return false; }

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
