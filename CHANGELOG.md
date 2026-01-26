# CHANGELOG — Mcaster1DSPEncoder

All notable changes to this project are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Versioning follows `vMAJOR.MINOR.PATCH` conventions.

---

## [Unreleased — linux-dev] — 2026-02-24

### Added

#### Media Library (`media.php`)
- Category right-click context menu: Queue in Player, Add to Playlist…, Delete Category
- "Add to Playlist…" bulk action from category context menu (loads up to 200 tracks, opens playlist modal)
- `doAddToCategory`: sidebar track count badge increments live after track is added to category
- `doAddToCategory`: if user is currently viewing the target category, track list auto-refreshes without page reload
- `oncontextmenu` attribute now correctly applied to dynamically-created categories (via "New Category" button)

#### Standalone Popup Player (`mediaplayerpro.php`) — NEW FILE
- WMP-style 3-column popup window: Library Tree | Track Browser | Queue
- Transport bar: Play/Pause, Previous, Next, Shuffle, Repeat All, Volume slider
- Album art display in Now Playing panel with MusicBrainz auto-fetch when no art is cached
- Resizable left (Library) and right (Queue) panes via draggable dividers
  - Pane widths persisted to `localStorage` via `mc1State`
  - CSS variables `--pro-lib-w` and `--pro-q-w` keep transport bar aligned with pane widths
- 13-item track right-click context menu: Edit Tags, Fetch Art, Add to Playlist, New Playlist from Track, Add to Category, Play Now, Add to Queue, Delete from Library, MusicBrainz Lookup, Last.fm Lookup, Discogs Lookup
- Queue right-click context menu: Play Now, Move to Top, Remove from Queue
- Multi-select tracks: Ctrl+click toggles, floating action bar for bulk ops (Add to Playlist, Add to Queue, Delete Selected)
- Full Edit Tags modal: title, artist, album, genre, year, BPM, star rating, rotation weight slider
- Add to Playlist modal with clockwheel weight slider (0.1–10.0)
- Inline player: `mc1Api`, `mc1Toast`, `mc1State` inlined (no footer.php dependency — self-contained popup)
- Auto-advance: `audio.addEventListener('ended', playerNext)` advances queue automatically
- Queue cleanup: played tracks removed from queue after playback

#### Embedded Player (`mediaplayer.php`)
- Queue right-click context menu: Play Now, Move to Top, Remove from Queue
- Album art auto-fetch from MusicBrainz API when `art_url` is null
- "Launch Standalone Player" button with tooltip: opens `mediaplayerpro.php` in popup window
- Auto-advance and queue cleanup (same as pro player)

#### Batch Upload (`media.php`)
- After all files uploaded successfully, automatically triggers `tracks.php?action=scan` to sync new files into the library database
- Scan result shown inline (files added, files already known)

---

### Fixed

#### `media.php` — Category System

| Bug | Root Cause | Fix |
|-----|-----------|-----|
| Right-click context menu never fires on any category | `json_encode('Name')` produces `"Name"` (double-quoted). Embedded inside double-quoted HTML attribute: `onclick="...,\"Name\","` → JS SyntaxError, event handler never called | Wrapped all `json_encode()` calls in PHP onclick/oncontextmenu attributes with `h()` → produces `&quot;Name&quot;` → browser decodes to `"Name"` → valid JS |
| Dynamically created categories (New Category button) also broken | Same quoting issue in JS-generated `li.innerHTML` | Applied `esc(JSON.stringify(name))` → `&quot;Name&quot;` in generated HTML |
| `oncontextmenu` missing from newly created categories | `doCreateCategory` appended `li.innerHTML` without `oncontextmenu` attribute | Added `oncontextmenu="catCtxShow(event,id,name)"` to generated HTML |
| Clicking category doesn't load tracks | `onclick` handler had JS SyntaxError from broken quoting — never fired | Fixed by quoting fix above |
| Adding track to category requires page refresh to see | `doAddToCategory` only showed toast and hid modal | Now increments count badge and re-fetches category tracks if currently viewing |
| `catCtxShow` could throw TypeError | `ctxMenu.style.display = 'none'` had no null guard | Added `if (ctxMenu)` guard |
| Context menu closes immediately after opening (race condition) | `document.addEventListener('click', ...)` could fire after `contextmenu` event in some browsers | Changed to `document.addEventListener('mousedown', ...)` with `catCtxMenu.contains(e.target)` check |
| `catCtxQueue` / `catCtxAddToPlaylist` fetched wrong number of tracks | Used `per_page: 500` but PHP API only reads `limit` parameter | Changed to `limit: 200` (API maximum) |

#### `media.php` — Batch Upload
- `triggerLibraryScan()` sent `dir` parameter but `tracks.php` expects `directory` → scan silently did nothing. Fixed.

#### `mediaplayerpro.php` — Layout
- CSS Grid → Flexbox migration required for resizable panes; transport bar column template now syncs via CSS variables

---

## [v1.4.4] — 2026-02-24 (linux-dev, prior sessions)

