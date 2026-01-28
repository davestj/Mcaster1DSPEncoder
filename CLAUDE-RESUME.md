# Mcaster1DSPEncoder — Claude Session Resume File

**Session ID:**       `MC1ENC-LINUX-DEV-20260224`
**Last Updated:**     2026-02-24
**Branch:**           `linux-dev`
**Working Directory:** `/var/www/mcaster1.com/Mcaster1DSPEncoder/`
**Platform:**         Linux (Debian 12, PHP 8.2-FPM, MariaDB)
**Maintainer:**       Dave St. John <davestj@gmail.com>

---

## Resume Command

To resume this exact session context, start Claude Code in the project root and paste this:

```
I am resuming session MC1ENC-LINUX-DEV-20260224 on the linux-dev branch of
Mcaster1DSPEncoder. Working dir: /var/www/mcaster1.com/Mcaster1DSPEncoder/
Please read CLAUDE-RESUME.md, CLAUDE.md, src/linux/CLAUDE.md, and
src/linux/web_ui/CLAUDE.md before continuing any work.
```

---

## Project Identity

- **Name:** Mcaster1DSPEncoder — Dual-platform broadcast DSP encoder (Linux focus)
- **Fork of:** EdCast / AltaCast DSP encoder by Ed Zaleski
- **Maintainer:** Dave St. John <davestj@gmail.com>
- **License:** GPL v2 (inherited)
- **Current Active Branch:** `linux-dev`
- **Main Branch:** `master`
- **Encoder URL (test):** https://encoder.mcaster1.com:8344 (HTTP: :8330)
- **Admin Credentials:** `djpulse` / `hackme`

---

## Phase Completion Status (as of 2026-02-24)

| Phase | Version | Description | Status |
|-------|---------|-------------|--------|
| L1 | v1.0.0 | Infrastructure, build system, platform abstraction | **COMPLETE** |
| L2 | v1.1.1 | HTTP/HTTPS admin server + login + web UI | **COMPLETE** |
| L3 | v1.2.0 | Audio encoding + streaming + FastCGI + PHP app layer | **COMPLETE** |
| L4 | v1.3.0 | DSP chain (EQ/AGC/xfade) + ICY2 + DNAS stats proxy | **COMPLETE** |
| L5 | v1.4.0 | PHP frontend overhaul + logging + autotools + rAF progress | **COMPLETE** |
| L5.1 | v1.4.1 | Media library (folder browser, track tags, artwork, playlists) | **COMPLETE** |
| L5.2 | v1.4.2 | Browser media player (queue, audio streaming, drag-select) | **COMPLETE** |
| L5.3 | v1.4.3 | Session fix, Content-Range fix (Firefox) | **COMPLETE** |
| L5.4 | v1.4.4 | Encoder editor, playlist generator wizard, user profile | **COMPLETE** |
| **L5.5** | **v1.4.5** | **Standalone popup player, category UX overhaul, bug sweep** | **COMPLETE — TODAY** |
| L6 | v1.5.0 | Streaming Server Relay Monitor (SAM Broadcaster-style) | PLANNED |
| L7 | v1.6.0 | Listener Analytics & Full Metrics Dashboard | PLANNED |
| L8 | v1.7.0 | System Health Monitoring (CPU/Mem/Net via /proc) | PLANNED |

---

## Today's Session — 2026-02-24 (L5.5)

### New Features Built

#### `web_ui/mediaplayerpro.php` — NEW (Standalone Popup Player)
- WMP-style 3-column popup: Library Tree | Track Browser | Queue
- Transport bar: Play/Pause, Prev, Next, Shuffle, Repeat All, Volume
- Resizable left/right panes via mouse drag, widths persisted to `localStorage`
  - CSS vars `--pro-lib-w`, `--pro-q-w` keep transport bar aligned with resized panes
- Album art in Now Playing: MusicBrainz auto-fetch when no `art_url` cached
- 13-item track right-click menu: Edit Tags, Fetch Art, Add to Playlist, New Playlist, Add to Category, Play Now, Add to Queue, Delete, MusicBrainz/Last.fm/Discogs lookups
- Queue right-click: Play Now, Move to Top, Remove from Queue
- Multi-select: Ctrl+click toggles, floating action bar (Add to Playlist, Add to Queue, Delete Selected)
- Full Edit Tags modal: title, artist, album, genre, year, BPM, star rating, weight slider
- Inline `mc1Api`, `mc1Toast`, `mc1State` (self-contained — no footer.php dependency)
- Auto-advance: played tracks removed from queue, next track auto-plays

#### `web_ui/mediaplayer.php` — MODIFIED
- Queue right-click context menu (Play Now, Move to Top, Remove from Queue)
- Album art auto-fetch via MusicBrainz when no art cached
- "Launch Standalone Player" button → opens `mediaplayerpro.php` popup
- Auto-advance: `audio.ended` event → `playerNext()`, played tracks removed from queue

#### `web_ui/media.php` — MODIFIED
- Category "Add to Playlist…" bulk action in right-click context menu
- Auto-library-sync after batch upload completes
- All category onclick/oncontextmenu HTML-quoting fixed

### Bugs Fixed Today

