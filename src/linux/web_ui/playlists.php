<?php
/**
 * playlists.php — Playlist management with AI-powered generation wizard.
 *
 * We display the full playlist library in a sidebar, let users browse tracks,
 * remove tracks, delete playlists, load to encoder slots, and launch the
 * multi-step generation wizard to auto-build playlists from the media library.
 *
 * @author  Dave St. John <davestj@gmail.com>
 * @version 1.4.0
 * @since   2026-02-23
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/app/inc/auth.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/mc1_config.php';
require_once __DIR__ . '/app/inc/user_auth.php';

$page_title = 'Playlists';
$active_nav = 'playlists';
$use_charts = false;

$playlists = [];
try {
    $playlists = mc1_db('mcaster1_media')
        ->query('SELECT id, name, type, description, track_count, created_at FROM playlists ORDER BY name')
        ->fetchAll();
} catch (Exception $e) {}

require_once __DIR__ . '/app/inc/header.php';
?>
<style>
/* We use these styles for the playlist wizard modal and track panel */
.wizard-steps  { display:flex; gap:0; border-bottom:1px solid var(--border); margin:-1px 0 20px; }
.wstep         { flex:1; text-align:center; padding:10px 4px 9px; font-size:11px; font-weight:600;
                 color:var(--muted); cursor:default; border-bottom:2px solid transparent;
                 text-transform:uppercase; letter-spacing:.04em; white-space:nowrap; }
.wstep.active  { color:var(--teal); border-bottom-color:var(--teal); }
.wstep.done    { color:var(--text); }
.wstep-num     { display:inline-block; width:18px; height:18px; line-height:18px; border-radius:50%;
                 background:var(--bg3); font-size:10px; margin-right:4px; }
