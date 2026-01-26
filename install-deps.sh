#!/usr/bin/env bash
# install-deps.sh — Install build + runtime dependencies for mcaster1-encoder
#
# Detects the OS and uses the appropriate package manager.
# Supports: Debian/Ubuntu (apt), RHEL/Fedora/Rocky (dnf/yum), macOS (brew)
#
# Usage:
#   bash install-deps.sh             # Install all deps (build tools + audio/codec libs)
#   bash install-deps.sh --build-only  # Install only build tools (autotools, compilers)
#
# Note: fdk-aac is NOT in standard repos. After running this script, build it manually:
#   git clone https://github.com/mstorsjo/fdk-aac && cd fdk-aac
#   autoreconf -fi && ./configure && make -j$(nproc) && sudo make install
#
set -euo pipefail

BUILD_ONLY=0
for arg in "$@"; do
    [[ "$arg" == "--build-only" ]] && BUILD_ONLY=1
done

# ── Detect OS / package manager ────────────────────────────────────────────────
OS=""
PKG_MGR=""

if [[ "$(uname)" == "Darwin" ]]; then
    OS="macos"
    if command -v brew &>/dev/null; then
        PKG_MGR="brew"
    else
        echo "ERROR: Homebrew not found. Install from https://brew.sh first."
        exit 1
    fi
elif [[ -f /etc/os-release ]]; then
    . /etc/os-release
    case "${ID:-}" in
        ubuntu|debian|linuxmint|pop|elementary)
            OS="debian"
            PKG_MGR="apt"
            ;;
        fedora)
            OS="fedora"
            PKG_MGR="dnf"
            ;;
        rhel|centos|rocky|almalinux|ol)
            OS="rhel"
            PKG_MGR=$(command -v dnf &>/dev/null && echo "dnf" || echo "yum")
            ;;
        opensuse*|sles)
            OS="suse"
            PKG_MGR="zypper"
            ;;
        arch|manjaro)
            OS="arch"
            PKG_MGR="pacman"
            ;;
        *)
            echo "WARNING: Unknown distro '${ID:-}'. Attempting Debian-style install..."
            OS="debian"
            PKG_MGR="apt"
            ;;
    esac
else
    echo "ERROR: Cannot detect OS (/etc/os-release not found)."
    exit 1
fi

echo ""
echo "── Mcaster1DSPEncoder dependency installer ──────────────────────────────────"
echo "   OS          : ${OS}"
echo "   Package mgr : ${PKG_MGR}"
echo "   Mode        : $([ $BUILD_ONLY -eq 1 ] && echo 'build tools only' || echo 'full (build + audio/codec)')"
echo ""

# ── Package lists ──────────────────────────────────────────────────────────────
case "$PKG_MGR" in

# ── apt (Debian / Ubuntu) ──────────────────────────────────────────────────────
apt)
    BUILD_DEPS=(
        g++ gcc pkg-config make
        autoconf automake autoconf-archive m4 libtool
    )

    AUDIO_DEPS=(
        libssl-dev
        libyaml-dev
        libmariadb-dev
        portaudio19-dev
        libmp3lame-dev
        libvorbis-dev libogg-dev
        libflac-dev
        libopus-dev libopusenc-dev
        libmpg123-dev
        libavformat-dev libavcodec-dev libavutil-dev libswresample-dev
        libtag1-dev
    )

    PHP_DEPS=(
        php8.2-fpm php8.2-mysql
        php8.2-curl php8.2-mbstring
    )

    echo "  Updating apt cache..."
    sudo apt-get update -qq

    echo "  Installing build tools..."
    sudo apt-get install -y "${BUILD_DEPS[@]}"

    if [[ $BUILD_ONLY -eq 0 ]]; then
        echo "  Installing audio + codec libraries..."
        sudo apt-get install -y "${AUDIO_DEPS[@]}" || {
            echo "  WARNING: Some packages may not be available. Continuing..."
            sudo apt-get install -y "${AUDIO_DEPS[@]}" 2>/dev/null || true
        }

        echo "  Installing PHP-FPM + extensions..."
        # Try PHP 8.2 first, fall back to whatever is available
        sudo apt-get install -y "${PHP_DEPS[@]}" 2>/dev/null || \
        sudo apt-get install -y php-fpm php-mysql php-curl php-mbstring || \
        echo "  WARNING: PHP-FPM install failed — web UI will not function."
    fi
    ;;

# ── dnf (Fedora / RHEL 8+) ────────────────────────────────────────────────────
dnf)
    BUILD_DEPS=(
        gcc gcc-c++ pkgconfig make
        autoconf automake m4 libtool
    )

    AUDIO_DEPS=(
        openssl-devel
        libyaml-devel
        mariadb-connector-c-devel
        portaudio-devel
        lame-devel
        libvorbis-devel libogg-devel
        flac-devel
        opus-devel
        mpg123-devel
        ffmpeg-free-devel
        taglib-devel
    )

    PHP_DEPS=(
        php-fpm php-mysqlnd php-curl php-mbstring
    )

    echo "  Installing build tools..."
    sudo dnf install -y "${BUILD_DEPS[@]}"

    # autoconf-archive is in EPEL on RHEL
    if [[ "$OS" == "rhel" ]]; then
        sudo dnf install -y epel-release 2>/dev/null || true
    fi
    sudo dnf install -y autoconf-archive 2>/dev/null || \
        echo "  WARNING: autoconf-archive not found — AX_CXX_COMPILE_STDCXX may fail."

    if [[ $BUILD_ONLY -eq 0 ]]; then
        echo "  Installing audio + codec libraries..."
        sudo dnf install -y "${AUDIO_DEPS[@]}" 2>/dev/null || \
            echo "  WARNING: Some packages unavailable. Enable RPM Fusion for codec libs."

        echo "  Installing PHP-FPM + extensions..."
        sudo dnf install -y "${PHP_DEPS[@]}" 2>/dev/null || \
            echo "  WARNING: PHP-FPM install failed."
    fi
    ;;

