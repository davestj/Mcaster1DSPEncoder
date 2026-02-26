# CLAUDE.md — Linux Source Context (src/linux/)

**Maintainer:** Dave St. John <davestj@gmail.com>
**Source root:** `src/linux/` within `/var/www/mcaster1.com/Mcaster1DSPEncoder/`
**Branch:** `linux-dev`
**Phase:** L5.5 complete (v1.4.5), L6 (Streaming Server Relay Monitor) planned

For mysql connection work from the command line, we use
mysql --defaults-extra-file=~/.my.cnf -e "SHOW DATABASES LIKE %yp%";

---

## What Lives Here

This directory contains ALL Linux-specific C++ source code and the PHP web UI. Nothing here is shared with the Windows build in `src/`. The Windows build lives in `src/` (project root), maintained separately in VS2022. Do not commingle the two.

```
src/linux/
├── http_api.cpp          ← HTTP/HTTPS admin server, route registration, FastCGI bridge, all /api/v1/ endpoints
├── main_cli.cpp          ← main(), CLI arg parsing (--config, --log-level, -v, --ssl-gencert)
├── config_loader.cpp     ← YAML → gAdminConfig + gSlotConfigs (libyaml-cpp)
├── config_loader.h
├── mc1_logger.h          ← Mc1Logger singleton: access.log, error.log, encoder.log, api.log
├── audio_pipeline.h/cpp  ← AudioPipeline: owns all EncoderSlots, start/stop coordination
├── encoder_slot.h/cpp    ← EncoderSlot: per-slot state machine (IDLE→LIVE→RECONNECTING)
├── stream_client.h/cpp   ← ICY/SOURCE protocol client → Icecast2 / Shoutcast / DNAS
├── file_source.h/cpp     ← libavcodec + libmpg123 file decode with real-time pacing
├── audio_source.h/cpp    ← PortAudio live device capture
├── playlist_parser.h/cpp ← M3U / PLS / XSPF / TXT playlist parser
├── fastcgi_client.h/cpp  ← Binary FastCGI/1.0 AF_UNIX socket client (→ php-fpm)
├── archive_writer.h/cpp  ← Simultaneous WAV + MP3 broadcast archive writer
├── dsp/
│   ├── eq.h/cpp          ← 10-band parametric EQ (RBJ biquad IIR filters)
│   ├── agc.h/cpp         ← AGC / compressor / hard limiter
│   ├── crossfader.h/cpp  ← Equal-power crossfader between tracks
│   └── dsp_chain.h/cpp   ← EQ → AGC pipeline orchestrator
├── external/
│   └── include/          ← Linux-only vendor headers (header-only libs, no system conflict)
│       ├── httplib.h     ← cpp-httplib v0.18 (copied from root external/include/)
│       └── nlohmann/
│           └── json.hpp  ← nlohmann/json v3.11
├── config/
│   └── mcaster1_rock_yolo.yaml ← Test station config (3 slots → dnas.mcaster1.com:9443)
├── web_ui/               ← PHP frontend served via FastCGI
│   ├── login.html        ← Pre-auth login (plain HTML, no PHP)
│   ├── style.css         ← Dark navy/teal theme
│   ├── dashboard.php     ← Live encoder cards + rAF progress bars + Chart.js bandwidth
│   ├── encoders.php      ← Per-slot DSP panel + live stats + SLEEP/Wake state
│   ├── edit_encoders.php ← Full 6-tab per-slot encoder config editor
│   ├── media.php         ← Track library + categories + folder scan + playlist files
│   ├── mediaplayer.php   ← Embedded browser audio player with DB-backed queue
│   ├── mediaplayerpro.php ← Standalone WMP-style 3-column popup player (NEW L5.5)
│   ├── playlists.php     ← DB-backed playlist CRUD + 4-step generation wizard
│   ├── metrics.php       ← Chart.js listener analytics
│   ├── profile.php       ← User profile (display_name, email, password change)
│   ├── settings.php      ← Server info + encoder config table + DB admin
│   └── app/
│       ├── inc/
│       │   ├── auth.php                          ← mc1_is_authed() C++ session gate
│       │   ├── user_auth.php                     ← MySQL PHP session (mc1_current_user)
│       │   ├── db.php                            ← mc1_db() PDO singleton + helpers
│       │   ├── traits.db.class.php               ← Mc1Db PDO trait (use this for all DB work)
│       │   ├── mc1_config.php                    ← MC1_AUDIO_ROOT, MC1_PLAYLIST_DIR, etc.
│       │   ├── header.php                        ← HTML head, sidebar nav, Chart.js flag
│       │   ├── footer.php                        ← Toasts, mc1Api(), mc1State, session bridge
│       │   ├── logger.php                        ← mc1_log(), mc1_log_request(), mc1_api_respond()
│       │   ├── edit.encoders.class.php           ← EditEncoders: load/all/validate/save/delete
│       │   ├── playlist.builder.algorithm.class.php ← 8 playlist generation algorithms
│       │   ├── playlist.manager.pro.class.php    ← PlaylistManagerPro: generate(), preview()
│       │   ├── profile.usr.class.php             ← ProfileUser: get/update/change_password
│       │   ├── metrics.serverstats.class.php     ← Mc1MetricsServerStats queries
│       │   ├── media.lib.player.php              ← Player queue helpers
│       │   └── icons.php                         ← SVG icon helper functions
│       └── api/
│           ├── tracks.php         ← Track CRUD + scan + browse + category management
│           ├── player.php         ← Queue CRUD (add, list, remove, clear, move_top)
│           ├── audio.php          ← HTTP Range (206) audio streaming
│           ├── artwork.php        ← Cover art serving by hash
│           ├── metrics.php        ← Listener analytics queries
│           ├── playlists.php      ← Playlist CRUD + wizard + load-to-slot
│           ├── encoders.php       ← DB-side encoder admin + user management
│           ├── servers.php        ← streaming_servers CRUD + stat proxy
│           ├── serverstats.php    ← Server statistics queries
│           ├── profile.php        ← User profile get/update/change_password
│           └── auth.php           ← auto_login, whoami, PHP session bridge
├── Makefile.am           ← Autotools build rules (canonical Linux build)
├── make_phase4.sh        ← Legacy monolithic g++ build script
├── make_phase3.sh        ← Previous phase build
└── make_phase2.sh        ← HTTP-only test build (no audio)
```

