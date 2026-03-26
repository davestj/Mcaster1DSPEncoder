<?php
/**
 * podcast-analytics.php — Podcast Analytics Dashboard
 *
 * File:    src/linux/web_ui/podcast-analytics.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Phase:   PC-4
 * Purpose: We provide a Chart.js analytics dashboard for podcast downloads,
 *          platform breakdown, top episodes, growth trends, and recent activity.
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We wrap all startup JS in DOMContentLoaded
 *  - We use mc1Api() for all fetch calls (defined in footer.php)
 *  - We use h() for all user data rendered into HTML
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/app/inc/mc1_config.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/traits.db.class.php';
require_once __DIR__ . '/app/inc/logger.php';
require_once __DIR__ . '/app/inc/auth.php';
require_once __DIR__ . '/app/inc/user_auth.php';

$page_title = 'Podcast Analytics';
$active_nav = 'podcast-analytics';
$use_charts = true;

/* We pre-load the show list for the dropdown filter */
class PcAnalyticsData {
    use Mc1Db;
    public static function getShows(): array
    {
        return self::rows('mcaster1_media', "SELECT id, title FROM podcast_shows ORDER BY title ASC");
    }
}
$shows = PcAnalyticsData::getShows();

require __DIR__ . '/app/inc/header.php';
?>

