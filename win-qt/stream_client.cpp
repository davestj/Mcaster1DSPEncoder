// stream_client.cpp — Icecast2 / Shoutcast SOURCE client
// Phase M4 — Mcaster1DSPEncoder macOS Qt 6 Build
//
// Ported from src/linux/stream_client.cpp.
// macOS adaptation: MSG_NOSIGNAL → SO_NOSIGPIPE socket option.
#include "stream_client.h"
#include "event_log.h"

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <cerrno>
#include <chrono>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#define close(fd) closesocket(fd)
#define SHUT_RDWR SD_BOTH
// On Windows with Unicode charset, gai_strerror maps to gai_strerrorW (wchar_t*).
// Force the ANSI variant so it works with std::string concatenation.
#ifdef gai_strerror
#  undef gai_strerror
#endif
#define gai_strerror gai_strerrorA
#else
// POSIX sockets
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#endif

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

// Helper: set SO_NOSIGPIPE on a socket fd (macOS equivalent of MSG_NOSIGNAL)
// Windows has no SIGPIPE concept — this is a no-op on Windows.
static void set_nosigpipe([[maybe_unused]] int fd)
{
#ifndef _WIN32
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
}

namespace mc1 {

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

    log_connect("[StreamClient]",
        "Watchdog starting — target: " + target_.host + ":" + std::to_string(target_.port) +
        target_.mount + "  max_retries=" + std::to_string(target_.max_retries) +
        "  retry_interval=" + std::to_string(target_.retry_interval_sec) + "s");

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
    // Use DISCONNECTED (not STOPPED) — STOPPED is reserved for watchdog giving up
    // after exhausting retries, which signals the encoder slot to enter SLEEP mode.
    // A deliberate disconnect() call must NOT trigger SLEEP.
    set_state(State::DISCONNECTED);
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
              "User-Agent: Mcaster1DSPEncoder/0.4.0\r\n"
              "\r\n";
    } else {
        // Icecast2 / Mcaster1DNAS / Live365: full ICY1 + ICY2.2 parameter set
        std::string query =
              "pass="    + enc_pass   +
              "&mode=updinfo"         +
              "&mount="  + enc_mount  +
              "&song="   + enc_song   +
              "&title="  + enc_title  +
              "&artist=" + enc_artist +
              "&icy-meta-track-title="  + enc_title  +
              "&icy-meta-track-artist=" + enc_artist;

        if (!album.empty())
            query += "&icy-meta-track-album=" + enc_album;
        if (!artwork.empty())
            query += "&icy-meta-track-artwork=" + enc_artwork;

        req = "GET /admin/metadata?" + query + " HTTP/1.0\r\n"
              "Host: " + target_.host + ":" + std::to_string(target_.port) + "\r\n"
              "Authorization: Basic " + b64 + "\r\n"
              "User-Agent: Mcaster1DSPEncoder/0.4.0\r\n"
              "\r\n";
    }

    // SEC-010: Warn when sending credentials over plain HTTP (raw sockets — no TLS support)
    // This client uses raw TCP sockets without TLS. Credentials (Basic auth, password params)
    // are transmitted in cleartext. A future enhancement should add OpenSSL/Schannel support.
    fprintf(stderr, "[StreamClient] WARNING: Metadata push uses plain HTTP — "
            "credentials sent in cleartext to %s:%d\n",
            target_.host.c_str(), target_.port);

    // Open a fresh TCP connection for the metadata update
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
        set_nosigpipe(fd);
        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(res);
    if (fd < 0) return false;

    // Send the request
    size_t sent = 0;
    size_t rlen = req.size();
    while (sent < rlen) {
        int n = ::send(fd, req.c_str() + sent, static_cast<int>(rlen - sent), 0);
        if (n <= 0) { ::close(fd); return false; }
        sent += static_cast<size_t>(n);
    }

    // Read response — check for <return>1</return> (DNAS success indicator)
    char resp[512] = {};
    int rn = ::recv(fd, resp, static_cast<int>(sizeof(resp) - 1), 0);
    ::close(fd);

    bool accepted = (rn > 0 && std::string(resp, static_cast<size_t>(rn)).find("<return>1</return>") != std::string::npos);
    log_icy("[StreamClient]",
        "Metadata push: \"" + song + "\" → " + target_.host + " — " +
        (accepted ? "ACK OK" : "no ACK / rejected"));
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
    log_connect("[StreamClient]", "Watchdog thread started");

    while (!watchdog_stop_.load()) {
        if (!do_connect()) {
            ++retry_count_;
            std::string err_msg = last_error();
            if (target_.max_retries >= 0 && retry_count_ > target_.max_retries) {
                log_error("[StreamClient]",
                    "Max retries (" + std::to_string(target_.max_retries) +
                    ") exceeded after " + std::to_string(retry_count_) +
                    " attempts — entering SLEEP. Last error: " + err_msg);
                set_error("Max retries exceeded");
                set_state(State::STOPPED);
                return;
            }
            log_warn("[StreamClient]",
                "Connect attempt " + std::to_string(retry_count_) + " failed: " + err_msg +
                " — retrying in " + std::to_string(target_.retry_interval_sec) + "s" +
                (target_.max_retries >= 0
                    ? " (" + std::to_string(target_.max_retries - retry_count_) + " remaining)"
                    : " (unlimited retries)"));
            set_state(State::RECONNECTING);
            for (int i = 0; i < target_.retry_interval_sec * 10 && !watchdog_stop_.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (!watchdog_stop_.load()) {
                log_connect("[StreamClient]",
                    "Retry " + std::to_string(retry_count_ + 1) +
                    " — reconnecting to " + target_.host + ":" + std::to_string(target_.port));
                set_state(State::CONNECTING);
            }
        } else {
            retry_count_ = 0;
            set_state(State::CONNECTED);
            log_connect("[StreamClient]",
                "CONNECTED to " + target_.host + ":" + std::to_string(target_.port) +
                target_.mount + " — monitoring for disconnect");

            // Monitor connection — poll for disconnect via MSG_PEEK
            while (!watchdog_stop_.load() && state_.load() == State::CONNECTED) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                {
                    std::lock_guard<std::mutex> lk(sock_mutex_);
                    if (sock_fd_ < 0) break;
                    char buf[1];
#ifdef _WIN32
                    /* Windows: use non-blocking mode temporarily for peek */
                    u_long nb = 1;
                    ioctlsocket(sock_fd_, FIONBIO, &nb);
                    ssize_t r = ::recv(sock_fd_, buf, 1, MSG_PEEK);
                    nb = 0;
                    ioctlsocket(sock_fd_, FIONBIO, &nb);
#else
                    ssize_t r = ::recv(sock_fd_, buf, 1, MSG_PEEK | MSG_DONTWAIT);
#endif
                    if (r == 0) {
                        do_disconnect_locked();
                        set_error("Server disconnected");
                        log_warn("[StreamClient]", "Server closed the connection — will reconnect");
                        break;
                    }
                }
            }
            if (!watchdog_stop_.load() && state_.load() != State::STOPPED) {
                log_connect("[StreamClient]",
                    "Connection lost — waiting " + std::to_string(target_.retry_interval_sec) +
                    "s before reconnect");
                set_state(State::RECONNECTING);
                for (int i = 0; i < target_.retry_interval_sec * 10 && !watchdog_stop_.load(); ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                if (!watchdog_stop_.load()) {
                    set_state(State::CONNECTING);
                }
            }
        }
    }

    log_connect("[StreamClient]", "Watchdog thread exiting (stop requested)");
}

