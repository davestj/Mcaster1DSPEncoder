<?php
define('MC1_BOOT', true);
require_once __DIR__ . '/app/inc/auth.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/user_auth.php';

$page_title = 'Dashboard';
$active_nav = 'dashboard';
$use_charts = true;

// Load encoder configs from MySQL — these are always available even if C++ is offline
$db_configs = [];
try {
    $db_configs = mc1_db('mcaster1_encoder')->query(
        'SELECT id, slot_id, name, input_type, playlist_path, codec, bitrate_kbps,
                sample_rate, channels, server_host, server_port, server_mount,
                station_name, genre, volume, shuffle, repeat_all, is_active
         FROM encoder_configs
         ORDER BY slot_id'
    )->fetchAll();
} catch (Exception $e) {}

require_once __DIR__ . '/app/inc/header.php';
?>

<div class="sec-hdr">
  <div class="sec-title"><i class="fa-solid fa-gauge" style="color:var(--teal);margin-right:8px"></i>Live Overview</div>
  <div style="display:flex;gap:8px;align-items:center;flex-wrap:wrap">
    <span id="uptime-badge" class="badge badge-gray">Up: —</span>
    <span id="ver-badge" class="badge badge-teal">v—</span>
    <span id="listeners-badge" class="badge badge-blue">— listeners</span>
    <span id="api-status" class="badge badge-gray" title="C++ API connection status">API: —</span>
    <span id="poll-dot" style="width:7px;height:7px;border-radius:50%;background:var(--muted);display:inline-block;transition:background .2s"></span>
  </div>
</div>

<!-- Encoder slot cards — rendered from MySQL on page load, overlaid with live C++ state -->
<div style="display:flex;flex-direction:column;gap:12px" id="slots-grid">
  <?php if (empty($db_configs)): ?>
  <div style="grid-column:1/-1" class="card">
    <div class="empty">
      <i class="fa-solid fa-circle-exclamation" style="color:var(--yellow)"></i>
      <p>No encoder slots configured. <a href="/settings.php">Add encoder configs in Settings.</a></p>
    </div>
  </div>
  <?php else: foreach ($db_configs as $idx => $cfg): ?>
  <div class="slot-card idle" id="sc-<?= (int)$cfg['slot_id'] ?>">
    <div class="slot-hdr">
      <div class="slot-num" style="color:<?= ['#14b8a6','#0891b2','#a78bfa'][$idx] ?? '#14b8a6' ?>;border:1px solid <?= ['#14b8a630','#0891b230','#a78bfa30'][$idx] ?? '#14b8a630' ?>">
        <?= (int)$cfg['slot_id'] ?>
      </div>
      <div class="slot-name"><?= h($cfg['name']) ?></div>
      <span class="state-badge idle" id="sb-<?= (int)$cfg['slot_id'] ?>">OFFLINE</span>
      <button class="btn btn-icon btn-xs" title="View stats" onclick="showSlotStats(<?= (int)$cfg['slot_id'] ?>)" style="margin-left:auto;flex-shrink:0">
        <i class="fa-solid fa-chart-line"></i>
      </button>
    </div>
    <div class="track-line" id="tl-<?= (int)$cfg['slot_id'] ?>">
      <span style="color:var(--muted);font-size:12px">
        <?= h($cfg['server_host'] . ':' . $cfg['server_port'] . $cfg['server_mount']) ?>
      </span>
    </div>
    <div style="display:flex;align-items:center;gap:8px;margin-bottom:10px">
      <span id="pe-<?= (int)$cfg['slot_id'] ?>" style="font-size:10px;color:var(--muted);font-variant-numeric:tabular-nums;min-width:28px">0:00</span>
      <div class="prog-wrap" style="flex:1;margin-bottom:0"><div class="prog-bar" id="pb-<?= (int)$cfg['slot_id'] ?>" style="width:0"></div></div>
      <span id="td-<?= (int)$cfg['slot_id'] ?>" style="font-size:10px;color:var(--muted);font-variant-numeric:tabular-nums;min-width:28px;text-align:right"></span>
    </div>
    <div class="slot-meta">
      <span><i class="fa-solid fa-compact-disc fa-fw" style="opacity:.5"></i> <?= h($cfg['codec'] ?? 'mp3') ?></span>
      <?php if ($cfg['bitrate_kbps']): ?>
      <span><?= (int)$cfg['bitrate_kbps'] ?>kbps</span>
      <?php endif; ?>
      <span><i class="fa-solid fa-link fa-fw" style="opacity:.5"></i> <?= h($cfg['server_mount'] ?? '—') ?></span>
      <span id="sm-up-<?= (int)$cfg['slot_id'] ?>"></span>
    </div>
    <div class="slot-acts" id="sa-<?= (int)$cfg['slot_id'] ?>">
      <button class="btn btn-success btn-sm" onclick="slotAct(<?= (int)$cfg['slot_id'] ?>, 'start')">
        <i class="fa-solid fa-play"></i> Start
      </button>
      <a href="/encoders.php" class="btn btn-icon btn-sm" title="DSP controls">
        <i class="fa-solid fa-sliders"></i>
      </a>
    </div>
    <div class="range-wrap" style="margin-top:10px;padding-top:8px;border-top:1px solid rgba(51,65,85,.4)">
      <i class="fa-solid fa-volume-low" style="color:var(--muted);font-size:11px"></i>
      <input type="range" min="0" max="100" value="<?= (int)($cfg['volume'] * 100) ?>"
             oninput="setVol(<?= (int)$cfg['slot_id'] ?>, this)"
             onchange="setVol(<?= (int)$cfg['slot_id'] ?>, this)">
      <span id="vv-<?= (int)$cfg['slot_id'] ?>" style="font-size:11px;color:var(--muted);min-width:26px">
        <?= (int)($cfg['volume'] * 100) ?>%
      </span>
    </div>
  </div>
  <?php endforeach; endif; ?>
