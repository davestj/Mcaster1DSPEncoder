// fastcgi_client.cpp — FastCGI/1.0 binary protocol implementation
// Mcaster1DSPEncoder Linux v1.2.0
//
// Protocol: https://fast-cgi.github.io/spec
// One Unix domain socket connection per request (FCGI_KEEP_CONN = 0).

#include "fastcgi_client.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <sstream>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// ── FastCGI record type constants ──────────────────────────────────────────

static constexpr uint8_t  FCGI_VERSION_1     = 1;
static constexpr uint8_t  FCGI_BEGIN_REQUEST = 1;
static constexpr uint8_t  FCGI_END_REQUEST   = 3;
static constexpr uint8_t  FCGI_PARAMS        = 4;
static constexpr uint8_t  FCGI_STDIN         = 5;
static constexpr uint8_t  FCGI_STDOUT        = 6;
static constexpr uint8_t  FCGI_STDERR        = 7;
static constexpr uint16_t FCGI_RESPONDER     = 1;
static constexpr uint16_t FCGI_REQUEST_ID    = 1;  // one request per connection

// ── Constructor ────────────────────────────────────────────────────────────

FastCgiClient::FastCgiClient(const std::string& sock_path)
    : sock_path_(sock_path) {}

// ── Name-value pair length encoding (FastCGI spec §3.4) ───────────────────
// Lengths < 128 use 1 byte; lengths >= 128 use 4 bytes with MSB set.

void FastCgiClient::encode_length(uint32_t len, std::string& out)
{
    if (len < 128) {
        out += static_cast<char>(len);
    } else {
        out += static_cast<char>((len >> 24) | 0x80u);
        out += static_cast<char>((len >> 16) & 0xFFu);
        out += static_cast<char>((len >>  8) & 0xFFu);
        out += static_cast<char>( len        & 0xFFu);
    }
}

void FastCgiClient::encode_nv(const std::string& name,
                               const std::string& value,
                               std::string& out)
{
    encode_length(static_cast<uint32_t>(name.size()),  out);
    encode_length(static_cast<uint32_t>(value.size()), out);
    out += name;
    out += value;
}

std::string FastCgiClient::build_params_data(
    const std::map<std::string, std::string>& params)
{
    std::string data;
    for (auto& [k, v] : params) encode_nv(k, v, data);
    return data;
}

// ── FastCGI record builder ─────────────────────────────────────────────────
// Header: version(1) type(1) requestId(2BE) contentLength(2BE)
//         paddingLength(1) reserved(1) content padding

std::string FastCgiClient::build_record(uint8_t type, uint16_t req_id,
                                         const void* data, size_t len)
{
    uint8_t pad = static_cast<uint8_t>((8 - (len & 7)) & 7);
    std::string rec(8 + len + pad, '\0');

    rec[0] = static_cast<char>(FCGI_VERSION_1);
    rec[1] = static_cast<char>(type);
    rec[2] = static_cast<char>((req_id >> 8) & 0xFFu);
    rec[3] = static_cast<char>( req_id       & 0xFFu);
    rec[4] = static_cast<char>((len    >> 8) & 0xFFu);
    rec[5] = static_cast<char>( len          & 0xFFu);
    rec[6] = static_cast<char>(pad);
    rec[7] = 0;  // reserved

    if (data && len > 0)
        std::memcpy(&rec[8], data, len);

    return rec;  // padding bytes remain '\0'
}

// ── Unix domain socket connect ─────────────────────────────────────────────

int FastCgiClient::connect_socket() const
{
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, sock_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

bool FastCgiClient::send_all(int fd, const void* buf, size_t n) const
{
    const char* p = static_cast<const char*>(buf);
    while (n > 0) {
        ssize_t w = ::send(fd, p, n, MSG_NOSIGNAL);
        if (w <= 0) return false;
        p += w;
        n -= static_cast<size_t>(w);
    }
    return true;
}

bool FastCgiClient::recv_all(int fd, void* buf, size_t n) const
{
    char* p = static_cast<char*>(buf);
    while (n > 0) {
        ssize_t r = ::recv(fd, p, n, 0);
        if (r <= 0) return false;
        p += r;
        n -= static_cast<size_t>(r);
    }
    return true;
}

// ── Parse PHP STDOUT into HTTP-style response ──────────────────────────────
// PHP CGI output: "Header: value\r\n...\r\n\r\n<body>"

FcgiResponse FastCgiClient::parse_output(const std::string& raw)
{
    FcgiResponse r;

    auto sep = raw.find("\r\n\r\n");
    if (sep == std::string::npos) {
        r.body         = raw;
        r.content_type = "text/html; charset=utf-8";
        r.ok           = true;
        return r;
    }

    std::string hdr_block = raw.substr(0, sep);
    r.body = raw.substr(sep + 4);

    std::istringstream ss(hdr_block);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        auto col = line.find(':');
        if (col == std::string::npos) continue;

        std::string hname = line.substr(0, col);
        std::string hval  = line.substr(col + 1);

        // trim leading whitespace from value
        auto v0 = hval.find_first_not_of(' ');
        if (v0 != std::string::npos) hval = hval.substr(v0);

        // Case-insensitive header name comparison
        std::string hname_lc = hname;
        for (auto& c : hname_lc)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (hname_lc == "status") {
            try { r.status = std::stoi(hval); } catch (...) {}
        } else if (hname_lc == "content-type") {
            r.content_type = hval;
        } else {
            r.headers[hname] = hval;
        }
    }

    if (r.content_type.empty())
        r.content_type = "text/html; charset=utf-8";

    r.ok = true;
    return r;
}

// ── Main forward — open connection, send request, read response ────────────

