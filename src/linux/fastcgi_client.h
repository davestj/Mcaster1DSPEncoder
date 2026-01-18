// fastcgi_client.h — FastCGI binary protocol client for php-fpm bridge
// Mcaster1DSPEncoder Linux v1.2.0
//
// One instance is shared across request threads; forward() opens a new Unix
// socket connection per request (php-fpm closes connections after each
// request by default, i.e. FCGI_KEEP_CONN = 0).

#pragma once

#include <map>
#include <string>

// Parsed response from php-fpm
struct FcgiResponse {
    int         status       = 200;
    std::string content_type = "text/html; charset=utf-8";
    std::map<std::string, std::string> headers;
    std::string body;
    std::string error;
    bool        ok           = false;
};

// FastCGI client — speaks the FastCGI/1.0 binary protocol over a Unix domain
// socket path (e.g. /run/php/php8.2-fpm-mc1.sock).
class FastCgiClient {
public:
    explicit FastCgiClient(const std::string& sock_path);

    // Forward one HTTP request to php-fpm. Thread-safe (no shared mutable state).
    FcgiResponse forward(
        const std::string& method,           // "GET" or "POST"
        const std::string& script_filename,  // absolute path on disk to .php file
        const std::string& script_name,      // URI path, e.g. "/app/media.php"
        const std::string& query_string,     // e.g. "page=2&q=jazz"
        const std::string& request_uri,      // path + "?" + query (or just path)
        const std::string& content_type,     // for POST (may be empty)
        const std::string& body,             // POST body (may be empty)
        const std::string& document_root,    // webroot absolute path
        const std::string& remote_addr  = "127.0.0.1",
        const std::string& server_name  = "localhost",
        int                server_port  = 8330,
        const std::map<std::string, std::string>& extra_params = {}
    );

private:
    std::string sock_path_;

    static void        encode_length(uint32_t len, std::string& out);
    static void        encode_nv(const std::string& name,
                                 const std::string& value, std::string& out);
    static std::string build_params_data(
                           const std::map<std::string, std::string>& params);
    static std::string build_record(uint8_t type, uint16_t req_id,
                                    const void* data, size_t len);
    static FcgiResponse parse_output(const std::string& raw);

    int  connect_socket()                             const;
    bool send_all(int fd, const void* buf, size_t n)  const;
    bool recv_all(int fd, void*       buf, size_t n)  const;
};