.wstep.active .wstep-num { background:var(--teal); color:#0a0f1e; }
.wstep.done .wstep-num   { background:var(--teal-glow); color:var(--teal); }
.wizard-body   { min-height:240px; }
.wizard-page   { display:none; }
.wizard-page.active { display:block; }
.algo-card     { border:1px solid var(--border); border-radius:6px; padding:10px 12px;
                 margin-bottom:6px; cursor:pointer; transition:border-color .12s, background .12s; }
.algo-card:hover   { border-color:var(--teal); background:var(--teal-glow); }
.algo-card.selected{ border-color:var(--teal); background:var(--teal-glow); }
.algo-card input   { margin-right:8px; accent-color:var(--teal); }
.algo-title    { font-size:13px; font-weight:600; color:var(--text); }
.algo-desc     { font-size:11px; color:var(--muted); margin-top:2px; }
.cat-chip      { display:inline-flex; align-items:center; gap:5px; padding:3px 10px;
                 border-radius:12px; border:1px solid var(--border); font-size:11px;
                 cursor:pointer; margin:2px; transition:background .12s, border-color .12s; }
.cat-chip.sel  { border-color:var(--teal); background:var(--teal-glow); color:var(--teal); }
.star-rate     { display:flex; gap:2px; cursor:pointer; font-size:18px; line-height:1; }
.star-rate .s  { color:var(--muted); transition:color .1s; }
.star-rate .s.on { color:#f59e0b; }
.preview-track { display:grid; grid-template-columns:28px 1fr 120px 60px 40px;
                 gap:8px; align-items:center; padding:6px 8px; border-radius:4px;
                 font-size:12px; }
.preview-track:nth-child(odd) { background:rgba(255,255,255,.03); }
.preview-track .pnum { color:var(--muted); text-align:right; font-size:11px; }
.preview-artist { color:var(--muted); }
.jingle-badge  { font-size:9px; background:rgba(139,92,246,.25); color:#a78bfa;
                 border-radius:3px; padding:1px 4px; margin-left:4px; }
.pl-type-badge { font-size:9px; padding:2px 6px; border-radius:10px; font-weight:700;
                 text-transform:uppercase; letter-spacing:.04em; }
.pl-type-static { background:rgba(100,116,139,.2); color:#94a3b8; }
.pl-type-smart  { background:rgba(20,184,166,.15); color:var(--teal); }
.pl-type-ai     { background:rgba(139,92,246,.2); color:#a78bfa; }
.pl-type-clock  { background:rgba(245,158,11,.15); color:#f59e0b; }
</style>

<div class="sec-hdr">
  <div class="sec-title"><i class="fa-solid fa-list-ul" style="color:var(--teal);margin-right:8px"></i>Playlists</div>
  <div style="display:flex;gap:8px">
    <button class="btn btn-secondary btn-sm" onclick="showCreate()"><i class="fa-solid fa-plus"></i> New Playlist</button>
    <button class="btn btn-primary btn-sm" onclick="genWizardOpen()"><i class="fa-solid fa-bolt"></i> Generate Playlist</button>
  </div>
</div>

<!-- ── Quick-create modal ──────────────────────────────────────────────── -->
<div id="create-modal" style="display:none;position:fixed;inset:0;background:rgba(0,0,0,.65);z-index:500;align-items:center;justify-content:center">
  <div class="card" style="width:420px;max-width:92vw;box-shadow:0 8px 40px rgba(0,0,0,.6)">
    <div class="card-hdr">
      <div class="card-title"><i class="fa-solid fa-plus fa-fw"></i> New Playlist</div>
      <button class="btn-icon" onclick="hideCreate()"><i class="fa-solid fa-xmark"></i></button>
    </div>
    <div class="form-group"><label class="form-label">Name</label><input class="form-input" id="np-name" placeholder="My Playlist"></div>
    <div class="form-group"><label class="form-label">Type</label>
      <select class="form-select" id="np-type">
        <option value="static">Static</option>
        <option value="smart">Smart</option>
        <option value="ai">AI</option>
        <option value="clock">Clock</option>
      </select>
    </div>
    <div class="form-group"><label class="form-label">Description</label><textarea class="form-textarea" id="np-desc" rows="2"></textarea></div>
    <div style="display:flex;gap:8px;justify-content:flex-end;margin-top:8px">
      <button class="btn btn-secondary" onclick="hideCreate()">Cancel</button>
      <button class="btn btn-primary" onclick="createPlaylist()"><i class="fa-solid fa-check"></i> Create</button>
    </div>
  </div>
</div>

<!-- ── Generator Wizard modal ──────────────────────────────────────────── -->
<div id="gen-modal" style="display:none;position:fixed;inset:0;background:rgba(0,0,0,.7);z-index:600;align-items:flex-start;justify-content:center;padding:40px 16px;overflow-y:auto">
  <div class="card" style="width:640px;max-width:96vw;box-shadow:0 12px 50px rgba(0,0,0,.7)">
    <div class="card-hdr">
      <div class="card-title"><i class="fa-solid fa-bolt" style="color:var(--teal)"></i> Generate Playlist</div>
      <button class="btn-icon" onclick="genWizardClose()"><i class="fa-solid fa-xmark"></i></button>
    </div>

    <!-- Step indicator -->
    <div class="wizard-steps">
      <div class="wstep active" id="wstep-1"><span class="wstep-num">1</span>Basic Info</div>
      <div class="wstep" id="wstep-2"><span class="wstep-num">2</span>Track Source</div>
      <div class="wstep" id="wstep-3"><span class="wstep-num">3</span>Rules</div>
      <div class="wstep" id="wstep-4"><span class="wstep-num">4</span>Preview</div>
    </div>

    <div class="wizard-body">

      <!-- Step 1: Basic Info -->
      <div id="wpage-1" class="wizard-page active">
        <div class="form-group">
          <label class="form-label">Playlist Name <span style="color:var(--red)">*</span></label>
          <input class="form-input" id="gn-name" placeholder="Morning Drive Rock">
        </div>
        <div class="form-group">
          <label class="form-label">Description</label>
          <input class="form-input" id="gn-desc" placeholder="Auto-generated rotation for AM drive">
        </div>
        <div class="form-group">
          <label class="form-label">Algorithm</label>
          <div id="algo-list" style="max-height:260px;overflow-y:auto;padding-right:4px">
            <div class="empty"><div class="spinner"></div></div>
          </div>
        </div>
        <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px">
          <div class="form-group">
            <label class="form-label">Target Track Count</label>
            <input class="form-input" id="gn-count" type="number" min="1" max="5000" value="50" placeholder="50">
          </div>
          <div class="form-group">
            <label class="form-label">— or — Duration (hours)</label>
            <input class="form-input" id="gn-dur" type="number" min="0" max="240" step="0.5" value="" placeholder="e.g. 2.5">
          </div>
        </div>
        <div class="form-group">
          <label class="form-label" style="display:flex;align-items:center;gap:8px">
            <input type="checkbox" id="gn-m3u" checked style="accent-color:var(--teal)">
            Also write M3U file to <code style="font-size:11px"><?= h(MC1_PLAYLIST_DIR) ?>/</code>
          </label>
        </div>
      </div>

      <!-- Step 2: Track Source -->
      <div id="wpage-2" class="wizard-page">
        <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px">
          <div class="form-group">
            <label class="form-label">Genre Filter</label>
            <input class="form-input" id="gf-genre" placeholder="Rock (partial match)">
          </div>
          <div class="form-group">
            <label class="form-label">Min Rating</label>
            <div class="star-rate" id="gf-rating" data-val="1">
              <span class="s on" data-v="1">★</span>
              <span class="s" data-v="2">★</span>
              <span class="s" data-v="3">★</span>
              <span class="s" data-v="4">★</span>
              <span class="s" data-v="5">★</span>
            </div>
            <div style="font-size:10px;color:var(--muted);margin-top:3px">Any rating (1+)</div>
          </div>
        </div>
        <div class="form-group">
          <label class="form-label">Categories (leave blank for all)</label>
          <div id="gf-cats" style="display:flex;flex-wrap:wrap;gap:4px;margin-top:4px">
            <div class="empty" style="padding:8px"><div class="spinner"></div></div>
          </div>
        </div>
        <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px">
          <div class="form-group">
            <label class="form-label">Year From</label>
            <input class="form-input" id="gf-yr-from" type="number" min="1900" max="2099" placeholder="e.g. 1970">
          </div>
          <div class="form-group">
            <label class="form-label">Year To</label>
            <input class="form-input" id="gf-yr-to" type="number" min="1900" max="2099" placeholder="e.g. 1999">
          </div>
        </div>
        <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px">
          <div class="form-group">
            <label class="form-label">BPM Min</label>
            <input class="form-input" id="gf-bpm-min" type="number" min="40" max="300" placeholder="e.g. 80">
          </div>
          <div class="form-group">
            <label class="form-label">BPM Max</label>
            <input class="form-input" id="gf-bpm-max" type="number" min="40" max="300" placeholder="e.g. 160">
          </div>
        </div>
        <div class="form-group">
          <label class="form-label">Include special types</label>
          <div style="display:flex;gap:16px;margin-top:6px">
            <label style="display:flex;align-items:center;gap:6px;font-size:13px;cursor:pointer">
              <input type="checkbox" id="gf-inc-jingle" style="accent-color:var(--teal)"> Jingles
            </label>
            <label style="display:flex;align-items:center;gap:6px;font-size:13px;cursor:pointer">
              <input type="checkbox" id="gf-inc-sweeper" style="accent-color:var(--teal)"> Sweepers
            </label>
            <label style="display:flex;align-items:center;gap:6px;font-size:13px;cursor:pointer">
              <input type="checkbox" id="gf-inc-spot" style="accent-color:var(--teal)"> Spots
            </label>
          </div>
        </div>
        <div id="pool-hint" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
      </div>

      <!-- Step 3: Rules -->
      <div id="wpage-3" class="wizard-page">
        <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px">
          <div class="form-group">
            <label class="form-label">Artist Separation</label>
            <div style="display:flex;align-items:center;gap:8px">
              <input class="form-input" id="gr-artist-sep" type="number" min="0" max="20" value="3" style="width:80px">
              <span style="font-size:12px;color:var(--muted)">tracks between same artist</span>
            </div>
          </div>
          <div class="form-group">
            <label class="form-label">Song Repeat Window</label>
            <div style="display:flex;align-items:center;gap:8px">
              <input class="form-input" id="gr-song-hrs" type="number" min="0" max="168" value="4" style="width:80px">
              <span style="font-size:12px;color:var(--muted)">hours before repeat</span>
            </div>
          </div>
        </div>
        <div class="form-group">
          <label class="form-label">Jingle Interleave</label>
          <div style="display:flex;align-items:center;gap:8px">
            <input class="form-input" id="gr-jingle-n" type="number" min="0" max="30" value="0" style="width:80px">
            <span style="font-size:12px;color:var(--muted)">songs between each jingle (0 = disabled)</span>
          </div>
          <div style="font-size:11px;color:var(--muted);margin-top:4px">We pull jingles from tracks marked is_jingle=1 in the media library.</div>
        </div>

        <!-- Energy direction — only relevant for energy_flow algo, but we show it always -->
        <div class="form-group" id="gr-energy-wrap">
          <label class="form-label">Energy Flow Direction</label>
          <div style="display:flex;gap:16px;margin-top:6px">
            <label style="display:flex;align-items:center;gap:6px;font-size:13px;cursor:pointer">
              <input type="radio" name="energy-dir" value="wave" checked style="accent-color:var(--teal)"> Wave (arc)
            </label>
            <label style="display:flex;align-items:center;gap:6px;font-size:13px;cursor:pointer">
              <input type="radio" name="energy-dir" value="ascending" style="accent-color:var(--teal)"> Ascending
            </label>
            <label style="display:flex;align-items:center;gap:6px;font-size:13px;cursor:pointer">
              <input type="radio" name="energy-dir" value="descending" style="accent-color:var(--teal)"> Descending
            </label>
          </div>
        </div>

        <!-- Saved rotation rule loader -->
        <div class="form-group">
          <label class="form-label">Load Saved Rotation Rule</label>
          <div style="display:flex;gap:8px">
            <select class="form-select" id="gr-rule-sel" style="flex:1">
              <option value="">— manual settings above —</option>
            </select>
            <button class="btn btn-secondary btn-sm" onclick="loadRotationRule()">Apply</button>
          </div>
        </div>
      </div>

      <!-- Step 4: Preview -->
      <div id="wpage-4" class="wizard-page">
        <div id="preview-summary" style="display:flex;gap:20px;margin-bottom:12px;flex-wrap:wrap"></div>
        <div id="preview-body">
          <div class="empty"><div class="spinner"></div><p style="margin-top:8px">Loading preview…</p></div>
        </div>
      </div>

    </div><!-- /wizard-body -->

    <!-- Wizard footer -->
    <div style="display:flex;align-items:center;justify-content:space-between;margin-top:16px;padding-top:12px;border-top:1px solid var(--border)">
      <button class="btn btn-secondary" id="wiz-back" onclick="genWizardPrev()" style="visibility:hidden"><i class="fa-solid fa-chevron-left"></i> Back</button>
      <div id="wiz-status" style="font-size:11px;color:var(--muted)"></div>
      <button class="btn btn-primary" id="wiz-next" onclick="genWizardNext()">Next <i class="fa-solid fa-chevron-right"></i></button>
    </div>
  </div>
</div>

<!-- ── Page layout ─────────────────────────────────────────────────────── -->
<div style="display:grid;grid-template-columns:300px 1fr;gap:16px;align-items:start">

  <!-- Sidebar: playlist list -->
  <div class="card" style="padding:0;overflow:hidden">
    <div style="padding:12px 14px;border-bottom:1px solid var(--border);display:flex;align-items:center;justify-content:space-between">
      <span style="font-size:13px;font-weight:600;color:var(--text)">My Playlists</span>
      <span class="badge badge-gray"><?= count($playlists) ?></span>
    </div>
    <div id="pl-list" style="max-height:640px;overflow-y:auto">
      <?php if (empty($playlists)): ?>
      <div class="empty" style="padding:24px"><i class="fa-solid fa-list fa-fw"></i><p style="margin-top:8px">No playlists yet</p></div>
      <?php else: foreach ($playlists as $pl): ?>
      <div class="pl-item" data-id="<?= $pl['id'] ?>"
           onclick="openPl(<?= $pl['id'] ?>, <?= h(json_encode($pl['name'])) ?>, <?= h(json_encode($pl['type'])) ?>)"
           style="padding:10px 14px;border-bottom:1px solid rgba(51,65,85,.4);cursor:pointer;
                  transition:background .12s;display:flex;align-items:center;justify-content:space-between;gap:8px">
        <div style="min-width:0">
          <div style="font-size:13px;font-weight:600;color:var(--text);white-space:nowrap;overflow:hidden;text-overflow:ellipsis"><?= h($pl['name']) ?></div>
          <div style="font-size:10px;color:var(--muted);margin-top:3px;display:flex;align-items:center;gap:6px">
            <span class="pl-type-badge pl-type-<?= h($pl['type']) ?>"><?= h($pl['type']) ?></span>
            <?= (int)$pl['track_count'] ?> tracks
          </div>
        </div>
        <button class="btn btn-danger btn-xs" onclick="delPl(<?= $pl['id'] ?>, event)" title="Delete playlist"><i class="fa-solid fa-trash"></i></button>
      </div>
      <?php endforeach; endif; ?>
    </div>
  </div>

  <!-- Track panel -->
  <div class="card" id="pl-panel">
    <div class="empty"><i class="fa-solid fa-hand-pointer fa-fw"></i><p>Select a playlist to view its tracks</p></div>
  </div>

</div>

<script>
(function () {
'use strict';

var activePl   = null;
var activeName = '';

// We build slot options from the live encoder list (falls back to 1-12 if offline)
window._mc1SlotOpts = Array.from({length:12}, function(_, i) {
    return '<option value="'+(i+1)+'">Slot '+(i+1)+'</option>';
}).join('');

// We defer the mc1Api call to DOMContentLoaded — mc1Api is defined by footer.php
// which is parsed after this <script> block, so calling it here immediately throws
// ReferenceError and kills the entire IIFE before any window.* functions are defined.
document.addEventListener('DOMContentLoaded', function () {
    mc1Api('GET', '/api/v1/encoders').then(function (enc) {
        if (!Array.isArray(enc) || !enc.length) return;
        window._mc1SlotOpts = enc.map(function (e) {
            return '<option value="'+e.slot_id+'">Slot '+e.slot_id+(e.name?' \u2014 '+e.name.substring(0,14):'')+'</option>';
        }).join('');
    }).catch(function () {});
});

// ── Quick-create ─────────────────────────────────────────────────────────

window.showCreate  = function () { document.getElementById('create-modal').style.display = 'flex'; };
window.hideCreate  = function () { document.getElementById('create-modal').style.display = 'none'; };

window.createPlaylist = function () {
    var name = document.getElementById('np-name').value.trim();
    if (!name) { mc1Toast('Name required', 'warn'); return; }
    mc1Api('POST', '/app/api/playlists.php', {
        action: 'create',
        name: name,
        type: document.getElementById('np-type').value,
        description: document.getElementById('np-desc').value.trim()
    }).then(function (d) {
        if (d.ok) { mc1Toast('Playlist created', 'ok'); hideCreate(); location.reload(); }
        else mc1Toast(d.error || 'Error', 'err');
    });
};

// ── Playlist open + track display ─────────────────────────────────────────

window.openPl = function (id, name, type) {
    activePl   = id;
    activeName = name;
    document.querySelectorAll('.pl-item').forEach(function (el) {
        el.style.background = parseInt(el.dataset.id) === id ? 'var(--teal-glow)' : '';
    });

    var typeBadge = '<span class="pl-type-badge pl-type-'+esc(type)+'">'+esc(type)+'</span>';
    var panel     = document.getElementById('pl-panel');
    panel.innerHTML = '<div class="card-hdr">'
        + '<div class="card-title"><i class="fa-solid fa-list fa-fw"></i> '+esc(name)+' '+typeBadge+'</div>'
        + '<div style="display:flex;gap:8px;align-items:center">'
        + '<select class="form-select" id="load-slot" style="width:90px;padding:4px 8px;font-size:12px">'+window._mc1SlotOpts+'</select>'
        + '<button class="btn btn-primary btn-sm" onclick="loadToSlot()"><i class="fa-solid fa-upload"></i> Load to Slot</button>'
        + '</div></div>'
        + '<div id="pl-tracks"><div class="empty"><div class="spinner"></div></div></div>';

    mc1Api('POST', '/app/api/playlists.php', {action: 'get_tracks', id: id}).then(function (d) {
        var el = document.getElementById('pl-tracks');
        if (!el) return;
        if (!d.ok) { el.innerHTML = '<div class="alert alert-error">'+esc(d.error || 'Error')+'</div>'; return; }
        if (!d.tracks || !d.tracks.length) {
            el.innerHTML = '<div class="empty"><i class="fa-solid fa-music fa-fw"></i><p>Empty playlist</p></div>';
            return;
        }
        var rows = d.tracks.map(function (t, idx) {
            var dur = t.duration_ms ? fmtDur(Math.round(t.duration_ms/1000)) : '\u2014';
            var bpm = t.bpm ? Math.round(t.bpm) + ' bpm' : '';
            var flags = '';
            if (t.is_jingle)  flags += '<span class="jingle-badge">JINGLE</span>';
            if (t.is_sweeper) flags += '<span class="jingle-badge" style="background:rgba(239,68,68,.2);color:#f87171">SWEEP</span>';
            return '<tr>'
                + '<td style="color:var(--muted);font-size:11px;text-align:right">'+(idx+1)+'</td>'
                + '<td class="td-title">'+esc(t.title||'\u2014')+flags+'</td>'
                + '<td>'+esc(t.artist||'\u2014')+'</td>'
                + '<td>'+esc(t.album||'\u2014')+'</td>'
                + '<td style="color:var(--muted)">'+dur+'</td>'
                + '<td style="color:var(--muted)">'+bpm+'</td>'
                + '<td class="td-acts"><button class="btn btn-danger btn-xs" onclick="rmTrack('+t.playlist_track_id+')"><i class="fa-solid fa-xmark"></i></button></td>'
                + '</tr>';
        }).join('');
        el.innerHTML = '<div class="tbl-wrap"><table><thead><tr>'
            + '<th style="width:32px">#</th><th>Title</th><th>Artist</th><th>Album</th>'
            + '<th>Duration</th><th>BPM</th><th></th></tr></thead>'
            + '<tbody>'+rows+'</tbody></table></div>';
    });
};

window.loadToSlot = function () {
    if (!activePl) return;
    var slot = parseInt(document.getElementById('load-slot').value);
    mc1Api('POST', '/app/api/playlists.php', {action: 'load', id: activePl, slot: slot})
        .then(function (d) {
            if (!d || d.ok === false) { mc1Toast(d && d.error ? d.error : 'Failed to fetch tracks', 'err'); return; }
            mc1Api('POST', '/api/v1/playlist/load', {slot: slot, tracks: d.tracks}).then(function (r) {
                if (r && r.ok !== false) mc1Toast('Loaded '+d.tracks.length+' tracks to Slot '+slot, 'ok');
                else mc1Toast((r && r.error) || 'Encoder rejected load', 'err');
            }).catch(function () { mc1Toast('C++ API offline — tracks fetched but not loaded', 'err'); });
        })
        .catch(function () { mc1Toast('Request failed', 'err'); });
};

window.rmTrack = function (ptId) {
    mc1Api('POST', '/app/api/playlists.php', {action: 'remove_track', playlist_track_id: ptId}).then(function (d) {
        if (d.ok) { mc1Toast('Track removed', 'ok'); openPl(activePl, activeName, 'static'); }
        else mc1Toast(d.error || 'Error', 'err');
    });
};

window.delPl = function (id, e) {
    e.stopPropagation();
    if (!confirm('Delete this playlist and all its tracks from the database?')) return;
    mc1Api('POST', '/app/api/playlists.php', {action: 'delete', id: id}).then(function (d) {
        if (d.ok) { mc1Toast('Playlist deleted', 'ok'); location.reload(); }
        else mc1Toast(d.error || 'Error', 'err');
    });
};

// ── Generator Wizard ──────────────────────────────────────────────────────

var _wStep     = 1;
var _algos     = {};
var _cats      = [];
var _rotRules  = [];
var _selCatIds = [];
var _selRating = 1;
var _selAlgo   = 'weighted_random';

window.genWizardOpen = function () {
    _wStep = 1;
    _selCatIds = [];
    _selRating = 1;
    _selAlgo   = 'weighted_random';
    document.getElementById('gen-modal').style.display = 'flex';
    document.body.style.overflow = 'hidden';
    wRenderStep(1);

    // We load algorithms and categories in parallel on first open
    if (Object.keys(_algos).length === 0) {
        mc1Api('POST', '/app/api/playlists.php', {action: 'get_algorithms'}).then(function (d) {
            if (d.ok) { _algos = d.algorithms; wRenderAlgos(); }
        });
    } else {
        wRenderAlgos();
    }

    if (_cats.length === 0) {
        mc1Api('POST', '/app/api/playlists.php', {action: 'get_categories'}).then(function (d) {
            if (d.ok) { _cats = d.categories; wRenderCats(); }
        });
    } else {
        wRenderCats();
    }

    if (_rotRules.length === 0) {
        mc1Api('POST', '/app/api/playlists.php', {action: 'get_rotation_rules'}).then(function (d) {
            if (d.ok) { _rotRules = d.rules; wRenderRotRules(); }
        });
    }
};

window.genWizardClose = function () {
    document.getElementById('gen-modal').style.display = 'none';
    document.body.style.overflow = '';
};

function wRenderStep(n) {
    _wStep = n;
    // Update step indicators
    for (var i = 1; i <= 4; i++) {
        var el = document.getElementById('wstep-' + i);
        el.classList.remove('active', 'done');
        if (i === n)      el.classList.add('active');
        else if (i < n)   el.classList.add('done');
    }
    // Show correct page
    for (var j = 1; j <= 4; j++) {
        document.getElementById('wpage-'+j).classList.toggle('active', j === n);
    }
    // Update footer buttons
    var back = document.getElementById('wiz-back');
    var next = document.getElementById('wiz-next');
    var stat = document.getElementById('wiz-status');
    back.style.visibility = n > 1 ? 'visible' : 'hidden';
    if (n < 4) {
        next.innerHTML = 'Next <i class="fa-solid fa-chevron-right"></i>';
        next.disabled  = false;
        stat.textContent = '';
    } else {
        next.innerHTML = '<i class="fa-solid fa-bolt"></i> Generate Playlist';
        next.disabled  = false;
        stat.textContent = '';
    }

    // We load the preview when the user arrives at step 4
    if (n === 4) wLoadPreview();
}

window.genWizardNext = function () {
    if (_wStep === 1) {
        var name = document.getElementById('gn-name').value.trim();
        if (!name) { mc1Toast('Playlist name is required', 'warn'); return; }
        wRenderStep(2);
    } else if (_wStep === 2) {
        wRenderStep(3);
    } else if (_wStep === 3) {
        wRenderStep(4);
    } else if (_wStep === 4) {
        wDoGenerate();
    }
};

window.genWizardPrev = function () {
    if (_wStep > 1) wRenderStep(_wStep - 1);
};

function wRenderAlgos() {
    var el = document.getElementById('algo-list');
    if (!el) return;
    var keys = Object.keys(_algos);
    if (!keys.length) { el.innerHTML = '<div class="alert alert-error">Failed to load algorithms</div>'; return; }
    el.innerHTML = keys.map(function (k) {
        var a   = _algos[k];
        var sel = k === _selAlgo ? ' selected' : '';
        return '<div class="algo-card'+sel+'" data-algo="'+esc(k)+'" onclick="wPickAlgo(\''+esc(k)+'\')">'
            + '<label style="cursor:pointer;display:flex;align-items:flex-start;gap:8px">'
            + '<input type="radio" name="algo" value="'+esc(k)+'"'+(k===_selAlgo?' checked':'')+'>'
            + '<div><div class="algo-title">'+esc(a.label)+'</div>'
            + '<div class="algo-desc">'+esc(a.desc)+'</div></div>'
            + '</label></div>';
    }).join('');
}

window.wPickAlgo = function (k) {
    _selAlgo = k;
    document.querySelectorAll('.algo-card').forEach(function (el) {
        el.classList.toggle('selected', el.dataset.algo === k);
        var r = el.querySelector('input[type=radio]');
        if (r) r.checked = el.dataset.algo === k;
    });
};

function wRenderCats() {
    var el = document.getElementById('gf-cats');
    if (!el) return;
    if (!_cats.length) { el.innerHTML = '<span style="font-size:12px;color:var(--muted)">No categories defined</span>'; return; }
    el.innerHTML = _cats.map(function (c) {
        return '<div class="cat-chip" data-cid="'+c.id+'" onclick="wToggleCat('+c.id+')">'
            + '<i class="fa-solid '+(c.icon||'fa-tag')+'" style="color:'+esc(c.color_hex||'#14b8a6')+'"></i>'
            + esc(c.name) + '</div>';
    }).join('');
}

window.wToggleCat = function (id) {
    var idx = _selCatIds.indexOf(id);
    if (idx === -1) _selCatIds.push(id);
    else _selCatIds.splice(idx, 1);
    document.querySelectorAll('.cat-chip').forEach(function (el) {
        el.classList.toggle('sel', _selCatIds.indexOf(parseInt(el.dataset.cid)) !== -1);
    });
    wUpdatePoolHint();
};

function wUpdatePoolHint() {
    var el = document.getElementById('pool-hint');
    if (!el) return;
    var f = wBuildFilters();
    mc1Api('POST', '/app/api/playlists.php', {action: 'preview', filters: f, track_count: 1}).then(function (d) {
        if (d.ok) el.textContent = d.pool_size + ' tracks available in this pool';
        else el.textContent = '';
    }).catch(function () { el.textContent = ''; });
}

// We wire up the star-rating click handler
(function () {
    var sr = document.getElementById('gf-rating');
    if (!sr) return;
    sr.querySelectorAll('.s').forEach(function (star) {
        star.addEventListener('click', function () {
            _selRating = parseInt(star.dataset.v);
            sr.querySelectorAll('.s').forEach(function (s) {
                s.classList.toggle('on', parseInt(s.dataset.v) <= _selRating);
            });
            var hint = sr.parentElement.querySelector('div');
            if (hint) hint.textContent = _selRating === 1 ? 'Any rating (1+)' : _selRating + '+ stars';
        });
    });
})();

function wRenderRotRules() {
    var sel = document.getElementById('gr-rule-sel');
    if (!sel || !_rotRules.length) return;
    _rotRules.forEach(function (r) {
        var opt = document.createElement('option');
        opt.value       = r.id;
        opt.textContent = r.name;
        opt.dataset.rule = JSON.stringify(r);
        sel.appendChild(opt);
    });
}

window.loadRotationRule = function () {
    var sel = document.getElementById('gr-rule-sel');
    if (!sel || !sel.value) return;
    var opt = sel.options[sel.selectedIndex];
    var rule = {};
    try { rule = JSON.parse(opt.dataset.rule); } catch (e) { return; }
    if (rule.artist_separation !== undefined)
        document.getElementById('gr-artist-sep').value = rule.artist_separation;
    if (rule.song_separation_hrs !== undefined)
        document.getElementById('gr-song-hrs').value = rule.song_separation_hrs;
    if (rule.jingle_every_n !== undefined)
        document.getElementById('gr-jingle-n').value = rule.jingle_every_n || 0;
    mc1Toast('Rule "'+opt.textContent+'" applied', 'ok');
};

function wBuildFilters() {
    var f = {};
    var genre = (document.getElementById('gf-genre') || {}).value || '';
    if (genre.trim()) f.genre = genre.trim();
    if (_selCatIds.length) f.category_ids = _selCatIds;
    var yrF = parseInt((document.getElementById('gf-yr-from') || {}).value);
    var yrT = parseInt((document.getElementById('gf-yr-to')   || {}).value);
    if (!isNaN(yrF) && yrF > 0) f.year_from = yrF;
    if (!isNaN(yrT) && yrT > 0) f.year_to   = yrT;
    var bF = parseFloat((document.getElementById('gf-bpm-min') || {}).value);
    var bT = parseFloat((document.getElementById('gf-bpm-max') || {}).value);
    if (!isNaN(bF) && bF > 0) f.bpm_min = bF;
    if (!isNaN(bT) && bT > 0) f.bpm_max = bT;
    if (_selRating > 1) f.rating_min = _selRating;
    if ((document.getElementById('gf-inc-jingle')  || {}).checked) f.include_jingles  = true;
    if ((document.getElementById('gf-inc-sweeper') || {}).checked) f.include_sweepers = true;
    if ((document.getElementById('gf-inc-spot')    || {}).checked) f.include_spots    = true;
    return f;
}

function wBuildRules() {
    return {
        artist_separation:   parseInt((document.getElementById('gr-artist-sep') || {}).value) || 3,
        song_separation_hrs: parseInt((document.getElementById('gr-song-hrs')   || {}).value) || 4,
        jingle_every_n:      parseInt((document.getElementById('gr-jingle-n')   || {}).value) || 0,
    };
}

function wGetEnergyDir() {
    var checked = document.querySelector('input[name="energy-dir"]:checked');
    return checked ? checked.value : 'wave';
}

function wGetTrackCount() {
    var dur = parseFloat((document.getElementById('gn-dur') || {}).value);
    if (!isNaN(dur) && dur > 0) return {duration_min: Math.round(dur * 60)};
    var cnt = parseInt((document.getElementById('gn-count') || {}).value) || 50;
    return {track_count: cnt};
}

function wLoadPreview() {
    var body = document.getElementById('preview-body');
    var summ = document.getElementById('preview-summary');
    body.innerHTML = '<div class="empty"><div class="spinner"></div><p style="margin-top:8px">Generating preview…</p></div>';
    summ.innerHTML = '';

    var payload = Object.assign({
        action:           'preview',
        algorithm:        _selAlgo,
        filters:          wBuildFilters(),
        rules:            wBuildRules(),
        energy_direction: wGetEnergyDir(),
    }, wGetTrackCount());

    mc1Api('POST', '/app/api/playlists.php', payload).then(function (d) {
        if (!d.ok) { body.innerHTML = '<div class="alert alert-error">'+esc(d.error||'Preview failed')+'</div>'; return; }

        // We render the summary stats
        var estDur = fmtDur(d.estimated_duration_sec);
        summ.innerHTML = '<div class="badge badge-gray"><i class="fa-solid fa-database"></i> '+d.pool_size+' in pool</div>'
            + '<div class="badge badge-gray"><i class="fa-solid fa-list"></i> Est. '+d.estimated_total+' tracks</div>'
            + '<div class="badge badge-gray"><i class="fa-solid fa-clock"></i> ~'+estDur+' runtime</div>';

        // We render the preview track list
        if (!d.tracks.length) {
            body.innerHTML = '<div class="empty"><p>No tracks matched — try relaxing the filters</p></div>';
            return;
        }
        var rows = d.tracks.map(function (t, i) {
            var flags = '';
            if (t.is_jingle)  flags = '<span class="jingle-badge">JINGLE</span>';
            if (t.is_sweeper) flags = '<span class="jingle-badge" style="background:rgba(239,68,68,.2);color:#f87171">SWEEP</span>';
            return '<div class="preview-track">'
                + '<div class="pnum">'+(i+1)+'</div>'
                + '<div><span style="color:var(--text)">'+esc(t.title||'\u2014')+'</span>'+flags
                + '<div class="preview-artist">'+esc(t.artist||'\u2014')+'</div></div>'
                + '<div style="color:var(--muted);font-size:11px">'+esc(t.album||'')+'</div>'
                + '<div style="color:var(--muted)">'+esc(t.duration||'\u2014')+'</div>'
                + '<div style="color:var(--muted)">'+(t.bpm?t.bpm+' bpm':'')+'</div>'
                + '</div>';
        }).join('');
        body.innerHTML = '<div style="font-size:11px;color:var(--muted);margin-bottom:6px">Showing first '+d.tracks.length+' tracks of the estimated playlist:</div>'
            + '<div style="border:1px solid var(--border);border-radius:6px;overflow:hidden">'
            + '<div style="display:grid;grid-template-columns:28px 1fr 120px 60px 40px;gap:8px;padding:6px 8px;font-size:10px;font-weight:700;color:var(--muted);text-transform:uppercase;border-bottom:1px solid var(--border)">'
            + '<div>#</div><div>Title / Artist</div><div>Album</div><div>Dur</div><div>BPM</div></div>'
            + rows + '</div>';
    }).catch(function (e) {
        body.innerHTML = '<div class="alert alert-error">Preview request failed</div>';
    });
}

function wDoGenerate() {
    var btn = document.getElementById('wiz-next');
    var stat = document.getElementById('wiz-status');
    btn.disabled = true;
    btn.innerHTML = '<div class="spinner" style="width:14px;height:14px;border-width:2px;display:inline-block;vertical-align:middle;margin-right:6px"></div> Generating…';
    stat.textContent = '';

    var payload = Object.assign({
        action:           'generate',
        name:             document.getElementById('gn-name').value.trim(),
        description:      document.getElementById('gn-desc').value.trim(),
        algorithm:        _selAlgo,
        filters:          wBuildFilters(),
        rules:            wBuildRules(),
        energy_direction: wGetEnergyDir(),
        write_m3u:        (document.getElementById('gn-m3u') || {}).checked !== false,
    }, wGetTrackCount());

    mc1Api('POST', '/app/api/playlists.php', payload).then(function (d) {
        btn.disabled = false;
        btn.innerHTML = '<i class="fa-solid fa-bolt"></i> Generate Playlist';
        if (!d.ok) {
            mc1Toast(d.error || 'Generation failed', 'err');
            stat.textContent = d.error || '';
            return;
        }
        var msg = 'Generated ' + d.track_count + ' tracks';
        if (d.m3u_path) msg += ' + M3U saved';
        mc1Toast(msg, 'ok');
        genWizardClose();
        location.reload();
    }).catch(function () {
        btn.disabled = false;
        btn.innerHTML = '<i class="fa-solid fa-bolt"></i> Generate Playlist';
        mc1Toast('Request failed', 'err');
    });
}

// ── Shared helpers ────────────────────────────────────────────────────────

function esc(s) {
    return String(s || '').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

function fmtDur(sec) {
    if (!sec || sec < 0) return '\u2014';
    var h = Math.floor(sec / 3600);
    var m = Math.floor((sec % 3600) / 60);
    var s = sec % 60;
    if (h > 0) return h + ':' + pad(m) + ':' + pad(s);
    return m + ':' + pad(s);
}

function pad(n) { return n < 10 ? '0'+n : ''+n; }

// We close the wizard on backdrop click or Escape
document.getElementById('gen-modal').addEventListener('click', function (e) {
    if (e.target === this) genWizardClose();
});
document.addEventListener('keydown', function (e) {
    if (e.key === 'Escape') {
        hideCreate();
        genWizardClose();
    }
});

})();
</script>

<?php require_once __DIR__ . '/app/inc/footer.php'; ?>