---

## Canonical Build (Autotools)

The canonical Linux build chain is autotools. Run these from the project root (`Mcaster1DSPEncoder/`):

```bash
# First time on a new machine: install all dependencies
bash install-deps.sh

# Bootstrap autotools (run after cloning or after changes to configure.ac / Makefile.am)
./autogen.sh

# Configure (detects codecs, sets AM_CONDITIONALs, writes config.h)
./configure

# Build
make -j$(nproc)
```

The binary is built at `src/linux/mcaster1-encoder` by autotools, then we copy to `build/`:

```bash
cp src/linux/mcaster1-encoder build/mcaster1-encoder
```

Or use the legacy script which does this all in one shot:

```bash
bash src/linux/make_phase4.sh
```

---

## Makefile.am: Critical Rules

### 1. Linux-only vendor include path

We use `-I$(srcdir)/external/include` which resolves to `src/linux/external/include`. This directory contains ONLY httplib.h and nlohmann/json.hpp. It does NOT contain Windows-specific headers (no pthread.h, no windows.h shims).

The project-root `external/include/` contains Windows vendor headers including pthreads-win32 (`pthread.h`). If that directory were added to the Linux include path with `-I`, it would shadow `/usr/include/pthread.h` and break compilation. It must never appear in `AM_CPPFLAGS` for the Linux build.

### 2. Explicit -DHAVE_XXX defines are mandatory

Source files use `#ifdef HAVE_LAME`, `#ifdef HAVE_OPUS`, etc. but do NOT include `config.h`. Autotools writes these defines to `config.h` via `AC_DEFINE`, but since source files never include it, those defines are invisible at compile time.

The `src/linux/Makefile.am` explicitly adds each define inside the corresponding `if HAVE_XXX` conditional block:

```makefile
if HAVE_LAME
AM_CPPFLAGS += $(LAME_CFLAGS) -DHAVE_LAME
mcaster1_encoder_LDADD += $(LAME_LIBS)
endif

if HAVE_OPUS
AM_CPPFLAGS += $(OPUS_CFLAGS) -DHAVE_OPUS
mcaster1_encoder_LDADD += $(OPUS_LIBS)
endif
```

If you ever remove these `-DHAVE_XXX` flags, the encoder will build but silently skip codecs at runtime with "LAME not compiled in" / "Opus not compiled in" etc.

