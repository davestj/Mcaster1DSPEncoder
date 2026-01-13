// http_api.h — Embedded HTTP/HTTPS admin server for mcaster1-encoder (Linux)
//
// Provides:
//   http_api_start()   — spawn listener threads for all gAdminConfig sockets
//   http_api_stop()    — gracefully stop all listeners
//   http_api_gencert() — SSL self-signed cert / CSR generation mode
//
// Thread model: one thread per configured listener socket.
// Sessions:     in-memory token map; cookie mc1session + X-API-Token header.
// Auth:         gAdminConfig.admin_username / admin_password (plaintext or
//               sha256:<hex>), with optional htpasswd file override.

#pragma once
#ifndef MCASTER1_HTTP_API_H
#define MCASTER1_HTTP_API_H

#include <string>

// Start all admin HTTP/HTTPS listener threads (non-blocking — returns immediately).
// webroot — filesystem path to the web_ui directory containing HTML/CSS/JS.
void http_api_start(const std::string& webroot);

// Signal all listener servers to stop and join their threads.
void http_api_stop();

// SSL certificate generation mode.
// Returns 0 on success, 1 on error (program should exit with this code).
//   gentype    — "selfsigned" or "csr"
//   subj       — X.509 subject string e.g. "/C=US/ST=FL/CN=host"
//   savepath   — output directory (created if absent)
//   keysize    — RSA key bits (2048 or 4096)
//   days       — validity days (selfsigned only)
//   basename   — output filename base ("server" → server.crt / server.key)
//   config     — YAML config path to patch; NULL = skip patching
int http_api_gencert(const char* gentype,
                     const char* subj,
                     const char* savepath,
                     int         keysize,
                     int         days,
                     const char* basename,
                     const char* config);

#endif /* MCASTER1_HTTP_API_H */