</div>

<!-- Bandwidth chart -->
<div class="card">
  <div class="card-hdr">
    <div class="card-title"><i class="fa-solid fa-chart-area fa-fw"></i> Live Bandwidth</div>
    <span class="badge badge-gray" id="total-bw">—</span>
  </div>
  <div class="chart-wrap"><canvas id="bw-chart"></canvas></div>
</div>

<!-- Slot Stats Modal -->
<div id="stats-modal" style="display:none;position:fixed;inset:0;z-index:800;background:rgba(1,11,15,.78);backdrop-filter:blur(4px);align-items:center;justify-content:center">
  <div style="background:var(--card);border:1px solid var(--border2);border-radius:var(--radius);width:min(640px,96vw);max-height:85vh;overflow-y:auto;box-shadow:0 8px 40px rgba(0,0,0,.6)">
    <div style="display:flex;align-items:center;justify-content:space-between;padding:16px 20px;border-bottom:1px solid var(--border)">
      <div style="font-weight:700;font-size:15px;color:var(--text)" id="sm-title">Slot Stats</div>
      <button class="btn btn-icon btn-sm" onclick="closeStatsModal()"><i class="fa-solid fa-xmark"></i></button>
    </div>
    <div style="padding:20px" id="sm-body">
      <div class="empty"><div class="spinner"></div></div>
    </div>
  </div>
</div>