### 3. -rdynamic linker flag

```makefile
AM_LDFLAGS = -rdynamic
```

This is required for `backtrace_symbols()` to resolve function names in stack traces written to `error.log`.

### 4. No Windows source files

The following files from the Windows build in `src/` must NEVER appear in `mcaster1_encoder_SOURCES`:
- `config_stub.cpp`
- `libmcaster1dspencoder.cpp`
- `Socket.cpp`
- `log.cpp`
- `podcast_rss_gen.cpp`
- `audio_wasapi.cpp`
- `audio_directsound.cpp`
- `win_service.cpp`
- `win_tray.cpp`

---

## EncoderSlot: Key C++ State

### first_connect_done_ atomic (Phase L5 fix)

`StreamClient` fires the `CONNECTED` callback on both initial TCP connect AND watchdog reconnects. We must not re-push ICY metadata on the first connect (the track-start callback will do it). We only re-push on reconnects.

Guard in `encoder_slot.h`:

```cpp
std::atomic<bool> first_connect_done_{false};
```

Reset in `start()`:

```cpp
stopping_.store(false);
++meta_gen_;
first_connect_done_.store(false);
```

CONNECTED callback in `encoder_slot.cpp`:

```cpp
stream_client_->set_state_callback([this](StreamClient::State cs) {
    if (cs == StreamClient::State::CONNECTED) {
        set_state(State::LIVE);
        const bool is_reconnect = first_connect_done_.exchange(true);
        if (is_reconnect) {
            std::string ttl, art;
            { std::lock_guard<std::mutex> lk(mtx_); ttl = current_title_; art = current_artist_; }
            if (!ttl.empty()) {
                auto sc = stream_client_;
                std::thread([sc, ttl, art]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    if (sc) sc->send_admin_metadata(ttl, art);
                }).detach();
            }
        }
    }
    if (cs == StreamClient::State::RECONNECTING) set_state(State::RECONNECTING);
});
```

Do not replace this with `source_->is_running()` as the guard. The source IS running by the time the async TCP connect fires CONNECTED.

### meta_gen_ atomic (stale thread guard)

Detached metadata push threads use `meta_gen_` to detect if the slot was stopped and restarted between when the thread was spawned and when the delay expires:

```cpp
auto sc  = stream_client_;
auto gen = meta_gen_.load();
std::thread([this, sc, ttl, art, delay_ms, gen]() {
    if (delay_ms > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    if (sc && !stopping_.load() && meta_gen_.load() == gen)
        sc->send_admin_metadata(ttl, art);
}).detach();
```

`meta_gen_` is incremented at the top of `start()`, invalidating any threads launched during a previous start/stop cycle.

---

## FileSource: Real-Time Pacing

`file_source.cpp` uses real-time pacing in the decode loop so audio frames are produced at the correct rate for streaming (not as fast as CPU allows, which would overflow the stream client buffer):

```cpp
// We pace delivery to match the stream's real-time rate
auto expected_us = (int64_t)pace_frames * 1000000LL / target_sr_;
auto elapsed_us  = std::chrono::duration_cast<std::chrono::microseconds>(
    std::chrono::steady_clock::now() - pace_start_).count();
if (expected_us > elapsed_us + 500)
    std::this_thread::sleep_for(std::chrono::microseconds(expected_us - elapsed_us - 500));
```

This prevents the CPU from reading the entire file into memory instantly. Without this pacing, live broadcasting to DNAS would buffer all audio up front and then go silent.

---

## Web UI: PHP Rules

### DOMContentLoaded Ordering

`dashboard.php` and `encoders.php` define JavaScript functions (e.g., `poll()`, `refreshLive()`) in a `<script>` block within the page body, BEFORE `app/inc/footer.php` is included. `footer.php` defines `mc1Api()` in its own `<script>` block.

If you call `poll()` or `mc1Api()` directly at the top level of the inline script (not inside a function), it will throw TypeError because `mc1Api` is not yet defined at parse time.

Always wrap all startup calls in a DOMContentLoaded listener:

```javascript
document.addEventListener('DOMContentLoaded', function() {
    poll();
    pollStatus();
    setInterval(poll, POLL_MS);
    setInterval(pollStatus, 30000);
    requestAnimationFrame(tickProgress);
});
```

