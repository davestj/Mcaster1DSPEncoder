# CLAUDE.md — web_ui Layer Context

**Path:** `src/linux/web_ui/`
**Last updated:** 2026-02-24

For mysql connection work, use:
```
mysql --defaults-extra-file=~/.my.cnf -e "SHOW DATABASES LIKE '%yp%';"
```

---

## PHP Rules (Mandatory)

- **No `exit()` or `die()`** — uopz extension is active. Use `return` inside functions.
- **No PHP→C++ loopback curl** — causes FastCGI thread pool deadlock. Browser JS calls `/api/v1/...` directly.
- **`Mc1Db` trait** for all DB access — raw SQL only, no ORMs.
- **`mc1_api_respond($data, $status)`** for all JSON API output.
- **`mc1_is_authed()`** auth gate in every API file.
- **`h()`** on all user data rendered into HTML.
- **`escapeshellarg()`** on all shell exec arguments.
- **DOMContentLoaded** — wrap all startup `mc1Api()` calls in `document.addEventListener('DOMContentLoaded', ...)`. The IIFE script runs before footer.php (which defines `mc1Api`). Any top-level `mc1Api()` call in the IIFE body will throw `ReferenceError: mc1Api is not defined` and kill all `window.*` function assignments.
- **`h(json_encode($var))` in HTML attributes** — `json_encode()` produces double-quoted strings (`"Rock"`). If you embed that inside a double-quoted HTML attribute (`onclick="foo(1,"Rock")"`), the browser closes the attribute at the first inner `"` and the JS is syntactically broken. The handler silently never fires. Always wrap with `h()` → `&quot;Rock&quot;` → browser decodes to `"Rock"` → valid JS. Same rule applies to JS-generated HTML: use `esc(JSON.stringify(name))` where `esc()` is an HTML-entity encoder.
  ```php
  // WRONG — "Rock" breaks double-quoted HTML attribute:
  onclick="loadCategoryTracks(<?= $cat['id'] ?>, <?= json_encode($cat['name']) ?>)"

  // CORRECT — &quot;Rock&quot; decoded by browser to "Rock":
  onclick="loadCategoryTracks(<?= (int)$cat['id'] ?>, <?= h(json_encode($cat['name'])) ?>)"
  ```
  ```javascript
  // WRONG in JS-generated HTML:
  li.innerHTML = '<div onclick="fn(' + id + ',' + JSON.stringify(name) + ')">';
  // CORRECT:
  li.innerHTML = '<div onclick="fn(' + id + ',' + esc(JSON.stringify(name)) + ')">';
  ```

---

## Page Inventory

| File | Nav | Description |
|------|-----|-------------|
| `login.html` | — | Pre-auth login (plain HTML, no PHP) |
| `dashboard.php` | Dashboard | Live encoder cards, rAF progress bars, Chart.js bandwidth |
| `encoders.php` | Encoders | Per-slot DSP panel + live stats + slot action buttons + SLEEP/Wake |
| `edit_encoders.php` | — | Full-page per-slot encoder config editor (6 tabs) |
| `media.php` | Media | Track library folder browser + categories + scan + playlist files |
| `mediaplayer.php` | Media | Embedded browser audio player with DB-backed queue |
| `mediaplayerpro.php` | — | Standalone WMP-style 3-column popup player (opened from mediaplayer.php) |
| `playlists.php` | Playlists | DB playlist CRUD + 4-step generation wizard (8 algorithms) |
| `metrics.php` | Metrics | Chart.js listener analytics |
| `profile.php` | — | User profile (display_name, email, password change) |
| `settings.php` | Settings | Server info + encoder config table + DB admin |

---

## API Inventory (`app/api/`)

| File | Actions |
|------|---------|
| `auth.php` | `auto_login`, `whoami`, `login`, `logout` |
| `tracks.php` | `list`, `search`, `add`, `update`, `delete`, `scan`, `playlist_files`, `browse`, `get_categories`, `create_category`, `delete_category`, `add_to_category`, `list_category_tracks`, `remove_from_category`, `get_artwork`, `set_artwork`, `fetch_artwork_musicbrainz` |
| `playlists.php` | `list`, `get_tracks`, `create`, `delete`, `add_track`, `remove_track`, `load`, `get_categories`, `get_algorithms`, `get_rotation_rules`, `preview`, `generate`, `get_clock_templates`, `save_clock_template` |
| `player.php` | `queue_add`, `queue_list`, `queue_remove`, `queue_clear`, `queue_move_top` |
| `audio.php` | HTTP Range (206) audio streaming — no action param; uses `?id=N` or `?path=...` |
| `artwork.php` | Cover art by hash — no action param; uses `?hash=...` |
| `encoders.php` | `list_configs`, `get_config`, `save_config`, `delete_config`, `update_playlist`, `add_user`, `update_user`, `delete_user`, `toggle_user`, `reset_password`, `db_stats`, `get_token`, `generate_token` |
| `metrics.php` | `summary`, `daily_stats`, `sessions`, `top_tracks` |
| `servers.php` | `list`, `get`, `create`, `update`, `delete`, `test` |
| `serverstats.php` | `live`, `history` |
| `profile.php` | `get_profile`, `update_profile`, `change_password` |

