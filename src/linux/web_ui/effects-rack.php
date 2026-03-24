<?php
/**
 * effects-rack.php -- Visual Pedalboard Effects Rack Editor
 *
 * File:    src/linux/web_ui/effects-rack.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Purpose: We provide a visual pedalboard with draggable broadcast-style rack
 *          unit pedals, SVG faceplates, bezier cable routing, and slide-out
 *          config panels. Replaces the flat signal chain with a 2D surface.
 *
 * Standards:
 *  - No exit()/die() -- uopz extension is active
 *  - DOMContentLoaded wraps all mc1Api() calls
 *  - Browser JS calls /api/v1/ directly (no PHP->C++ loopback)
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/app/inc/auth.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/user_auth.php';

if (!mc1_is_authed()) {
    http_response_code(302);
    header('Location: /login');
    return;
}

$page_title = 'Effects Rack';
$active_nav = 'effects';

require_once __DIR__ . '/app/inc/header.php';
?>

<style>
/* ══════════════════════════════════════════════════════════════════════════
 * Pedalboard Page Layout
 * ══════════════════════════════════════════════════════════════════════════ */
.pb-page { padding:0; display:flex; flex-direction:column; height:100%; }

.pb-toolbar {
    display:flex; align-items:center; gap:10px; padding:12px 20px;
    background:var(--card); border-bottom:1px solid var(--border);
    flex-wrap:wrap; flex-shrink:0;
}
.pb-toolbar h2 { margin:0; font-size:1.1rem; color:var(--text); display:flex; align-items:center; gap:8px; }
.pb-toolbar-spacer { flex:1; }
.pb-slot-select {
    padding:6px 12px; background:rgba(255,255,255,.05); border:1px solid var(--border);
    border-radius:var(--radius-sm); color:var(--text); font-size:.85rem;
}
.pb-slot-select option { background:var(--card); color:var(--text); }

/* ── Pedalboard Surface ──────────────────────────────────────────────── */
.pb-surface {
    flex:1; min-height:600px; position:relative; overflow:auto;
    background:var(--bg);
    border:1px solid var(--border); margin:0;
}

/* ── Pedal elements ──────────────────────────────────────────────────── */
.pb-pedal {
    position:absolute; cursor:grab; z-index:10;
    border:2px solid transparent; border-radius:6px;
    transition: border-color .2s, box-shadow .2s;
    user-select:none; -webkit-user-select:none;
}
.pb-pedal:hover { border-color:rgba(20,184,166,.5); box-shadow:0 4px 20px rgba(20,184,166,.12); }
.pb-pedal.dragging { cursor:grabbing; opacity:.85; z-index:100 !important; box-shadow:0 8px 32px rgba(0,0,0,.4); }
.pb-pedal.disabled { opacity:.45; }
.pb-pedal.disabled:hover { border-color:rgba(100,116,139,.4); }

.pb-pedal-svg { pointer-events:none; }
.pb-pedal-svg svg { width:100%; height:100%; display:block; }

.pb-pedal-overlay {
    position:absolute; top:0; left:0; right:0; bottom:0;
    display:flex; flex-direction:column; align-items:center; justify-content:flex-end;
    opacity:0; transition:opacity .2s; pointer-events:none;
    background:rgba(15,23,42,.4); border-radius:4px;
}
.pb-pedal:hover .pb-pedal-overlay { opacity:1; pointer-events:auto; }

.pb-pedal-controls {
    display:flex; gap:6px; margin-bottom:6px;
}
.pb-pedal-controls button {
    width:28px; height:28px; border-radius:6px;
    background:rgba(30,41,59,.9); border:1px solid var(--border);
    color:var(--text-dim); font-size:12px; cursor:pointer;
    display:flex; align-items:center; justify-content:center;
    transition:all .15s;
}
.pb-pedal-controls button:hover { border-color:var(--teal); color:var(--teal); }
.pb-btn-remove:hover { border-color:var(--red) !important; color:var(--red) !important; }
.pb-pedal-name {
    font-size:9px; color:var(--text-dim); background:rgba(30,41,59,.85);
    padding:2px 8px; border-radius:3px; margin-bottom:4px;
    white-space:nowrap; overflow:hidden; text-overflow:ellipsis; max-width:90%;
}

/* ── Meter canvas overlay on pedals ──────────────────────────────────── */
.pb-meter-canvas {
    position:absolute; bottom:2px; left:50%; transform:translateX(-50%);
    pointer-events:none; z-index:15;
    image-rendering:crisp-edges;
}

/* ── Connectors ──────────────────────────────────────────────────────── */
.pb-connector {
    position:absolute; top:50%; width:12px; height:12px;
    border-radius:50%; background:#1a2030; border:2px solid var(--teal);
    transform:translateY(-50%); pointer-events:auto; cursor:crosshair;
    z-index:20; transition:all .15s;
}
.pb-connector:hover {
    background:var(--teal); border-color:#fff; transform:translateY(-50%) scale(1.3);
    box-shadow:0 0 8px var(--teal);
}
.pb-conn-in { left:-6px; }
.pb-conn-out { right:-6px; }

/* ── Fixed pseudo-pedals (input/output) ────────────────────────────────── */
.pb-pedal-fixed {
    cursor:default !important; border:2px solid rgba(30,58,106,.6) !important;
    border-radius:8px;
}
.pb-pedal-fixed:hover {
    border-color:rgba(30,58,106,.9) !important;
    box-shadow:0 2px 12px rgba(30,58,106,.2) !important;
}

/* ── Cable drawing mode ────────────────────────────────────────────────── */
.pb-cabling-active { cursor:crosshair !important; }
.pb-cabling-active .pb-pedal { pointer-events:none; }
.pb-cabling-active .pb-connector { pointer-events:auto !important; }
.pb-cabling-source { border-color:var(--teal) !important; box-shadow:0 0 16px rgba(20,184,166,.3) !important; }

/* ── Cable flow animation ──────────────────────────────────────────────── */
@keyframes pb-cable-flow {
    from { stroke-dashoffset: 18; }
    to { stroke-dashoffset: 0; }
}
.pb-cable-path { animation: pb-cable-flow 0.6s linear infinite; }

/* ── Cable midpoint disconnect button ──────────────────────────────────── */
.pb-cable-midpoint { transition:all .15s; }
.pb-cable-midpoint:hover {
    r:8; stroke:#ef4444 !important; stroke-width:2 !important;
    fill:#1e293b;
}

