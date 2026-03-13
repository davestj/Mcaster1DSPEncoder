<?php
/**
 * crossfader.php — DJ Crossfader Curve Visualization & Selector
 *
 * File:    src/linux/web_ui/crossfader.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-25
 * Purpose: We display all 9 crossfade curve algorithms with interactive Canvas
 *          visualizations. Users can select a curve, preview it with a draggable
 *          fader, and apply it to an encoder slot. Launch button for dual-deck
 *          popup player.
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

$page_title = 'DJ Crossfader';
$active_nav = 'crossfader';

/* We load encoder slots for the slot selector dropdown */
$slots = [];
try {
    $pdo = mc1_db('mcaster1_encoder');
    $slots = $pdo->query("SELECT id, slot_id, name, crossfade_curve FROM encoder_configs ORDER BY slot_id")->fetchAll(PDO::FETCH_ASSOC);
} catch (Exception $e) {}

require_once __DIR__ . '/app/inc/header.php';
?>

<style>
.xf-page { padding: 24px; }
.xf-header { display:flex; align-items:center; justify-content:space-between; margin-bottom:24px; }
.xf-header h2 { margin:0; font-size:1.4rem; color:var(--text); }
.btn-teal { background:var(--teal); color:#fff; border:none; padding:10px 20px; border-radius:var(--radius-sm); cursor:pointer; font-weight:600; font-size:.9rem; display:inline-flex; align-items:center; gap:8px; }
.btn-teal:hover { background:var(--teal2); }
.btn-teal i { font-size:1rem; }

/* ── Active Curve Display ──────────────────────────────────── */
.xf-active { background:var(--card); border:1px solid var(--border); border-radius:var(--radius); padding:24px; margin-bottom:24px; display:flex; gap:24px; align-items:flex-start; flex-wrap:wrap; }
.xf-active-graph { flex:1; min-width:360px; }
.xf-active-graph canvas { width:100%; height:280px; border-radius:var(--radius-sm); background:var(--bg); }
.xf-active-info { flex:0 0 280px; }
.xf-active-info h3 { margin:0 0 8px; color:var(--teal); font-size:1.1rem; }
.xf-active-info p { color:var(--text-dim); font-size:.85rem; line-height:1.5; margin:0 0 16px; }
.xf-fader-wrap { margin-top:16px; }
.xf-fader-wrap label { display:block; color:var(--text-dim); font-size:.8rem; margin-bottom:6px; }
.xf-fader { width:100%; -webkit-appearance:none; appearance:none; height:8px; border-radius:4px; background:linear-gradient(to right, var(--teal), var(--cyan)); outline:none; cursor:pointer; }
.xf-fader::-webkit-slider-thumb { -webkit-appearance:none; width:20px; height:20px; border-radius:50%; background:#fff; border:2px solid var(--teal); cursor:grab; }
.xf-fader::-moz-range-thumb { width:20px; height:20px; border-radius:50%; background:#fff; border:2px solid var(--teal); cursor:grab; }
.xf-gains { display:flex; justify-content:space-between; margin-top:8px; font-family:var(--font-mono); font-size:.8rem; }
.xf-gains .ga { color:var(--teal); }
.xf-gains .gb { color:var(--cyan); }

/* ── Slot Assign ───────────────────────────────────────────── */
.xf-assign { display:flex; gap:12px; align-items:center; margin-top:16px; }
.xf-assign select { background:var(--bg3); border:1px solid var(--border); color:var(--text); padding:8px 12px; border-radius:var(--radius-xs); font-size:.85rem; }
.xf-assign button { background:var(--teal); border:none; color:#fff; padding:8px 16px; border-radius:var(--radius-xs); cursor:pointer; font-size:.85rem; }
.xf-assign button:hover { background:var(--teal2); }

/* ── 3x3 Curve Grid ───────────────────────────────────────── */
.xf-grid { display:grid; grid-template-columns:repeat(3, 1fr); gap:16px; }
@media (max-width:900px) { .xf-grid { grid-template-columns:repeat(2, 1fr); } }
@media (max-width:600px) { .xf-grid { grid-template-columns:1fr; } }
.xf-card { background:var(--card); border:1px solid var(--border); border-radius:var(--radius); padding:12px; cursor:pointer; transition:border-color .2s, box-shadow .2s; }
.xf-card:hover { border-color:var(--teal); }
.xf-card.selected { border-color:var(--teal); box-shadow:0 0 12px var(--teal-glow); }
.xf-card canvas { width:100%; height:120px; border-radius:var(--radius-xs); background:var(--bg); margin-bottom:8px; }
.xf-card h4 { margin:0; font-size:.9rem; color:var(--text); }
.xf-card p { margin:4px 0 0; font-size:.75rem; color:var(--muted); line-height:1.3; }
</style>

<div class="xf-page">
  <div class="xf-header">
    <h2><i class="fa-solid fa-arrows-left-right" style="color:var(--teal);margin-right:8px"></i>DJ Crossfader — 9 Curve Algorithms</h2>
    <button class="btn-teal" onclick="window.open('/dualdeck-player.php','mc1dualdeck','width=1400,height=800')">
      <i class="fa-solid fa-headphones"></i> Open Dual-Deck Player
    </button>
  </div>

  <!-- Active Curve Preview -->
  <div class="xf-active">
    <div class="xf-active-graph">
      <canvas id="xf-main-canvas"></canvas>
    </div>
    <div class="xf-active-info">
      <h3 id="xf-name">Constant Power</h3>
      <p id="xf-desc">sin/cos taper — standard DJ crossfader. -3 dB at center, smooth blend.</p>
      <div class="xf-fader-wrap">
        <label>Fader Position: <span id="xf-pos-label">0.50</span></label>
        <input type="range" class="xf-fader" id="xf-fader" min="0" max="1" step="0.005" value="0.5">
      </div>
      <div class="xf-gains">
        <span class="ga">Deck A: <span id="xf-ga-db">-3.0 dB</span></span>
        <span class="gb">Deck B: <span id="xf-gb-db">-3.0 dB</span></span>
      </div>
      <div class="xf-assign">
        <select id="xf-slot">
          <option value="">— Select Slot —</option>
          <?php foreach ($slots as $s): ?>
          <option value="<?= (int)$s['slot_id'] ?>" data-curve="<?= (int)$s['crossfade_curve'] ?>">
            Slot <?= (int)$s['slot_id'] ?>: <?= h($s['name']) ?>
          </option>
          <?php endforeach; ?>
        </select>
        <button onclick="applyToSlot()">Apply Curve</button>
      </div>
    </div>
  </div>

  <!-- 3x3 Curve Grid -->
  <div class="xf-grid" id="xf-grid"></div>
</div>

<script>
/* ══════════════════════════════════════════════════════════════════════════════
 * Crossfader Curve Math — duplicated from C++ crossfader_curves.h
 * All 9 algorithms, client-side for instant interactive scrubbing.
 * ══════════════════════════════════════════════════════════════════════════════ */
var kPi2 = Math.PI / 2;

var CURVES = [
  { id:0, name:'Linear',           desc:'Simple linear gain — A=1-x, B=x. Equal attenuation both sides.',
    fn: function(x){ return {a:1-x, b:x}; } },
  { id:1, name:'Constant Power',   desc:'sin/cos taper — standard DJ crossfader. -3 dB at center, smooth blend.',
    fn: function(x){ return {a:Math.cos(x*kPi2), b:Math.sin(x*kPi2)}; } },
  { id:2, name:'S-Curve',          desc:'Cubic smooth-step — slow at extremes, fast through the center.',
    fn: function(x){ var t=x*x*(3-2*x); return {a:1-t, b:t}; } },
  { id:3, name:'Exponential',      desc:'Squared law (6 dB/octave) — more aggressive than constant power.',
    fn: function(x){ return {a:(1-x)*(1-x), b:x*x}; } },
  { id:4, name:'Log Taper',        desc:'1.5-power law — perceptually linear loudness (matches hearing curve).',
    fn: function(x){ return {a:Math.pow(1-x,1.5), b:Math.pow(x,1.5)}; } },
  { id:5, name:'Broadcast Blend',  desc:'EBU broadcast blend — maintains equal loudness across the entire range.',
    fn: function(x){ return {a:Math.cos(x*kPi2)*0.7071+(1-x)*0.2929, b:Math.sin(x*kPi2)*0.7071+x*0.2929}; } },
  { id:6, name:'Transform Cut',    desc:'Instant cut at 50% — battle DJ and scratching.',
    fn: function(x){ return {a:x<0.5?1:0, b:x>=0.5?1:0}; } },
  { id:7, name:'Hard Cut',         desc:'Sharp 10% overlap — clean live radio hand-off, no pop, no dropout.',
    fn: function(x){
      var lo=0.45,hi=0.55,ov=0.10;
      var a=x<lo?1:x>hi?0:Math.cos(((x-lo)/ov)*kPi2);
      var b=x>hi?1:x<lo?0:Math.sin(((x-lo)/ov)*kPi2);
      return {a:a, b:b};
    }},
  { id:8, name:'Pioneer Style',    desc:'Pioneer DJM style — both decks fully open, only fades the opposite deck.',
    fn: function(x){ return {a:x<0.5?1:Math.cos((x-0.5)*kPi2*2), b:x>0.5?1:Math.cos((0.5-x)*kPi2*2)}; } }
];

var selectedCurve = 1;  // default ConstantPower

function toDb(g) { return g > 1e-6 ? (20*Math.log10(g)).toFixed(1) : '-inf'; }

/* ── Draw a curve on a canvas ─────────────────────────────────────────────── */
function drawCurve(canvas, curveIdx, pos, large) {
  var ctx = canvas.getContext('2d');
  var dpr = window.devicePixelRatio || 1;
  var w = canvas.clientWidth, h = canvas.clientHeight;
  canvas.width = w * dpr; canvas.height = h * dpr;
  ctx.scale(dpr, dpr);

  var fn = CURVES[curveIdx].fn;
  var n = 100;

  /* We clear with background */
  ctx.fillStyle = '#0f172a';
  ctx.fillRect(0, 0, w, h);

  /* We draw grid lines */
  ctx.strokeStyle = 'rgba(51,65,85,0.4)';
  ctx.lineWidth = 1;
  for (var i = 0; i <= 4; i++) {
    var gy = h * i / 4;
    ctx.beginPath(); ctx.moveTo(0, gy); ctx.lineTo(w, gy); ctx.stroke();
  }
  ctx.beginPath(); ctx.moveTo(w/2, 0); ctx.lineTo(w/2, h); ctx.stroke();

  /* We draw Deck A curve (teal) with fill */
  ctx.beginPath();
  for (var i = 0; i <= n; i++) {
    var x = i / n, g = fn(x);
    var px = x * w, py = (1 - g.a) * h;
    i === 0 ? ctx.moveTo(px, py) : ctx.lineTo(px, py);
  }
  ctx.strokeStyle = '#14b8a6'; ctx.lineWidth = large ? 2.5 : 2; ctx.stroke();
  /* We fill under the A curve */
  ctx.lineTo(w, h); ctx.lineTo(0, h); ctx.closePath();
  ctx.fillStyle = 'rgba(20,184,166,0.12)'; ctx.fill();

  /* We draw Deck B curve (cyan) with fill */
  ctx.beginPath();
  for (var i = 0; i <= n; i++) {
    var x = i / n, g = fn(x);
    var px = x * w, py = (1 - g.b) * h;
    i === 0 ? ctx.moveTo(px, py) : ctx.lineTo(px, py);
  }
  ctx.strokeStyle = '#0891b2'; ctx.lineWidth = large ? 2.5 : 2; ctx.stroke();
  ctx.lineTo(w, h); ctx.lineTo(0, h); ctx.closePath();
  ctx.fillStyle = 'rgba(8,145,178,0.10)'; ctx.fill();

  /* We draw position marker if pos is defined */
  if (typeof pos === 'number') {
    var px = pos * w;
    ctx.setLineDash([4, 3]);
    ctx.strokeStyle = 'rgba(255,255,255,0.5)'; ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(px, 0); ctx.lineTo(px, h); ctx.stroke();
    ctx.setLineDash([]);

    /* We draw gain dots */
    var g = fn(pos);
    ctx.fillStyle = '#14b8a6';
    ctx.beginPath(); ctx.arc(px, (1 - g.a) * h, large ? 6 : 4, 0, Math.PI * 2); ctx.fill();
    ctx.fillStyle = '#0891b2';
    ctx.beginPath(); ctx.arc(px, (1 - g.b) * h, large ? 6 : 4, 0, Math.PI * 2); ctx.fill();

    /* We show dB labels on large canvas */
    if (large) {
      ctx.font = '11px Inter, system-ui';
      ctx.fillStyle = '#14b8a6';
      ctx.fillText('A: ' + toDb(g.a) + ' dB', px + 10, (1 - g.a) * h - 4);
      ctx.fillStyle = '#0891b2';
      ctx.fillText('B: ' + toDb(g.b) + ' dB', px + 10, (1 - g.b) * h + 14);
    }
  }

  /* We draw axis labels on large canvas */
  if (large) {
    ctx.font = '10px Inter, system-ui'; ctx.fillStyle = '#64748b';
    ctx.fillText('Deck A', 4, 14);
    ctx.fillText('Deck B', w - 48, 14);
    ctx.fillText('0 dB', 4, h - 4);
  }
}

/* ── Build the 3x3 grid ──────────────────────────────────────────────────── */
function buildGrid() {
  var grid = document.getElementById('xf-grid');
  grid.innerHTML = '';
  for (var i = 0; i < CURVES.length; i++) {
    var c = CURVES[i];
    var card = document.createElement('div');
    card.className = 'xf-card' + (i === selectedCurve ? ' selected' : '');
    card.setAttribute('data-id', i);
    card.innerHTML = '<canvas id="xf-mini-' + i + '"></canvas>' +
      '<h4>' + c.name + '</h4><p>' + c.desc + '</p>';
    card.addEventListener('click', (function(idx){ return function(){ selectCurve(idx); }; })(i));
    grid.appendChild(card);
  }
  /* We draw all mini canvases after DOM insertion */
  requestAnimationFrame(function(){
    for (var i = 0; i < CURVES.length; i++) {
      drawCurve(document.getElementById('xf-mini-' + i), i, undefined, false);
    }
  });
}

/* ── Select a curve ──────────────────────────────────────────────────────── */
function selectCurve(idx) {
  selectedCurve = idx;
  document.querySelectorAll('.xf-card').forEach(function(c){
    c.classList.toggle('selected', parseInt(c.getAttribute('data-id')) === idx);
  });
  document.getElementById('xf-name').textContent = CURVES[idx].name;
  document.getElementById('xf-desc').textContent = CURVES[idx].desc;
  updateMainCanvas();
}

/* ── Update the large preview canvas ─────────────────────────────────────── */
function updateMainCanvas() {
  var pos = parseFloat(document.getElementById('xf-fader').value);
  document.getElementById('xf-pos-label').textContent = pos.toFixed(2);
  drawCurve(document.getElementById('xf-main-canvas'), selectedCurve, pos, true);
  var g = CURVES[selectedCurve].fn(pos);
  document.getElementById('xf-ga-db').textContent = toDb(g.a) + ' dB';
  document.getElementById('xf-gb-db').textContent = toDb(g.b) + ' dB';
}

/* ── Apply curve to encoder slot ─────────────────────────────────────────── */
function applyToSlot() {
  var slotSel = document.getElementById('xf-slot');
  var slotId = slotSel.value;
  if (!slotId) { mc1Toast('Select a slot first', 'warn'); return; }
  mc1Api('PUT', '/api/v1/encoders/' + slotId + '/crossfader', {
    curve: selectedCurve
  }).then(function(d) {
    if (d && d.ok) {
      mc1Toast('Applied ' + CURVES[selectedCurve].name + ' to Slot ' + slotId, 'ok');
    } else {
      mc1Toast((d && d.error) || 'Failed to apply', 'err');
    }
  }).catch(function(){ mc1Toast('API offline', 'err'); });
}
</script>

<script>
document.addEventListener('DOMContentLoaded', function() {
  buildGrid();
  updateMainCanvas();

  /* We update the large canvas when fader is dragged */
  document.getElementById('xf-fader').addEventListener('input', updateMainCanvas);

  /* We handle window resize to redraw canvases */
  var resizeTimer;
  window.addEventListener('resize', function() {
    clearTimeout(resizeTimer);
    resizeTimer = setTimeout(function() {
      buildGrid();
      updateMainCanvas();
    }, 200);
  });

  /* We load current curve from selected slot */
  document.getElementById('xf-slot').addEventListener('change', function() {
    var opt = this.options[this.selectedIndex];
    if (opt && opt.dataset.curve !== undefined) {
      var curveId = parseInt(opt.dataset.curve);
      if (curveId >= 0 && curveId < 9) selectCurve(curveId);
    }
  });
});
</script>

<?php require_once __DIR__ . '/app/inc/footer.php'; ?>
