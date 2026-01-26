<?php
/**
 * mediaplayerpro.php — Standalone Popout Media Player
 *
 * File:    src/linux/web_ui/mediaplayerpro.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-02-24
 * Purpose: We provide a self-contained popup-window media player modelled
 *          after classic Windows Media Player — folder tree on the left,
 *          track browser in the centre, queue on the right, transport bar
 *          pinned at the bottom. No sidebar/topbar from the main app.
 *
 *          Opened from mediaplayer.php via window.open(). Keeps playing
 *          while the user navigates the main app window. Queue is DB-backed
 *          and user-scoped (key="u{user_id}") so it persists across page
 *          refreshes and re-logins as the same user.
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - mc1Api / mc1Toast / mc1State are inlined (no footer.php)
 *  - We use first-person plural in all comments
 *
 * CHANGELOG:
 *  2026-02-24  v1.0.0  Initial — WMP-style 3-panel standalone player
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/app/inc/auth.php';
require_once __DIR__ . '/app/inc/mc1_config.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/user_auth.php';

$is_authed = mc1_is_authed();

/* We pre-load audio root for the folder tree seed */
$audio_root = defined('MC1_AUDIO_ROOT') ? rtrim(MC1_AUDIO_ROOT, '/') : '';

/* We pre-load extra library paths so the tree shows all roots */
$extra_roots = [];
if ($is_authed) {
    try {
        $extra_roots = mc1_db('mcaster1_media')
            ->query('SELECT id, path, label FROM media_library_paths WHERE is_active = 1 ORDER BY date_added')
            ->fetchAll(\PDO::FETCH_ASSOC);
    } catch (Exception $e) {}
}

/* We pre-load playlists for the Add to Playlist modal dropdown */
$playlists_for_modal = [];
if ($is_authed) {
    try {
        $playlists_for_modal = mc1_db('mcaster1_media')
            ->query('SELECT id, name, type, track_count FROM playlists ORDER BY name')
            ->fetchAll(\PDO::FETCH_ASSOC);
    } catch (Exception $e) {}
}

/* We pre-load categories for the Add to Category modal dropdown */
$categories_for_modal = [];
if ($is_authed) {
    try {
        $categories_for_modal = mc1_db('mcaster1_media')
            ->query('SELECT id, name, color_hex, icon FROM categories ORDER BY sort_order, name')
            ->fetchAll(\PDO::FETCH_ASSOC);
    } catch (Exception $e) {}
}
?>
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Mcaster1 Player</title>
<link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.5.1/css/all.min.css"
      crossorigin="anonymous" referrerpolicy="no-referrer">
<style>
/* ── Reset + CSS Variables ────────────────────────────────────────────── */
*,*::before,*::after { box-sizing:border-box; margin:0; padding:0; }
html,body { height:100%; overflow:hidden; }
body {
  font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;
  font-size:13px; background:#0f172a; color:#e2e8f0; line-height:1.5;
}
:root {
  --bg:#0f172a; --bg2:#1e293b; --bg3:#162032; --bg4:#0d1a2a;
  --border:#334155; --border2:#263348;
  --teal:#14b8a6; --teal2:#0d9488; --teal-glow:rgba(20,184,166,.15);
  --text:#e2e8f0; --text-dim:#94a3b8; --muted:#64748b;
  --red:#ef4444; --orange:#f97316; --yellow:#eab308;
  --pro-lib-w: 220px;
  --pro-q-w: 280px;
}
a { color:var(--teal); text-decoration:none; }
button { cursor:pointer; font-family:inherit; }
::-webkit-scrollbar { width:5px; height:5px; }
::-webkit-scrollbar-track { background:transparent; }
::-webkit-scrollbar-thumb { background:var(--border); border-radius:3px; }
::-webkit-scrollbar-thumb:hover { background:var(--muted); }

/* ── App Shell — 3 rows: titlebar | body | transport ─────────────────── */
.pro-shell {
  display:grid;
  grid-template-rows: 34px 1fr 76px;
  height:100vh; overflow:hidden;
}

/* ── Title Bar ────────────────────────────────────────────────────────── */
.pro-titlebar {
  background:var(--bg2); border-bottom:1px solid var(--border);
  display:flex; align-items:center; padding:0 10px; gap:8px;
  user-select:none;
}
.pro-titlebar-logo {
  font-size:11px; font-weight:700; color:var(--teal);
  letter-spacing:.06em; text-transform:uppercase; flex:1;
  display:flex; align-items:center; gap:6px;
}
.pro-titlebar-logo i { font-size:13px; }
.pro-titlebar-btn {
  height:22px; padding:0 9px; font-size:11px; border-radius:4px;
  border:1px solid var(--border); background:transparent; color:var(--muted);
  display:flex; align-items:center; gap:5px; cursor:pointer;
  transition:color .12s, border-color .12s;
}
.pro-titlebar-btn:hover { color:var(--text); border-color:var(--teal); }

/* ── Body — 3 panels with resizable dividers ──────────────────────────── */
.pro-body {
  display:flex;
  min-height:0; overflow:hidden;
}

/* ── Pane resize handle ───────────────────────────────────────────────── */
.pane-divider {
  width:4px; flex-shrink:0; cursor:col-resize;
  background:var(--border2); transition:background .15s;
  position:relative; z-index:1;
}
.pane-divider:hover, .pane-divider.dragging { background:var(--teal); }

/* ─── Left panel: Library folder tree ────────────────────────────────── */
.pro-lib {
  width:var(--pro-lib-w); min-width:140px; flex-shrink:0;
  background:var(--bg4);
  display:flex; flex-direction:column; overflow:hidden;
}
.pro-lib-hdr {
  padding:7px 10px 6px; border-bottom:1px solid var(--border);
  font-size:10px; font-weight:700; color:var(--muted);
  letter-spacing:.08em; text-transform:uppercase; flex-shrink:0;
}
.pro-tree { flex:1; overflow-y:auto; padding:4px 0; }

/* Folder tree nodes */
.ptree-item {
  display:flex; align-items:center; gap:5px;
  padding:4px 8px 4px 10px; cursor:pointer; font-size:12px;
  color:var(--text-dim); white-space:nowrap; overflow:hidden;
  text-overflow:ellipsis; user-select:none; border-radius:0;
}
.ptree-item:hover { background:rgba(255,255,255,.04); }
.ptree-item.active { background:var(--teal-glow); color:var(--teal); }
.ptree-arrow { width:11px; text-align:center; flex-shrink:0; font-size:8px;
               transition:transform .12s; color:var(--muted); }
.ptree-arrow.open { transform:rotate(90deg); }
.ptree-icon { font-size:11px; flex-shrink:0; color:var(--muted); }
.ptree-item.active .ptree-icon { color:var(--teal); }
.ptree-label { overflow:hidden; text-overflow:ellipsis; }
.ptree-children { list-style:none; padding:0; margin:0; display:none; }
.ptree-children.open { display:block; }
.ptree-item-wrap { padding-left:16px; }

/* ─── Middle panel: Track Browser ────────────────────────────────────── */
.pro-tracks {
  flex:1; min-width:200px;
  display:flex; flex-direction:column; overflow:hidden; min-height:0;
}
.pro-tracks-bar {
  padding:7px 10px 6px; border-bottom:1px solid var(--border);
  display:flex; align-items:center; gap:7px; flex-shrink:0;
  background:var(--bg2);
}
.pro-tracks-bar input {
  flex:1; height:26px; background:var(--bg3); border:1px solid var(--border);
  border-radius:5px; color:var(--text); padding:0 8px; font-size:12px; outline:none;
}
.pro-tracks-bar input:focus { border-color:var(--teal); }
.pro-tracks-bar input::placeholder { color:var(--muted); }
.pro-ctx-label {
  font-size:10px; color:var(--muted); white-space:nowrap; overflow:hidden;
  text-overflow:ellipsis; max-width:140px; flex-shrink:0;
}
.pro-track-list { flex:1; overflow-y:auto; }
.pro-track-row {
  display:grid; grid-template-columns:28px 1fr 90px 56px;
  align-items:center; gap:8px; padding:5px 10px;
  cursor:pointer; border-bottom:1px solid rgba(51,65,85,.2);
  transition:background .1s; user-select:none;
}
.pro-track-row:hover { background:rgba(255,255,255,.04); }
.pro-track-row.current { background:var(--teal-glow); }
.pro-track-row.selected { background:rgba(20,184,166,.06); }
.pro-track-num { font-size:10px; color:var(--muted); text-align:center;
                 font-variant-numeric:tabular-nums; }
.pro-track-info { min-width:0; }
.pro-track-title  { font-size:12px; color:var(--text); white-space:nowrap;
                    overflow:hidden; text-overflow:ellipsis; }
.pro-track-artist { font-size:10px; color:var(--muted); white-space:nowrap;
                    overflow:hidden; text-overflow:ellipsis; }
.pro-track-album  { font-size:10px; color:var(--muted); white-space:nowrap;
                    overflow:hidden; text-overflow:ellipsis; }
.pro-track-dur { font-size:10px; color:var(--muted); text-align:right;
                 font-variant-numeric:tabular-nums; }
.pro-pagination {
  display:flex; align-items:center; justify-content:center; gap:8px;
  padding:7px; border-top:1px solid var(--border); flex-shrink:0;
  font-size:11px; color:var(--muted);
}
.pro-page-btn {
  height:22px; padding:0 8px; font-size:11px; border-radius:4px;
  border:1px solid var(--border); background:transparent; color:var(--muted);
  cursor:pointer;
}
.pro-page-btn:hover:not(:disabled) { color:var(--teal); border-color:var(--teal); }
.pro-page-btn:disabled { opacity:.35; cursor:default; }

/* Context menu for tracks */
.pro-ctx-menu {
  position:fixed; z-index:9000; background:var(--bg2);
  border:1px solid var(--border); border-radius:6px; padding:4px 0;
  min-width:170px; box-shadow:0 6px 24px rgba(0,0,0,.5);
  display:none; user-select:none;
}
.pro-ctx-item {
  display:flex; align-items:center; gap:8px; padding:7px 14px;
  cursor:pointer; font-size:12px; color:var(--text-dim);
}
.pro-ctx-item:hover { background:var(--bg3); color:var(--text); }
.pro-ctx-item i { width:14px; text-align:center; color:var(--teal); font-size:12px; }

/* ─── Right panel: Queue ──────────────────────────────────────────────── */
.pro-queue {
  width:var(--pro-q-w); min-width:160px; flex-shrink:0;
  display:flex; flex-direction:column; overflow:hidden; min-height:0;
  background:var(--bg);
}
.pro-queue-hdr {
  padding:7px 10px 6px; border-bottom:1px solid var(--border);
  display:flex; align-items:center; justify-content:space-between;
  flex-shrink:0; gap:6px;
}
.pro-queue-hdr-title {
  font-size:10px; font-weight:700; color:var(--muted);
  letter-spacing:.08em; text-transform:uppercase;
  display:flex; align-items:center; gap:6px;
}
.pro-q-count {
  background:var(--bg3); border:1px solid var(--border); border-radius:10px;
  padding:0 6px; font-size:10px; color:var(--muted);
}
.pro-queue-list { flex:1; overflow-y:auto; }