### Added
- `edit_encoders.php`: Full 6-tab per-slot encoder config editor
- `app/inc/edit.encoders.class.php`: `EditEncoders` static class (load/all/validate/save/delete)
- Playlist generator wizard in `playlists.php`: 4-step modal, 8 algorithms
- `app/inc/playlist.builder.algorithm.class.php`: 8 generation algorithms
- `app/inc/playlist.manager.pro.class.php`: orchestrator
- Gear icon on `encoders.php` + edit button on `settings.php` link to `edit_encoders.php`
- SLEEP slot state: orange badge, Wake button, forced-Start button in `encoders.php`
- 30-day session cookie TTL (was session-scoped)
- Content-Range fix for Firefox audio streaming (duplicate header removed)
- User profile page (`profile.php`)
- Per-user session TTL override from `users.session_ttl_override`

---

## [v1.4.3] — 2026-02-23 (linux-dev)

### Added
- `mediaplayer.php`: Embedded browser media player with DB-backed queue
- `app/api/player.php`: Queue CRUD (queue_add, queue_list, queue_remove, queue_clear)
- `app/api/audio.php`: HTTP Range (206) audio streaming via FastCGI
- `app/api/artwork.php`: Cover art serving by hash
- Drag-select multi-select in track table
- DB-backed player queue scoped per user

### Fixed
- Content-Range duplicate header (C++ httplib + PHP both setting it)
- Firefox audio streaming 416 error

---

## [v1.4.2] — 2026-02-23 (linux-dev)

### Added
- `media.php` v2.0: Split-pane folder tree browser + track list
- Right-click context menu on tracks: Edit Tags, Fetch Art, Add to Playlist, Add to Category, Delete
- Edit Tags modal with full ID3 metadata + ffmpeg file write
- Category management: create, color-pick, bulk-add tracks
- DB library paths: add/remove server paths without YAML edits
- MusicBrainz album art auto-fetch
- Clockwheel weight slider in Add to Playlist modal
- Folder Scan tab
- Playlist Files tab

---

## [v1.4.1] — 2026-02-22 (linux-dev)

### Added
- `playlists.php`: DB-backed playlist CRUD + load-to-slot
- `app/api/playlists.php`: Full playlist API
- `app/api/tracks.php`: Track CRUD, scan, folder browse, category management
- `app/inc/mc1_config.php`: `MC1_AUDIO_ROOT`, `MC1_PLAYLIST_DIR`, `MC1_ARTWORK_DIR` constants
- `app/inc/traits.db.class.php`: `Mc1Db` PDO trait

---

## [v1.4.0] — 2026-02-21 (linux-dev) — Phase L5 COMPLETE

### Added
- `mc1_logger.h`: Singleton C++ logger with 5 log levels
- Logging: `access.log`, `error.log`, `encoder.log`, `api.log`
- Autotools canonical build (`configure.ac`, `Makefile.am`, `autogen.sh`)
- `install-deps.sh`: Multi-distro dependency installer
- `dashboard.php`: rAF 60fps progress bars, Chart.js bandwidth
- `encoders.php`: Per-slot DSP panel + live stats
- `settings.php`: Server info + config overview
- `metrics.php`: Listener analytics (placeholder)
- PHP session auto-bootstrap bridge (`footer.php` → `auth.php`)
- `mc1State` localStorage helper in `footer.php`
- ICY metadata reconnect fix (`first_connect_done_` atomic + `meta_gen_` atomic)

---

## [v1.3.0] — 2026-02-18 (linux-dev) — Phase L4 COMPLETE

### Added
- DSP chain: 10-band parametric EQ (RBJ biquad IIR), AGC/compressor/limiter, equal-power crossfader
- ICY2 headers: full station metadata push on connect
- DNAS stats proxy: `GET /api/v1/dnas/stats`
- EQ presets: classic_rock, country, modern_rock, pop, jazz, bass_boost, flat, etc.

---

## [v1.2.0] — 2026-02-14 (linux-dev) — Phase L3 COMPLETE

### Added
- Multi-format audio encoding: MP3 (LAME), Ogg Vorbis, Opus, FLAC, AAC-LC, HE-AAC v1/v2, AAC-ELD
- Live broadcast to Icecast2, Shoutcast v1/v2, Mcaster1DNAS
- FastCGI bridge to PHP-FPM (AF_UNIX socket, binary FastCGI/1.0)
- File source: libavcodec + libmpg123 decode with real-time pacing
- Crossfader: equal-power transition between tracks
- Archive writer: simultaneous WAV + MP3 recording

---

## [v1.1.1] — 2026-02-10 (linux-dev) — Phase L2 COMPLETE

### Added
- HTTP/HTTPS embedded admin server (cpp-httplib v0.18)
- Two-layer auth: C++ in-memory sessions + MySQL PHP sessions
- Login page (`login.html`)
- SSL/TLS support with self-signed cert generation
- Session TTL management

---

## [v1.0.0] — 2026-02-05 (linux-dev) — Phase L1 COMPLETE

### Added
- Linux source tree under `src/linux/`
- Autotools scaffolding (pre-canonical)
- Platform abstraction (no shared code with Windows build)
- MariaDB connectivity
- libyaml-cpp YAML config loading
- PortAudio device enumeration

---

*Windows build history is tracked separately in the Windows branch changelog.*
