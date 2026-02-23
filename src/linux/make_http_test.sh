#!/usr/bin/env bash
# make_http_test.sh — Quick HTTP/HTTPS browser-test build for mcaster1-encoder
#
# Builds ONLY the HTTP admin server + minimal stubs (no audio, no codec deps).
# Dependencies: g++, libssl-dev, libpthread
#
# Usage:
#   cd /path/to/Mcaster1DSPEncoder
#   bash src/linux/make_http_test.sh
#
# Produces: build/mcaster1-encoder-http-test
# Run:
#   ./build/mcaster1-encoder-http-test
#   Browse to: http://127.0.0.1:8330  (login: admin / hackme)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build"
OUT="${BUILD_DIR}/mcaster1-encoder-http-test"

CXX="${CXX:-g++}"
CXXFLAGS_EXTRA="${CXXFLAGS:-}"

mkdir -p "${BUILD_DIR}"

echo "── Mcaster1 DSP Encoder v5.0 — HTTP browser-test build ──"
echo "   Repo  : ${REPO_ROOT}"
echo "   Output: ${OUT}"
echo ""

# ── Detect OpenSSL ──────────────────────────────────────────────────────────
OPENSSL_INC=""
OPENSSL_LIB="-lssl -lcrypto"

if command -v pkg-config &>/dev/null && pkg-config --exists openssl 2>/dev/null; then
    OPENSSL_INC="$(pkg-config --cflags openssl)"
    OPENSSL_LIB="$(pkg-config --libs   openssl)"
    echo "   OpenSSL: $(pkg-config --modversion openssl) [pkg-config]"
elif [ -f /usr/include/openssl/ssl.h ]; then
    echo "   OpenSSL: found at /usr/include/openssl/"
else
    echo "ERROR: OpenSSL development headers not found."
    echo "  Install with:  sudo apt install libssl-dev"
    exit 1
fi

# ── Check httplib.h vendor header ───────────────────────────────────────────
HTTPLIB_H="${REPO_ROOT}/external/include/httplib.h"
if [ ! -f "${HTTPLIB_H}" ]; then
    echo ""
    echo "ERROR: Missing ${HTTPLIB_H}"
    echo "  Download with:"
    echo "  curl -L https://github.com/yhirose/cpp-httplib/raw/v0.18.1/httplib.h \\"
    echo "       -o external/include/httplib.h"
    exit 1
fi
echo "   httplib.h: found"

# ── Check nlohmann/json.hpp vendor header ───────────────────────────────────
JSON_H="${REPO_ROOT}/external/include/nlohmann/json.hpp"
if [ ! -f "${JSON_H}" ]; then
    echo ""
    echo "ERROR: Missing ${JSON_H}"
    echo "  Download with:"
    echo "  mkdir -p external/include/nlohmann"
    echo "  curl -L https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp \\"
    echo "       -o external/include/nlohmann/json.hpp"
    exit 1
fi
echo "   json.hpp : found"
echo ""

# ── Source files for HTTP-only test ─────────────────────────────────────────
SRCS=(
    "${REPO_ROOT}/src/linux/main_cli.cpp"
    "${REPO_ROOT}/src/linux/http_api.cpp"
    "${REPO_ROOT}/src/linux/config_stub.cpp"
)

# ── Include paths ────────────────────────────────────────────────────────────
# Use -idirafter for external/include so native system headers (pthread.h, etc.)
# take priority over Windows-specific compat headers in that directory.
INCLUDES=(
    "-I${REPO_ROOT}/src"
    "-I${REPO_ROOT}/src/libmcaster1dspencoder"
    "-idirafter${REPO_ROOT}/external/include"
)

# ── Compile flags ────────────────────────────────────────────────────────────
CXXFLAGS=(
    "-std=c++17"
    "-O2"
    "-Wall"
    "-Wno-unused-parameter"
    "-DCPPHTTPLIB_OPENSSL_SUPPORT"
    "-DMC1_HTTP_TEST_BUILD"
    ${OPENSSL_INC}
    ${INCLUDES[@]}
    ${CXXFLAGS_EXTRA}
)

# ── Link flags ───────────────────────────────────────────────────────────────
LDFLAGS=(
    ${OPENSSL_LIB}
    "-lpthread"
)

# ── Build ────────────────────────────────────────────────────────────────────
echo "Compiling…"
"${CXX}" "${CXXFLAGS[@]}" "${SRCS[@]}" "${LDFLAGS[@]}" -o "${OUT}"

echo ""
echo "── Build successful ─────────────────────────────────────────────────────"
echo ""
echo "  Binary : ${OUT}"
echo ""
echo "  HTTP test  (plain):"
echo "    ${OUT}"
echo "    Browse: http://127.0.0.1:8330"
echo "    Login : admin / hackme"
echo ""
echo "  HTTPS test (self-signed cert):"
echo "    First generate a cert:"
echo "    ${OUT} --ssl-gencert --ssl-gentype=selfsigned \\"
echo "             --subj=\"/C=US/ST=FL/CN=localhost\" \\"
echo "             --ssl-gencert-savepath=./certs"
echo "    Then run:"
echo "    ${OUT} --ssl-cert ./certs/server.crt \\"
echo "            --ssl-key  ./certs/server.key \\"
echo "            --https-port 8443"
echo "    Browse: https://127.0.0.1:8443"
echo ""
