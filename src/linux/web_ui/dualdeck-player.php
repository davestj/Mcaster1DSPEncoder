<?php
/**
 * dualdeck-player.php — Standalone Dual-Deck DJ Player (Frameless Popup)
 *
 * File:    src/linux/web_ui/dualdeck-player.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-25
 * Purpose: Self-contained dual-deck A/B player with crossfader, PTT, and
 *          per-deck EQ kill switches. Opened via window.open() from
 *          crossfader.php or encoders.php. Does NOT include footer.php —
 *          inlines its own mc1Api, mc1Toast, and mc1State.
 *
 * Standards:
 *  - No exit()/die() — uopz extension is active
 *  - Self-contained: no header.php/footer.php dependencies
 *  - DOMContentLoaded wraps all startup calls
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/app/inc/mc1_config.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/auth.php';
require_once __DIR__ . '/app/inc/user_auth.php';

if (!mc1_is_authed()) { header('Location: /login.html'); return; }

/* We load tracks for the library browser */
$tracks = [];
try {
    $pdo = mc1_db('mcaster1_media');
    $tracks = $pdo->query("SELECT id, title, artist, album, duration_ms FROM tracks ORDER BY artist, title LIMIT 500")->fetchAll(PDO::FETCH_ASSOC);
} catch (Exception $e) {}