.pro-q-item {
  display:flex; align-items:center; gap:7px; padding:6px 9px;
  cursor:pointer; border-bottom:1px solid rgba(51,65,85,.2);
  transition:background .1s;
}
.pro-q-item:hover { background:rgba(255,255,255,.04); }
.pro-q-item.current { background:var(--teal-glow); border-left:2px solid var(--teal); }
.pro-q-pos {
  width:18px; text-align:center; font-size:9px; color:var(--muted);
  flex-shrink:0; font-variant-numeric:tabular-nums;
}
.pro-q-art {
  width:30px; height:30px; border-radius:3px; object-fit:cover;
  flex-shrink:0; background:var(--bg3);
}
.pro-q-artph {
  width:30px; height:30px; border-radius:3px; background:var(--bg3);
  display:flex; align-items:center; justify-content:center;
  font-size:10px; color:var(--muted); flex-shrink:0;
}
.pro-q-info { flex:1; min-width:0; }
.pro-q-title  { font-size:11px; color:var(--text); white-space:nowrap;
                overflow:hidden; text-overflow:ellipsis; }
.pro-q-artist { font-size:10px; color:var(--muted); white-space:nowrap;
                overflow:hidden; text-overflow:ellipsis; }
.pro-q-dur { font-size:9px; color:var(--muted); flex-shrink:0;
             font-variant-numeric:tabular-nums; }
.pro-q-rm {
  flex-shrink:0; background:none; border:none; color:var(--muted);
  font-size:10px; cursor:pointer; opacity:0; padding:2px 4px; border-radius:3px;
}
.pro-q-item:hover .pro-q-rm { opacity:1; }
.pro-q-rm:hover { color:var(--red); background:rgba(239,68,68,.12); }

.pro-empty {
  display:flex; flex-direction:column; align-items:center; justify-content:center;
  height:100%; gap:6px; color:var(--muted); font-size:11px; padding:20px;
  text-align:center;
}
.pro-empty i { font-size:24px; margin-bottom:4px; }

/* ─── Transport Bar ───────────────────────────────────────────────────── */
.pro-transport {
  background:var(--bg2); border-top:1px solid var(--border);
  display:grid; grid-template-columns:var(--pro-lib-w) 1fr var(--pro-q-w);
  align-items:center; padding:0; overflow:hidden;
}

/* Now-playing mini (left third, matches lib panel width) */
.pro-np-mini {
  display:flex; align-items:center; gap:9px; padding:0 12px;
  min-width:0; overflow:hidden; height:100%;
  border-right:1px solid var(--border);
}
.pro-np-art {
  width:42px; height:42px; border-radius:5px; object-fit:cover;
  flex-shrink:0; background:var(--bg3);
}
.pro-np-artph {
  width:42px; height:42px; border-radius:5px; background:var(--bg3);
  display:flex; align-items:center; justify-content:center;
  font-size:14px; color:var(--muted); flex-shrink:0;
}
.pro-np-info { flex:1; min-width:0; }
.pro-np-title  { font-size:12px; font-weight:600; color:var(--text); white-space:nowrap;
                 overflow:hidden; text-overflow:ellipsis; }
.pro-np-artist { font-size:10px; color:var(--teal); white-space:nowrap;
                 overflow:hidden; text-overflow:ellipsis; }

/* Controls (centre third) */
.pro-ctrl {
  display:flex; flex-direction:column; align-items:center;
  justify-content:center; padding:6px 16px; gap:5px;
}
.pro-seek-row {
  display:flex; align-items:center; gap:8px; width:100%;
}
.pro-time {
  font-size:9px; color:var(--muted); font-variant-numeric:tabular-nums;
  min-width:28px; text-align:center;
}
.pro-seekbar {
  flex:1; -webkit-appearance:none; appearance:none;
  height:3px; border-radius:2px; background:var(--border); outline:none; cursor:pointer;
}
.pro-seekbar::-webkit-slider-thumb {
  -webkit-appearance:none; width:13px; height:13px; border-radius:50%;
  background:var(--teal); cursor:pointer; box-shadow:0 0 4px rgba(20,184,166,.5);
}
.pro-buttons { display:flex; align-items:center; gap:10px; }
.pro-btn {
  background:none; border:none; color:var(--text-dim); font-size:15px;
  cursor:pointer; padding:4px; border-radius:4px;
  transition:color .12s, background .12s; line-height:1;
}
.pro-btn:hover  { color:var(--text); background:rgba(255,255,255,.06); }
.pro-btn.active { color:var(--teal); }
.pro-btn-play {
  width:36px; height:36px; border-radius:50%; background:var(--teal);
  color:#0f172a; font-size:15px; display:flex; align-items:center;
  justify-content:center; border:none; cursor:pointer;
  transition:background .15s, transform .1s;
}
.pro-btn-play:hover { background:var(--teal2); transform:scale(1.06); }

/* Volume (right third, matches queue panel width) */
.pro-vol {
  display:flex; align-items:center; gap:7px; padding:0 12px;
  height:100%; border-left:1px solid var(--border); justify-content:flex-end;
}
.pro-volbar {
  width:80px; -webkit-appearance:none; appearance:none;
  height:3px; border-radius:2px; background:var(--border); outline:none; cursor:pointer;
}
.pro-volbar::-webkit-slider-thumb {
  -webkit-appearance:none; width:12px; height:12px;
  border-radius:50%; background:var(--teal); cursor:pointer;
}

/* ── Auth-wall overlay ────────────────────────────────────────────────── */
.pro-auth-wall {
  position:fixed; inset:0; background:#0f172a; display:flex;
  flex-direction:column; align-items:center; justify-content:center;
  gap:14px; z-index:9999;
}
.pro-auth-wall i { font-size:44px; color:var(--muted); }
.pro-auth-wall h2 { font-size:17px; font-weight:600; color:var(--text); }
.pro-auth-wall p { font-size:12px; color:var(--muted); text-align:center; max-width:260px; }
.pro-auth-wall .pro-auth-btn {
  padding:8px 20px; background:var(--teal); color:#0f172a; border:none;
  border-radius:6px; font-size:13px; font-weight:600; cursor:pointer;
}
.pro-auth-wall .pro-auth-btn:hover { background:var(--teal2); }

/* ── Toast ────────────────────────────────────────────────────────────── */
#mc1-toast {
  display:none; position:fixed; bottom:84px; right:14px; z-index:9998;
  background:#1e293b; border:1px solid #334155; border-radius:7px;
  padding:8px 13px; align-items:center; gap:8px;
  box-shadow:0 4px 20px rgba(0,0,0,.5); font-size:12px; color:#e2e8f0;
  max-width:300px;
}

/* ── Util buttons ─────────────────────────────────────────────────────── */
.btn-xxs {
  height:20px; padding:0 7px; font-size:10px; border-radius:3px;
  border:1px solid var(--border); background:transparent; color:var(--muted);
  cursor:pointer; transition:color .12s, border-color .12s;
}
.btn-xxs:hover { color:var(--red); border-color:var(--red); }

/* ── Context menu separator + danger ─────────────────────────────────── */
.pro-ctx-sep { border:none; border-top:1px solid var(--border); margin:3px 0; }
.pro-ctx-item.danger { color:var(--red); }
.pro-ctx-item.danger i { color:var(--red); }
.pro-ctx-item.danger:hover { background:rgba(239,68,68,.1); color:var(--red); }

/* ── Multi-select track rows ──────────────────────────────────────────── */
.pro-track-row.selected { background:rgba(20,184,166,.07) !important; outline:1px solid rgba(20,184,166,.2); }

/* ── Multi-select action bar ──────────────────────────────────────────── */
.pro-sel-bar {
  position:fixed; bottom:76px; left:0; right:0; z-index:8000;
  background:var(--bg2); border-top:2px solid var(--teal); padding:6px 14px;
  display:none; align-items:center; gap:8px;
  box-shadow:0 -4px 18px rgba(0,0,0,.5);
}

/* ── Modals ───────────────────────────────────────────────────────────── */
.mc1-modal-bg {
  display:none; position:fixed; inset:0; z-index:9500;
  background:rgba(0,0,0,.65); align-items:center; justify-content:center;
}
.mc1-modal-bg.open { display:flex; }
.mc1-modal {
  background:var(--bg2); border:1px solid var(--border); border-radius:8px;
  width:min(640px,96vw); max-height:90vh; display:flex; flex-direction:column;
  box-shadow:0 20px 60px rgba(0,0,0,.6);
}
.mc1-modal-hdr {
  padding:12px 14px; border-bottom:1px solid var(--border);
  display:flex; align-items:center; justify-content:space-between; flex-shrink:0;
}
.mc1-modal-title { font-size:13px; font-weight:600; color:var(--text); }
.mc1-modal-body  { padding:14px; overflow-y:auto; flex:1; }
.mc1-modal-ftr   {
  padding:10px 14px; border-top:1px solid var(--border);
  display:flex; justify-content:flex-end; gap:8px; flex-shrink:0;
}

/* ── Form elements ────────────────────────────────────────────────────── */
.form-row { display:grid; grid-template-columns:1fr; gap:10px; margin-bottom:10px; }
.form-row.cols-2 { grid-template-columns:1fr 1fr; }
.form-row.cols-3 { grid-template-columns:1fr 1fr 1fr; }
.form-group { display:flex; flex-direction:column; gap:4px; }
.form-label { font-size:11px; font-weight:500; color:var(--muted); }
.form-input, .form-select {
  height:30px; background:var(--bg3); border:1px solid var(--border);
  border-radius:5px; color:var(--text); padding:0 9px; font-size:12px;
  font-family:inherit; outline:none;
}
.form-input:focus, .form-select:focus { border-color:var(--teal); }
.form-input::placeholder { color:var(--muted); }
.form-select option { background:var(--bg2); }
.chk-row { display:flex; align-items:center; gap:6px; font-size:12px; cursor:pointer; }
.chk-row input { width:14px; height:14px; cursor:pointer; accent-color:var(--teal); }

