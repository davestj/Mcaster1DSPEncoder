<?php
/**
 * dualdeck-player.php — Standalone Dual-Deck DJ Player (Frameless Popup)
 *
 * File:    src/linux/web_ui/dualdeck-player.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-04-03
 * Purpose: Self-contained dual-deck A/B player with crossfader, PTT,
 *          DJ queue, Auto-DJ mode, and per-deck EQ kill switches.
 *          Opened via window.open() from crossfader.php or nav.
 *
 * Standards:
 *  - No exit()/die() — uopz extension is active
 *  - Self-contained: no header.php/footer.php dependencies
 *  - DOMContentLoaded wraps all startup calls
 *  - Single click = select, double-click = load+play, right-click = context menu
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/app/inc/mc1_config.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/auth.php';
require_once __DIR__ . '/app/inc/user_auth.php';
require_once __DIR__ . '/app/inc/playlist.builder.algorithm.class.php';

if (!mc1_is_authed()) { header('Location: /login.html'); return; }

/* We load tracks for the library browser */
$tracks = [];
try {
    $pdo = mc1_db('mcaster1_media');
    $tracks = $pdo->query("SELECT id, title, artist, album, duration_ms FROM tracks WHERE is_missing=0 ORDER BY artist, title LIMIT 1000")->fetchAll(PDO::FETCH_ASSOC);
} catch (Exception $e) {}

/* We load encoder slots for output assignment */
$slots = [];
try {
    $pdo2 = mc1_db('mcaster1_encoder');
    $slots = $pdo2->query("SELECT slot_id, name FROM encoder_configs ORDER BY slot_id")->fetchAll(PDO::FETCH_ASSOC);
} catch (Exception $e) {}

/* We load playlists for auto-DJ source selection */
$playlists = [];
try {
    $pdo3 = mc1_db('mcaster1_media');
    $playlists = $pdo3->query("SELECT id, name FROM playlists ORDER BY name")->fetchAll(PDO::FETCH_ASSOC);
} catch (Exception $e) {}

