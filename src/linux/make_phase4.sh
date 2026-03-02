#!/usr/bin/env bash
# make_phase4.sh — Full Phase 4 build: DSP chain + ICY2 + DNAS stats integration
#
# New in Phase 4 vs Phase 3:
#   • src/linux/dsp/eq.cpp          — 10-band parametric EQ (RBJ biquad)
#   • src/linux/dsp/agc.cpp         — AGC / feedforward compressor / limiter
#   • src/linux/dsp/crossfader.cpp  — Equal-power crossfader
#   • src/linux/dsp/dsp_chain.cpp   — Master DSP chain orchestrator
#   • ICY2 extended headers (Twitter, Facebook, Instagram, …) in StreamTarget
#   • Live DSP reconfiguration via PUT /api/v1/encoders/{slot}/dsp
#   • EQ preset API: POST /api/v1/encoders/{slot}/dsp/eq/preset
#   • DNAS stats proxy: GET /api/v1/dnas/stats
#
# Produces: build/mcaster1-encoder (replaces Phase 3 binary in-place)
#
# Prerequisites (Debian/Ubuntu):
#   sudo apt install \
#     libportaudio2 portaudio19-dev libmpg123-dev \
#     libavformat-dev libavcodec-dev libavutil-dev libswresample-dev \
#     libmp3lame-dev libvorbis-dev libogg-dev libopus-dev libopusenc-dev \
#     libflac-dev libtag1-dev libssl-dev libyaml-dev
#
# FDK-AAC (NOT in standard Debian repos — build from source):
#   git clone --depth=1 https://github.com/mstorsjo/fdk-aac.git /tmp/fdk-aac
#   cd /tmp/fdk-aac && autoreconf -fiv && ./configure --prefix=/usr/local
#   make -j$(nproc) && sudo make install && sudo ldconfig
#
# Usage:
#   cd /path/to/Mcaster1DSPEncoder
#   bash src/linux/make_phase4.sh
#   ./build/mcaster1-encoder --config src/linux/config/mcaster1_rock_yolo.yaml

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build"
OUT="${BUILD_DIR}/mcaster1-encoder"

CXX="${CXX:-g++}"
CXXFLAGS_EXTRA="${CXXFLAGS:-}"

mkdir -p "${BUILD_DIR}"

echo "── Mcaster1 DSP Encoder v1.3.0 — Phase 4 Full Build ────────────────"
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
if pkg-config --exists portaudio-2.0 2>/dev/null; then
    PORTAUDIO_FLAGS="$(pkg-config --cflags portaudio-2.0) -DHAVE_PORTAUDIO"
    PORTAUDIO_LIBS="$(pkg-config --libs portaudio-2.0)"
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

# ── FDK-AAC (libfdk-aac — AAC-LC / HE-AAC v1 / HE-AAC v2 / AAC-ELD) ─────────
# Built from source to /usr/local; check PKG_CONFIG_PATH for non-standard prefix
export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
FDKAAC_FLAGS=""
FDKAAC_LIBS=""
if pkg-config --exists fdk-aac 2>/dev/null; then
    FDKAAC_FLAGS="$(pkg-config --cflags fdk-aac) -DHAVE_FDK_AAC"
    FDKAAC_LIBS="$(pkg-config --libs fdk-aac)"
    echo "   FDK-AAC   : $(pkg-config --modversion fdk-aac) — AAC-LC/HE-AAC v1+v2/ELD"
elif [ -f /usr/local/include/fdk-aac/aacenc_lib.h ]; then
    FDKAAC_FLAGS="-I/usr/local/include -DHAVE_FDK_AAC"
    FDKAAC_LIBS="-L/usr/local/lib -lfdk-aac"
    echo "   FDK-AAC   : found at /usr/local (no pkg-config)"
else
    echo "   FDK-AAC   : NOT FOUND (AAC encoding disabled — build from source: github.com/mstorsjo/fdk-aac)"
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

# ── MariaDB / MySQL C client (required for DB slot loading) ──────────────────
MARIADB_FLAGS=""
MARIADB_LIBS=""
if pkg-config --exists libmariadb 2>/dev/null; then
    MARIADB_FLAGS="$(pkg-config --cflags libmariadb)"
    MARIADB_LIBS="$(pkg-config --libs libmariadb)"
    echo "   MariaDB   : $(pkg-config --modversion libmariadb)"
elif [ -f /usr/include/mysql/mysql.h ]; then
    MARIADB_FLAGS="-I/usr/include/mysql"
    MARIADB_LIBS="-lmariadb"
    echo "   MariaDB   : found at /usr/include/mysql (no pkg-config)"
