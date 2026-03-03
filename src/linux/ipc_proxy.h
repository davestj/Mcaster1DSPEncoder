/*
 * Mcaster1DSPEncoder — IPC Proxy (Admin → Encoder)
 * ipc_proxy.h
 *
 * We forward HTTP API requests from the admin binary to the encoder binary
 * running on localhost:8331. Uses httplib::Client internally.
 * Returns {"ok":false,"error":"Encoder offline"} when encoder is down.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "external/include/httplib.h"
#include <string>
#include <memory>
#include <atomic>

class IpcProxy {
public:
    IpcProxy() = default;

    /* We configure the encoder's IPC address */
    void set_encoder_addr(const std::string& host, int port) {
        host_ = host;
        port_ = port;
    }

    /* We set whether the encoder is known to be alive (updated by supervisor) */
    void set_encoder_alive(bool alive) { encoder_alive_.store(alive); }
    bool is_encoder_alive() const { return encoder_alive_.load(); }

    /* We forward a GET request to the encoder and return the response body.
     * Sets res.status appropriately. */
    void proxy_get(const httplib::Request& req, httplib::Response& res,
                   const std::string& path);

    /* We forward a POST request */
    void proxy_post(const httplib::Request& req, httplib::Response& res,
                    const std::string& path);

    /* We forward a PUT request */
    void proxy_put(const httplib::Request& req, httplib::Response& res,
                   const std::string& path);

    /* We forward a DELETE request */
    void proxy_delete(const httplib::Request& req, httplib::Response& res,
                      const std::string& path);

private:
    std::string host_ = "127.0.0.1";
    int         port_ = 8331;
    std::atomic<bool> encoder_alive_{false};

    /* We create a fresh httplib::Client per request (short-lived connections) */
    std::unique_ptr<httplib::Client> make_client();

    /* We set the offline error response */
    void set_offline_response(httplib::Response& res);

    /* We copy auth headers from the original request */
    httplib::Headers forward_headers(const httplib::Request& req);
};