---

## Class Inventory (`app/inc/`)

| File | Class / Functions | Purpose |
|------|------------------|---------|
| `auth.php` | `mc1_is_authed()` | C++ FastCGI auth gate |
| `user_auth.php` | `mc1_current_user()`, `mc1_require_auth()` | MySQL PHP session |
| `db.php` | `mc1_db($name)`, `h()`, `mc1_format_duration()` | PDO singleton + helpers |
| `traits.db.class.php` | `Mc1Db` trait | `rows()`, `row()`, `run()`, `scalar()`, `lastId()` |
| `logger.php` | `mc1_log()`, `mc1_api_respond()`, `mc1_log_request()` | PHP logging |
| `mc1_config.php` | Constants | `MC1_AUDIO_ROOT`, `MC1_PLAYLIST_DIR`, `MC1_ARTWORK_DIR`, etc. |
| `header.php` | — | HTML head, sidebar nav, Chart.js flag |
| `footer.php` | `mc1Api()`, `mc1Toast()` | JS helpers, PHP session auto-bootstrap |
| `edit.encoders.class.php` | `EditEncoders` static | `load()`, `all()`, `validate()`, `save()`, `delete()` |
| `playlist.builder.algorithm.class.php` | `PlaylistBuilderAlgorithm` static | 8 generation algorithms |
| `playlist.manager.pro.class.php` | `PlaylistManagerPro` static | `generate()`, `preview()` orchestrator |

---

## Encoder Editor (edit_encoders.php)

6-tab page for editing a single encoder slot config row.

**URL patterns:**
- `?id=N` — edit by `encoder_configs.id`
- `?slot=N` — edit by `slot_id`
- `?new=1` — create new (empty form)