else
    echo "ERROR: libmariadb-dev not found — encoder slot DB loading requires it"
    echo "  Install: sudo apt install libmariadb-dev"
    exit 1
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
    # Core engine
    "${REPO_ROOT}/src/linux/main_cli.cpp"
    "${REPO_ROOT}/src/linux/http_api.cpp"
    "${REPO_ROOT}/src/linux/fastcgi_client.cpp"
    "${REPO_ROOT}/src/linux/audio_pipeline.cpp"
    "${REPO_ROOT}/src/linux/encoder_slot.cpp"
    "${REPO_ROOT}/src/linux/audio_source.cpp"
    "${REPO_ROOT}/src/linux/file_source.cpp"
    "${REPO_ROOT}/src/linux/playlist_parser.cpp"
    "${REPO_ROOT}/src/linux/stream_client.cpp"
    "${REPO_ROOT}/src/linux/archive_writer.cpp"
    "${REPO_ROOT}/src/linux/config_loader.cpp"   # YAML → gAdminConfig + pipeline slots

    # Phase 4: DSP chain (EQ → AGC → Crossfader)
    "${REPO_ROOT}/src/linux/dsp/eq.cpp"
    "${REPO_ROOT}/src/linux/dsp/agc.cpp"
    "${REPO_ROOT}/src/linux/dsp/crossfader.cpp"
    "${REPO_ROOT}/src/linux/dsp/dsp_chain.cpp"
)

# ── Include paths ─────────────────────────────────────────────────────────────
INCLUDES=(
    "-I${REPO_ROOT}/src"
    "-I${REPO_ROOT}/src/linux"
    "-I${REPO_ROOT}/src/libmcaster1dspencoder"
    "-idirafter${REPO_ROOT}/external/include"
)

# ── Compile flags ─────────────────────────────────────────────────────────────
CXXFLAGS=(
    "-std=c++17"
    "-O2"
    "-Wall"
    "-Wno-unused-parameter"
    "-DCPPHTTPLIB_OPENSSL_SUPPORT"
    "${INCLUDES[@]}"
)
read -ra _extra <<< "$OPENSSL_CFLAGS $PORTAUDIO_FLAGS $MPG123_FLAGS $AV_FLAGS \
$LAME_FLAGS $VORBIS_FLAGS $OPUS_FLAGS $FLAC_FLAGS $FDKAAC_FLAGS $TAGLIB_FLAGS \
$MARIADB_FLAGS $CXXFLAGS_EXTRA"
CXXFLAGS+=("${_extra[@]}")

# ── Link flags ────────────────────────────────────────────────────────────────
LDFLAGS=()
read -ra _ldextra <<< "$OPENSSL_LIBS $PORTAUDIO_LIBS $MPG123_LIBS $AV_LIBS \
$LAME_LIBS $VORBIS_LIBS $OPUS_LIBS $FLAC_LIBS $FDKAAC_LIBS $TAGLIB_LIBS \
$MARIADB_LIBS"
LDFLAGS+=("${_ldextra[@]}")
LDFLAGS+=("-lpthread" "-lyaml" "-lm" "-rdynamic")

# ── Build ─────────────────────────────────────────────────────────────────────
echo "Compiling Phase 4 (this may take ~30s)…"
"${CXX}" "${CXXFLAGS[@]}" "${SRCS[@]}" "${LDFLAGS[@]}" -o "${OUT}"

echo ""
echo "── Build successful ──────────────────────────────────────────────────────"
echo ""
echo "  Binary  : ${OUT}"
echo "  Version : Mcaster1DSPEncoder v1.3.0 — Phase 4"
echo ""
echo "  Quick start — Mcaster1 Rock & Roll Yolo! test station:"
echo "    ${OUT} --config src/linux/config/mcaster1_rock_yolo.yaml"
echo "    Browse: http://YOUR_IP:8330   Login: djpulse / hackme"
echo ""
echo "  DSP API examples:"
echo "    # List DSP config for slot 1"
echo "    curl -sb 'mc1session=TOKEN' http://localhost:8330/api/v1/encoders/1/dsp"
echo ""
echo "    # Enable classic rock EQ preset on slot 1"
echo "    curl -sXPOST -b 'mc1session=TOKEN' \\"
echo "         -H 'Content-Type: application/json' \\"
echo "         -d '{\"preset\":\"classic_rock\"}' \\"
echo "         http://localhost:8330/api/v1/encoders/1/dsp/eq/preset"
echo ""
echo "    # Live DSP update (enable AGC, switch to broadcast EQ)"
echo "    curl -sXPUT -b 'mc1session=TOKEN' \\"
echo "         -H 'Content-Type: application/json' \\"
echo "         -d '{\"agc_enabled\":true,\"eq_preset\":\"broadcast\"}' \\"
echo "         http://localhost:8330/api/v1/encoders/1/dsp"
echo ""
echo "    # Fetch live DNAS stats (proxied from dnas.mcaster1.com:9443)"
echo "    curl -sb 'mc1session=TOKEN' http://localhost:8330/api/v1/dnas/stats"
echo ""
echo "  Available EQ presets:"
echo "    flat · classic_rock · country · modern_rock · broadcast · spoken_word"
echo ""
