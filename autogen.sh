#!/usr/bin/env bash
# autogen.sh — Bootstrap autotools for Mcaster1 DSP Encoder (Linux)
#
# Run once after a fresh clone or after modifying configure.ac / Makefile.am.
# Requires: autoconf >= 2.69, automake >= 1.15, m4
#
# Usage:
#   ./autogen.sh [--prefix=/opt/mcaster1]
#   ./configure [--prefix=/opt/mcaster1] [options...]
#   make -j$(nproc)
#   sudo make install

set -euo pipefail

echo "── Mcaster1 DSP Encoder — autotools bootstrap ──"

check_tool() {
    if ! command -v "$1" &>/dev/null; then
        echo "ERROR: '$1' not found."
        echo "  Install with:  sudo apt install $2"
        exit 1
    fi
}

check_tool aclocal   "automake"
check_tool autoconf  "autoconf"
check_tool automake  "automake"

echo "  aclocal…"
aclocal

echo "  automake --add-missing --copy…"
automake --add-missing --copy 2>/dev/null || automake --add-missing

echo "  autoconf…"
autoconf

echo ""
echo "── Done. Now run:"
echo ""
echo "   ./configure [--prefix=/usr/local] [--with-webroot=/usr/share/mcaster1/web_ui]"
echo "   make -j\$(nproc)"
echo "   sudo make install"
echo ""
echo "   Or for a quick HTTP test (no autotools needed):"
echo "   bash src/linux/make_http_test.sh"
echo ""