<script>
(function(){
var POLL_MS = 5000, MAX_PTS = 30;
var prevBytes = {}, bwHist = {}, chartInst = null, chartLabels = [];
var SLOT_COLORS = ['#14b8a6','#0891b2','#a78bfa'];
var apiOnline = false;

// Per-slot interpolation state for smooth real-time progress bars.
// Updated on every poll; requestAnimationFrame loop advances bars between polls.
var slotLiveState = {}; // slot_id → { pos_ms, dur_ms, poll_time, is_live, color }

// Init Chart.js bandwidth chart
(function(){
  var ctx = document.getElementById('bw-chart');
  if (!ctx || !window.Chart) return;
  chartInst = new Chart(ctx, {
    type: 'line',
    data: { labels: chartLabels, datasets: [] },
    options: {
      responsive: true, maintainAspectRatio: false, animation: { duration: 0 },
      interaction: { mode: 'index', intersect: false },
      scales: {
        x: { display: false },
        y: { grid: { color: 'rgba(51,65,85,.5)' }, ticks: { color: '#64748b', font: { size: 11 } }, beginAtZero: true,
             callback: function(v){ return v>=1024 ? (v/1024).toFixed(0)+'k' : v; } }
      },
      plugins: { legend: { labels: { color: '#94a3b8', font: { size: 11 } } } }
    }
  });
})();

function fmtBw(b) {
  if (b >= 1048576) return (b/1048576).toFixed(1)+' MB/s';
  if (b >= 1024) return (b/1024).toFixed(1)+' KB/s';
  return b+' B/s';
}
function fmtUp(s) {
  if (!s) return '';
  var h=Math.floor(s/3600), m=Math.floor((s%3600)/60), ss=s%60;
  if (h>0) return h+'h '+m+'m';
  if (m>0) return m+'m '+ss+'s';
  return ss+'s';
}
function stateClass(state) {
  return ({'live':'live','idle':'idle','connecting':'connecting','reconnecting':'connecting','error':'error','stopping':'idle','starting':'connecting'})[(state||'').toLowerCase()]||'idle';
}
function esc(s){ return String(s||'').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;'); }

function updateApiStatus(online) {
  apiOnline = online;
  var el = document.getElementById('api-status');
  if (!el) return;
  el.textContent = online ? 'API: Online' : 'API: Offline';
  el.className = 'badge ' + (online ? 'badge-green' : 'badge-red');
}

// Update an existing slot card in-place from live C++ data
function updateSlotLive(enc, idx) {
  var id = enc.slot_id;
  var card = document.getElementById('sc-'+id);
  if (!card) return;

  var cls = stateClass(enc.state);
  var color = SLOT_COLORS[idx % SLOT_COLORS.length];
  var isLive = cls === 'live';
  var track = [enc.track_title, enc.track_artist].filter(Boolean).join(' \u2014 ') || 'No track loaded';

  // Update card class
  card.className = 'slot-card ' + cls;

  // Update state badge
  var sb = document.getElementById('sb-'+id);
  if (sb) { sb.textContent = (enc.state||'idle').toUpperCase(); sb.className = 'state-badge '+cls; }

  // Update track line
  var tl = document.getElementById('tl-'+id);
  if (tl) tl.textContent = track;

  // Seed interpolation state from this poll so rAF can smoothly advance the bar.
  // Disable CSS transition on the bar — the rAF loop provides the animation.
  var pb = document.getElementById('pb-'+id);
  if (pb) { pb.style.background = color; pb.style.transition = 'none'; }
  slotLiveState[id] = {
    pos_ms:    enc.position_ms  || 0,
    dur_ms:    enc.duration_ms  || 0,
    poll_time: Date.now(),
    is_live:   isLive,
    color:     color
  };
  // If not live, snap bar to 0 immediately (rAF won't advance it)
  if (!isLive && pb) pb.style.width = '0%';

  // Update time display (rAF keeps pos updating; duration is static per track)
  var td = document.getElementById('td-'+id);
  if (td && enc.duration_ms) td.textContent = fmtDur(enc.duration_ms);
  else if (td) td.textContent = '';

  // Update uptime
  var up = document.getElementById('sm-up-'+id);
  if (up && enc.uptime_sec) up.innerHTML = '<i class="fa-regular fa-clock fa-fw" style="opacity:.5"></i> '+fmtUp(enc.uptime_sec);
  else if (up) up.innerHTML = '';

  // Update action buttons
  var sa = document.getElementById('sa-'+id);
  if (sa) {
    if (enc.is_live || cls === 'live' || cls === 'connecting') {
      sa.innerHTML = '<button class="btn btn-danger btn-sm" onclick="slotAct('+id+',\'stop\')">'
        +'<i class="fa-solid fa-stop"></i> Stop</button>'
        +'<button class="btn btn-secondary btn-sm" onclick="slotAct('+id+',\'restart\')">'
        +'<i class="fa-solid fa-rotate-right"></i></button>'
        +'<a href="/encoders.php" class="btn btn-icon btn-sm" title="DSP controls"><i class="fa-solid fa-sliders"></i></a>';
    } else {
      sa.innerHTML = '<button class="btn btn-success btn-sm" onclick="slotAct('+id+',\'start\')">'
        +'<i class="fa-solid fa-play"></i> Start</button>'
        +'<a href="/encoders.php" class="btn btn-icon btn-sm" title="DSP controls"><i class="fa-solid fa-sliders"></i></a>';
    }
  }
}

window.slotAct = function(slot, action) {
  mc1Api('POST', '/api/v1/encoders/'+slot+'/'+action).then(function(d){
    if (d && d.ok === false) {
      mc1Toast('Slot '+slot+' '+action+' error: '+(d.error||'unknown'), 'err');
    } else {
      mc1Toast('Slot '+slot+' '+action, 'ok');
    }
    setTimeout(poll, 800);
  }).catch(function(){ mc1Toast('Encoder API unreachable — is the encoder running?','err'); });
};

window.setVol = function(slot, inp) {
  var v = parseInt(inp.value);
  var el = document.getElementById('vv-'+slot);
  if (el) el.textContent = v+'%';
  mc1Api('PUT', '/api/v1/volume', {slot: slot, volume: v/100}).catch(function(){});
};

function pollStatus() {
  mc1Api('GET', '/api/v1/status').then(function(d){
    if (!d || d.ok === false) { updateApiStatus(false); return; }
    updateApiStatus(true);
    var ub = document.getElementById('uptime-badge');
    var vb = document.getElementById('ver-badge');
    if (ub && d.uptime)   ub.textContent = 'Up: '+d.uptime;
    if (vb && d.version)  vb.textContent = 'v'+d.version;
  }).catch(function(){ updateApiStatus(false); });

  mc1Api('GET', '/api/v1/dnas/stats').then(function(d){
    var lb = document.getElementById('listeners-badge');
    if (!lb || !d) return;
    var n = 0;
    if (d.body) {
      var m = d.body.match(/<listeners>(\d+)<\/listeners>/i);
      if (m) n = parseInt(m[1]);
    }
    lb.textContent = n+' listener'+(n===1?'':'s');
  }).catch(function(){});
}

function poll() {
  var dot = document.getElementById('poll-dot');
  if (dot) dot.style.background = 'var(--teal)';

  mc1Api('GET', '/api/v1/encoders').then(function(encoders){
    if (dot) setTimeout(function(){ dot.style.background = 'var(--muted)'; }, 400);
    if (!Array.isArray(encoders) || encoders.length === 0) {
      updateApiStatus(false);
      return;
    }
    updateApiStatus(true);

    var lbl = new Date().toLocaleTimeString([],{hour:'2-digit',minute:'2-digit',second:'2-digit'});
    chartLabels.push(lbl);
    if (chartLabels.length > MAX_PTS) chartLabels.shift();

    var totalBw = 0;
    encoders.forEach(function(enc, idx){
      updateSlotLive(enc, idx);

      // Bandwidth calculation
      var id = enc.slot_id;
      var prev = prevBytes[id] || 0;
      var bw = (enc.bytes_sent > prev) ? Math.round((enc.bytes_sent - prev) / POLL_MS * 1000) : 0;
      prevBytes[id] = enc.bytes_sent || 0;
      bwHist[id] = bwHist[id] || [];
      bwHist[id].push(bw);
      if (bwHist[id].length > MAX_PTS) bwHist[id].shift();
      totalBw += bw;

      if (chartInst) {
        if (!chartInst.data.datasets[idx]) {
          chartInst.data.datasets.push({
            label: enc.name || 'Slot '+id,
            data: [],
            borderColor: SLOT_COLORS[idx] || '#14b8a6',
            backgroundColor: 'rgba(20,184,166,.08)',
            tension: .4, fill: true, pointRadius: 0, borderWidth: 2
          });
        }
        chartInst.data.datasets[idx].data = (bwHist[id] || []).slice();
      }
    });

    var tbw = document.getElementById('total-bw');
    if (tbw) tbw.textContent = fmtBw(totalBw);
    if (chartInst) { chartInst.data.labels = chartLabels.slice(); chartInst.update(); }

  }).catch(function(){
    if (dot) dot.style.background = 'var(--red)';
    updateApiStatus(false);
  });
}

// Defer until DOMContentLoaded so footer.php's mc1Api is defined before we call it.
// (The dashboard <script> block executes before footer.php's <script>, so calling
// poll() immediately here would throw "mc1Api is not a function" and kill setInterval.)
document.addEventListener('DOMContentLoaded', function() {
  poll();
  pollStatus();
  setInterval(poll, POLL_MS);
  setInterval(pollStatus, 30000);
  requestAnimationFrame(tickProgress); // start smooth progress bar animation
});

// ── Slot Stats Modal ──────────────────────────────────────────────────
window.showSlotStats = function(slotId) {
  var modal = document.getElementById('stats-modal');
  var title = document.getElementById('sm-title');
  var body  = document.getElementById('sm-body');
  modal.style.display = 'flex';
  title.textContent = 'Slot ' + slotId + ' — Stats';
  body.innerHTML = '<div class="empty"><div class="spinner"></div></div>';

  mc1Api('GET', '/api/v1/encoders/' + slotId + '/stats').then(function(d) {
    if (!d || d.ok === false) {
      body.innerHTML = '<div class="alert alert-error"><i class="fa-solid fa-xmark"></i> ' + esc(d && d.error ? d.error : 'C++ API offline') + '</div>';
      return;
    }
    var cls   = stateClass(d.state || 'idle');
    var clsLabel = {'live':'Live','idle':'Idle','connecting':'Connecting','error':'Error'}[cls] || 'Idle';
    var track = [d.track_title, d.track_artist].filter(Boolean).join(' \u2014 ') || '—';
    var pct   = (d.duration_ms && d.position_ms) ? Math.min(100, Math.round(d.position_ms / d.duration_ms * 100)) : 0;
    var bwMb  = d.bytes_sent > 0 ? (d.bytes_sent / 1048576).toFixed(1) + ' MB' : '—';
    var posStr = d.position_ms ? fmtDur(d.position_ms) : '—';
    var durStr = d.duration_ms ? fmtDur(d.duration_ms) : '—';

    body.innerHTML =
      '<div style="display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:18px">'
      + statBox('fa-tower-broadcast', 'State',    '<span class="state-badge '+cls+'">'+clsLabel+'</span>', 'stat-icon-teal')
      + statBox('fa-clock',           'Uptime',   d.uptime_sec ? fmtUp(d.uptime_sec) : '—',               'stat-icon-cyan')
      + statBox('fa-arrow-up',        'Sent',     bwMb,                                                    'stat-icon-green')
      + statBox('fa-users',           'Listeners',d.listeners !== undefined ? d.listeners : '—',           'stat-icon-yellow')
      + '</div>'
      + '<div style="background:var(--bg2);border:1px solid var(--border);border-radius:var(--radius-sm);padding:14px;margin-bottom:14px">'
      +   '<div style="font-size:11px;color:var(--muted);text-transform:uppercase;letter-spacing:.06em;margin-bottom:8px">Now Playing</div>'
      +   '<div style="font-size:13px;color:var(--text);margin-bottom:6px">'+esc(track)+'</div>'
      +   '<div style="display:flex;align-items:center;gap:10px;margin-top:8px">'
      +     '<div style="flex:1;height:4px;background:rgba(255,255,255,.08);border-radius:2px"><div style="width:'+pct+'%;height:100%;background:var(--teal);border-radius:2px"></div></div>'
      +     '<span style="font-size:11px;color:var(--muted);font-variant-numeric:tabular-nums">'+posStr+' / '+durStr+'</span>'
      +   '</div>'
      + '</div>'
      + '<div style="display:flex;gap:8px;flex-wrap:wrap">'
      + '<a href="/encoders.php" class="btn btn-secondary btn-sm"><i class="fa-solid fa-sliders"></i> DSP Controls</a>'
      + (cls === 'live' || cls === 'connecting'
          ? '<button class="btn btn-danger btn-sm" onclick="slotAct('+slotId+',\'stop\');closeStatsModal()"><i class="fa-solid fa-stop"></i> Stop</button>'
            + '<button class="btn btn-secondary btn-sm" onclick="slotAct('+slotId+',\'restart\');closeStatsModal()"><i class="fa-solid fa-rotate-right"></i> Restart</button>'
          : '<button class="btn btn-success btn-sm" onclick="slotAct('+slotId+',\'start\');closeStatsModal()"><i class="fa-solid fa-play"></i> Start</button>')
      + '</div>';
  }).catch(function() {
    body.innerHTML = '<div class="alert alert-error"><i class="fa-solid fa-xmark"></i> API unreachable</div>';
  });
};

function statBox(icon, label, val, iconCls) {
  return '<div style="background:var(--bg2);border:1px solid var(--border);border-radius:var(--radius-sm);padding:14px;display:flex;gap:12px;align-items:center">'
    + '<div class="stat-icon '+iconCls+'"><i class="fa-solid '+icon+'"></i></div>'
    + '<div><div style="font-size:18px;font-weight:700;color:var(--text);line-height:1.2">'+val+'</div>'
    + '<div style="font-size:11px;color:var(--muted);margin-top:3px;text-transform:uppercase;letter-spacing:.05em">'+label+'</div></div>'
    + '</div>';
}

function fmtDur(ms){ var s=Math.floor(ms/1000),m=Math.floor(s/60),ss=s%60; return m+':'+(ss<10?'0':'')+ss; }
function fmtPos(ms){ var s=Math.floor(ms/1000),m=Math.floor(s/60),ss=s%60; return m+':'+(ss<10?'0':'')+ss; }

// requestAnimationFrame loop — smoothly advances every live slot's progress bar
// and elapsed time display between API polls (no extra server requests).
function tickProgress() {
  var now = Date.now();
  for (var sid in slotLiveState) {
    var s = slotLiveState[sid];
    if (!s.is_live || !s.dur_ms) continue;
    var est_ms = s.pos_ms + (now - s.poll_time);
    if (est_ms > s.dur_ms) est_ms = s.dur_ms;
    var pct = (est_ms / s.dur_ms * 100).toFixed(3);

    var pb = document.getElementById('pb-' + sid);
    if (pb) pb.style.width = pct + '%';

    // Elapsed / total time display  e.g.  2:14 / 4:46
    var pe = document.getElementById('pe-' + sid);
    if (pe) pe.textContent = fmtPos(est_ms);
  }
  requestAnimationFrame(tickProgress);
}

window.closeStatsModal = function() {
  document.getElementById('stats-modal').style.display = 'none';
};

document.getElementById('stats-modal').addEventListener('click', function(e) {
  if (e.target === this) closeStatsModal();
});

})();
</script>

<?php require_once __DIR__ . '/app/inc/footer.php'; ?>