FcgiResponse FastCgiClient::forward(
    const std::string& method,
    const std::string& script_filename,
    const std::string& script_name,
    const std::string& query_string,
    const std::string& request_uri,
    const std::string& content_type,
    const std::string& body,
    const std::string& document_root,
    const std::string& remote_addr,
    const std::string& server_name,
    int                server_port,
    const std::map<std::string, std::string>& extra_params)
{
    FcgiResponse err;

    // ── Connect ────────────────────────────────────────────────────────────
    int fd = connect_socket();
    if (fd < 0) {
        err.error = "FastCGI: cannot connect to " + sock_path_
                    + " — " + std::strerror(errno);
        return err;
    }

    // ── BEGIN_REQUEST ──────────────────────────────────────────────────────
    {
        uint8_t begin[8] = {};
        begin[0] = 0;                   // role hi-byte
        begin[1] = FCGI_RESPONDER;      // role lo-byte (1)
        begin[2] = 0;                   // flags: 0 = close on done
        auto rec = build_record(FCGI_BEGIN_REQUEST, FCGI_REQUEST_ID, begin, 8);
        if (!send_all(fd, rec.data(), rec.size())) {
            ::close(fd);
            err.error = "FastCGI: send BEGIN_REQUEST failed";
            return err;
        }
    }

    // ── PARAMS ────────────────────────────────────────────────────────────
    {
        std::map<std::string, std::string> params;
        params["GATEWAY_INTERFACE"] = "CGI/1.1";
        params["SERVER_SOFTWARE"]   = "mcaster1-encoder/1.2.0";
        params["SERVER_NAME"]       = server_name;
        params["SERVER_PORT"]       = std::to_string(server_port);
        params["SERVER_PROTOCOL"]   = "HTTP/1.1";
        params["REQUEST_METHOD"]    = method;
        params["SCRIPT_FILENAME"]   = script_filename;
        params["SCRIPT_NAME"]       = script_name;
        params["QUERY_STRING"]      = query_string;
        params["REQUEST_URI"]       = request_uri;
        params["DOCUMENT_ROOT"]     = document_root;
        params["REMOTE_ADDR"]       = remote_addr;
        params["REMOTE_PORT"]       = "0";
        params["REDIRECT_STATUS"]   = "200";  // required by php-fpm security

        if (!body.empty() || method == "POST") {
            params["CONTENT_TYPE"]   = content_type.empty()
                                       ? "application/x-www-form-urlencoded"
                                       : content_type;
            params["CONTENT_LENGTH"] = std::to_string(body.size());
        }

        // Merge extra params (HTTP_* headers, auth markers, etc.)
        for (auto& [k, v] : extra_params) params[k] = v;

        std::string pdata = build_params_data(params);

        // Send in chunks ≤ 65535 bytes (FastCGI contentLength is uint16)
        for (size_t off = 0; off < pdata.size(); ) {
            size_t chunk = std::min<size_t>(65535, pdata.size() - off);
            auto rec = build_record(FCGI_PARAMS, FCGI_REQUEST_ID,
                                    pdata.data() + off, chunk);
            if (!send_all(fd, rec.data(), rec.size())) {
                ::close(fd);
                err.error = "FastCGI: send PARAMS chunk failed";
                return err;
            }
            off += chunk;
        }
        // Terminate PARAMS stream with zero-length record
        auto eop = build_record(FCGI_PARAMS, FCGI_REQUEST_ID, nullptr, 0);
        if (!send_all(fd, eop.data(), eop.size())) {
            ::close(fd);
            err.error = "FastCGI: send PARAMS terminator failed";
            return err;
        }
    }

    // ── STDIN (POST body) ──────────────────────────────────────────────────
    {
        for (size_t off = 0; off < body.size(); ) {
            size_t chunk = std::min<size_t>(65535, body.size() - off);
            auto rec = build_record(FCGI_STDIN, FCGI_REQUEST_ID,
                                    body.data() + off, chunk);
            if (!send_all(fd, rec.data(), rec.size())) {
                ::close(fd);
                err.error = "FastCGI: send STDIN chunk failed";
                return err;
            }
            off += chunk;
        }
        // Terminate STDIN stream
        auto eos = build_record(FCGI_STDIN, FCGI_REQUEST_ID, nullptr, 0);
        if (!send_all(fd, eos.data(), eos.size())) {
            ::close(fd);
            err.error = "FastCGI: send STDIN terminator failed";
            return err;
        }
    }

    // ── Read response records until END_REQUEST ────────────────────────────
    std::string stdout_data, stderr_data;
    bool done = false;

    while (!done) {
        uint8_t hdr[8];
        if (!recv_all(fd, hdr, 8)) break;

        uint8_t  rtype = hdr[1];
        uint16_t clen  = static_cast<uint16_t>(
                             (static_cast<unsigned>(hdr[4]) << 8) | hdr[5]);
        uint8_t  plen  = hdr[6];

        std::string content(clen, '\0');
        if (clen > 0 && !recv_all(fd, content.data(), clen)) break;

        uint8_t pad_buf[255];
        if (plen > 0 && !recv_all(fd, pad_buf, plen)) break;

        switch (rtype) {
        case FCGI_STDOUT:      stdout_data += content; break;
        case FCGI_STDERR:      stderr_data += content; break;
        case FCGI_END_REQUEST: done = true;            break;
        default: break;
        }
    }

    ::close(fd);

    if (!done && stdout_data.empty()) {
        err.error = "FastCGI: no response received";
        if (!stderr_data.empty())
            err.error += "; php-fpm stderr: " + stderr_data.substr(0, 512);
        return err;
    }

    FcgiResponse resp = parse_output(stdout_data);
    if (!stderr_data.empty())
        resp.headers["X-PHP-Stderr"] = stderr_data.substr(0, 256);

    return resp;
}
