<?php
/**
 * media.php — Media Library Manager
 *
 * File:    src/linux/web_ui/media.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-02-23
 * Purpose: We provide a SAM Broadcaster-style media library with:
 *          - Two-pane folder tree browser (left) + track list (right)
 *          - Right-click context menu: edit tags, add to playlist/category, fetch art, delete
 *          - Edit Tags modal: full ID3/metadata + write to file via ffmpeg
 *          - Add to Playlist modal with clockwheel weight slider (0.1 - 10.0)
 *          - Custom categories: create, manage, bulk-add tracks
 *          - DB-stored extra library paths (no YAML editing required)
 *          - Folder Scan tab with built-in path from MC1_AUDIO_ROOT
 *          - Playlist Files tab: list + load to encoder slot
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We wrap all startup JS in DOMContentLoaded to avoid mc1Api ordering bug
 *  - We use mc1Api() for all AJAX and mc1Toast() for all notifications
 *  - We use h() on all PHP-rendered user data in HTML
 *
 * CHANGELOG:
 *  2026-02-23  v2.0.0  Full rewrite — split-pane UI, right-click menu, edit tags modal,
 *                       category management, DB library paths, MusicBrainz art fetch,
 *                       clockwheel weight, DOMContentLoaded fix.
 *  2026-02-21  v1.0.0  Initial scaffold — 3 tabs, basic track list
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/app/inc/auth.php';
require_once __DIR__ . '/app/inc/mc1_config.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/user_auth.php';

$page_title = 'Media Library';
$active_nav = 'media';
$use_charts = false;

/* We pre-load the track count for the header badge */
$total_tracks = 0;
try {
    $total_tracks = (int)mc1_db('mcaster1_media')->query('SELECT COUNT(*) FROM tracks')->fetchColumn();
} catch (Exception $e) {}

/* We load encoder slot options for the playlist load-to-slot dropdown */
$slot_options = [];
try {
    $slot_options = mc1_db('mcaster1_encoder')
        ->query('SELECT slot_id, name FROM encoder_configs ORDER BY slot_id')
        ->fetchAll(\PDO::FETCH_ASSOC);
} catch (Exception $e) {}

/* We pre-load playlists for the Add to Playlist modal dropdown */
$playlists_for_modal = [];
try {
    $playlists_for_modal = mc1_db('mcaster1_media')
        ->query('SELECT id, name, type, track_count FROM playlists ORDER BY name')
        ->fetchAll(\PDO::FETCH_ASSOC);
} catch (Exception $e) {}

/* We load categories for the left sidebar panel */
$categories = [];
try {
    $categories = mc1_db('mcaster1_media')->query(
        'SELECT c.id, c.name, c.color_hex, c.icon, c.type,
                COUNT(tc.track_id) AS track_count
         FROM categories c
         LEFT JOIN track_categories tc ON tc.category_id = c.id
         GROUP BY c.id ORDER BY c.sort_order, c.name'
    )->fetchAll(\PDO::FETCH_ASSOC);
} catch (Exception $e) {}

/* We load extra library paths from the DB */
$extra_library_paths = [];
try {
    $extra_library_paths = mc1_db('mcaster1_media')
        ->query('SELECT id, path, label FROM media_library_paths WHERE is_active = 1 ORDER BY date_added')
        ->fetchAll(\PDO::FETCH_ASSOC);
} catch (Exception $e) {}

require_once __DIR__ . '/app/inc/header.php';

/* We build the slot options HTML once in PHP for injection into JS */
$slot_opts_html = '';
foreach ($slot_options ?: [['slot_id' => 1, 'name' => 'Encoder 1']] as $s) {
    $slot_opts_html .= '<option value="' . h($s['slot_id']) . '">Slot ' . h($s['slot_id']) . ' — ' . h($s['name']) . '</option>';
}
?>

<style>
/* ── Media Library Layout ─────────────────────────────────────────────── */
.media-layout    { display:flex; gap:0; height:calc(100vh - 220px); min-height:460px; overflow:hidden; }
.ftree-pane      { width:260px; min-width:180px; max-width:360px; border-right:1px solid var(--border);
                   overflow-y:auto; padding:6px 0; flex-shrink:0; background:var(--bg2); }
.track-pane      { flex:1; overflow:auto; display:flex; flex-direction:column; min-width:0; }
.track-pane-top  { padding:10px 14px 8px; border-bottom:1px solid var(--border); background:var(--bg2);
                   display:flex; align-items:center; gap:8px; flex-wrap:wrap; }
.track-pane-body { flex:1; overflow-y:auto; }

/* ── Folder Tree ──────────────────────────────────────────────────────── */
.ftree-section   { padding:6px 8px 2px; font-size:10px; font-weight:700; color:var(--muted);
                   text-transform:uppercase; letter-spacing:.06em; }
.ftree ul        { list-style:none; padding:0; margin:0; }
.ftree li        { padding:0; margin:0; }
.ftree-item      { display:flex; align-items:center; gap:5px; padding:5px 8px 5px 10px;
                   cursor:pointer; font-size:12.5px; color:var(--text); white-space:nowrap;
                   overflow:hidden; text-overflow:ellipsis; user-select:none; border-radius:0; }
.ftree-item:hover { background:var(--bg3); }
.ftree-item.active { background:rgba(20,184,166,.15); color:var(--teal); font-weight:500; }
.ftree-arrow     { width:12px; text-align:center; flex-shrink:0; font-size:9px;
                   transition:transform .12s; color:var(--muted); }
.ftree-arrow.open{ transform:rotate(90deg); }
.ftree-icon      { width:14px; text-align:center; flex-shrink:0; font-size:11px; color:var(--muted); }
.ftree-label     { flex:1; overflow:hidden; text-overflow:ellipsis; }
.ftree-cnt       { font-size:10px; color:var(--muted); margin-left:auto; padding-left:4px; }
.ftree-children  { display:none; padding-left:14px; }
.ftree-children.open { display:block; }
.ftree-add-btn   { display:flex; align-items:center; gap:5px; padding:4px 8px 4px 24px;
                   cursor:pointer; font-size:11px; color:var(--teal); }