/* ── Add Effect Drawer ───────────────────────────────────────────────── */
.pb-drawer {
    display:none; background:var(--card); border:1px solid var(--border);
    border-radius:var(--radius); padding:16px 20px; margin:12px 20px 0;
    flex-shrink:0;
}
.pb-drawer.open { display:block; }
.pb-drawer h3 { margin:0 0 12px; color:var(--text); font-size:.95rem; }
.pb-drawer-grid {
    display:grid; grid-template-columns:repeat(auto-fill, minmax(160px, 1fr)); gap:10px;
}
.pb-type-card {
    background:var(--bg3); border:1px solid var(--border); border-radius:var(--radius-sm);
    padding:12px; cursor:pointer; text-align:center; transition:border-color .2s;
}
.pb-type-card:hover { border-color:var(--teal); }
.pb-type-card h4 { margin:4px 0 2px; font-size:.8rem; color:var(--text); }
.pb-type-card p { margin:0; font-size:.65rem; color:var(--muted); line-height:1.3; }
.pb-type-card .pb-type-stub { color:var(--orange); font-size:.6rem; font-weight:700; }

/* ── PTT Section ─────────────────────────────────────────────────────── */
.pb-ptt {
    background:var(--card); border:1px solid var(--border);
    border-radius:var(--radius); padding:16px 20px; margin:12px 20px;
    flex-shrink:0;
}
.pb-ptt-row { display:flex; align-items:center; gap:16px; flex-wrap:wrap; }
.ptt-button {
    width:64px; height:64px; border-radius:50%; border:3px solid var(--border);
    background:var(--bg3); color:var(--text-dim); font-size:.85rem; font-weight:700;
    cursor:pointer; display:flex; flex-direction:column; align-items:center;
    justify-content:center; gap:2px; transition:all .15s; user-select:none;
}
.ptt-button:hover { border-color:var(--teal); color:var(--teal); }
.ptt-button.active { background:var(--red); border-color:var(--red); color:#fff; box-shadow:0 0 20px rgba(239,68,68,.4); transform:scale(1.05); }
.ptt-button i { font-size:1.2rem; }

/* ── NLP Command Bar ──────────────────────────────────────────────────── */
.pb-nlp-bar {
    display:flex; align-items:center; gap:10px; padding:10px 20px;
    background:var(--card); border-top:1px solid var(--border);
    flex-shrink:0;
}
.pb-nlp-input {
    flex:1; padding:8px 12px; background:rgba(255,255,255,.05);
    border:1px solid var(--border); border-radius:var(--radius-sm);
    color:var(--text); font-size:.85rem; outline:none;
    transition:border-color .2s;
}
.pb-nlp-input:focus { border-color:var(--teal); }
.pb-nlp-input::placeholder { color:var(--muted); }
.pb-nlp-send { flex-shrink:0; }

/* ── Accent button (AI) ──────────────────────────────────────────────── */
.btn-accent {
    background:linear-gradient(135deg, rgba(20,184,166,.2), rgba(99,102,241,.2));
    border:1px solid rgba(20,184,166,.4); color:var(--teal);
}
.btn-accent:hover {
    background:linear-gradient(135deg, rgba(20,184,166,.35), rgba(99,102,241,.35));
    border-color:var(--teal); color:#fff;
}

/* ── AI Suggest Results ──────────────────────────────────────────────── */
.ai-chain-unit {
    display:flex; align-items:center; gap:10px; padding:8px 12px;
    background:rgba(255,255,255,.03); border:1px solid rgba(51,65,85,.4);
    border-radius:var(--radius-sm);
}
.ai-chain-unit-type {
    font-weight:700; color:var(--teal); font-size:.8rem;
    text-transform:uppercase; min-width:90px;
}
.ai-chain-unit-params {
    font-size:.75rem; color:var(--text-dim);
    font-family:'SF Mono','Fira Code',monospace;
}

/* ── Status Bar ──────────────────────────────────────────────────────── */
.pb-status {
    display:flex; align-items:center; gap:20px; padding:8px 20px;
    background:var(--card); border-top:1px solid var(--border);
    flex-shrink:0; font-size:.8rem;
}
.pb-status-label { color:var(--text-dim); }
.pb-status-val { color:var(--teal); font-family:'SF Mono',monospace; }

/* ══════════════════════════════════════════════════════════════════════════
 * Config Panel Styles (pedal-configs.js)
 * ══════════════════════════════════════════════════════════════════════════ */
.pc-backdrop {
    position:fixed; top:0; left:0; right:0; bottom:0; z-index:500;
    background:rgba(0,0,0,.4); opacity:0; transition:opacity .25s;
    pointer-events:none;
}
.pc-backdrop.open { opacity:1; pointer-events:auto; }

.pc-panel {
    position:fixed; top:0; right:-420px; bottom:0; width:400px; z-index:510;
    background:var(--bg2); border-left:1px solid var(--border);
    box-shadow:-4px 0 30px rgba(0,0,0,.4); transition:right .3s ease;
    display:flex; flex-direction:column; overflow:hidden;
}
.pc-panel.open { right:0; }

.pc-panel-header {
    display:flex; align-items:center; justify-content:space-between;
    padding:14px 18px; border-bottom:1px solid var(--border);
    background:var(--card); flex-shrink:0;
}
.pc-panel-title { display:flex; align-items:center; gap:10px; }
.pc-panel-name { font-size:1rem; font-weight:700; color:var(--text); }
.pc-panel-ver { font-size:.7rem; color:var(--teal); background:rgba(20,184,166,.12); padding:2px 8px; border-radius:10px; }
.pc-panel-close {
    background:none; border:none; color:var(--text-dim); font-size:16px;
    cursor:pointer; padding:4px 8px;
}
.pc-panel-close:hover { color:var(--text); }

.pc-panel-body {
    flex:1; overflow-y:auto; padding:18px;
    display:flex; flex-direction:column; gap:16px;
}

/* Knobs */
.pc-knob-wrap { display:flex; flex-direction:column; align-items:center; gap:4px; min-width:70px; }
.pc-knob-label { font-size:.65rem; font-weight:700; color:var(--muted); text-transform:uppercase; letter-spacing:.05em; }
.pc-knob {
    width:56px; height:56px; cursor:pointer;
    display:flex; align-items:center; justify-content:center;
}
.pc-knob-body {
    width:48px; height:48px; border-radius:50%;
    background:radial-gradient(circle at 35% 35%, #3a4a5a, #252e3e 60%, #1a2030);
    border:2px solid #3a4a5a; position:relative;
    box-shadow: inset 0 2px 4px rgba(0,0,0,.3), 0 2px 8px rgba(0,0,0,.2);
}
.pc-knob-indicator {
    position:absolute; top:4px; left:50%; width:2px; height:12px;
    background:var(--teal); border-radius:1px; transform:translateX(-50%);
    box-shadow:0 0 4px var(--teal);
}
.pc-knob-value { font-size:.7rem; color:var(--teal); font-family:'SF Mono',monospace; }
.pc-knob-row { display:flex; flex-wrap:wrap; gap:16px; justify-content:center; }

/* Vertical sliders */
.pc-vslider-wrap { display:flex; flex-direction:column; align-items:center; gap:2px; min-width:36px; }
.pc-vslider-label { font-size:.55rem; font-weight:700; color:var(--muted); text-transform:uppercase; }
.pc-vslider {
    -webkit-appearance:slider-vertical; writing-mode:bt-lr;
    width:20px; height:100px; background:var(--border); outline:none;
}
.pc-vslider-value { font-size:.6rem; color:var(--teal); font-family:'SF Mono',monospace; }

/* EQ band grid */
.pc-eq-bands { display:flex; gap:4px; justify-content:center; flex-wrap:wrap; padding:8px 0; }
.pc-eq-q-row { display:grid; grid-template-columns:repeat(5,1fr); gap:6px; }

/* Toggles */
.pc-toggle-wrap { display:flex; align-items:center; gap:10px; }
.pc-toggle-label { font-size:.85rem; color:var(--text-dim); }
.pc-toggle { position:relative; display:inline-block; width:36px; height:20px; flex-shrink:0; }
.pc-toggle input { opacity:0; width:0; height:0; }
.pc-toggle-slider {
    position:absolute; inset:0; background:var(--border); border-radius:10px;
    cursor:pointer; transition:.2s;
}
.pc-toggle-slider::before {
    content:''; position:absolute; height:14px; width:14px; left:3px; bottom:3px;
    background:#fff; border-radius:50%; transition:.2s;
}
.pc-toggle input:checked + .pc-toggle-slider { background:var(--teal); }
.pc-toggle input:checked + .pc-toggle-slider::before { transform:translateX(16px); }

/* Select */
.pc-select-wrap { display:flex; flex-direction:column; gap:4px; }
.pc-select-label { font-size:.7rem; font-weight:700; color:var(--muted); text-transform:uppercase; }
.pc-select {
    width:100%; padding:7px 10px; background:rgba(255,255,255,.05);
    border:1px solid var(--border); border-radius:var(--radius-sm);
    color:var(--text); font-size:.85rem;
}
.pc-select option { background:var(--card); }

/* Horizontal slider */
.pc-hslider-wrap { display:flex; flex-direction:column; gap:4px; }
.pc-hslider-label { font-size:.7rem; font-weight:700; color:var(--muted); text-transform:uppercase; }
.pc-hslider-value { color:var(--teal); font-family:'SF Mono',monospace; }
.pc-hslider { width:100%; }

/* Section label */
.pc-section-label { font-size:.75rem; font-weight:700; color:var(--text-dim); margin-top:8px; padding-bottom:4px; border-bottom:1px solid var(--border); }

/* Stub note */
.pc-stub-note { font-size:.8rem; color:var(--orange); background:rgba(249,115,22,.08); border:1px solid rgba(249,115,22,.2); border-radius:var(--radius-sm); padding:10px; }

/* ══════════════════════════════════════════════════════════════════════════
 * Version Info Modal
 * ══════════════════════════════════════════════════════════════════════════ */
.pb-modal-overlay {
    position:fixed; top:0; left:0; right:0; bottom:0; z-index:600;
    background:rgba(0,0,0,.5); display:flex; align-items:center; justify-content:center;
}
.pb-modal {
    background:var(--card); border:1px solid var(--border); border-radius:var(--radius);
    width:420px; max-width:90vw; max-height:80vh; overflow-y:auto;
}
.pb-modal-header {
    display:flex; align-items:center; justify-content:space-between;
    padding:14px 18px; border-bottom:1px solid var(--border);
}
.pb-modal-header h3 { margin:0; font-size:1rem; color:var(--text); }
.pb-modal-header button { background:none; border:none; color:var(--text-dim); font-size:16px; cursor:pointer; }
.pb-modal-body { padding:16px 18px; }
.pb-modal-row { display:flex; justify-content:space-between; align-items:center; padding:6px 0; border-bottom:1px solid rgba(51,65,85,.3); }
.pb-modal-label { font-size:.75rem; font-weight:700; color:var(--muted); text-transform:uppercase; }
.pb-modal-desc { margin-top:10px; }
.pb-modal-desc p { font-size:.85rem; color:var(--text-dim); line-height:1.5; margin:4px 0 0; }
</style>

<div class="pb-page">

  <!-- Toolbar -->
  <div class="pb-toolbar">
    <h2><i class="fa-solid fa-sliders" style="color:var(--teal)"></i> Effects Rack</h2>
    <div class="pb-toolbar-spacer"></div>

    <select class="pb-slot-select" id="pb-slot-sel" onchange="switchSlot()">
      <option value="">Global Rack</option>
    </select>

    <button class="btn btn-sm btn-secondary" onclick="toggleProfilePanel()" title="Effect profiles library">
      <i class="fa-solid fa-folder-open"></i> Profiles
    </button>
    <button class="btn btn-sm btn-primary" onclick="toggleDrawer()">
      <i class="fa-solid fa-plus"></i> Add Effect
    </button>
    <button class="btn btn-sm btn-accent" onclick="openAiSuggest()" title="AI-powered effects chain suggestion">
      <i class="fa-solid fa-wand-magic-sparkles"></i> AI Suggest
    </button>
    <button class="btn btn-sm btn-secondary" onclick="pbBoard.resetLayout()">
      <i class="fa-solid fa-arrows-rotate"></i> Reset Layout
    </button>
    <button class="btn btn-sm btn-secondary" onclick="pbBoard.saveLayout(); mc1Toast('Layout saved','ok')">
      <i class="fa-solid fa-floppy-disk"></i> Save Layout
    </button>
    <div class="toggle-wrap" style="margin-left:8px">
      <label class="toggle">
        <input type="checkbox" id="pb-bypass" onchange="toggleBypass(this.checked)">
        <span class="toggle-slider"></span>
      </label>
      <span style="font-size:.8rem;color:var(--text-dim)">Bypass</span>
    </div>
  </div>

  <!-- Add Effect Drawer -->
  <div class="pb-drawer" id="pb-drawer">
    <h3>Add Effect Unit</h3>
    <div class="pb-drawer-grid" id="pb-types"></div>
  </div>

  <!-- Pedalboard Surface -->
  <div class="pb-surface" id="pb-surface"></div>

  <!-- PTT Section -->
  <div class="pb-ptt">
    <div class="pb-ptt-row">
      <div style="display:flex;align-items:center;gap:12px">
        <h3 style="margin:0;color:var(--text);font-size:.95rem"><i class="fa-solid fa-microphone" style="color:var(--teal);margin-right:4px"></i>Push-to-Talk</h3>
        <button id="ptt-btn" class="ptt-button" onmousedown="pttDown()" onmouseup="pttUp()" ontouchstart="pttDown()" ontouchend="pttUp()">
          <i class="fa-solid fa-microphone"></i> PTT
        </button>
        <span id="ptt-state" style="font-size:.75rem;color:var(--muted)">Inactive</span>
      </div>
      <div style="display:flex;align-items:center;gap:14px;flex-wrap:wrap;margin-left:auto">
        <div style="min-width:100px">
          <label style="display:block;font-size:.65rem;color:var(--text-dim);margin-bottom:2px">Duck <span id="ptt-duck-val" style="color:var(--teal);font-family:monospace">-15 dB</span></label>
          <input type="range" id="ptt-duck" min="-30" max="-3" step="1" value="-15" style="width:100px" oninput="document.getElementById('ptt-duck-val').textContent=this.value+' dB'" onchange="updatePttConfig()">
        </div>
        <div style="min-width:80px">
          <label style="display:block;font-size:.65rem;color:var(--text-dim);margin-bottom:2px">Attack <span id="ptt-atk-val" style="color:var(--teal);font-family:monospace">50 ms</span></label>
          <input type="range" id="ptt-attack" min="5" max="200" step="5" value="50" style="width:80px" oninput="document.getElementById('ptt-atk-val').textContent=this.value+' ms'" onchange="updatePttConfig()">
        </div>
        <div style="min-width:80px">
          <label style="display:block;font-size:.65rem;color:var(--text-dim);margin-bottom:2px">Release <span id="ptt-rel-val" style="color:var(--teal);font-family:monospace">500 ms</span></label>
          <input type="range" id="ptt-release" min="50" max="2000" step="50" value="500" style="width:80px" oninput="document.getElementById('ptt-rel-val').textContent=this.value+' ms'" onchange="updatePttConfig()">
        </div>
        <div style="text-align:center;min-width:30px">
          <div style="font-size:.6rem;color:var(--text-dim);margin-bottom:2px">Duck</div>
          <div id="ptt-meter" style="width:14px;height:40px;background:var(--bg);border:1px solid var(--border);border-radius:2px;margin:0 auto;position:relative;overflow:hidden">
            <div id="ptt-meter-fill" style="position:absolute;bottom:0;left:0;right:0;height:0;background:linear-gradient(to top,var(--teal),var(--cyan));transition:height .1s"></div>
          </div>
        </div>
      </div>
    </div>
    <p style="margin:6px 0 0;font-size:.65rem;color:var(--muted)">Hold PTT or press <kbd style="background:var(--bg3);border:1px solid var(--border);padding:1px 5px;border-radius:2px;font-size:.65rem">Space</kbd> to duck music.</p>
  </div>

  <!-- NLP Command Input Bar (persistent at bottom of pedalboard) -->
  <div class="pb-nlp-bar">
    <i class="fa-solid fa-robot" style="color:var(--teal);font-size:1rem;flex-shrink:0"></i>
    <input type="text" id="pb-nlp-input" class="pb-nlp-input"
           placeholder="Type a command... e.g., 'make my voice warmer' or 'add more bass'"
           autocomplete="off">
    <button class="btn btn-sm btn-primary pb-nlp-send" id="pb-nlp-send" onclick="sendNlpCommand()">
      <i class="fa-solid fa-paper-plane"></i> Send
    </button>
  </div>

  <!-- Status Bar -->
  <div class="pb-status">
    <span class="pb-status-label">Units: <span class="pb-status-val" id="pb-count">0</span></span>
    <span class="pb-status-label">Mode: <span class="pb-status-val" id="pb-mode">Global</span></span>
    <span class="pb-status-label">Status: <span class="pb-status-val" id="pb-state">Active</span></span>
  </div>
</div>

<!-- ══════════════════════════════════════════════════════════════════════════
   AI Suggest Chain Modal
   ══════════════════════════════════════════════════════════════════════════ -->
<div id="ai-suggest-overlay" class="pb-modal-overlay" style="display:none">
  <div class="pb-modal" style="width:560px">
    <div class="pb-modal-header">
      <h3><i class="fa-solid fa-wand-magic-sparkles" style="color:var(--teal);margin-right:6px"></i> AI Effects Chain Suggestion</h3>
      <button onclick="closeAiSuggest()">&times;</button>
    </div>
    <div class="pb-modal-body">
      <div style="margin-bottom:14px">
        <label style="font-size:.75rem;font-weight:700;color:var(--muted);text-transform:uppercase;display:block;margin-bottom:6px">
          Describe your audio setup or what you want to achieve
        </label>
        <textarea id="ai-suggest-prompt" rows="3" style="width:100%;background:rgba(255,255,255,.05);border:1px solid var(--border);border-radius:var(--radius-sm);color:var(--text);padding:10px;font-size:.85rem;resize:vertical"
          placeholder="e.g., Podcast with one host, recording in a treated room, need warm professional sound"></textarea>
      </div>

      <div style="margin-bottom:14px">
        <label style="font-size:.7rem;font-weight:700;color:var(--muted);text-transform:uppercase;display:block;margin-bottom:6px">Quick Presets</label>
        <div style="display:flex;gap:8px;flex-wrap:wrap">
          <button class="btn btn-xs btn-secondary ai-preset-btn" onclick="aiPresetFill('Podcast Voice')">Podcast Voice</button>
          <button class="btn btn-xs btn-secondary ai-preset-btn" onclick="aiPresetFill('Music Stream')">Music Stream</button>
          <button class="btn btn-xs btn-secondary ai-preset-btn" onclick="aiPresetFill('Sports Commentary')">Sports Commentary</button>
          <button class="btn btn-xs btn-secondary ai-preset-btn" onclick="aiPresetFill('DJ Set')">DJ Set</button>
          <button class="btn btn-xs btn-secondary ai-preset-btn" onclick="aiPresetFill('Voice + Music Mix')">Voice + Music Mix</button>
        </div>
      </div>

      <button class="btn btn-primary" id="ai-suggest-btn" onclick="runAiSuggest()" style="width:100%;margin-bottom:14px">
        <i class="fa-solid fa-wand-magic-sparkles"></i> Suggest Effects Chain
      </button>

      <!-- Results panel -->
      <div id="ai-suggest-results" style="display:none">
        <div style="font-size:.75rem;font-weight:700;color:var(--muted);text-transform:uppercase;margin-bottom:8px">Suggested Chain</div>
        <div id="ai-suggest-chain-list" style="display:flex;flex-direction:column;gap:6px;margin-bottom:10px"></div>
        <div id="ai-suggest-rationale" style="font-size:.8rem;color:var(--text-dim);margin-bottom:12px;padding:8px;background:rgba(255,255,255,.03);border-radius:var(--radius-sm);border:1px solid rgba(51,65,85,.3)"></div>
        <div style="display:flex;gap:8px">
          <button class="btn btn-success" id="ai-apply-btn" onclick="applyAiSuggestion()" style="flex:1">
            <i class="fa-solid fa-check"></i> Apply Chain
          </button>
          <button class="btn btn-secondary" onclick="closeAiSuggest()" style="flex:1">
            <i class="fa-solid fa-xmark"></i> Dismiss
          </button>
        </div>
      </div>
    </div>
  </div>
</div>

<!-- ══════════════════════════════════════════════════════════════════════════
   Effect Profiles Slide-Out Panel
   ══════════════════════════════════════════════════════════════════════════ -->
<div id="ep-backdrop" class="pc-backdrop" onclick="closeProfilePanel()"></div>
<div id="ep-panel" class="pc-panel" style="width:460px;right:-480px">
  <div class="pc-panel-header">
    <div class="pc-panel-title">
      <i class="fa-solid fa-folder-open" style="color:var(--teal)"></i>
      <span class="pc-panel-name">Effect Profiles</span>
    </div>
    <button class="pc-panel-close" onclick="closeProfilePanel()">&times;</button>
  </div>
  <div style="padding:10px 18px 0;border-bottom:1px solid var(--border);flex-shrink:0">
    <div style="display:flex;gap:6px;flex-wrap:wrap;margin-bottom:10px">
      <button class="btn btn-xs ep-cat-btn active" data-cat="" onclick="filterProfiles('')">All</button>
      <button class="btn btn-xs ep-cat-btn" data-cat="voice" onclick="filterProfiles('voice')">Voice</button>
      <button class="btn btn-xs ep-cat-btn" data-cat="music" onclick="filterProfiles('music')">Music</button>
      <button class="btn btn-xs ep-cat-btn" data-cat="podcast" onclick="filterProfiles('podcast')">Podcast</button>
      <button class="btn btn-xs ep-cat-btn" data-cat="dj" onclick="filterProfiles('dj')">DJ</button>
      <button class="btn btn-xs ep-cat-btn" data-cat="broadcast" onclick="filterProfiles('broadcast')">Broadcast</button>
      <button class="btn btn-xs ep-cat-btn" data-cat="custom" onclick="filterProfiles('custom')">Custom</button>
    </div>
  </div>
  <div class="pc-panel-body" id="ep-list" style="gap:10px">
    <div style="color:var(--muted);font-size:.85rem;text-align:center;padding:20px">Loading profiles...</div>
  </div>
  <div style="padding:12px 18px;border-top:1px solid var(--border);flex-shrink:0">
    <button class="btn btn-primary btn-sm" style="width:100%" onclick="saveCurrentAsProfile()">
      <i class="fa-solid fa-floppy-disk"></i> Save Current Rack as Profile
    </button>
  </div>
</div>

<style>
/* ── Effect Profile Panel styles ───────────────────────────────────────── */
.ep-cat-btn { background:rgba(255,255,255,.05); border:1px solid var(--border); color:var(--text-dim); font-size:.7rem; padding:4px 10px; border-radius:12px; cursor:pointer; transition:all .15s; }
.ep-cat-btn:hover { border-color:var(--teal); color:var(--teal); }
.ep-cat-btn.active { background:rgba(20,184,166,.15); border-color:var(--teal); color:var(--teal); }

.ep-card {
    background:var(--bg3); border:1px solid var(--border); border-radius:var(--radius-sm);
    padding:12px 14px; position:relative; border-left:3px solid var(--border);
    transition:border-color .2s;
}
.ep-card:hover { border-color:var(--teal); }
.ep-card[data-cat="voice"]     { border-left-color:#8b5cf6; }
.ep-card[data-cat="music"]     { border-left-color:#06b6d4; }
.ep-card[data-cat="podcast"]   { border-left-color:#f59e0b; }
.ep-card[data-cat="dj"]        { border-left-color:#ef4444; }
.ep-card[data-cat="broadcast"] { border-left-color:#22c55e; }
.ep-card[data-cat="custom"]    { border-left-color:#64748b; }

.ep-card-name { font-size:.9rem; font-weight:700; color:var(--text); margin-bottom:2px; }
.ep-card-desc { font-size:.75rem; color:var(--text-dim); margin-bottom:8px; line-height:1.4; }
.ep-card-meta { display:flex; align-items:center; gap:8px; font-size:.65rem; color:var(--muted); margin-bottom:8px; }
.ep-card-badge {
    display:inline-block; padding:1px 8px; border-radius:10px; font-size:.6rem;
    font-weight:700; text-transform:uppercase; letter-spacing:.03em;
}
.ep-card-badge[data-cat="voice"]     { background:rgba(139,92,246,.15); color:#8b5cf6; }
.ep-card-badge[data-cat="music"]     { background:rgba(6,182,212,.15); color:#06b6d4; }
.ep-card-badge[data-cat="podcast"]   { background:rgba(245,158,11,.15); color:#f59e0b; }
.ep-card-badge[data-cat="dj"]        { background:rgba(239,68,68,.15); color:#ef4444; }
.ep-card-badge[data-cat="broadcast"] { background:rgba(34,197,94,.15); color:#22c55e; }
.ep-card-badge[data-cat="custom"]    { background:rgba(100,116,139,.15); color:#64748b; }

.ep-card-actions { display:flex; gap:6px; }
.ep-card-chain { font-size:.65rem; color:var(--muted); font-family:'SF Mono',monospace; margin-bottom:6px; }
</style>

<!-- Pedalboard JS (order matters: svgs first, then configs, then engine) -->
<script src="/js/pedal-svgs.js"></script>
<script src="/js/pedal-configs.js"></script>
<script src="/js/pedalboard.js"></script>

<script>
var pbBoard = new Pedalboard();
var pbVersions = {};
var pbBypass = false;
var pttActive = false;

/* ── Load effects versions from API ──────────────────────────────────── */
function loadVersions() {
    return mc1Api('GET', '/api/v1/effects/versions').then(function(d) {
        if (!d || !d.ok) return;
        var fx = d.effects || [];
        for (var i = 0; i < fx.length; i++) {
            pbVersions[fx[i].type_id] = fx[i];
        }
    }).catch(function(){});
}

/* ── Load rack state and init pedalboard ─────────────────────────────── */
function loadRack() {
    mc1Api('GET', '/api/v1/effects/global').then(function(d) {
        if (!d || !d.ok) return;
        var rack = d.rack || { bypass: false, units: [] };
        pbBypass = rack.bypass || false;
        document.getElementById('pb-bypass').checked = pbBypass;
        updateStatus(rack);

        if (Object.keys(pbBoard.pedals).length === 0) {
            // Prepend fixed pseudo-pedals: Encoder Input and Encoder Output
            var allEffects = [];
            allEffects.push({ id: '__input', type: '__input', enabled: true, params: {} });
            var units = rack.units || [];
            for (var i = 0; i < units.length; i++) allEffects.push(units[i]);
            allEffects.push({ id: '__output', type: '__output', enabled: true, params: {} });
            // Optional Head-End output tap
            allEffects.push({ id: '__headend', type: '__headend', enabled: true, params: {} });

            pbBoard.init('pb-surface', allEffects, pbVersions, null);
        } else {
            pbBoard.refresh(rack.units || []);
        }
    }).catch(function(){});
}

/* ── Load available unit types for drawer ────────────────────────────── */
function loadTypes() {
    mc1Api('GET', '/api/v1/effects/unit-types').then(function(d) {
        if (!d || !d.ok) return;
        var grid = document.getElementById('pb-types');
        grid.innerHTML = '';
        (d.types || []).forEach(function(t) {
            var vi = pbVersions[t.type] || null;
            var svgThumb = window.generatePedalSVG ? generatePedalSVG(t.type, vi) : '';
            var card = document.createElement('div');
            card.className = 'pb-type-card';
            card.innerHTML =
                '<div style="width:120px;height:45px;margin:0 auto;overflow:hidden">' + svgThumb + '</div>' +
                '<h4>' + t.name + '</h4>' +
                '<p>' + (t.description || '').substring(0, 60) + '</p>' +
                (vi && vi.is_stub ? '<div class="pb-type-stub">STUB</div>' : '');
            card.addEventListener('click', function() { addUnit(t.type); });
            grid.appendChild(card);
        });
    }).catch(function(){});
}

/* ── Load encoder slots for selector ─────────────────────────────────── */
function loadSlots() {
    mc1Api('GET', '/api/v1/encoders').then(function(encoders) {
        if (!Array.isArray(encoders)) return;
        var sel = document.getElementById('pb-slot-sel');
        encoders.forEach(function(e) {
            var opt = document.createElement('option');
            opt.value = e.slot || e.id || '';
            opt.textContent = 'Slot ' + (e.slot || e.id) + ' - ' + (e.mount || e.name || '');
            sel.appendChild(opt);
        });
    }).catch(function(){});
}

/* ── Add effect unit ─────────────────────────────────────────────────── */
function addUnit(type) {
    mc1Api('POST', '/api/v1/effects/global/units', { type: type, enabled: true }).then(function(d) {
        if (d && d.ok) {
            mc1Toast('Added ' + type, 'ok');
            loadRack();
            document.getElementById('pb-drawer').classList.remove('open');
        } else {
            mc1Toast((d && d.error) || 'Add failed', 'err');
        }
    }).catch(function() { mc1Toast('API offline', 'err'); });
}

/* ── Toggle bypass ───────────────────────────────────────────────────── */
function toggleBypass(on) {
    mc1Api('PUT', '/api/v1/effects/global', { bypass: on }).then(function(d) {
        if (d && d.ok) {
            pbBypass = on;
            updateStatus(d.rack || { units: Object.keys(pbBoard.pedals).length });
        }
    });
}

/* ── Switch slot ─────────────────────────────────────────────────────── */
function switchSlot() {
    var val = document.getElementById('pb-slot-sel').value;
    pbBoard.slotId = val || null;
    loadRack();
}

/* ── Toggle drawer ───────────────────────────────────────────────────── */
function toggleDrawer() {
    document.getElementById('pb-drawer').classList.toggle('open');
}

/* ── Update status bar ───────────────────────────────────────────────── */
function updateStatus(rack) {
    var units = rack ? (rack.units ? rack.units.length : 0) : Object.keys(pbBoard.pedals).length;
    document.getElementById('pb-count').textContent = units;
    document.getElementById('pb-mode').textContent = pbBoard.slotId ? 'Slot ' + pbBoard.slotId : 'Global';
    document.getElementById('pb-state').textContent = pbBypass ? 'Bypassed' : 'Active';
    document.getElementById('pb-state').style.color = pbBypass ? 'var(--orange)' : 'var(--teal)';
}

/* ══════════════════════════════════════════════════════════════════════════
 * PTT Controls (same as before, carried over)
 * ══════════════════════════════════════════════════════════════════════════ */
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
        document.getElementById('ptt-duck').value = c.duck_amount_db || -15;
        document.getElementById('ptt-duck-val').textContent = (c.duck_amount_db || -15) + ' dB';
        document.getElementById('ptt-attack').value = c.attack_ms || 50;
        document.getElementById('ptt-atk-val').textContent = (c.attack_ms || 50) + ' ms';
        document.getElementById('ptt-release').value = c.release_ms || 500;
        document.getElementById('ptt-rel-val').textContent = (c.release_ms || 500) + ' ms';
    }).catch(function(){});
}

/* ══════════════════════════════════════════════════════════════════════════
 * AI Suggest Chain Modal
 * ══════════════════════════════════════════════════════════════════════════ */
var aiLastSuggestion = null;

function openAiSuggest() {
    document.getElementById('ai-suggest-overlay').style.display = '';
    document.getElementById('ai-suggest-results').style.display = 'none';
    aiLastSuggestion = null;
}

function closeAiSuggest() {
    document.getElementById('ai-suggest-overlay').style.display = 'none';
}

function aiPresetFill(preset) {
    var prompts = {
        'Podcast Voice':       'Single-host podcast in a treated room. Need warm, clear voice with controlled dynamics and no background noise.',
        'Music Stream':        'Music streaming station playing mixed genres. Need consistent loudness, punchy bass, clear highs, and broadcast-ready limiting.',
        'Sports Commentary':   'Live sports commentary with crowd noise. Need aggressive noise gate, heavy compression for excitement, and presence boost for intelligibility.',
        'DJ Set':              'DJ mixing live with turntables. Need tight limiting, bass enhancement, stereo excitement, and smooth transitions.',
        'Voice + Music Mix':   'Voice-over with background music bed. Need voice presence boost, music ducking via sidechain, and final limiting for broadcast.'
    };
    document.getElementById('ai-suggest-prompt').value = prompts[preset] || preset;
}

function escAiHtml(s) {
    return String(s||'').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

function runAiSuggest() {
    var prompt = document.getElementById('ai-suggest-prompt').value.trim();
    if (!prompt) { mc1Toast('Please describe your audio setup', 'warn'); return; }

    var btn = document.getElementById('ai-suggest-btn');
    btn.disabled = true;
    btn.innerHTML = '<div class="spinner" style="width:14px;height:14px;border-width:2px;display:inline-block;vertical-align:middle"></div> Thinking...';

    mc1Api('POST', '/api/v1/ai/suggest-chain', { description: prompt, use_case: prompt })
        .then(function(d) {
            btn.disabled = false;
            btn.innerHTML = '<i class="fa-solid fa-wand-magic-sparkles"></i> Suggest Effects Chain';

            if (!d || !d.ok) {
                mc1Toast('AI suggestion failed: ' + (d && d.error ? d.error : 'unknown'), 'err');
                return;
            }

            var chain = null;
            var rationale = '';

            if (d.suggested_chain) {
                chain = d.suggested_chain.chain || null;
                rationale = d.suggested_chain.rationale || d.rationale || '';
            } else if (d.response) {
                try {
                    var parsed = JSON.parse(d.response);
                    chain = parsed.chain || null;
                    rationale = parsed.rationale || '';
                } catch(e) {
                    var m = d.response.match(/\{[\s\S]*\}/);
                    if (m) {
                        try {
                            var parsed2 = JSON.parse(m[0]);
                            chain = parsed2.chain || null;
                            rationale = parsed2.rationale || '';
                        } catch(e2) {}
                    }
                }
            }

            if (!chain || !Array.isArray(chain)) {
                mc1Toast('AI returned a response but no valid effects chain was found', 'warn');
                document.getElementById('ai-suggest-results').style.display = 'none';
                return;
            }

            aiLastSuggestion = chain;

            var list = document.getElementById('ai-suggest-chain-list');
            list.innerHTML = '';
            chain.forEach(function(unit, idx) {
                var paramStr = '';
                if (unit.params) {
                    var parts = [];
                    for (var k in unit.params) {
                        parts.push(k + ': ' + unit.params[k]);
                    }
                    paramStr = parts.join(', ');
                }
                var div = document.createElement('div');
                div.className = 'ai-chain-unit';
                div.innerHTML = '<span style="color:var(--muted);font-size:.7rem;min-width:20px">' + (idx+1) + '.</span>'
                    + '<span class="ai-chain-unit-type">' + escAiHtml(unit.type || '?') + '</span>'
                    + '<span class="ai-chain-unit-params">' + escAiHtml(paramStr) + '</span>';
                list.appendChild(div);
            });

            document.getElementById('ai-suggest-rationale').textContent = rationale || 'No rationale provided.';
            document.getElementById('ai-suggest-results').style.display = '';
        })
        .catch(function() {
            btn.disabled = false;
            btn.innerHTML = '<i class="fa-solid fa-wand-magic-sparkles"></i> Suggest Effects Chain';
            mc1Toast('AI service unreachable', 'err');
        });
}

function applyAiSuggestion() {
    if (!aiLastSuggestion) { mc1Toast('No suggestion to apply', 'warn'); return; }

    var prompt = document.getElementById('ai-suggest-prompt').value.trim();
    var applyBtn = document.getElementById('ai-apply-btn');
    applyBtn.disabled = true;
    applyBtn.innerHTML = '<div class="spinner" style="width:14px;height:14px;border-width:2px;display:inline-block;vertical-align:middle"></div> Applying...';

    mc1Api('POST', '/api/v1/ai/suggest-chain', { description: prompt, use_case: prompt, apply: true })
        .then(function(d) {
            applyBtn.disabled = false;
            applyBtn.innerHTML = '<i class="fa-solid fa-check"></i> Apply Chain';
            if (d && d.ok && d.applied) {
                mc1Toast('Effects chain applied (' + (d.applied_count || 0) + ' units)', 'ok');
                closeAiSuggest();
                loadRack();
            } else {
                mc1Toast('Failed to apply chain: ' + (d && d.error ? d.error : 'check AI response'), 'err');
            }
        })
        .catch(function() {
            applyBtn.disabled = false;
            applyBtn.innerHTML = '<i class="fa-solid fa-check"></i> Apply Chain';
            mc1Toast('API unreachable', 'err');
        });
}

/* ══════════════════════════════════════════════════════════════════════════
 * NLP Natural Language Command
 * ══════════════════════════════════════════════════════════════════════════ */
function sendNlpCommand() {
    var input = document.getElementById('pb-nlp-input');
    var command = input.value.trim();
    if (!command) return;

    var btn = document.getElementById('pb-nlp-send');
    btn.disabled = true;
    btn.innerHTML = '<div class="spinner" style="width:12px;height:12px;border-width:2px;display:inline-block;vertical-align:middle"></div>';

    mc1Api('POST', '/api/v1/ai/natural-command', { command: command })
        .then(function(d) {
            btn.disabled = false;
            btn.innerHTML = '<i class="fa-solid fa-paper-plane"></i> Send';

            if (!d || !d.ok) {
                mc1Toast('AI error: ' + (d && d.error ? d.error : 'unknown'), 'err');
                return;
            }

            if (d.executed) {
                mc1Toast(d.result || 'Command executed', 'ok');
                input.value = '';
                loadRack();
            } else if (d.parsed_action && d.parsed_action.action === 'clarify') {
                mc1Toast(d.result || d.parsed_action.message || 'Please clarify your command', 'warn');
            } else {
                mc1Toast(d.result || 'Could not execute command', 'warn');
            }
        })
        .catch(function() {
            btn.disabled = false;
            btn.innerHTML = '<i class="fa-solid fa-paper-plane"></i> Send';
            mc1Toast('AI service unreachable', 'err');
        });
}

/* ══════════════════════════════════════════════════════════════════════════
 * Effect Profiles Panel
 * ══════════════════════════════════════════════════════════════════════════ */
var epProfiles = [];
var epFilter = '';

function toggleProfilePanel() {
    var panel = document.getElementById('ep-panel');
    var backdrop = document.getElementById('ep-backdrop');
    var isOpen = panel.classList.contains('open');
    if (isOpen) {
        closeProfilePanel();
    } else {
        panel.classList.add('open');
        panel.style.right = '0';
        backdrop.classList.add('open');
        loadProfiles();
    }
}

function closeProfilePanel() {
    var panel = document.getElementById('ep-panel');
    var backdrop = document.getElementById('ep-backdrop');
    panel.classList.remove('open');
    panel.style.right = '-480px';
    backdrop.classList.remove('open');
}

function filterProfiles(cat) {
    epFilter = cat;
    var btns = document.querySelectorAll('.ep-cat-btn');
    for (var i = 0; i < btns.length; i++) {
        btns[i].classList.toggle('active', btns[i].getAttribute('data-cat') === cat);
    }
    renderProfiles();
}

function loadProfiles() {
    mc1Api('GET', '/api/v1/effects/profiles').then(function(d) {
        if (!d || !d.ok) return;
        epProfiles = d.profiles || [];
        renderProfiles();
    }).catch(function() { mc1Toast('Could not load profiles', 'err'); });
}

function escPhtml(s) {
    return String(s||'').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

function renderProfiles() {
    var list = document.getElementById('ep-list');
    var filtered = epProfiles;
    if (epFilter) {
        filtered = epProfiles.filter(function(p) { return p.category === epFilter; });
    }
    if (filtered.length === 0) {
        list.innerHTML = '<div style="color:var(--muted);font-size:.85rem;text-align:center;padding:20px">No profiles found</div>';
        return;
    }
    var html = '';
    filtered.forEach(function(p) {
        var chain = p.effects_chain_json || [];
        var chainStr = chain.map(function(u) { return u.type || '?'; }).join(' > ');
        var isBuiltin = p.user_id === 0;
        html += '<div class="ep-card" data-cat="' + escPhtml(p.category) + '">'
            + '<div class="ep-card-name">' + escPhtml(p.profile_name) + '</div>'
            + '<div class="ep-card-desc">' + escPhtml(p.description) + '</div>'
            + '<div class="ep-card-chain">' + escPhtml(chainStr) + '</div>'
            + '<div class="ep-card-meta">'
            + '  <span class="ep-card-badge" data-cat="' + escPhtml(p.category) + '">' + escPhtml(p.category) + '</span>'
            + '  <span><i class="fa-solid fa-play"></i> ' + (p.use_count || 0) + ' uses</span>'
            + (isBuiltin ? '  <span><i class="fa-solid fa-lock"></i> Built-in</span>' : '')
            + '</div>'
            + '<div class="ep-card-actions">'
            + '  <button class="btn btn-xs btn-primary" onclick="applyProfile(' + p.id + ')"><i class="fa-solid fa-check"></i> Apply</button>'
            + (!isBuiltin ? '  <button class="btn btn-xs btn-danger" onclick="deleteProfile(' + p.id + ')"><i class="fa-solid fa-trash"></i> Delete</button>' : '')
            + '</div>'
            + '</div>';
    });
    list.innerHTML = html;
}

function applyProfile(id) {
    mc1Api('POST', '/api/v1/effects/profiles/' + id + '/apply').then(function(d) {
        if (d && d.ok) {
            mc1Toast('Profile applied (' + (d.applied_count || 0) + ' effects)', 'ok');
            closeProfilePanel();
            loadRack();
        } else {
            mc1Toast('Apply failed: ' + (d && d.error ? d.error : 'unknown'), 'err');
        }
    }).catch(function() { mc1Toast('API unreachable', 'err'); });
}

function deleteProfile(id) {
    if (!confirm('Delete this profile?')) return;
    mc1Api('DELETE', '/api/v1/effects/profiles/' + id).then(function(d) {
        if (d && d.ok) {
            mc1Toast('Profile deleted', 'ok');
            loadProfiles();
        } else {
            mc1Toast('Delete failed: ' + (d && d.error ? d.error : 'unknown'), 'err');
        }
    }).catch(function() { mc1Toast('API unreachable', 'err'); });
}

function saveCurrentAsProfile() {
    var name = prompt('Profile name:');
    if (!name || !name.trim()) return;
    name = name.trim();

    var catOptions = ['voice','music','podcast','dj','broadcast','custom'];
    var cat = prompt('Category (' + catOptions.join(', ') + '):', 'custom');
    if (!cat || catOptions.indexOf(cat.trim().toLowerCase()) === -1) cat = 'custom';
    cat = cat.trim().toLowerCase();

    var desc = prompt('Description (optional):', '');

    mc1Api('POST', '/api/v1/effects/profiles/save-current', {
        profile_name: name,
        description: desc || '',
        category: cat
    }).then(function(d) {
        if (d && d.ok) {
            mc1Toast('Profile saved: ' + name, 'ok');
            loadProfiles();
        } else {
            mc1Toast('Save failed: ' + (d && d.error ? d.error : 'unknown'), 'err');
        }
    }).catch(function() { mc1Toast('API unreachable', 'err'); });
}

/* ── DOMContentLoaded: init everything ───────────────────────────────── */
document.addEventListener('DOMContentLoaded', function() {
    loadVersions().then(function() {
        loadRack();
        loadTypes();
    });
    loadSlots();
    loadPttConfig();

    setInterval(loadRack, 5000);
    setInterval(pollPttStatus, 1000);

    /* NLP input: Enter key sends command */
    var nlpInput = document.getElementById('pb-nlp-input');
    if (nlpInput) {
        nlpInput.addEventListener('keydown', function(e) {
            if (e.key === 'Enter') {
                e.preventDefault();
                sendNlpCommand();
            }
        });
    }

    /* AI suggest modal: close on overlay click */
    var aiOverlay = document.getElementById('ai-suggest-overlay');
    if (aiOverlay) {
        aiOverlay.addEventListener('click', function(e) {
            if (e.target === aiOverlay) closeAiSuggest();
        });
    }

    document.addEventListener('keydown', function(e) {
        if (e.code === 'Space' && !e.repeat && e.target.tagName !== 'INPUT' && e.target.tagName !== 'SELECT' && e.target.tagName !== 'TEXTAREA') {
            e.preventDefault();
            pttDown();
        }
    });
    document.addEventListener('keyup', function(e) {
        if (e.code === 'Space' && e.target.tagName !== 'INPUT' && e.target.tagName !== 'SELECT' && e.target.tagName !== 'TEXTAREA') {
            e.preventDefault();
            pttUp();
        }
    });
});
</script>

<?php require_once __DIR__ . '/app/inc/footer.php'; ?>
