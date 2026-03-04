// podcast/http_client.h — HTTP client wrapper (libcurl)
// Phase M8 — Mcaster1DSPEncoder macOS
//
// Thin wrapper around libcurl for REST API podcast publishing.
// All methods return immediately (blocking) — call from background thread
// for non-blocking UI behavior.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>
#include <cstdint>

namespace mc1 {

struct HttpResponse {
    int         status_code = 0;
    std::string body;
    std::map<std::string, std::string> headers;
    std::string error;       // non-empty on transport failure (DNS, timeout, etc.)
    bool        ok() const { return status_code >= 200 && status_code < 300; }
};

class HttpClient {
public:
    using ProgressCallback = std::function<void(int64_t bytes_sent, int64_t total_bytes)>;
    using Headers = std::map<std::string, std::string>;

    // Returns true if libcurl was compiled in
    static bool isAvailable();

    // GET request
    static HttpResponse get(const std::string& url,
                            const Headers& headers = {});

    // POST with JSON body (Content-Type: application/json)
    static HttpResponse post_json(const std::string& url,
                                  const std::string& json_body,
                                  const Headers& headers = {});

    // POST with form-encoded body (Content-Type: application/x-www-form-urlencoded)
    static HttpResponse post_form(const std::string& url,
                                  const std::string& form_body,
                                  const Headers& headers = {});

    // POST multipart form data (for file uploads)
    struct MultipartField {
        std::string name;
        std::string value;           // for text fields
        std::string filepath;        // for file fields (mutually exclusive with value)
        std::string content_type;    // MIME type for file fields
        std::string filename;        // override filename in Content-Disposition
    };
    static HttpResponse post_multipart(const std::string& url,
                                       const std::vector<MultipartField>& fields,
                                       const Headers& headers = {},
                                       ProgressCallback progress = nullptr);

    // PUT raw data (e.g., S3 presigned URL upload)
    static HttpResponse put_data(const std::string& url,
                                 const std::string& data,
                                 const std::string& content_type,
                                 const Headers& headers = {},
                                 ProgressCallback progress = nullptr);

    // PUT file from disk
    static HttpResponse put_file(const std::string& url,
                                 const std::string& filepath,
                                 const std::string& content_type,
                                 const Headers& headers = {},
                                 ProgressCallback progress = nullptr);

    // Helper: Base64 encode a string
    static std::string base64_encode(const std::string& input);

    // Helper: Build "Basic <base64(user:pass)>" header value
    static std::string basic_auth_header(const std::string& username,
                                         const std::string& password);
};

} // namespace mc1
