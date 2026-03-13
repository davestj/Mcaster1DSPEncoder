<?php
/**
 * jack.php — JACK Audio Routing Management
 *
 * File:    src/linux/web_ui/jack.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-25
 * Purpose: We manage the JACK audio daemon, virtual audio cables, and port
 *          connections. Supports headless (dummy) and desktop (ALSA) modes.
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/app/inc/auth.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/user_auth.php';

$page_title = 'JACK Audio';
$active_nav = 'jack';

require_once __DIR__ . '/app/inc/header.php';
?>

<style>
.jack-page { padding:24px; }
.jack-header { display:flex; align-items:center; justify-content:space-between; margin-bottom:20px; flex-wrap:wrap; gap:12px; }
.jack-header h2 { margin:0; font-size:1.4rem; color:var(--text); }

/* ── Status Panel ─────────────────────────────── */
.jack-status { background:var(--card); border:1px solid var(--border); border-radius:var(--radius); padding:20px; margin-bottom:20px; display:grid; grid-template-columns:repeat(auto-fit,minmax(150px,1fr)); gap:16px; }
.jack-stat { text-align:center; }
.jack-stat-label { font-size:.7rem; color:var(--muted); text-transform:uppercase; letter-spacing:.5px; margin-bottom:4px; }
.jack-stat-val { font-size:1.2rem; font-weight:700; font-family:var(--font-mono); color:var(--text); }
.jack-stat-val.online { color:var(--green); }
.jack-stat-val.offline { color:var(--red); }