/* We load encoder slots for output assignment */
$slots = [];
try {
    $pdo2 = mc1_db('mcaster1_encoder');
    $slots = $pdo2->query("SELECT slot_id, name FROM encoder_configs ORDER BY slot_id")->fetchAll(PDO::FETCH_ASSOC);
} catch (Exception $e) {}
?>
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Mcaster1 — Dual-Deck DJ Player</title>
<link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.5.1/css/all.min.css">
<style>
:root{--bg:#0f172a;--bg2:#1e293b;--bg3:#162032;--card:#1e293b;--border:#334155;--border2:#2d3f55;--teal:#14b8a6;--teal2:#0d9488;--teal-glow:rgba(20,184,166,.18);--cyan:#0891b2;--text:#e2e8f0;--text-dim:#94a3b8;--muted:#64748b;--red:#ef4444;--green:#22c55e;--orange:#f97316;--yellow:#eab308;--radius:10px;--radius-sm:6px;--font:'Inter','Segoe UI',system-ui,sans-serif;--font-mono:'SF Mono','Fira Code',monospace;}
*{margin:0;padding:0;box-sizing:border-box;}
body{background:var(--bg);color:var(--text);font-family:var(--font);font-size:13px;overflow:hidden;height:100vh;display:flex;flex-direction:column;}

/* ── Header ─────────────────────────────────── */
.dd-top{background:var(--bg2);border-bottom:1px solid var(--border);padding:8px 16px;display:flex;align-items:center;justify-content:space-between;}
.dd-top h1{font-size:1rem;color:var(--teal);font-weight:600;}
.dd-top-right{display:flex;align-items:center;gap:12px;}
.dd-top select{background:var(--bg3);border:1px solid var(--border);color:var(--text);padding:4px 8px;border-radius:var(--radius-sm);font-size:.8rem;}

/* ── Main Layout ────────────────────────────── */
.dd-main{flex:1;display:grid;grid-template-columns:1fr auto 1fr;gap:0;overflow:hidden;}

/* ── Deck ───────────────────────────────────── */
.deck{display:flex;flex-direction:column;padding:12px;overflow:hidden;}
.deck-label{font-size:.75rem;font-weight:700;text-transform:uppercase;letter-spacing:1px;margin-bottom:8px;display:flex;align-items:center;gap:8px;}
.deck-a .deck-label{color:var(--teal);}
.deck-b .deck-label{color:var(--cyan);}
.deck-info{background:var(--card);border:1px solid var(--border);border-radius:var(--radius-sm);padding:10px;margin-bottom:8px;min-height:60px;}
.deck-title{font-size:.9rem;font-weight:600;color:var(--text);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;}
.deck-artist{font-size:.8rem;color:var(--text-dim);}
.deck-time{font-family:var(--font-mono);font-size:.75rem;color:var(--muted);margin-top:4px;}
.deck-transport{display:flex;gap:6px;margin-bottom:8px;}
.deck-transport button{flex:1;padding:8px;border:1px solid var(--border);background:var(--bg3);color:var(--text);border-radius:var(--radius-xs);cursor:pointer;font-size:.8rem;}
.deck-transport button:hover{border-color:var(--teal);color:var(--teal);}
.deck-transport button.playing{background:var(--teal);border-color:var(--teal);color:#fff;}
.deck-vol{display:flex;align-items:center;gap:6px;margin-bottom:8px;font-size:.7rem;color:var(--text-dim);}
.deck-vol input{flex:1;height:4px;-webkit-appearance:none;appearance:none;background:var(--border);border-radius:2px;outline:none;}
.deck-vol input::-webkit-slider-thumb{-webkit-appearance:none;width:12px;height:12px;border-radius:50%;background:var(--teal);cursor:pointer;}
.deck-kills{display:flex;gap:4px;margin-bottom:8px;}
.deck-kills button{flex:1;padding:4px;border:1px solid var(--border);background:transparent;color:var(--muted);border-radius:3px;cursor:pointer;font-size:.65rem;font-weight:700;}
.deck-kills button.killed{background:var(--red);border-color:var(--red);color:#fff;}
.deck-library{flex:1;overflow-y:auto;background:var(--card);border:1px solid var(--border);border-radius:var(--radius-sm);}
.deck-library table{width:100%;border-collapse:collapse;}
.deck-library th{position:sticky;top:0;background:var(--bg2);padding:4px 8px;text-align:left;font-size:.65rem;color:var(--muted);text-transform:uppercase;border-bottom:1px solid var(--border);}
.deck-library td{padding:4px 8px;font-size:.75rem;border-bottom:1px solid var(--border2);cursor:pointer;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:150px;}
.deck-library tr:hover td{background:var(--bg3);}
.deck-library tr.loaded td{background:var(--teal-glow);}

/* ── Center Crossfader ──────────────────────── */
.dd-center{width:220px;background:var(--bg2);border-left:1px solid var(--border);border-right:1px solid var(--border);display:flex;flex-direction:column;padding:12px;gap:10px;overflow-y:auto;}
.xf-label{font-size:.7rem;text-transform:uppercase;color:var(--muted);letter-spacing:.5px;text-align:center;}
.xf-canvas-wrap{width:100%;height:160px;}
.xf-canvas-wrap canvas{width:100%;height:100%;border-radius:var(--radius-sm);background:var(--bg);}
.xf-slider{width:100%;height:8px;-webkit-appearance:none;appearance:none;background:linear-gradient(to right,var(--teal),var(--cyan));border-radius:4px;outline:none;cursor:pointer;}
.xf-slider::-webkit-slider-thumb{-webkit-appearance:none;width:22px;height:22px;border-radius:50%;background:#fff;border:3px solid var(--teal);cursor:grab;}
.xf-gains{display:flex;justify-content:space-between;font-family:var(--font-mono);font-size:.7rem;}
.xf-gains .ga{color:var(--teal);}
.xf-gains .gb{color:var(--cyan);}
.xf-curve-sel{width:100%;background:var(--bg3);border:1px solid var(--border);color:var(--text);padding:6px;border-radius:var(--radius-xs);font-size:.75rem;}
.xf-auto{display:flex;gap:6px;align-items:center;}
.xf-auto input{width:50px;background:var(--bg3);border:1px solid var(--border);color:var(--text);padding:4px;border-radius:3px;font-size:.75rem;text-align:center;}
.xf-auto button{flex:1;padding:6px;border:1px solid var(--border);background:var(--bg3);color:var(--text);border-radius:3px;cursor:pointer;font-size:.7rem;}
.xf-auto button:hover{border-color:var(--teal);color:var(--teal);}

/* ── PTT Button ─────────────────────────────── */
.dd-ptt{width:60px;height:60px;border-radius:50%;border:3px solid var(--border);background:var(--bg3);color:var(--text-dim);font-size:.7rem;font-weight:700;cursor:pointer;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:2px;transition:all .15s;margin:0 auto;user-select:none;}
.dd-ptt:hover{border-color:var(--teal);}
.dd-ptt.active{background:var(--red);border-color:var(--red);color:#fff;box-shadow:0 0 16px rgba(239,68,68,.4);}
.dd-ptt i{font-size:1.1rem;}

/* ── Toast ──────────────────────────────────── */
.toast-container{position:fixed;top:12px;right:12px;z-index:9999;}
.toast{padding:8px 16px;border-radius:var(--radius-sm);margin-bottom:6px;font-size:.8rem;animation:fadeIn .2s;}
.toast-ok{background:#1a2e22;border:1px solid rgba(34,197,94,.4);color:var(--green);}
.toast-err{background:#2a1a1a;border:1px solid rgba(239,68,68,.4);color:var(--red);}
.toast-warn{background:#2a2410;border:1px solid rgba(234,179,8,.4);color:var(--yellow);}
@keyframes fadeIn{from{opacity:0;transform:translateY(-8px)}to{opacity:1;transform:none}}
</style>
</head>
<body>

<!-- Header -->
<div class="dd-top">
  <h1><i class="fa-solid fa-headphones"></i> Dual-Deck Player</h1>
  <div class="dd-top-right">
    <label style="font-size:.75rem;color:var(--muted);">Output Slot:</label>
    <select id="dd-slot">
      <option value="">— None —</option>
      <?php foreach ($slots as $s): ?>
      <option value="<?= (int)$s['slot_id'] ?>">Slot <?= (int)$s['slot_id'] ?>: <?= htmlspecialchars($s['name'], ENT_QUOTES) ?></option>
      <?php endforeach; ?>
    </select>
  </div>
</div>

<!-- Main -->
<div class="dd-main">
  <!-- Deck A -->
  <div class="deck deck-a">
    <div class="deck-label"><i class="fa-solid fa-a"></i> Deck A</div>
    <div class="deck-info">
      <div class="deck-title" id="da-title">No track loaded</div>
      <div class="deck-artist" id="da-artist">&nbsp;</div>
      <div class="deck-time" id="da-time">0:00 / 0:00</div>
    </div>
    <div class="deck-transport">
      <button onclick="deckPlay('a')" id="da-play"><i class="fa-solid fa-play"></i></button>
      <button onclick="deckPause('a')"><i class="fa-solid fa-pause"></i></button>
      <button onclick="deckStop('a')"><i class="fa-solid fa-stop"></i></button>
      <button onclick="deckSkip('a')"><i class="fa-solid fa-forward-step"></i></button>
    </div>
    <div class="deck-vol"><i class="fa-solid fa-volume-low"></i><input type="range" min="0" max="1" step="0.01" value="1" oninput="setDeckVol('a',this.value)"><span id="da-vol">100%</span></div>
    <div class="deck-kills">
      <button onclick="toggleKill('a','lo',this)">LO</button>
      <button onclick="toggleKill('a','mid',this)">MID</button>
      <button onclick="toggleKill('a','hi',this)">HI</button>
    </div>
    <div class="deck-library">
      <table><thead><tr><th>Title</th><th>Artist</th><th>Dur</th></tr></thead>
      <tbody id="da-lib">
      <?php foreach ($tracks as $t): ?>
      <tr onclick="loadToDeck('a',<?= (int)$t['id'] ?>,this)" data-id="<?= (int)$t['id'] ?>">
        <td><?= htmlspecialchars($t['title'] ?? '', ENT_QUOTES) ?></td>
        <td><?= htmlspecialchars($t['artist'] ?? '', ENT_QUOTES) ?></td>
        <td><?= isset($t['duration_ms']) ? gmdate('i:s', (int)$t['duration_ms']/1000) : '' ?></td>
      </tr>
      <?php endforeach; ?>
      </tbody></table>
    </div>
  </div>

  <!-- Center: Crossfader -->
  <div class="dd-center">
    <div class="xf-label">Crossfader</div>
    <div class="xf-canvas-wrap"><canvas id="dd-xf-canvas"></canvas></div>
    <input type="range" class="xf-slider" id="dd-xf" min="0" max="1" step="0.005" value="0.5" oninput="onFaderMove()">
    <div class="xf-gains">
      <span class="ga">A: <span id="dd-ga">-3.0</span> dB</span>
      <span class="gb">B: <span id="dd-gb">-3.0</span> dB</span>
    </div>
    <select class="xf-curve-sel" id="dd-curve" onchange="onCurveChange()">
      <option value="0">Linear</option>
      <option value="1" selected>Constant Power</option>
      <option value="2">S-Curve</option>
      <option value="3">Exponential</option>
      <option value="4">Log Taper</option>
      <option value="5">Broadcast Blend</option>
      <option value="6">Transform Cut</option>
      <option value="7">Hard Cut</option>
      <option value="8">Pioneer Style</option>
    </select>
    <div class="xf-label" style="margin-top:8px;">Auto Crossfade</div>
    <div class="xf-auto">
      <input type="number" id="dd-auto-dur" value="5" min="1" max="30" step="0.5" title="Duration (sec)">
      <button onclick="autoXfade(0)">→ A</button>
      <button onclick="autoXfade(1)">→ B</button>
    </div>
    <div class="xf-label" style="margin-top:12px;">PTT</div>
    <button class="dd-ptt" id="dd-ptt" onmousedown="pttOn()" onmouseup="pttOff()" ontouchstart="pttOn()" ontouchend="pttOff()">
      <i class="fa-solid fa-microphone"></i>PTT
    </button>
    <div style="font-size:.6rem;color:var(--muted);text-align:center;">Space = PTT</div>
  </div>

  <!-- Deck B -->
  <div class="deck deck-b">
    <div class="deck-label"><i class="fa-solid fa-b"></i> Deck B</div>
    <div class="deck-info">
      <div class="deck-title" id="db-title">No track loaded</div>
      <div class="deck-artist" id="db-artist">&nbsp;</div>
      <div class="deck-time" id="db-time">0:00 / 0:00</div>
    </div>
    <div class="deck-transport">
      <button onclick="deckPlay('b')" id="db-play"><i class="fa-solid fa-play"></i></button>
      <button onclick="deckPause('b')"><i class="fa-solid fa-pause"></i></button>
      <button onclick="deckStop('b')"><i class="fa-solid fa-stop"></i></button>
      <button onclick="deckSkip('b')"><i class="fa-solid fa-forward-step"></i></button>
    </div>
    <div class="deck-vol"><i class="fa-solid fa-volume-low"></i><input type="range" min="0" max="1" step="0.01" value="1" oninput="setDeckVol('b',this.value)"><span id="db-vol">100%</span></div>
    <div class="deck-kills">
      <button onclick="toggleKill('b','lo',this)">LO</button>
      <button onclick="toggleKill('b','mid',this)">MID</button>
      <button onclick="toggleKill('b','hi',this)">HI</button>
    </div>
    <div class="deck-library">
      <table><thead><tr><th>Title</th><th>Artist</th><th>Dur</th></tr></thead>
      <tbody id="db-lib">
      <?php foreach ($tracks as $t): ?>
      <tr onclick="loadToDeck('b',<?= (int)$t['id'] ?>,this)" data-id="<?= (int)$t['id'] ?>">
        <td><?= htmlspecialchars($t['title'] ?? '', ENT_QUOTES) ?></td>
        <td><?= htmlspecialchars($t['artist'] ?? '', ENT_QUOTES) ?></td>
        <td><?= isset($t['duration_ms']) ? gmdate('i:s', (int)$t['duration_ms']/1000) : '' ?></td>
      </tr>
      <?php endforeach; ?>
      </tbody></table>
    </div>
  </div>
</div>

<div class="toast-container" id="toast-container"></div>

<script>
/* ── Self-contained mc1Api + mc1Toast (same as mediaplayerpro.php pattern) ── */
window.mc1Api = function(method, url, body) {
  var opts = {method:method, headers:{'Content-Type':'application/json'}, credentials:'same-origin'};
  if (body !== undefined && body !== null) opts.body = JSON.stringify(body);
  return fetch(url, opts).then(function(r){ return r.json().then(function(d){ d._status = r.status; return d; }); });
};
window.mc1Toast = function(msg, type) {
  var el = document.createElement('div');
  el.className = 'toast toast-' + (type||'ok');
  el.textContent = msg;
  document.getElementById('toast-container').appendChild(el);
  setTimeout(function(){ el.remove(); }, 3000);
};

/* ── Crossfader curve math (all 9 algorithms) ────────────────────────────── */
var P2 = Math.PI / 2;
var CF = [
  function(x){return{a:1-x,b:x}},
  function(x){return{a:Math.cos(x*P2),b:Math.sin(x*P2)}},
  function(x){var t=x*x*(3-2*x);return{a:1-t,b:t}},
  function(x){return{a:(1-x)*(1-x),b:x*x}},
  function(x){return{a:Math.pow(1-x,1.5),b:Math.pow(x,1.5)}},
  function(x){return{a:Math.cos(x*P2)*.7071+(1-x)*.2929,b:Math.sin(x*P2)*.7071+x*.2929}},
  function(x){return{a:x<.5?1:0,b:x>=.5?1:0}},
  function(x){var l=.45,h=.55,o=.1;return{a:x<l?1:x>h?0:Math.cos(((x-l)/o)*P2),b:x>h?1:x<l?0:Math.sin(((x-l)/o)*P2)}},
  function(x){return{a:x<.5?1:Math.cos((x-.5)*P2*2),b:x>.5?1:Math.cos((.5-x)*P2*2)}}
];
var curCurve = 1;
function toDb(g){return g>1e-6?(20*Math.log10(g)).toFixed(1):'-inf';}

/* ── Audio elements for each deck ─────────────────────────────────────────── */
var audioA = new Audio();
var audioB = new Audio();
audioA.crossOrigin = 'anonymous';
audioB.crossOrigin = 'anonymous';

var deckState = {a:{id:0,playing:false}, b:{id:0,playing:false}};

function loadToDeck(deck, trackId, row) {
  var audio = deck==='a' ? audioA : audioB;
  audio.src = '/app/api/audio.php?id=' + trackId;
  deckState[deck].id = trackId;
  /* We highlight loaded row */
  var tbody = document.getElementById('d'+deck+'-lib');
  tbody.querySelectorAll('tr').forEach(function(r){r.classList.remove('loaded');});
  if (row) row.classList.add('loaded');
  /* We get track info */
  mc1Api('POST', '/app/api/tracks.php', {action:'list', ids:[trackId]}).then(function(d){
    if (!d || !d.data || !d.data[0]) return;
    var t = d.data[0];
    document.getElementById('d'+deck+'-title').textContent = t.title || 'Unknown';
    document.getElementById('d'+deck+'-artist').textContent = t.artist || '';
  }).catch(function(){});
  mc1Toast('Loaded to Deck ' + deck.toUpperCase(), 'ok');
}

function deckPlay(deck) {
  var audio = deck==='a' ? audioA : audioB;
  audio.play().catch(function(){});
  deckState[deck].playing = true;
  document.getElementById('d'+deck+'-play').classList.add('playing');
}
function deckPause(deck) {
  var audio = deck==='a' ? audioA : audioB;
  audio.pause();
  deckState[deck].playing = false;
  document.getElementById('d'+deck+'-play').classList.remove('playing');
}
function deckStop(deck) {
  var audio = deck==='a' ? audioA : audioB;
  audio.pause(); audio.currentTime = 0;
  deckState[deck].playing = false;
  document.getElementById('d'+deck+'-play').classList.remove('playing');
}
function deckSkip(deck) { deckStop(deck); }

function setDeckVol(deck, val) {
  var audio = deck==='a' ? audioA : audioB;
  audio.volume = parseFloat(val);
  document.getElementById('d'+deck+'-vol').textContent = Math.round(val*100)+'%';
}

function toggleKill(deck, band, btn) {
  btn.classList.toggle('killed');
  /* We would apply 3-band EQ kill via Web Audio API if AudioContext is available */
}

/* ── Crossfader visual + gain application ─────────────────────────────────── */
function drawXfCanvas() {
  var c = document.getElementById('dd-xf-canvas');
  var ctx = c.getContext('2d');
  var dpr = window.devicePixelRatio||1;
  var w = c.clientWidth, h = c.clientHeight;
  c.width = w*dpr; c.height = h*dpr; ctx.scale(dpr,dpr);
  ctx.fillStyle='#0f172a'; ctx.fillRect(0,0,w,h);
  var fn = CF[curCurve];
  var pos = parseFloat(document.getElementById('dd-xf').value);
  /* We draw grid */
  ctx.strokeStyle='rgba(51,65,85,.4)'; ctx.lineWidth=1;
  for(var i=0;i<=4;i++){var gy=h*i/4;ctx.beginPath();ctx.moveTo(0,gy);ctx.lineTo(w,gy);ctx.stroke();}
  /* We draw A curve */
  ctx.beginPath();
  for(var i=0;i<=80;i++){var x=i/80,g=fn(x);i===0?ctx.moveTo(x*w,(1-g.a)*h):ctx.lineTo(x*w,(1-g.a)*h);}
  ctx.strokeStyle='#14b8a6';ctx.lineWidth=2;ctx.stroke();
  /* We draw B curve */
  ctx.beginPath();
  for(var i=0;i<=80;i++){var x=i/80,g=fn(x);i===0?ctx.moveTo(x*w,(1-g.b)*h):ctx.lineTo(x*w,(1-g.b)*h);}
  ctx.strokeStyle='#0891b2';ctx.lineWidth=2;ctx.stroke();
  /* We draw position marker */
  var px=pos*w;
  ctx.setLineDash([3,3]);ctx.strokeStyle='rgba(255,255,255,.5)';ctx.lineWidth=1;
  ctx.beginPath();ctx.moveTo(px,0);ctx.lineTo(px,h);ctx.stroke();ctx.setLineDash([]);
  var g=fn(pos);
  ctx.fillStyle='#14b8a6';ctx.beginPath();ctx.arc(px,(1-g.a)*h,5,0,Math.PI*2);ctx.fill();
  ctx.fillStyle='#0891b2';ctx.beginPath();ctx.arc(px,(1-g.b)*h,5,0,Math.PI*2);ctx.fill();
}

function onFaderMove() {
  var pos = parseFloat(document.getElementById('dd-xf').value);
  var g = CF[curCurve](pos);
  document.getElementById('dd-ga').textContent = toDb(g.a);
  document.getElementById('dd-gb').textContent = toDb(g.b);
  /* We apply crossfade gains to the audio elements */
  audioA.volume = g.a * parseFloat(document.querySelector('.deck-a .deck-vol input').value);
  audioB.volume = g.b * parseFloat(document.querySelector('.deck-b .deck-vol input').value);
  drawXfCanvas();
}

function onCurveChange() {
  curCurve = parseInt(document.getElementById('dd-curve').value);
  onFaderMove();
}

/* ── Auto crossfade ───────────────────────────────────────────────────────── */
var autoTimer = null;
function autoXfade(target) {
  if (autoTimer) { clearInterval(autoTimer); autoTimer = null; }
  var dur = parseFloat(document.getElementById('dd-auto-dur').value) || 5;
  var fader = document.getElementById('dd-xf');
  var start = parseFloat(fader.value);
  var startTime = Date.now();
  var durMs = dur * 1000;
  autoTimer = setInterval(function(){
    var elapsed = Date.now() - startTime;
    var progress = Math.min(1, elapsed / durMs);
    fader.value = start + (target - start) * progress;
    onFaderMove();
    if (progress >= 1) { clearInterval(autoTimer); autoTimer = null; }
  }, 16);
}

/* ── PTT ──────────────────────────────────────────────────────────────────── */
function pttOn() {
  document.getElementById('dd-ptt').classList.add('active');
  mc1Api('POST', '/api/v1/ptt/activate').catch(function(){});
}
function pttOff() {
  document.getElementById('dd-ptt').classList.remove('active');
  mc1Api('POST', '/api/v1/ptt/deactivate').catch(function(){});
}

/* ── Time display update ──────────────────────────────────────────────────── */
function fmtTime(sec) {
  var m = Math.floor(sec/60), s = Math.floor(sec%60);
  return m + ':' + (s<10?'0':'') + s;
}
function tickTime() {
  ['a','b'].forEach(function(d){
    var audio = d==='a' ? audioA : audioB;
    var el = document.getElementById('d'+d+'-time');
    if (el && audio.duration) {
      el.textContent = fmtTime(audio.currentTime) + ' / ' + fmtTime(audio.duration);
    }
  });
  requestAnimationFrame(tickTime);
}

/* ── Startup ──────────────────────────────────────────────────────────────── */
document.addEventListener('DOMContentLoaded', function() {
  drawXfCanvas();
  onFaderMove();
  requestAnimationFrame(tickTime);

  /* We handle spacebar for PTT */
  document.addEventListener('keydown', function(e) {
    if (e.code === 'Space' && !e.repeat && e.target.tagName !== 'INPUT' && e.target.tagName !== 'SELECT') {
      e.preventDefault(); pttOn();
    }
  });
  document.addEventListener('keyup', function(e) {
    if (e.code === 'Space' && e.target.tagName !== 'INPUT' && e.target.tagName !== 'SELECT') {
      e.preventDefault(); pttOff();
    }
  });

  /* We handle window resize */
  window.addEventListener('resize', function(){ drawXfCanvas(); });
});
</script>
</body>
</html>
