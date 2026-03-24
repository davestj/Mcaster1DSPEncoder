/*
 * Mcaster1 VoicTune — Ollama LLM Client v1.0.0
 * voictune/ollama_client.h
 *
 * HTTP client for the Ollama REST API (localhost:11434).
 * Thread-safe — can be called from any thread.
 * Graceful degradation: is_available() returns false if Ollama is offline.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "../external/include/nlohmann/json.hpp"
#include <string>
#include <mutex>

using json = nlohmann::json;

namespace mc1vt {

class OllamaClient {
public:
    explicit OllamaClient(const std::string& endpoint = "http://127.0.0.1:11434",
                          const std::string& default_model = "llama3.2",
                          int timeout_sec = 30);

    /* We check if Ollama is reachable. */
    bool is_available();

    /* We list available models. Returns {"models": [...]} or empty on error. */
    json list_models();

    /* We send a chat completion request.
     * messages: [{"role":"system","content":"..."}, {"role":"user","content":"..."}]
     * Returns response JSON or {"error":"..."} on failure. */
    json chat(const json& messages, const std::string& model = "");

    /* We send a simple generate request (single prompt, no chat history).
     * Returns {"response":"..."} or {"error":"..."} on failure. */
    json generate(const std::string& prompt, const std::string& model = "");

    void set_endpoint(const std::string& ep) { endpoint_ = ep; }
    void set_model(const std::string& m) { default_model_ = m; }
    void set_timeout(int sec) { timeout_sec_ = sec; }

    const std::string& endpoint() const { return endpoint_; }
    const std::string& model() const { return default_model_; }

private:
    std::string endpoint_;
    std::string default_model_;
    int         timeout_sec_;
    std::mutex  mtx_;

    /* We extract host and port from endpoint URL. */
    void parse_endpoint(std::string& host, int& port, bool& use_ssl) const;

    /* We make an HTTP POST and return the response body as JSON. */
    json post(const std::string& path, const json& body);

    /* We make an HTTP GET and return the response body as JSON. */
    json get(const std::string& path);
};

} // namespace mc1vt