# ── yum (RHEL 7 / CentOS 7) ───────────────────────────────────────────────────
yum)
    BUILD_DEPS=(
        gcc gcc-c++ pkgconfig make
        autoconf automake m4 libtool
    )

    echo "  Installing build tools..."
    sudo yum install -y "${BUILD_DEPS[@]}"
    sudo yum install -y epel-release 2>/dev/null && \
    sudo yum install -y autoconf-archive 2>/dev/null || \
        echo "  WARNING: autoconf-archive not found — install manually."

    if [[ $BUILD_ONLY -eq 0 ]]; then
        sudo yum install -y openssl-devel libyaml-devel mariadb-devel \
            portaudio-devel lame-devel libvorbis-devel libogg-devel \
            flac-devel opus-devel mpg123-devel taglib-devel 2>/dev/null || \
            echo "  WARNING: Some packages unavailable on CentOS 7."
    fi
    ;;

# ── zypper (openSUSE) ──────────────────────────────────────────────────────────
zypper)
    BUILD_DEPS=(
        gcc gcc-c++ pkg-config make
        autoconf automake m4 libtool autoconf-archive
    )

    AUDIO_DEPS=(
        libopenssl-devel
        libyaml-devel
        libmariadb-devel
        portaudio-devel
        libmp3lame-devel
        libvorbis-devel libogg-devel
        flac-devel
        opus-devel
        mpg123-devel
        ffmpeg-4-libavformat-devel ffmpeg-4-libswresample-devel
        taglib-devel
    )

    echo "  Installing build tools..."
    sudo zypper install -y "${BUILD_DEPS[@]}"

    if [[ $BUILD_ONLY -eq 0 ]]; then
        echo "  Installing audio + codec libraries..."
        sudo zypper install -y "${AUDIO_DEPS[@]}" 2>/dev/null || \
            echo "  WARNING: Some packages unavailable."
    fi
    ;;

# ── pacman (Arch / Manjaro) ───────────────────────────────────────────────────
pacman)
    BUILD_DEPS=(
        gcc base-devel autoconf automake m4 libtool
    )

    AUDIO_DEPS=(
        openssl libyaml mariadb-libs portaudio
        lame libvorbis libogg flac opus opusfile
        mpg123 ffmpeg taglib
    )

    echo "  Installing build tools..."
    sudo pacman -Sy --noconfirm "${BUILD_DEPS[@]}"

    # autoconf-archive from AUR or community
    sudo pacman -Sy --noconfirm autoconf-archive 2>/dev/null || \
        echo "  WARNING: autoconf-archive not found — install from AUR: yay -S autoconf-archive"

    if [[ $BUILD_ONLY -eq 0 ]]; then
        echo "  Installing audio + codec libraries..."
        sudo pacman -Sy --noconfirm "${AUDIO_DEPS[@]}" || \
            echo "  WARNING: Some packages unavailable."
    fi
    ;;

# ── brew (macOS) ──────────────────────────────────────────────────────────────
brew)
    BUILD_DEPS=(
        autoconf automake m4 libtool pkg-config autoconf-archive
    )

    AUDIO_DEPS=(
        openssl@3 libyaml mariadb-connector-c portaudio
        lame libvorbis libogg flac opus opusfile
        mpg123 ffmpeg taglib
    )

    echo "  Installing build tools..."
    brew install "${BUILD_DEPS[@]}"

    if [[ $BUILD_ONLY -eq 0 ]]; then
        echo "  Installing audio + codec libraries..."
        brew install "${AUDIO_DEPS[@]}"

        # Ensure PKG_CONFIG_PATH is set for Homebrew-installed libs
        echo ""
        echo "  NOTE (macOS): Add these lines to ~/.zshrc or ~/.bash_profile:"
        echo "    export PKG_CONFIG_PATH=\"\$(brew --prefix openssl@3)/lib/pkgconfig:\$PKG_CONFIG_PATH\""
        echo "    export PKG_CONFIG_PATH=\"\$(brew --prefix mariadb-connector-c)/lib/pkgconfig:\$PKG_CONFIG_PATH\""
    fi
    ;;

*)
    echo "ERROR: Unsupported package manager '$PKG_MGR'."
    exit 1
    ;;
esac

echo ""
echo "── Dependency install complete ───────────────────────────────────────────────"
echo ""
if [[ $BUILD_ONLY -eq 0 ]]; then
    echo "  NOTE: fdk-aac (AAC encoding) is NOT in standard repos."
    echo "  To enable AAC, build from source:"
    echo ""
    echo "    git clone https://github.com/mstorsjo/fdk-aac"
    echo "    cd fdk-aac && autoreconf -fi"
    echo "    ./configure --prefix=/usr/local"
    echo "    make -j\$(nproc) && sudo make install"
    echo "    sudo ldconfig"
    echo ""
fi
echo "  Next steps:"
echo "    ./autogen.sh"
echo "    ./configure [--prefix=/usr/local] [--with-webroot=/usr/share/mcaster1/web_ui]"
echo "    make -j\$(nproc)"
echo ""