This guarantees footer.php scripts have run and mc1Api is defined before we call it.

### Real-Time Progress Bar (dashboard.php)

We use requestAnimationFrame at ~60fps for smooth progress bar interpolation between 5-second API polls:

```javascript
var slotLiveState = {}; // slot_id → { pos_ms, dur_ms, poll_time, is_live }

function tickProgress() {
    var now = Date.now();
    for (var sid in slotLiveState) {
        var s = slotLiveState[sid];
        if (!s.is_live || !s.dur_ms) continue;
        var est_ms = s.pos_ms + (now - s.poll_time);
        if (est_ms > s.dur_ms) est_ms = s.dur_ms;
        var pct = (est_ms / s.dur_ms * 100).toFixed(3);
        var pb = document.getElementById('pb-' + sid);
        if (pb) pb.style.width = pct + '%';
        var pe = document.getElementById('pe-' + sid);
        if (pe) pe.textContent = fmtPos(est_ms);
    }
    requestAnimationFrame(tickProgress);
}
```

Each 5-second API poll seeds `slotLiveState[id]` with `{ pos_ms, dur_ms, poll_time: Date.now(), is_live }`. The rAF loop advances `est_ms` forward using wall-clock time delta.

The progress bar element must have `transition: none` set (no CSS transition) or the 60fps updates create visual artifacts from competing CSS animation.

### Never use exit() or die() in PHP

The server has the `uopz` PHP extension which intercepts all `exit()` and `die()` calls. Any PHP file that calls `exit()` or `die()` will silently misbehave. Always use `return` inside functions and `if/elseif` chains instead.

### Never curl from PHP back to C++ (loopback deadlock)

Never make a curl request from PHP code to `127.0.0.1:8330` or `127.0.0.1:8344`. This causes a thread pool deadlock: the C++ thread is blocked in FastCGI waiting for PHP to return, while PHP is blocked in curl waiting for C++. Browser JavaScript has the `mc1session` cookie and can call `/api/v1/encoders/{slot}/start` directly.

---

## Run the Encoder

```bash
# Kill any existing instance first (SO_REUSEPORT gotcha: two instances can bind the same port)
pkill -9 -f 'build/mcaster1-encoder'
sleep 2

# Start in daemon mode
nohup ./build/mcaster1-encoder \
  --config src/linux/config/mcaster1_rock_yolo.yaml \
  > /tmp/mc1enc.log 2>&1 & disown $!

echo "Encoder PID: $!"
```

Always use `disown $!`. Without it the shell sends SIGHUP when it exits and the encoder shuts down.

---

## Logs

```bash
tail -f /tmp/mc1enc.log               # Startup stderr (first thing to check)
tail -f /var/log/mcaster1/error.log   # C++ app errors
tail -f /var/log/mcaster1/encoder.log # Per-slot events (START/STOP/track change)
tail -f /var/log/mcaster1/access.log  # HTTP access log
tail -f /var/log/mcaster1/api.log     # API request/response bodies (level 5 only)
tail -f /var/log/mcaster1/php_error.log  # PHP errors
tail -f /var/log/mcaster1/php_fpm.log    # PHP-FPM pool log
```

Enable debug logging:

```bash
./build/mcaster1-encoder --config ... --log-level 5
# or: -v flag also sets level 5
```

---

## configure.ac: What to Know

- `AX_CXX_COMPILE_STDCXX([17], [noext], [mandatory])` from autoconf-archive
- MariaDB detection is required (not optional): tries `libmariadb` → `mariadb` pkg-config → manual header check
- `PKG_CONFIG_PATH` is extended to `/usr/local/lib/pkgconfig` for fdk-aac (not in standard repos)
- Per-codec AM_CONDITIONALs: `HAVE_LAME`, `HAVE_VORBIS`, `HAVE_FLAC`, `HAVE_OPUS`, `HAVE_FDKAAC`, `HAVE_MPG123`, `HAVE_AVFORMAT`, `HAVE_TAGLIB`, `HAVE_PORTAUDIO`
- The Linux vendor note in configure.ac:
  ```
  dnl Linux vendor headers: src/linux/external/include
  dnl httplib.h + nlohmann/json.hpp live there (header-only, not in system repos)
  dnl Windows vendor headers live in external/ at project root — not referenced here
  ```

---

## Test Station Config

**File:** `src/linux/config/mcaster1_rock_yolo.yaml`

