/*
 * Mcaster1DSPEncoder — IPC Proxy Implementation
 * ipc_proxy.cpp
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ipc_proxy.h"
#include "mc1_logger.h"

std::unique_ptr<httplib::Client> IpcProxy::make_client() {
    auto cli = std::make_unique<httplib::Client>(host_, port_);
    cli->set_connection_timeout(2);
    cli->set_read_timeout(10);
    cli->set_write_timeout(5);
    return cli;
}

void IpcProxy::set_offline_response(httplib::Response& res) {
    res.status = 503;
    res.set_content(
        R"({"ok":false,"error":"Encoder process offline — restarting...","encoder_offline":true})",
        "application/json");
}

httplib::Headers IpcProxy::forward_headers(const httplib::Request& req) {
    httplib::Headers hdrs;
    /* We forward Content-Type and any auth-related headers */
    if (req.has_header("Content-Type")) {
        hdrs.emplace("Content-Type", req.get_header_value("Content-Type"));
    }
    /* We pass along the original remote addr for logging */
    hdrs.emplace("X-Forwarded-For", req.remote_addr);
    return hdrs;
}

void IpcProxy::proxy_get(const httplib::Request& req, httplib::Response& res,
                         const std::string& path) {
    if (!encoder_alive_.load()) {
        set_offline_response(res);
        return;
    }

    auto cli = make_client();
    auto result = cli->Get(path, forward_headers(req));

    if (result) {
        res.status = result->status;
        res.set_content(result->body, result->get_header_value("Content-Type"));
    } else {
        MC1_WARN("[IPC] GET " + path + " failed — encoder unreachable");
        encoder_alive_.store(false);
        set_offline_response(res);
    }
}

void IpcProxy::proxy_post(const httplib::Request& req, httplib::Response& res,
                          const std::string& path) {
    if (!encoder_alive_.load()) {
        set_offline_response(res);
        return;
    }

    auto cli = make_client();
    auto result = cli->Post(path, forward_headers(req), req.body,
                            req.get_header_value("Content-Type"));

    if (result) {
        res.status = result->status;
        res.set_content(result->body, result->get_header_value("Content-Type"));
    } else {
        MC1_WARN("[IPC] POST " + path + " failed — encoder unreachable");
        encoder_alive_.store(false);
        set_offline_response(res);
    }
}

void IpcProxy::proxy_put(const httplib::Request& req, httplib::Response& res,
                         const std::string& path) {
    if (!encoder_alive_.load()) {
        set_offline_response(res);
        return;
    }

    auto cli = make_client();
    auto result = cli->Put(path, forward_headers(req), req.body,
                           req.get_header_value("Content-Type"));

    if (result) {
        res.status = result->status;
        res.set_content(result->body, result->get_header_value("Content-Type"));
    } else {
        MC1_WARN("[IPC] PUT " + path + " failed — encoder unreachable");
        encoder_alive_.store(false);
        set_offline_response(res);
    }
}

void IpcProxy::proxy_delete(const httplib::Request& req, httplib::Response& res,
                            const std::string& path) {
    if (!encoder_alive_.load()) {
        set_offline_response(res);
        return;
    }

    auto cli = make_client();
    auto result = cli->Delete(path, forward_headers(req));

    if (result) {
        res.status = result->status;
        res.set_content(result->body, result->get_header_value("Content-Type"));
    } else {
        MC1_WARN("[IPC] DELETE " + path + " failed — encoder unreachable");
        encoder_alive_.store(false);
        set_offline_response(res);
    }
}
