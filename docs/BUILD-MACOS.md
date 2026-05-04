# Building Mcaster1DSPEncoder on macOS

## Prerequisites

- **Xcode Command Line Tools**: `xcode-select --install`
- **Homebrew**: https://brew.sh

## Step 1: Install Dependencies

```bash
bash install-deps.sh
```

This installs all build tools and audio/codec libraries via Homebrew:
autoconf, automake, libtool, pkg-config, openssl@3, libyaml, portaudio,
mariadb-connector-c, lame, libvorbis, flac, opus, opusenc, mpg123,
ffmpeg, taglib, libsndfile, jack, fdk-aac (if available).

## Step 2: Set Up PKG_CONFIG_PATH

Add to your `~/.zshrc` (or `~/.bash_profile`):

```bash
export PKG_CONFIG_PATH="$(brew --prefix openssl@3)/lib/pkgconfig:$PKG_CONFIG_PATH"
export PKG_CONFIG_PATH="$(brew --prefix mariadb-connector-c)/lib/pkgconfig:$PKG_CONFIG_PATH"
```

Then reload: `source ~/.zshrc`

## Step 3: Build

```bash
./autogen.sh
./configure
make -j$(sysctl -n hw.ncpu)
```

The four binaries are built in `src/linux/`:
- `mcaster1-dsp-encoder-admin`
- `mcaster1-dsp-encoder`
- `mcaster1-voictune`
- `mcaster1-producer`

## Audio Backend

PortAudio automatically uses **CoreAudio** on macOS. No direct CoreAudio
code is needed. The `-framework CoreAudio -framework AudioUnit -framework CoreServices`
flags are added by the build system for any native macOS APIs used internally.

## Known Limitations

- **No inotify USB hotplug**: macOS does not have Linux's inotify filesystem
  watcher. The VoicTune USB monitor falls back to periodic PortAudio device
  re-enumeration (every 3 seconds) instead of instant hotplug detection.
- **No /proc filesystem**: System health metrics (CPU, memory, network) use
  macOS-native Mach/sysctl APIs instead of `/proc/stat`, `/proc/meminfo`, etc.
- **No ALSA**: macOS uses CoreAudio, not ALSA. The `/proc/asound/cards` check
  is skipped; the health API reports `coreaudio` as the available driver.
- **JACK is optional**: Most macOS users will not have JACK installed. The build
  continues without it; JACK audio routing features are disabled.
- **PHP-FPM**: If you need the web UI, install PHP via Homebrew:
  `brew install php` and configure php-fpm manually. The macOS build focuses
  on the C++ encoder binaries.
- **No `-lcrypt`/`-lrt`**: These Linux-specific libraries are not linked on macOS.
  The build system handles this automatically.

## fdk-aac (AAC Encoding)

If `brew install fdk-aac` is not available, build from source:

```bash
git clone https://github.com/mstorsjo/fdk-aac
cd fdk-aac && autoreconf -fi
./configure --prefix=/usr/local
make -j$(sysctl -n hw.ncpu) && sudo make install
```

## Troubleshooting

**`configure: error: OpenSSL >= 1.1.0 required`**
Ensure PKG_CONFIG_PATH includes the OpenSSL path (see Step 2).

**`AX_CXX_COMPILE_STDCXX not found`**
Install autoconf-archive: `brew install autoconf-archive`

**`mariadb not found`**
Ensure PKG_CONFIG_PATH includes the MariaDB connector path (see Step 2).

**Apple Silicon vs Intel**
Homebrew installs to `/opt/homebrew` on Apple Silicon (M1/M2/M3/M4) and
`/usr/local` on Intel Macs. The configure script checks both paths.
