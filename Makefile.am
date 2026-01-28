# Makefile.am — Root (Mcaster1 DSP Encoder Linux)
#
# Autotools entry point.  All Linux build targets live under src/linux/.
# Windows targets are built by Visual Studio (.vcxproj) — not touched here.
#
# Usage:
#   ./autogen.sh
#   ./configure [--prefix=/usr/local] [--with-webroot=/usr/share/mcaster1/web_ui]
#   make -j$(nproc)
#   sudo make install

SUBDIRS = src/linux

EXTRA_DIST = \
    autogen.sh \
    install-deps.sh \
    README-LINUX.md \
    CLAUDE.md \
    INSTRUCTIONS.md \
    docs/index.html
