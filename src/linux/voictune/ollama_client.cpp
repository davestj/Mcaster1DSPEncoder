/*
 * Mcaster1 VoicTune — Ollama LLM Client v1.0.0
 * voictune/ollama_client.cpp
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif

#include "ollama_client.h"
#include "vt_logger.h"
#include "../external/include/httplib.h"

#include <regex>

namespace mc1vt {

OllamaClient::OllamaClient(const std::string& endpoint,
                             const std::string& default_model,
                             int timeout_sec)
    : endpoint_(endpoint)
    , default_model_(default_model)
    , timeout_sec_(timeout_sec)
{}

void OllamaClient::parse_endpoint(std::string& host, int& port, bool& use_ssl) const
{
    /* We parse "http://host:port" or "https://host:port" */
    use_ssl = false;
    host    = "127.0.0.1";
    port    = 11434;

    std::string ep = endpoint_;
    if (ep.substr(0, 8) == "https://") {
        use_ssl = true;
        ep = ep.substr(8);
    } else if (ep.substr(0, 7) == "http://") {
        ep = ep.substr(7);
    }

    auto colon = ep.find(':');
    if (colon != std::string::npos) {
        host = ep.substr(0, colon);
        port = std::stoi(ep.substr(colon + 1));
    } else {
        host = ep;
        port = use_ssl ? 443 : 11434;
    }
}

json OllamaClient::get(const std::string& path)
{
    std::lock_guard<std::mutex> lk(mtx_);
    std::string host;
    int port;
    bool use_ssl;
    parse_endpoint(host, port, use_ssl);

    httplib::Client cli(host, port);
    cli.set_connection_timeout(3);
    cli.set_read_timeout(timeout_sec_);

    auto result = cli.Get(path);
    if (!result) {
        return {{"error", "Connection failed to " + endpoint_ + path}};
    }
    if (result->status != 200) {
        return {{"error", "HTTP " + std::to_string(result->status)}, {"body", result->body}};
    }
    try {
        return json::parse(result->body);
    } catch (...) {
        return {{"error", "Invalid JSON response"}, {"body", result->body}};
    }
}

json OllamaClient::post(const std::string& path, const json& body)
{
    std::lock_guard<std::mutex> lk(mtx_);
    std::string host;
    int port;
    bool use_ssl;
    parse_endpoint(host, port, use_ssl);

    httplib::Client cli(host, port);
    cli.set_connection_timeout(3);
    cli.set_read_timeout(timeout_sec_);
    cli.set_write_timeout(5);

    auto result = cli.Post(path, body.dump(), "application/json");
    if (!result) {
        return {{"error", "Connection failed to " + endpoint_ + path}};
    }
    if (result->status != 200) {
        return {{"error", "HTTP " + std::to_string(result->status)}, {"body", result->body}};
    }
    try {
        return json::parse(result->body);
    } catch (...) {
        return {{"error", "Invalid JSON response"}, {"body", result->body}};
    }
}

bool OllamaClient::is_available()
{
    auto r = get("/api/tags");
    return !r.contains("error");
}

json OllamaClient::list_models()
{
    return get("/api/tags");
}

json OllamaClient::chat(const json& messages, const std::string& model)
{
    std::string m = model.empty() ? default_model_ : model;
    json body = {
        {"model", m},
        {"messages", messages},
        {"stream", false}  /* We don't stream — collect full response */
    };

    VT_DBG("Ollama chat request: model=" + m + " messages=" + std::to_string(messages.size()));

    auto result = post("/api/chat", body);

    if (result.contains("error")) {
        VT_WARN("Ollama chat failed: " + result.value("error", "unknown"));
    } else {
        VT_DBG("Ollama chat response received");
    }
    return result;
}

json OllamaClient::generate(const std::string& prompt, const std::string& model)
{
    std::string m = model.empty() ? default_model_ : model;
    json body = {
        {"model", m},
        {"prompt", prompt},
        {"stream", false}
    };

    VT_DBG("Ollama generate request: model=" + m);

    auto result = post("/api/generate", body);

    if (result.contains("error")) {
        VT_WARN("Ollama generate failed: " + result.value("error", "unknown"));
    }
    return result;
}

} // namespace mc1vt
