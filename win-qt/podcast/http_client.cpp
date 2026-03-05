// podcast/http_client.cpp — HTTP client wrapper (libcurl) implementation
// Phase M8 — Mcaster1DSPEncoder macOS
#include "http_client.h"

#include <mutex>
#include <cstdio>

#ifdef HAVE_LIBCURL
#include <curl/curl.h>
#endif

namespace mc1 {

// ── Base64 encoder (no external dependency) ──────────────────────────────

static const char kBase64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string HttpClient::base64_encode(const std::string& input)
{
    std::string out;
    out.reserve(((input.size() + 2) / 3) * 4);

    const auto* p = reinterpret_cast<const unsigned char*>(input.data());
    size_t len = input.size();

    for (size_t i = 0; i < len; i += 3) {
        unsigned val = (unsigned)p[i] << 16;
        if (i + 1 < len) val |= (unsigned)p[i+1] << 8;
        if (i + 2 < len) val |= (unsigned)p[i+2];

        out += kBase64Table[(val >> 18) & 0x3F];
        out += kBase64Table[(val >> 12) & 0x3F];
        out += (i + 1 < len) ? kBase64Table[(val >> 6) & 0x3F] : '=';
        out += (i + 2 < len) ? kBase64Table[val & 0x3F]        : '=';
    }
    return out;
}

std::string HttpClient::basic_auth_header(const std::string& username,
                                           const std::string& password)
{
    return "Basic " + base64_encode(username + ":" + password);
}

// ── libcurl implementation ───────────────────────────────────────────────

#ifdef HAVE_LIBCURL

static std::once_flag g_curl_init;

static void ensure_curl_init()
{
    std::call_once(g_curl_init, []() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    });
}

// Write callback: append response body to std::string
static size_t write_cb(void* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* body = static_cast<std::string*>(userdata);
    body->append(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

// Header callback: parse "Key: Value" into map
static size_t header_cb(char* buffer, size_t size, size_t nmemb, void* userdata)
{
    auto* headers = static_cast<HttpClient::Headers*>(userdata);
    size_t total = size * nmemb;
    std::string line(buffer, total);

    // Strip trailing \r\n
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
        line.pop_back();

    auto colon = line.find(':');
    if (colon != std::string::npos && colon > 0) {
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        // Trim leading whitespace from value
        size_t start = val.find_first_not_of(" \t");
        if (start != std::string::npos)
            val = val.substr(start);
        (*headers)[key] = val;
    }
    return total;
}

// Progress callback wrapper
struct ProgressData {
    HttpClient::ProgressCallback cb;
};

static int progress_cb(void* clientp, curl_off_t /*dltotal*/, curl_off_t /*dlnow*/,
                        curl_off_t ultotal, curl_off_t ulnow)
{
    auto* pd = static_cast<ProgressData*>(clientp);
    if (pd && pd->cb)
        pd->cb(static_cast<int64_t>(ulnow), static_cast<int64_t>(ultotal));
    return 0;
}

// Apply common curl options
static void set_common_opts(CURL* curl, HttpClient::Headers& resp_headers,
                             std::string& resp_body, const HttpClient::Headers& req_headers,
                             struct curl_slist** header_list)
{
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp_headers);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);

    // Apply request headers
    *header_list = nullptr;
    for (auto& [key, val] : req_headers)
        *header_list = curl_slist_append(*header_list, (key + ": " + val).c_str());
    if (*header_list)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, *header_list);
}

static HttpResponse finish_request(CURL* curl, CURLcode res, struct curl_slist* headers,
                                    std::string& body, HttpClient::Headers& resp_headers)
{
    HttpResponse resp;
    if (res != CURLE_OK) {
        resp.error = curl_easy_strerror(res);
    } else {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        resp.status_code = static_cast<int>(code);
        resp.body = std::move(body);
        resp.headers = std::move(resp_headers);
    }
    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return resp;
}

bool HttpClient::isAvailable() { return true; }

HttpResponse HttpClient::get(const std::string& url, const Headers& headers)
{
    ensure_curl_init();
    CURL* curl = curl_easy_init();
    if (!curl) return { 0, "", {}, "Failed to init curl" };

    std::string body;
    Headers resp_headers;
    struct curl_slist* hlist = nullptr;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    set_common_opts(curl, resp_headers, body, headers, &hlist);

    CURLcode res = curl_easy_perform(curl);
    return finish_request(curl, res, hlist, body, resp_headers);
}

HttpResponse HttpClient::post_json(const std::string& url,
                                    const std::string& json_body,
                                    const Headers& headers)
{
    ensure_curl_init();
    CURL* curl = curl_easy_init();
    if (!curl) return { 0, "", {}, "Failed to init curl" };

    std::string body;
    Headers resp_headers;
    struct curl_slist* hlist = nullptr;

    // Merge Content-Type with user headers
    Headers merged = headers;
    if (merged.find("Content-Type") == merged.end())
        merged["Content-Type"] = "application/json";

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(json_body.size()));
    set_common_opts(curl, resp_headers, body, merged, &hlist);

    CURLcode res = curl_easy_perform(curl);
    return finish_request(curl, res, hlist, body, resp_headers);
}