<style>
.pa-toolbar { display:flex; gap:10px; align-items:center; flex-wrap:wrap; margin-bottom:16px; }
.pa-toolbar .sec-title { margin-right:auto; font-size:17px; font-weight:700; color:var(--text); display:flex; align-items:center; gap:8px; }
.pa-toolbar .sec-title i { color:var(--teal); }
.pa-filter { display:flex; gap:8px; align-items:center; flex-wrap:wrap; }
.pa-filter label { font-size:12px; color:var(--muted); white-space:nowrap; }
.pa-filter select, .pa-filter input[type="number"] {
  background:var(--bg2); border:1px solid var(--border); border-radius:var(--radius-sm);
  color:var(--text); padding:5px 10px; font-size:12px;
}
.pa-filter input[type="number"] { width:60px; }
.pa-stats { display:grid; grid-template-columns:repeat(4,1fr); gap:12px; margin-bottom:18px; }
.pa-stat { background:var(--card); border:1px solid var(--border); border-radius:var(--radius); padding:14px 18px; }
.pa-stat-label { font-size:10px; font-weight:700; text-transform:uppercase; letter-spacing:.07em; color:var(--muted); }
.pa-stat-val { font-size:24px; font-weight:700; color:var(--text); line-height:1.2; margin:2px 0; }
.pa-stat-sub { font-size:11px; color:var(--text-dim); }
.pa-charts { display:grid; grid-template-columns:1fr 1fr; gap:16px; margin-bottom:18px; }
.pa-chart-card { background:var(--card); border:1px solid var(--border); border-radius:var(--radius); padding:18px 20px; }
.pa-chart-title { font-size:14px; font-weight:600; color:var(--text); margin-bottom:12px; display:flex; align-items:center; gap:6px; }
.pa-chart-title i { color:var(--teal); font-size:13px; }
.pa-chart-wrap { position:relative; height:260px; }
.pa-full { grid-column:1 / -1; }
.pa-tbl { width:100%; border-collapse:collapse; }
.pa-tbl th { font-size:11px; color:var(--muted); text-align:left; padding:8px 10px; border-bottom:1px solid var(--border); }
.pa-tbl td { font-size:12px; padding:8px 10px; border-bottom:1px solid rgba(51,65,85,.3); color:var(--text-dim); }
.pa-tbl tr:hover td { background:rgba(255,255,255,.02); }
.pa-badge { display:inline-block; padding:1px 7px; border-radius:4px; font-size:10px; font-weight:600; }
.pa-badge.apple_podcasts { background:rgba(168,85,247,.2); color:#a855f7; }
.pa-badge.spotify { background:rgba(34,197,94,.2); color:#22c55e; }
.pa-badge.overcast { background:rgba(249,115,22,.2); color:#f97316; }
.pa-badge.pocket_casts { background:rgba(239,68,68,.2); color:#ef4444; }
.pa-badge.browser { background:rgba(96,165,250,.2); color:#60a5fa; }
.pa-badge.rss_reader { background:rgba(234,179,8,.2); color:#eab308; }
.pa-badge.unknown { background:rgba(100,116,139,.2); color:#94a3b8; }
.pa-completed { color:var(--green); }
.pa-partial { color:var(--orange); }

@media(max-width:900px) {
  .pa-stats { grid-template-columns:repeat(2,1fr); }
  .pa-charts { grid-template-columns:1fr; }
}
</style>

<!-- Toolbar -->
<div class="pa-toolbar">
  <span class="sec-title"><i class="fa-solid fa-chart-line"></i> Podcast Analytics</span>
  <div class="pa-filter">
    <label>Show:</label>
    <select id="pa-show" onchange="paReload()">
      <option value="0">All Shows</option>
      <?php foreach ($shows as $s): ?>
      <option value="<?= (int)$s['id'] ?>"><?= h($s['title']) ?></option>
      <?php endforeach; ?>
    </select>
    <label>Days:</label>
    <input type="number" id="pa-days" value="30" min="1" max="365" onchange="paReload()">
    <button class="btn btn-secondary btn-sm" onclick="paExportCsv()"><i class="fa-solid fa-file-csv"></i> Export CSV</button>
  </div>
</div>

<!-- Summary stats -->
<div class="pa-stats">
  <div class="pa-stat">
    <div class="pa-stat-label">Total Downloads</div>
    <div class="pa-stat-val" id="pa-total">--</div>
    <div class="pa-stat-sub" id="pa-total-sub">&nbsp;</div>
  </div>
  <div class="pa-stat">
    <div class="pa-stat-label">Unique Listeners</div>
    <div class="pa-stat-val" id="pa-unique">--</div>
    <div class="pa-stat-sub" id="pa-unique-sub">&nbsp;</div>
  </div>
  <div class="pa-stat">
    <div class="pa-stat-label">Completion Rate</div>
    <div class="pa-stat-val" id="pa-completion">--</div>
    <div class="pa-stat-sub" id="pa-completion-sub">&nbsp;</div>
  </div>
  <div class="pa-stat">
    <div class="pa-stat-label">Top Platform</div>
    <div class="pa-stat-val" id="pa-top-plat">--</div>
    <div class="pa-stat-sub" id="pa-top-plat-sub">&nbsp;</div>
  </div>
</div>

<!-- Charts -->
<div class="pa-charts">

  <!-- Downloads Over Time (line chart) -->
  <div class="pa-chart-card pa-full">
    <div class="pa-chart-title"><i class="fa-solid fa-chart-area"></i> Downloads Over Time</div>
    <div class="pa-chart-wrap"><canvas id="ch-downloads"></canvas></div>
  </div>

  <!-- Platform Breakdown (doughnut) -->
  <div class="pa-chart-card">
    <div class="pa-chart-title"><i class="fa-solid fa-circle-nodes"></i> Platform Breakdown</div>
    <div class="pa-chart-wrap"><canvas id="ch-platforms"></canvas></div>
  </div>

  <!-- Top Episodes (horizontal bar) -->
  <div class="pa-chart-card">
    <div class="pa-chart-title"><i class="fa-solid fa-trophy"></i> Top Episodes</div>
    <div class="pa-chart-wrap"><canvas id="ch-top-episodes"></canvas></div>
  </div>

  <!-- Growth Trend (area chart) -->
  <div class="pa-chart-card pa-full">
    <div class="pa-chart-title"><i class="fa-solid fa-arrow-trend-up"></i> Cumulative Growth</div>
    <div class="pa-chart-wrap"><canvas id="ch-growth"></canvas></div>
  </div>

</div>

<!-- Recent Downloads table -->
<div class="pa-chart-card">
  <div class="pa-chart-title"><i class="fa-solid fa-clock-rotate-left"></i> Recent Downloads</div>
  <div style="max-height:400px; overflow-y:auto;">
    <table class="pa-tbl">
      <thead>
        <tr>
          <th>Time</th>
          <th>Episode</th>
          <th>Show</th>
          <th>Platform</th>
          <th>Country</th>
          <th>Status</th>
        </tr>
      </thead>
      <tbody id="pa-recent-body">
        <tr><td colspan="6" style="text-align:center;color:var(--muted)">Loading...</td></tr>
      </tbody>
    </table>
  </div>
</div>

<script>
(function(){

var charts = {};
var PLATFORM_COLORS = {
  apple_podcasts: '#a855f7',
  spotify:        '#22c55e',
  overcast:       '#f97316',
  pocket_casts:   '#ef4444',
  castbox:        '#3b82f6',
  castro:         '#ec4899',
  podcast_addict: '#8b5cf6',
  google_podcasts:'#eab308',
  stitcher:       '#06b6d4',
  tunein:         '#14b8a6',
  deezer:         '#f43f5e',
  browser:        '#60a5fa',
  rss_reader:     '#eab308',
  unknown:        '#64748b'
};

var PLATFORM_LABELS = {
  apple_podcasts: 'Apple Podcasts',
  spotify:        'Spotify',
  overcast:       'Overcast',
  pocket_casts:   'Pocket Casts',
  castbox:        'Castbox',
  castro:         'Castro',
  podcast_addict: 'Podcast Addict',
  google_podcasts:'Google Podcasts',
  stitcher:       'Stitcher',
  tunein:         'TuneIn',
  deezer:         'Deezer',
  browser:        'Browser',
  rss_reader:     'RSS Reader',
  unknown:        'Unknown'
};

function getFilters() {
  return {
    show_id: parseInt(document.getElementById('pa-show').value) || 0,
    days:    parseInt(document.getElementById('pa-days').value) || 30
  };
}

function api(action, extra) {
  var f = getFilters();
  var body = Object.assign({ action: action, show_id: f.show_id, days: f.days }, extra || {});
  return mc1Api('POST', '/app/api/podcast.php', body);
}

function fmtNum(n) {
  n = parseInt(n) || 0;
  if (n >= 1000000) return (n / 1000000).toFixed(1) + 'M';
  if (n >= 1000) return (n / 1000).toFixed(1) + 'K';
  return n.toString();
}

function fmtPlatform(p) { return PLATFORM_LABELS[p] || p || 'Unknown'; }

function destroyChart(key) {
  if (charts[key]) { charts[key].destroy(); charts[key] = null; }
}

/* ── Load Downloads Over Time ── */
function loadDownloads() {
  api('download_stats', { period: 'daily' }).then(function(d) {
    if (!d.ok) return;

    var labels = (d.totals || []).map(function(r) { return r.period_label; });
    var values = (d.totals || []).map(function(r) { return parseInt(r.downloads) || 0; });

    // We also update the total stat card
    var total = values.reduce(function(a, b) { return a + b; }, 0);
    document.getElementById('pa-total').textContent = fmtNum(total);
    document.getElementById('pa-total-sub').textContent = 'last ' + getFilters().days + ' days';

    destroyChart('downloads');
    var ctx = document.getElementById('ch-downloads').getContext('2d');
    charts.downloads = new Chart(ctx, {
      type: 'line',
      data: {
        labels: labels,
        datasets: [{
          label: 'Downloads',
          data: values,
          borderColor: '#14b8a6',
          backgroundColor: 'rgba(20,184,166,.15)',
          fill: true,
          tension: 0.3,
          pointRadius: 2,
          pointHoverRadius: 5,
          borderWidth: 2
        }]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        plugins: { legend: { display: false } },
        scales: {
          x: { ticks: { color: '#64748b', font: { size: 10 }, maxRotation: 45 }, grid: { color: 'rgba(51,65,85,.3)' } },
          y: { beginAtZero: true, ticks: { color: '#64748b', font: { size: 10 } }, grid: { color: 'rgba(51,65,85,.3)' } }
        }
      }
    });
  });
}

/* ── Load Platform Breakdown ── */
function loadPlatforms() {
  api('platform_breakdown').then(function(d) {
    if (!d.ok) return;

    var platforms = d.platforms || [];
    var labels = platforms.map(function(r) { return fmtPlatform(r.platform); });
    var values = platforms.map(function(r) { return parseInt(r.downloads) || 0; });
    var colors = platforms.map(function(r) { return PLATFORM_COLORS[r.platform] || '#64748b'; });

    // We update the top platform stat
    if (platforms.length > 0) {
      document.getElementById('pa-top-plat').textContent = fmtPlatform(platforms[0].platform);
      document.getElementById('pa-top-plat-sub').textContent = fmtNum(platforms[0].downloads) + ' downloads';
    }

    destroyChart('platforms');
    var ctx = document.getElementById('ch-platforms').getContext('2d');
    charts.platforms = new Chart(ctx, {
      type: 'doughnut',
      data: {
        labels: labels,
        datasets: [{
          data: values,
          backgroundColor: colors,
          borderColor: '#1e293b',
          borderWidth: 2
        }]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        cutout: '55%',
        plugins: {
          legend: {
            position: 'right',
            labels: { color: '#94a3b8', font: { size: 11 }, padding: 8, usePointStyle: true, pointStyleWidth: 8 }
          }
        }
      }
    });
  });
}

/* ── Load Top Episodes ── */
function loadTopEpisodes() {
  api('top_episodes', { limit: 10 }).then(function(d) {
    if (!d.ok) return;

    var episodes = d.episodes || [];
    var labels = episodes.map(function(r) {
      var lbl = (r.episode_number ? 'Ep ' + r.episode_number + ': ' : '') + (r.title || 'Untitled');
      return lbl.length > 30 ? lbl.substring(0, 28) + '...' : lbl;
    });
    var values = episodes.map(function(r) { return parseInt(r.downloads) || 0; });

    destroyChart('topEpisodes');
    var ctx = document.getElementById('ch-top-episodes').getContext('2d');
    charts.topEpisodes = new Chart(ctx, {
      type: 'bar',
      data: {
        labels: labels,
        datasets: [{
          label: 'Downloads',
          data: values,
          backgroundColor: 'rgba(20,184,166,.6)',
          borderColor: '#14b8a6',
          borderWidth: 1,
          borderRadius: 4
        }]
      },
      options: {
        indexAxis: 'y',
        responsive: true,
        maintainAspectRatio: false,
        plugins: { legend: { display: false } },
        scales: {
          x: { beginAtZero: true, ticks: { color: '#64748b', font: { size: 10 } }, grid: { color: 'rgba(51,65,85,.3)' } },
          y: { ticks: { color: '#94a3b8', font: { size: 10 } }, grid: { display: false } }
        }
      }
    });
  });
}

/* ── Load Growth Trend ── */
function loadGrowth() {
  api('growth_trend', { period: 'weekly' }).then(function(d) {
    if (!d.ok) return;

    var trend = d.trend || [];
    var labels = trend.map(function(r) { return r.period_label; });
    var cumulative = trend.map(function(r) { return parseInt(r.cumulative) || 0; });
    var perPeriod  = trend.map(function(r) { return parseInt(r.downloads) || 0; });

    destroyChart('growth');
    var ctx = document.getElementById('ch-growth').getContext('2d');
    charts.growth = new Chart(ctx, {
      type: 'line',
      data: {
        labels: labels,
        datasets: [
          {
            label: 'Cumulative',
            data: cumulative,
            borderColor: '#14b8a6',
            backgroundColor: 'rgba(20,184,166,.1)',
            fill: true,
            tension: 0.3,
            pointRadius: 2,
            borderWidth: 2,
            yAxisID: 'y'
          },
          {
            label: 'Per Week',
            data: perPeriod,
            borderColor: '#60a5fa',
            backgroundColor: 'rgba(96,165,250,.4)',
            type: 'bar',
            borderRadius: 3,
            borderWidth: 1,
            yAxisID: 'y1'
          }
        ]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        plugins: {
          legend: { labels: { color: '#94a3b8', font: { size: 11 }, usePointStyle: true, pointStyleWidth: 8 } }
        },
        scales: {
          x: { ticks: { color: '#64748b', font: { size: 10 }, maxRotation: 45 }, grid: { color: 'rgba(51,65,85,.3)' } },
          y:  { position: 'left',  beginAtZero: true, ticks: { color: '#14b8a6', font: { size: 10 } }, grid: { color: 'rgba(51,65,85,.3)' } },
          y1: { position: 'right', beginAtZero: true, ticks: { color: '#60a5fa', font: { size: 10 } }, grid: { display: false } }
        }
      }
    });
  });
}

/* ── Load retention + unique listeners for stat cards ── */
function loadRetention() {
  api('episode_retention').then(function(d) {
    if (!d.ok) return;
    var rows = d.retention || [];
    var totalDl = 0, totalComplete = 0;
    rows.forEach(function(r) {
      totalDl += parseInt(r.total_downloads) || 0;
      totalComplete += parseInt(r.completed) || 0;
    });
    var rate = totalDl > 0 ? (totalComplete / totalDl * 100).toFixed(1) : '0.0';
    document.getElementById('pa-completion').textContent = rate + '%';
    document.getElementById('pa-completion-sub').textContent = totalComplete + ' / ' + totalDl + ' completed';
  });

  api('top_episodes', { limit: 1000 }).then(function(d) {
    if (!d.ok) return;
    var eps = d.episodes || [];
    var uniqueSet = {};
    var totalUnique = 0;
    eps.forEach(function(r) { totalUnique += parseInt(r.unique_listeners) || 0; });
    document.getElementById('pa-unique').textContent = fmtNum(totalUnique);
    document.getElementById('pa-unique-sub').textContent = 'across ' + eps.length + ' episodes';
  });
}

/* ── Load Recent Downloads ── */
function loadRecent() {
  api('recent_downloads', { limit: 50 }).then(function(d) {
    if (!d.ok) return;
    var rows = d.downloads || [];
    var tbody = document.getElementById('pa-recent-body');
    if (rows.length === 0) {
      tbody.innerHTML = '<tr><td colspan="6" style="text-align:center;color:var(--muted)">No downloads yet</td></tr>';
      return;
    }
    var html = '';
    rows.forEach(function(r) {
      var dt = r.downloaded_at || '';
      var epLabel = (r.episode_number ? '#' + r.episode_number + ' ' : '') + (r.episode_title || 'Unknown');
      var platClass = r.platform || 'unknown';
      html += '<tr>'
        + '<td>' + esc(dt) + '</td>'
        + '<td>' + esc(epLabel) + '</td>'
        + '<td>' + esc(r.show_title || '') + '</td>'
        + '<td><span class="pa-badge ' + esc(platClass) + '">' + esc(fmtPlatform(r.platform)) + '</span></td>'
        + '<td>' + esc(r.country || '--') + '</td>'
        + '<td>' + (parseInt(r.completed) ? '<span class="pa-completed">Complete</span>' : '<span class="pa-partial">Partial</span>') + '</td>'
        + '</tr>';
    });
    tbody.innerHTML = html;
  });
}

function esc(s) {
  if (!s) return '';
  var d = document.createElement('div');
  d.appendChild(document.createTextNode(s));
  return d.innerHTML;
}

/* ── Public API ── */
window.paReload = function() {
  loadDownloads();
  loadPlatforms();
  loadTopEpisodes();
  loadGrowth();
  loadRetention();
  loadRecent();
};

window.paExportCsv = function() {
  var f = getFilters();
  mc1Api('POST', '/app/api/podcast.php', {
    action: 'export_csv',
    show_id: f.show_id,
    days: f.days,
    type: 'downloads'
  }).then(function(d) {
    if (!d.ok) { mc1Toast(d.error || 'Export failed', 'err'); return; }
    var blob = new Blob([d.csv], { type: 'text/csv' });
    var url = URL.createObjectURL(blob);
    var a = document.createElement('a');
    a.href = url;
    a.download = 'podcast_analytics_' + f.days + 'd.csv';
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
    mc1Toast('CSV exported (' + d.rows + ' rows)');
  }).catch(function() { mc1Toast('Export failed', 'err'); });
};

/* ── Init ── */
document.addEventListener('DOMContentLoaded', function() {
  paReload();
});

})();
</script>

<?php require __DIR__ . '/app/inc/footer.php'; ?>
