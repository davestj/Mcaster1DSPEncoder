<?php
// metrics.php — 5-tab metrics dashboard
// Tabs: System Health | Encoder Slots | Streaming Servers | Listener Analytics | Event Log
//
// Note: No exit()/die() — uopz active. Live data (CPU/mem/slots/servers) is fetched
// by browser JS directly from C++ /api/v1/ endpoints. PHP loads only historical DB data.

define('MC1_BOOT', true);
require_once __DIR__ . '/app/inc/auth.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/user_auth.php';
require_once __DIR__ . '/app/inc/metrics.datacalculate.class.php';

$page_title = 'Metrics Dashboard';
$active_nav = 'metrics';
$use_charts = true;

// Pre-load data for Listener Analytics tab (DB queries at page render time)
$summary    = Mc1MetricsCalculator::getListenerSummary(30);
$daily      = Mc1MetricsCalculator::getDailyListenerTrend(30);
$mounts     = Mc1MetricsCalculator::getMountBreakdown(30);
$top_tracks = Mc1MetricsCalculator::getTopTracks(20);
$recent     = Mc1MetricsCalculator::getRecentSessions(50);

require_once __DIR__ . '/app/inc/header.php';
?>

<style>
.tab-bar { display:flex; gap:4px; margin-bottom:16px; border-bottom:1px solid rgba(51,65,85,.5); }
.tab-btn { background:none; border:none; color:var(--muted); padding:10px 16px; cursor:pointer; font-size:13px; border-bottom:2px solid transparent; transition:.15s; white-space:nowrap; }
.tab-btn:hover { color:var(--text); }
.tab-btn.active { color:var(--teal); border-bottom-color:var(--teal); }
.tab-panel { display:none; }
.tab-panel.active { display:block; }
.gauge-grid { display:grid; grid-template-columns:repeat(4,1fr); gap:12px; margin-bottom:16px; }
.gauge-card { background:var(--card); border:1px solid var(--border); border-radius:10px; padding:16px; }
.gauge-label { font-size:11px; color:var(--muted); margin-bottom:6px; }
.gauge-val { font-size:26px; font-weight:700; color:var(--text); line-height:1; }
.gauge-bar { height:4px; background:var(--border); border-radius:2px; margin-top:8px; }
.gauge-fill { height:4px; border-radius:2px; transition:.4s; }
.gauge-sub { font-size:10px; color:var(--muted); margin-top:4px; }
.slot-grid { display:grid; grid-template-columns:repeat(auto-fill,minmax(280px,1fr)); gap:12px; }
.slot-card { background:var(--card); border:1px solid var(--border); border-radius:10px; padding:14px; }
.slot-card.live { border-color:#14b8a6; }
.slot-card.reconnecting { border-color:#f97316; }
.slot-card.error { border-color:#ef4444; }
.slot-card.sleep { border-color:#eab308; }
.srv-table { width:100%; border-collapse:collapse; }
.srv-table th { font-size:11px; color:var(--muted); text-align:left; padding:8px 10px; border-bottom:1px solid var(--border); }
.srv-table td { font-size:12px; padding:8px 10px; border-bottom:1px solid rgba(51,65,85,.3); vertical-align:middle; }
.dot { width:8px; height:8px; border-radius:50%; display:inline-block; margin-right:5px; }
.dot.online { background:#22c55e; }
.dot.offline { background:#ef4444; }
.dot.unknown { background:#64748b; }
.ev-badge { display:inline-block; padding:1px 7px; border-radius:4px; font-size:11px; font-weight:600; }
.ev-start { background:rgba(20,184,166,.2); color:#14b8a6; }
.ev-stop  { background:rgba(100,116,139,.2); color:#94a3b8; }
.ev-reconnect { background:rgba(249,115,22,.2); color:#f97316; }
.ev-error { background:rgba(239,68,68,.2); color:#ef4444; }
.ev-metadata { background:rgba(96,165,250,.2); color:#60a5fa; }
</style>

<!-- Tab bar -->
<div class="tab-bar">
  <button class="tab-btn active" onclick="switchTab('health')"><i class="fa-solid fa-heartbeat fa-fw"></i> System Health</button>
  <button class="tab-btn"        onclick="switchTab('slots')"><i class="fa-solid fa-microchip fa-fw"></i> Encoder Slots</button>
  <button class="tab-btn"        onclick="switchTab('servers')"><i class="fa-solid fa-server fa-fw"></i> Streaming Servers</button>
  <button class="tab-btn"        onclick="switchTab('analytics')"><i class="fa-solid fa-chart-line fa-fw"></i> Listener Analytics</button>
  <button class="tab-btn"        onclick="switchTab('events')"><i class="fa-solid fa-list fa-fw"></i> Event Log</button>
</div>

<!-- ═══════════════════════════════════════════════════════════════════════ -->
<!-- TAB 1: System Health                                                    -->
<!-- ═══════════════════════════════════════════════════════════════════════ -->
<div id="tab-health" class="tab-panel active">

  <!-- Live gauges -->
  <div class="gauge-grid">
    <div class="gauge-card">
      <div class="gauge-label"><i class="fa-solid fa-microchip fa-fw"></i> CPU Usage</div>
      <div class="gauge-val"><span id="g-cpu">—</span><span style="font-size:14px;color:var(--muted)">%</span></div>
      <div class="gauge-bar"><div class="gauge-fill" id="g-cpu-bar" style="background:#14b8a6;width:0%"></div></div>
      <div class="gauge-sub" id="g-threads">threads: —</div>
    </div>
    <div class="gauge-card">
      <div class="gauge-label"><i class="fa-solid fa-memory fa-fw"></i> Memory</div>
      <div class="gauge-val"><span id="g-mem">—</span><span style="font-size:14px;color:var(--muted)">%</span></div>
      <div class="gauge-bar"><div class="gauge-fill" id="g-mem-bar" style="background:#0891b2;width:0%"></div></div>
      <div class="gauge-sub" id="g-mem-detail">— / — MB</div>
    </div>
    <div class="gauge-card">
      <div class="gauge-label"><i class="fa-solid fa-arrow-down fa-fw"></i> Net RX</div>
      <div class="gauge-val"><span id="g-rx">—</span><span style="font-size:14px;color:var(--muted)"> kbps</span></div>
      <div class="gauge-bar"><div class="gauge-fill" id="g-rx-bar" style="background:#a78bfa;width:0%"></div></div>
      <div class="gauge-sub" id="g-iface">iface: —</div>
    </div>
    <div class="gauge-card">
      <div class="gauge-label"><i class="fa-solid fa-arrow-up fa-fw"></i> Net TX</div>
      <div class="gauge-val"><span id="g-tx">—</span><span style="font-size:14px;color:var(--muted)"> kbps</span></div>
      <div class="gauge-bar"><div class="gauge-fill" id="g-tx-bar" style="background:#f97316;width:0%"></div></div>
      <div class="gauge-sub"><span id="g-sampled">—</span></div>
    </div>
  </div>

  <!-- CPU + Memory history chart -->
  <div class="card">
    <div class="card-hdr">
      <div class="card-title"><i class="fa-solid fa-chart-area fa-fw"></i> CPU &amp; Memory — Last 60 Minutes</div>
      <span class="badge badge-gray" id="h-chart-age">loading…</span>
    </div>
    <div class="chart-wrap"><canvas id="health-chart"></canvas></div>
  </div>
</div>

<!-- ═══════════════════════════════════════════════════════════════════════ -->
<!-- TAB 2: Encoder Slots                                                    -->
<!-- ═══════════════════════════════════════════════════════════════════════ -->
<div id="tab-slots" class="tab-panel">
  <div class="slot-grid" id="slot-cards">
    <div class="card" style="grid-column:1/-1;text-align:center;padding:32px">
      <i class="fa-solid fa-circle-notch fa-spin fa-2x" style="color:var(--teal)"></i>
      <p style="color:var(--muted);margin-top:12px">Loading slot data…</p>
    </div>
  </div>
</div>

<!-- ═══════════════════════════════════════════════════════════════════════ -->
<!-- TAB 3: Streaming Servers                                                -->
<!-- ═══════════════════════════════════════════════════════════════════════ -->
<div id="tab-servers" class="tab-panel">
  <div class="card-hdr" style="margin-bottom:12px">
    <div class="card-title"><i class="fa-solid fa-tower-broadcast fa-fw"></i> Streaming Server Status</div>
    <div style="display:flex;gap:8px;align-items:center">
      <span class="badge badge-gray" id="srv-age">not polled</span>
      <button class="btn btn-sm" onclick="forcePollAll()"><i class="fa-solid fa-rotate fa-fw"></i> Poll All</button>
    </div>
  </div>
  <div class="card">
    <div class="tbl-wrap">
      <table class="srv-table">
        <thead>
          <tr>
            <th>Name</th><th>Type</th><th>Host:Port</th><th>Status</th>
            <th>Listeners</th><th>kbps Out</th><th>Uptime</th><th>Last Polled</th><th></th>
          </tr>
        </thead>
        <tbody id="srv-tbody">
          <tr><td colspan="9" style="text-align:center;color:var(--muted);padding:24px">Loading…</td></tr>
        </tbody>
      </table>
    </div>
  </div>
  <div id="srv-mounts" style="margin-top:12px"></div>
</div>

<!-- ═══════════════════════════════════════════════════════════════════════ -->
<!-- TAB 4: Listener Analytics                                               -->
<!-- ═══════════════════════════════════════════════════════════════════════ -->
<div id="tab-analytics" class="tab-panel">

  <div class="sec-hdr">
    <div class="sec-title"><i class="fa-solid fa-chart-line" style="color:var(--teal);margin-right:8px"></i>Listener Analytics — Last 30 Days</div>
  </div>

  <!-- Summary cards -->
  <div class="card-grid grid-4">
    <div class="stat-card">
      <div class="stat-icon teal"><i class="fa-solid fa-users fa-fw"></i></div>
      <div class="stat-label">Total Sessions</div>
      <div class="stat-value"><?= number_format((int)($summary['total_sessions'] ?? 0)) ?></div>
      <div class="stat-sub">Last 30 days</div>
    </div>
    <div class="stat-card">
      <div class="stat-icon cyan"><i class="fa-solid fa-globe fa-fw"></i></div>
      <div class="stat-label">Unique IPs</div>
      <div class="stat-value"><?= number_format((int)($summary['unique_ips'] ?? 0)) ?></div>
      <div class="stat-sub">Distinct listeners</div>
    </div>
    <div class="stat-card">
      <div class="stat-icon green"><i class="fa-solid fa-clock fa-fw"></i></div>
      <div class="stat-label">Total Hours</div>
      <div class="stat-value"><?= number_format((float)($summary['total_hours'] ?? 0), 1) ?></div>
      <div class="stat-sub">Stream hours served</div>
    </div>
    <div class="stat-card">
      <div class="stat-icon orange"><i class="fa-solid fa-hourglass-half fa-fw"></i></div>
      <div class="stat-label">Avg Duration</div>
      <div class="stat-value"><?= number_format((float)($summary['avg_listen_min'] ?? 0), 1) ?></div>
      <div class="stat-sub">Minutes per session</div>
    </div>
  </div>

  <!-- Daily chart + Mount doughnut -->
  <div style="display:grid;grid-template-columns:2fr 1fr;gap:16px">
    <div class="card">
      <div class="card-hdr"><div class="card-title"><i class="fa-solid fa-chart-area fa-fw"></i> Daily Sessions</div></div>
      <div class="chart-wrap"><canvas id="daily-chart"></canvas></div>
    </div>
    <div class="card">
      <div class="card-hdr"><div class="card-title"><i class="fa-solid fa-chart-pie fa-fw"></i> By Mount</div></div>
      <div class="chart-wrap" style="height:200px"><canvas id="mount-chart"></canvas></div>
      <?php if (empty($mounts)): ?>
      <div class="empty" style="padding:16px"><i class="fa-solid fa-chart-pie fa-fw"></i><p>No data yet</p></div>
      <?php endif; ?>
    </div>
  </div>

  <!-- Top tracks -->
  <?php if (!empty($top_tracks)): ?>
  <div class="card">
    <div class="card-hdr"><div class="card-title"><i class="fa-solid fa-chart-bar fa-fw"></i> Top Tracks by Play Count</div></div>
    <div class="chart-wrap" style="height:280px"><canvas id="tracks-chart"></canvas></div>
  </div>
  <?php endif; ?>

  <!-- Recent sessions -->
  <div class="card">
    <div class="card-hdr"><div class="card-title"><i class="fa-solid fa-table fa-fw"></i> Recent Sessions</div></div>
    <?php if (empty($recent)): ?>
    <div class="empty"><i class="fa-solid fa-table fa-fw"></i><p>No sessions recorded yet.<br>Listener sessions are populated by the server relay monitor when streams go live.</p></div>
    <?php else: ?>
    <div class="tbl-wrap">
      <table>
        <thead><tr><th>Mount</th><th>IP</th><th>Duration</th><th>Bytes</th><th>Connected</th></tr></thead>
        <tbody>
          <?php foreach ($recent as $s): ?>
          <tr>
            <td><span class="badge badge-teal"><?= h($s['stream_mount']) ?></span></td>
            <td class="td-mono"><?= h($s['client_ip']) ?></td>
            <td><?= $s['duration_sec'] ? round($s['duration_sec'] / 60, 1) . 'm' : '—' ?></td>
            <td><?= $s['bytes_sent'] ? round($s['bytes_sent'] / 1024 / 1024, 2) . ' MB' : '—' ?></td>
            <td style="font-size:11px;color:var(--muted)"><?= h($s['connected_at']) ?></td>
          </tr>
          <?php endforeach; ?>
        </tbody>
      </table>
    </div>
    <?php endif; ?>
  </div>
</div>

<!-- ═══════════════════════════════════════════════════════════════════════ -->
<!-- TAB 5: Event Log                                                        -->
<!-- ═══════════════════════════════════════════════════════════════════════ -->
<div id="tab-events" class="tab-panel">
  <div class="card-hdr" style="margin-bottom:12px">
    <div class="card-title"><i class="fa-solid fa-list fa-fw"></i> Stream Event Log</div>
    <div style="display:flex;gap:8px;align-items:center">
      <select id="ev-slot" class="form-ctrl" style="padding:4px 8px;font-size:12px" onchange="loadEvents()">
        <option value="">All Slots</option>
        <?php
        try {
            $cfgs = mc1_db('mcaster1_encoder')->query(
                "SELECT slot_id, name FROM encoder_configs WHERE is_active=1 ORDER BY slot_id"
            )->fetchAll();
            foreach ($cfgs as $c) {
                echo '<option value="' . (int)$c['slot_id'] . '">Slot ' . (int)$c['slot_id'] . ': ' . h($c['name']) . '</option>';
            }
        } catch (Exception $e) {}
        ?>
      </select>
      <select id="ev-type" class="form-ctrl" style="padding:4px 8px;font-size:12px" onchange="loadEvents()">
        <option value="">All Types</option>
        <option value="start">start</option>
        <option value="stop">stop</option>
        <option value="reconnect">reconnect</option>
        <option value="error">error</option>
        <option value="metadata">metadata</option>
      </select>
      <button class="btn btn-sm" onclick="loadEvents()"><i class="fa-solid fa-rotate fa-fw"></i> Refresh</button>
    </div>
  </div>
  <div class="card">
    <div class="tbl-wrap">
      <table>
        <thead><tr><th>Time</th><th>Slot</th><th>Event</th><th>Mount</th><th>Detail</th></tr></thead>
        <tbody id="ev-tbody">
          <tr><td colspan="5" style="text-align:center;color:var(--muted);padding:24px">Loading…</td></tr>
        </tbody>
      </table>
    </div>
    <div id="ev-pager" style="display:flex;justify-content:center;gap:8px;padding:12px"></div>
  </div>
</div>

<!-- ═══════════════════════════════════════════════════════════════════════ -->
<!-- Listener Analytics static charts (PHP data)                            -->
<!-- ═══════════════════════════════════════════════════════════════════════ -->
<script>
(function(){
  var C = ['#14b8a6','#0891b2','#a78bfa','#f97316','#22c55e','#eab308','#ef4444'];
  var base = {
    responsive:true, maintainAspectRatio:false,
    plugins:{legend:{labels:{color:'#94a3b8',font:{size:11}}}}
  };

  // Daily sessions line chart
  var dCtx = document.getElementById('daily-chart');
  if (dCtx && window.Chart) {
    new Chart(dCtx, {
      type:'line',
      data:{
        labels: <?= json_encode(array_column($daily,'stat_date')) ?>,
        datasets:[
          {label:'Sessions',data:<?= json_encode(array_map('intval',array_column($daily,'sessions'))) ?>,
           borderColor:'#14b8a6',backgroundColor:'rgba(20,184,166,.12)',tension:.4,fill:true,pointRadius:2,borderWidth:2},
          {label:'Peak',data:<?= json_encode(array_map('intval',array_column($daily,'peak'))) ?>,
           borderColor:'#0891b2',backgroundColor:'transparent',tension:.4,pointRadius:2,borderWidth:2,borderDash:[4,3]}
        ]
      },
      options:{...base,scales:{
        x:{ticks:{color:'#64748b',font:{size:10},maxRotation:45},grid:{color:'rgba(51,65,85,.4)'}},
        y:{ticks:{color:'#64748b',font:{size:11}},grid:{color:'rgba(51,65,85,.4)'},beginAtZero:true}
      }}
    });
  }

  // Mount doughnut
  var mCtx = document.getElementById('mount-chart');
  if (mCtx && window.Chart && <?= count($mounts) > 0 ? 'true' : 'false' ?>) {
    new Chart(mCtx, {
      type:'doughnut',
      data:{
        labels:<?= json_encode(array_column($mounts,'stream_mount')) ?>,
        datasets:[{data:<?= json_encode(array_map('intval',array_column($mounts,'sessions'))) ?>,
                   backgroundColor:C,borderColor:'#1e293b',borderWidth:2}]
      },
      options:{...base,plugins:{legend:{position:'bottom',labels:{color:'#94a3b8',font:{size:11},padding:10}}}}
    });
  }

  // Top tracks bar chart
  var tCtx = document.getElementById('tracks-chart');
  if (tCtx && window.Chart && <?= count($top_tracks) > 0 ? 'true' : 'false' ?>) {
    new Chart(tCtx, {
      type:'bar',
      data:{
        labels:<?= json_encode(array_map(function($t){ return ($t['artist']?$t['artist'].': ':'').$t['title']; }, $top_tracks)) ?>,
        datasets:[{label:'Plays',data:<?= json_encode(array_map('intval',array_column($top_tracks,'play_count'))) ?>,
                   backgroundColor:'rgba(20,184,166,.7)',borderColor:'#14b8a6',borderWidth:1}]
      },
      options:{...base,indexAxis:'y',scales:{
        x:{ticks:{color:'#64748b',font:{size:11}},grid:{color:'rgba(51,65,85,.4)'},beginAtZero:true},
        y:{ticks:{color:'#94a3b8',font:{size:10}},grid:{display:false}}
      },plugins:{legend:{display:false}}}
    });
  }
})();
</script>

<!-- ═══════════════════════════════════════════════════════════════════════ -->
<!-- Live polling JS                                                         -->
<!-- ═══════════════════════════════════════════════════════════════════════ -->
<script>
var activeTab = 'health';
var healthChart = null;
var healthChartLabels = [];
var healthChartCpu = [];
var healthChartMem = [];
var evPage = 1;

function switchTab(name) {
    document.querySelectorAll('.tab-panel').forEach(function(p){ p.classList.remove('active'); });
    document.querySelectorAll('.tab-btn').forEach(function(b){ b.classList.remove('active'); });
    var panel = document.getElementById('tab-' + name);
    if (panel) panel.classList.add('active');
    document.querySelectorAll('.tab-btn').forEach(function(b){
        if (b.getAttribute('onclick') === 'switchTab(\'' + name + '\')') b.classList.add('active');
    });
    activeTab = name;
    if (name === 'events') { evPage = 1; loadEvents(); }
    if (name === 'servers') pollServers();
}

// ── System Health live poll ────────────────────────────────────────────────

function pollHealth() {
    mc1Api('GET', '/api/v1/system/health').then(function(d) {
        if (!d || !d.ok) return;
        setText('g-cpu',   d.cpu_pct.toFixed(1));
        setBar('g-cpu-bar', d.cpu_pct);
        setText('g-mem',   d.mem_pct.toFixed(1));
        setBar('g-mem-bar', d.mem_pct);
        setText('g-rx',    d.net_in_kbps);
        setBar('g-rx-bar', Math.min(100, d.net_in_kbps / 10));
        setText('g-tx',    d.net_out_kbps);
        setBar('g-tx-bar', Math.min(100, d.net_out_kbps / 10));
        setText('g-threads', 'threads: ' + d.thread_count);
        setText('g-iface',   'iface: ' + d.net_iface);
        var dt = d.sampled_at ? new Date(d.sampled_at * 1000).toLocaleTimeString() : '';
        setText('g-sampled', dt);
        setText('g-mem-detail', d.mem_used_mb + ' / ' + d.mem_total_mb + ' MB');

        if (activeTab === 'health') updateSlotCards(d.slots || []);
    }).catch(function(){});
}

function pollHealthHistory() {
    mc1Api('GET', '/api/v1/system/health/history?n=60').then(function(d) {
        if (!d || !d.ok || !d.data) return;
        var labels = d.data.map(function(r){ return new Date(r.sampled_at*1000).toLocaleTimeString(); });
        var cpus   = d.data.map(function(r){ return r.cpu_pct; });
        var mems   = d.data.map(function(r){ return r.mem_pct; });
        var age    = d.data.length ? new Date(d.data[d.data.length-1].sampled_at*1000).toLocaleTimeString() : '';
        setText('h-chart-age', age ? 'as of ' + age : '');
        buildOrUpdateHealthChart(labels, cpus, mems);
    }).catch(function(){});
}

function buildOrUpdateHealthChart(labels, cpus, mems) {
    var ctx = document.getElementById('health-chart');
    if (!ctx || !window.Chart) return;
    if (healthChart) {
        healthChart.data.labels = labels;
        healthChart.data.datasets[0].data = cpus;
        healthChart.data.datasets[1].data = mems;
        healthChart.update('none');
        return;
    }
    healthChart = new Chart(ctx, {
        type:'line',
        data:{
            labels: labels,
            datasets:[
                {label:'CPU %',data:cpus,borderColor:'#14b8a6',backgroundColor:'rgba(20,184,166,.1)',
                 tension:.4,fill:true,pointRadius:0,borderWidth:2},
                {label:'Mem %',data:mems,borderColor:'#0891b2',backgroundColor:'rgba(8,145,178,.08)',
                 tension:.4,fill:true,pointRadius:0,borderWidth:2}
            ]
        },
        options:{
            responsive:true, maintainAspectRatio:false,
            animation:{duration:0},
            plugins:{legend:{labels:{color:'#94a3b8',font:{size:11}}}},
            scales:{
                x:{ticks:{color:'#64748b',font:{size:10},maxTicksLimit:12},grid:{color:'rgba(51,65,85,.3)'}},
                y:{min:0,max:100,ticks:{color:'#64748b',font:{size:11},callback:function(v){return v+'%'}},
                   grid:{color:'rgba(51,65,85,.3)'}}
            }
        }
    });
}

// ── Slot cards ────────────────────────────────────────────────────────────

function updateSlotCards(slots) {
    var ctr = document.getElementById('slot-cards');
    if (!ctr) return;
    if (!slots.length) {
        ctr.innerHTML = '<div class="card" style="grid-column:1/-1;text-align:center;padding:32px"><p style="color:var(--muted)">No active encoder slots.</p></div>';
        return;
    }
    var stateColors = {live:'teal',reconnecting:'orange',error:'red',sleep:'yellow',idle:'',starting:'',connecting:''};
    var html = slots.map(function(sl){
        var cls = stateColors[sl.state] || '';
        var stateBadge = '<span class="badge badge-' + (cls||'gray') + '">' + sl.state.toUpperCase() + '</span>';
        return '<div class="slot-card ' + sl.state + '">'
             + '<div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px">'
             + '<span style="font-size:13px;font-weight:600">Slot ' + sl.slot_id + '</span>' + stateBadge
             + '</div>'
             + '<div style="font-size:12px;color:var(--muted);margin-bottom:4px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis" title="' + escHtml(sl.track_title||'') + '">'
             + '<i class="fa-solid fa-music fa-fw"></i> ' + (sl.track_title || '—') + '</div>'
             + '<div style="display:flex;gap:16px;font-size:12px;margin-top:8px">'
             + '<span><i class="fa-solid fa-broadcast-tower fa-fw" style="color:var(--teal)"></i> ' + sl.out_kbps + ' kbps</span>'
             + '<span><i class="fa-solid fa-users fa-fw" style="color:#60a5fa"></i> ' + sl.listeners + ' listeners</span>'
             + '</div>'
             + '<div style="font-size:10px;color:var(--muted);margin-top:6px">'
             + fmtBytes(sl.bytes_out) + ' sent'
             + '</div>'
             + '</div>';
    }).join('');
    ctr.innerHTML = html;
}

// ── Streaming Servers poll ────────────────────────────────────────────────

var expandedSrv = null;

function pollServers() {
    mc1Api('GET', '/api/v1/server_monitors/stats').then(function(d) {
        if (!d || !d.ok) return;
        setText('srv-age', 'updated ' + new Date().toLocaleTimeString());
        var rows = (d.servers || []).map(function(s){
            var dot = '<span class="dot ' + s.status + '"></span>';
            var typeBadge = '<span class="badge badge-gray" style="font-size:10px">' + escHtml(s.server_type) + '</span>';
            var age = s.polled_at ? new Date(s.polled_at*1000).toLocaleTimeString() : '—';
            return '<tr onclick="toggleMounts(' + s.server_id + ')" style="cursor:pointer">'
                 + '<td style="font-weight:600">' + escHtml(s.name) + '</td>'
                 + '<td>' + typeBadge + '</td>'
                 + '<td class="td-mono">' + escHtml(s.host) + ':' + s.port + '</td>'
                 + '<td>' + dot + s.status + '</td>'
                 + '<td>' + s.listeners + '</td>'
                 + '<td>' + s.out_kbps + '</td>'
                 + '<td>' + escHtml(s.uptime||'—') + '</td>'
                 + '<td style="font-size:11px;color:var(--muted)">' + age + '</td>'
                 + '<td><button class="btn btn-sm" onclick="event.stopPropagation();forcePoll(' + s.server_id + ')"><i class="fa-solid fa-rotate fa-fw"></i></button></td>'
                 + '</tr>';
        }).join('');
        var tbody = document.getElementById('srv-tbody');
        if (tbody) tbody.innerHTML = rows || '<tr><td colspan="9" style="text-align:center;color:var(--muted);padding:24px">No servers configured</td></tr>';
    }).catch(function(){});
}

function toggleMounts(server_id) {
    var ctr = document.getElementById('srv-mounts');
    if (!ctr) return;
    if (expandedSrv === server_id) { expandedSrv = null; ctr.innerHTML = ''; return; }
    expandedSrv = server_id;
    mc1Api('GET', '/api/v1/server_monitors/stats?id=' + server_id).then(function(d){
        if (!d || !d.ok || !d.server) return;
        var srv = d.server;
        var mts = (srv.mounts||[]).map(function(m){
            var ours = m.ours ? ' <span class="badge badge-teal" style="font-size:10px">ours</span>' : '';
            return '<tr>'
                 + '<td>' + escHtml(m.mount) + ours + '</td>'
                 + '<td>' + escHtml(m.codec||'—') + '</td>'
                 + '<td>' + m.listeners + '</td>'
                 + '<td>' + m.bitrate + ' kbps</td>'
                 + '<td style="max-width:240px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap">' + escHtml(m.title||'—') + '</td>'
                 + '<td><span class="dot ' + (m.online?'online':'offline') + '"></span>' + (m.online?'online':'offline') + '</td>'
                 + '</tr>';
        }).join('');
        ctr.innerHTML = '<div class="card" style="margin-top:0"><div class="card-hdr"><div class="card-title">Mounts — ' + escHtml(srv.name) + '</div><span class="badge badge-gray">fetch: ' + srv.fetch_ms + 'ms | ' + escHtml(srv.status_str||'') + '</span></div>'
            + (mts ? '<div class="tbl-wrap"><table><thead><tr><th>Mount</th><th>Codec</th><th>Listeners</th><th>Bitrate</th><th>Now Playing</th><th>Status</th></tr></thead><tbody>' + mts + '</tbody></table></div>'
                   : '<div class="empty" style="padding:16px"><p>No mounts found</p></div>')
            + '</div>';
    }).catch(function(){});
}

function forcePoll(id) {
    mc1Api('POST', '/api/v1/server_monitors/poll', {id: id}).then(function(){ setTimeout(pollServers, 500); });
}
function forcePollAll() {
    mc1Api('POST', '/api/v1/server_monitors/poll', {}).then(function(){ setTimeout(pollServers, 500); });
}

// ── Event Log ─────────────────────────────────────────────────────────────

function loadEvents() {
    var slot_id    = document.getElementById('ev-slot') ? document.getElementById('ev-slot').value : '';
    var event_type = document.getElementById('ev-type') ? document.getElementById('ev-type').value : '';
    var body = {action:'stream_events', page: evPage, limit: 50};
    if (slot_id) body.slot_id = parseInt(slot_id);
    if (event_type) body.event_type = event_type;
    mc1Api('POST', '/app/api/metrics.php', body).then(function(d){
        if (!d || !d.ok) return;
        var evCls = {start:'ev-start',stop:'ev-stop',reconnect:'ev-reconnect',error:'ev-error',metadata:'ev-metadata'};
        var rows = (d.data||[]).map(function(e){
            var cls = evCls[e.event_type] || '';
            return '<tr>'
                 + '<td style="font-size:11px;color:var(--muted)">' + escHtml(e.created_at||'') + '</td>'
                 + '<td>Slot ' + e.slot_id + '</td>'
                 + '<td><span class="ev-badge ' + cls + '">' + escHtml(e.event_type) + '</span></td>'
                 + '<td class="td-mono">' + escHtml(e.mount||'') + '</td>'
                 + '<td style="font-size:11px;color:var(--muted)">' + escHtml(e.detail||'') + '</td>'
                 + '</tr>';
        }).join('');
        var tbody = document.getElementById('ev-tbody');
        if (tbody) tbody.innerHTML = rows || '<tr><td colspan="5" style="text-align:center;color:var(--muted);padding:24px">No events yet. Events are written when encoder slots start, stop, reconnect, or encounter errors.</td></tr>';

        // Pagination
        var pager = document.getElementById('ev-pager');
        if (pager && d.pages > 1) {
            var btns = '';
            if (evPage > 1) btns += '<button class="btn btn-sm" onclick="evPage--; loadEvents()">Prev</button>';
            btns += '<span style="font-size:12px;color:var(--muted)">Page ' + d.page + ' of ' + d.pages + '</span>';
            if (evPage < d.pages) btns += '<button class="btn btn-sm" onclick="evPage++; loadEvents()">Next</button>';
            pager.innerHTML = btns;
        } else if (pager) {
            pager.innerHTML = '';
        }
    }).catch(function(){});
}

// ── Helpers ───────────────────────────────────────────────────────────────

function setText(id, v) { var el = document.getElementById(id); if (el) el.textContent = v; }
function setBar(id, pct) { var el = document.getElementById(id); if (el) el.style.width = Math.min(100,Math.max(0,pct)).toFixed(1) + '%'; }
function fmtBytes(b) {
    if (!b) return '0 B';
    if (b < 1024) return b + ' B';
    if (b < 1048576) return (b/1024).toFixed(1) + ' KB';
    if (b < 1073741824) return (b/1048576).toFixed(2) + ' MB';
    return (b/1073741824).toFixed(2) + ' GB';
}
function escHtml(s) {
    return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

// ── Startup ────────────────────────────────────────────────────────────────

document.addEventListener('DOMContentLoaded', function() {
    pollHealth();
    pollHealthHistory();
    setInterval(pollHealth, 5000);
    setInterval(pollHealthHistory, 60000);
    setInterval(function(){ if (activeTab === 'servers') pollServers(); }, 30000);
});
</script>

<?php require_once __DIR__ . '/app/inc/footer.php'; ?>