// ---------------------------------------------------------------------------
// do_connect — blocking connect attempt
// ---------------------------------------------------------------------------
bool StreamClient::do_connect()
{
    log_connect("[StreamClient]",
        "TCP connect → " + target_.host + ":" + std::to_string(target_.port) + target_.mount);

    if (!tcp_connect(target_.host, target_.port)) return false;

    bool ok = false;
    // Icecast2, Shoutcast v2, Live365, and Mcaster1 DNAS all use Icecast2 PUT protocol
    if (target_.protocol == StreamTarget::Protocol::ICECAST2 ||
        target_.protocol == StreamTarget::Protocol::SHOUTCAST_V2 ||
        target_.protocol == StreamTarget::Protocol::LIVE365 ||
        target_.protocol == StreamTarget::Protocol::MCASTER1_DNAS) {
        ok = send_icecast2_headers();
    } else {
        ok = send_shoutcast_v1_headers();
    }

    if (ok) {
        connect_time_.store(static_cast<uint64_t>(std::time(nullptr)));
        log_connect("[StreamClient]", "Handshake OK — source connection live");
    } else {
        log_error("[StreamClient]", "Handshake FAILED: " + last_error());
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
    log_auth("[StreamClient]",
        "ICY2/Icecast2 PUT handshake — user=" + target_.username +
        " content-type=" + target_.content_type +
        " mount=" + target_.mount);

    std::string cred = target_.username + ":" + target_.password;
    std::string b64  = base64_encode(cred);

    std::string req;
    req.reserve(4096);
    req += "PUT " + target_.mount + " HTTP/1.1\r\n";
    req += "Host: " + target_.host + ":" + std::to_string(target_.port) + "\r\n";
    req += "Authorization: Basic " + b64 + "\r\n";
    req += "User-Agent: Mcaster1DSPEncoder/1.3.1\r\n";
    req += "Content-Type: " + target_.content_type + "\r\n";
    req += "Ice-Public: " + std::to_string(target_.icy2_is_public ? 1 : 0) + "\r\n";
    req += "Ice-Name: " + target_.station_name + "\r\n";
    req += "Ice-Description: " + target_.description + "\r\n";
    req += "Ice-Genre: " + target_.genre + "\r\n";
    req += "Ice-Url: " + target_.url + "\r\n";
    req += "Ice-Audio-Info: ice-samplerate=" + std::to_string(target_.sample_rate)
        + ";ice-bitrate=" + std::to_string(target_.bitrate)
        + ";ice-channels=" + std::to_string(target_.channels) + "\r\n";

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

    req += "Transfer-Encoding: chunked\r\n"
           "Expect: 100-continue\r\n"
           "\r\n";

    std::lock_guard<std::mutex> lk(sock_mutex_);
    if (tcp_write(reinterpret_cast<const uint8_t*>(req.c_str()), req.size()) < 0) {
        set_error("Failed to send PUT headers");
        return false;
    }

    char resp[512] = {};
    int n = ::recv(sock_fd_, resp, static_cast<int>(sizeof(resp) - 1), 0);
    if (n <= 0) { set_error("No response from server"); return false; }

    std::string rs(resp, static_cast<size_t>(n));
    // Log the server's response (first line, truncated)
    std::string rs_preview = rs.substr(0, std::min((size_t)120, rs.size()));
    // Strip CR/LF for cleaner log output
    rs_preview.erase(std::remove(rs_preview.begin(), rs_preview.end(), '\r'), rs_preview.end());
    rs_preview.erase(std::remove(rs_preview.begin(), rs_preview.end(), '\n'), rs_preview.end());
    if (rs.find("100") != std::string::npos || rs.find("200") != std::string::npos) {
        log_auth("[StreamClient]", "Server accepted PUT: " + rs_preview);
        return true;
    } else {
        log_error("[StreamClient]", "Server rejected PUT: " + rs_preview);
        set_error("Server rejected connection: " + rs.substr(0, 80));
        return false;
    }
}

// ---------------------------------------------------------------------------
// send_shoutcast_v1_headers — legacy SOURCE handshake
// ---------------------------------------------------------------------------
bool StreamClient::send_shoutcast_v1_headers()
{
    log_auth("[StreamClient]",
        "Shoutcast v1 SOURCE handshake — mount=" + target_.mount +
        "  br=" + std::to_string(target_.bitrate) + "kbps");

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
    int n = ::recv(sock_fd_, resp, static_cast<int>(sizeof(resp) - 1), 0);
    if (n <= 0) { set_error("No response"); return false; }

    std::string rs(resp, static_cast<size_t>(n));
    std::string rs_preview = rs.substr(0, std::min((size_t)80, rs.size()));
    rs_preview.erase(std::remove(rs_preview.begin(), rs_preview.end(), '\r'), rs_preview.end());
    rs_preview.erase(std::remove(rs_preview.begin(), rs_preview.end(), '\n'), rs_preview.end());
    if (rs.find("OK") != std::string::npos || rs.find("200") != std::string::npos) {
        log_auth("[StreamClient]", "Shoutcast v1 accepted: " + rs_preview);
        return true;
    } else {
        log_error("[StreamClient]", "Shoutcast v1 rejected: " + rs_preview);
        set_error("Shoutcast rejected: " + rs.substr(0, 60));
        return false;
    }
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
        set_nosigpipe(fd);
        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(res);

    if (fd < 0) {
        set_error("Could not connect to " + host + ":" + port_str);
        log_error("[StreamClient]", "TCP connect FAILED: " + host + ":" + port_str);
        return false;
    }

    log_connect("[StreamClient]", "TCP connect OK: " + host + ":" + port_str);
    std::lock_guard<std::mutex> lk(sock_mutex_);
    do_disconnect_locked();
    sock_fd_ = fd;
    return true;
}

// ---------------------------------------------------------------------------
// tcp_write — blocking write (SO_NOSIGPIPE set on socket instead of MSG_NOSIGNAL)
// ---------------------------------------------------------------------------
ssize_t StreamClient::tcp_write(const uint8_t* data, size_t len)
{
    if (sock_fd_ < 0) return -1;
    size_t sent = 0;
    while (sent < len) {
        auto* buf = reinterpret_cast<const char*>(data + sent);
        ssize_t n = ::send(sock_fd_, buf, static_cast<int>(len - sent), 0);
        if (n < 0) {
#ifdef _WIN32
            int e = WSAGetLastError();
            if (e == WSAEINTR) continue;
            set_error("send error: " + std::to_string(e));
#else
            if (errno == EINTR) continue;
            set_error(std::string("send: ") + strerror(errno));
#endif
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
    // SEC-009: Scrub potential credentials from log output
    std::string safe_msg = msg;
    // Redact Base64 auth tokens
    auto pos = safe_msg.find("Basic ");
    if (pos != std::string::npos) {
        auto end = safe_msg.find_first_of("\r\n ", pos + 6);
        if (end != std::string::npos)
            safe_msg.replace(pos + 6, end - pos - 6, "[REDACTED]");
        else
            safe_msg.replace(pos + 6, std::string::npos, "[REDACTED]");
    }
    // Redact password= query parameters
    pos = safe_msg.find("pass=");
    if (pos != std::string::npos) {
        auto end = safe_msg.find_first_of("&\r\n ", pos + 5);
        if (end != std::string::npos)
            safe_msg.replace(pos + 5, end - pos - 5, "[REDACTED]");
        else
            safe_msg.replace(pos + 5, std::string::npos, "[REDACTED]");
    }
    // Redact icy-password header values
    pos = safe_msg.find("icy-password: ");
    if (pos != std::string::npos) {
        auto end = safe_msg.find_first_of("\r\n", pos + 14);
        if (end != std::string::npos)
            safe_msg.replace(pos + 14, end - pos - 14, "[REDACTED]");
        else
            safe_msg.replace(pos + 14, std::string::npos, "[REDACTED]");
    }
    fprintf(stderr, "[StreamClient] Error: %s\n", safe_msg.c_str());
}

void StreamClient::set_state(State s)
{
    state_.store(s);
    if (state_cb_) state_cb_(s);
}

} // namespace mc1