**Tabs:**
1. **Basic** — slot_id, name, input_type (playlist/url/device), playlist_path, device_index, description, is_active
2. **Audio** — codec, bitrate_kbps, sample_rate, channels, volume, shuffle, repeat_all *(codec-aware UI coming in Task #25)*
3. **Server** — protocol, host, port, mount, user, pass, quick presets
4. **Station & ICY2** — station_name, genre, website_url, icy2_* fields
5. **DSP** — eq_enabled/preset, agc_enabled, crossfade toggle+duration; applyDspLive() calls C++ directly
6. **Archive** — archive_enabled, archive_dir

**JS key functions:**
- `saveConfig()` → POST `/app/api/encoders.php {action:'save_config',...}`
- `applyDspLive()` → PUT `/api/v1/encoders/{slot}/dsp` (direct C++, no PHP proxy)
- `deleteConfig()` → POST `/app/api/encoders.php {action:'delete_config',id:...}` → redirect settings.php
- `loadDevices()` → GET `/api/v1/devices`
- `codecChanged()` *(planned Task #25)* → show/hide codec-specific fields

---

## Gear Icon Link Fix (2026-02-24)

Both `encoders.php` gear icons now link to `edit_encoders.php`:
- PHP template: `href="/edit_encoders.php?id=<?= (int)$cfg['id'] ?>"`
- JS `updateSlotLive()`: uses `SLOT_CFG_IDS[id]` PHP-rendered map (slot_id → db id)
- `settings.php` encoder table: edit button is now `<a href="/edit_encoders.php?id=...">` link

---

## Playlist Generator (playlists.php)

4-step wizard modal for AI/algorithmic playlist generation.

**Algorithms (8 types):**

| Constant | Label | Strategy |
|----------|-------|---------|
| `weighted_random` | Weighted Random | Weighted pick ignoring recent plays |
| `smart_rotation` | Smart Rotation | Artist separation + progressive relaxation |
| `hot_rotation` | Hot Rotation | Top-rated tracks, high frequency |
| `clock_wheel` | Clockwheel | Category-sequence repeating pattern |
| `genre_block` | Genre Block | Group tracks by genre |
| `energy_flow` | Energy Flow | Sort by energy_level ASC or DESC |
| `ai_adaptive` | AI Adaptive | Multi-factor score: freshness 35%, rating 30%, weight 20%, play_count 10%, energy 5% |
| `daypart` | Daypart | Different style by time of day |

**Generated M3U files:** Written to `MC1_PLAYLIST_DIR/{safe-name}_{YYYYMMDD_HHMM}.m3u`

---

## SLEEP State (2026-02-24)

The `sleep` CSS class is defined in `header.php` (orange badge + card border). The `slotAct()` function in `encoders.php` handles `'wake'` → `POST /api/v1/encoders/{slot}/wake` automatically. The Wake button and a forced-Start button appear when `cls === 'sleep'` in `updateSlotLive()`.

---

## Media Player Architecture (L5.2 + L5.5)

### mediaplayer.php (embedded player)
- Embedded on the `media.php` page or loaded standalone
- Uses `app/api/player.php` for DB-backed queue (per user, scoped to `user_id`)
- Uses `app/api/audio.php` for HTTP Range audio streaming (206 Partial Content)
- Queue right-click: Play Now, Move to Top, Remove from Queue
- Album art: auto-fetches from MusicBrainz if `art_url` is null in DB
- "Launch Standalone Player" button: `window.open('/mediaplayerpro.php', 'mc1pro', 'width=1200,height=750')`
- Auto-advance: `audio.addEventListener('ended', playerNext)`

### mediaplayerpro.php (standalone popup — NEW L5.5)
- Self-contained: inlines `mc1Api`, `mc1Toast`, `mc1State` — does NOT include `footer.php`
  - This is intentional: popup windows don't share the parent page's JS context
  - Rule for future changes: keep it self-contained; don't add footer.php dependency
- 3-column layout via CSS Flexbox (not Grid — required for resizable panes)
  - Left: library folder/category tree (width CSS var `--pro-lib-w`, localStorage key `pro_lib_w`)
  - Center: track browser table
  - Right: queue list (width CSS var `--pro-q-w`, localStorage key `pro_q_w`)
- Pane resize: `mousedown` on divider → `mousemove` updates CSS var → `mouseup` persists to localStorage
- Transport bar: stays full-width via `grid-template-columns: var(--pro-lib-w) 1fr var(--pro-q-w)` → same widths as panes
- Track right-click menu: 13 items (Edit Tags, Fetch Art, Add to Playlist, New Playlist, Add to Category, Play Now, Add to Queue, Delete, MusicBrainz Lookup, Last.fm Lookup, Discogs Lookup)
- Queue right-click: Play Now, Move to Top, Remove from Queue
- Multi-select: Ctrl+click toggles; floating action bar appears when ≥1 selected
- Edit Tags modal: `tracks.php {action:'update'}` → full ID3 fields + BPM + rating + weight

---

## Category System (L5.1 + L5.5 — FULLY FIXED)

### HTML Attribute Quoting — CRITICAL LESSON

The entire category system was silently broken by a PHP HTML attribute quoting bug. This is documented here so it is never reintroduced.

**The bug:**
```php
// json_encode('Rock') = "Rock"  (double-quoted)
// Inside a double-quoted HTML attribute:
onclick="loadCategoryTracks(1,"Rock")"
//                               ^ browser closes attribute here
// Result: JS SyntaxError — event handler NEVER registered
```

**The fix:**
```php
onclick="loadCategoryTracks(<?= (int)$cat['id'] ?>, <?= h(json_encode($cat['name'])) ?>)"
// h() converts " to &quot;
// Browser decodes &quot; back to " when evaluating the attribute
// JS engine sees: loadCategoryTracks(1, "Rock")  — valid
```

**Same issue in JS-generated HTML:**
```javascript
// WRONG:
li.innerHTML = '<div onclick="fn(' + id + ',' + JSON.stringify(name) + ')">';
// CORRECT:
li.innerHTML = '<div onclick="fn(' + id + ',' + esc(JSON.stringify(name)) + ')">';
// esc() = str.replace(/&/g,'&amp;').replace(/"/g,'&quot;').replace(/'/g,'&#39;').replace(/</g,'&lt;').replace(/>/g,'&gt;')
```

**Rule:** Any time a PHP variable (especially a string) is embedded in a double-quoted HTML attribute that will be evaluated as JavaScript, it MUST go through `h(json_encode(...))`. No exceptions.

---

## Pending Work

Phase L5.5 COMPLETE as of 2026-02-24. All Tasks #24–#26 also complete.

Next planned work:
- **L6**: Streaming Server Relay Monitor (`servers.php` + settings panel + polling)
- **L7**: Metrics Dashboard full rewrite (5 tabs: System Health, Encoder Slots, Servers, Listener Analytics, Event Log)
- **L8**: System Health (C++ `/proc/` sampler + `/api/v1/system/health` endpoint)
