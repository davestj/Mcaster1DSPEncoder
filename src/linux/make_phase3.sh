#!/usr/bin/env bash
# make_phase3.sh — Full Phase 3 build: audio encoding + streaming engine
#
# Includes: PortAudio, libmpg123, libavcodec, LAME, Vorbis, Opus, FLAC, TagLib
# Produces: build/mcaster1-encoder
#
# Prerequisites (Debian/Ubuntu):
#   sudo apt install \
#     libportaudio2 portaudio19-dev libmpg123-dev \
#     libavformat-dev libavcodec-dev libavutil-dev libswresample-dev \
#     libmp3lame-dev libvorbis-dev libogg-dev libopus-dev libopusenc-dev \
#     libflac-dev libtag1-dev libssl-dev
#
# Usage:
#   cd /path/to/Mcaster1DSPEncoder
#   bash src/linux/make_phase3.sh
#   ./build/mcaster1-encoder --http-port 8330

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build"
OUT="${BUILD_DIR}/mcaster1-encoder"

CXX="${CXX:-g++}"
CXXFLAGS_EXTRA="${CXXFLAGS:-}"

mkdir -p "${BUILD_DIR}"

echo "── Mcaster1 DSP Encoder v1.2.0 — Phase 3 Full Build ────────────────"
echo "   Repo  : ${REPO_ROOT}"
echo "   Output: ${OUT}"
echo ""

# ── Helper: require pkg-config package ──────────────────────────────────────
require_pkg() {
    local pkg="$1"
    local apt_hint="$2"
    if ! pkg-config --exists "${pkg}" 2>/dev/null; then
        echo "ERROR: pkg-config package '${pkg}' not found."
        echo "  Install: sudo apt install ${apt_hint}"
        exit 1
    fi
}

# ── OpenSSL (required) ───────────────────────────────────────────────────────
require_pkg "openssl" "libssl-dev"
OPENSSL_CFLAGS="$(pkg-config --cflags openssl)"
OPENSSL_LIBS="$(pkg-config --libs openssl)"
echo "   OpenSSL   : $(pkg-config --modversion openssl)"

# ── PortAudio (required for device capture) ──────────────────────────────────
PORTAUDIO_FLAGS=""
PORTAUDIO_LIBS=""
HAVE_PORTAUDIO=""
if pkg-config --exists portaudio-2.0 2>/dev/null; then
    PORTAUDIO_FLAGS="$(pkg-config --cflags portaudio-2.0) -DHAVE_PORTAUDIO"
    PORTAUDIO_LIBS="$(pkg-config --libs portaudio-2.0)"
    HAVE_PORTAUDIO=1
    echo "   PortAudio : $(pkg-config --modversion portaudio-2.0)"
else
    echo "   PortAudio : NOT FOUND (device capture disabled)"
fi

# ── libmpg123 ────────────────────────────────────────────────────────────────
MPG123_FLAGS=""
MPG123_LIBS=""
if pkg-config --exists libmpg123 2>/dev/null; then
    MPG123_FLAGS="$(pkg-config --cflags libmpg123) -DHAVE_MPG123"
    MPG123_LIBS="$(pkg-config --libs libmpg123)"
    echo "   libmpg123 : $(pkg-config --modversion libmpg123)"
else
    echo "   libmpg123 : NOT FOUND (MP3 file decode via avcodec)"
fi

# ── FFmpeg (libavformat/libavcodec/libswresample) ────────────────────────────
AV_FLAGS=""
AV_LIBS=""
if pkg-config --exists libavformat libavcodec libavutil libswresample 2>/dev/null; then
    AV_FLAGS="$(pkg-config --cflags libavformat libavcodec libavutil libswresample) -DHAVE_AVFORMAT"
    AV_LIBS="$(pkg-config --libs libavformat libavcodec libavutil libswresample)"
    echo "   FFmpeg    : $(pkg-config --modversion libavformat)"
else
    echo "   FFmpeg    : NOT FOUND (file decode limited to MP3)"
fi

# ── LAME ─────────────────────────────────────────────────────────────────────
LAME_FLAGS=""
LAME_LIBS=""
if pkg-config --exists lame 2>/dev/null; then
    LAME_FLAGS="$(pkg-config --cflags lame) -DHAVE_LAME"
    LAME_LIBS="$(pkg-config --libs lame)"
    echo "   LAME      : $(pkg-config --modversion lame)"
elif [ -f /usr/include/lame/lame.h ]; then
    LAME_FLAGS="-DHAVE_LAME"
    LAME_LIBS="-lmp3lame"
    echo "   LAME      : found (no pkg-config)"
else
    echo "   LAME      : NOT FOUND (MP3 encoding disabled)"
fi

# ── Ogg + Vorbis ─────────────────────────────────────────────────────────────
VORBIS_FLAGS=""
VORBIS_LIBS=""
if pkg-config --exists vorbisenc ogg vorbis 2>/dev/null; then
    VORBIS_FLAGS="$(pkg-config --cflags vorbisenc ogg vorbis) -DHAVE_VORBIS"
    VORBIS_LIBS="$(pkg-config --libs vorbisenc ogg vorbis)"
    echo "   Vorbis    : $(pkg-config --modversion vorbisenc)"
else
    echo "   Vorbis    : NOT FOUND (Ogg/Vorbis encoding disabled)"
fi

# ── Opus (libopusenc for Ogg/Opus muxing) ────────────────────────────────────
OPUS_FLAGS=""
OPUS_LIBS=""
if pkg-config --exists libopusenc 2>/dev/null; then
    OPUS_FLAGS="$(pkg-config --cflags libopusenc) -DHAVE_OPUS"
    OPUS_LIBS="$(pkg-config --libs libopusenc)"
    echo "   Opus      : $(pkg-config --modversion libopusenc)"