/* ── Buttons (modal use) ──────────────────────────────────────────────── */
.btn {
  height:32px; padding:0 14px; border-radius:5px; border:1px solid var(--border);
  font-size:12px; font-family:inherit; cursor:pointer; display:inline-flex;
  align-items:center; gap:6px; transition:background .12s, border-color .12s;
}
.btn-primary   { background:var(--teal); color:#0f172a; border-color:var(--teal); }
.btn-primary:hover  { background:var(--teal2); border-color:var(--teal2); }
.btn-secondary { background:transparent; color:var(--muted); }
.btn-secondary:hover { color:var(--text); border-color:var(--teal); }
.btn-danger    { background:rgba(239,68,68,.1); color:var(--red); border-color:var(--red); }
.btn-danger:hover { background:rgba(239,68,68,.2); }
.btn-sm  { height:26px; padding:0 10px; font-size:11px; }

/* ── Star rating ──────────────────────────────────────────────────────── */
.star-rating { display:flex; gap:4px; cursor:pointer; font-size:20px; }
.star-rating .s { color:var(--muted); transition:color .1s; }
.star-rating .s.on, .star-rating .s.hover { color:#f59e0b; }

/* ── Weight slider ────────────────────────────────────────────────────── */
.weight-wrap { display:flex; align-items:center; gap:10px; }
.weight-wrap input[type=range] { flex:1; accent-color:var(--teal); }
.weight-val {
  min-width:30px; text-align:right; font-variant-numeric:tabular-nums;
  font-size:13px; font-weight:600; color:var(--teal);
}
</style>
</head>
<body>

<?php if (!$is_authed): ?>
<div class="pro-auth-wall">
  <i class="fa-solid fa-lock"></i>
  <h2>Session Expired</h2>
  <p>Your encoder session has expired. Log in again to resume playback.</p>
  <button class="pro-auth-btn" onclick="proReAuth()">
    <i class="fa-solid fa-right-to-bracket"></i> Log In
  </button>
</div>
<script>
function proReAuth() {
  if (window.opener && !window.opener.closed) {
    try { window.opener.location.href = '/login'; window.opener.focus(); } catch(e) {}
    window.close();
  } else { window.location.href = '/login'; }
}
</script>
</body></html>
<?php return; ?>
<?php endif; ?>

<!-- ── Toast ──────────────────────────────────────────────────────────── -->
<div id="mc1-toast">
  <i id="mc1-toast-icon" class="fa-solid fa-circle-check" style="font-size:14px;flex-shrink:0;color:#14b8a6"></i>
  <span id="mc1-toast-msg"></span>
</div>

<!-- Hidden audio element -->
<audio id="mp-audio" preload="auto" style="display:none"></audio>

<!-- Track context menu -->
<div class="pro-ctx-menu" id="pro-ctx-menu">
  <div class="pro-ctx-item" onclick="ctxPlayNow()">
    <i class="fa-solid fa-play"></i> Play Now
  </div>
  <div class="pro-ctx-item" onclick="ctxAddToQueue()">
    <i class="fa-solid fa-list-ul"></i> Add to Queue
  </div>
  <hr class="pro-ctx-sep">
  <div class="pro-ctx-item" onclick="ctxEditTags()">
    <i class="fa-solid fa-pen-to-square"></i> Edit Tags
  </div>
  <div class="pro-ctx-item" onclick="ctxFetchArt()">
    <i class="fa-solid fa-image"></i> Fetch Album Art
  </div>
  <hr class="pro-ctx-sep">
  <div class="pro-ctx-item" onclick="ctxAddToPlaylist()">
    <i class="fa-solid fa-list-music"></i> Add to Playlist
  </div>
  <div class="pro-ctx-item" onclick="ctxNewPlaylistAdd()">
    <i class="fa-solid fa-plus"></i> New Playlist + Add
  </div>
  <div class="pro-ctx-item" onclick="ctxAddToCategory()">
    <i class="fa-solid fa-tag"></i> Add to Category
  </div>
  <hr class="pro-ctx-sep">
  <div class="pro-ctx-item" onclick="ctxLookupMusicBrainz()">
    <i class="fa-solid fa-magnifying-glass"></i> MusicBrainz
  </div>
  <div class="pro-ctx-item" onclick="ctxLookupLastFm()">
    <i class="fa-brands fa-lastfm"></i> Last.fm
  </div>
  <div class="pro-ctx-item" onclick="ctxLookupDiscogs()">
    <i class="fa-solid fa-record-vinyl"></i> Discogs
  </div>
  <hr class="pro-ctx-sep">
  <div class="pro-ctx-item danger" onclick="ctxDelete()">
    <i class="fa-solid fa-trash"></i> Delete from Library
  </div>
</div>

<!-- ── App Shell ─────────────────────────────────────────────────────── -->
<div class="pro-shell">

  <!-- Title Bar -->
  <div class="pro-titlebar">
    <div class="pro-titlebar-logo">
      <i class="fa-solid fa-radio"></i> Mcaster1 Player
    </div>
    <button class="pro-titlebar-btn" onclick="proOpenInTab()" title="Open full media player in main window">
      <i class="fa-solid fa-arrow-up-right-from-square"></i> Open in Tab
    </button>
  </div>

  <!-- Body: library tree | track browser | queue -->
  <div class="pro-body">

    <!-- ── Library Folder Tree (left) + resize handle ──────────────── -->
    <div class="pro-lib">
      <div class="pro-lib-hdr">
        <i class="fa-solid fa-folder-tree fa-fw" style="margin-right:4px;color:var(--teal)"></i>
        Library
      </div>
      <div class="pro-tree" id="ptree-root">
        <!-- Audio root (PHP rendered) -->
        <ul style="list-style:none;padding:0;margin:0">
          <li id="pfnode-root">
            <div class="ptree-item" id="ptree-item-root"
                 onclick="ptreeToggle('root','<?= addslashes($audio_root) ?>')"
                 data-path="<?= htmlspecialchars($audio_root, ENT_QUOTES) ?>">
              <span class="ptree-arrow" id="pfa-root">&#9658;</span>
              <span class="ptree-icon"><i class="fa-solid fa-hard-drive"></i></span>
              <span class="ptree-label"><?= htmlspecialchars(basename($audio_root) ?: 'Audio Root', ENT_QUOTES) ?></span>
            </div>
            <ul class="ptree-children" id="pfc-root"></ul>
          </li>
          <?php foreach ($extra_roots as $ep): ?>
          <li id="pfnode-ep-<?= (int)$ep['id'] ?>">
            <div class="ptree-item"
                 onclick="ptreeToggle('ep-<?= (int)$ep['id'] ?>','<?= addslashes($ep['path']) ?>')"
                 data-path="<?= htmlspecialchars($ep['path'], ENT_QUOTES) ?>">
              <span class="ptree-arrow" id="pfa-ep-<?= (int)$ep['id'] ?>">&#9658;</span>
              <span class="ptree-icon"><i class="fa-solid fa-hard-drive"></i></span>
              <span class="ptree-label"><?= htmlspecialchars($ep['label'] ?: basename($ep['path']), ENT_QUOTES) ?></span>
            </div>
            <ul class="ptree-children" id="pfc-ep-<?= (int)$ep['id'] ?>"></ul>
          </li>
          <?php endforeach; ?>
        </ul>
      </div>
    </div>
    <div class="pane-divider" id="div-left" data-side="left" title="Drag to resize"></div>

    <!-- ── Track Browser (centre) ────────────────────────────────────── -->
    <div class="pro-tracks">
      <!-- Search + context label -->
      <div class="pro-tracks-bar">
        <i class="fa-solid fa-magnifying-glass" style="color:var(--muted);font-size:11px;flex-shrink:0"></i>
        <input type="text" id="ptq-search" placeholder="Search tracks…" autocomplete="off"
               oninput="ptSearchDebounce()">
        <span class="pro-ctx-label" id="pt-ctx-label">All Tracks</span>
      </div>
      <!-- Track list -->
      <div class="pro-track-list" id="pt-track-list">
        <div class="pro-empty" style="height:100%">
          <i class="fa-solid fa-music"></i>
          <div>Select a folder to browse tracks</div>
        </div>
      </div>
      <!-- Pagination -->
      <div class="pro-pagination" id="pt-pagination" style="display:none">
        <button class="pro-page-btn" id="pt-prev-btn" onclick="ptLoadPage(ptPage - 1)">
          <i class="fa-solid fa-chevron-left"></i>
        </button>
        <span id="pt-page-info">1 / 1</span>
        <button class="pro-page-btn" id="pt-next-btn" onclick="ptLoadPage(ptPage + 1)">
          <i class="fa-solid fa-chevron-right"></i>
        </button>
        <span id="pt-total-label" style="margin-left:6px"></span>
      </div>
    </div>

    <div class="pane-divider" id="div-right" data-side="right" title="Drag to resize"></div>

    <!-- ── Queue (right) ──────────────────────────────────────────────── -->
    <div class="pro-queue">
      <div class="pro-queue-hdr">
        <div class="pro-queue-hdr-title">
          <i class="fa-solid fa-list-ul" style="color:var(--teal)"></i>
          Queue <span class="pro-q-count" id="mp-q-count">0</span>
        </div>
        <button class="btn-xxs" onclick="playerClearQueue()" title="Clear queue">
          <i class="fa-solid fa-trash-can"></i>
        </button>
      </div>
      <div class="pro-queue-list" id="mp-queue-list">
        <div class="pro-empty">
          <i class="fa-solid fa-headphones"></i>
          <div>Queue is empty</div>
          <div style="font-size:10px;margin-top:4px">Double-click a track to play</div>
        </div>
      </div>
    </div>

  </div><!-- /pro-body -->

  <!-- ── Transport Bar ─────────────────────────────────────────────── -->
  <div class="pro-transport">

    <!-- Left: now-playing mini -->
    <div class="pro-np-mini">
      <div id="mp-np-art-wrap">
        <div class="pro-np-artph"><i class="fa-solid fa-music"></i></div>
      </div>
      <div class="pro-np-info">
        <div class="pro-np-title"  id="mp-np-title">No track loaded</div>
        <div class="pro-np-artist" id="mp-np-artist"></div>
      </div>
    </div>

    <!-- Centre: controls + seekbar -->
    <div class="pro-ctrl">
      <div class="pro-seek-row">
        <span class="pro-time" id="mp-pos">0:00</span>
        <input type="range" class="pro-seekbar" id="mp-seekbar" min="0" max="1000" value="0" step="1">
        <span class="pro-time" id="mp-dur">0:00</span>
      </div>
      <div class="pro-buttons">
        <button class="pro-btn" id="mp-btn-shuffle" onclick="playerToggleShuffle()" title="Shuffle">
          <i class="fa-solid fa-shuffle"></i>
        </button>
        <button class="pro-btn" onclick="playerPrev()" title="Previous">
          <i class="fa-solid fa-backward-step"></i>
        </button>
        <button class="pro-btn-play" id="mp-btn-play" onclick="playerPlayPause()">
          <i class="fa-solid fa-play" id="mp-play-icon"></i>
        </button>
        <button class="pro-btn" onclick="playerNext()" title="Next">
          <i class="fa-solid fa-forward-step"></i>
        </button>
        <button class="pro-btn" id="mp-btn-repeat" onclick="playerCycleRepeat()" title="Repeat: off">
          <i class="fa-solid fa-repeat"></i>
        </button>
      </div>
    </div>

    <!-- Right: volume -->
    <div class="pro-vol">
      <i class="fa-solid fa-volume-low" style="color:var(--muted);font-size:11px;flex-shrink:0"></i>
      <input type="range" class="pro-volbar" id="mp-volbar" min="0" max="100" value="80">
      <span id="mp-vol-pct" style="font-size:10px;color:var(--muted);min-width:26px;text-align:right">80%</span>
    </div>

  </div><!-- /pro-transport -->

</div><!-- /pro-shell -->

<!-- ── Multi-select action bar ──────────────────────────────────────────── -->
<div class="pro-sel-bar" id="pro-sel-bar"></div>

<!-- Queue item context menu -->
<div class="pro-ctx-menu" id="pro-q-ctx-menu" style="min-width:160px">
  <div class="pro-ctx-item" onclick="qCtxPlayNow()">
    <i class="fa-solid fa-play"></i> Play Now
  </div>
  <div class="pro-ctx-item" onclick="qCtxMoveToTop()">
    <i class="fa-solid fa-arrow-up-to-line"></i> Move to Top
  </div>
  <hr class="pro-ctx-sep">
  <div class="pro-ctx-item danger" onclick="qCtxRemove()">
    <i class="fa-solid fa-xmark"></i> Remove from Queue
  </div>
</div>

<!-- ══════════════════════ MODAL: EDIT TAGS ═══════════════════════════════ -->
<div class="mc1-modal-bg" id="modal-edit">
  <div class="mc1-modal">
    <div class="mc1-modal-hdr">
      <span class="mc1-modal-title"><i class="fa-solid fa-pen-to-square" style="color:var(--teal);margin-right:6px"></i>Edit Track Tags</span>
      <button class="btn btn-secondary btn-sm" onclick="hideModal('modal-edit')"><i class="fa-solid fa-xmark"></i></button>
    </div>
    <div class="mc1-modal-body">
      <input type="hidden" id="edit-track-id">
      <div class="form-row cols-2">
        <div class="form-group"><label class="form-label">Title</label><input type="text" class="form-input" id="edit-title"></div>
        <div class="form-group"><label class="form-label">Artist</label><input type="text" class="form-input" id="edit-artist"></div>
      </div>
      <div class="form-row cols-2">
        <div class="form-group"><label class="form-label">Album</label><input type="text" class="form-input" id="edit-album"></div>
        <div class="form-group"><label class="form-label">Genre</label><input type="text" class="form-input" id="edit-genre"></div>
      </div>
      <div class="form-row cols-3">
        <div class="form-group"><label class="form-label">Year</label><input type="number" class="form-input" id="edit-year" min="1900" max="2099"></div>
        <div class="form-group"><label class="form-label">BPM</label><input type="number" class="form-input" id="edit-bpm" min="0" max="300" step="0.5"></div>
        <div class="form-group"><label class="form-label">Energy (0–1)</label><input type="number" class="form-input" id="edit-energy" min="0" max="1" step="0.01"></div>
      </div>
      <div class="form-row cols-2">
        <div class="form-group"><label class="form-label">Mood Tag</label><input type="text" class="form-input" id="edit-mood" placeholder="e.g. upbeat, mellow"></div>
        <div class="form-group"><label class="form-label">Musical Key</label><input type="text" class="form-input" id="edit-key" placeholder="e.g. Cmaj, Am"></div>
      </div>
      <div class="form-row cols-2">
        <div class="form-group">
          <label class="form-label">Rating</label>
          <div class="star-rating" id="edit-stars">
            <span class="s" data-v="1">&#9733;</span><span class="s" data-v="2">&#9733;</span>
            <span class="s" data-v="3">&#9733;</span><span class="s" data-v="4">&#9733;</span>
            <span class="s" data-v="5">&#9733;</span>
          </div>
          <input type="hidden" id="edit-rating" value="3">
        </div>
        <div class="form-group"><label class="form-label">Track #</label><input type="number" class="form-input" id="edit-tracknum" min="1" max="999"></div>
      </div>
      <div class="form-row cols-2">
        <div class="form-group"><label class="form-label">Intro (ms)</label><input type="number" class="form-input" id="edit-intro" min="0"></div>
        <div class="form-group"><label class="form-label">Outro (ms)</label><input type="number" class="form-input" id="edit-outro" min="0"></div>
      </div>
      <div class="form-row cols-2">
        <div class="form-group"><label class="form-label">Cue In (ms)</label><input type="number" class="form-input" id="edit-cuein" min="0"></div>
        <div class="form-group"><label class="form-label">Cue Out (ms)</label><input type="number" class="form-input" id="edit-cueout" min="0"></div>
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
      <button class="btn btn-primary" onclick="saveEditTags()"><i class="fa-solid fa-floppy-disk"></i> Save Changes</button>
    </div>
  </div>
</div>

<!-- ══════════════════════ MODAL: ADD TO PLAYLIST ═════════════════════════ -->
<div class="mc1-modal-bg" id="modal-addpl">
  <div class="mc1-modal" style="width:min(420px,96vw)">
    <div class="mc1-modal-hdr">
      <span class="mc1-modal-title"><i class="fa-solid fa-list-music" style="color:var(--teal);margin-right:6px"></i>Add to Playlist</span>
      <button class="btn btn-secondary btn-sm" onclick="hideModal('modal-addpl')"><i class="fa-solid fa-xmark"></i></button>
    </div>
    <div class="mc1-modal-body">
      <div class="form-group" style="margin-bottom:14px">
        <label class="form-label">Playlist</label>
        <select class="form-select" id="addpl-select">
          <?php foreach ($playlists_for_modal as $pl): ?>
          <option value="<?= (int)$pl['id'] ?>"><?= htmlspecialchars($pl['name'], ENT_QUOTES) ?> (<?= (int)$pl['track_count'] ?> tracks)</option>
          <?php endforeach; ?>
          <?php if (!$playlists_for_modal): ?>
          <option value="">— No playlists yet —</option>
          <?php endif; ?>
        </select>
      </div>
      <div class="form-group">
        <label class="form-label">Rotation Weight <span style="font-size:10px;color:var(--muted);margin-left:4px">— clockwheel playback frequency</span></label>
        <div class="weight-wrap">
          <span style="font-size:11px;color:var(--muted)">Rare<br>0.1</span>
          <input type="range" id="addpl-weight" min="0.1" max="10" step="0.1" value="1.0"
                 oninput="document.getElementById('addpl-weight-val').textContent=parseFloat(this.value).toFixed(1)">
          <span style="font-size:11px;color:var(--muted)">Often<br>10.0</span>
          <span class="weight-val" id="addpl-weight-val">1.0</span>
        </div>
        <div style="font-size:11px;color:var(--muted);margin-top:4px">Weight 1.0 = normal &nbsp; 3.0 = plays 3× as often &nbsp; 0.3 = rarely</div>
      </div>
    </div>
    <div class="mc1-modal-ftr">
      <button class="btn btn-secondary" onclick="hideModal('modal-addpl')">Cancel</button>
      <button class="btn btn-primary" onclick="doAddToPlaylist()"><i class="fa-solid fa-plus"></i> Add Track</button>
    </div>
  </div>
</div>

<!-- ══════════════════════ MODAL: NEW PLAYLIST + ADD ══════════════════════ -->
<div class="mc1-modal-bg" id="modal-newpl">
  <div class="mc1-modal" style="width:min(420px,96vw)">
    <div class="mc1-modal-hdr">
      <span class="mc1-modal-title"><i class="fa-solid fa-plus" style="color:var(--teal);margin-right:6px"></i>New Playlist</span>
      <button class="btn btn-secondary btn-sm" onclick="hideModal('modal-newpl')"><i class="fa-solid fa-xmark"></i></button>
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
      <button class="btn btn-primary" onclick="doNewPlaylistAdd()"><i class="fa-solid fa-plus"></i> Create &amp; Add</button>
    </div>
  </div>
</div>

<!-- ══════════════════════ MODAL: DELETE CONFIRM ══════════════════════════ -->
<div class="mc1-modal-bg" id="modal-del">
  <div class="mc1-modal" style="width:min(380px,96vw)">
    <div class="mc1-modal-hdr">
      <span class="mc1-modal-title" style="color:var(--red)"><i class="fa-solid fa-trash" style="margin-right:6px"></i>Delete Track</span>
      <button class="btn btn-secondary btn-sm" onclick="hideModal('modal-del')"><i class="fa-solid fa-xmark"></i></button>
    </div>
    <div class="mc1-modal-body">
      <p style="margin:0 0 14px">Remove <strong id="del-track-name"></strong> from the library?</p>
      <label class="chk-row" style="color:var(--red)">
        <input type="checkbox" id="del-also-file">
        <strong>Also permanently delete the file from disk</strong>
        <span style="font-size:11px;color:var(--muted);margin-left:4px">(cannot be undone)</span>
      </label>
    </div>
    <div class="mc1-modal-ftr">
      <button class="btn btn-secondary" onclick="hideModal('modal-del')">Cancel</button>
      <button class="btn btn-danger" onclick="doDeleteTrack()"><i class="fa-solid fa-trash"></i> Delete</button>
    </div>
  </div>
</div>

<!-- ══════════════════════ MODAL: ADD TO CATEGORY ═════════════════════════ -->
<div class="mc1-modal-bg" id="modal-addcat">
  <div class="mc1-modal" style="width:min(380px,96vw)">
    <div class="mc1-modal-hdr">
      <span class="mc1-modal-title"><i class="fa-solid fa-tag" style="color:var(--teal);margin-right:6px"></i>Add to Category</span>
      <button class="btn btn-secondary btn-sm" onclick="hideModal('modal-addcat')"><i class="fa-solid fa-xmark"></i></button>
    </div>
    <div class="mc1-modal-body">
      <div class="form-group">
        <label class="form-label">Category</label>
        <select class="form-select" id="addcat-select"></select>
      </div>
    </div>
    <div class="mc1-modal-ftr">
      <button class="btn btn-secondary" onclick="hideModal('modal-addcat')">Cancel</button>
      <button class="btn btn-primary" onclick="doAddToCategory()"><i class="fa-solid fa-plus"></i> Add to Category</button>
    </div>
  </div>
</div>

<script>
(function() {
'use strict';

/* ── PHP-injected data ────────────────────────────────────────────────── */
var proCategories = <?= json_encode($categories_for_modal) ?>;

/* ── Inlined mc1State ────────────────────────────────────────────────── */
window.mc1State = (function() {
  var P = 'mc1.';
  function k(p, n) { return P + p + '.' + n; }
  return {
    get: function(pg, nm, def) {
      try { var v = localStorage.getItem(k(pg,nm)); return v !== null ? JSON.parse(v) : def; } catch(e) { return def; }
    },
    set: function(pg, nm, val) {
      try { localStorage.setItem(k(pg,nm), JSON.stringify(val)); } catch(e) {}
    },
    del: function(pg, nm) {
      try { localStorage.removeItem(k(pg,nm)); } catch(e) {}
    }
  };
})();

/* ── Inlined mc1Toast ────────────────────────────────────────────────── */
var _tTimer = null;
window.mc1Toast = function(msg, type) {
  var el = document.getElementById('mc1-toast');
  var ic = document.getElementById('mc1-toast-icon');
  var tx = document.getElementById('mc1-toast-msg');
  if (!el) return;
  ic.className = type==='err' ? 'fa-solid fa-circle-xmark' : type==='warn' ? 'fa-solid fa-triangle-exclamation' : 'fa-solid fa-circle-check';
  ic.style.color = type==='err' ? '#ef4444' : type==='warn' ? '#eab308' : '#14b8a6';
  tx.textContent = msg;
  el.style.display = 'flex';
  if (_tTimer) clearTimeout(_tTimer);
  _tTimer = setTimeout(function(){ el.style.display='none'; }, 4000);
};

/* ── Inlined mc1Api ──────────────────────────────────────────────────── */
window.mc1Api = function(method, url, body) {
  var opts = { method:method, headers:{'Content-Type':'application/json'}, credentials:'same-origin' };
  if (body !== undefined && body !== null) opts.body = JSON.stringify(body);
  return fetch(url, opts).then(function(r){
    return r.json().then(function(d){ d._status = r.status; return d; });
  });
};

/* ── PHP session auto-bootstrap ──────────────────────────────────────── */
(function(){
  if (sessionStorage.getItem('mc1php_ok')) return;
  mc1Api('POST','/app/api/auth.php',{action:'auto_login'}).then(function(d){
    if (d&&d.ok) sessionStorage.setItem('mc1php_ok','1');
  }).catch(function(){});
})();

/* ── "Open in Tab" ───────────────────────────────────────────────────── */
window.proOpenInTab = function() {
  if (window.opener && !window.opener.closed) {
    try { window.opener.location.href='/mediaplayer.php'; window.opener.focus(); return; } catch(e) {}
  }
  window.open('/mediaplayer.php', '_blank');
};

/* ── Session expired overlay ─────────────────────────────────────────── */
function showSessionExpired() {
  var w = document.createElement('div');
  w.className = 'pro-auth-wall';
  w.innerHTML = '<i class="fa-solid fa-lock"></i><h2>Session Expired</h2>'
    + '<p>Your encoder session has expired. Log in again to resume — your queue is saved.</p>'
    + '<button class="pro-auth-btn" onclick="proReAuth()"><i class="fa-solid fa-right-to-bracket"></i> Log In</button>';
  document.body.appendChild(w);
}
window.proReAuth = function() {
  if (window.opener && !window.opener.closed) {
    try { window.opener.location.href='/login'; window.opener.focus(); } catch(e) {}
    window.close();
  } else { window.location.href='/login'; }
};

/* ════════════════════════════════════════════════════════════════════════
 * FOLDER TREE
 * ════════════════════════════════════════════════════════════════════════ */
var ptCurrentFolder = '';

window.ptreeToggle = function(nodeId, path) {
  var arrow    = document.getElementById('pfa-' + nodeId);
  var children = document.getElementById('pfc-' + nodeId);
  if (!children || !arrow) return;

  var isOpen = children.classList.contains('open');
  if (isOpen) {
    children.classList.remove('open');
    arrow.classList.remove('open');
    return;
  }

  /* Set active and load tracks */
  ptSetFolder(path);

  if (children.innerHTML.trim() !== '') {
    children.classList.add('open');
    arrow.classList.add('open');
    return;
  }

  children.innerHTML = '<li style="padding:4px 8px 4px 22px;font-size:10px;color:var(--muted)">'
    + '<i class="fa-solid fa-spinner fa-spin" style="font-size:9px"></i></li>';
  children.classList.add('open');
  arrow.classList.add('open');

  mc1Api('POST','/app/api/tracks.php',{action:'folders',path:path}).then(function(d){
    children.innerHTML = '';
    if (!d.ok || !d.folders || !d.folders.length) {
      children.innerHTML = '<li style="padding:4px 8px 4px 22px;font-size:10px;color:var(--muted);font-style:italic">Empty</li>';
      return;
    }
    d.folders.forEach(function(f){
      var nid = 'n-' + Math.random().toString(36).substr(2,8);
      var li  = document.createElement('li');
      li.id   = 'pfnode-' + nid;
      var indent = 'style="padding-left:' + ((path.split('/').length - (ptCurrentFolder||path).split('/').length + 2) * 10 + 10) + 'px"';
      li.innerHTML =
        '<div class="ptree-item" onclick="ptreeToggle(\'' + _e(nid) + '\',\'' + _e(f.path) + '\')"'
        + ' data-path="' + _e(f.path) + '" id="ptree-item-' + _e(nid) + '">'
        + '<span class="ptree-arrow" id="pfa-' + _e(nid) + '">' + (f.has_children ? '&#9658;' : '&nbsp;') + '</span>'
        + '<span class="ptree-icon"><i class="fa-solid fa-folder"></i></span>'
        + '<span class="ptree-label">' + _e(f.name) + '</span>'
        + '</div>'
        + '<ul class="ptree-children" id="pfc-' + _e(nid) + '"></ul>';
      children.appendChild(li);
    });
  }).catch(function(){
    children.innerHTML = '<li style="padding:4px 8px 4px 22px;font-size:10px;color:var(--red)">Failed</li>';
  });
};

function ptSetFolder(path) {
  ptCurrentFolder = path;
  document.querySelectorAll('.ptree-item').forEach(function(el){
    el.classList.toggle('active', el.getAttribute('data-path') === path);
  });
  /* Update context label */
  var lbl = path.split('/').pop() || 'Audio Root';
  document.getElementById('pt-ctx-label').textContent = lbl;
  ptPage = 1;
  ptLoadPage(1);
}

/* ════════════════════════════════════════════════════════════════════════
 * TRACK BROWSER
 * ════════════════════════════════════════════════════════════════════════ */
var ptPage       = 1;
var ptTotalPages = 1;
var ptTotalTracks = 0;
var ptPerPage    = 100;
var ptSearchVal  = '';
var ptSearchTimer = null;
var ptTracks     = [];   /* current page of track objects */
var ptSelIdx     = -1;   /* selected track index in ptTracks */
var ctxTrackId   = null; /* track ID under context menu */

function ptSearchDebounce() {
  if (ptSearchTimer) clearTimeout(ptSearchTimer);
  ptSearchTimer = setTimeout(function(){
    ptSearchVal = document.getElementById('ptq-search').value.trim();
    ptPage = 1;
    ptLoadPage(1);
  }, 280);
}

function ptLoadPage(page) {
  if (page < 1) page = 1;
  if (page > ptTotalPages && ptTotalPages > 0) page = ptTotalPages;
  ptPage = page;

  var body = { page: ptPage, per_page: ptPerPage };
  if (ptSearchVal) {
    body.action = 'search';
    body.q = ptSearchVal;
  } else {
    body.action = 'list';
    body.folder = ptCurrentFolder;
  }

  var listEl = document.getElementById('pt-track-list');
  listEl.innerHTML = '<div class="pro-empty" style="height:100%"><i class="fa-solid fa-spinner fa-spin"></i></div>';

  mc1Api('POST','/app/api/tracks.php', body).then(function(d){
    if (!d.ok) { listEl.innerHTML = '<div class="pro-empty" style="height:100%"><i class="fa-solid fa-circle-xmark"></i><div>Load failed</div></div>'; return; }
    ptTracks = d.data || [];
    var total = d.total || ptTracks.length;
    ptTotalTracks = total;
    ptTotalPages = Math.max(1, Math.ceil(total / ptPerPage));

    /* Pagination bar */
    var pgEl = document.getElementById('pt-pagination');
    var piEl = document.getElementById('pt-page-info');
    var tlEl = document.getElementById('pt-total-label');
    if (ptTotalPages > 1) {
      pgEl.style.display = 'flex';
      piEl.textContent = ptPage + ' / ' + ptTotalPages;
      tlEl.textContent = total + ' tracks';
      document.getElementById('pt-prev-btn').disabled = (ptPage <= 1);
      document.getElementById('pt-next-btn').disabled = (ptPage >= ptTotalPages);
    } else {
      pgEl.style.display = (total > 0) ? 'flex' : 'none';
      if (total > 0) { piEl.textContent = ''; tlEl.textContent = total + ' tracks'; }
    }

    if (!ptTracks.length) {
      listEl.innerHTML = '<div class="pro-empty" style="height:100%"><i class="fa-solid fa-music"></i><div>No tracks found</div></div>';
      return;
    }

    var html = '';
    for (var i = 0; i < ptTracks.length; i++) {
      var t = ptTracks[i];
      var dur = t.duration_ms > 0 ? fmtTime(t.duration_ms/1000) : '';
      html += '<div class="pro-track-row" data-idx="' + i + '" data-tid="' + t.id + '"'
            + ' ondblclick="ptDblClick(' + i + ')"'
            + ' onclick="ptClick(event,' + i + ')"'
            + ' oncontextmenu="ptCtxShow(event,' + i + ')">'
            + '<span class="pro-track-num">' + ((ptPage-1)*ptPerPage + i + 1) + '</span>'
            + '<div class="pro-track-info">'
            + '<div class="pro-track-title">' + _e(t.title || t.file_path && t.file_path.split('/').pop() || '—') + '</div>'
            + '<div class="pro-track-artist">' + _e(t.artist || '') + (t.album ? ' · ' + _e(t.album) : '') + '</div>'
            + '</div>'
            + '<div class="pro-track-album">' + _e(t.genre || '') + '</div>'
            + '<span class="pro-track-dur">' + dur + '</span>'
            + '</div>';
    }
    listEl.innerHTML = html;
    ptSelIdx = -1;

  }).catch(function(){
    listEl.innerHTML = '<div class="pro-empty" style="height:100%"><i class="fa-solid fa-circle-xmark"></i><div>Request failed</div></div>';
  });
}

/* Track row single click — select (Ctrl+click for multi-select) */
window.ptClick = function(e, idx) {
  var t = ptTracks[idx];
  if (!t) return;
  if (e && e.ctrlKey) {
    /* We toggle this track in the multi-selection */
    var row = document.querySelector('.pro-track-row[data-idx="' + idx + '"]');
    if (selIds.has(t.id)) {
      selIds.delete(t.id);
      if (row) row.classList.remove('selected');
    } else {
      selIds.add(t.id);
      if (row) row.classList.add('selected');
    }
    ptSelIdx = idx;
    updateSelBar();
  } else {
    /* We clear multi-select and select only this row */
    clearProSelection();
    ptSelIdx = idx;
    document.querySelectorAll('.pro-track-row').forEach(function(el){
      el.classList.toggle('selected', parseInt(el.getAttribute('data-idx')) === idx);
    });
    selIds.add(t.id);
    updateSelBar();
  }
};

/* Track row double-click — add to queue + play */
window.ptDblClick = function(idx) {
  var t = ptTracks[idx];
  if (!t) return;
  mc1Api('POST','/app/api/player.php',{action:'queue_add',track_id:t.id}).then(function(d){
    if (d.ok) {
      queue = d.queue || [];
      renderQueue();
      /* We jump to the track we just added (last in queue) */
      playerJumpTo(queue.length - 1);
    } else {
      mc1Toast(d.error || 'Could not add track', 'err');
    }
  }).catch(function(){ mc1Toast('Request failed','err'); });
};

/* ── Multi-select state ───────────────────────────────────────────────── */
var selIds     = new Set(); /* We track multi-selected track IDs */
var selDragging = false;

function clearProSelection() {
  selIds.clear();
  document.querySelectorAll('.pro-track-row.selected').forEach(function(el){
    el.classList.remove('selected');
  });
  updateSelBar();
}

function updateSelBar() {
  var bar = document.getElementById('pro-sel-bar');
  if (!bar) return;
  if (selIds.size === 0) { bar.style.display = 'none'; return; }
  bar.style.display = 'flex';
  bar.innerHTML = '<strong style="color:var(--text);white-space:nowrap">'
    + selIds.size + ' track' + (selIds.size !== 1 ? 's' : '') + ' selected</strong>'
    + '<button class="btn btn-secondary btn-sm" onclick="selAddToPlaylist()">'
    + '<i class="fa-solid fa-list-music"></i> Add to Playlist</button>'
    + '<button class="btn btn-secondary btn-sm" onclick="selAddToQueue()">'
    + '<i class="fa-solid fa-headphones"></i> Add to Queue</button>'
    + '<button class="btn btn-danger btn-sm" onclick="selDeleteAll()">'
    + '<i class="fa-solid fa-trash"></i> Delete Selected</button>'
    + '<button class="btn btn-secondary btn-sm" style="margin-left:auto" onclick="clearProSelection()" title="Clear selection">'
    + '<i class="fa-solid fa-xmark"></i></button>';
}

window.selAddToPlaylist = function() {
  if (!selIds.size) return;
  var modal = document.getElementById('modal-addpl');
  modal.dataset.bulkIds = JSON.stringify(Array.from(selIds));
  showModal('modal-addpl');
};

window.selAddToQueue = function() {
  if (!selIds.size) return;
  var ids = Array.from(selIds);
  var chain = Promise.resolve(); var added = 0;
  ids.forEach(function(id) {
    chain = chain.then(function() {
      return mc1Api('POST','/app/api/player.php',{action:'queue_add',track_id:id}).then(function(d){
        if (d.ok) { queue = d.queue || []; added++; }
      });
    });
  });
  chain.then(function() {
    renderQueue();
    mc1Toast(added + ' track' + (added !== 1 ? 's' : '') + ' added to queue', 'ok');
    clearProSelection();
  });
};

window.selDeleteAll = function() {
  if (!selIds.size) return;
  if (!confirm('Delete ' + selIds.size + ' track(s) from the library?')) return;
  var ids = Array.from(selIds);
  var chain = Promise.resolve(); var deleted = 0;
  ids.forEach(function(id) {
    chain = chain.then(function() {
      return mc1Api('POST','/app/api/tracks.php',{action:'delete',id:id,delete_file:false}).then(function(d){
        if (d.ok) deleted++;
      });
    });
  });
  chain.then(function() {
    mc1Toast(deleted + ' track(s) deleted', 'ok');
    clearProSelection();
    ptLoadPage(ptPage);
  });
};

/* ── Modal helpers ───────────────────────────────────────────────────────── */
function showModal(id) {
  var m = document.getElementById(id);
  if (m) m.classList.add('open');
}
window.hideModal = function(id) {
  var m = document.getElementById(id);
  if (m) m.classList.remove('open');
};

/* Track context menu */
var ctxTrack = null;
window.ptCtxShow = function(e, idx) {
  e.preventDefault();
  ptSelIdx = idx;
  ctxTrack = ptTracks[idx] || null;
  /* We also mark this track as selected in the UI */
  if (!selIds.has(ctxTrack && ctxTrack.id)) {
    clearProSelection();
    if (ctxTrack) { selIds.add(ctxTrack.id); }
    document.querySelectorAll('.pro-track-row').forEach(function(el){
      el.classList.toggle('selected', parseInt(el.getAttribute('data-idx')) === idx);
    });
  }
  var menu = document.getElementById('pro-ctx-menu');
  menu.style.display = 'block';
  var vw = window.innerWidth, vh = window.innerHeight;
  var mw = menu.offsetWidth || 200, mh = menu.offsetHeight || 80;
  menu.style.left = Math.min(e.clientX, vw-mw-6) + 'px';
  menu.style.top  = Math.min(e.clientY, vh-mh-6) + 'px';
};
document.addEventListener('click', function(e){
  /* Close context menu on any click outside it */
  var m = document.getElementById('pro-ctx-menu');
  if (m && !m.contains(e.target)) m.style.display = 'none';
  /* Close modals on backdrop click */
  document.querySelectorAll('.mc1-modal-bg.open').forEach(function(bg){
    if (e.target === bg) bg.classList.remove('open');
  });
});

/* Play Now: add to queue + immediately play */
window.ctxPlayNow = function() {
  if (!ctxTrack) return;
  mc1Api('POST','/app/api/player.php',{action:'queue_add',track_id:ctxTrack.id}).then(function(d){
    if (d.ok) {
      queue = d.queue || [];
      renderQueue();
      playerJumpTo(queue.length - 1);
    } else { mc1Toast(d.error||'Could not add track','err'); }
  }).catch(function(){ mc1Toast('Request failed','err'); });
};

/* Add to Queue: add without changing current playback */
window.ctxAddToQueue = function() {
  if (!ctxTrack) return;
  mc1Api('POST','/app/api/player.php',{action:'queue_add',track_id:ctxTrack.id}).then(function(d){
    if (d.ok) {
      queue = d.queue || [];
      renderQueue();
      mc1Toast('Added to queue', 'ok');
    } else { mc1Toast(d.error||'Could not add track','err'); }
  }).catch(function(){ mc1Toast('Request failed','err'); });
};

/* ════════════════════════════════════════════════════════════════════════
 * PLAYER ENGINE
 * ════════════════════════════════════════════════════════════════════════ */
var queue      = [];
var currentIdx = -1;
var shuffle    = false;
var repeatMode = 0;
var seeking    = false;

var audio   = document.getElementById('mp-audio');
var seekbar = document.getElementById('mp-seekbar');
var volbar  = document.getElementById('mp-volbar');

audio.addEventListener('timeupdate', function(){
  if (seeking || !audio.duration) return;
  seekbar.value = Math.floor((audio.currentTime / audio.duration) * 1000);
  document.getElementById('mp-pos').textContent = fmtTime(audio.currentTime);
});
audio.addEventListener('loadedmetadata', function(){
  document.getElementById('mp-dur').textContent = fmtTime(audio.duration);
});
audio.addEventListener('ended', function(){ playerNext(); });
audio.addEventListener('play',  function(){ document.getElementById('mp-play-icon').className = 'fa-solid fa-pause'; });
audio.addEventListener('pause', function(){ document.getElementById('mp-play-icon').className = 'fa-solid fa-play'; });
audio.addEventListener('error', function(){
  mc1Api('POST','/app/api/player.php',{action:'queue_list'}).then(function(d){
    if (d._status===403) showSessionExpired();
    else mc1Toast('Playback error — file may be missing','err');
  }).catch(function(){ mc1Toast('Playback error','err'); });
});

seekbar.addEventListener('mousedown',  function(){ seeking=true; });
seekbar.addEventListener('touchstart', function(){ seeking=true; });
seekbar.addEventListener('mouseup',  function(){ seeking=false; if(audio.duration) audio.currentTime=(seekbar.value/1000)*audio.duration; });
seekbar.addEventListener('touchend', function(){ seeking=false; if(audio.duration) audio.currentTime=(seekbar.value/1000)*audio.duration; });
seekbar.addEventListener('input', function(){
  if (audio.duration) document.getElementById('mp-pos').textContent = fmtTime((seekbar.value/1000)*audio.duration);
});

volbar.addEventListener('input', function(){
  audio.volume = volbar.value/100;
  document.getElementById('mp-vol-pct').textContent = volbar.value + '%';
  mc1State.set('player','volume',parseInt(volbar.value));
});

window.playerPlayPause = function(){
  if (!queue.length){ mc1Toast('Queue is empty','warn'); return; }
  if (currentIdx<0){ playerJumpTo(0); return; }
  if (audio.paused) audio.play().catch(function(e){ mc1Toast('Blocked: '+e.message,'err'); });
  else audio.pause();
};

window.playerNext = function(){
  if (!queue.length) return;
  var next;
  if (repeatMode===1){ next = currentIdx<0?0:currentIdx; }
  else if (shuffle){
    var pool=[]; for(var i=0;i<queue.length;i++) if(i!==currentIdx) pool.push(i);
    if (!pool.length){ if(repeatMode===2) pool=[0]; else { audio.pause(); return; } }
    next = pool[Math.floor(Math.random()*pool.length)];
  } else {
    next = currentIdx+1;
    if (next>=queue.length){ if(repeatMode===2) next=0; else { audio.pause(); currentIdx=-1; updateNowPlaying(null); renderQueue(); return; } }
  }
  playerJumpTo(next);
};

window.playerPrev = function(){
  if (!queue.length) return;
  if (audio.currentTime>3){ audio.currentTime=0; return; }
  var prev=currentIdx-1;
  if (prev<0) prev=(repeatMode===2)?queue.length-1:0;
  playerJumpTo(prev);
};

window.playerJumpTo = function(idx){
  if (idx<0||idx>=queue.length) return;
  currentIdx=idx;
  var item=queue[idx];
  mc1State.set('player','queueId',item.queue_id||null);
  audio.src='/app/api/audio.php?id='+item.track_id;
  audio.load();
  audio.play().catch(function(e){ mc1Toast('Cannot play: '+(e.message||'error'),'err'); });
  updateNowPlaying(item);
  renderQueue();
  /* Highlight track in browser if it matches current folder */
  document.querySelectorAll('.pro-track-row').forEach(function(el){
    el.classList.toggle('current', parseInt(el.getAttribute('data-tid'))===item.track_id);
  });
  /* We auto-fetch album art from MusicBrainz if the track has none */
  if (!item.art_url && item.track_id && (item.artist || item.title)) {
    mc1Api('POST','/app/api/tracks.php',{
      action:'fetch_art', track_id:item.track_id,
      artist:item.artist||'', title:item.title||'', album:item.album||'', embed_in_file:false
    }).then(function(d){
      if (d.ok && d.art_url) {
        item.art_url = d.art_url;
        /* We only update if this track is still the current one */
        if (queue[currentIdx] && queue[currentIdx].track_id === item.track_id) {
          updateNowPlaying(item);
          renderQueue();
        }
      }
    }).catch(function(){});
  }
};

window.playerToggleShuffle = function(){
  shuffle=!shuffle;
  mc1State.set('player','shuffle',shuffle);
  document.getElementById('mp-btn-shuffle').classList.toggle('active',shuffle);
  mc1Toast(shuffle?'Shuffle on':'Shuffle off','ok');
};

window.playerCycleRepeat = function(){
  repeatMode=(repeatMode+1)%3;
  mc1State.set('player','repeat',repeatMode);
  var btn=document.getElementById('mp-btn-repeat');
  var icon=btn.querySelector('i');
  if(repeatMode===0){ btn.classList.remove('active'); btn.title='Repeat: off'; icon.className='fa-solid fa-repeat'; mc1Toast('Repeat off','ok'); }
  else if(repeatMode===1){ btn.classList.add('active'); btn.title='Repeat: one'; icon.className='fa-solid fa-repeat-1'; mc1Toast('Repeat one','ok'); }
  else { btn.classList.add('active'); btn.title='Repeat: all'; icon.className='fa-solid fa-repeat'; mc1Toast('Repeat all','ok'); }
};

/* ── Queue management ────────────────────────────────────────────────── */
function loadQueue(){
  mc1Api('POST','/app/api/player.php',{action:'queue_list'}).then(function(d){
    if (d.ok){
      queue=d.queue||[];
      if (currentIdx<0){
        var sid=mc1State.get('player','queueId',null);
        if (sid!==null){
          var idx=queue.findIndex(function(i){ return i.queue_id===sid; });
          if (idx>=0){ currentIdx=idx; updateNowPlaying(queue[idx]); }
        }
      }
      renderQueue();
    }
  }).catch(function(){});
}

window.playerClearQueue = function(){
  if (!queue.length) return;
  if (!confirm('Clear the entire playback queue?')) return;
  mc1Api('POST','/app/api/player.php',{action:'queue_clear'}).then(function(d){
    if (d.ok){
      queue=[]; currentIdx=-1; mc1State.del('player','queueId');
      audio.pause(); audio.src=''; seekbar.value=0;
      document.getElementById('mp-pos').textContent='0:00';
      document.getElementById('mp-dur').textContent='0:00';
      updateNowPlaying(null); renderQueue(); mc1Toast('Queue cleared','ok');
    }
  }).catch(function(){ mc1Toast('Clear failed','err'); });
};

window.playerRemoveFromQueue = function(queue_id){
  mc1Api('POST','/app/api/player.php',{action:'queue_remove',queue_id:queue_id}).then(function(d){
    if (d.ok){
      var ri=queue.findIndex(function(i){ return i.queue_id===queue_id; });
      queue=d.queue||[];
      if (ri>=0&&ri<currentIdx) currentIdx--;
      else if (ri===currentIdx) currentIdx=-1;
      renderQueue();
    }
  }).catch(function(){ mc1Toast('Remove failed','err'); });
};

/* ── Queue rendering ─────────────────────────────────────────────────── */
function renderQueue(){
  var list=document.getElementById('mp-queue-list');
  var cnt =document.getElementById('mp-q-count');
  cnt.textContent=queue.length;

  if (!queue.length){
    list.innerHTML='<div class="pro-empty"><i class="fa-solid fa-headphones"></i><div>Queue is empty</div><div style="font-size:10px;margin-top:4px">Double-click a track to play</div></div>';
    return;
  }

  var html='';
  for (var i=0;i<queue.length;i++){
    var item=queue[i]; var isCur=(i===currentIdx);
    var art=item.art_url
      ?'<img class="pro-q-art" src="'+_e(item.art_url)+'" onerror="this.outerHTML=\'<div class=pro-q-artph><i class=\\\'fa-solid fa-music\\\'></i></div>\'">'
      :'<div class="pro-q-artph"><i class="fa-solid fa-music"></i></div>';
    var dur=item.duration_ms>0?fmtTime(item.duration_ms/1000):'';
    html+='<div class="pro-q-item'+(isCur?' current':'')+'" onclick="playerJumpTo('+i+')" oncontextmenu="qCtxShow(event,'+i+')" data-qid="'+item.queue_id+'">'
         +'<span class="pro-q-pos">'+(isCur?'<i class="fa-solid fa-volume-high" style="color:var(--teal);font-size:8px"></i>':(i+1))+'</span>'
         +art
         +'<div class="pro-q-info">'
         +'<div class="pro-q-title">'+_e(item.title||'—')+'</div>'
         +'<div class="pro-q-artist">'+_e(item.artist||'')+'</div>'
         +'</div>'
         +(dur?'<span class="pro-q-dur">'+dur+'</span>':'')
         +'<button class="pro-q-rm" onclick="event.stopPropagation();playerRemoveFromQueue('+item.queue_id+')" title="Remove"><i class="fa-solid fa-xmark"></i></button>'
         +'</div>';
  }
  list.innerHTML=html;

  if (currentIdx>=0){
    var items=list.querySelectorAll('.pro-q-item');
    if (items[currentIdx]) items[currentIdx].scrollIntoView({block:'nearest',behavior:'smooth'});
  }
}

/* ── Now Playing update (transport + mini panel) ─────────────────────── */
function updateNowPlaying(item){
  var tEl = document.getElementById('mp-np-title');
  var aEl = document.getElementById('mp-np-artist');
  var aw  = document.getElementById('mp-np-art-wrap');

  if (!item){
    tEl.textContent='No track loaded'; aEl.textContent='';
    aw.innerHTML='<div class="pro-np-artph"><i class="fa-solid fa-music"></i></div>';
    document.title='Mcaster1 Player';
    return;
  }

  tEl.textContent = item.title  || '—';
  aEl.textContent = item.artist || '';
  document.title  = (item.artist?item.artist+' — ':'')+(item.title||'?')+' · Mcaster1';

  if (item.art_url){
    aw.innerHTML='<img class="pro-np-art" src="'+_e(item.art_url)+'" onerror="this.outerHTML=\'<div class=pro-np-artph><i class=\\\'fa-solid fa-music\\\'></i></div>\'">';
  } else {
    aw.innerHTML='<div class="pro-np-artph"><i class="fa-solid fa-music"></i></div>';
  }
}

/* ── Keyboard shortcuts ──────────────────────────────────────────────── */
document.addEventListener('keydown', function(e){
  if (e.target.tagName==='INPUT'||e.target.tagName==='TEXTAREA') return;
  if (e.code==='Space')      { e.preventDefault(); playerPlayPause(); }
  if (e.code==='ArrowRight') { if(audio.duration) audio.currentTime=Math.min(audio.duration,audio.currentTime+5); }
  if (e.code==='ArrowLeft')  { audio.currentTime=Math.max(0,audio.currentTime-5); }
  if (e.code==='ArrowUp')    { e.preventDefault(); audio.volume=Math.min(1,audio.volume+0.05); volbar.value=Math.round(audio.volume*100); document.getElementById('mp-vol-pct').textContent=volbar.value+'%'; }
  if (e.code==='ArrowDown')  { e.preventDefault(); audio.volume=Math.max(0,audio.volume-0.05); volbar.value=Math.round(audio.volume*100); document.getElementById('mp-vol-pct').textContent=volbar.value+'%'; }
  if (e.code==='KeyN') playerNext();
  if (e.code==='KeyP') playerPrev();
});

/* ── Utility ─────────────────────────────────────────────────────────── */
function fmtTime(sec){
  sec=Math.floor(sec||0);
  var m=Math.floor(sec/60), s=sec%60;
  return m+':'+(s<10?'0':'')+s;
}
function _e(s){
  return String(s||'').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

/* ════════════════════════════════════════════════════════════════════════
 * CONTEXT MENU ACTIONS
 * ════════════════════════════════════════════════════════════════════════ */
function _closeCtx() { var m = document.getElementById('pro-ctx-menu'); if (m) m.style.display='none'; }

window.ctxEditTags = function() {
  _closeCtx();
  if (!ctxTrack) return;
  openEditModal(ctxTrack.id);
};
window.ctxFetchArt = function() {
  _closeCtx();
  if (!ctxTrack) return;
  doFetchArt(ctxTrack.id, ctxTrack);
};
window.ctxAddToPlaylist = function() {
  _closeCtx();
  /* We clear any bulk ID from a previous multi-select add so single-track path runs */
  var modal = document.getElementById('modal-addpl');
  if (modal) delete modal.dataset.bulkIds;
  showModal('modal-addpl');
};
window.ctxNewPlaylistAdd = function() { _closeCtx(); showModal('modal-newpl'); };
window.ctxAddToCategory = function() {
  _closeCtx();
  var sel = document.getElementById('addcat-select');
  sel.innerHTML = '';
  if (proCategories && proCategories.length) {
    proCategories.forEach(function(c) {
      var o = document.createElement('option');
      o.value = c.id;
      o.textContent = c.name;
      sel.appendChild(o);
    });
  } else {
    sel.innerHTML = '<option value="">— No categories yet —</option>';
  }
  showModal('modal-addcat');
};
window.ctxDelete = function() {
  _closeCtx();
  if (!ctxTrack) return;
  var name = (ctxTrack.artist || '?') + ' — ' + (ctxTrack.title || '?');
  document.getElementById('del-track-name').textContent = name;
  document.getElementById('del-also-file').checked = false;
  showModal('modal-del');
};
window.ctxLookupMusicBrainz = function() {
  _closeCtx();
  if (!ctxTrack) return;
  var q = encodeURIComponent((ctxTrack.artist || '') + ' ' + (ctxTrack.title || ''));
  window.open('https://musicbrainz.org/search?query=' + q + '&type=recording', '_blank');
};
window.ctxLookupLastFm = function() {
  _closeCtx();
  if (!ctxTrack) return;
  var a = encodeURIComponent(ctxTrack.artist || '');
  var t = encodeURIComponent(ctxTrack.title  || '');
  window.open('https://www.last.fm/music/' + a + '/_/' + t, '_blank');
};
window.ctxLookupDiscogs = function() {
  _closeCtx();
  if (!ctxTrack) return;
  var q = encodeURIComponent((ctxTrack.artist || '') + ' ' + (ctxTrack.title || ''));
  window.open('https://www.discogs.com/search/?q=' + q + '&type=all', '_blank');
};

/* ════════════════════════════════════════════════════════════════════════
 * MODAL ACTIONS
 * ════════════════════════════════════════════════════════════════════════ */
function openEditModal(id) {
  mc1Api('POST','/app/api/tracks.php',{action:'get',id:id}).then(function(d){
    if (!d.ok) { mc1Toast(d.error || 'Failed to load track', 'err'); return; }
    var t = d.track;
    document.getElementById('edit-track-id').value  = t.id;
    document.getElementById('edit-title').value     = t.title   || '';
    document.getElementById('edit-artist').value    = t.artist  || '';
    document.getElementById('edit-album').value     = t.album   || '';
    document.getElementById('edit-genre').value     = t.genre   || '';
    document.getElementById('edit-year').value      = t.year    || '';
    document.getElementById('edit-bpm').value       = t.bpm     || '';
    document.getElementById('edit-energy').value    = t.energy_level !== null ? t.energy_level : '';
    document.getElementById('edit-mood').value      = t.mood_tag     || '';
    document.getElementById('edit-key').value       = t.musical_key  || '';
    document.getElementById('edit-tracknum').value  = t.track_number || '';
    document.getElementById('edit-intro').value     = t.intro_ms  || '';
    document.getElementById('edit-outro').value     = t.outro_ms  || '';
    document.getElementById('edit-cuein').value     = t.cue_in_ms || '';
    document.getElementById('edit-cueout').value    = t.cue_out_ms|| '';
    document.getElementById('edit-jingle').checked  = !!t.is_jingle;
    document.getElementById('edit-sweeper').checked = !!t.is_sweeper;
    document.getElementById('edit-spot').checked    = !!t.is_spot;
    document.getElementById('edit-filepath').textContent = t.file_path || '';
    document.getElementById('edit-write-tags').checked   = false;
    setStarRating(t.rating || 3);
    showModal('modal-edit');
  }).catch(function(){ mc1Toast('Request failed', 'err'); });
}

window.saveEditTags = function() {
  var id = parseInt(document.getElementById('edit-track-id').value);
  if (!id) return;
  var btn = document.querySelector('#modal-edit .btn-primary');
  btn.disabled = true;
  mc1Api('POST','/app/api/tracks.php',{
    action:     'update', id: id,
    title:      document.getElementById('edit-title').value,
    artist:     document.getElementById('edit-artist').value,
    album:      document.getElementById('edit-album').value,
    genre:      document.getElementById('edit-genre').value,
    year:       document.getElementById('edit-year').value,
    bpm:        document.getElementById('edit-bpm').value,
    energy_level: document.getElementById('edit-energy').value,
    mood_tag:   document.getElementById('edit-mood').value,
    musical_key:  document.getElementById('edit-key').value,
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
  }).then(function(d){
    btn.disabled = false;
    if (d.ok) {
      var msg = 'Tags saved';
      if (d.wrote_tags) msg += ' + written to file';
      else if (d.tag_error) msg += ' (file write: ' + d.tag_error + ')';
      mc1Toast(msg, 'ok');
      hideModal('modal-edit');
      ptLoadPage(ptPage); /* We refresh the track list to reflect changes */
    } else { mc1Toast(d.error || 'Save failed', 'err'); }
  }).catch(function(){ btn.disabled = false; mc1Toast('Request failed', 'err'); });
};

function doFetchArt(id, t) {
  mc1Toast('Fetching album art from MusicBrainz…', 'ok');
  mc1Api('POST','/app/api/tracks.php',{
    action: 'fetch_art', track_id: id,
    artist: t.artist || '', title: t.title || '', album: t.album || '', embed_in_file: false,
  }).then(function(d){
    if (d.ok) { mc1Toast('Album art saved!', 'ok'); }
    else { mc1Toast(d.error || 'Art not found on MusicBrainz', 'warn'); }
  }).catch(function(){ mc1Toast('Art fetch failed', 'err'); });
}

window.doAddToPlaylist = function() {
  var plId = parseInt(document.getElementById('addpl-select').value);
  var wt   = parseFloat(document.getElementById('addpl-weight').value);
  var modal = document.getElementById('modal-addpl');
  if (!plId) { mc1Toast('Select a playlist', 'warn'); return; }
  /* We support bulk-add from selection bar (bulkIds stored on modal.dataset) */
  var bulkRaw = modal.dataset.bulkIds;
  if (bulkRaw) {
    delete modal.dataset.bulkIds;
    var ids = JSON.parse(bulkRaw);
    var chain = Promise.resolve(); var added = 0;
    ids.forEach(function(id) {
      chain = chain.then(function() {
        return mc1Api('POST','/app/api/tracks.php',{action:'add_to_playlist',track_id:id,playlist_id:plId,weight:wt}).then(function(d){ if(d.ok) added++; });
      });
    });
    chain.then(function(){
      mc1Toast('Added ' + added + ' track' + (added !== 1 ? 's' : '') + ' to playlist', 'ok');
      hideModal('modal-addpl');
      clearProSelection();
    });
    return;
  }
  if (!ctxTrack) { mc1Toast('No track selected', 'warn'); return; }
  mc1Api('POST','/app/api/tracks.php',{action:'add_to_playlist',track_id:ctxTrack.id,playlist_id:plId,weight:wt}).then(function(d){
    if (d.ok) { mc1Toast('Added to playlist (weight ' + wt.toFixed(1) + ')', 'ok'); hideModal('modal-addpl'); }
    else { mc1Toast(d.error || 'Failed', 'err'); }
  }).catch(function(){ mc1Toast('Request failed', 'err'); });
};

window.doNewPlaylistAdd = function() {
  var name = document.getElementById('newpl-name').value.trim();
  var type = document.getElementById('newpl-type').value;
  var desc = document.getElementById('newpl-desc').value.trim();
  var wt   = parseFloat(document.getElementById('newpl-weight').value);
  if (!name) { mc1Toast('Enter a playlist name', 'warn'); return; }
  if (!ctxTrack) { mc1Toast('No track selected', 'warn'); return; }
  mc1Api('POST','/app/api/tracks.php',{
    action: 'create_playlist_add', track_id: ctxTrack.id,
    name: name, type: type, description: desc, weight: wt,
  }).then(function(d){
    if (d.ok) { mc1Toast('Playlist "' + name + '" created and track added', 'ok'); hideModal('modal-newpl'); document.getElementById('newpl-name').value = ''; }
    else { mc1Toast(d.error || 'Failed', 'err'); }
  }).catch(function(){ mc1Toast('Request failed', 'err'); });
};

window.doDeleteTrack = function() {
  var also = document.getElementById('del-also-file').checked;
  if (!ctxTrack) return;
  mc1Api('POST','/app/api/tracks.php',{action:'delete',id:ctxTrack.id,delete_file:also}).then(function(d){
    if (d.ok) {
      mc1Toast('Track deleted' + (d.file_deleted ? ' + file removed from disk' : ''), 'ok');
      hideModal('modal-del');
      clearProSelection();
      ptLoadPage(ptPage);
    } else { mc1Toast(d.error || 'Delete failed', 'err'); }
  }).catch(function(){ mc1Toast('Request failed', 'err'); });
};

window.doAddToCategory = function() {
  var catId = parseInt(document.getElementById('addcat-select').value);
  if (!catId || !ctxTrack) { mc1Toast('Select a category', 'warn'); return; }
  mc1Api('POST','/app/api/tracks.php',{action:'add_to_category',track_id:ctxTrack.id,category_id:catId}).then(function(d){
    if (d.ok) { mc1Toast('Added to category', 'ok'); hideModal('modal-addcat'); }
    else { mc1Toast(d.error || 'Failed', 'err'); }
  }).catch(function(){ mc1Toast('Request failed', 'err'); });
};

/* ── Star rating ─────────────────────────────────────────────────────── */
function setStarRating(v) {
  document.getElementById('edit-rating').value = v;
  var stars = document.getElementById('edit-stars');
  if (stars) stars.querySelectorAll('.s').forEach(function(x){
    x.classList.toggle('on', parseInt(x.getAttribute('data-v')) <= v);
  });
}

/* ════════════════════════════════════════════════════════════════════════
 * QUEUE CONTEXT MENU
 * ════════════════════════════════════════════════════════════════════════ */
var qCtxIdx = -1;
var qCtxQueueId = null;

window.qCtxShow = function(e, idx) {
  e.preventDefault();
  e.stopPropagation();
  /* We close the track browser context menu if open */
  var tm = document.getElementById('pro-ctx-menu');
  if (tm) tm.style.display = 'none';
  qCtxIdx     = idx;
  qCtxQueueId = (queue[idx] && queue[idx].queue_id != null) ? queue[idx].queue_id : null;
  var menu = document.getElementById('pro-q-ctx-menu');
  menu.style.display = 'block';
  var vw = window.innerWidth, vh = window.innerHeight;
  var mw = menu.offsetWidth || 160, mh = menu.offsetHeight || 80;
  menu.style.left = Math.min(e.clientX, vw-mw-6) + 'px';
  menu.style.top  = Math.min(e.clientY, vh-mh-6) + 'px';
};

window.qCtxPlayNow = function() {
  var m = document.getElementById('pro-q-ctx-menu');
  if (m) m.style.display = 'none';
  if (qCtxIdx >= 0) playerJumpTo(qCtxIdx);
};

window.qCtxMoveToTop = function() {
  var m = document.getElementById('pro-q-ctx-menu');
  if (m) m.style.display = 'none';
  if (qCtxQueueId === null || qCtxIdx <= 0) return;
  /* We re-add the track at position 0 by removing and re-inserting */
  var item = queue[qCtxIdx];
  if (!item) return;
  mc1Api('POST','/app/api/player.php',{action:'queue_remove',queue_id:qCtxQueueId}).then(function(d){
    if (!d.ok) { mc1Toast(d.error||'Move failed','err'); return; }
    return mc1Api('POST','/app/api/player.php',{action:'queue_add',track_id:item.track_id,position:0});
  }).then(function(d){
    if (d && d.ok) {
      queue = d.queue || [];
      if (currentIdx > 0) currentIdx++;
      renderQueue();
      mc1Toast('Moved to top','ok');
    }
  }).catch(function(){ mc1Toast('Request failed','err'); });
};

window.qCtxRemove = function() {
  var m = document.getElementById('pro-q-ctx-menu');
  if (m) m.style.display = 'none';
  if (qCtxQueueId === null) return;
  playerRemoveFromQueue(qCtxQueueId);
};

/* ════════════════════════════════════════════════════════════════════════
 * RESIZABLE PANES (drag handles between library | tracks | queue)
 * ════════════════════════════════════════════════════════════════════════ */
function initPaneResize() {
  var root = document.documentElement;
  document.querySelectorAll('.pane-divider').forEach(function(handle) {
    var side = handle.dataset.side;
    handle.addEventListener('mousedown', function(e) {
      e.preventDefault();
      var startX = e.clientX;
      var libEl  = document.querySelector('.pro-lib');
      var qEl    = document.querySelector('.pro-queue');
      var startW = side === 'left' ? libEl.offsetWidth : qEl.offsetWidth;
      handle.classList.add('dragging');
      document.body.style.cursor    = 'col-resize';
      document.body.style.userSelect = 'none';

      function onMove(me) {
        var delta = me.clientX - startX;
        if (side === 'left') {
          var w = Math.max(140, Math.min(420, startW + delta));
          root.style.setProperty('--pro-lib-w', w + 'px');
        } else {
          var w = Math.max(160, Math.min(460, startW - delta));
          root.style.setProperty('--pro-q-w', w + 'px');
        }
      }
      function onUp() {
        handle.classList.remove('dragging');
        document.body.style.cursor    = '';
        document.body.style.userSelect = '';
        document.removeEventListener('mousemove', onMove);
        document.removeEventListener('mouseup',   onUp);
        /* We persist pane widths in localStorage */
        mc1State.set('player','libW', parseInt(root.style.getPropertyValue('--pro-lib-w')));
        mc1State.set('player','qW',   parseInt(root.style.getPropertyValue('--pro-q-w')));
      }
      document.addEventListener('mousemove', onMove);
      document.addEventListener('mouseup',   onUp);
    });
  });
}

/* ── Poll queue from DB so tracks added via main window appear here ──── */
function pollQueueFromMain(){
  mc1Api('POST','/app/api/player.php',{action:'queue_list'}).then(function(d){
    if (!d.ok) return;
    var nq=d.queue||[];
    var changed = nq.length!==queue.length;
    if (!changed&&nq.length>0) changed=(nq[nq.length-1].queue_id!==queue[queue.length-1].queue_id);
    if (changed){ queue=nq; renderQueue(); }
  }).catch(function(){});
}

/* ── DOMContentLoaded ─────────────────────────────────────────────────── */
document.addEventListener('DOMContentLoaded', function(){
  /* Star rating wiring for Edit Tags modal */
  var editStars = document.getElementById('edit-stars');
  if (editStars) {
    editStars.querySelectorAll('.s').forEach(function(s) {
      s.addEventListener('mouseover', function() {
        var v = parseInt(s.getAttribute('data-v'));
        editStars.querySelectorAll('.s').forEach(function(x){
          x.classList.toggle('hover', parseInt(x.getAttribute('data-v')) <= v);
        });
      });
      s.addEventListener('mouseout', function() {
        editStars.querySelectorAll('.s').forEach(function(x){ x.classList.remove('hover'); });
      });
      s.addEventListener('click', function() {
        setStarRating(parseInt(s.getAttribute('data-v')));
      });
    });
  }

  /* Restore volume */
  var sv=mc1State.get('player','volume',80);
  audio.volume=sv/100; volbar.value=sv;
  document.getElementById('mp-vol-pct').textContent=sv+'%';

  /* Restore shuffle + repeat */
  shuffle=mc1State.get('player','shuffle',false);
  document.getElementById('mp-btn-shuffle').classList.toggle('active',shuffle);
  repeatMode=mc1State.get('player','repeat',0);
  var rBtn=document.getElementById('mp-btn-repeat');
  if (rBtn&&repeatMode>0){
    rBtn.classList.add('active');
    var ri=rBtn.querySelector('i');
    if (ri&&repeatMode===1){ rBtn.title='Repeat: one'; ri.className='fa-solid fa-repeat-1'; }
    else if (ri){ rBtn.title='Repeat: all'; }
  }

  /* Restore saved pane widths */
  var savedLibW = mc1State.get('player','libW', 0);
  var savedQW   = mc1State.get('player','qW', 0);
  var root = document.documentElement;
  if (savedLibW > 0) root.style.setProperty('--pro-lib-w', savedLibW + 'px');
  if (savedQW   > 0) root.style.setProperty('--pro-q-w',   savedQW   + 'px');

  /* Initialize resizable pane drag handles */
  initPaneResize();

  /* Load queue and auto-resume position */
  loadQueue();

  /* Poll for new queue entries every 5 seconds */
  setInterval(pollQueueFromMain, 5000);
});

})(); /* end IIFE */
</script>

</body>
</html>
