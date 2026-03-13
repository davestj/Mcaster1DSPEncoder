<?php
/**
 * effects-rack.php — Modular Effects Rack Editor
 *
 * File:    src/linux/web_ui/effects-rack.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-25
 * Purpose: We provide a drag-and-drop signal chain editor with 3D SVG icons.
 *          Users can add/remove/reorder DSP units, toggle enable, and adjust
 *          parameters live. Supports global rack and per-slot racks.
 *
 * Standards:
 *  - No exit()/die() — uopz extension is active
 *  - DOMContentLoaded wraps all mc1Api() calls
 *  - Browser JS calls /api/v1/ directly (no PHP→C++ loopback)
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/app/inc/auth.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/user_auth.php';

$page_title = 'Effects Rack';
$active_nav = 'effects';

require_once __DIR__ . '/app/inc/header.php';
?>

<style>
.fx-page { padding:24px; }
.fx-header { display:flex; align-items:center; justify-content:space-between; margin-bottom:20px; flex-wrap:wrap; gap:12px; }
.fx-header h2 { margin:0; font-size:1.4rem; color:var(--text); }
.fx-toolbar { display:flex; gap:10px; align-items:center; }
.fx-bypass { display:flex; align-items:center; gap:8px; padding:8px 16px; background:var(--card); border:1px solid var(--border); border-radius:var(--radius-sm); cursor:pointer; color:var(--text-dim); font-size:.85rem; }
.fx-bypass.active { border-color:var(--orange); color:var(--orange); }
.fx-bypass i { font-size:1rem; }
.btn-add { background:var(--teal); border:none; color:#fff; padding:9px 18px; border-radius:var(--radius-sm); cursor:pointer; font-size:.85rem; font-weight:600; display:flex; align-items:center; gap:6px; }
.btn-add:hover { background:var(--teal2); }

/* ── Signal Chain ─────────────────────────────── */
.fx-chain { display:flex; align-items:stretch; gap:0; margin:20px 0; min-height:200px; overflow-x:auto; padding-bottom:8px; }
.fx-chain-empty { flex:1; display:flex; align-items:center; justify-content:center; background:var(--card); border:2px dashed var(--border); border-radius:var(--radius); color:var(--muted); font-size:.95rem; min-height:200px; }
.fx-wire { width:36px; min-width:36px; display:flex; align-items:center; justify-content:center; position:relative; }
.fx-wire::before { content:''; position:absolute; top:50%; left:0; right:0; height:3px; background:linear-gradient(to right, var(--teal), var(--cyan)); border-radius:2px; }
.fx-wire-dot { width:10px; height:10px; background:var(--teal); border-radius:50%; position:relative; z-index:1; box-shadow:0 0 6px var(--teal-glow); }