Slots 1-12 are defined (MP3, Opus, AAC-LC, HE-AAC v1/v2, AAC-ELD, FLAC, etc.) all pointing to `dnas.mcaster1.com:9443`. Slot 1 (`/yolo-mp3-128`) is the primary test slot used for most E2E testing.

Credentials: `djpulse` / `hackme` for both C++ admin and MySQL.

---

## Phase Status

| Phase | Version | Description | Status |
|-------|---------|-------------|--------|
| L1 | v1.0.0 | Infrastructure, build system, platform abstraction | **COMPLETE** |
| L2 | v1.1.1 | HTTP/HTTPS admin + login + web UI | **COMPLETE** |
| L3 | v1.2.0 | Audio encoding + streaming + FastCGI + PHP | **COMPLETE** |
| L4 | v1.3.0 | DSP chain (EQ/AGC/xfade) + ICY2 + DNAS stats | **COMPLETE** |
| L5 | v1.4.0 | PHP frontend overhaul + logging + autotools + rAF progress | **COMPLETE** |
| L5.1 | v1.4.1 | Media library (folder browser, track tags, artwork, playlists) | **COMPLETE** |
| L5.2 | v1.4.2 | Browser media player (queue, audio streaming, drag-select) | **COMPLETE** |
| L5.3 | v1.4.3 | Session fix, Content-Range fix (Firefox), 30-day cookie TTL | **COMPLETE** |
| L5.4 | v1.4.4 | Encoder editor, playlist generator wizard, user profile | **COMPLETE** |
| L5.5 | v1.4.5 | Standalone popup player, category UX overhaul, HTML quoting bug sweep | **COMPLETE** |
| L6 | v1.5.0 | Streaming Server Relay Monitor (multi-DNAS/Icecast/Shoutcast) | PLANNED |
| L7 | v1.6.0 | Listener Analytics & Full Metrics Dashboard | PLANNED |
| L8 | v1.7.0 | System Health Monitoring (CPU/Mem/Net via /proc) | PLANNED |

---

## EncoderConfig Codec Parameters (C++ actual behavior)

What `encoder_slot.cpp` currently reads from `EncoderConfig` per codec:

| Codec | bitrate_kbps | quality (int 0-10) | sample_rate | channels | Notes |
|-------|-------------|-------------------|-------------|----------|-------|
| MP3 | ✓ CBR via `lame_set_brate()` | algorithm quality hardcoded=2 (planned: use cfg_.quality) | ✓ | MONO or JOINT_STEREO | CBR only currently |
| Vorbis | ✗ | ✓ VBR: `quality/10.0f` → libvorbis 0.0-1.0 | ✓ | ✓ | VBR only |
| Opus | ✓ `bitrate_kbps×1000` via `OPUS_SET_BITRATE` | ✗ | ✓ (→48kHz internal) | ✓ | VBR by default |
| FLAC | ✗ lossless | compression hardcoded=5 (planned: use quality) | ✓ | ✓ | |
| AAC-LC | ✓ `bitrate_kbps×1000` via `AACENC_BITRATE` | ✗ (afterburner=1) | ✓ | 1 or 2 | |
| HE-AAC v1 | ✓ | ✗ | ✓ | 1 or 2 | SBR downsampled |
| HE-AAC v2 | ✓ | ✗ | ✓ | MUST=2 (PS) | SBR+PS; mono input → encoder fail |
| AAC-ELD | ✓ | ✗ | ✓ | 1 or 2 | 512-sample granule, SBR-ELD |

### Planned additions (Task #26) to `EncoderConfig` struct:

```cpp
enum class EncodeMode { CBR, VBR, ABR } encode_mode = EncodeMode::CBR;
enum class ChannelMode { STEREO, JOINT, MONO, PARAMETRIC } channel_mode = ChannelMode::JOINT;
// quality int already exists (default 5)
```

### Pending DB migration (Task #24):

```sql
ALTER TABLE encoder_configs
  ADD COLUMN quality      INT          NOT NULL DEFAULT 5    AFTER channels,
  ADD COLUMN encode_mode  ENUM('cbr','vbr','abr') NOT NULL DEFAULT 'cbr' AFTER quality,
  ADD COLUMN channel_mode ENUM('stereo','joint','mono','parametric') NOT NULL DEFAULT 'joint' AFTER encode_mode;
```