/* ── Controls ─────────────────────────────────── */
.jack-controls { display:flex; gap:12px; flex-wrap:wrap; margin-bottom:20px; }
.jack-controls select, .jack-controls input { background:var(--bg3); border:1px solid var(--border); color:var(--text); padding:8px 12px; border-radius:var(--radius-xs); font-size:.85rem; }
.jack-btn { padding:9px 18px; border:none; border-radius:var(--radius-sm); cursor:pointer; font-size:.85rem; font-weight:600; display:inline-flex; align-items:center; gap:6px; }
.jack-btn-start { background:var(--green); color:#fff; }
.jack-btn-start:hover { opacity:.9; }
.jack-btn-stop { background:var(--red); color:#fff; }
.jack-btn-stop:hover { opacity:.9; }
.jack-btn-cable { background:var(--teal); color:#fff; }
.jack-btn-cable:hover { background:var(--teal2); }

/* ── Cables Table ─────────────────────────────── */
.jack-section { background:var(--card); border:1px solid var(--border); border-radius:var(--radius); padding:20px; margin-bottom:20px; }
.jack-section h3 { margin:0 0 16px; font-size:1rem; color:var(--text); }
.jack-table { width:100%; border-collapse:collapse; }
.jack-table th { text-align:left; padding:8px 12px; border-bottom:1px solid var(--border); font-size:.75rem; color:var(--muted); text-transform:uppercase; }
.jack-table td { padding:8px 12px; border-bottom:1px solid var(--border2); font-size:.85rem; color:var(--text); }
.jack-table td.mono { font-family:var(--font-mono); font-size:.8rem; color:var(--teal); }
.jack-table tr:hover td { background:var(--bg3); }
.jack-del { background:none; border:1px solid var(--border); color:var(--text-dim); padding:4px 10px; border-radius:var(--radius-xs); cursor:pointer; font-size:.75rem; }
.jack-del:hover { border-color:var(--red); color:var(--red); }

/* ── Port Matrix ──────────────────────────────── */
.jack-matrix { overflow-x:auto; }
.jack-matrix table { border-collapse:collapse; }
.jack-matrix th { padding:4px 8px; font-size:.65rem; color:var(--muted); writing-mode:vertical-lr; transform:rotate(180deg); text-align:left; max-width:30px; }
.jack-matrix td { padding:0; text-align:center; }
.jack-matrix .mx-cell { width:24px; height:24px; border:1px solid var(--border2); cursor:pointer; display:inline-block; border-radius:3px; transition:background .15s; }
.jack-matrix .mx-cell:hover { border-color:var(--teal); }
.jack-matrix .mx-cell.connected { background:var(--teal); box-shadow:0 0 6px var(--teal-glow); }
.jack-matrix .mx-row-label { font-size:.65rem; color:var(--text-dim); font-family:var(--font-mono); padding:4px 8px; text-align:right; white-space:nowrap; }
</style>

<div class="jack-page">
  <div class="jack-header">
    <h2><i class="fa-solid fa-plug" style="color:var(--teal);margin-right:8px"></i>JACK Audio Routing</h2>
  </div>

  <!-- Status Panel -->
  <div class="jack-status">
    <div class="jack-stat">
      <div class="jack-stat-label">Daemon</div>
      <div class="jack-stat-val" id="jk-daemon">—</div>
    </div>
    <div class="jack-stat">
      <div class="jack-stat-label">Client</div>
      <div class="jack-stat-val" id="jk-client">—</div>
    </div>
    <div class="jack-stat">
      <div class="jack-stat-label">Driver</div>
      <div class="jack-stat-val" id="jk-driver">—</div>
    </div>
    <div class="jack-stat">
      <div class="jack-stat-label">Sample Rate</div>
      <div class="jack-stat-val" id="jk-sr">—</div>
    </div>
    <div class="jack-stat">
      <div class="jack-stat-label">Buffer Size</div>
      <div class="jack-stat-val" id="jk-bs">—</div>
    </div>
    <div class="jack-stat">
      <div class="jack-stat-label">Cables</div>
      <div class="jack-stat-val" id="jk-cables">—</div>
    </div>
  </div>

  <!-- Controls -->
  <div class="jack-controls">
    <select id="jk-drv-sel">
      <option value="dummy">Dummy (Headless)</option>
      <option value="alsa">ALSA (Desktop)</option>
    </select>
    <input type="number" id="jk-sr-in" value="44100" min="22050" max="192000" step="100" style="width:90px" title="Sample Rate">
    <input type="number" id="jk-bs-in" value="1024" min="64" max="8192" step="64" style="width:80px" title="Buffer Size">
    <input type="number" id="jk-cab-in" value="12" min="1" max="64" step="1" style="width:60px" title="Cables">
    <button class="jack-btn jack-btn-start" onclick="startJack()"><i class="fa-solid fa-play"></i> Start JACK</button>
    <button class="jack-btn jack-btn-stop" onclick="stopJack()"><i class="fa-solid fa-stop"></i> Stop</button>
    <button class="jack-btn jack-btn-cable" onclick="addCable()"><i class="fa-solid fa-plus"></i> Add Cable</button>
  </div>

  <!-- Virtual Audio Cables -->
  <div class="jack-section">
    <h3><i class="fa-solid fa-cable-car" style="color:var(--cyan);margin-right:6px"></i>Virtual Audio Cables</h3>
    <table class="jack-table">
      <thead><tr><th>ID</th><th>Capture Port</th><th>Playback Port</th><th></th></tr></thead>
      <tbody id="jk-cable-body"><tr><td colspan="4" style="color:var(--muted);text-align:center">No cables — start JACK first</td></tr></tbody>
    </table>
  </div>

  <!-- Port Connection Matrix -->
  <div class="jack-section">
    <h3><i class="fa-solid fa-diagram-project" style="color:var(--cyan);margin-right:6px"></i>Port Connection Matrix</h3>
    <div class="jack-matrix" id="jk-matrix">
      <p style="color:var(--muted);font-size:.85rem;text-align:center;">Start JACK to see available ports</p>
    </div>
  </div>
</div>

<script>
var jackPorts = [];
var jackConnections = [];

function pollJackStatus() {
  mc1Api('GET', '/api/v1/jack/status').then(function(d) {
    if (!d || !d.ok) return;
    var de = document.getElementById('jk-daemon');
    de.textContent = d.daemon_running ? 'Running' : 'Stopped';
    de.className = 'jack-stat-val ' + (d.daemon_running ? 'online' : 'offline');
    var ce = document.getElementById('jk-client');
    ce.textContent = d.client_connected ? 'Connected' : 'Disconnected';
    ce.className = 'jack-stat-val ' + (d.client_connected ? 'online' : 'offline');
    document.getElementById('jk-driver').textContent = d.driver || '—';
    document.getElementById('jk-sr').textContent = d.sample_rate || '—';
    document.getElementById('jk-bs').textContent = d.buffer_size || '—';
    document.getElementById('jk-cables').textContent = d.cable_count || 0;
    if (d.client_connected) { loadCables(); loadPorts(); }
  }).catch(function(){});
}

function startJack() {
  var driver = document.getElementById('jk-drv-sel').value;
  var sr = parseInt(document.getElementById('jk-sr-in').value) || 44100;
  var bs = parseInt(document.getElementById('jk-bs-in').value) || 1024;
  var cab = parseInt(document.getElementById('jk-cab-in').value) || 12;
  mc1Api('POST', '/api/v1/jack/start', {driver:driver, sample_rate:sr, buffer_size:bs, cables:cab}).then(function(d) {
    if (d && d.ok) {
      mc1Toast('JACK started: ' + (d.cable_count||0) + ' cables @ ' + (d.sample_rate||sr) + ' Hz', 'ok');
      pollJackStatus();
    } else {
      mc1Toast((d && d.error) || 'Start failed', 'err');
    }
  }).catch(function(){ mc1Toast('API offline', 'err'); });
}

function stopJack() {
  mc1Api('POST', '/api/v1/jack/stop').then(function(d) {
    if (d && d.ok) { mc1Toast('JACK stopped', 'ok'); pollJackStatus(); }
  });
}

function addCable() {
  mc1Api('POST', '/api/v1/jack/cables').then(function(d) {
    if (d && d.ok) { mc1Toast('Cable #' + d.cable_id + ' created', 'ok'); loadCables(); }
    else mc1Toast('Create failed', 'err');
  });
}

function removeCable(id) {
  mc1Api('DELETE', '/api/v1/jack/cables/' + id).then(function(d) {
    if (d && d.ok) { mc1Toast('Cable removed', 'ok'); loadCables(); }
  });
}

function loadCables() {
  mc1Api('GET', '/api/v1/jack/cables').then(function(d) {
    if (!d || !d.ok) return;
    var tb = document.getElementById('jk-cable-body');
    var cables = d.cables || [];
    if (cables.length === 0) {
      tb.innerHTML = '<tr><td colspan="4" style="color:var(--muted);text-align:center">No cables</td></tr>';
      return;
    }
    tb.innerHTML = '';
    cables.forEach(function(c) {
      var tr = document.createElement('tr');
      tr.innerHTML = '<td>' + c.id + '</td><td class="mono">' + c.capture + '</td><td class="mono">' + c.playback + '</td>' +
        '<td><button class="jack-del" onclick="removeCable(' + c.id + ')"><i class="fa-solid fa-trash-can"></i></button></td>';
      tb.appendChild(tr);
    });
  });
}

function loadPorts() {
  mc1Api('GET', '/api/v1/jack/ports').then(function(d) {
    if (!d || !d.ok) return;
    jackPorts = d.ports || [];
    renderMatrix();
  });
}

function renderMatrix() {
  var outputs = jackPorts.filter(function(p){ return p.is_output; });
  var inputs  = jackPorts.filter(function(p){ return p.is_input; });
  if (outputs.length === 0 || inputs.length === 0) {
    document.getElementById('jk-matrix').innerHTML = '<p style="color:var(--muted);font-size:.85rem;text-align:center;">No ports available</p>';
    return;
  }

  var html = '<table><thead><tr><th></th>';
  inputs.forEach(function(p) {
    var short = p.name.split(':').pop() || p.name;
    html += '<th title="' + p.name + '">' + short + '</th>';
  });
  html += '</tr></thead><tbody>';

  outputs.forEach(function(out) {
    var shortOut = out.name.split(':').pop() || out.name;
    html += '<tr><td class="mx-row-label" title="' + out.name + '">' + shortOut + '</td>';
    inputs.forEach(function(inp) {
      var esc_src = out.name.replace(/'/g, "\\'");
      var esc_dst = inp.name.replace(/'/g, "\\'");
      html += '<td><span class="mx-cell" data-src="' + out.name + '" data-dst="' + inp.name + '" ' +
        'onclick="toggleConnection(\'' + esc_src + '\',\'' + esc_dst + '\',this)" title="' + out.name + ' → ' + inp.name + '"></span></td>';
    });
    html += '</tr>';
  });
  html += '</tbody></table>';
  document.getElementById('jk-matrix').innerHTML = html;
}

function toggleConnection(src, dst, el) {
  var connected = el.classList.contains('connected');
  var action = connected ? '/api/v1/jack/disconnect' : '/api/v1/jack/connect';
  mc1Api('POST', action, {src:src, dst:dst}).then(function(d) {
    if (d && d.ok) {
      el.classList.toggle('connected');
      mc1Toast(connected ? 'Disconnected' : 'Connected', 'ok');
    }
  });
}
</script>

<script>
document.addEventListener('DOMContentLoaded', function() {
  pollJackStatus();
  setInterval(pollJackStatus, 5000);
});
</script>

<?php require_once __DIR__ . '/app/inc/footer.php'; ?>
