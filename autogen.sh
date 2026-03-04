#!/usr/bin/env bash
# autogen.sh — Bootstrap autotools for Mcaster1 DSP Encoder (Linux)
#
# Run once after a fresh clone or after modifying configure.ac / Makefile.am.
# Requires: autoconf >= 2.69, automake >= 1.15, autoconf-archive, m4
#
# FIRST TIME SETUP — install all build + audio dependencies:
#   bash install-deps.sh
#
# Then bootstrap, configure, and build:
#   ./autogen.sh
#   ./configure [--prefix=/usr/local] [--with-webroot=/usr/share/mcaster1/web_ui]
#   make -j$(nproc)
#   sudo make install

set -euo pipefail

# macOS/Homebrew: ensure aclocal can find autoconf-archive m4 macros
if [ -d /opt/homebrew/share/aclocal ]; then
    export ACLOCAL_PATH="/opt/homebrew/share/aclocal${ACLOCAL_PATH:+:${ACLOCAL_PATH}}"
fi

echo "── Mcaster1 DSP Encoder — autotools bootstrap ──"

# ── Tool availability check ────────────────────────────────────────────────────
missing_tools=()

check_tool() {
    local tool="$1"
    local pkg="$2"
    if ! command -v "$tool" &>/dev/null; then
        echo "  MISSING: '$tool' (package: $pkg)"
        missing_tools+=("$tool")
    fi
}

check_tool aclocal    "automake"
check_tool autoconf   "autoconf"
check_tool automake   "automake"
check_tool m4         "m4"

# Check for autoconf-archive (provides AX_CXX_COMPILE_STDCXX etc.)
# Note: pipefail causes find to fail if any search path doesn't exist,
# so we check each directory individually.
found_ax_m4=no
for _d in "$(aclocal --print-ac-dir 2>/dev/null)" \
          /usr/share/aclocal /usr/local/share/aclocal \
          /opt/homebrew/share/aclocal; do
    if [ -d "$_d" ] && [ -f "$_d/ax_cxx_compile_stdcxx.m4" ]; then
        found_ax_m4=yes
        break
    fi
done
if [ "$found_ax_m4" = "no" ]; then
    echo "  MISSING: ax_cxx_compile_stdcxx.m4 (package: autoconf-archive)"
    missing_tools+=("autoconf-archive")
fi

if [[ ${#missing_tools[@]} -gt 0 ]]; then
    echo ""
    echo "  ERROR: Required build tools are missing."
    echo "  Run the dependency installer first:"
    echo ""
    echo "    bash install-deps.sh --build-only"
    echo ""
    exit 1
fi

if command -v autoreconf &>/dev/null; then
    # Preferred: autoreconf handles aclocal → autoheader → automake → autoconf
    # in the correct dependency order.
    echo "  autoreconf -fi …"
    autoreconf --force --install 2>&1 | grep -v "^$" | grep -v "^automake:" || true
else
    # Fallback: manual sequence
    echo "  aclocal…"
    aclocal

    echo "  autoheader…"
    autoheader

    echo "  automake --add-missing --copy…"
    automake --add-missing --copy 2>/dev/null || automake --add-missing

    echo "  autoconf…"
    autoconf
fi

echo ""
echo "── Done. Now run:"
echo ""
echo "   ./configure [--prefix=/usr/local] [--with-webroot=/usr/share/mcaster1/web_ui]"
echo "   make -j\$(nproc)"
echo "   sudo make install"
echo ""
echo "   Missing libraries? Run:  bash install-deps.sh"
echo ""