| # | Bug | File | Root Cause | Fix |
|---|-----|------|-----------|-----|
| 1 | All category onclick/oncontextmenu broken | `media.php` lines 321-322 | `json_encode('Name')` = `"Name"` (double-quoted). Inside double-quoted HTML attr → JS SyntaxError. Browser never calls the handler. | Wrapped with `h()` → `&quot;Name&quot;` → browser decodes to `"Name"` → valid JS |
| 2 | Dynamically-created categories also broken | `media.php` ~line 1977 | `JSON.stringify(name)` in `li.innerHTML` had same quoting issue | Applied `esc(JSON.stringify(name))` |
| 3 | Right-click menu missing on new categories | `media.php` ~line 1972 | `doCreateCategory` appended `li` without `oncontextmenu` attribute | Added `oncontextmenu="catCtxShow(event,id,name)"` to generated HTML |
| 4 | Clicking category doesn't load tracks | `media.php` | Same as bug #1 — onclick SyntaxError | Fixed by quoting fix |
| 5 | Add-to-category requires page refresh | `media.php` `doAddToCategory` | Success handler only toasted, never refreshed | Now updates sidebar badge count + re-fetches tracks if viewing that category |
| 6 | `catCtxShow` TypeError crash | `media.php` line 1381 | `ctxMenu.style.display = 'none'` had no null guard | Added `if (ctxMenu)` guard |
| 7 | Cat context menu closes immediately | `media.php` line 1389 | `document.addEventListener('click')` could fire on some browsers after contextmenu | Changed to `mousedown` + `catCtxMenu.contains(e.target)` check |
| 8 | `catCtxQueue` only fetched 50 tracks max | `media.php` | Used `per_page: 500` but PHP only reads `limit` | Changed to `limit: 200` |
| 9 | Batch upload scan param wrong | `media.php` upload handler | Sent `dir:` but `tracks.php` expects `directory:` | Fixed to `directory:` |

---

## Architecture — Key Rules (Must Not Violate)

1. **No PHP→C++ loopback curl** — Browser JS calls `/api/v1/...` directly. PHP never curls back to C++ on the same port. Thread pool deadlock.
2. **No `exit()` or `die()` in PHP** — `uopz` extension is active. Use `return` inside functions.
3. **`DOMContentLoaded` wrapper required** — `mc1Api` is defined in `footer.php` (included AFTER the page `<script>` block). Any top-level `mc1Api()` call in the IIFE throws `ReferenceError`. Always wrap startup calls in `document.addEventListener('DOMContentLoaded', ...)`.
4. **`Mc1Db` trait for all DB** — No raw PDO; no ORMs. Run `SET foreign_key_checks=0` at connection time.
5. **`h()` on all PHP→HTML output** — `htmlspecialchars(ENT_QUOTES|ENT_HTML5)`.
6. **`h(json_encode($var))` in HTML attributes** — Never bare `json_encode()` in double-quoted HTML attributes; the outer double-quotes in JSON break the attribute. Use `h()` or `JSON_HEX_QUOT` flag.
7. **SO_REUSEPORT deadlock** — Always `pkill -9 -f mcaster1-encoder && sleep 2` before restarting. Two instances can bind the same port simultaneously.
8. **Codec -DHAVE_XXX must be explicit** — Source files don't include `config.h`. Autotools `AM_CPPFLAGS` must explicitly add `-DHAVE_LAME`, `-DHAVE_OPUS`, etc. in each `if HAVE_XXX` conditional.

---

## Database Layout

```
mcaster1_encoder   — users, roles, user_sessions, encoder_configs, streaming_servers
mcaster1_media     — tracks, playlists, playlist_tracks, player_queue, cover_art,
                     categories, track_categories, media_library_paths
mcaster1_metrics   — listener_sessions, daily_stats
```

MySQL CLI: `mysql --defaults-extra-file=~/.my.cnf`
Never inline credentials.

---

## Build & Run

```bash
# Kill existing instance first (SO_REUSEPORT gotcha)
pkill -9 -f 'build/mcaster1-encoder'; sleep 2

# Build (autotools)
./configure && make -j$(nproc)

# Start
nohup ./build/mcaster1-encoder \
  --config src/linux/config/mcaster1_rock_yolo.yaml \
  > /tmp/mc1enc.log 2>&1 & disown $!

# Logs
tail -f /var/log/mcaster1/error.log
tail -f /var/log/mcaster1/encoder.log
tail -f /var/log/mcaster1/access.log
tail -f /var/log/mcaster1/php_error.log
```

---

## File Inventory — Modified Today

| File | Change |
|------|--------|
| `src/linux/web_ui/media.php` | Category quoting bug fix, mousedown listener fix, doAddToCategory refresh, batch upload scan param fix, catCtxQueue limit fix, Add-to-Playlist from category context |
| `src/linux/web_ui/mediaplayerpro.php` | NEW — Standalone WMP-style popup player |
| `src/linux/web_ui/mediaplayer.php` | Queue ctx menu, art auto-fetch, launch popup button |

---

## Next Session Priorities (L6)

1. **Phase L6 — Streaming Server Relay Monitor**
   - `streaming_servers` table in `mcaster1_encoder`
   - `app/api/servers.php` — CRUD + live stat proxy for Icecast2, Shoutcast v1/v2, Mcaster1DNAS
   - `settings.php` — multi-server management panel with per-server listener/bitrate stats
   - Client-side polling at 30s intervals (no PHP cron)
   - Color-coded status: Online/Offline/Unknown

2. **Phase L7 — Metrics Dashboard**
   - Real-time listener count graph
   - Unique listeners / geographic breakdown / top tracks
   - `metrics.php` full rewrite

3. **Phase L8 — System Health**
   - `/proc/stat` CPU, `/proc/meminfo` memory, `/proc/net/dev` network
   - `health.php` or System tab in `settings.php`

---

## Personal History

- **Dave St. John** and **Ed Zaleski** — collaboration since 2001 (Castit, casterclub.com)
- EdCast/AltaCast VC6 source → Mcaster1DSPEncoder
- Dave sold mediacast1.com to Spacial Audio (→ Triton Digital); kept casterclub.com
- **Companion project:** Mcaster1DNAS — Icecast 2.4 fork at `/var/www/mcaster1.com/mcaster1dnas/`