elif pkg-config --exists opus 2>/dev/null; then
    OPUS_FLAGS="$(pkg-config --cflags opus) -DHAVE_OPUS"
    OPUS_LIBS="$(pkg-config --libs opus)"
    echo "   Opus      : $(pkg-config --modversion opus) (raw, no container)"
else
    echo "   Opus      : NOT FOUND (Opus encoding disabled)"
fi

# ── FLAC ─────────────────────────────────────────────────────────────────────
FLAC_FLAGS=""
FLAC_LIBS=""
if pkg-config --exists flac 2>/dev/null; then
    FLAC_FLAGS="$(pkg-config --cflags flac) -DHAVE_FLAC"
    FLAC_LIBS="$(pkg-config --libs flac)"
    echo "   FLAC      : $(pkg-config --modversion flac)"
else
    echo "   FLAC      : NOT FOUND (FLAC encoding disabled)"
fi

# ── TagLib ───────────────────────────────────────────────────────────────────
TAGLIB_FLAGS=""
TAGLIB_LIBS=""
if pkg-config --exists taglib 2>/dev/null; then
    TAGLIB_FLAGS="$(pkg-config --cflags taglib) -DHAVE_TAGLIB"
    TAGLIB_LIBS="$(pkg-config --libs taglib)"
    echo "   TagLib    : $(pkg-config --modversion taglib)"
else
    echo "   TagLib    : NOT FOUND (metadata from filenames only)"
fi

echo ""

# ── Check vendor headers ─────────────────────────────────────────────────────
if [ ! -f "${REPO_ROOT}/external/include/httplib.h" ]; then
    echo "ERROR: Missing external/include/httplib.h"
    exit 1
fi
if [ ! -f "${REPO_ROOT}/external/include/nlohmann/json.hpp" ]; then
    echo "ERROR: Missing external/include/nlohmann/json.hpp"
    exit 1
fi

# ── Source files ─────────────────────────────────────────────────────────────
SRCS=(
    "${REPO_ROOT}/src/linux/main_cli.cpp"
    "${REPO_ROOT}/src/linux/http_api.cpp"
    "${REPO_ROOT}/src/linux/audio_pipeline.cpp"
    "${REPO_ROOT}/src/linux/encoder_slot.cpp"
    "${REPO_ROOT}/src/linux/audio_source.cpp"
    "${REPO_ROOT}/src/linux/file_source.cpp"
    "${REPO_ROOT}/src/linux/playlist_parser.cpp"
    "${REPO_ROOT}/src/linux/stream_client.cpp"
    "${REPO_ROOT}/src/linux/archive_writer.cpp"
    "${REPO_ROOT}/src/linux/config_stub.cpp"
)

# ── Include paths ─────────────────────────────────────────────────────────────
INCLUDES=(
    "-I${REPO_ROOT}/src"
    "-I${REPO_ROOT}/src/linux"
    "-I${REPO_ROOT}/src/libmcaster1dspencoder"
    "-idirafter${REPO_ROOT}/external/include"
)

# ── Compile flags ─────────────────────────────────────────────────────────────
# Note: library flag variables are unquoted to allow word-splitting into separate args
CXXFLAGS=(
    "-std=c++17"
    "-O2"
    "-Wall"
    "-Wno-unused-parameter"
    "-DCPPHTTPLIB_OPENSSL_SUPPORT"
    "${INCLUDES[@]}"
)
# Append flag strings with word-splitting (flags contain spaces like -I/path -DFOO)
read -ra _extra <<< "$OPENSSL_CFLAGS $PORTAUDIO_FLAGS $MPG123_FLAGS $AV_FLAGS $LAME_FLAGS $VORBIS_FLAGS $OPUS_FLAGS $FLAC_FLAGS $TAGLIB_FLAGS $CXXFLAGS_EXTRA"
CXXFLAGS+=("${_extra[@]}")

# ── Link flags ────────────────────────────────────────────────────────────────
LDFLAGS=()
read -ra _ldextra <<< "$OPENSSL_LIBS $PORTAUDIO_LIBS $MPG123_LIBS $AV_LIBS $LAME_LIBS $VORBIS_LIBS $OPUS_LIBS $FLAC_LIBS $TAGLIB_LIBS"
LDFLAGS+=("${_ldextra[@]}")
LDFLAGS+=("-lpthread" "-lyaml")

# ── Build ──────────────────────────────────────────────────────────────────────
echo "Compiling Phase 3 (this may take ~30s)…"
"${CXX}" "${CXXFLAGS[@]}" "${SRCS[@]}" "${LDFLAGS[@]}" -o "${OUT}"

echo ""
echo "── Build successful ──────────────────────────────────────────────────────"
echo ""
echo "  Binary : ${OUT}"
echo ""
echo "  Start HTTP admin server:"
echo "    ${OUT} --http-port 8330 --bind 0.0.0.0"
echo "    Browse: http://YOUR_IP:8330"
echo "    Login : admin / hackme"
echo ""
echo "  With HTTPS (requires prior --ssl-gencert):"
echo "    ${OUT} --http-port 8330 --https-port 8443 \\"
echo "            --ssl-cert ./certs/server.crt \\"
echo "            --ssl-key  ./certs/server.key \\"
echo "            --bind 0.0.0.0"
echo ""
echo "  API — list audio devices:"
echo "    curl -s -b 'mc1session=TOKEN' http://localhost:8330/api/v1/devices"
echo ""