HttpResponse HttpClient::post_form(const std::string& url,
                                    const std::string& form_body,
                                    const Headers& headers)
{
    ensure_curl_init();
    CURL* curl = curl_easy_init();
    if (!curl) return { 0, "", {}, "Failed to init curl" };

    std::string body;
    Headers resp_headers;
    struct curl_slist* hlist = nullptr;

    Headers merged = headers;
    if (merged.find("Content-Type") == merged.end())
        merged["Content-Type"] = "application/x-www-form-urlencoded";

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form_body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(form_body.size()));
    set_common_opts(curl, resp_headers, body, merged, &hlist);

    CURLcode res = curl_easy_perform(curl);
    return finish_request(curl, res, hlist, body, resp_headers);
}

HttpResponse HttpClient::post_multipart(const std::string& url,
                                         const std::vector<MultipartField>& fields,
                                         const Headers& headers,
                                         ProgressCallback progress)
{
    ensure_curl_init();
    CURL* curl = curl_easy_init();
    if (!curl) return { 0, "", {}, "Failed to init curl" };

    std::string body;
    Headers resp_headers;
    struct curl_slist* hlist = nullptr;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    set_common_opts(curl, resp_headers, body, headers, &hlist);

    // Build multipart form
    curl_mime* mime = curl_mime_init(curl);
    for (auto& f : fields) {
        curl_mimepart* part = curl_mime_addpart(mime);
        curl_mime_name(part, f.name.c_str());
        if (!f.filepath.empty()) {
            curl_mime_filedata(part, f.filepath.c_str());
            if (!f.content_type.empty())
                curl_mime_type(part, f.content_type.c_str());
            if (!f.filename.empty())
                curl_mime_filename(part, f.filename.c_str());
        } else {
            curl_mime_data(part, f.value.c_str(), f.value.size());
        }
    }
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

    // Progress
    ProgressData pd{progress};
    if (progress) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &pd);
    }

    CURLcode res = curl_easy_perform(curl);
    curl_mime_free(mime);
    return finish_request(curl, res, hlist, body, resp_headers);
}

HttpResponse HttpClient::put_data(const std::string& url,
                                   const std::string& data,
                                   const std::string& content_type,
                                   const Headers& headers,
                                   ProgressCallback progress)
{
    ensure_curl_init();
    CURL* curl = curl_easy_init();
    if (!curl) return { 0, "", {}, "Failed to init curl" };

    std::string body;
    Headers resp_headers;
    struct curl_slist* hlist = nullptr;

    Headers merged = headers;
    if (!content_type.empty())
        merged["Content-Type"] = content_type;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(data.size()));
    set_common_opts(curl, resp_headers, body, merged, &hlist);

    ProgressData pd{progress};
    if (progress) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &pd);
    }

    CURLcode res = curl_easy_perform(curl);
    return finish_request(curl, res, hlist, body, resp_headers);
}

HttpResponse HttpClient::put_file(const std::string& url,
                                   const std::string& filepath,
                                   const std::string& content_type,
                                   const Headers& headers,
                                   ProgressCallback progress)
{
    ensure_curl_init();

    // Read file into memory (reasonable for podcast episodes < 500MB)
    FILE* fp = fopen(filepath.c_str(), "rb");
    if (!fp) return { 0, "", {}, "Cannot open file: " + filepath };

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    std::string file_data(static_cast<size_t>(file_size), '\0');
    size_t bytes_read = fread(&file_data[0], 1, static_cast<size_t>(file_size), fp);
    fclose(fp);

    if (static_cast<long>(bytes_read) != file_size)
        return { 0, "", {}, "Failed to read complete file" };

    return put_data(url, file_data, content_type, headers, progress);
}

#else // !HAVE_LIBCURL

bool HttpClient::isAvailable() { return false; }

static HttpResponse not_available()
{
    return { 0, "", {}, "libcurl not available — install via: brew install curl" };
}

HttpResponse HttpClient::get(const std::string&, const Headers&)
{ return not_available(); }

HttpResponse HttpClient::post_json(const std::string&, const std::string&, const Headers&)
{ return not_available(); }

HttpResponse HttpClient::post_form(const std::string&, const std::string&, const Headers&)
{ return not_available(); }

HttpResponse HttpClient::post_multipart(const std::string&, const std::vector<MultipartField>&,
                                         const Headers&, ProgressCallback)
{ return not_available(); }

HttpResponse HttpClient::put_data(const std::string&, const std::string&, const std::string&,
                                   const Headers&, ProgressCallback)
{ return not_available(); }

HttpResponse HttpClient::put_file(const std::string&, const std::string&, const std::string&,
                                   const Headers&, ProgressCallback)
{ return not_available(); }

#endif // HAVE_LIBCURL

} // namespace mc1