.ftree-add-btn:hover { color:var(--teal-bright, #2dd4bf); }
.cat-dot         { width:8px; height:8px; border-radius:50%; flex-shrink:0; }

/* ── Track Table ──────────────────────────────────────────────────────── */
.art-thumb       { width:38px; height:38px; object-fit:cover; border-radius:4px; vertical-align:middle; }
.art-ph          { width:38px; height:38px; border-radius:4px; background:var(--bg3); display:inline-flex;
                   align-items:center; justify-content:center; color:var(--muted); font-size:14px; vertical-align:middle; }
td.td-art        { width:46px; padding:4px 4px 4px 10px; }
td.td-title      { max-width:220px; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
td.td-artist     { max-width:160px; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
td.td-album      { max-width:160px; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.star-disp       { color:#f59e0b; font-size:11px; letter-spacing:1px; }

/* ── Right-Click Context Menu ─────────────────────────────────────────── */
#ctx-menu        { position:fixed; z-index:9999; background:var(--card); border:1px solid var(--border);
                   border-radius:7px; box-shadow:0 8px 28px rgba(0,0,0,.45); min-width:190px;
                   padding:5px 0; display:none; }
.ctx-item        { display:flex; align-items:center; gap:10px; padding:8px 16px;
                   cursor:pointer; font-size:13px; color:var(--text); }
.ctx-item:hover  { background:var(--bg3); }
.ctx-item .ci-icon { width:16px; text-align:center; }
.ctx-item.danger { color:var(--red, #ef4444); }
hr.ctx-sep       { border:none; border-top:1px solid var(--border); margin:4px 0; }

/* ── Modals ───────────────────────────────────────────────────────────── */
.mc1-modal-bg    { position:fixed; inset:0; background:rgba(0,0,0,.6); z-index:8000;
                   display:none; align-items:center; justify-content:center; }
.mc1-modal-bg.open { display:flex; }
.mc1-modal       { background:var(--card); border:1px solid var(--border); border-radius:10px;
                   width:min(640px, 96vw); max-height:90vh; overflow-y:auto;
                   box-shadow:0 16px 48px rgba(0,0,0,.5); }
.mc1-modal-hdr   { display:flex; align-items:center; justify-content:space-between;
                   padding:16px 20px 12px; border-bottom:1px solid var(--border); }
.mc1-modal-title { font-size:16px; font-weight:600; color:var(--text); }
.mc1-modal-body  { padding:18px 20px; }
.mc1-modal-ftr   { padding:14px 20px; border-top:1px solid var(--border);
                   display:flex; justify-content:flex-end; gap:8px; }
.form-row        { display:grid; gap:12px; margin-bottom:12px; }
.form-row.cols-2 { grid-template-columns:1fr 1fr; }
.form-row.cols-3 { grid-template-columns:1fr 1fr 1fr; }
.form-group      { display:flex; flex-direction:column; gap:4px; }
.form-label      { font-size:12px; font-weight:500; color:var(--muted); }
.chk-row         { display:flex; align-items:center; gap:6px; font-size:13px; cursor:pointer; }
.chk-row input   { width:15px; height:15px; cursor:pointer; accent-color:var(--teal); }

/* ── Star Rating Selector ─────────────────────────────────────────────── */
.star-rating     { display:flex; gap:5px; cursor:pointer; font-size:22px; }
.star-rating .s  { color:var(--muted); transition:color .1s; }
.star-rating .s.on, .star-rating .s.hover { color:#f59e0b; }

/* ── Weight Slider ────────────────────────────────────────────────────── */
.weight-wrap     { display:flex; align-items:center; gap:10px; }
.weight-wrap input[type=range] { flex:1; accent-color:var(--teal); }
.weight-val      { min-width:32px; text-align:right; font-variant-numeric:tabular-nums;
                   font-size:14px; font-weight:600; color:var(--teal); }

/* ── Color Picker Strip ───────────────────────────────────────────────── */
.color-strip     { display:flex; gap:6px; flex-wrap:wrap; }
.color-swatch    { width:24px; height:24px; border-radius:50%; cursor:pointer; border:2px solid transparent; }
.color-swatch.sel { border-color:var(--text); }

/* ── Path Manager ─────────────────────────────────────────────────────── */
.path-item       { display:flex; align-items:center; gap:8px; padding:6px 0;
                   border-bottom:1px solid var(--border); font-size:13px; }
.path-label      { flex:1; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.path-sub        { font-size:11px; color:var(--muted); }

/* ── Pagination ───────────────────────────────────────────────────────── */
.pagination      { display:flex; align-items:center; gap:4px; padding:10px 14px;
                   border-top:1px solid var(--border); background:var(--bg2); flex-shrink:0; }
.pagination a, .pagination span { padding:3px 8px; border-radius:4px; font-size:12px; }
.pagination a    { color:var(--teal); cursor:pointer; }
.pagination a:hover { background:var(--bg3); }
.pagination .cur { background:var(--teal); color:#000; font-weight:600; }
.pagination .ellipsis { color:var(--muted); }

/* ── Multi-select drag ───────────────────────────────────────────────── */
.track-row.selected td { background:rgba(20,184,166,.1) !important; }
.track-row.selected    { outline:1px solid rgba(20,184,166,.25); }
.sel-bar { position:fixed; bottom:0; left:var(--sidebar-w); right:0;
           background:var(--card); border-top:2px solid var(--teal); padding:10px 20px;
           display:flex; align-items:center; gap:10px; z-index:7000;
           box-shadow:0 -4px 20px rgba(0,0,0,.4); }

/* ── Category context menu ───────────────────────────────────────────── */
#cat-ctx-menu { position:fixed; z-index:9999; background:var(--card); border:1px solid var(--border);
                border-radius:7px; box-shadow:0 8px 28px rgba(0,0,0,.45); min-width:170px;
                padding:5px 0; display:none; }

/* ── Folder context menu ─────────────────────────────────────────────── */
#folder-ctx-menu { position:fixed; z-index:9999; background:var(--card); border:1px solid var(--border);
                   border-radius:7px; box-shadow:0 8px 28px rgba(0,0,0,.45); min-width:190px;
                   padding:5px 0; display:none; }

/* ── Media tabs card: allow content to flow freely, no clipping ────── */
/* The global .card rule has overflow:hidden (for border-radius clipping).
 * That causes the tab panes to be cut off when taller than the viewport.
 * We override it here so the card grows naturally and .main scrolls. */
[data-tabs-container="1"] { overflow: visible !important; }
[data-tabs-container="1"] > .tabs { border-radius: var(--radius) var(--radius) 0 0; overflow: hidden; }

/* ── Batch Upload Tab ────────────────────────────────────────────────── */
#upload-dropzone         { border:2px dashed var(--border); border-radius:10px; padding:36px 20px;
                           text-align:center; cursor:pointer; margin-bottom:14px;
                           transition:border-color .15s, background .15s; }
#upload-dropzone.dragover{ border-color:var(--teal); background:rgba(20,184,166,.06); }
.uprog-row               { display:flex; align-items:flex-start; gap:8px; padding:6px 4px;
                           font-size:12px; }
.uprog-row + .uprog-row  { border-top:1px solid var(--border); }
.uprog-icon              { flex-shrink:0; width:16px; text-align:center; margin-top:1px; }
.uprog-name              { flex:1; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.uprog-size              { flex-shrink:0; color:var(--muted); }
.uprog-bar-wrap          { height:3px; background:var(--bg3); border-radius:2px; margin-top:4px; }
.uprog-bar-fill          { height:100%; border-radius:2px; transition:width .1s; }
</style>

<div class="sec-hdr">
  <div class="sec-title"><i class="fa-solid fa-music" style="color:var(--teal);margin-right:8px"></i>Media Library</div>
  <div style="display:flex;align-items:center;gap:8px;flex-wrap:wrap">
    <span class="badge badge-teal" id="track-total-badge"><?= number_format($total_tracks) ?> tracks</span>
    <button class="btn btn-secondary btn-sm" id="sync-lib-btn" onclick="syncLibrary()" title="Scan all audio roots, mark missing files, report duplicates">
      <i class="fa-solid fa-rotate"></i> Sync Library
    </button>
    <button class="btn btn-secondary btn-sm" onclick="removeMissingTracks()" title="Delete all tracks flagged as missing from the database">
      <i class="fa-solid fa-file-circle-xmark"></i> Remove Missing
    </button>
    <button class="btn btn-danger btn-sm" onclick="showWipeLibraryModal()" title="Mass delete all tracks from database">
      <i class="fa-solid fa-triangle-exclamation"></i> Wipe Library
    </button>
  </div>
</div>
<div id="sync-result-bar" style="display:none;background:var(--bg3);border-radius:6px;padding:8px 14px;margin-bottom:10px;font-size:12px;display:flex;gap:16px;flex-wrap:wrap;align-items:center">
  <span id="sync-result-text"></span>
  <span style="margin-left:auto;color:var(--muted);cursor:pointer" onclick="document.getElementById('sync-result-bar').style.display='none'">
    <i class="fa-solid fa-xmark"></i>
  </span>
</div>

<div class="card" style="padding:0;overflow:visible" data-tabs-container="1">
  <div class="tabs" id="media-tabs" style="padding:0 16px;border-bottom:1px solid var(--border)">
    <button class="tab-btn active" data-tab="library">
      <i class="fa-solid fa-list fa-fw"></i> Track Library
    </button>
    <button class="tab-btn" data-tab="scan">
      <i class="fa-solid fa-folder-open fa-fw"></i> Folder Scan
    </button>
    <button class="tab-btn" data-tab="plfiles">
      <i class="fa-solid fa-file-lines fa-fw"></i> Playlist Files
    </button>
    <button class="tab-btn" data-tab="libpaths">
      <i class="fa-solid fa-hard-drive fa-fw"></i> Library Paths
    </button>
    <button class="tab-btn" data-tab="upload" id="upload-tab-btn" onclick="initUploadTab()">
      <i class="fa-solid fa-cloud-arrow-up fa-fw"></i> Batch Upload
    </button>
  </div>

  <!-- ═══════════════════ TAB: TRACK LIBRARY ════════════════════════════ -->
  <div class="tab-pane active" id="tab-library">
    <div class="media-layout">

      <!-- Left pane: Folder tree + Category browser -->
      <div class="ftree-pane" id="ftree-pane">
        <div class="ftree" id="ftree">

          <!-- Folder tree section -->
          <div class="ftree-section">Library Folders</div>
          <ul id="ftree-root">
            <!-- We inject the audio root node statically; children load on expand -->
            <li id="fnode-root">
              <div class="ftree-item" onclick="ftreeToggle('root','<?= addslashes(MC1_AUDIO_ROOT) ?>')"
                   oncontextmenu="folderCtxShow(event,'<?= addslashes(MC1_AUDIO_ROOT) ?>')"
                   data-path="<?= h(MC1_AUDIO_ROOT) ?>">
                <span class="ftree-arrow" id="fa-root">&#9658;</span>
                <span class="ftree-icon"><i class="fa-solid fa-house-chimney"></i></span>
                <span class="ftree-label">Audio Root</span>
              </div>
              <ul class="ftree-children" id="fc-root"></ul>
            </li>
            <?php foreach ($extra_library_paths as $ep): ?>
            <li id="fnode-ep-<?= (int)$ep['id'] ?>">
              <div class="ftree-item"
                   onclick="ftreeToggle('ep-<?= (int)$ep['id'] ?>','<?= addslashes($ep['path']) ?>')"
                   oncontextmenu="folderCtxShow(event,'<?= addslashes($ep['path']) ?>')"
                   data-path="<?= h($ep['path']) ?>">
                <span class="ftree-arrow" id="fa-ep-<?= (int)$ep['id'] ?>">&#9658;</span>
                <span class="ftree-icon"><i class="fa-solid fa-hard-drive"></i></span>
                <span class="ftree-label"><?= h($ep['label'] ?: basename($ep['path'])) ?></span>
              </div>
              <ul class="ftree-children" id="fc-ep-<?= (int)$ep['id'] ?>"></ul>
            </li>
            <?php endforeach; ?>
          </ul>

          <!-- Categories section -->
          <div class="ftree-section" style="margin-top:12px">Categories</div>
          <ul id="cat-list">
            <?php foreach ($categories as $cat): ?>
            <li>
              <div class="ftree-item" onclick="loadCategoryTracks(<?= (int)$cat['id'] ?>,<?= h(json_encode($cat['name'])) ?>)"
                   oncontextmenu="catCtxShow(event,<?= (int)$cat['id'] ?>,<?= h(json_encode($cat['name'])) ?>)"
                   id="cat-item-<?= (int)$cat['id'] ?>"
                   data-cat-id="<?= (int)$cat['id'] ?>">
                <span class="ftree-arrow" style="visibility:hidden"></span>
                <span class="cat-dot" style="background:<?= h($cat['color_hex']) ?>"></span>
                <span class="ftree-label"><?= h($cat['name']) ?></span>
                <span class="ftree-cnt"><?= (int)$cat['track_count'] ?></span>
              </div>
            </li>
            <?php endforeach; ?>
          </ul>
          <div class="ftree-add-btn" onclick="showCreateCategory()">
            <i class="fa-solid fa-plus fa-fw" style="font-size:10px"></i> New Category
          </div>

        </div><!-- /ftree -->
      </div><!-- /ftree-pane -->

      <!-- Right pane: Search bar + track table -->
      <div class="track-pane">
        <div class="track-pane-top">
          <input type="text" class="form-input" id="track-q" style="flex:1;min-width:160px"
                 placeholder="Search title, artist, album, genre…" oninput="debSearch(this.value)">
          <button class="btn btn-primary btn-sm" onclick="loadTracks(currentFolder,1)">
            <i class="fa-solid fa-magnifying-glass"></i>
          </button>
          <button class="btn btn-secondary btn-sm" onclick="clearSearch()">
            <i class="fa-solid fa-xmark"></i>
          </button>
          <div id="track-context-label" style="font-size:12px;color:var(--muted);white-space:nowrap;margin-left:6px">
            All tracks
          </div>
        </div>
        <div class="track-pane-body">
          <div id="track-tbl"><div class="empty" style="padding:48px"><div class="spinner"></div></div></div>
        </div>
        <div id="track-pages" class="pagination"></div>
      </div>

    </div><!-- /media-layout -->
  </div>

  <!-- ═══════════════════ TAB: FOLDER SCAN ══════════════════════════════ -->
  <div class="tab-pane" id="tab-scan" style="padding:18px 20px">
    <div class="alert alert-info" style="margin-bottom:14px">
      <i class="fa-solid fa-circle-info"></i>
      Recursively scans a directory for audio files (mp3, ogg, flac, opus, aac, m4a, wav)
      and adds them to the library database. Already-known files are skipped gracefully.
    </div>
    <div class="form-group" style="margin-bottom:14px">
      <label class="form-label">Directory Path</label>
      <div style="display:flex;gap:8px">
        <input type="text" class="form-input" id="scan-dir"
               value="<?= h(MC1_AUDIO_ROOT) ?>"
               placeholder="/path/to/audio">
        <button class="btn btn-primary" id="scan-btn" onclick="doScan()">
          <i class="fa-solid fa-folder-open"></i> Scan
        </button>
      </div>
      <div class="form-hint" style="margin-top:6px">
        Quick:
        <a href="#" onclick="document.getElementById('scan-dir').value='<?= addslashes(MC1_AUDIO_ROOT) ?>';return false">
          Audio Root (<?= h(MC1_AUDIO_ROOT) ?>)
        </a>
        <?php foreach (MC1_AUDIO_GENRE_DIRS as $gd): ?>
        &nbsp; <a href="#" onclick="document.getElementById('scan-dir').value='<?= addslashes($gd) ?>';return false">
          <?= h(basename($gd)) ?>
        </a>
        <?php endforeach; ?>
        <?php foreach ($extra_library_paths as $ep): ?>
        &nbsp; <a href="#" onclick="document.getElementById('scan-dir').value='<?= addslashes($ep['path']) ?>';return false">
          <?= h($ep['label'] ?: basename($ep['path'])) ?>
        </a>
        <?php endforeach; ?>
      </div>
    </div>
    <div id="scan-res"></div>
  </div>

  <!-- ═══════════════════ TAB: PLAYLIST FILES ═══════════════════════════ -->
  <div class="tab-pane" id="tab-plfiles" style="padding:18px 20px">
    <div class="alert alert-info" style="margin-bottom:12px">
      <i class="fa-solid fa-circle-info"></i>
      We discover .m3u, .pls, .xspf and .txt playlist files in
      <code><?= h(MC1_PLAYLIST_DIR) ?></code> and <code><?= h(MC1_AUDIO_ROOT) ?></code>.
      Select a slot and click Load to send the playlist to an encoder.
    </div>
    <div style="display:flex;justify-content:flex-end;margin-bottom:10px">
      <button class="btn btn-secondary btn-sm" onclick="loadPfList()">
        <i class="fa-solid fa-rotate-right"></i> Refresh
      </button>
    </div>
    <div id="pf-tbl"><div class="empty"><div class="spinner"></div></div></div>
  </div>

  <!-- ═══════════════════ TAB: LIBRARY PATHS ════════════════════════════ -->
  <div class="tab-pane" id="tab-libpaths" style="padding:18px 20px">
    <div class="sec-hdr" style="margin-bottom:14px">
      <div>
        <div class="sec-title" style="font-size:15px">Media Library Scan Paths</div>
        <div class="form-hint">
          We store additional scan directories in the database. No YAML editing required.
          Only the primary audio root is set at startup; everything else lives here.
        </div>
      </div>
      <button class="btn btn-primary btn-sm" onclick="showAddPath()">
        <i class="fa-solid fa-plus"></i> Add Path
      </button>
    </div>

    <div id="libpath-list">
      <!-- Built-in paths (read-only) -->
      <div class="path-item">
        <span class="ftree-icon"><i class="fa-solid fa-lock" style="color:var(--muted)"></i></span>
        <div class="path-label">
          <div><?= h(MC1_AUDIO_ROOT) ?></div>
          <div class="path-sub">Primary audio root (built-in)</div>
        </div>
        <span class="badge badge-gray" style="font-size:10px">Built-in</span>
      </div>
      <?php foreach (MC1_AUDIO_GENRE_DIRS as $gd): ?>
      <div class="path-item">
        <span class="ftree-icon"><i class="fa-solid fa-lock" style="color:var(--muted)"></i></span>
        <div class="path-label">
          <div><?= h($gd) ?></div>
          <div class="path-sub">Genre subdirectory (built-in)</div>
        </div>
        <span class="badge badge-gray" style="font-size:10px">Built-in</span>
      </div>
      <?php endforeach; ?>

      <!-- DB-stored paths (removable) -->
      <div id="db-path-list">
        <?php foreach ($extra_library_paths as $ep): ?>
        <div class="path-item" id="pitem-<?= (int)$ep['id'] ?>">
          <span class="ftree-icon"><i class="fa-solid fa-hard-drive" style="color:var(--teal)"></i></span>
          <div class="path-label">
            <div><?= h($ep['path']) ?></div>
            <div class="path-sub"><?= h($ep['label'] ?: '') ?></div>
          </div>
          <button class="btn btn-danger btn-xs" onclick="removePath(<?= (int)$ep['id'] ?>)">
            <i class="fa-solid fa-trash"></i>
          </button>
        </div>
        <?php endforeach; ?>
      </div>
    </div>

    <!-- Add path inline form -->
    <div id="add-path-form" style="display:none;margin-top:16px;padding:14px;background:var(--bg3);border-radius:8px">
      <div class="form-row cols-2" style="margin-bottom:10px">
        <div class="form-group">
          <label class="form-label">Server Path</label>
          <input type="text" class="form-input" id="new-path-val" placeholder="/home/username/Music">
        </div>
        <div class="form-group">
          <label class="form-label">Label (optional)</label>
          <input type="text" class="form-input" id="new-path-label" placeholder="DJ Bob's Music">
        </div>
      </div>
      <div style="display:flex;gap:8px">
        <button class="btn btn-primary btn-sm" onclick="doAddPath()">
          <i class="fa-solid fa-plus"></i> Add Path
        </button>
        <button class="btn btn-secondary btn-sm" onclick="hideAddPath()">Cancel</button>
      </div>
    </div>
  </div>

  <!-- ═══════════════════ TAB: BATCH UPLOAD ════════════════════════════ -->
  <div class="tab-pane" id="tab-upload" style="padding:18px 20px">

    <div class="sec-hdr" style="margin-bottom:14px">
      <div>
        <div class="sec-title" style="font-size:15px">Batch Upload Audio Files</div>
        <div class="form-hint">Upload audio files from your computer directly to the server's audio library.</div>
      </div>
    </div>

    <!-- Directory picker -->
    <div style="background:var(--bg3);border-radius:8px;padding:14px;margin-bottom:14px">
      <div style="display:flex;align-items:center;gap:10px;flex-wrap:wrap">
        <div style="flex:1;min-width:200px">
          <label class="form-label" style="margin-bottom:4px">
            <i class="fa-solid fa-folder-open" style="color:var(--teal);margin-right:5px"></i>
            Upload Destination
          </label>
          <select class="form-select" id="upload-dir-select" style="width:100%">
            <option value="">Click "Batch Upload" tab to load directories…</option>
          </select>
        </div>
        <div style="display:flex;gap:6px;align-self:flex-end;padding-bottom:1px">
          <button class="btn btn-secondary btn-sm" onclick="loadUploadDirs()" title="Refresh directory list">
            <i class="fa-solid fa-rotate-right"></i>
          </button>
          <button class="btn btn-secondary btn-sm" onclick="showCreateDirForm()" title="Create new subdirectory">
            <i class="fa-solid fa-folder-plus"></i> New Folder
          </button>
        </div>
      </div>
      <!-- Create subfolder inline form -->
      <div id="create-dir-form" style="display:none;margin-top:12px;padding:12px;background:var(--bg2);border-radius:6px;border:1px solid var(--border)">
        <div style="font-size:11px;font-weight:700;color:var(--muted);margin-bottom:8px;text-transform:uppercase;letter-spacing:.05em">
          Create New Subfolder in Selected Directory
        </div>
        <div style="display:flex;gap:8px;align-items:center;flex-wrap:wrap">
          <input type="text" class="form-input" id="new-dir-name" placeholder="Folder name"
                 style="flex:1;min-width:160px;max-width:320px">
          <button class="btn btn-primary btn-sm" onclick="doCreateDir()">
            <i class="fa-solid fa-plus"></i> Create
          </button>
          <button class="btn btn-secondary btn-sm" onclick="hideCreateDirForm()">Cancel</button>
        </div>
        <div style="font-size:11px;margin-top:6px" id="create-dir-hint"></div>
      </div>
    </div>

    <!-- Drop zone -->
    <div id="upload-dropzone">
      <div style="font-size:34px;color:var(--muted);margin-bottom:10px">
        <i class="fa-solid fa-cloud-arrow-up"></i>
      </div>
      <div style="font-size:15px;font-weight:500;margin-bottom:6px">Drag &amp; drop audio files here</div>
      <div style="font-size:12px;color:var(--muted);margin-bottom:14px">
        Drop files <em>or</em> an entire folder here &mdash; MP3, FLAC, WAV, OGG, Opus, AAC, M4A, WMA, AIFF, APE and more
      </div>
      <input type="file" id="upload-file-input" multiple
             accept=".mp3,.flac,.wav,.ogg,.opus,.aac,.m4a,.mp4,.wma,.aiff,.aif,.ape,.alac"
             style="display:none"
             onchange="mc1UploadFiles(this)">
      <input type="file" id="upload-folder-input" webkitdirectory mozdirectory multiple
             style="display:none"
             onchange="mc1UploadFiles(this)">
      <div style="display:flex;gap:8px;justify-content:center">
        <button class="btn btn-secondary btn-sm"
                onclick="document.getElementById('upload-file-input').click();event.stopPropagation()"
                title="Select individual audio files">
          <i class="fa-solid fa-file-audio"></i> Browse Files
        </button>
        <button class="btn btn-secondary btn-sm"
                onclick="document.getElementById('upload-folder-input').click();event.stopPropagation()"
                title="Select an entire folder — navigate to the folder, single-click it, then click Select Folder">
          <i class="fa-solid fa-folder-open"></i> Browse Folder
        </button>
      </div>
      <div style="font-size:11px;color:var(--muted);margin-top:8px">
        <i class="fa-solid fa-circle-info" style="margin-right:4px"></i>
        <strong>Browse Folder:</strong> navigate to the folder, <em>single-click</em> it, then click &ldquo;Select Folder&rdquo; &mdash; don&rsquo;t double-click into it.
        Drag &amp; drop files/folders from Explorer also works.
      </div>
    </div>

    <!-- File queue preview (shown once files are selected) -->
    <div id="upload-queue-wrap" style="display:none">
      <div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:8px">
        <div style="font-size:13px;font-weight:600">
          <i class="fa-solid fa-list" style="color:var(--teal);margin-right:5px"></i>
          <span id="upload-queue-count">0</span> file(s) ready to upload
        </div>
        <div style="display:flex;gap:6px">
          <button class="btn btn-secondary btn-sm" onclick="clearUploadQueue()">
            <i class="fa-solid fa-xmark"></i> Clear
          </button>
          <button class="btn btn-primary btn-sm" id="btn-start-upload" onclick="startBatchUpload()">
            <i class="fa-solid fa-upload"></i> Start Upload
          </button>
        </div>
      </div>
      <div id="upload-queue-list"
           style="background:var(--bg3);border-radius:8px;max-height:280px;overflow-y:auto;padding:8px 10px">
      </div>
      <div id="upload-queue-pager" style="display:flex;justify-content:center;gap:6px;margin-top:8px"></div>
    </div>

  </div><!-- /tab-upload -->

</div><!-- /card -->

<!-- ══════════════════════ MODAL: UPLOAD PROGRESS ════════════════════════ -->
<div class="mc1-modal-bg" id="modal-upload-progress">
  <div class="mc1-modal" style="width:min(720px,96vw)">
    <div class="mc1-modal-hdr">
      <span class="mc1-modal-title">
        <i class="fa-solid fa-cloud-arrow-up" style="color:var(--teal);margin-right:6px"></i>
        Upload Progress
      </span>
      <span id="uprog-summary" style="font-size:12px;color:var(--muted)"></span>
    </div>
    <div class="mc1-modal-body" style="padding:14px 20px">
      <!-- Overall progress bar -->
      <div style="margin-bottom:14px">
        <div style="display:flex;justify-content:space-between;font-size:12px;color:var(--muted);margin-bottom:4px">
          <span id="uprog-label">Uploading…</span>
          <span id="uprog-pct">0%</span>
        </div>
        <div style="height:6px;background:var(--bg3);border-radius:3px;overflow:hidden">
          <div id="uprog-bar" style="height:100%;background:var(--teal);width:0%;transition:width .25s"></div>
        </div>
      </div>
      <!-- Per-file list (25 per page) -->
      <div id="uprog-file-list" style="max-height:380px;overflow-y:auto"></div>
      <!-- Pagination -->
      <div id="uprog-pager" style="display:flex;justify-content:center;gap:6px;margin-top:10px;flex-wrap:wrap"></div>
    </div>
    <div class="mc1-modal-ftr">
      <button class="btn btn-secondary btn-sm" id="uprog-cancel-btn" onclick="cancelUpload()">
        <i class="fa-solid fa-stop"></i> Cancel
      </button>
      <button class="btn btn-secondary btn-sm" id="uprog-scan-btn" style="display:none"
              onclick="triggerLibraryScan()">
        <i class="fa-solid fa-database"></i> Add to Library
      </button>
      <button class="btn btn-primary btn-sm" id="uprog-done-btn" style="display:none"
              onclick="hideModal('modal-upload-progress');clearUploadQueue()">
        <i class="fa-solid fa-check"></i> Done
      </button>
    </div>
  </div>
</div>

<!-- ══════════════════════ RIGHT-CLICK CONTEXT MENU ═══════════════════════ -->
<div id="ctx-menu">
  <div class="ctx-item" onclick="ctxPlayTrack()">
    <span class="ci-icon"><i class="fa-solid fa-play"></i></span> Play Track
  </div>
  <div class="ctx-item" onclick="ctxEditTags()">
    <span class="ci-icon"><i class="fa-solid fa-pen-to-square"></i></span> Edit Tags
  </div>
  <div class="ctx-item" onclick="ctxFetchArt()">
    <span class="ci-icon"><i class="fa-solid fa-image"></i></span> Fetch Album Art
  </div>
  <hr class="ctx-sep">
  <div class="ctx-item" onclick="ctxAddToPlaylist()">
    <span class="ci-icon"><i class="fa-solid fa-list-music"></i></span> Add to Playlist
  </div>
  <div class="ctx-item" onclick="ctxNewPlaylistAdd()">
    <span class="ci-icon"><i class="fa-solid fa-plus"></i></span> New Playlist + Add
  </div>
  <div class="ctx-item" onclick="ctxAddToCategory()">
    <span class="ci-icon"><i class="fa-solid fa-tag"></i></span> Add to Category
  </div>
  <hr class="ctx-sep">
  <div class="ctx-item danger" onclick="ctxDelete()">
    <span class="ci-icon"><i class="fa-solid fa-trash"></i></span> Delete from Library
  </div>
</div>

<!-- ══════════════ CATEGORY RIGHT-CLICK CONTEXT MENU ═══════════════════ -->
<div id="cat-ctx-menu">
  <div class="ctx-item" onclick="catCtxQueue()">
    <span class="ci-icon"><i class="fa-solid fa-headphones"></i></span> Queue in Player
  </div>
  <div class="ctx-item" onclick="catCtxAddToPlaylist()">
    <span class="ci-icon"><i class="fa-solid fa-list-music"></i></span> Add to Playlist…
  </div>
  <hr class="ctx-sep">
  <div class="ctx-item danger" onclick="catCtxDelete()">
    <span class="ci-icon"><i class="fa-solid fa-trash"></i></span> Delete Category
  </div>
</div>

<!-- ═══════════════ FOLDER RIGHT-CLICK CONTEXT MENU ════════════════════ -->
<div id="folder-ctx-menu">
  <div class="ctx-item" onclick="folderCtxScan()">
    <span class="ci-icon"><i class="fa-solid fa-folder-open"></i></span> Scan Folder into Library
  </div>
  <div class="ctx-item" onclick="folderCtxQueue()">
    <span class="ci-icon"><i class="fa-solid fa-headphones"></i></span> Queue in Player
  </div>
  <div class="ctx-item" onclick="folderCtxPlaylist()">
    <span class="ci-icon"><i class="fa-solid fa-list-music"></i></span> Create Playlist from Folder
  </div>
  <hr class="ctx-sep">
  <div class="ctx-item danger" onclick="folderCtxDelete()">
    <span class="ci-icon"><i class="fa-solid fa-trash"></i></span> Remove from Library
  </div>
</div>

<!-- ══════════════════════ MODAL: EDIT TAGS ═══════════════════════════════ -->
<div class="mc1-modal-bg" id="modal-edit">
  <div class="mc1-modal">
    <div class="mc1-modal-hdr">
      <span class="mc1-modal-title"><i class="fa-solid fa-pen-to-square" style="color:var(--teal);margin-right:6px"></i>Edit Track Tags</span>
      <button class="btn btn-secondary btn-sm" onclick="hideModal('modal-edit')">
        <i class="fa-solid fa-xmark"></i>
      </button>
    </div>
    <div class="mc1-modal-body">
      <input type="hidden" id="edit-track-id">
      <div class="form-row cols-2">
        <div class="form-group">
          <label class="form-label">Title</label>
          <input type="text" class="form-input" id="edit-title">
        </div>
        <div class="form-group">
          <label class="form-label">Artist</label>
          <input type="text" class="form-input" id="edit-artist">
        </div>
      </div>
      <div class="form-row cols-2">
        <div class="form-group">
          <label class="form-label">Album</label>
          <input type="text" class="form-input" id="edit-album">
        </div>
        <div class="form-group">
          <label class="form-label">Genre</label>
          <input type="text" class="form-input" id="edit-genre">
        </div>
      </div>
      <div class="form-row cols-3">
        <div class="form-group">
          <label class="form-label">Year</label>
          <input type="number" class="form-input" id="edit-year" min="1900" max="2099" step="1">
        </div>
        <div class="form-group">
          <label class="form-label">BPM</label>
          <input type="number" class="form-input" id="edit-bpm" min="0" max="300" step="0.5">
        </div>
        <div class="form-group">
          <label class="form-label">Energy (0.0–1.0)</label>
          <input type="number" class="form-input" id="edit-energy" min="0" max="1" step="0.01">
        </div>
      </div>
      <div class="form-row cols-2">
        <div class="form-group">
          <label class="form-label">Mood Tag</label>
          <input type="text" class="form-input" id="edit-mood" placeholder="e.g. upbeat, mellow">
        </div>
        <div class="form-group">
          <label class="form-label">Musical Key</label>
          <input type="text" class="form-input" id="edit-key" placeholder="e.g. Cmaj, Am">
        </div>
      </div>
      <div class="form-row cols-2">
        <div class="form-group">
          <label class="form-label">Rating</label>
          <div class="star-rating" id="edit-stars">
            <span class="s" data-v="1">&#9733;</span>
            <span class="s" data-v="2">&#9733;</span>
            <span class="s" data-v="3">&#9733;</span>
            <span class="s" data-v="4">&#9733;</span>
            <span class="s" data-v="5">&#9733;</span>
          </div>
          <input type="hidden" id="edit-rating" value="3">
        </div>
        <div class="form-group">
          <label class="form-label">Track #</label>
          <input type="number" class="form-input" id="edit-tracknum" min="1" max="999">
        </div>
      </div>
      <div class="form-row cols-2">
        <div class="form-group">
          <label class="form-label">Intro (ms)</label>
          <input type="number" class="form-input" id="edit-intro" min="0">
        </div>
        <div class="form-group">
          <label class="form-label">Outro (ms)</label>
          <input type="number" class="form-input" id="edit-outro" min="0">
        </div>
      </div>
      <div class="form-row cols-2">
        <div class="form-group">
          <label class="form-label">Cue In (ms)</label>
          <input type="number" class="form-input" id="edit-cuein" min="0">
        </div>
        <div class="form-group">
          <label class="form-label">Cue Out (ms)</label>
          <input type="number" class="form-input" id="edit-cueout" min="0">
        </div>
      </div>
      <div class="form-row" style="margin-bottom:8px">
        <div style="display:flex;gap:20px;flex-wrap:wrap">
          <label class="chk-row"><input type="checkbox" id="edit-jingle"> Jingle</label>
          <label class="chk-row"><input type="checkbox" id="edit-sweeper"> Sweeper</label>
          <label class="chk-row"><input type="checkbox" id="edit-spot"> Spot/Ad</label>
        </div>
      </div>
      <div style="padding:10px;background:var(--bg3);border-radius:6px;margin-top:8px">
        <label class="chk-row" style="margin-bottom:6px">
          <input type="checkbox" id="edit-write-tags">
          <strong>Write tags to file</strong>
          <span style="font-size:11px;color:var(--muted);margin-left:4px">(uses ffmpeg, no re-encode)</span>
        </label>
        <div style="font-size:11px;color:var(--muted);word-break:break-all" id="edit-filepath"></div>
      </div>
    </div>
    <div class="mc1-modal-ftr">
      <button class="btn btn-secondary" onclick="hideModal('modal-edit')">Cancel</button>
      <button class="btn btn-primary" onclick="saveEditTags()">
        <i class="fa-solid fa-floppy-disk"></i> Save Changes
      </button>
    </div>
  </div>
</div>

<!-- ══════════════════════ MODAL: ADD TO PLAYLIST ═════════════════════════ -->
<div class="mc1-modal-bg" id="modal-addpl">
  <div class="mc1-modal" style="width:min(420px,96vw)">
    <div class="mc1-modal-hdr">
      <span class="mc1-modal-title"><i class="fa-solid fa-list-music" style="color:var(--teal);margin-right:6px"></i>Add to Playlist</span>
      <button class="btn btn-secondary btn-sm" onclick="hideModal('modal-addpl')">
        <i class="fa-solid fa-xmark"></i>
      </button>
    </div>
    <div class="mc1-modal-body">
      <div class="form-group" style="margin-bottom:14px">
        <label class="form-label">Playlist</label>
        <select class="form-select" id="addpl-select">
          <?php foreach ($playlists_for_modal as $pl): ?>
          <option value="<?= (int)$pl['id'] ?>"><?= h($pl['name']) ?> (<?= (int)$pl['track_count'] ?> tracks)</option>
          <?php endforeach; ?>
        </select>
      </div>
      <div class="form-group">
        <label class="form-label">
          Rotation Weight
          <span style="font-size:10px;color:var(--muted);margin-left:4px">— clockwheel playback frequency</span>
        </label>
        <div class="weight-wrap">
          <span style="font-size:11px;color:var(--muted)">Rare<br>0.1</span>
          <input type="range" id="addpl-weight" min="0.1" max="10" step="0.1" value="1.0"
                 oninput="document.getElementById('addpl-weight-val').textContent=parseFloat(this.value).toFixed(1)">
          <span style="font-size:11px;color:var(--muted)">Often<br>10.0</span>
          <span class="weight-val" id="addpl-weight-val">1.0</span>
        </div>
        <div style="font-size:11px;color:var(--muted);margin-top:4px">
          Weight 1.0 = normal rotation &nbsp; 3.0 = plays 3× as often &nbsp; 0.3 = rarely selected
        </div>
      </div>
    </div>
    <div class="mc1-modal-ftr">
      <button class="btn btn-secondary" onclick="hideModal('modal-addpl')">Cancel</button>
      <button class="btn btn-primary" onclick="doAddToPlaylist()">
        <i class="fa-solid fa-plus"></i> Add Track
      </button>
    </div>
  </div>
</div>

<!-- ══════════════════════ MODAL: NEW PLAYLIST + ADD ══════════════════════ -->
<div class="mc1-modal-bg" id="modal-newpl">
  <div class="mc1-modal" style="width:min(420px,96vw)">
    <div class="mc1-modal-hdr">
      <span class="mc1-modal-title"><i class="fa-solid fa-plus" style="color:var(--teal);margin-right:6px"></i>New Playlist</span>
      <button class="btn btn-secondary btn-sm" onclick="hideModal('modal-newpl')">
        <i class="fa-solid fa-xmark"></i>
      </button>
    </div>
    <div class="mc1-modal-body">
      <div class="form-group" style="margin-bottom:12px">
        <label class="form-label">Playlist Name</label>
        <input type="text" class="form-input" id="newpl-name" placeholder="Drive Time Classics">
      </div>
      <div class="form-row cols-2">
        <div class="form-group">
          <label class="form-label">Type</label>
          <select class="form-select" id="newpl-type">
            <option value="static">Static</option>
            <option value="clock">Clock-based</option>
            <option value="smart">Smart</option>
          </select>
        </div>
        <div class="form-group">
          <label class="form-label">Rotation Weight</label>
          <div class="weight-wrap">
            <input type="range" id="newpl-weight" min="0.1" max="10" step="0.1" value="1.0"
                   oninput="document.getElementById('newpl-weight-val').textContent=parseFloat(this.value).toFixed(1)">
            <span class="weight-val" id="newpl-weight-val">1.0</span>
          </div>
        </div>
      </div>
      <div class="form-group">
        <label class="form-label">Description</label>
        <input type="text" class="form-input" id="newpl-desc" placeholder="Optional description">
      </div>
    </div>
    <div class="mc1-modal-ftr">
      <button class="btn btn-secondary" onclick="hideModal('modal-newpl')">Cancel</button>
      <button class="btn btn-primary" onclick="doNewPlaylistAdd()">
        <i class="fa-solid fa-plus"></i> Create &amp; Add
      </button>
    </div>
  </div>
</div>

<!-- ══════════════════════ MODAL: DELETE CONFIRM ══════════════════════════ -->
<div class="mc1-modal-bg" id="modal-del">
  <div class="mc1-modal" style="width:min(380px,96vw)">
    <div class="mc1-modal-hdr">
      <span class="mc1-modal-title" style="color:var(--red,#ef4444)">
        <i class="fa-solid fa-trash" style="margin-right:6px"></i>Delete Track
      </span>
      <button class="btn btn-secondary btn-sm" onclick="hideModal('modal-del')">
        <i class="fa-solid fa-xmark"></i>
      </button>
    </div>
    <div class="mc1-modal-body">
      <p style="margin:0 0 14px">Remove <strong id="del-track-name"></strong> from the library?</p>
      <label class="chk-row" style="color:var(--red,#ef4444)">
        <input type="checkbox" id="del-also-file">
        <strong>Also permanently delete the file from disk</strong>
        <span style="font-size:11px;color:var(--muted);margin-left:4px">(cannot be undone)</span>
      </label>
    </div>
    <div class="mc1-modal-ftr">
      <button class="btn btn-secondary" onclick="hideModal('modal-del')">Cancel</button>
      <button class="btn btn-danger" onclick="doDeleteTrack()">
        <i class="fa-solid fa-trash"></i> Delete
      </button>
    </div>
  </div>
</div>

<!-- ══════════════════════ MODAL: ADD TO CATEGORY ═════════════════════════ -->
<div class="mc1-modal-bg" id="modal-addcat">
  <div class="mc1-modal" style="width:min(380px,96vw)">
    <div class="mc1-modal-hdr">
      <span class="mc1-modal-title"><i class="fa-solid fa-tag" style="color:var(--teal);margin-right:6px"></i>Add to Category</span>
      <button class="btn btn-secondary btn-sm" onclick="hideModal('modal-addcat')">
        <i class="fa-solid fa-xmark"></i>
      </button>
    </div>
    <div class="mc1-modal-body">
      <div class="form-group">
        <label class="form-label">Category</label>
        <select class="form-select" id="addcat-select"></select>
      </div>
    </div>
    <div class="mc1-modal-ftr">
      <button class="btn btn-secondary" onclick="hideModal('modal-addcat')">Cancel</button>
      <button class="btn btn-primary" onclick="doAddToCategory()">
        <i class="fa-solid fa-plus"></i> Add to Category
      </button>
    </div>
  </div>
</div>

<!-- ══════════════════════ MODAL: CREATE CATEGORY ═════════════════════════ -->
<div class="mc1-modal-bg" id="modal-newcat">
  <div class="mc1-modal" style="width:min(420px,96vw)">
    <div class="mc1-modal-hdr">
      <span class="mc1-modal-title"><i class="fa-solid fa-tag" style="color:var(--teal);margin-right:6px"></i>New Category</span>
      <button class="btn btn-secondary btn-sm" onclick="hideModal('modal-newcat')">
        <i class="fa-solid fa-xmark"></i>
      </button>
    </div>
    <div class="mc1-modal-body">
      <div class="form-group" style="margin-bottom:12px">
        <label class="form-label">Category Name</label>
        <input type="text" class="form-input" id="newcat-name" placeholder="e.g. Drive Time, Sweepers, Holiday">
      </div>
      <div class="form-group" style="margin-bottom:12px">
        <label class="form-label">Color</label>
        <div class="color-strip" id="newcat-color-strip">
          <?php
          $cat_colors = ['#14b8a6','#3b82f6','#8b5cf6','#f59e0b','#ef4444','#10b981','#f97316','#ec4899','#6366f1','#84cc16'];
          foreach ($cat_colors as $ci => $cc):
          ?>
          <div class="color-swatch <?= $ci === 0 ? 'sel' : '' ?>"
               style="background:<?= $cc ?>"
               data-color="<?= $cc ?>"
               onclick="selectColor(this)"></div>
          <?php endforeach; ?>
        </div>
        <input type="hidden" id="newcat-color" value="#14b8a6">
      </div>
    </div>
    <div class="mc1-modal-ftr">
      <button class="btn btn-secondary" onclick="hideModal('modal-newcat')">Cancel</button>
      <button class="btn btn-primary" onclick="doCreateCategory()">
        <i class="fa-solid fa-plus"></i> Create Category
      </button>
    </div>
  </div>
</div>

<!-- ══════════════════ MODAL: WIPE LIBRARY ════════════════════════════════ -->
<div class="mc1-modal-bg" id="modal-wipe">
  <div class="mc1-modal" style="max-width:480px">
    <div class="mc1-modal-hdr">
      <span class="mc1-modal-title" style="color:var(--red)">
        <i class="fa-solid fa-triangle-exclamation" style="margin-right:6px"></i>Wipe Entire Library
      </span>
      <button class="btn btn-secondary btn-sm" onclick="hideModal('modal-wipe')">
        <i class="fa-solid fa-xmark"></i>
      </button>
    </div>
    <div class="mc1-modal-body">
      <div class="alert alert-error" style="margin-bottom:14px">
        <i class="fa-solid fa-triangle-exclamation"></i>
        <div>
          <strong>This permanently deletes ALL tracks from the database.</strong><br>
          Physical audio files on disk are <strong>NOT</strong> deleted — only the library records.
          Playlists and categories will be emptied. This cannot be undone.
        </div>
      </div>
      <div class="form-group">
        <label class="form-label">Type <strong>WIPE</strong> to confirm</label>
        <input type="text" class="form-input" id="wipe-confirm-input"
               placeholder="Type WIPE here" autocomplete="off">
      </div>
    </div>
    <div class="mc1-modal-ftr">
      <button class="btn btn-secondary" onclick="hideModal('modal-wipe')">Cancel</button>
      <button class="btn btn-danger" onclick="doWipeLibrary()">
        <i class="fa-solid fa-trash"></i> Delete All Library Records
      </button>
    </div>
  </div>
</div>

<script>
(function() {
/* ── State ──────────────────────────────────────────────────────────────── */
var currentFolder  = '';        // We track the currently selected folder path
var currentCatId   = null;      // We track the currently selected category id
var currentPage    = 1;
var curQ           = '';
var debTimer       = null;
var ctxTrackId     = null;      // We store the track under the right-click cursor
var ctxTrackData   = {};        // We store full track JSON for edit modal pre-fill
var ctxCatId       = null;      // We store the category under the right-click cursor
var ctxCatName     = '';        // We store the category name for confirmations
var selIds         = new Set(); // We track multi-selected track IDs
var selDragging    = false;     // We track whether a drag-select is in progress
var allCategories  = <?= json_encode(array_values($categories)) ?>;
var SLOT_OPTS      = <?= json_encode($slot_opts_html) ?>;
var MC1_AUDIO_ROOT_JS = <?= json_encode(defined('MC1_AUDIO_ROOT') ? MC1_AUDIO_ROOT : '') ?>;

/* ── Tab switching ──────────────────────────────────────────────────────── */
document.getElementById('media-tabs').querySelectorAll('.tab-btn').forEach(function(btn) {
    btn.addEventListener('click', function() {
        document.getElementById('media-tabs').querySelectorAll('.tab-btn')
            .forEach(function(b) { b.classList.remove('active'); });
        btn.classList.add('active');
        ['library','scan','plfiles','libpaths','upload'].forEach(function(t) {
            var p = document.getElementById('tab-' + t);
            if (p) p.classList.toggle('active', t === btn.dataset.tab);
        });
        mc1State.set('media', 'tab', btn.dataset.tab);
        if (btn.dataset.tab === 'plfiles') loadPfList();
        if (btn.dataset.tab === 'libpaths') refreshLibPaths();
        if (btn.dataset.tab === 'upload')   initUploadTab();
    });
});

/* ── Folder tree ────────────────────────────────────────────────────────── */
window.ftreeToggle = function(nodeId, path) {
    var arrow    = document.getElementById('fa-' + nodeId);
    var children = document.getElementById('fc-' + nodeId);
    if (!children || !arrow) return;

    var isOpen = children.classList.contains('open');
    if (isOpen) {
        children.classList.remove('open');
        arrow.classList.remove('open');
        return;
    }

    /* We set active state on this folder item and load its tracks */
    setActiveFolder(path);

    /* We only load children if the container is empty */
    if (children.innerHTML.trim() !== '') {
        children.classList.add('open');
        arrow.classList.add('open');
        return;
    }

    children.innerHTML = '<li style="padding:4px 8px 4px 24px;font-size:11px;color:var(--muted)"><div class="spinner" style="width:12px;height:12px"></div></li>';
    children.classList.add('open');
    arrow.classList.add('open');

    mc1Api('POST', '/app/api/tracks.php', {action: 'folders', path: path}).then(function(d) {
        children.innerHTML = '';
        if (!d.ok || !d.folders || !d.folders.length) {
            children.innerHTML = '<li style="padding:4px 8px 4px 24px;font-size:11px;color:var(--muted);font-style:italic">Empty folder</li>';
            return;
        }
        d.folders.filter(function(f) { return !deletedFolders.has(f.path); }).forEach(function(f) {
            var nid = 'n-' + Math.random().toString(36).substr(2,8);
            var li  = document.createElement('li');
            li.id   = 'fnode-' + nid;
            var hasArrow = f.has_children ? '&#9658;' : '&nbsp;';
            li.innerHTML =
                '<div class="ftree-item" onclick="ftreeToggle(\'' + nid + '\',\'' + esc(f.path) + '\')"'
                + ' oncontextmenu="folderCtxShow(event,\'' + esc(f.path) + '\')"'
                + ' data-path="' + esc(f.path) + '">'
                + '<span class="ftree-arrow" id="fa-' + nid + '">' + hasArrow + '</span>'
                + '<span class="ftree-icon"><i class="fa-solid fa-folder"></i></span>'
                + '<span class="ftree-label">' + esc(f.name) + '</span>'
                + (f.track_count > 0 ? '<span class="ftree-cnt">' + f.track_count + '</span>' : '')
                + '</div>'
                + '<ul class="ftree-children" id="fc-' + nid + '"></ul>';
            children.appendChild(li);
        });
    }).catch(function() {
        children.innerHTML = '<li style="padding:4px 8px 4px 24px;font-size:11px;color:var(--red)">Failed to load</li>';
    });
};

function setActiveFolder(path) {
    /* We clear category selection and set folder as active */
    currentFolder = path;
    currentCatId  = null;
    mc1State.set('media', 'folder', path);
    mc1State.set('media', 'catId', null);
    mc1State.del('media', 'search');
    document.querySelectorAll('.ftree-item').forEach(function(el) {
        el.classList.toggle('active', el.getAttribute('data-path') === path);
    });
    document.querySelectorAll('[id^="cat-item-"]').forEach(function(el) {
        el.classList.remove('active');
    });
    var label = path.split('/').pop() || 'Audio Root';
    document.getElementById('track-context-label').textContent = label;
    loadTracks(path, 1);
}

/* ── Category browsing ──────────────────────────────────────────────────── */
window.loadCategoryTracks = function(catId, catName) {
    currentCatId  = catId;
    currentFolder = '';
    currentPage   = 1;
    mc1State.set('media', 'catId', catId);
    mc1State.set('media', 'folder', '');
    mc1State.set('media', 'page', 1);
    document.querySelectorAll('.ftree-item').forEach(function(el) { el.classList.remove('active'); });
    var ci = document.getElementById('cat-item-' + catId);
    if (ci) ci.classList.add('active');
    document.getElementById('track-context-label').textContent = catName + ' (category)';
    fetchCategoryTracks(catId, 1);
};

function fetchCategoryTracks(catId, page) {
    var wrap = document.getElementById('track-tbl');
    wrap.innerHTML = '<div class="empty" style="padding:48px"><div class="spinner"></div></div>';
    mc1Api('POST', '/app/api/tracks.php', {action: 'list_category_tracks', category_id: catId, page: page, limit: 50})
    .then(function(d) {
        renderTrackList(wrap, d);
        currentPage = page;
    }).catch(function() { wrap.innerHTML = '<div class="alert alert-error">Request failed</div>'; });
}

/* ── Track list ─────────────────────────────────────────────────────────── */
window.debSearch = function(v) {
    clearTimeout(debTimer);
    debTimer = setTimeout(function() { loadTracks(currentFolder, 1, v); }, 380);
};
window.clearSearch = function() {
    document.getElementById('track-q').value = '';
    curQ = '';
    if (currentCatId) fetchCategoryTracks(currentCatId, 1);
    else loadTracks(currentFolder, 1, '');
};

window.loadTracks = function(folder, page, q) {
    if (q !== undefined) { curQ = q; mc1State.set('media', 'search', q); }
    currentFolder = folder || '';
    currentPage   = page || 1;
    mc1State.set('media', 'folder', currentFolder);
    mc1State.set('media', 'page', currentPage);
    var wrap = document.getElementById('track-tbl');
    wrap.innerHTML = '<div class="empty" style="padding:48px"><div class="spinner"></div></div>';
    mc1Api('POST', '/app/api/tracks.php', {
        action: 'list', page: currentPage, limit: 50,
        q: curQ, folder: currentFolder
    }).then(function(d) {
        renderTrackList(wrap, d);
        refreshTotalBadge();
    }).catch(function() { wrap.innerHTML = '<div class="alert alert-error">Request failed</div>'; });
};

function renderTrackList(wrap, d) {
    if (!d.ok) {
        wrap.innerHTML = '<div class="alert alert-error"><i class="fa-solid fa-xmark"></i> ' + esc(d.error || 'Error') + '</div>';
        document.getElementById('track-pages').innerHTML = '';
        return;
    }
    if (!d.data || !d.data.length) {
        wrap.innerHTML = '<div class="empty" style="padding:48px"><i class="fa-solid fa-music fa-2x" style="margin-bottom:10px"></i><p>No tracks found</p></div>';
        document.getElementById('track-pages').innerHTML = '';
        return;
    }

    var rows = d.data.map(function(t) {
        var dur   = t.duration_ms ? fmtDur(t.duration_ms) : '—';
        var stars = '';
        for (var i = 1; i <= 5; i++) { stars += (i <= (t.rating||0)) ? '★' : '☆'; }
        var art = t.art_url
            ? '<img class="art-thumb" src="' + esc(t.art_url) + '" onerror="this.outerHTML=\'<span class=art-ph><i class=fa-solid\\ fa-music></i></span>\'">'
            : '<span class="art-ph"><i class="fa-solid fa-music"></i></span>';
        var badges = '';
        if (t.is_jingle)  badges += '<span class="badge badge-gray" style="font-size:9px">Jingle</span>';
        if (t.is_sweeper) badges += '<span class="badge badge-gray" style="font-size:9px">Sweep</span>';
        if (t.is_spot)    badges += '<span class="badge badge-gray" style="font-size:9px">Spot</span>';
        if (t.is_missing) badges += '<span class="badge badge-red"  style="font-size:9px">Missing</span>';
        return '<tr class="track-row" data-id="' + t.id + '" data-t="' + esc(JSON.stringify(t)) + '">'
            + '<td class="td-art">' + art + '</td>'
            + '<td class="td-title" title="' + esc(t.file_path||'') + '">' + esc(t.title||'—') + ' ' + badges + '</td>'
            + '<td class="td-artist">' + esc(t.artist||'—') + '</td>'
            + '<td class="td-album">'  + esc(t.album||'—')  + '</td>'
            + '<td>' + esc(t.genre||'—') + '</td>'
            + '<td style="white-space:nowrap">' + dur + '</td>'
            + '<td>' + (t.bpm ? Math.round(t.bpm) : '—') + '</td>'
            + '<td><span class="star-disp">' + stars + '</span></td>'
            + '<td>' + (t.play_count||0) + '</td>'
            + '</tr>';
    }).join('');

    wrap.innerHTML = '<div class="tbl-wrap"><table><thead><tr>'
        + '<th style="width:46px"></th>'
        + '<th>Title</th><th>Artist</th><th>Album</th>'
        + '<th>Genre</th><th>Duration</th><th>BPM</th><th>★</th><th>Plays</th>'
        + '</tr></thead><tbody id="track-tbody">' + rows + '</tbody></table></div>';

    /* We wire right-click and dblclick on each row after DOM insertion */
    var tbody = document.getElementById('track-tbody');
    tbody.querySelectorAll('.track-row').forEach(function(row) {
        row.addEventListener('contextmenu', function(e) {
            e.preventDefault();
            ctxTrackId   = parseInt(row.getAttribute('data-id'));
            ctxTrackData = JSON.parse(row.getAttribute('data-t'));
            document.getElementById('cat-ctx-menu').style.display = 'none';
            showCtxMenu(e.clientX, e.clientY);
        });
        /* We open the Edit Tags modal on double-click — quick shortcut for power users */
        row.addEventListener('dblclick', function(e) {
            e.preventDefault();
            ctxTrackId   = parseInt(row.getAttribute('data-id'));
            ctxTrackData = JSON.parse(row.getAttribute('data-t'));
            openEditModal(ctxTrackId);
        });
        /* We handle mousedown for drag-select */
        row.addEventListener('mousedown', function(e) {
            if (e.button !== 0) { return; }   /* We only care about left button */
            var tid = parseInt(row.getAttribute('data-id'));
            if (!e.ctrlKey && !e.shiftKey) {
                /* We clear selection on plain click unless already selected */
                if (!selIds.has(tid)) { clearSelection(); }
            }
            selDragging = true;
            row.classList.add('selected');
            selIds.add(tid);
            updateSelBar();
        });
        row.addEventListener('mouseover', function() {
            if (!selDragging) { return; }
            var tid = parseInt(row.getAttribute('data-id'));
            row.classList.add('selected');
            selIds.add(tid);
            updateSelBar();
        });
    });
    document.addEventListener('mouseup', function() { selDragging = false; }, {once: false});

    renderPages(d.page, d.pages, function(p) {
        if (currentCatId) fetchCategoryTracks(currentCatId, p);
        else loadTracks(currentFolder, p);
    });
}

function renderPages(page, pages, cb) {
    var el = document.getElementById('track-pages');
    if (!pages || pages <= 1) { el.innerHTML = ''; return; }
    var h = '';
    if (page > 1) h += '<a onclick="(' + cb.toString() + ')(' + (page-1) + ')">&laquo;</a>';
    var lo = Math.max(1, page-2), hi = Math.min(pages, page+2);
    if (lo > 1) h += '<a onclick="(' + cb.toString() + ')(1)">1</a>' + (lo > 2 ? '<span class="ellipsis">…</span>' : '');
    for (var i = lo; i <= hi; i++) {
        h += (i === page) ? '<span class="cur">' + i + '</span>'
            : '<a onclick="(' + cb.toString() + ')(' + i + ')">' + i + '</a>';
    }
    if (hi < pages) {
        h += (hi < pages-1 ? '<span class="ellipsis">…</span>' : '')
            + '<a onclick="(' + cb.toString() + ')(' + pages + ')">' + pages + '</a>';
    }
    if (page < pages) h += '<a onclick="(' + cb.toString() + ')(' + (page+1) + ')">&raquo;</a>';
    el.innerHTML = h;
}

function refreshTotalBadge() {
    mc1Api('POST', '/app/api/tracks.php', {action:'list', page:1, limit:1, q:''}).then(function(d) {
        if (d.ok && d.total !== undefined) {
            document.getElementById('track-total-badge').textContent = d.total.toLocaleString() + ' tracks';
        }
    });
}

/* ── Right-click context menu ───────────────────────────────────────────── */
var ctxMenu = document.getElementById('ctx-menu');
function showCtxMenu(x, y) {
    ctxMenu.style.display = 'block';
    /* We keep menu inside the viewport */
    var vw = window.innerWidth, vh = window.innerHeight;
    var mw = ctxMenu.offsetWidth  || 200;
    var mh = ctxMenu.offsetHeight || 160;
    ctxMenu.style.left = Math.min(x, vw - mw - 8) + 'px';
    ctxMenu.style.top  = Math.min(y, vh - mh - 8) + 'px';
}
document.addEventListener('click', function() { ctxMenu.style.display = 'none'; });
document.addEventListener('keydown', function(e) { if (e.key === 'Escape') ctxMenu.style.display = 'none'; });

window.ctxEditTags = function() {
    ctxMenu.style.display = 'none';
    openEditModal(ctxTrackId);
};
window.ctxFetchArt = function() {
    ctxMenu.style.display = 'none';
    doFetchArt(ctxTrackId, ctxTrackData);
};
window.ctxAddToPlaylist  = function() { ctxMenu.style.display = 'none'; showModal('modal-addpl'); };
window.ctxNewPlaylistAdd = function() { ctxMenu.style.display = 'none'; showModal('modal-newpl'); };
window.ctxAddToCategory  = function() {
    ctxMenu.style.display = 'none';
    /* We populate the category select from the in-memory categories list */
    var sel = document.getElementById('addcat-select');
    sel.innerHTML = '';
    allCategories.forEach(function(c) {
        var o = document.createElement('option');
        o.value = c.id;
        o.textContent = c.name + ' (' + (c.track_count||0) + ' tracks)';
        sel.appendChild(o);
    });
    if (!allCategories.length) {
        sel.innerHTML = '<option value="">— No categories yet —</option>';
    }
    showModal('modal-addcat');
};
window.ctxDelete = function() {
    ctxMenu.style.display = 'none';
    var name = (ctxTrackData.artist || '?') + ' — ' + (ctxTrackData.title || '?');
    document.getElementById('del-track-name').textContent = name;
    document.getElementById('del-also-file').checked = false;
    showModal('modal-del');
};

/* ── Play Track — add to player queue then navigate to mediaplayer ───────── */
window.ctxPlayTrack = function() {
    ctxMenu.style.display = 'none';
    if (!ctxTrackId) { return; }
    mc1Api('POST', '/app/api/player.php', {action: 'queue_add', track_id: ctxTrackId})
    .then(function(d) {
        if (d.ok) {
            window.location.href = '/mediaplayer.php?track_id=' + ctxTrackId;
        } else {
            mc1Toast(d.error || 'Could not add track to player queue', 'err');
        }
    }).catch(function() { mc1Toast('Request failed', 'err'); });
};

/* ── Category context menu ──────────────────────────────────────────────── */
var catCtxMenu = document.getElementById('cat-ctx-menu');

window.catCtxShow = function(e, catId, catName) {
    e.preventDefault();
    e.stopPropagation();
    ctxCatId   = catId;
    ctxCatName = catName;
    if (ctxMenu) ctxMenu.style.display = 'none';   /* We hide the track menu if open */
    catCtxMenu.style.display = 'block';
    var vw = window.innerWidth, vh = window.innerHeight;
    var mw = catCtxMenu.offsetWidth  || 170;
    var mh = catCtxMenu.offsetHeight || 80;
    catCtxMenu.style.left = Math.min(e.clientX, vw - mw - 8) + 'px';
    catCtxMenu.style.top  = Math.min(e.clientY, vh - mh - 8) + 'px';
};
document.addEventListener('mousedown', function(e) {
    if (catCtxMenu.style.display === 'block' && !catCtxMenu.contains(e.target)) {
        catCtxMenu.style.display = 'none';
    }
});
document.addEventListener('keydown', function(e) { if (e.key === 'Escape') catCtxMenu.style.display = 'none'; });

/* We queue all tracks in a category to the mediaplayer, then navigate */
window.catCtxQueue = function() {
    catCtxMenu.style.display = 'none';
    if (!ctxCatId) { return; }
    /* We first load the track IDs for this category then bulk-add to queue */
    mc1Api('POST', '/app/api/tracks.php', {action: 'list_category_tracks', category_id: ctxCatId, page: 1, limit: 200})
    .then(function(d) {
        if (!d.ok || !d.data || !d.data.length) {
            mc1Toast('Category is empty or could not load tracks', 'warn');
            return;
        }
        var ids = d.data.map(function(t) { return t.id; });
        return mc1Api('POST', '/app/api/player.php', {action: 'queue_add', track_ids: ids});
    }).then(function(d) {
        if (d && d.ok) {
            mc1Toast(d.added + ' tracks queued — opening player…', 'ok');
            setTimeout(function() { window.location.href = '/mediaplayer.php'; }, 900);
        } else if (d) {
            mc1Toast(d.error || 'Could not queue tracks', 'err');
        }
    }).catch(function() { mc1Toast('Queue request failed', 'err'); });
};

/* We add all category tracks to an existing playlist (opens modal-addpl with bulk IDs) */
window.catCtxAddToPlaylist = function() {
    catCtxMenu.style.display = 'none';
    if (!ctxCatId) { return; }
    mc1Toast('Loading category tracks…', 'ok');
    mc1Api('POST', '/app/api/tracks.php', {action: 'list_category_tracks', category_id: ctxCatId, page: 1, limit: 200})
    .then(function(d) {
        if (!d.ok || !d.data || !d.data.length) {
            mc1Toast('Category is empty or could not load tracks', 'warn');
            return;
        }
        var ids = d.data.map(function(t) { return t.id; });
        var modal = document.getElementById('modal-addpl');
        modal.dataset.bulkIds = JSON.stringify(ids);
        showModal('modal-addpl');
    }).catch(function() { mc1Toast('Request failed', 'err'); });
};

/* We delete a category after confirmation — tracks themselves are NOT deleted */
window.catCtxDelete = function() {
    catCtxMenu.style.display = 'none';
    if (!ctxCatId) { return; }
    if (!confirm('Delete category "' + ctxCatName + '"?\n\nTracks in this category will NOT be deleted — only the category grouping is removed.')) { return; }
    mc1Api('POST', '/app/api/tracks.php', {action: 'delete_category', id: ctxCatId})
    .then(function(d) {
        if (d.ok) {
            mc1Toast('Category "' + ctxCatName + '" deleted', 'ok');
            /* We remove the category item from the sidebar DOM */
            var el = document.getElementById('cat-item-' + ctxCatId);
            if (el && el.closest('li')) { el.closest('li').remove(); }
            /* We clear category view if we were viewing this category */
            if (currentCatId === ctxCatId) {
                currentCatId = null;
                loadTracks(currentFolder, 1);
            }
            ctxCatId = null;
        } else {
            mc1Toast(d.error || 'Delete failed', 'err');
        }
    }).catch(function() { mc1Toast('Delete request failed', 'err'); });
};

/* ── Folder context menu ────────────────────────────────────────────────── */
var folderCtxMenu  = document.getElementById('folder-ctx-menu');
var ctxFolderPath  = '';

/* We persist folders the user removed from the library so they stay hidden
 * across page refreshes. The Set is backed by localStorage. */
var deletedFolders = new Set(JSON.parse(localStorage.getItem('mc1_deleted_folders') || '[]'));
function saveDeletedFolders() {
    localStorage.setItem('mc1_deleted_folders', JSON.stringify([...deletedFolders]));
}
/* Remove a specific <li> from the folder tree by its data-path value */
function removeFolderFromTree(fp) {
    document.querySelectorAll('.ftree-item[data-path]').forEach(function(el) {
        if (el.getAttribute('data-path') === fp) {
            var li = el.closest('li');
            if (li) li.remove();
        }
    });
}

window.folderCtxShow = function(e, path) {
    e.preventDefault();
    e.stopPropagation();
    ctxFolderPath = path;
    /* Close other context menus */
    ctxMenu.style.display    = 'none';
    catCtxMenu.style.display = 'none';
    folderCtxMenu.style.display = 'block';
    var vw = window.innerWidth, vh = window.innerHeight;
    var mw = folderCtxMenu.offsetWidth  || 190;
    var mh = folderCtxMenu.offsetHeight || 110;
    folderCtxMenu.style.left = Math.min(e.clientX, vw - mw - 8) + 'px';
    folderCtxMenu.style.top  = Math.min(e.clientY, vh - mh - 8) + 'px';
};
document.addEventListener('click', function() { if (folderCtxMenu) folderCtxMenu.style.display = 'none'; });
document.addEventListener('keydown', function(e) { if (e.key === 'Escape' && folderCtxMenu) folderCtxMenu.style.display = 'none'; });

/* Scan this folder into the library database */
window.folderCtxScan = function() {
    folderCtxMenu.style.display = 'none';
    if (!ctxFolderPath) return;
    var path = ctxFolderPath;
    mc1Toast('Scanning ' + path.split('/').pop() + '…', 'info');
    mc1Api('POST', '/app/api/tracks.php', {action: 'scan', directory: path})
    .then(function(d) {
        if (d.ok) {
            mc1Toast('Scan complete: ' + d.added + ' added, ' + d.skipped + ' skipped', 'ok');
            /* Un-delete: if this folder was previously hidden, make it visible again */
            if (deletedFolders.has(path)) {
                deletedFolders.delete(path);
                saveDeletedFolders();
            }
            if (d.added > 0) loadTracks(currentFolder, 1);
        } else {
            mc1Toast(d.error || 'Scan failed', 'err');
        }
    }).catch(function() { mc1Toast('Scan request failed', 'err'); });
};

/* Add all tracks in this folder to the media player queue */
window.folderCtxQueue = function() {
    folderCtxMenu.style.display = 'none';
    if (!ctxFolderPath) return;
    mc1Api('POST', '/app/api/tracks.php', {action: 'list', folder: ctxFolderPath, page: 1, per_page: 500})
    .then(function(d) {
        if (!d.ok || !d.data || !d.data.length) {
            mc1Toast('No tracks found in this folder (scan it first)', 'warn');
            return;
        }
        var ids = d.data.map(function(t) { return t.id; });
        return mc1Api('POST', '/app/api/player.php', {action: 'queue_add', track_ids: ids});
    }).then(function(d) {
        if (d && d.ok) {
            mc1Toast(d.added + ' tracks queued — opening player…', 'ok');
            setTimeout(function() { window.location.href = '/mediaplayer.php'; }, 900);
        } else if (d) {
            mc1Toast(d.error || 'Could not queue tracks', 'err');
        }
    }).catch(function() { mc1Toast('Queue request failed', 'err'); });
};

/* Create a new playlist from all tracks in this folder */
window.folderCtxPlaylist = function() {
    folderCtxMenu.style.display = 'none';
    if (!ctxFolderPath) return;
    var folderName = ctxFolderPath.split('/').pop() || 'New Playlist';
    var plName = prompt('Playlist name:', folderName);
    if (plName === null) return;          // user cancelled
    plName = plName.trim();
    if (plName === '') { mc1Toast('Playlist name cannot be empty', 'warn'); return; }
    mc1Api('POST', '/app/api/tracks.php', {
        action: 'create_playlist_from_folder',
        folder: ctxFolderPath,
        name:   plName
    }).then(function(d) {
        if (d.ok) {
            mc1Toast('Playlist "' + plName + '" created with ' + d.track_count + ' tracks', 'ok');
        } else {
            mc1Toast(d.error || 'Could not create playlist', 'err');
        }
    }).catch(function() { mc1Toast('Request failed', 'err'); });
};

/* Remove all DB tracks under this folder (does NOT delete files from disk) */
window.folderCtxDelete = function() {
    folderCtxMenu.style.display = 'none';
    if (!ctxFolderPath) return;
    var folderName = ctxFolderPath.split('/').pop() || ctxFolderPath;
    if (!confirm('Remove all library entries under "' + folderName + '" from the database?\n\nFiles on disk are NOT deleted — only the database records are removed.')) return;
    var removedPath = ctxFolderPath;
    mc1Api('POST', '/app/api/tracks.php', {action: 'delete_folder', folder: removedPath})
    .then(function(d) {
        if (d.ok) {
            mc1Toast(d.deleted + ' tracks removed from library', 'ok');
            /* Hide folder from tree immediately and persist across page refreshes */
            deletedFolders.add(removedPath);
            saveDeletedFolders();
            removeFolderFromTree(removedPath);
            if (currentFolder === removedPath || (currentFolder && currentFolder.indexOf(removedPath + '/') === 0)) {
                currentFolder = '';
                loadTracks('', 1);
            } else {
                loadTracks(currentFolder, 1);
            }
        } else {
            mc1Toast(d.error || 'Remove failed', 'err');
        }
    }).catch(function() { mc1Toast('Request failed', 'err'); });
};

/* ── Sync Library ────────────────────────────────────────────────────────── */
window.syncLibrary = function() {
    var btn = document.getElementById('sync-lib-btn');
    if (btn) { btn.disabled = true; btn.innerHTML = '<i class="fa-solid fa-spinner fa-spin"></i> Syncing…'; }
    var bar = document.getElementById('sync-result-bar');
    var txt = document.getElementById('sync-result-text');
    if (bar) bar.style.display = 'none';

    mc1Api('POST', '/app/api/tracks.php', {action: 'sync_library'})
    .then(function(d) {
        if (btn) { btn.disabled = false; btn.innerHTML = '<i class="fa-solid fa-rotate"></i> Sync Library'; }
        if (d.ok) {
            var parts = [];
            if (d.added          > 0) parts.push('<span style="color:var(--teal)">' + d.added + ' added</span>');
            if (d.skipped        > 0) parts.push(d.skipped + ' already known');
            if (d.missing_marked > 0) parts.push('<span style="color:var(--orange)">' + d.missing_marked + ' marked missing</span>');
            if (d.dupes_found    > 0) parts.push('<span style="color:var(--orange)">' + d.dupes_found + ' possible duplicate groups</span>');
            if (!parts.length)         parts.push('Library is up to date');
            if (d.errors && d.errors.length) parts.push('<span style="color:var(--red)">' + d.errors.length + ' error(s)</span>');
            if (txt) txt.innerHTML = '<i class="fa-solid fa-rotate" style="color:var(--teal);margin-right:6px"></i><strong>Sync complete:</strong> ' + parts.join(' &nbsp;·&nbsp; ');
            if (bar) { bar.style.display = 'flex'; }
            if (d.added > 0) loadTracks(currentFolder, 1);
        } else {
            mc1Toast(d.error || 'Sync failed', 'err');
        }
    }).catch(function() {
        if (btn) { btn.disabled = false; btn.innerHTML = '<i class="fa-solid fa-rotate"></i> Sync Library'; }
        mc1Toast('Sync request failed', 'err');
    });
};

/* Remove all is_missing tracks from DB after user confirmation */
window.removeMissingTracks = function() {
    if (!confirm('Delete all tracks flagged as "missing" from the library database?\n\nOnly removes database records — files on disk are unaffected.')) return;
    mc1Api('POST', '/app/api/tracks.php', {action: 'remove_missing'})
    .then(function(d) {
        if (d.ok) {
            mc1Toast(d.deleted + ' missing track records removed', 'ok');
            if (d.deleted > 0) loadTracks(currentFolder, 1);
        } else {
            mc1Toast(d.error || 'Remove failed', 'err');
        }
    }).catch(function() { mc1Toast('Request failed', 'err'); });
};

/* ── Multi-select selection bar ─────────────────────────────────────────── */
function getSelBar() {
    var bar = document.getElementById('sel-bar');
    if (!bar) {
        bar = document.createElement('div');
        bar.id = 'sel-bar';
        bar.className = 'sel-bar';
        bar.style.display = 'none';
        document.body.appendChild(bar);
    }
    return bar;
}

function clearSelection() {
    selIds.clear();
    document.querySelectorAll('.track-row.selected').forEach(function(r) {
        r.classList.remove('selected');
    });
    updateSelBar();
}

function updateSelBar() {
    var count = selIds.size;
    var bar   = getSelBar();
    if (count === 0) { bar.style.display = 'none'; return; }
    bar.style.display = 'flex';
    bar.innerHTML = '<strong style="color:var(--text);white-space:nowrap">'
        + count + ' track' + (count !== 1 ? 's' : '') + ' selected</strong>'
        + '<button class="btn btn-secondary btn-sm" onclick="selAddToPlaylist()">'
        + '<i class="fa-solid fa-list-music"></i> Add to Playlist</button>'
        + '<button class="btn btn-secondary btn-sm" onclick="selQueueInPlayer()">'
        + '<i class="fa-solid fa-headphones"></i> Queue in Player</button>'
        + '<button class="btn btn-danger btn-sm" onclick="selDeleteAll()">'
        + '<i class="fa-solid fa-trash"></i> Delete Selected</button>'
        + '<button class="btn btn-secondary btn-xs" style="margin-left:auto" onclick="clearSelection()" title="Clear selection">'
        + '<i class="fa-solid fa-xmark"></i></button>';
}

/* We add all selected tracks to an existing playlist — we reuse the addpl modal */
window.selAddToPlaylist = function() {
    if (!selIds.size) { return; }
    /* We store the bulk IDs on the modal and re-use the single-track modal UI */
    var modal = document.getElementById('modal-addpl');
    modal.dataset.bulkIds = JSON.stringify(Array.from(selIds));
    showModal('modal-addpl');
};

/* We queue all selected tracks in the media player, then open it */
window.selQueueInPlayer = function() {
    var ids = Array.from(selIds);
    if (!ids.length) { return; }
    mc1Api('POST', '/app/api/player.php', {action: 'queue_add', track_ids: ids})
    .then(function(d) {
        if (d.ok) {
            mc1Toast(d.added + ' track' + (d.added !== 1 ? 's' : '') + ' queued — opening player…', 'ok');
            clearSelection();
            setTimeout(function() { window.location.href = '/mediaplayer.php'; }, 900);
        } else {
            mc1Toast(d.error || 'Could not queue tracks', 'err');
        }
    }).catch(function() { mc1Toast('Request failed', 'err'); });
};

/* We delete all selected tracks from the library (no file deletion) */
window.selDeleteAll = function() {
    var ids = Array.from(selIds);
    if (!ids.length) { return; }
    if (!confirm('Delete ' + ids.length + ' track' + (ids.length !== 1 ? 's' : '') + ' from the library?\n\nPhysical files on disk are NOT deleted.')) { return; }
    var chain = Promise.resolve();
    var deleted = 0;
    ids.forEach(function(id) {
        chain = chain.then(function() {
            return mc1Api('POST', '/app/api/tracks.php', {action: 'delete', id: id, delete_file: false})
            .then(function(d) { if (d.ok) { deleted++; } });
        });
    });
    chain.then(function() {
        mc1Toast('Removed ' + deleted + ' track' + (deleted !== 1 ? 's' : '') + ' from library', 'ok');
        clearSelection();
        if (currentCatId) { fetchCategoryTracks(currentCatId, 1); }
        else { loadTracks(currentFolder, 1); }
        refreshTotalBadge();
    });
};

/* ── Wipe Library (mass delete from DB) ─────────────────────────────────── */
window.showWipeLibraryModal = function() {
    document.getElementById('wipe-confirm-input').value = '';
    showModal('modal-wipe');
};

window.doWipeLibrary = function() {
    var confirmed = document.getElementById('wipe-confirm-input').value.trim().toUpperCase();
    if (confirmed !== 'WIPE') {
        mc1Toast('You must type WIPE exactly to confirm', 'warn');
        return;
    }
    mc1Api('POST', '/app/api/tracks.php', {action: 'wipe_library'})
    .then(function(d) {
        if (d.ok) {
            mc1Toast('Library wiped — ' + (d.deleted || 0) + ' records removed', 'ok');
            hideModal('modal-wipe');
            loadTracks('', 1);
            refreshTotalBadge();
        } else {
            mc1Toast(d.error || 'Wipe failed', 'err');
        }
    }).catch(function() { mc1Toast('Wipe request failed', 'err'); });
};

/* ── Edit Tags Modal ────────────────────────────────────────────────────── */
function openEditModal(id) {
    /* We fetch fresh data from the server so the modal always has current values */
    mc1Api('POST', '/app/api/tracks.php', {action: 'get', id: id}).then(function(d) {
        if (!d.ok) { mc1Toast(d.error || 'Failed to load track', 'err'); return; }
        var t = d.track;
        document.getElementById('edit-track-id').value = t.id;
        document.getElementById('edit-title').value    = t.title   || '';
        document.getElementById('edit-artist').value   = t.artist  || '';
        document.getElementById('edit-album').value    = t.album   || '';
        document.getElementById('edit-genre').value    = t.genre   || '';
        document.getElementById('edit-year').value     = t.year    || '';
        document.getElementById('edit-bpm').value      = t.bpm     || '';
        document.getElementById('edit-energy').value   = t.energy_level !== null ? t.energy_level : '';
        document.getElementById('edit-mood').value     = t.mood_tag     || '';
        document.getElementById('edit-key').value      = t.musical_key  || '';
        document.getElementById('edit-tracknum').value = t.track_number || '';
        document.getElementById('edit-intro').value    = t.intro_ms  || '';
        document.getElementById('edit-outro').value    = t.outro_ms  || '';
        document.getElementById('edit-cuein').value    = t.cue_in_ms || '';
        document.getElementById('edit-cueout').value   = t.cue_out_ms|| '';
        document.getElementById('edit-jingle').checked  = !!t.is_jingle;
        document.getElementById('edit-sweeper').checked = !!t.is_sweeper;
        document.getElementById('edit-spot').checked    = !!t.is_spot;
        document.getElementById('edit-filepath').textContent = t.file_path || '';
        document.getElementById('edit-write-tags').checked = false;
        setStarRating(t.rating || 3);
        showModal('modal-edit');
    }).catch(function() { mc1Toast('Request failed', 'err'); });
}

/* We wire the star rating interactive selector */
var editStars = document.getElementById('edit-stars');
editStars.querySelectorAll('.s').forEach(function(s) {
    s.addEventListener('mouseover', function() {
        var v = parseInt(s.getAttribute('data-v'));
        editStars.querySelectorAll('.s').forEach(function(x) {
            x.classList.toggle('hover', parseInt(x.getAttribute('data-v')) <= v);
        });
    });
    s.addEventListener('mouseout', function() {
        editStars.querySelectorAll('.s').forEach(function(x) { x.classList.remove('hover'); });
    });
    s.addEventListener('click', function() {
        setStarRating(parseInt(s.getAttribute('data-v')));
    });
});
function setStarRating(v) {
    document.getElementById('edit-rating').value = v;
    editStars.querySelectorAll('.s').forEach(function(x) {
        x.classList.toggle('on', parseInt(x.getAttribute('data-v')) <= v);
    });
}

window.saveEditTags = function() {
    var id = parseInt(document.getElementById('edit-track-id').value);
    if (!id) return;
    var btn = document.querySelector('#modal-edit .btn-primary');
    btn.disabled = true;
    mc1Api('POST', '/app/api/tracks.php', {
        action:     'update',
        id:         id,
        title:      document.getElementById('edit-title').value,
        artist:     document.getElementById('edit-artist').value,
        album:      document.getElementById('edit-album').value,
        genre:      document.getElementById('edit-genre').value,
        year:       document.getElementById('edit-year').value,
        bpm:        document.getElementById('edit-bpm').value,
        energy_level: document.getElementById('edit-energy').value,
        mood_tag:   document.getElementById('edit-mood').value,
        musical_key:document.getElementById('edit-key').value,
        track_number: document.getElementById('edit-tracknum').value,
        rating:     parseInt(document.getElementById('edit-rating').value),
        intro_ms:   document.getElementById('edit-intro').value,
        outro_ms:   document.getElementById('edit-outro').value,
        cue_in_ms:  document.getElementById('edit-cuein').value,
        cue_out_ms: document.getElementById('edit-cueout').value,
        is_jingle:  document.getElementById('edit-jingle').checked,
        is_sweeper: document.getElementById('edit-sweeper').checked,
        is_spot:    document.getElementById('edit-spot').checked,
        write_tags: document.getElementById('edit-write-tags').checked,
    }).then(function(d) {
        btn.disabled = false;
        if (d.ok) {
            var msg = 'Tags saved';
            if (d.wrote_tags) msg += ' + written to file';
            else if (d.tag_error) msg += ' (file write: ' + d.tag_error + ')';
            mc1Toast(msg, 'ok');
            hideModal('modal-edit');
            /* We reload the current track list to reflect changes */
            if (currentCatId) fetchCategoryTracks(currentCatId, currentPage);
            else loadTracks(currentFolder, currentPage);
        } else {
            mc1Toast(d.error || 'Save failed', 'err');
        }
    }).catch(function() { btn.disabled = false; mc1Toast('Request failed', 'err'); });
};

/* ── Fetch Album Art ────────────────────────────────────────────────────── */
function doFetchArt(id, t) {
    mc1Toast('Fetching album art from MusicBrainz…', 'ok');
    mc1Api('POST', '/app/api/tracks.php', {
        action:         'fetch_art',
        track_id:       id,
        artist:         t.artist || '',
        title:          t.title  || '',
        album:          t.album  || '',
        embed_in_file:  false,
    }).then(function(d) {
        if (d.ok) {
            mc1Toast('Album art saved!', 'ok');
            /* We refresh the row art thumbnail without a full reload */
            var row = document.querySelector('tr.track-row[data-id="' + id + '"]');
            if (row && d.art_url) {
                var td = row.querySelector('.td-art');
                if (td) td.innerHTML = '<img class="art-thumb" src="' + esc(d.art_url) + '?t=' + Date.now() + '">';
            }
        } else {
            mc1Toast(d.error || 'Art not found on MusicBrainz', 'warn');
        }
    }).catch(function() { mc1Toast('Art fetch failed', 'err'); });
}

/* ── Add to Playlist ────────────────────────────────────────────────────── */
window.doAddToPlaylist = function() {
    var plId  = parseInt(document.getElementById('addpl-select').value);
    var wt    = parseFloat(document.getElementById('addpl-weight').value);
    var modal = document.getElementById('modal-addpl');
    if (!plId) { mc1Toast('Select a playlist', 'warn'); return; }

    /* We support bulk add from the selection bar (bulkIds stored on modal dataset) */
    var bulkRaw = modal.dataset.bulkIds;
    if (bulkRaw) {
        /* We clear the bulk tag after reading so the next single-track use works normally */
        delete modal.dataset.bulkIds;
        var ids = JSON.parse(bulkRaw);
        var chain = Promise.resolve();
        var added = 0;
        ids.forEach(function(id) {
            chain = chain.then(function() {
                return mc1Api('POST', '/app/api/tracks.php', {
                    action: 'add_to_playlist', track_id: id, playlist_id: plId, weight: wt
                }).then(function(d) { if (d.ok) { added++; } });
            });
        });
        chain.then(function() {
            mc1Toast('Added ' + added + ' track' + (added !== 1 ? 's' : '') + ' to playlist', 'ok');
            hideModal('modal-addpl');
            clearSelection();
        });
        return;
    }

    /* We handle single-track add (the normal right-click path) */
    if (!ctxTrackId) { mc1Toast('No track selected', 'warn'); return; }
    mc1Api('POST', '/app/api/tracks.php', {
        action: 'add_to_playlist', track_id: ctxTrackId, playlist_id: plId, weight: wt
    }).then(function(d) {
        if (d.ok) { mc1Toast('Added to playlist (weight ' + wt.toFixed(1) + ')', 'ok'); hideModal('modal-addpl'); }
        else { mc1Toast(d.error || 'Failed', 'err'); }
    }).catch(function() { mc1Toast('Request failed', 'err'); });
};

/* ── New Playlist + Add ─────────────────────────────────────────────────── */
window.doNewPlaylistAdd = function() {
    var name = document.getElementById('newpl-name').value.trim();
    var type = document.getElementById('newpl-type').value;
    var desc = document.getElementById('newpl-desc').value.trim();
    var wt   = parseFloat(document.getElementById('newpl-weight').value);
    if (!name) { mc1Toast('Enter a playlist name', 'warn'); return; }
    mc1Api('POST', '/app/api/tracks.php', {
        action: 'create_playlist_add', track_id: ctxTrackId,
        name: name, type: type, description: desc, weight: wt
    }).then(function(d) {
        if (d.ok) {
            mc1Toast('Playlist "' + name + '" created and track added', 'ok');
            hideModal('modal-newpl');
            document.getElementById('newpl-name').value = '';
        } else mc1Toast(d.error || 'Failed', 'err');
    }).catch(function() { mc1Toast('Request failed', 'err'); });
};

/* ── Delete Track ───────────────────────────────────────────────────────── */
window.doDeleteTrack = function() {
    var also = document.getElementById('del-also-file').checked;
    mc1Api('POST', '/app/api/tracks.php', {
        action: 'delete', id: ctxTrackId, delete_file: also
    }).then(function(d) {
        if (d.ok) {
            var msg = 'Track deleted' + (d.file_deleted ? ' + file removed from disk' : '');
            mc1Toast(msg, 'ok');
            hideModal('modal-del');
            if (currentCatId) fetchCategoryTracks(currentCatId, currentPage);
            else loadTracks(currentFolder, currentPage);
            refreshTotalBadge();
        } else mc1Toast(d.error || 'Delete failed', 'err');
    }).catch(function() { mc1Toast('Request failed', 'err'); });
};

/* ── Add to Category ────────────────────────────────────────────────────── */
window.doAddToCategory = function() {
    var catId = parseInt(document.getElementById('addcat-select').value);
    if (!catId || !ctxTrackId) { mc1Toast('Select a category', 'warn'); return; }
    mc1Api('POST', '/app/api/tracks.php', {
        action: 'add_to_category', track_id: ctxTrackId, category_id: catId
    }).then(function(d) {
        if (d.ok) {
            mc1Toast('Added to category', 'ok');
            hideModal('modal-addcat');
            /* We increment the count badge in the sidebar without a page reload */
            var cntEl = document.querySelector('#cat-item-' + catId + ' .ftree-cnt');
            if (cntEl) { cntEl.textContent = (parseInt(cntEl.textContent) || 0) + 1; }
            /* We refresh the track list if the user is currently viewing this category */
            if (currentCatId === catId) { fetchCategoryTracks(catId, currentPage); }
        } else mc1Toast(d.error || 'Failed', 'err');
    }).catch(function() { mc1Toast('Request failed', 'err'); });
};

/* ── Create Category ────────────────────────────────────────────────────── */
window.showCreateCategory = function() {
    document.getElementById('newcat-name').value = '';
    showModal('modal-newcat');
};
window.doCreateCategory = function() {
    var name  = document.getElementById('newcat-name').value.trim();
    var color = document.getElementById('newcat-color').value;
    if (!name) { mc1Toast('Enter a category name', 'warn'); return; }
    mc1Api('POST', '/app/api/tracks.php', {
        action: 'create_category', name: name, color_hex: color
    }).then(function(d) {
        if (d.ok) {
            mc1Toast('Category "' + name + '" created', 'ok');
            hideModal('modal-newcat');
            /* We add the new category to the sidebar without a full page reload */
            var cat = {id: d.id, name: name, color_hex: color, track_count: 0};
            allCategories.push(cat);
            var li = document.createElement('li');
            li.innerHTML = '<div class="ftree-item" id="cat-item-' + d.id + '" data-cat-id="' + d.id + '"'
                + ' onclick="loadCategoryTracks(' + d.id + ',' + esc(JSON.stringify(name)) + ')"'
                + ' oncontextmenu="catCtxShow(event,' + d.id + ',' + esc(JSON.stringify(name)) + ')">'
                + '<span class="ftree-arrow" style="visibility:hidden"></span>'
                + '<span class="cat-dot" style="background:' + esc(color) + '"></span>'
                + '<span class="ftree-label">' + esc(name) + '</span>'
                + '<span class="ftree-cnt">0</span>'
                + '</div>';
            document.getElementById('cat-list').appendChild(li);
        } else mc1Toast(d.error || 'Create failed', 'err');
    }).catch(function() { mc1Toast('Request failed', 'err'); });
};

window.selectColor = function(el) {
    document.querySelectorAll('#newcat-color-strip .color-swatch').forEach(function(s) { s.classList.remove('sel'); });
    el.classList.add('sel');
    document.getElementById('newcat-color').value = el.getAttribute('data-color');
};

/* ── Folder Scan ────────────────────────────────────────────────────────── */
window.doScan = function() {
    var dir = document.getElementById('scan-dir').value.trim();
    if (!dir) { mc1Toast('Enter a directory path', 'warn'); return; }
    var btn = document.getElementById('scan-btn');
    var res = document.getElementById('scan-res');
    btn.disabled = true;
    btn.innerHTML = '<div class="spinner" style="width:14px;height:14px;display:inline-block"></div> Scanning…';
    res.innerHTML = '<div class="alert alert-info"><i class="fa-solid fa-circle-info"></i> Scanning ' + esc(dir) + '…</div>';
    mc1Api('POST', '/app/api/tracks.php', {action: 'scan', directory: dir}).then(function(d) {
        btn.disabled = false;
        btn.innerHTML = '<i class="fa-solid fa-folder-open"></i> Scan';
        if (d.ok) {
            var errs = (d.errors && d.errors.length) ? ' Errors: ' + esc(d.errors.join('; ')) : '';
            res.innerHTML = '<div class="alert alert-success"><i class="fa-solid fa-circle-check"></i> '
                + 'Added <strong>' + d.added + '</strong> tracks, skipped ' + d.skipped + '.' + errs + '</div>';
            refreshTotalBadge();
            loadTracks(currentFolder, 1);
        } else {
            res.innerHTML = '<div class="alert alert-error"><i class="fa-solid fa-xmark"></i> ' + esc(d.error || 'Scan failed') + '</div>';
        }
    }).catch(function() {
        btn.disabled = false;
        btn.innerHTML = '<i class="fa-solid fa-folder-open"></i> Scan';
        res.innerHTML = '<div class="alert alert-error">Request failed</div>';
    });
};

/* ── Playlist Files ─────────────────────────────────────────────────────── */
window.loadPfList = function() {
    var wrap = document.getElementById('pf-tbl');
    wrap.innerHTML = '<div class="empty"><div class="spinner"></div></div>';
    mc1Api('POST', '/app/api/tracks.php', {action: 'playlist_files'}).then(function(d) {
        if (!d.ok) { wrap.innerHTML = '<div class="alert alert-error">' + esc(d.error||'Error') + '</div>'; return; }
        if (!d.files||!d.files.length) { wrap.innerHTML = '<div class="empty"><i class="fa-solid fa-list fa-fw"></i><p>No playlist files found</p></div>'; return; }
        var rows = d.files.map(function(f) {
            var sz  = f.size_bytes ? (f.size_bytes > 1048576 ? (f.size_bytes/1048576).toFixed(1)+'MB' : Math.round(f.size_bytes/1024)+'KB') : '—';
            var pfx = 'pf-' + Math.random().toString(36).substr(2,8);
            return '<tr>'
                + '<td class="td-title" title="' + esc(f.path) + '">' + esc(f.name||f.path) + '</td>'
                + '<td><span class="badge badge-gray">' + esc(f.format) + '</span></td>'
                + '<td>' + (f.track_count||'—') + '</td>'
                + '<td>' + sz + '</td>'
                + '<td class="td-acts" style="white-space:nowrap">'
                + '<select class="form-select" id="sl-' + pfx + '" style="width:130px;padding:3px 6px;font-size:12px">' + SLOT_OPTS + '</select>'
                + ' <button class="btn btn-primary btn-xs" onclick="loadPf(' + JSON.stringify(f.path) + ',\'sl-' + pfx + '\')"><i class="fa-solid fa-upload"></i> Load</button>'
                + '</td>'
                + '</tr>';
        }).join('');
        wrap.innerHTML = '<div class="tbl-wrap"><table><thead><tr><th>File</th><th>Format</th><th>Tracks</th><th>Size</th><th style="text-align:right">Load to Slot</th></tr></thead><tbody>' + rows + '</tbody></table></div>';
    }).catch(function() { wrap.innerHTML = '<div class="alert alert-error">Request failed</div>'; });
};

window.loadPf = function(path, slotElId) {
    var slot = parseInt(document.getElementById(slotElId).value);
    /* We update MySQL first (safe — no C++ loopback), then tell C++ directly from browser */
    mc1Api('POST', '/app/api/encoders.php', {action: 'update_playlist', slot_id: slot, path: path}).then(function(d) {
        mc1Api('POST', '/api/v1/playlist/load', {slot: slot, path: path}).then(function() {
            mc1Toast('Playlist loaded to slot ' + slot, 'ok');
        }).catch(function() {
            mc1Toast('Config saved. Restart slot ' + slot + ' to apply.', 'warn');
        });
    }).catch(function() { mc1Toast('Request failed', 'err'); });
};

/* ── Library Paths tab ──────────────────────────────────────────────────── */
window.showAddPath = function() {
    document.getElementById('new-path-val').value   = '';
    document.getElementById('new-path-label').value = '';
    document.getElementById('add-path-form').style.display = 'block';
    document.getElementById('new-path-val').focus();
};
window.hideAddPath = function() { document.getElementById('add-path-form').style.display = 'none'; };

window.doAddPath = function() {
    var path  = document.getElementById('new-path-val').value.trim();
    var label = document.getElementById('new-path-label').value.trim();
    if (!path) { mc1Toast('Enter a server path', 'warn'); return; }
    mc1Api('POST', '/app/api/tracks.php', {action: 'add_library_path', path: path, label: label}).then(function(d) {
        if (d.ok) {
            mc1Toast('Path added: ' + path, 'ok');
            hideAddPath();
            /* We inject the new path row into the DOM directly */
            var div = document.createElement('div');
            div.className = 'path-item';
            div.id = 'pitem-' + d.id;
            div.innerHTML = '<span class="ftree-icon"><i class="fa-solid fa-hard-drive" style="color:var(--teal)"></i></span>'
                + '<div class="path-label"><div>' + esc(d.path) + '</div><div class="path-sub">' + esc(label) + '</div></div>'
                + '<button class="btn btn-danger btn-xs" onclick="removePath(' + d.id + ')"><i class="fa-solid fa-trash"></i></button>';
            document.getElementById('db-path-list').appendChild(div);

            /* We also add a quick link in the folder scan hints */
            var hint = document.querySelector('#tab-scan .form-hint');
            if (hint) {
                var a = document.createElement('a');
                a.href = '#';
                a.textContent = ' ' + (label || path.split('/').pop());
                a.onclick = function() { document.getElementById('scan-dir').value = d.path; return false; };
                hint.appendChild(document.createTextNode(' '));
                hint.appendChild(a);
            }
        } else mc1Toast(d.error || 'Failed', 'err');
    }).catch(function() { mc1Toast('Request failed', 'err'); });
};

window.removePath = function(id) {
    if (!confirm('Remove this library path?')) return;
    mc1Api('POST', '/app/api/tracks.php', {action: 'remove_library_path', id: id}).then(function(d) {
        if (d.ok) {
            mc1Toast('Path removed', 'ok');
            var el = document.getElementById('pitem-' + id);
            if (el) el.remove();
        } else mc1Toast(d.error || 'Failed', 'err');
    }).catch(function() { mc1Toast('Request failed', 'err'); });
};

window.refreshLibPaths = function() { /* We refresh via the PHP-rendered list on next page load; dynamic add handles live updates */ };

/* ════════════════════════════════════════════════════════════════════════
 * BATCH UPLOAD FUNCTIONALITY
 * ════════════════════════════════════════════════════════════════════════ */

var uploadQueue       = [];      // {file, subdir} objects waiting to upload
var uploadResults     = [];      // {name, size, status, error, progress, saved_as}
var uploadRunning     = false;
var uploadCancelled   = false;
var uploadQueuePage   = 1;       // current page in queue preview
var uploadProgPage    = 1;       // current page in progress modal
var UPLOAD_PAGE_SIZE  = 25;
var UPLOAD_CONCUR     = 3;       // max concurrent XHR uploads
var uploadDone        = 0;
var uploadTotal       = 0;
var uploadQueueIdx    = 0;       // next queue index to dispatch
var uploadActiveCount = 0;       // running XHR slots right now
var uploadTabInited   = false;
var dzDragCount       = 0;       // counter for dragenter/dragleave on child elements

/* Called when the Batch Upload tab button is clicked (first time only).
 * setupDropzone() is called in DOMContentLoaded so handlers are always
 * attached regardless of which tab was open on page load. */
window.initUploadTab = function() {
    if (uploadTabInited) return;
    uploadTabInited = true;
    loadUploadDirs();
};

/* Load directory list from upload.php */
window.loadUploadDirs = function() {
    var sel = document.getElementById('upload-dir-select');
    if (!sel) return;
    sel.innerHTML = '<option value="">Loading directories…</option>';
    sel.disabled  = true;
    mc1Api('POST', '/app/api/upload.php', {action: 'list_dirs'}).then(function(d) {
        sel.innerHTML = '';
        sel.disabled  = false;
        if (!d.ok || !d.dirs || !d.dirs.length) {
            sel.innerHTML = '<option value="">No upload directories found</option>';
            return;
        }
        d.dirs.forEach(function(dir) {
            var opt       = document.createElement('option');
            opt.value     = dir.path;
            opt.textContent = dir.label;
            sel.appendChild(opt);
        });
    }).catch(function() {
        sel.innerHTML = '<option value="">Error loading directories</option>';
        if (sel) sel.disabled = false;
    });
};

/*
 * window.mc1UploadFiles — called by onchange attributes on both file inputs.
 * Using inline onchange is the most reliable approach: it does not depend on
 * setupDropzone() having run. Inline event attributes execute in global scope.
 */
window.mc1UploadFiles = function(input) {
    if (input && input.files && input.files.length) {
        addFilesToQueue(input.files, '');
    }
    try { input.value = ''; } catch(e) {}
};

/* Attach drag-drop and click-to-browse events to the drop zone.
 * NOTE: file input change events are handled by onchange in the HTML — not here. */
function setupDropzone() {
    var dz  = document.getElementById('upload-dropzone');
    var inp = document.getElementById('upload-file-input');
    if (!dz || !inp) return;

    /* Stop programmatic .click() on the hidden inputs from bubbling up to the
     * dropzone and accidentally triggering the file picker a second time. */
    inp.addEventListener('click', function(e) { e.stopPropagation(); });
    var fldInp2 = document.getElementById('upload-folder-input');
    if (fldInp2) fldInp2.addEventListener('click', function(e) { e.stopPropagation(); });

    /* Click on the zone background (not any interactive child) → open file picker */
    dz.addEventListener('click', function(e) {
        if (e.target.tagName !== 'BUTTON' && e.target.tagName !== 'I') inp.click();
    });

    /* Use a counter so dragleave on child elements doesn't cancel the visual */
    dz.addEventListener('dragover',  function(e) { e.preventDefault(); });
    dz.addEventListener('dragenter', function(e) { e.preventDefault(); dzDragCount++; dz.classList.add('dragover'); });
    dz.addEventListener('dragleave', function()  { dzDragCount--; if (dzDragCount <= 0) { dzDragCount = 0; dz.classList.remove('dragover'); } });
    dz.addEventListener('dragend',   function()  { dzDragCount = 0; dz.classList.remove('dragover'); });

    dz.addEventListener('drop', function(e) {
        e.preventDefault();
        dzDragCount = 0;
        dz.classList.remove('dragover');

        /* Prefer the FileSystem API (supports dropped folders) */
        var items = e.dataTransfer && e.dataTransfer.items;
        if (items && items.length && items[0].webkitGetAsEntry) {
            var entries = [];
            for (var i = 0; i < items.length; i++) {
                var entry = items[i].webkitGetAsEntry ? items[i].webkitGetAsEntry() : null;
                if (entry) entries.push(entry);
            }
            if (entries.length) {
                traverseEntries(entries, '', function(added) {
                    if (added === 0) mc1Toast('No supported audio files in dropped items', 'warn');
                    else renderQueuePreview();
                });
                return;
            }
        }
        /* Plain file list fallback */
        var files = e.dataTransfer ? e.dataTransfer.files : null;
        if (files && files.length) addFilesToQueue(files, '');
    });
}

/*
 * Recursively traverse FileSystem API entries (supports dropped folders).
 * basePath is the relative subdir within the upload target (e.g. "MyAlbum/Disc 1").
 * Calls done(totalAdded) when all entries are processed.
 */
function traverseEntries(entries, basePath, done) {
    var pending    = entries.length;
    var addedTotal = 0;
    if (!pending) { done(0); return; }

    function onEntry(added) {
        addedTotal += added;
        if (--pending === 0) done(addedTotal);
    }

    entries.forEach(function(entry) {
        if (entry.isFile) {
            entry.getFile(function(file) {
                onEntry(addSingleToQueue(file, basePath) ? 1 : 0);
            }, function() { onEntry(0); });

        } else if (entry.isDirectory) {
            var subPath = (basePath ? basePath + '/' : '') + entry.name;
            var reader  = entry.createReader();
            var allEnts = [];

            /* readEntries may return results in batches — keep calling until empty */
            function readAll() {
                reader.readEntries(function(results) {
                    if (!results.length) {
                        traverseEntries(allEnts, subPath, function(added) { onEntry(added); });
                        return;
                    }
                    allEnts = allEnts.concat(Array.prototype.slice.call(results));
                    readAll();
                }, function() { onEntry(0); });
            }
            readAll();

        } else {
            onEntry(0);
        }
    });
}

/*
 * Add a FileList to the queue, filtering by audio extension, deduplicating.
 * Each queue item is {file, subdir} where subdir is the relative path under target_dir.
 * For webkitdirectory inputs, subdir is derived from file.webkitRelativePath.
 * For plain multi-select inputs, subdir is '' (flat into target_dir).
 */
function addFilesToQueue(files, defaultSubdir) {
    defaultSubdir = defaultSubdir || '';
    var added = 0;
    for (var i = 0; i < files.length; i++) {
        var f       = files[i];
        /* Derive subdir from webkitRelativePath when available (webkitdirectory input) */
        var relPath = f.webkitRelativePath || '';
        var subdir  = defaultSubdir;
        if (relPath) {
            var parts = relPath.split('/');
            parts.pop();                /* remove the filename — keep the dirs */
            subdir = parts.join('/');
        }
        if (addSingleToQueue(f, subdir)) added++;
    }
    if (added === 0 && files.length > 0) {
        mc1Toast('No supported audio files found in selection', 'warn');
    }
    renderQueuePreview();
}

/*
 * Add one File to the queue with an explicit subdir.
 * Returns true if added, false if skipped (wrong extension or duplicate).
 */
function addSingleToQueue(f, subdir) {
    var audioExts = ['mp3','ogg','flac','opus','aac','m4a','mp4','wav','wma','aiff','aif','ape','alac'];
    var ext = f.name.split('.').pop().toLowerCase();
    if (audioExts.indexOf(ext) === -1) return false;
    var dup = uploadQueue.some(function(q) {
        return q.file.name === f.name && q.file.size === f.size && q.subdir === subdir;
    });
    if (dup) return false;
    uploadQueue.push({file: f, subdir: subdir || ''});
    return true;
}

/* Render the queue preview below the drop zone (paginated 25 per page) */
function renderQueuePreview() {
    var wrap  = document.getElementById('upload-queue-wrap');
    var list  = document.getElementById('upload-queue-list');
    var cnt   = document.getElementById('upload-queue-count');
    var pager = document.getElementById('upload-queue-pager');
    if (!wrap) return;

    if (uploadQueue.length === 0) {
        wrap.style.display = 'none';
        return;
    }
    wrap.style.display = 'block';
    cnt.textContent    = uploadQueue.length;

    var totalPages = Math.max(1, Math.ceil(uploadQueue.length / UPLOAD_PAGE_SIZE));
    if (uploadQueuePage > totalPages) uploadQueuePage = totalPages;

    var start = (uploadQueuePage - 1) * UPLOAD_PAGE_SIZE;
    var slice = uploadQueue.slice(start, start + UPLOAD_PAGE_SIZE);

    list.innerHTML = slice.map(function(item, i) {
        var gi      = start + i;
        var subdirBadge = item.subdir
            ? '<div style="font-size:10px;color:var(--muted);overflow:hidden;text-overflow:ellipsis;white-space:nowrap">'
              + '<i class="fa-solid fa-folder fa-fw" style="font-size:9px;margin-right:3px"></i>'
              + esc(item.subdir) + '</div>'
            : '';
        return '<div style="display:flex;align-items:center;gap:8px;padding:5px 4px;font-size:12px'
             + (i > 0 ? ';border-top:1px solid var(--border)' : '') + '">'
             + '<i class="fa-solid fa-music" style="color:var(--muted);width:14px;text-align:center;flex-shrink:0;margin-top:1px"></i>'
             + '<div style="flex:1;min-width:0">'
             + '<div style="overflow:hidden;text-overflow:ellipsis;white-space:nowrap" title="'
             + esc(item.file.name) + '">' + esc(item.file.name) + '</div>'
             + subdirBadge
             + '</div>'
             + '<span style="color:var(--muted);flex-shrink:0">' + fmtBytes(item.file.size) + '</span>'
             + '<button class="btn btn-danger btn-xs" onclick="removeQueueItem(' + gi + ')" title="Remove">'
             + '<i class="fa-solid fa-xmark"></i></button>'
             + '</div>';
    }).join('');

    pager.innerHTML = '';
    if (totalPages > 1) {
        renderUploadPager(pager, uploadQueuePage, totalPages, function(p) {
            uploadQueuePage = p;
            renderQueuePreview();
        });
    }
}

window.removeQueueItem = function(idx) {
    uploadQueue.splice(idx, 1);
    var totalPages = Math.max(1, Math.ceil(uploadQueue.length / UPLOAD_PAGE_SIZE));
    if (uploadQueuePage > totalPages) uploadQueuePage = Math.max(1, totalPages);
    renderQueuePreview();
};

window.clearUploadQueue = function() {
    if (uploadRunning) return;
    uploadQueue     = [];
    uploadQueuePage = 1;
    renderQueuePreview();
};

/* Show/hide the create-subfolder inline form */
window.showCreateDirForm = function() {
    var frm = document.getElementById('create-dir-form');
    if (frm) frm.style.display = 'block';
    var inp = document.getElementById('new-dir-name');
    if (inp) { inp.value = ''; inp.focus(); }
    var hint = document.getElementById('create-dir-hint');
    if (hint) { hint.textContent = ''; }
};
window.hideCreateDirForm = function() {
    var frm = document.getElementById('create-dir-form');
    if (frm) frm.style.display = 'none';
};

window.doCreateDir = function() {
    var parent = (document.getElementById('upload-dir-select') || {}).value || '';
    var name   = ((document.getElementById('new-dir-name') || {}).value || '').trim();
    var hint   = document.getElementById('create-dir-hint');

    function setHintErr(msg) {
        if (!hint) return;
        hint.textContent  = msg;
        hint.style.color  = 'var(--red, #ef4444)';
    }
    if (!parent) { setHintErr('Select a parent directory first'); return; }
    if (!name)   { setHintErr('Enter a folder name'); return; }

    mc1Api('POST', '/app/api/upload.php', {action: 'create_dir', parent: parent, name: name})
        .then(function(d) {
            if (d.ok) {
                mc1Toast(d.created ? 'Folder created: ' + name : 'Folder already exists', 'ok');
                hideCreateDirForm();
                loadUploadDirs();
            } else {
                setHintErr(d.error || 'Failed to create folder');
            }
        }).catch(function() { setHintErr('Request failed'); });
};

/* ── Upload Engine ─────────────────────────────────────────────────────── */

window.startBatchUpload = function() {
    var sel = document.getElementById('upload-dir-select');
    var targetDir = (sel && sel.value) ? sel.value : '';
    // If no destination selected, send empty string — PHP will default to MC1_AUDIO_ROOT.
    // When uploading a folder the top-level folder name is already in the subdir path,
    // so the files land under audio_root/FolderName/... automatically.
    if (uploadQueue.length === 0){ mc1Toast('No files in the queue', 'warn'); return; }
    if (uploadRunning)           return;

    uploadRunning     = true;
    uploadCancelled   = false;
    uploadDone        = 0;
    uploadTotal       = uploadQueue.length;
    uploadQueueIdx    = 0;
    uploadActiveCount = 0;
    uploadProgPage    = 1;

    /* Build results array from the {file, subdir} queue items */
    uploadResults = uploadQueue.map(function(item) {
        return { name: item.file.name, size: item.file.size,
                 subdir: item.subdir, status: 'pending', error: '', progress: 0 };
    });

    /* Show the progress modal in a "preparing" state */
    var cancelBtn = document.getElementById('uprog-cancel-btn');
    var scanBtn   = document.getElementById('uprog-scan-btn');
    var doneBtn   = document.getElementById('uprog-done-btn');
    var lbl       = document.getElementById('uprog-label');
    if (cancelBtn) cancelBtn.style.display = '';
    if (scanBtn)   scanBtn.style.display   = 'none';
    if (doneBtn)   doneBtn.style.display   = 'none';
    if (lbl)       lbl.textContent         = 'Preparing upload directories…';

    showModal('modal-upload-progress');
    updateOverallProgress();
    renderProgressPage();

    /* Collect unique non-empty subdirs that need to be created on the server */
    var subdirSet = {};
    uploadQueue.forEach(function(item) { if (item.subdir) subdirSet[item.subdir] = true; });
    var subdirs = Object.keys(subdirSet);

    /* Pre-create all subdirs sequentially, then kick off concurrent file uploads */
    createSubdirsSequentially(targetDir, subdirs, 0, function() {
        if (uploadCancelled) return;
        for (var i = 0; i < UPLOAD_CONCUR; i++) uploadNext(targetDir);
    });
};

/* Create an array of subdir paths one at a time (each may be multi-level) */
function createSubdirsSequentially(baseDir, subdirs, idx, done) {
    if (idx >= subdirs.length) { done(); return; }
    ensureSubdir(baseDir, subdirs[idx], function() {
        createSubdirsSequentially(baseDir, subdirs, idx + 1, done);
    });
}

/*
 * Create every segment of a nested subdir path relative to baseDir.
 * e.g. ensureSubdir('/music', 'Beatles/Abbey Road', cb)
 *   → create_dir(parent='/music',      name='Beatles')
 *   → create_dir(parent='/music/Beatles', name='Abbey Road')
 * Errors are ignored (best-effort) so a missing dir doesn't block the upload.
 */
function ensureSubdir(baseDir, subdirPath, done) {
    var parts = subdirPath.split('/').filter(Boolean);
    function step(currentBase, i) {
        if (i >= parts.length) { done(); return; }
        mc1Api('POST', '/app/api/upload.php',
               {action: 'create_dir', parent: currentBase, name: parts[i]})
            .then(function(d) {
                var nextBase = (d.ok && d.path) ? d.path : (currentBase + '/' + parts[i]);
                step(nextBase, i + 1);
            })
            .catch(function() { step(currentBase + '/' + parts[i], i + 1); });
    }
    step(baseDir, 0);
}

function uploadNext(targetDir) {
    if (uploadCancelled) {
        uploadActiveCount = Math.max(0, uploadActiveCount - 1);
        checkUploadComplete();
        return;
    }
    if (uploadQueueIdx >= uploadTotal) {
        uploadActiveCount = Math.max(0, uploadActiveCount - 1);
        checkUploadComplete();
        return;
    }

    var idx       = uploadQueueIdx++;
    var item      = uploadQueue[idx];
    var file      = item.file;
    var actualDir = item.subdir ? (targetDir + '/' + item.subdir) : targetDir;
    var res       = uploadResults[idx];
    uploadActiveCount++;
    res.status   = 'uploading';
    res.progress = 0;

    /* Re-render if this file is on the current progress page */
    var filePage = Math.floor(idx / UPLOAD_PAGE_SIZE) + 1;
    if (filePage === uploadProgPage) renderProgressPage();

    var fd = new FormData();
    fd.append('action',     'upload');
    fd.append('target_dir', actualDir);
    fd.append('file',       file);

    var xhr = new XMLHttpRequest();
    xhr.open('POST', '/app/api/upload.php', true);
    xhr.withCredentials = true;

    xhr.upload.onprogress = function(e) {
        if (!e.lengthComputable) return;
        res.progress = Math.round(e.loaded / e.total * 100);
        if (filePage === uploadProgPage) renderProgressPage();
    };

    xhr.onload = function() {
        uploadActiveCount = Math.max(0, uploadActiveCount - 1);
        uploadDone++;
        try {
            var d = JSON.parse(xhr.responseText);
            if (d.ok) {
                res.status   = 'done';
                res.progress = 100;
                res.saved_as = d.saved_as || file.name;
            } else {
                res.status = 'error';
                res.error  = d.error || 'Upload failed';
            }
        } catch (ex) {
            res.status = 'error';
            res.error  = 'Invalid server response';
        }
        updateOverallProgress();
        if (filePage === uploadProgPage) renderProgressPage();
        uploadNext(targetDir);
    };

    xhr.onerror = function() {
        uploadActiveCount = Math.max(0, uploadActiveCount - 1);
        uploadDone++;
        res.status = 'error';
        res.error  = 'Network error';
        updateOverallProgress();
        if (filePage === uploadProgPage) renderProgressPage();
        uploadNext(targetDir);
    };

    xhr.send(fd);
}

function checkUploadComplete() {
    /* All slots have drained — nothing in flight and nothing left to dispatch */
    if (uploadActiveCount > 0 || uploadQueueIdx < uploadTotal) return;

    uploadRunning = false;

    var cancelBtn = document.getElementById('uprog-cancel-btn');
    var scanBtn   = document.getElementById('uprog-scan-btn');
    var doneBtn   = document.getElementById('uprog-done-btn');
    if (cancelBtn) cancelBtn.style.display = 'none';
    if (doneBtn)   doneBtn.style.display   = 'inline-flex';

    var ok     = uploadResults.filter(function(r) { return r.status === 'done';  }).length;
    var failed = uploadResults.filter(function(r) { return r.status === 'error'; }).length;
    var lbl    = document.getElementById('uprog-label');
    if (lbl) {
        lbl.textContent = ok + ' file' + (ok !== 1 ? 's' : '') + ' uploaded'
                        + (failed > 0 ? ', ' + failed + ' failed' : ' successfully');
    }
    renderProgressPage();

    /* We automatically sync uploaded files to the library database.
     * The "Add to Library" button is only shown as a fallback if sync fails. */
    if (ok > 0) {
        var targetDir = (document.getElementById('upload-dir-select') || {}).value || '';
        if (lbl) lbl.innerHTML = (lbl.textContent) + ' &nbsp;<i class="fa-solid fa-spinner fa-spin" style="color:var(--teal)"></i> Syncing to library…';
        mc1Api('POST', '/app/api/tracks.php', {
            action: 'scan',
            directory: targetDir || MC1_AUDIO_ROOT_JS
        }).then(function(d) {
            if (d.ok) {
                if (lbl) lbl.innerHTML = ok + ' file' + (ok !== 1 ? 's' : '') + ' uploaded'
                    + (failed > 0 ? ', ' + failed + ' failed' : '')
                    + ' &nbsp;<i class="fa-solid fa-circle-check" style="color:var(--teal)"></i>'
                    + ' <span style="color:var(--teal)">' + d.added + ' added to library'
                    + (d.skipped > 0 ? ', ' + d.skipped + ' already known' : '') + '</span>';
                if (d.added > 0) loadTracks(currentFolder, 1);
            } else {
                if (lbl) lbl.textContent = ok + ' uploaded' + (failed > 0 ? ', ' + failed + ' failed' : '') + ' — sync failed';
                if (scanBtn) scanBtn.style.display = 'inline-flex';
                mc1Toast(d.error || 'Library sync failed', 'err');
            }
        }).catch(function() {
            if (lbl) lbl.textContent = ok + ' uploaded — sync request failed';
            if (scanBtn) scanBtn.style.display = 'inline-flex';
        });
    } else {
        /* Nothing uploaded successfully — show manual button as fallback */
        if (scanBtn) scanBtn.style.display = 'inline-flex';
    }
}

window.cancelUpload = function() {
    if (!uploadRunning) { hideModal('modal-upload-progress'); return; }
    uploadCancelled = true;
    uploadRunning   = false;
    mc1Toast('Upload cancelled', 'warn');
};

/* Update the overall progress bar and counter */
function updateOverallProgress() {
    var pct   = uploadTotal > 0 ? Math.round(uploadDone / uploadTotal * 100) : 0;
    var bar   = document.getElementById('uprog-bar');
    var pctEl = document.getElementById('uprog-pct');
    var sumEl = document.getElementById('uprog-summary');
    if (bar)   bar.style.width   = pct + '%';
    if (pctEl) pctEl.textContent = pct + '%';
    if (sumEl) sumEl.textContent = uploadDone + ' / ' + uploadTotal + ' files';
    var lbl = document.getElementById('uprog-label');
    if (lbl && uploadRunning) {
        lbl.textContent = 'Uploading… (' + uploadDone + ' of ' + uploadTotal + ')';
    }
}

/* Render per-file list for the current progress page */
function renderProgressPage() {
    var list  = document.getElementById('uprog-file-list');
    var pager = document.getElementById('uprog-pager');
    if (!list) return;

    var totalPages = Math.max(1, Math.ceil((uploadTotal || 1) / UPLOAD_PAGE_SIZE));
    if (uploadProgPage > totalPages) uploadProgPage = totalPages;
    var start = (uploadProgPage - 1) * UPLOAD_PAGE_SIZE;
    var slice = uploadResults.slice(start, start + UPLOAD_PAGE_SIZE);

    list.innerHTML = slice.map(function(r, i) {
        var iconHtml, iconColor;
        switch (r.status) {
            case 'done':
                iconHtml  = '<i class="fa-solid fa-circle-check"></i>';
                iconColor = 'var(--teal)';
                break;
            case 'error':
                iconHtml  = '<i class="fa-solid fa-circle-xmark"></i>';
                iconColor = 'var(--red, #ef4444)';
                break;
            case 'uploading':
                iconHtml  = '<div class="spinner" style="width:13px;height:13px"></div>';
                iconColor = '#f59e0b';
                break;
            default:
                iconHtml  = '<i class="fa-regular fa-circle"></i>';
                iconColor = 'var(--muted)';
        }
        var subdirLine = r.subdir
            ? '<div style="font-size:10px;color:var(--muted);overflow:hidden;text-overflow:ellipsis;white-space:nowrap">'
              + '<i class="fa-solid fa-folder fa-fw" style="font-size:9px;margin-right:3px"></i>'
              + esc(r.subdir) + '</div>'
            : '';
        var sub = '';
        if (r.status === 'uploading') {
            sub = '<div class="uprog-bar-wrap"><div class="uprog-bar-fill"'
                + ' style="background:#f59e0b;width:' + (r.progress || 0) + '%"></div></div>';
        } else if (r.status === 'done') {
            sub = '<div class="uprog-bar-wrap"><div class="uprog-bar-fill"'
                + ' style="background:var(--teal);width:100%"></div></div>';
        } else if (r.status === 'error') {
            sub = '<div style="font-size:10px;color:var(--red,#ef4444);margin-top:2px">'
                + esc(r.error) + '</div>';
        }
        return '<div class="uprog-row" style="' + (i > 0 ? 'border-top:1px solid var(--border)' : '') + '">'
             + '<span class="uprog-icon" style="color:' + iconColor + '">' + iconHtml + '</span>'
             + '<div style="flex:1;min-width:0">'
             + '<div class="uprog-name" title="' + esc(r.name) + '">' + esc(r.name) + '</div>'
             + subdirLine
             + sub
             + '</div>'
             + '<span class="uprog-size">' + fmtBytes(r.size || 0) + '</span>'
             + '</div>';
    }).join('');

    pager.innerHTML = '';
    if (totalPages > 1) {
        renderUploadPager(pager, uploadProgPage, totalPages, function(p) {
            uploadProgPage = p;
            renderProgressPage();
        });
    }
}

/* Trigger a library scan on the upload destination to import the new files */
window.triggerLibraryScan = function() {
    var targetDir = (document.getElementById('upload-dir-select') || {}).value || MC1_AUDIO_ROOT_JS;
    var scanBtn   = document.getElementById('uprog-scan-btn');
    if (scanBtn) { scanBtn.disabled = true; scanBtn.innerHTML = '<i class="fa-solid fa-spinner fa-spin"></i> Scanning…'; }
    mc1Api('POST', '/app/api/tracks.php', {action: 'scan', directory: targetDir}).then(function(d) {
        if (scanBtn) { scanBtn.disabled = false; scanBtn.innerHTML = '<i class="fa-solid fa-database"></i> Add to Library'; }
        if (d.ok) {
            mc1Toast(d.added + ' tracks added to library' + (d.skipped > 0 ? ', ' + d.skipped + ' already known' : ''), 'ok');
            if (d.added > 0) loadTracks(currentFolder, 1);
        } else {
            mc1Toast(d.error || 'Scan failed', 'err');
        }
    }).catch(function() {
        if (scanBtn) { scanBtn.disabled = false; scanBtn.innerHTML = '<i class="fa-solid fa-database"></i> Add to Library'; }
        mc1Toast('Scan request failed', 'err');
    });
};

/* ── Shared paginator for upload UI ────────────────────────────────────── */
function renderUploadPager(container, current, total, onChange) {
    container.innerHTML = '';
    /* Build a compact page list: always show first, last, and up to 2 around current */
    var show = {};
    [1, 2, total - 1, total, current - 1, current, current + 1].forEach(function(p) {
        if (p >= 1 && p <= total) show[p] = true;
    });
    var pages = Object.keys(show).map(Number).sort(function(a, b) { return a - b; });
    var prev  = 0;
    pages.forEach(function(p) {
        if (p - prev > 1) {
            var ell        = document.createElement('span');
            ell.textContent = '…';
            ell.style.cssText = 'padding:2px 4px;color:var(--muted);font-size:12px;line-height:1';
            container.appendChild(ell);
        }
        var btn           = document.createElement('button');
        btn.textContent   = p;
        btn.className     = 'btn btn-sm ' + (p === current ? 'btn-primary' : 'btn-secondary');
        btn.style.cssText = 'min-width:28px;padding:2px 5px;font-size:12px';
        if (p !== current) {
            btn.addEventListener('click', (function(pg) { return function() { onChange(pg); }; })(p));
        }
        container.appendChild(btn);
        prev = p;
    });
}

/* ── Format bytes ───────────────────────────────────────────────────────── */
function fmtBytes(bytes) {
    bytes = bytes || 0;
    if (bytes < 1024)    return bytes + ' B';
    if (bytes < 1048576) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / 1048576).toFixed(1) + ' MB';
}

/* ── Modal helpers ──────────────────────────────────────────────────────── */
window.showModal = function(id) { document.getElementById(id).classList.add('open'); };
window.hideModal = function(id) { document.getElementById(id).classList.remove('open'); };

/* We close modals on backdrop click */
document.querySelectorAll('.mc1-modal-bg').forEach(function(bg) {
    bg.addEventListener('click', function(e) { if (e.target === bg) bg.classList.remove('open'); });
});

/* ── Utility functions ──────────────────────────────────────────────────── */
function fmtDur(ms) {
    var s = Math.floor(ms / 1000), m = Math.floor(s / 60), ss = s % 60;
    return m + ':' + (ss < 10 ? '0' : '') + ss;
}
function esc(s) {
    return String(s || '').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

/* ── Bootstrap on DOMContentLoaded ─────────────────────────────────────── */
/*
 * We MUST wrap all startup calls here. mc1Api is defined in footer.php which is
 * included AFTER this script block. We also restore persisted navigation state
 * so the user lands back where they were before navigating away.
 */
document.addEventListener('DOMContentLoaded', function() {
    /* Attach upload drag-drop + file-picker handlers unconditionally so they
     * work even when the Batch Upload tab is restored as the active tab and
     * the user goes straight to Browse Files without clicking the tab button. */
    setupDropzone();

    /* We prune any PHP-rendered (static) folder nodes that the user previously
     * removed from the library so they don't reappear after a page refresh. */
    if (deletedFolders.size > 0) {
        deletedFolders.forEach(function(fp) {
            removeFolderFromTree(fp);
        });
    }

    var savedTab    = mc1State.get('media', 'tab',    'library');
    var savedFolder = mc1State.get('media', 'folder', '');
    var savedCatId  = mc1State.get('media', 'catId',  null);
    var savedPage   = mc1State.get('media', 'page',   1);
    var savedSearch = mc1State.get('media', 'search', '');

    /* We restore search input */
    if (savedSearch) {
        var qEl = document.getElementById('track-q');
        if (qEl) { qEl.value = savedSearch; curQ = savedSearch; }
    }

    /* We restore active tab — clicking the button triggers all listeners */
    if (savedTab && savedTab !== 'library') {
        var tabBtn = document.querySelector('#media-tabs [data-tab="' + savedTab + '"]');
        if (tabBtn) { tabBtn.click(); return; }
    }

    /* We restore library: folder or category */
    if (savedCatId !== null) {
        /* We restore category view */
        currentCatId = savedCatId;
        var catEl = document.getElementById('cat-item-' + savedCatId);
        if (catEl) {
            catEl.classList.add('active');
            var lbl = catEl.querySelector('.ftree-label');
            if (lbl) document.getElementById('track-context-label').textContent = lbl.textContent + ' (category)';
        }
        fetchCategoryTracks(savedCatId, savedPage);
    } else {
        /* We restore folder view.
         * We load the track list unconditionally.
         * For the tree highlight we must also expand the parent node so the saved
         * subfolder's DOM element exists — root-level nodes are PHP-rendered but
         * their children are lazy-loaded via AJAX only after the user expands them. */
        currentFolder = savedFolder;
        if (savedFolder) {
            document.getElementById('track-context-label').textContent =
                savedFolder.split('/').pop() || 'Audio Root';

            /* We first try to highlight a node that is already in the DOM
             * (happens when savedFolder is a root-level node itself). */
            var alreadyInDom = document.querySelector(
                '.ftree-item[data-path="' + savedFolder.replace(/\\/g,'\\\\').replace(/"/g,'\\"') + '"]'
            );
            if (alreadyInDom) {
                alreadyInDom.classList.add('active');
            } else {
                /* We locate the root tree node whose path is an ancestor of savedFolder,
                 * expand it via the API, and highlight the matching child once rendered. */
                var rootNodes = document.querySelectorAll('#ftree-root > li > .ftree-item[data-path]');
                rootNodes.forEach(function(rootEl) {
                    var rp = rootEl.getAttribute('data-path') || '';
                    if (!rp || !savedFolder.startsWith(rp + '/')) return;
                    /* We extract the nodeId from the onclick attribute */
                    var m = (rootEl.getAttribute('onclick') || '').match(/ftreeToggle\('([^']+)'/);
                    if (!m) return;
                    var nid = m[1];
                    var ul  = document.getElementById('fc-' + nid);
                    var ar  = document.getElementById('fa-' + nid);
                    if (!ul) return;
                    ul.classList.add('open');
                    if (ar) ar.classList.add('open');
                    /* We skip the API call if children are already loaded */
                    if (ul.innerHTML.trim() !== '') {
                        document.querySelectorAll('.ftree-item[data-path]').forEach(function(el) {
                            el.classList.toggle('active', el.getAttribute('data-path') === savedFolder);
                        });
                        return;
                    }
                    /* We fetch and render the children, marking savedFolder active */
                    ul.innerHTML = '<li style="padding:4px 8px 4px 24px;font-size:11px;color:var(--muted)"><div class="spinner" style="width:12px;height:12px;display:inline-block"></div></li>';
                    mc1Api('POST', '/app/api/tracks.php', {action: 'folders', path: rp}).then(function(d) {
                        ul.innerHTML = '';
                        if (!d.ok || !d.folders || !d.folders.length) return;
                        d.folders.filter(function(f) { return !deletedFolders.has(f.path); }).forEach(function(f) {
                            var fnid     = 'n-' + Math.random().toString(36).substr(2, 8);
                            var isActive = f.path === savedFolder;
                            var li       = document.createElement('li');
                            li.id        = 'fnode-' + fnid;
                            li.innerHTML =
                                '<div class="ftree-item' + (isActive ? ' active' : '') + '" '
                                + 'onclick="ftreeToggle(\'' + fnid + '\',\'' + esc(f.path) + '\')" '
                                + 'oncontextmenu="folderCtxShow(event,\'' + esc(f.path) + '\')" '
                                + 'data-path="' + esc(f.path) + '">'
                                + '<span class="ftree-arrow" id="fa-' + fnid + '">'
                                + (f.has_children ? '&#9658;' : '&nbsp;') + '</span>'
                                + '<span class="ftree-icon"><i class="fa-solid fa-folder"></i></span>'
                                + '<span class="ftree-label">' + esc(f.name) + '</span>'
                                + (f.track_count > 0 ? '<span class="ftree-cnt">' + f.track_count + '</span>' : '')
                                + '</div>'
                                + '<ul class="ftree-children" id="fc-' + fnid + '"></ul>';
                            ul.appendChild(li);
                        });
                    }).catch(function() {});
                });
            }
        }
        loadTracks(savedFolder, savedPage, savedSearch || undefined);
    }
});

})();
</script>

<?php require_once __DIR__ . '/app/inc/footer.php'; ?>