.fx-unit { width:200px; min-width:200px; background:var(--card); border:1px solid var(--border); border-radius:var(--radius); padding:16px 14px; cursor:grab; transition:border-color .2s, box-shadow .2s, transform .15s; position:relative; }
.fx-unit:hover { border-color:var(--teal); }
.fx-unit.dragging { opacity:.5; transform:scale(.95); }
.fx-unit.drag-over { border-color:var(--cyan); box-shadow:0 0 16px rgba(8,145,178,.3); }
.fx-unit.disabled { opacity:.5; }
.fx-unit-header { display:flex; align-items:center; gap:10px; margin-bottom:12px; }
.fx-unit-icon { width:48px; height:48px; flex-shrink:0; }
.fx-unit-icon svg { width:48px; height:48px; }
.fx-unit-title { flex:1; }
.fx-unit-title h4 { margin:0; font-size:.85rem; color:var(--text); }
.fx-unit-title span { font-size:.7rem; color:var(--muted); }
.fx-unit-toggle { width:36px; height:20px; border-radius:10px; background:var(--border); cursor:pointer; position:relative; transition:background .2s; border:none; padding:0; }
.fx-unit-toggle.on { background:var(--teal); }
.fx-unit-toggle::after { content:''; position:absolute; top:2px; left:2px; width:16px; height:16px; border-radius:50%; background:#fff; transition:left .2s; }
.fx-unit-toggle.on::after { left:18px; }
.fx-unit-params { margin-top:8px; }
.fx-param { margin-bottom:8px; }
.fx-param label { display:block; font-size:.7rem; color:var(--text-dim); margin-bottom:3px; }
.fx-param input[type=range] { width:100%; height:4px; -webkit-appearance:none; appearance:none; background:var(--border); border-radius:2px; outline:none; }
.fx-param input[type=range]::-webkit-slider-thumb { -webkit-appearance:none; width:14px; height:14px; border-radius:50%; background:var(--teal); cursor:pointer; }
.fx-param .fx-val { float:right; font-family:var(--font-mono); font-size:.7rem; color:var(--teal); }
.fx-unit-actions { display:flex; gap:6px; margin-top:10px; }
.fx-unit-actions button { flex:1; padding:5px; border:1px solid var(--border); background:transparent; color:var(--text-dim); border-radius:var(--radius-xs); cursor:pointer; font-size:.7rem; }
.fx-unit-actions button:hover { border-color:var(--teal); color:var(--teal); }
.fx-unit-actions button.danger:hover { border-color:var(--red); color:var(--red); }

/* ── Add Effect Drawer ────────────────────────── */
.fx-drawer { display:none; background:var(--card); border:1px solid var(--border); border-radius:var(--radius); padding:20px; margin-bottom:20px; }
.fx-drawer.open { display:block; }
.fx-drawer h3 { margin:0 0 16px; color:var(--text); font-size:1rem; }
.fx-drawer-grid { display:grid; grid-template-columns:repeat(auto-fill, minmax(180px, 1fr)); gap:12px; }
.fx-type-card { background:var(--bg3); border:1px solid var(--border); border-radius:var(--radius-sm); padding:14px; cursor:pointer; text-align:center; transition:border-color .2s; }
.fx-type-card:hover { border-color:var(--teal); }
.fx-type-card svg { width:48px; height:48px; margin-bottom:8px; }
.fx-type-card h4 { margin:0 0 4px; font-size:.85rem; color:var(--text); }
.fx-type-card p { margin:0; font-size:.7rem; color:var(--muted); line-height:1.3; }

/* ── Status Bar ───────────────────────────────── */
.fx-status { background:var(--card); border:1px solid var(--border); border-radius:var(--radius); padding:12px 16px; margin-top:16px; display:flex; align-items:center; justify-content:space-between; }
.fx-status-label { color:var(--text-dim); font-size:.8rem; }
.fx-status-val { color:var(--teal); font-family:var(--font-mono); font-size:.8rem; }
</style>

<div class="fx-page">
  <div class="fx-header">
    <h2><i class="fa-solid fa-sliders" style="color:var(--teal);margin-right:8px"></i>Effects Rack</h2>
    <div class="fx-toolbar">
      <div class="fx-bypass" id="fx-bypass" onclick="toggleBypass()">
        <i class="fa-solid fa-power-off"></i> <span>Bypass</span>
      </div>
      <button class="btn-add" onclick="toggleDrawer()">
        <i class="fa-solid fa-plus"></i> Add Effect
      </button>
    </div>
  </div>

  <!-- Add Effect Drawer -->
  <div class="fx-drawer" id="fx-drawer">
    <h3>Add Effect Unit</h3>
    <div class="fx-drawer-grid" id="fx-types"></div>
  </div>

  <!-- Signal Chain -->
  <div class="fx-chain" id="fx-chain">
    <div class="fx-chain-empty" id="fx-empty">
      <span>No effects in chain — click "Add Effect" to get started</span>
    </div>
  </div>

  <!-- PTT Section -->
  <div style="background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:20px;margin-top:20px;">
    <div style="display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:16px;">
      <div style="display:flex;align-items:center;gap:16px;">
        <h3 style="margin:0;color:var(--text);font-size:1rem;"><i class="fa-solid fa-microphone" style="color:var(--teal);margin-right:6px"></i>Push-to-Talk</h3>
        <button id="ptt-btn" class="ptt-button" onmousedown="pttDown()" onmouseup="pttUp()" ontouchstart="pttDown()" ontouchend="pttUp()">
          <i class="fa-solid fa-microphone"></i> PTT
        </button>
        <span id="ptt-state" style="font-size:.8rem;color:var(--muted);">Inactive</span>
      </div>
      <div style="display:flex;align-items:center;gap:16px;flex-wrap:wrap;">
        <div style="min-width:120px;">
          <label style="display:block;font-size:.7rem;color:var(--text-dim);margin-bottom:3px;">Duck Amount <span id="ptt-duck-val" style="color:var(--teal);font-family:var(--font-mono);">-15 dB</span></label>
          <input type="range" id="ptt-duck" min="-30" max="-3" step="1" value="-15" style="width:120px;height:4px;-webkit-appearance:none;appearance:none;background:var(--border);border-radius:2px;outline:none;" oninput="document.getElementById('ptt-duck-val').textContent=this.value+' dB'" onchange="updatePttConfig()">
        </div>
        <div style="min-width:100px;">
          <label style="display:block;font-size:.7rem;color:var(--text-dim);margin-bottom:3px;">Attack <span id="ptt-atk-val" style="color:var(--teal);font-family:var(--font-mono);">50 ms</span></label>
          <input type="range" id="ptt-attack" min="5" max="200" step="5" value="50" style="width:100px;height:4px;-webkit-appearance:none;appearance:none;background:var(--border);border-radius:2px;outline:none;" oninput="document.getElementById('ptt-atk-val').textContent=this.value+' ms'" onchange="updatePttConfig()">
        </div>
        <div style="min-width:100px;">
          <label style="display:block;font-size:.7rem;color:var(--text-dim);margin-bottom:3px;">Release <span id="ptt-rel-val" style="color:var(--teal);font-family:var(--font-mono);">500 ms</span></label>
          <input type="range" id="ptt-release" min="50" max="2000" step="50" value="500" style="width:100px;height:4px;-webkit-appearance:none;appearance:none;background:var(--border);border-radius:2px;outline:none;" oninput="document.getElementById('ptt-rel-val').textContent=this.value+' ms'" onchange="updatePttConfig()">
        </div>
        <div style="text-align:center;min-width:60px;">
          <div style="font-size:.7rem;color:var(--text-dim);margin-bottom:3px;">Duck Level</div>
          <div id="ptt-meter" style="width:20px;height:50px;background:var(--bg);border:1px solid var(--border);border-radius:3px;margin:0 auto;position:relative;overflow:hidden;">
            <div id="ptt-meter-fill" style="position:absolute;bottom:0;left:0;right:0;height:0;background:linear-gradient(to top,var(--teal),var(--cyan));transition:height .1s;"></div>
          </div>
        </div>
      </div>
    </div>
    <p style="margin:8px 0 0;font-size:.7rem;color:var(--muted);">Hold the PTT button or press <kbd style="background:var(--bg3);border:1px solid var(--border);padding:1px 6px;border-radius:3px;font-size:.7rem;">Space</kbd> to duck music for voiceover.</p>
  </div>

  <style>
  .ptt-button { width:80px; height:80px; border-radius:50%; border:3px solid var(--border); background:var(--bg3); color:var(--text-dim); font-size:1rem; font-weight:700; cursor:pointer; display:flex; flex-direction:column; align-items:center; justify-content:center; gap:4px; transition:all .15s; user-select:none; -webkit-user-select:none; }
  .ptt-button:hover { border-color:var(--teal); color:var(--teal); }
  .ptt-button.active { background:var(--red); border-color:var(--red); color:#fff; box-shadow:0 0 20px rgba(239,68,68,.4); transform:scale(1.05); }
  .ptt-button i { font-size:1.4rem; }
  </style>

  <!-- Status Bar -->
  <div class="fx-status">
    <span class="fx-status-label">Units: <span class="fx-status-val" id="fx-count">0</span></span>
    <span class="fx-status-label">Mode: <span class="fx-status-val" id="fx-mode">Global</span></span>
    <span class="fx-status-label">Status: <span class="fx-status-val" id="fx-state">Active</span></span>
  </div>
</div>

<script>
/* ══════════════════════════════════════════════════════════════════════════════
 * 3D SVG Icons for each effect type
 * ══════════════════════════════════════════════════════════════════════════════ */
var FX_ICONS = {
  eq: '<svg viewBox="0 0 48 48" xmlns="http://www.w3.org/2000/svg"><defs><linearGradient id="g-eq" x1="0" y1="0" x2="1" y2="1"><stop offset="0%" stop-color="#14b8a6"/><stop offset="100%" stop-color="#0891b2"/></linearGradient><filter id="f-eq"><feDropShadow dx="1" dy="2" stdDeviation="2" flood-color="#000" flood-opacity=".3"/><feSpecularLighting surfaceScale="3" specularConstant=".4" specularExponent="15" result="spec"><fePointLight x="24" y="-10" z="30"/></feSpecularLighting></filter></defs><g filter="url(#f-eq)" style="transform:perspective(200px) rotateY(-5deg)"><rect x="6" y="8" width="8" height="32" rx="3" fill="url(#g-eq)" opacity=".9"/><rect x="20" y="14" width="8" height="26" rx="3" fill="url(#g-eq)" opacity=".8"/><rect x="34" y="6" width="8" height="34" rx="3" fill="url(#g-eq)" opacity=".7"/><circle cx="10" cy="18" r="3" fill="#fff" opacity=".9"/><circle cx="24" cy="24" r="3" fill="#fff" opacity=".9"/><circle cx="38" cy="15" r="3" fill="#fff" opacity=".9"/></g></svg>',

  compressor: '<svg viewBox="0 0 48 48" xmlns="http://www.w3.org/2000/svg"><defs><linearGradient id="g-comp" x1="0" y1="0" x2="1" y2="1"><stop offset="0%" stop-color="#14b8a6"/><stop offset="100%" stop-color="#0891b2"/></linearGradient><filter id="f-comp"><feDropShadow dx="1" dy="2" stdDeviation="2" flood-color="#000" flood-opacity=".3"/></filter></defs><g filter="url(#f-comp)" style="transform:perspective(200px) rotateY(-5deg)"><path d="M4 32 Q8 32 10 20 Q12 8 16 24 Q20 40 22 24 Q24 12 28 24 Q30 32 34 24 Q36 18 38 24 Q40 28 44 28" fill="none" stroke="url(#g-comp)" stroke-width="2.5" stroke-linecap="round"/><path d="M24 6 L24 14 M20 10 L28 10" stroke="#ef4444" stroke-width="2" stroke-linecap="round" opacity=".7"/><line x1="4" y1="24" x2="44" y2="24" stroke="#334155" stroke-width="1" stroke-dasharray="2 3"/></g></svg>',

  limiter: '<svg viewBox="0 0 48 48" xmlns="http://www.w3.org/2000/svg"><defs><linearGradient id="g-lim" x1="0" y1="0" x2="1" y2="1"><stop offset="0%" stop-color="#14b8a6"/><stop offset="100%" stop-color="#0891b2"/></linearGradient><filter id="f-lim"><feDropShadow dx="1" dy="2" stdDeviation="2" flood-color="#000" flood-opacity=".3"/></filter></defs><g filter="url(#f-lim)" style="transform:perspective(200px) rotateY(-5deg)"><path d="M4 28 Q8 28 10 18 Q12 8 14 12 L14 12 L18 12 Q20 12 22 18 Q24 28 26 28 Q28 28 30 18 Q32 8 34 12 L38 12 Q40 12 42 20 Q44 28 44 28" fill="none" stroke="url(#g-lim)" stroke-width="2.5" stroke-linecap="round"/><line x1="2" y1="12" x2="46" y2="12" stroke="#ef4444" stroke-width="2" stroke-dasharray="4 2" opacity=".6"/><text x="24" y="44" text-anchor="middle" fill="#64748b" font-size="7" font-family="Inter,system-ui">CEILING</text></g></svg>',

  noise_gate: '<svg viewBox="0 0 48 48" xmlns="http://www.w3.org/2000/svg"><defs><linearGradient id="g-ng" x1="0" y1="0" x2="1" y2="1"><stop offset="0%" stop-color="#14b8a6"/><stop offset="100%" stop-color="#0891b2"/></linearGradient><filter id="f-ng"><feDropShadow dx="1" dy="2" stdDeviation="2" flood-color="#000" flood-opacity=".3"/></filter></defs><g filter="url(#f-ng)" style="transform:perspective(200px) rotateY(-5deg)"><path d="M4 24 Q6 24 8 16 Q10 8 12 24 Q14 32 16 24" fill="none" stroke="url(#g-ng)" stroke-width="2.5" stroke-linecap="round"/><line x1="16" y1="24" x2="32" y2="24" stroke="#64748b" stroke-width="1.5" stroke-dasharray="3 2"/><path d="M32 24 Q34 16 36 24 Q38 32 40 16 Q42 8 44 24" fill="none" stroke="url(#g-ng)" stroke-width="2.5" stroke-linecap="round"/><rect x="18" y="18" width="12" height="12" rx="2" fill="none" stroke="#eab308" stroke-width="1.5" opacity=".5"/><text x="24" y="27" text-anchor="middle" fill="#eab308" font-size="7" font-family="Inter,system-ui" opacity=".6">GATE</text></g></svg>',

  reverb: '<svg viewBox="0 0 48 48" xmlns="http://www.w3.org/2000/svg"><defs><linearGradient id="g-rv" x1="0" y1="0" x2="1" y2="1"><stop offset="0%" stop-color="#14b8a6"/><stop offset="100%" stop-color="#0891b2"/></linearGradient><filter id="f-rv"><feDropShadow dx="1" dy="2" stdDeviation="2" flood-color="#000" flood-opacity=".3"/></filter></defs><g filter="url(#f-rv)" style="transform:perspective(200px) rotateY(-5deg)"><circle cx="24" cy="24" r="6" fill="none" stroke="url(#g-rv)" stroke-width="2"/><circle cx="24" cy="24" r="12" fill="none" stroke="url(#g-rv)" stroke-width="1.5" opacity=".6"/><circle cx="24" cy="24" r="18" fill="none" stroke="url(#g-rv)" stroke-width="1" opacity=".3"/><circle cx="24" cy="24" r="22" fill="none" stroke="url(#g-rv)" stroke-width=".5" opacity=".15"/></g></svg>',

  delay: '<svg viewBox="0 0 48 48" xmlns="http://www.w3.org/2000/svg"><defs><linearGradient id="g-dl" x1="0" y1="0" x2="1" y2="1"><stop offset="0%" stop-color="#14b8a6"/><stop offset="100%" stop-color="#0891b2"/></linearGradient><filter id="f-dl"><feDropShadow dx="1" dy="2" stdDeviation="2" flood-color="#000" flood-opacity=".3"/></filter></defs><g filter="url(#f-dl)" style="transform:perspective(200px) rotateY(-5deg)"><path d="M4 24 Q8 12 12 24 Q16 36 20 24" fill="none" stroke="url(#g-dl)" stroke-width="2.5" opacity="1"/><path d="M14 24 Q18 14 22 24 Q26 34 30 24" fill="none" stroke="url(#g-dl)" stroke-width="2" opacity=".6"/><path d="M24 24 Q28 16 32 24 Q36 32 40 24" fill="none" stroke="url(#g-dl)" stroke-width="1.5" opacity=".35"/><path d="M34 24 Q37 18 40 24 Q43 30 46 24" fill="none" stroke="url(#g-dl)" stroke-width="1" opacity=".15"/></g></svg>'
};

var FX_PARAM_DEFS = {
  eq:         [{key:'preset',label:'Preset',type:'select',opts:['flat','classic_rock','country','modern_rock','broadcast','spoken_word']}],
  compressor: [
    {key:'threshold_db',label:'Threshold',min:-60,max:0,step:1,unit:'dB'},
    {key:'ratio',label:'Ratio',min:1,max:20,step:0.5,unit:':1'},
    {key:'attack_ms',label:'Attack',min:0.1,max:100,step:0.5,unit:'ms'},
    {key:'release_ms',label:'Release',min:10,max:1000,step:10,unit:'ms'},
    {key:'makeup_gain_db',label:'Makeup',min:0,max:24,step:0.5,unit:'dB'},
    {key:'limiter_db',label:'Limiter',min:-12,max:0,step:0.5,unit:'dB'}
  ],
  limiter: [
    {key:'ceiling_db',label:'Ceiling',min:-12,max:0,step:0.5,unit:'dB'},
    {key:'attack_ms',label:'Attack',min:0.01,max:10,step:0.01,unit:'ms'},
    {key:'release_ms',label:'Release',min:1,max:500,step:1,unit:'ms'}
  ],
  noise_gate: [
    {key:'threshold_db',label:'Threshold',min:-80,max:0,step:1,unit:'dB'},
    {key:'range_db',label:'Range',min:-96,max:0,step:1,unit:'dB'},
    {key:'attack_ms',label:'Attack',min:0.1,max:50,step:0.1,unit:'ms'},
    {key:'hold_ms',label:'Hold',min:0,max:500,step:5,unit:'ms'},
    {key:'release_ms',label:'Release',min:1,max:500,step:5,unit:'ms'}
  ],
  reverb: [{key:'mix',label:'Mix',min:0,max:1,step:0.01,unit:''}],
  delay:  [
    {key:'time_ms',label:'Time',min:1,max:2000,step:1,unit:'ms'},
    {key:'feedback',label:'Feedback',min:0,max:0.95,step:0.01,unit:''}
  ]
};

var rackState = {bypass:false, units:[]};
var dragSrcId = null;

/* ── Load rack state from API ─────────────────────────────────────────────── */
function loadRack() {
  mc1Api('GET', '/api/v1/effects/global').then(function(d) {
    if (!d || !d.ok) return;
    rackState = d.rack || {bypass:false, units:[]};
    renderChain();
    updateStatus();
  }).catch(function(){});
}

/* ── Load available unit types for the drawer ─────────────────────────────── */
function loadTypes() {
  mc1Api('GET', '/api/v1/effects/unit-types').then(function(d) {
    if (!d || !d.ok) return;
    var grid = document.getElementById('fx-types');
    grid.innerHTML = '';
    (d.types || []).forEach(function(t) {
      var card = document.createElement('div');
      card.className = 'fx-type-card';
      card.innerHTML = (FX_ICONS[t.type] || '') +
        '<h4>' + t.name + '</h4><p>' + t.description + '</p>';
      card.addEventListener('click', function(){ addUnit(t.type); });
      grid.appendChild(card);
    });
  }).catch(function(){});
}

/* ── Add a unit to the rack ───────────────────────────────────────────────── */
function addUnit(type) {
  mc1Api('POST', '/api/v1/effects/global/units', {type:type, enabled:true}).then(function(d) {
    if (d && d.ok) {
      mc1Toast('Added ' + type, 'ok');
      loadRack();
      document.getElementById('fx-drawer').classList.remove('open');
    } else {
      mc1Toast((d && d.error) || 'Add failed', 'err');
    }
  }).catch(function(){ mc1Toast('API offline', 'err'); });
}

/* ── Remove a unit ────────────────────────────────────────────────────────── */
function removeUnit(id) {
  mc1Api('DELETE', '/api/v1/effects/global/units/' + id).then(function(d) {
    if (d && d.ok) { mc1Toast('Removed', 'ok'); loadRack(); }
    else mc1Toast('Remove failed', 'err');
  });
}

/* ── Toggle unit enabled ──────────────────────────────────────────────────── */
function toggleUnit(id, on) {
  mc1Api('PUT', '/api/v1/effects/global', {unit_id:id, enabled:on}).then(function(d) {
    if (d && d.ok) loadRack();
  });
}

/* ── Update unit params ───────────────────────────────────────────────────── */
function updateParam(id, key, val) {
  var params = {}; params[key] = val;
  mc1Api('PUT', '/api/v1/effects/global', {unit_id:id, params:params}).then(function(){});
}

/* ── Toggle bypass ────────────────────────────────────────────────────────── */
function toggleBypass() {
  var newState = !rackState.bypass;
  mc1Api('PUT', '/api/v1/effects/global', {bypass:newState}).then(function(d) {
    if (d && d.ok) { rackState.bypass = newState; updateStatus(); }
  });
}

/* ── Toggle drawer ────────────────────────────────────────────────────────── */
function toggleDrawer() {
  document.getElementById('fx-drawer').classList.toggle('open');
}

/* ── Render the signal chain ──────────────────────────────────────────────── */
function renderChain() {
  var chain = document.getElementById('fx-chain');
  var empty = document.getElementById('fx-empty');
  var units = rackState.units || [];

  if (units.length === 0) {
    chain.innerHTML = '';
    chain.appendChild(empty);
    empty.style.display = 'flex';
    return;
  }
  empty.style.display = 'none';
  chain.innerHTML = '';

  /* We add input wire dot */
  chain.innerHTML += '<div class="fx-wire"><div class="fx-wire-dot"></div></div>';

  units.forEach(function(u, idx) {
    var icon = FX_ICONS[u.type] || '';
    var isOn = u.enabled;
    var paramDefs = FX_PARAM_DEFS[u.type] || [];
    var params = u.params || {};

    var html = '<div class="fx-unit' + (isOn ? '' : ' disabled') + '" draggable="true" data-id="' + u.id + '" data-idx="' + idx + '">';
    html += '<div class="fx-unit-header">';
    html += '<div class="fx-unit-icon">' + icon + '</div>';
    html += '<div class="fx-unit-title"><h4>' + (u.type || 'Unknown') + '</h4><span>Unit #' + u.id + '</span></div>';
    html += '<button class="fx-unit-toggle ' + (isOn ? 'on' : '') + '" onclick="toggleUnit(' + u.id + ',' + (!isOn) + ')"></button>';
    html += '</div>';

    /* We render parameter sliders */
    if (paramDefs.length > 0) {
      html += '<div class="fx-unit-params">';
      paramDefs.forEach(function(p) {
        var val = params[p.key] !== undefined ? params[p.key] : (p.min || 0);
        if (p.type === 'select') {
          html += '<div class="fx-param"><label>' + p.label + '</label>';
          html += '<select onchange="updateParam(' + u.id + ',\'' + p.key + '\',this.value)" style="width:100%;background:var(--bg3);border:1px solid var(--border);color:var(--text);padding:4px;border-radius:var(--radius-xs);font-size:.75rem">';
          (p.opts||[]).forEach(function(o){ html += '<option value="' + o + '"' + (val===o?' selected':'') + '>' + o + '</option>'; });
          html += '</select></div>';
        } else {
          html += '<div class="fx-param"><label>' + p.label + ' <span class="fx-val" id="pv-' + u.id + '-' + p.key + '">' + parseFloat(val).toFixed(1) + ' ' + (p.unit||'') + '</span></label>';
          html += '<input type="range" min="' + p.min + '" max="' + p.max + '" step="' + p.step + '" value="' + val + '" oninput="document.getElementById(\'pv-' + u.id + '-' + p.key + '\').textContent=parseFloat(this.value).toFixed(1)+\' ' + (p.unit||'') + '\'" onchange="updateParam(' + u.id + ',\'' + p.key + '\',parseFloat(this.value))"></div>';
        }
      });
      html += '</div>';
    }

    html += '<div class="fx-unit-actions">';
    html += '<button onclick="removeUnit(' + u.id + ')" class="danger"><i class="fa-solid fa-trash-can"></i> Remove</button>';
    html += '</div></div>';

    chain.innerHTML += html;

    /* We add wire between units */
    if (idx < units.length - 1) {
      chain.innerHTML += '<div class="fx-wire"><div class="fx-wire-dot"></div></div>';
    }
  });

  /* We add output wire dot */
  chain.innerHTML += '<div class="fx-wire"><div class="fx-wire-dot"></div></div>';

  /* We set up drag-and-drop */
  setupDragDrop();
}

/* ── Drag and Drop ────────────────────────────────────────────────────────── */
function setupDragDrop() {
  document.querySelectorAll('.fx-unit').forEach(function(el) {
    el.addEventListener('dragstart', function(e) {
      dragSrcId = parseInt(el.getAttribute('data-id'));
      el.classList.add('dragging');
      e.dataTransfer.effectAllowed = 'move';
    });
    el.addEventListener('dragend', function() {
      el.classList.remove('dragging');
      document.querySelectorAll('.fx-unit').forEach(function(u){ u.classList.remove('drag-over'); });
    });
    el.addEventListener('dragover', function(e) {
      e.preventDefault();
      e.dataTransfer.dropEffect = 'move';
      el.classList.add('drag-over');
    });
    el.addEventListener('dragleave', function() { el.classList.remove('drag-over'); });
    el.addEventListener('drop', function(e) {
      e.preventDefault();
      el.classList.remove('drag-over');
      var targetId = parseInt(el.getAttribute('data-id'));
      if (dragSrcId === targetId) return;
      /* We build new order: move dragSrcId to targetId's position */
      var ids = (rackState.units || []).map(function(u){ return u.id; });
      var srcIdx = ids.indexOf(dragSrcId);
      var tgtIdx = ids.indexOf(targetId);
      if (srcIdx < 0 || tgtIdx < 0) return;
      ids.splice(srcIdx, 1);
      ids.splice(tgtIdx, 0, dragSrcId);
      mc1Api('PUT', '/api/v1/effects/global/reorder', {order:ids}).then(function(d) {
        if (d && d.ok) { mc1Toast('Reordered', 'ok'); loadRack(); }
        else mc1Toast('Reorder failed', 'err');
      });
    });
  });
}

/* ── Update status bar ────────────────────────────────────────────────────── */
function updateStatus() {
  var units = rackState.units || [];
  document.getElementById('fx-count').textContent = units.length;
  document.getElementById('fx-mode').textContent = 'Global';
  var bypass = rackState.bypass;
  document.getElementById('fx-state').textContent = bypass ? 'Bypassed' : 'Active';
  document.getElementById('fx-state').style.color = bypass ? 'var(--orange)' : 'var(--teal)';
  document.getElementById('fx-bypass').classList.toggle('active', bypass);
}
</script>

<script>
/* ── PTT (Push-to-Talk) controls ──────────────────────────────────────────── */
var pttActive = false;

function pttDown() {
  if (pttActive) return;
  pttActive = true;
  document.getElementById('ptt-btn').classList.add('active');
  document.getElementById('ptt-state').textContent = 'ACTIVE';
  document.getElementById('ptt-state').style.color = 'var(--red)';
  mc1Api('POST', '/api/v1/ptt/activate').catch(function(){});
}

function pttUp() {
  if (!pttActive) return;
  pttActive = false;
  document.getElementById('ptt-btn').classList.remove('active');
  document.getElementById('ptt-state').textContent = 'Inactive';
  document.getElementById('ptt-state').style.color = 'var(--muted)';
  mc1Api('POST', '/api/v1/ptt/deactivate').catch(function(){});
}

function updatePttConfig() {
  mc1Api('PUT', '/api/v1/ptt/config', {
    duck_amount_db: parseFloat(document.getElementById('ptt-duck').value),
    attack_ms:      parseFloat(document.getElementById('ptt-attack').value),
    release_ms:     parseFloat(document.getElementById('ptt-release').value)
  }).catch(function(){});
}

function pollPttStatus() {
  mc1Api('GET', '/api/v1/ptt/status').then(function(d) {
    if (!d || !d.ok) return;
    var duck = Math.abs(d.current_duck_db || 0);
    var pct = Math.min(100, (duck / 30) * 100);
    document.getElementById('ptt-meter-fill').style.height = pct + '%';
    /* We sync UI if PTT was toggled externally */
    if (d.ptt_active && !pttActive) {
      pttActive = true;
      document.getElementById('ptt-btn').classList.add('active');
      document.getElementById('ptt-state').textContent = 'ACTIVE';
      document.getElementById('ptt-state').style.color = 'var(--red)';
    } else if (!d.ptt_active && pttActive) {
      pttActive = false;
      document.getElementById('ptt-btn').classList.remove('active');
      document.getElementById('ptt-state').textContent = 'Inactive';
      document.getElementById('ptt-state').style.color = 'var(--muted)';
    }
  }).catch(function(){});
}

function loadPttConfig() {
  mc1Api('GET', '/api/v1/ptt/status').then(function(d) {
    if (!d || !d.ok || !d.config) return;
    var c = d.config;
    document.getElementById('ptt-duck').value    = c.duck_amount_db || -15;
    document.getElementById('ptt-duck-val').textContent = (c.duck_amount_db || -15) + ' dB';
    document.getElementById('ptt-attack').value  = c.attack_ms || 50;
    document.getElementById('ptt-atk-val').textContent  = (c.attack_ms || 50) + ' ms';
    document.getElementById('ptt-release').value = c.release_ms || 500;
    document.getElementById('ptt-rel-val').textContent  = (c.release_ms || 500) + ' ms';
  }).catch(function(){});
}

document.addEventListener('DOMContentLoaded', function() {
  loadRack();
  loadTypes();
  loadPttConfig();
  /* We poll the rack state every 5 seconds for live parameter updates */
  setInterval(loadRack, 5000);
  /* We poll PTT status every second for duck meter */
  setInterval(pollPttStatus, 1000);
  /* We handle spacebar for PTT */
  document.addEventListener('keydown', function(e) {
    if (e.code === 'Space' && !e.repeat && e.target.tagName !== 'INPUT' && e.target.tagName !== 'SELECT') {
      e.preventDefault();
      pttDown();
    }
  });
  document.addEventListener('keyup', function(e) {
    if (e.code === 'Space' && e.target.tagName !== 'INPUT' && e.target.tagName !== 'SELECT') {
      e.preventDefault();
      pttUp();
    }
  });
});
</script>

<?php require_once __DIR__ . '/app/inc/footer.php'; ?>