/* We load the algorithm catalog for the auto-DJ dropdown */
$algos = PlaylistBuilderAlgorithm::algo_catalog();
?>
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Mcaster1 — Dual-Deck DJ Player</title>
<link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.5.1/css/all.min.css">
<style>
:root{--bg:#0f172a;--bg2:#1e293b;--bg3:#162032;--card:#1e293b;--border:#334155;--border2:#2d3f55;--teal:#14b8a6;--teal2:#0d9488;--teal-glow:rgba(20,184,166,.18);--cyan:#0891b2;--text:#e2e8f0;--text-dim:#94a3b8;--muted:#64748b;--red:#ef4444;--green:#22c55e;--orange:#f97316;--yellow:#eab308;--radius:10px;--radius-sm:6px;--radius-xs:4px;--font:'Inter','Segoe UI',system-ui,sans-serif;--font-mono:'SF Mono','Fira Code',monospace;}
*{margin:0;padding:0;box-sizing:border-box;}
body{background:var(--bg);color:var(--text);font-family:var(--font);font-size:13px;overflow:hidden;height:100vh;display:flex;flex-direction:column;}

/* ── Header ─────────────────────────────────── */
.dd-top{background:var(--bg2);border-bottom:1px solid var(--border);padding:6px 16px;display:flex;align-items:center;justify-content:space-between;}
.dd-top h1{font-size:.95rem;color:var(--teal);font-weight:600;}
.dd-top-right{display:flex;align-items:center;gap:12px;}
.dd-top select{background:var(--bg3);border:1px solid var(--border);color:var(--text);padding:4px 8px;border-radius:var(--radius-sm);font-size:.75rem;}

/* ── Main Layout ────────────────────────────── */
.dd-main{flex:1;display:grid;grid-template-columns:1fr 280px 1fr;gap:0;overflow:hidden;}

/* ── Deck ───────────────────────────────────── */
.deck{display:flex;flex-direction:column;padding:10px;overflow:hidden;}
.deck-label{font-size:.7rem;font-weight:700;text-transform:uppercase;letter-spacing:1px;margin-bottom:6px;display:flex;align-items:center;gap:8px;}
.deck-a .deck-label{color:var(--teal);}
.deck-b .deck-label{color:var(--cyan);}
.deck-info{background:var(--card);border:1px solid var(--border);border-radius:var(--radius-sm);padding:8px 10px;margin-bottom:6px;min-height:52px;}
.deck-title{font-size:.85rem;font-weight:600;color:var(--text);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;}
.deck-artist{font-size:.75rem;color:var(--text-dim);}
.deck-time{font-family:var(--font-mono);font-size:.7rem;color:var(--muted);margin-top:3px;}
.deck-transport{display:flex;gap:4px;margin-bottom:6px;}
.deck-transport button{flex:1;padding:6px;border:1px solid var(--border);background:var(--bg3);color:var(--text);border-radius:var(--radius-xs);cursor:pointer;font-size:.75rem;}
.deck-transport button:hover{border-color:var(--teal);color:var(--teal);}
.deck-transport button.playing{background:var(--teal);border-color:var(--teal);color:#fff;}
.deck-vol{display:flex;align-items:center;gap:6px;margin-bottom:6px;font-size:.65rem;color:var(--text-dim);}
.deck-vol input{flex:1;height:3px;-webkit-appearance:none;appearance:none;background:var(--border);border-radius:2px;outline:none;}
.deck-vol input::-webkit-slider-thumb{-webkit-appearance:none;width:10px;height:10px;border-radius:50%;background:var(--teal);cursor:pointer;}
.deck-kills{display:flex;gap:3px;margin-bottom:6px;}
.deck-kills button{flex:1;padding:3px;border:1px solid var(--border);background:transparent;color:var(--muted);border-radius:3px;cursor:pointer;font-size:.6rem;font-weight:700;}
.deck-kills button.killed{background:var(--red);border-color:var(--red);color:#fff;}
.deck-library{flex:1;overflow-y:auto;background:var(--card);border:1px solid var(--border);border-radius:var(--radius-sm);}
.deck-library table{width:100%;border-collapse:collapse;}
.deck-library th{position:sticky;top:0;background:var(--bg2);padding:3px 6px;text-align:left;font-size:.6rem;color:var(--muted);text-transform:uppercase;border-bottom:1px solid var(--border);z-index:1;}
.deck-library td{padding:3px 6px;font-size:.7rem;border-bottom:1px solid var(--border2);cursor:pointer;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:140px;user-select:none;}
.deck-library tr:hover td{background:var(--bg3);}
.deck-library tr.selected td{background:rgba(99,102,241,.15);color:var(--text);}
.deck-library tr.loaded td{background:var(--teal-glow);color:var(--teal);}

/* ── Center Column ─────────────────────────── */
.dd-center{width:280px;background:var(--bg2);border-left:1px solid var(--border);border-right:1px solid var(--border);display:flex;flex-direction:column;padding:8px 10px;gap:6px;overflow-y:auto;}
.xf-label{font-size:.65rem;text-transform:uppercase;color:var(--muted);letter-spacing:.5px;text-align:center;font-weight:600;}
.xf-canvas-wrap{width:100%;height:100px;}
.xf-canvas-wrap canvas{width:100%;height:100%;border-radius:var(--radius-sm);background:var(--bg);}
.xf-slider{width:100%;height:6px;-webkit-appearance:none;appearance:none;background:linear-gradient(to right,var(--teal),var(--cyan));border-radius:3px;outline:none;cursor:pointer;}
.xf-slider::-webkit-slider-thumb{-webkit-appearance:none;width:18px;height:18px;border-radius:50%;background:#fff;border:3px solid var(--teal);cursor:grab;}
.xf-gains{display:flex;justify-content:space-between;font-family:var(--font-mono);font-size:.65rem;}
.xf-gains .ga{color:var(--teal);}
.xf-gains .gb{color:var(--cyan);}
.xf-curve-sel{width:100%;background:var(--bg3);border:1px solid var(--border);color:var(--text);padding:4px;border-radius:var(--radius-xs);font-size:.7rem;}
.xf-auto{display:flex;gap:4px;align-items:center;}
.xf-auto input{width:44px;background:var(--bg3);border:1px solid var(--border);color:var(--text);padding:3px;border-radius:3px;font-size:.7rem;text-align:center;}
.xf-auto button{flex:1;padding:4px;border:1px solid var(--border);background:var(--bg3);color:var(--text);border-radius:3px;cursor:pointer;font-size:.65rem;}
.xf-auto button:hover{border-color:var(--teal);color:var(--teal);}

/* ── PTT ───────────────────────────────────── */
.dd-ptt{width:48px;height:48px;border-radius:50%;border:2px solid var(--border);background:var(--bg3);color:var(--text-dim);font-size:.6rem;font-weight:700;cursor:pointer;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:1px;transition:all .15s;margin:0 auto;user-select:none;}
.dd-ptt:hover{border-color:var(--teal);}
.dd-ptt.active{background:var(--red);border-color:var(--red);color:#fff;box-shadow:0 0 12px rgba(239,68,68,.4);}
.dd-ptt i{font-size:.9rem;}

/* ── DJ Mode Toggle ────────────────────────── */
.dd-mode-row{display:flex;align-items:center;justify-content:center;gap:8px;padding:4px 0;}
.dd-mode-label{font-size:.7rem;font-weight:600;color:var(--text-dim);}
.dd-mode-toggle{position:relative;width:36px;height:18px;cursor:pointer;}
.dd-mode-toggle input{opacity:0;width:0;height:0;position:absolute;}
.dd-mode-slider{position:absolute;inset:0;background:var(--border);border-radius:9px;transition:.2s;}
.dd-mode-slider::before{content:'';position:absolute;height:12px;width:12px;left:3px;bottom:3px;background:#fff;border-radius:50%;transition:.2s;}
.dd-mode-toggle input:checked+.dd-mode-slider{background:var(--teal);}
.dd-mode-toggle input:checked+.dd-mode-slider::before{transform:translateX(18px);}

/* ── Auto-DJ Settings ──────────────────────── */
.dd-autodj-panel{display:none;background:var(--bg3);border:1px solid var(--border);border-radius:var(--radius-sm);padding:8px;font-size:.7rem;}
.dd-autodj-panel.open{display:block;}
.dd-autodj-row{display:flex;align-items:center;justify-content:space-between;margin-bottom:5px;gap:6px;}
.dd-autodj-row:last-child{margin-bottom:0;}
.dd-autodj-row label{color:var(--text-dim);font-size:.65rem;flex-shrink:0;}
.dd-autodj-row select,.dd-autodj-row input[type=number]{flex:1;background:var(--bg);border:1px solid var(--border);color:var(--text);padding:3px 5px;border-radius:3px;font-size:.7rem;min-width:0;}
.dd-autodj-row input[type=number]{width:50px;text-align:center;}
.dd-refill-btn{width:100%;margin-top:6px;padding:5px;border:1px solid var(--teal);background:rgba(20,184,166,.1);color:var(--teal);border-radius:var(--radius-xs);cursor:pointer;font-size:.65rem;font-weight:600;}
.dd-refill-btn:hover{background:rgba(20,184,166,.2);}

/* ── DJ Queue ──────────────────────────────── */
.dd-queue{flex:1;min-height:80px;overflow-y:auto;background:var(--bg);border:1px solid var(--border);border-radius:var(--radius-sm);}
.dd-queue-empty{text-align:center;color:var(--muted);padding:16px 8px;font-size:.7rem;}
.dd-queue-item{display:flex;align-items:center;gap:5px;padding:4px 6px;border-bottom:1px solid var(--border2);font-size:.7rem;cursor:default;user-select:none;}
.dd-queue-item:hover{background:var(--bg3);}
.dd-queue-item:last-child{border-bottom:none;}
.dd-q-pos{width:16px;text-align:center;color:var(--muted);font-size:.6rem;font-weight:700;flex-shrink:0;}
.dd-q-info{flex:1;min-width:0;overflow:hidden;}
.dd-q-title{white-space:nowrap;overflow:hidden;text-overflow:ellipsis;color:var(--text);font-weight:500;font-size:.68rem;}
.dd-q-artist{white-space:nowrap;overflow:hidden;text-overflow:ellipsis;color:var(--muted);font-size:.6rem;}
.dd-q-dur{font-family:var(--font-mono);font-size:.6rem;color:var(--muted);flex-shrink:0;}
.dd-q-src{font-size:.5rem;padding:1px 4px;border-radius:3px;flex-shrink:0;font-weight:600;}
.dd-q-src.auto{background:rgba(20,184,166,.12);color:var(--teal);}
.dd-q-src.manual{background:rgba(99,102,241,.12);color:#818cf8;}

/* ── Context Menus ─────────────────────────── */
.dd-ctx{position:fixed;display:none;background:var(--card);border:1px solid var(--border);border-radius:var(--radius-sm);box-shadow:0 8px 24px rgba(0,0,0,.6);z-index:9000;min-width:160px;padding:4px 0;font-size:.75rem;}
.dd-ctx-item{padding:6px 14px;color:var(--text-dim);cursor:pointer;display:flex;align-items:center;gap:8px;}
.dd-ctx-item:hover{background:rgba(255,255,255,.06);color:var(--text);}
.dd-ctx-item i{width:14px;text-align:center;font-size:.7rem;}
.dd-ctx-sep{height:1px;background:var(--border);margin:3px 0;}

/* ── Toast ──────────────────────────────────── */
.toast-container{position:fixed;top:10px;right:10px;z-index:9999;}
.toast{padding:6px 14px;border-radius:var(--radius-sm);margin-bottom:5px;font-size:.75rem;animation:fadeIn .2s;}
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
    <label style="font-size:.7rem;color:var(--muted);">Output Slot:</label>
    <select id="dd-slot">
      <option value="">-- None --</option>
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
      <button onclick="deckPlay('a')" id="da-play" title="Play"><i class="fa-solid fa-play"></i></button>
      <button onclick="deckPause('a')" title="Pause"><i class="fa-solid fa-pause"></i></button>
      <button onclick="deckStop('a')" title="Stop"><i class="fa-solid fa-stop"></i></button>
      <button onclick="deckSkip('a')" title="Skip to next"><i class="fa-solid fa-forward-step"></i></button>
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
      <tr data-id="<?= (int)$t['id'] ?>" data-title="<?= htmlspecialchars($t['title'] ?? '', ENT_QUOTES) ?>" data-artist="<?= htmlspecialchars($t['artist'] ?? '', ENT_QUOTES) ?>">
        <td><?= htmlspecialchars($t['title'] ?? '', ENT_QUOTES) ?></td>
        <td><?= htmlspecialchars($t['artist'] ?? '', ENT_QUOTES) ?></td>
        <td><?= isset($t['duration_ms']) ? gmdate('i:s', (int)$t['duration_ms']/1000) : '' ?></td>
      </tr>
      <?php endforeach; ?>
      </tbody></table>
    </div>
  </div>

  <!-- Center: Crossfader + PTT + Auto-DJ + Queue -->
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
      <option value="8">Dual Open</option>
    </select>
    <div class="xf-auto">
      <input type="number" id="dd-auto-dur" value="5" min="1" max="30" step="0.5" title="Crossfade duration (sec)">
      <button onclick="autoXfade(0)" title="Crossfade to A">A</button>
      <button onclick="autoXfade(1)" title="Crossfade to B">B</button>
    </div>

    <!-- PTT -->
    <div style="text-align:center;padding:4px 0;">
      <button class="dd-ptt" id="dd-ptt" onmousedown="pttOn()" onmouseup="pttOff()" ontouchstart="pttOn()" ontouchend="pttOff()">
        <i class="fa-solid fa-microphone"></i>PTT
      </button>
      <div style="font-size:.55rem;color:var(--muted);margin-top:2px;">Space = PTT</div>
    </div>

    <!-- DJ Mode Toggle -->
    <div class="dd-mode-row">
      <span class="dd-mode-label">MANUAL</span>
      <label class="dd-mode-toggle">
        <input type="checkbox" id="dd-mode-chk" onchange="toggleDjMode()">
        <span class="dd-mode-slider"></span>
      </label>
      <span class="dd-mode-label" id="dd-mode-auto-label" style="color:var(--muted);">AUTO-DJ</span>
    </div>

    <!-- Auto-DJ Settings -->
    <div class="dd-autodj-panel" id="dd-autodj-panel">
      <div class="dd-autodj-row">
        <label>Algorithm</label>
        <select id="dj-algo">
          <?php foreach ($algos as $key => $info): ?>
          <option value="<?= htmlspecialchars($key, ENT_QUOTES) ?>" <?= $key === 'smart_rotation' ? 'selected' : '' ?>><?= htmlspecialchars($info['label'], ENT_QUOTES) ?></option>
          <?php endforeach; ?>
        </select>
      </div>
      <div class="dd-autodj-row">
        <label>Source</label>
        <select id="dj-source">
          <option value="0">All Tracks</option>
          <?php foreach ($playlists as $pl): ?>
          <option value="<?= (int)$pl['id'] ?>"><?= htmlspecialchars($pl['name'], ENT_QUOTES) ?></option>
          <?php endforeach; ?>
        </select>
      </div>
      <div class="dd-autodj-row">
        <label>Queue Depth</label>
        <input type="number" id="dj-depth" value="10" min="3" max="50" step="1">
      </div>
      <div class="dd-autodj-row">
        <label>Artist Sep.</label>
        <input type="number" id="dj-artist-sep" value="3" min="0" max="20" step="1">
      </div>
      <div class="dd-autodj-row">
        <label>No Repeat (hrs)</label>
        <input type="number" id="dj-repeat-hrs" value="1" min="0" max="24" step="1">
      </div>
      <button class="dd-refill-btn" onclick="djAutoFill()"><i class="fa-solid fa-rotate"></i> Refill Queue Now</button>
    </div>

    <!-- DJ Queue -->
    <div class="xf-label">DJ Queue</div>
    <div class="dd-queue" id="dd-queue-list">
      <div class="dd-queue-empty">Queue empty — right-click tracks to add</div>
    </div>
    <div style="display:flex;gap:4px;">
      <button onclick="djQueueClear()" style="flex:1;padding:3px;border:1px solid var(--border);background:var(--bg3);color:var(--muted);border-radius:3px;cursor:pointer;font-size:.6rem;" title="Clear queue"><i class="fa-solid fa-trash"></i> Clear</button>
    </div>
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
      <button onclick="deckPlay('b')" id="db-play" title="Play"><i class="fa-solid fa-play"></i></button>
      <button onclick="deckPause('b')" title="Pause"><i class="fa-solid fa-pause"></i></button>
      <button onclick="deckStop('b')" title="Stop"><i class="fa-solid fa-stop"></i></button>
      <button onclick="deckSkip('b')" title="Skip to next"><i class="fa-solid fa-forward-step"></i></button>
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
      <tr data-id="<?= (int)$t['id'] ?>" data-title="<?= htmlspecialchars($t['title'] ?? '', ENT_QUOTES) ?>" data-artist="<?= htmlspecialchars($t['artist'] ?? '', ENT_QUOTES) ?>">
        <td><?= htmlspecialchars($t['title'] ?? '', ENT_QUOTES) ?></td>
        <td><?= htmlspecialchars($t['artist'] ?? '', ENT_QUOTES) ?></td>
        <td><?= isset($t['duration_ms']) ? gmdate('i:s', (int)$t['duration_ms']/1000) : '' ?></td>
      </tr>
      <?php endforeach; ?>
      </tbody></table>
    </div>
  </div>
</div>

<!-- Context Menu: Track Library -->
<div class="dd-ctx" id="dd-track-ctx">
  <div class="dd-ctx-item" onclick="ctxPlayNext()"><i class="fa-solid fa-forward"></i> Play Next</div>
  <div class="dd-ctx-item" onclick="ctxAddToQueue()"><i class="fa-solid fa-plus"></i> Add to Queue</div>
</div>

<!-- Context Menu: Queue Item -->
<div class="dd-ctx" id="dd-queue-ctx">
  <div class="dd-ctx-item" onclick="qCtxPlayNow()"><i class="fa-solid fa-play"></i> Play Now</div>
  <div class="dd-ctx-item" onclick="qCtxMoveTop()"><i class="fa-solid fa-arrow-up"></i> Move to Top</div>
  <div class="dd-ctx-sep"></div>
  <div class="dd-ctx-item" onclick="qCtxRemove()" style="color:var(--red)"><i class="fa-solid fa-xmark"></i> Remove</div>
</div>

<div class="toast-container" id="toast-container"></div>

<script>
/* ── Self-contained utilities ────────────────────────────────────────────── */
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
function esc(s) {
  var d = document.createElement('div'); d.textContent = s || ''; return d.innerHTML;
}
function fmtTime(sec) {
  if (!sec || !isFinite(sec)) return '0:00';
  var m = Math.floor(sec/60), s = Math.floor(sec%60);
  return m + ':' + (s<10?'0':'') + s;
}
function fmtMs(ms) { return fmtTime((ms||0)/1000); }

/* ── Crossfader curve math ───────────────────────────────────────────────── */
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

/* ── Audio elements ──────────────────────────────────────────────────────── */
var audioA = new Audio();
var audioB = new Audio();
audioA.crossOrigin = 'anonymous';
audioB.crossOrigin = 'anonymous';

/* ── Deck state ──────────────────────────────────────────────────────────── */
var deckState = {a:{id:0,playing:false}, b:{id:0,playing:false}};
var activeDeck = null;   /* Which deck is currently "live" (fader target) */

/* ── DJ state ────────────────────────────────────────────────────────────── */
var djQueue = [];
var djMode = 'manual';   /* 'manual' | 'auto' */
var djSettings = {
  algorithm: 'smart_rotation',
  sourcePlaylistId: 0,
  queueDepth: 10,
  artistSeparation: 3,
  songRepeatHrs: 1
};
var xfadeInProgress = false;

/* ── Context menu state ──────────────────────────────────────────────────── */
var ctxState = {deck:null, trackId:0, queueIdx:-1, queueId:0};

/* ════════════════════════════════════════════════════════════════════════════
 * DECK OPERATIONS
 * ════════════════════════════════════════════════════════════════════════════ */

function loadTrackToDeck(deck, trackId, title, artist) {
  var audio = deck==='a' ? audioA : audioB;
  audio.src = '/app/api/audio.php?id=' + trackId;
  deckState[deck].id = trackId;
  document.getElementById('d'+deck+'-title').textContent = title || 'Unknown';
  document.getElementById('d'+deck+'-artist').textContent = artist || '';
  /* We highlight the loaded track in the library */
  var tbody = document.getElementById('d'+deck+'-lib');
  tbody.querySelectorAll('tr').forEach(function(r){r.classList.remove('loaded');});
  var row = tbody.querySelector('tr[data-id="'+trackId+'"]');
  if (row) row.classList.add('loaded');
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

function deckSkip(deck) {
  deckStop(deck);
  onDeckEnded(deck);
}

function setDeckVol(deck, val) {
  document.getElementById('d'+deck+'-vol').textContent = Math.round(val*100)+'%';
  onFaderMove(); /* We recalculate with new deck volume */
}

function toggleKill(deck, band, btn) {
  btn.classList.toggle('killed');
}

/* ════════════════════════════════════════════════════════════════════════════
 * CROSSFADER
 * ════════════════════════════════════════════════════════════════════════════ */

function drawXfCanvas() {
  var c = document.getElementById('dd-xf-canvas');
  if (!c) return;
  var ctx = c.getContext('2d');
  var dpr = window.devicePixelRatio||1;
  var w = c.clientWidth, h = c.clientHeight;
  c.width = w*dpr; c.height = h*dpr; ctx.scale(dpr,dpr);
  ctx.fillStyle='#0f172a'; ctx.fillRect(0,0,w,h);
  var fn = CF[curCurve];
  var pos = parseFloat(document.getElementById('dd-xf').value);
  ctx.strokeStyle='rgba(51,65,85,.4)'; ctx.lineWidth=1;
  for(var i=0;i<=4;i++){var gy=h*i/4;ctx.beginPath();ctx.moveTo(0,gy);ctx.lineTo(w,gy);ctx.stroke();}
  ctx.beginPath();
  for(var i=0;i<=80;i++){var x=i/80,g=fn(x);i===0?ctx.moveTo(x*w,(1-g.a)*h):ctx.lineTo(x*w,(1-g.a)*h);}
  ctx.strokeStyle='#14b8a6';ctx.lineWidth=2;ctx.stroke();
  ctx.beginPath();
  for(var i=0;i<=80;i++){var x=i/80,g=fn(x);i===0?ctx.moveTo(x*w,(1-g.b)*h):ctx.lineTo(x*w,(1-g.b)*h);}
  ctx.strokeStyle='#0891b2';ctx.lineWidth=2;ctx.stroke();
  var px=pos*w;
  ctx.setLineDash([3,3]);ctx.strokeStyle='rgba(255,255,255,.5)';ctx.lineWidth=1;
  ctx.beginPath();ctx.moveTo(px,0);ctx.lineTo(px,h);ctx.stroke();ctx.setLineDash([]);
  var g=fn(pos);
  ctx.fillStyle='#14b8a6';ctx.beginPath();ctx.arc(px,(1-g.a)*h,4,0,Math.PI*2);ctx.fill();
  ctx.fillStyle='#0891b2';ctx.beginPath();ctx.arc(px,(1-g.b)*h,4,0,Math.PI*2);ctx.fill();
}

function onFaderMove() {
  var pos = parseFloat(document.getElementById('dd-xf').value);
  var g = CF[curCurve](pos);
  document.getElementById('dd-ga').textContent = toDb(g.a);
  document.getElementById('dd-gb').textContent = toDb(g.b);
  var volA = parseFloat(document.querySelector('.deck-a .deck-vol input').value);
  var volB = parseFloat(document.querySelector('.deck-b .deck-vol input').value);
  audioA.volume = g.a * volA;
  audioB.volume = g.b * volB;
  drawXfCanvas();
}

function onCurveChange() {
  curCurve = parseInt(document.getElementById('dd-curve').value);
  onFaderMove();
}

var autoTimer = null;
function autoXfade(target) {
  if (autoTimer) { clearInterval(autoTimer); autoTimer = null; }
  var dur = parseFloat(document.getElementById('dd-auto-dur').value) || 5;
  var fader = document.getElementById('dd-xf');
  var start = parseFloat(fader.value);
  var startTime = Date.now();
  var durMs = dur * 1000;
  xfadeInProgress = true;
  autoTimer = setInterval(function(){
    var elapsed = Date.now() - startTime;
    var progress = Math.min(1, elapsed / durMs);
    fader.value = start + (target - start) * progress;
    onFaderMove();
    if (progress >= 1) {
      clearInterval(autoTimer);
      autoTimer = null;
      xfadeInProgress = false;
    }
  }, 16);
}

/* ════════════════════════════════════════════════════════════════════════════
 * PTT
 * ════════════════════════════════════════════════════════════════════════════ */

function pttOn() {
  document.getElementById('dd-ptt').classList.add('active');
  mc1Api('POST', '/api/v1/ptt/activate').catch(function(){});
}
function pttOff() {
  document.getElementById('dd-ptt').classList.remove('active');
  mc1Api('POST', '/api/v1/ptt/deactivate').catch(function(){});
}

/* ════════════════════════════════════════════════════════════════════════════
 * AUTO-ADVANCE: track ended → crossfade → load next
 * ════════════════════════════════════════════════════════════════════════════ */

function onDeckEnded(deck) {
  deckState[deck].playing = false;
  document.getElementById('d'+deck+'-play').classList.remove('playing');

  if (djMode === 'manual') return; /* Manual mode: DJ handles it */

  /* Auto-DJ: crossfade to the other deck, load next from queue to idle deck */
  var other = deck === 'a' ? 'b' : 'a';

  if (deckState[other].id > 0) {
    /* We start the other deck and crossfade to it */
    deckPlay(other);
    autoXfade(other === 'b' ? 1 : 0);
    activeDeck = other;
  }

  /* We load the next track from the queue into the now-idle deck */
  loadNextFromQueue(deck);
}

function loadNextFromQueue(deck) {
  mc1Api('POST', '/app/api/dj.php', {action:'queue_pop'}).then(function(d) {
    if (!d || !d.ok || !d.track) {
      /* Queue empty — try to refill if auto-DJ */
      if (djMode === 'auto') {
        djAutoFill().then(function() {
          mc1Api('POST', '/app/api/dj.php', {action:'queue_pop'}).then(function(d2) {
            if (d2 && d2.ok && d2.track) {
              loadTrackToDeck(deck, d2.track.track_id, d2.track.title, d2.track.artist);
              djQueueLoad();
            }
          });
        });
      }
      return;
    }
    loadTrackToDeck(deck, d.track.track_id, d.track.title, d.track.artist);
    djQueueLoad();
    maybeAutoRefill();
  });
}

/* We start crossfade early (before track ends) for seamless transitions */
function checkEarlyCrossfade(deck) {
  var audio = deck === 'a' ? audioA : audioB;
  if (!audio.duration || !deckState[deck].playing || djMode !== 'auto') return;
  var remaining = audio.duration - audio.currentTime;
  var xfDur = parseFloat(document.getElementById('dd-auto-dur').value) || 5;
  if (remaining <= xfDur && remaining > 0 && !xfadeInProgress) {
    var other = deck === 'a' ? 'b' : 'a';
    if (deckState[other].id > 0 && !deckState[other].playing) {
      deckPlay(other);
      autoXfade(other === 'b' ? 1 : 0);
      activeDeck = other;
    }
  }
}

/* ════════════════════════════════════════════════════════════════════════════
 * DJ MODE TOGGLE
 * ════════════════════════════════════════════════════════════════════════════ */

function toggleDjMode() {
  var chk = document.getElementById('dd-mode-chk');
  djMode = chk.checked ? 'auto' : 'manual';
  document.getElementById('dd-mode-auto-label').style.color = djMode === 'auto' ? 'var(--teal)' : 'var(--muted)';
  var panel = document.getElementById('dd-autodj-panel');
  if (djMode === 'auto') {
    panel.classList.add('open');
    readDjSettings();
    djAutoFill();
    /* If nothing playing, start the A/B chain */
    if (!deckState.a.playing && !deckState.b.playing) {
      loadNextFromQueue('a');
      setTimeout(function() {
        if (deckState.a.id > 0) {
          deckPlay('a');
          activeDeck = 'a';
          loadNextFromQueue('b'); /* Preload B */
        }
      }, 500);
    }
  } else {
    panel.classList.remove('open');
  }
}

function readDjSettings() {
  djSettings.algorithm = document.getElementById('dj-algo').value;
  djSettings.sourcePlaylistId = parseInt(document.getElementById('dj-source').value) || 0;
  djSettings.queueDepth = parseInt(document.getElementById('dj-depth').value) || 10;
  djSettings.artistSeparation = parseInt(document.getElementById('dj-artist-sep').value) || 3;
  djSettings.songRepeatHrs = parseInt(document.getElementById('dj-repeat-hrs').value) || 1;
}

function maybeAutoRefill() {
  if (djMode !== 'auto') return;
  readDjSettings();
  if (djQueue.length < djSettings.queueDepth) {
    djAutoFill();
  }
}

/* ════════════════════════════════════════════════════════════════════════════
 * DJ QUEUE OPERATIONS
 * ════════════════════════════════════════════════════════════════════════════ */

function djQueueLoad() {
  mc1Api('POST', '/app/api/dj.php', {action:'queue_list'}).then(function(d) {
    if (d && d.ok) {
      djQueue = d.queue || [];
      renderDjQueue();
    }
  }).catch(function(){});
}

function djQueueAdd(trackIds, playNext) {
  mc1Api('POST', '/app/api/dj.php', {action:'queue_add', track_ids:trackIds, play_next:!!playNext, source:'manual'}).then(function(d) {
    if (d && d.ok) {
      djQueue = d.queue || [];
      renderDjQueue();
      mc1Toast(playNext ? 'Playing next' : 'Added to queue', 'ok');
    }
  }).catch(function(){ mc1Toast('Queue add failed', 'err'); });
}

function djQueueRemove(queueId) {
  mc1Api('POST', '/app/api/dj.php', {action:'queue_remove', queue_id:queueId}).then(function(d) {
    if (d && d.ok) {
      djQueue = d.queue || [];
      renderDjQueue();
    }
  }).catch(function(){});
}

function djQueueMoveTop(queueId) {
  mc1Api('POST', '/app/api/dj.php', {action:'queue_move_top', queue_id:queueId}).then(function(d) {
    if (d && d.ok) {
      djQueue = d.queue || [];
      renderDjQueue();
      mc1Toast('Moved to top', 'ok');
    }
  }).catch(function(){});
}

function djQueueClear() {
  mc1Api('POST', '/app/api/dj.php', {action:'queue_clear'}).then(function(d) {
    djQueue = [];
    renderDjQueue();
    mc1Toast('Queue cleared', 'ok');
  }).catch(function(){});
}

function djAutoFill() {
  readDjSettings();
  var needed = Math.max(1, djSettings.queueDepth - djQueue.length);
  return mc1Api('POST', '/app/api/dj.php', {
    action: 'auto_fill',
    algorithm: djSettings.algorithm,
    source_playlist_id: djSettings.sourcePlaylistId,
    count: needed,
    artist_separation: djSettings.artistSeparation,
    song_repeat_hrs: djSettings.songRepeatHrs
  }).then(function(d) {
    if (d && d.ok) {
      djQueue = d.queue || [];
      renderDjQueue();
      if (d.added > 0) mc1Toast('Auto-DJ: added ' + d.added + ' tracks', 'ok');
    } else {
      mc1Toast('Auto-fill: ' + (d && d.error ? d.error : 'failed'), 'err');
    }
  }).catch(function(){ mc1Toast('Auto-fill failed', 'err'); });
}

function renderDjQueue() {
  var el = document.getElementById('dd-queue-list');
  if (!djQueue.length) {
    el.innerHTML = '<div class="dd-queue-empty">Queue empty — right-click tracks to add</div>';
    return;
  }
  var html = '';
  for (var i = 0; i < djQueue.length; i++) {
    var item = djQueue[i];
    html += '<div class="dd-queue-item" data-qid="' + item.queue_id + '" data-idx="' + i + '" oncontextmenu="showQueueCtx(event,' + i + ')">'
         +  '<span class="dd-q-pos">' + (i+1) + '</span>'
         +  '<div class="dd-q-info"><div class="dd-q-title">' + esc(item.title) + '</div>'
         +  '<div class="dd-q-artist">' + esc(item.artist) + '</div></div>'
         +  '<span class="dd-q-dur">' + fmtMs(item.duration_ms) + '</span>'
         +  '<span class="dd-q-src ' + item.source + '">' + item.source + '</span>'
         +  '</div>';
  }
  el.innerHTML = html;
}

/* ════════════════════════════════════════════════════════════════════════════
 * CONTEXT MENUS
 * ════════════════════════════════════════════════════════════════════════════ */

function showTrackCtx(e, deck, trackId) {
  e.preventDefault();
  ctxState.deck = deck;
  ctxState.trackId = trackId;
  hideAllCtx();
  positionCtx('dd-track-ctx', e);
}

function showQueueCtx(e, idx) {
  e.preventDefault();
  e.stopPropagation();
  ctxState.queueIdx = idx;
  ctxState.queueId = djQueue[idx] ? djQueue[idx].queue_id : 0;
  hideAllCtx();
  positionCtx('dd-queue-ctx', e);
}

function positionCtx(id, e) {
  var menu = document.getElementById(id);
  menu.style.display = 'block';
  var mw = menu.offsetWidth, mh = menu.offsetHeight;
  menu.style.left = Math.min(e.clientX, window.innerWidth - mw - 8) + 'px';
  menu.style.top  = Math.min(e.clientY, window.innerHeight - mh - 8) + 'px';
}

function hideAllCtx() {
  document.querySelectorAll('.dd-ctx').forEach(function(m){ m.style.display = 'none'; });
}

/* Track context menu actions */
function ctxPlayNext() {
  hideAllCtx();
  if (ctxState.trackId > 0) djQueueAdd([ctxState.trackId], true);
}
function ctxAddToQueue() {
  hideAllCtx();
  if (ctxState.trackId > 0) djQueueAdd([ctxState.trackId], false);
}

/* Queue context menu actions */
function qCtxPlayNow() {
  hideAllCtx();
  if (ctxState.queueIdx < 0 || !djQueue[ctxState.queueIdx]) return;
  var item = djQueue[ctxState.queueIdx];
  /* We determine which deck to load: prefer the idle one */
  var deck = (!deckState.a.playing) ? 'a' : (!deckState.b.playing) ? 'b' : 'a';
  loadTrackToDeck(deck, item.track_id, item.title, item.artist);
  deckPlay(deck);
  if (deck === 'a') autoXfade(0);
  if (deck === 'b') autoXfade(1);
  activeDeck = deck;
  djQueueRemove(item.queue_id);
}
function qCtxMoveTop() {
  hideAllCtx();
  if (ctxState.queueId > 0) djQueueMoveTop(ctxState.queueId);
}
function qCtxRemove() {
  hideAllCtx();
  if (ctxState.queueId > 0) djQueueRemove(ctxState.queueId);
}

/* ════════════════════════════════════════════════════════════════════════════
 * TIME DISPLAY
 * ════════════════════════════════════════════════════════════════════════════ */

function tickTime() {
  ['a','b'].forEach(function(d){
    var audio = d==='a' ? audioA : audioB;
    var el = document.getElementById('d'+d+'-time');
    if (el && audio.duration) {
      el.textContent = fmtTime(audio.currentTime) + ' / ' + fmtTime(audio.duration);
    }
    /* We check for early crossfade opportunity */
    checkEarlyCrossfade(d);
  });
  requestAnimationFrame(tickTime);
}

/* ════════════════════════════════════════════════════════════════════════════
 * STARTUP
 * ════════════════════════════════════════════════════════════════════════════ */

document.addEventListener('DOMContentLoaded', function() {
  drawXfCanvas();
  onFaderMove();
  requestAnimationFrame(tickTime);
  djQueueLoad();

  /* ── Audio ended listeners ──────────────────────────────────────────── */
  audioA.addEventListener('ended', function(){ onDeckEnded('a'); });
  audioB.addEventListener('ended', function(){ onDeckEnded('b'); });

  /* ── Event delegation for deck libraries ────────────────────────────── */
  ['a','b'].forEach(function(deck) {
    var tbody = document.getElementById('d'+deck+'-lib');

    /* Single click = select (highlight only) */
    tbody.addEventListener('click', function(e) {
      var row = e.target.closest('tr');
      if (!row || !row.dataset.id) return;
      tbody.querySelectorAll('tr.selected').forEach(function(r){ r.classList.remove('selected'); });
      row.classList.add('selected');
    });

    /* Double click = load to deck and play */
    tbody.addEventListener('dblclick', function(e) {
      var row = e.target.closest('tr');
      if (!row || !row.dataset.id) return;
      var trackId = parseInt(row.dataset.id);
      var title = row.dataset.title || '';
      var artist = row.dataset.artist || '';
      loadTrackToDeck(deck, trackId, title, artist);
      deckPlay(deck);
      activeDeck = deck;
      mc1Toast('Loaded to Deck ' + deck.toUpperCase(), 'ok');
    });

    /* Right click = context menu */
    tbody.addEventListener('contextmenu', function(e) {
      var row = e.target.closest('tr');
      if (!row || !row.dataset.id) return;
      showTrackCtx(e, deck, parseInt(row.dataset.id));
    });
  });

  /* ── Spacebar for PTT ──────────────────────────────────────────────── */
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

  /* ── Close context menus on outside click ──────────────────────────── */
  document.addEventListener('click', function(e) {
    if (!e.target.closest('.dd-ctx')) hideAllCtx();
  });

  /* ── Window resize ─────────────────────────────────────────────────── */
  window.addEventListener('resize', function(){ drawXfCanvas(); });
});
</script>
</body>
</html>
