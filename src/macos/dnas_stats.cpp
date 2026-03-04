// dnas_stats.cpp — Mcaster1DNAS XML stats fetcher
// Phase M4 — Mcaster1DSPEncoder macOS Qt 6 Build
#include "dnas_stats.h"

#include <cstring>
#include <cstdio>
#include <cerrno>
#include <chrono>
#include <regex>

// POSIX sockets
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

// macOS SSL (Security.framework) for HTTPS
#include <Security/Security.h>
#include <Security/SecureTransport.h>

// Base64 for HTTP Basic auth (same as stream_client.cpp)
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

namespace mc1 {

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
DnasStats::DnasStats(const DnasConfig& cfg)
    : cfg_(cfg)
{
}

DnasStats::~DnasStats()
{
    stop_polling();
}

// ---------------------------------------------------------------------------
// fetch — one-shot synchronous fetch
// ---------------------------------------------------------------------------
DnasStatsResult DnasStats::fetch()
{
    DnasStatsResult result;
    result.source_label = cfg_.host + ":" + std::to_string(cfg_.port) + cfg_.stats_url;

    std::string body;
    int status = 0;
    bool ok = http_get(cfg_.host, cfg_.port, cfg_.stats_url,
                       cfg_.username, cfg_.password,
                       cfg_.ssl, 10, status, body);

    if (!ok) {
        result.error = "DNAS unreachable";
        std::lock_guard<std::mutex> lk(result_mutex_);
        last_result_ = result;
        return result;
    }

    result.ok            = (status == 200);
    result.http_status   = status;
    result.raw_body      = body;
    result.listeners     = parse_listeners(body);
    result.max_listeners = parse_max_listeners(body);

    if (!result.ok)
        result.error = "HTTP " + std::to_string(status);

    std::lock_guard<std::mutex> lk(result_mutex_);
    last_result_ = result;
    return result;
}

// ---------------------------------------------------------------------------
// Polling
// ---------------------------------------------------------------------------
void DnasStats::start_polling(int interval_sec, Callback cb)
{
    stop_polling();
    poll_interval_sec_ = interval_sec;
    poll_cb_ = cb;
    poll_stop_.store(false);
    polling_.store(true);
    poll_thread_ = std::thread(&DnasStats::poll_loop, this);
}

void DnasStats::stop_polling()
{
    poll_stop_.store(true);
    if (poll_thread_.joinable()) poll_thread_.join();
    polling_.store(false);
}

DnasStatsResult DnasStats::last_result() const
{
    std::lock_guard<std::mutex> lk(result_mutex_);
    return last_result_;
}

void DnasStats::poll_loop()
{
    while (!poll_stop_.load()) {
        auto result = fetch();
        if (poll_cb_ && !poll_stop_.load())
            poll_cb_(result);

        // Sleep in 100ms increments so stop_polling() is responsive
        for (int i = 0; i < poll_interval_sec_ * 10 && !poll_stop_.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// ---------------------------------------------------------------------------
// http_get — plain HTTP GET via raw TCP socket
// For SSL targets, uses macOS SecureTransport (deprecated but functional).
// ---------------------------------------------------------------------------
bool DnasStats::http_get(const std::string& host, uint16_t port,
                         const std::string& path,
                         const std::string& username,
                         const std::string& password,
                         bool use_ssl,
                         int timeout_sec,
                         int& out_status,
                         std::string& out_body)
{
    // Resolve + connect
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    std::string port_str = std::to_string(port);

    if (::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0)
        return false;

    int fd = -1;
    for (auto* p = res; p; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        // Set SO_NOSIGPIPE
        int on = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
        // Set connect timeout via non-blocking + poll
        fcntl(fd, F_SETFL, O_NONBLOCK);
        int cr = ::connect(fd, p->ai_addr, p->ai_addrlen);
        if (cr == 0) {
            fcntl(fd, F_SETFL, 0); // back to blocking
            break;
        }
        if (errno == EINPROGRESS) {
            struct pollfd pfd = { fd, POLLOUT, 0 };
            int pr = ::poll(&pfd, 1, timeout_sec * 1000);
            if (pr > 0 && (pfd.revents & POLLOUT)) {
                int err = 0;
                socklen_t len = sizeof(err);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
                if (err == 0) {
                    fcntl(fd, F_SETFL, 0);
                    break;
                }
            }
        }
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(res);
    if (fd < 0) return false;

    // Set read timeout
    struct timeval tv;
    tv.tv_sec  = timeout_sec;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Build request
    std::string req = "GET " + path + " HTTP/1.0\r\n"
                      "Host: " + host + ":" + port_str + "\r\n"
                      "User-Agent: Mcaster1DSPEncoder/0.4.0\r\n"
                      "Connection: close\r\n";
    if (!username.empty()) {
        std::string cred = username + ":" + password;
        req += "Authorization: Basic " + base64_encode(cred) + "\r\n";
    }
    req += "\r\n";

    // SSL or plain send/recv
    SSLContextRef ssl_ctx = nullptr;
    if (use_ssl) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        ssl_ctx = SSLCreateContext(kCFAllocatorDefault, kSSLClientSide, kSSLStreamType);
        if (!ssl_ctx) { ::close(fd); return false; }

        // I/O callbacks using the raw socket fd
        SSLSetIOFuncs(ssl_ctx,
            // read callback
            [](SSLConnectionRef conn, void* data, size_t* len) -> OSStatus {
                int sock = (int)(intptr_t)conn;
                ssize_t r = ::recv(sock, data, *len, 0);
                if (r > 0) { *len = (size_t)r; return noErr; }
                if (r == 0) { *len = 0; return errSSLClosedGraceful; }
                *len = 0;
                return errSSLWouldBlock;
            },
            // write callback
            [](SSLConnectionRef conn, const void* data, size_t* len) -> OSStatus {
                int sock = (int)(intptr_t)conn;
                ssize_t w = ::send(sock, data, *len, 0);
                if (w > 0) { *len = (size_t)w; return noErr; }
                *len = 0;
                return errSSLWouldBlock;
            }
        );
        SSLSetConnection(ssl_ctx, (SSLConnectionRef)(intptr_t)fd);

        // Disable certificate verification (DNAS often uses self-signed certs)
        SSLSetSessionOption(ssl_ctx, kSSLSessionOptionBreakOnServerAuth, true);

        OSStatus hs;
        do {
            hs = SSLHandshake(ssl_ctx);
            if (hs == errSSLServerAuthCompleted) {
                // Accept any cert — continue handshake
                continue;
            }
        } while (hs == errSSLWouldBlock);

        if (hs != noErr && hs != errSSLServerAuthCompleted) {
            CFRelease(ssl_ctx);
            ::close(fd);
            return false;
        }

        // Send request via SSL
        size_t written = 0;
        SSLWrite(ssl_ctx, req.c_str(), req.size(), &written);

        // Read response via SSL
        char buf[4096];
        std::string raw;
        while (true) {
            size_t read_len = 0;
            OSStatus rs = SSLRead(ssl_ctx, buf, sizeof(buf), &read_len);
            if (read_len > 0)
                raw.append(buf, read_len);
            if (rs == errSSLClosedGraceful || rs == errSSLClosedAbort || read_len == 0)
                break;
        }

        SSLClose(ssl_ctx);
        CFRelease(ssl_ctx);
#pragma clang diagnostic pop
        ::close(fd);

        // Parse HTTP response
        auto hdr_end = raw.find("\r\n\r\n");
        if (hdr_end == std::string::npos) return false;
        std::string status_line = raw.substr(0, raw.find("\r\n"));
        // "HTTP/1.x NNN ..."
        auto sp = status_line.find(' ');
        if (sp != std::string::npos)
            out_status = std::atoi(status_line.c_str() + sp + 1);
        out_body = raw.substr(hdr_end + 4);
        return true;
    }

    // Plain HTTP (no SSL)
    const auto* sdata = reinterpret_cast<const uint8_t*>(req.c_str());
    size_t slen = req.size(), sent = 0;
    while (sent < slen) {
        ssize_t n = ::send(fd, sdata + sent, slen - sent, 0);
        if (n <= 0) { ::close(fd); return false; }
        sent += (size_t)n;
    }

    // Read full response
    std::string raw;
    char buf[4096];
    while (true) {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        raw.append(buf, (size_t)n);
    }
    ::close(fd);

    // Parse HTTP response
    auto hdr_end = raw.find("\r\n\r\n");
    if (hdr_end == std::string::npos) return false;
    std::string status_line = raw.substr(0, raw.find("\r\n"));
    auto sp = status_line.find(' ');
    if (sp != std::string::npos)
        out_status = std::atoi(status_line.c_str() + sp + 1);
    out_body = raw.substr(hdr_end + 4);
    return true;
}

// ---------------------------------------------------------------------------
// XML parsing helpers — extract listener counts via regex
// ---------------------------------------------------------------------------
int DnasStats::parse_listeners(const std::string& body)
{
    // Try multiple XML element names used by different server types
    std::regex patterns[] = {
        std::regex("<listeners>(\\d+)</listeners>", std::regex::icase),
        std::regex("<total_listeners>(\\d+)</total_listeners>", std::regex::icase),
        std::regex("<currentlisteners>(\\d+)</currentlisteners>", std::regex::icase),
    };
    for (const auto& re : patterns) {
        std::smatch m;
        if (std::regex_search(body, m, re))
            return std::stoi(m[1].str());
    }
    return 0;
}

int DnasStats::parse_max_listeners(const std::string& body)
{
    std::regex patterns[] = {
        std::regex("<maxlisteners>(\\d+)</maxlisteners>", std::regex::icase),
        std::regex("<max_listeners>(\\d+)</max_listeners>", std::regex::icase),
    };
    for (const auto& re : patterns) {
        std::smatch m;
        if (std::regex_search(body, m, re))
            return std::stoi(m[1].str());
    }
    return 0;
}

} // namespace mc1
