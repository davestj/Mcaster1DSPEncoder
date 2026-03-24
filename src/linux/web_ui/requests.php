<?php
/**
 * requests.php — Song Request Queue & Dedication Management
 *
 * File:    src/linux/web_ui/requests.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Purpose: We display and manage the song request queue and dedications panel
 *          for broadcast DJs. Includes pending queue, history, stats, and
 *          dedication management.
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We use first-person plural in all comments
 *  - We use h() for all user data rendered into HTML
 *  - We use mc1Api() from footer.php for all JSON calls
 *  - DOMContentLoaded for all startup JS
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/app/inc/auth.php';
require_once __DIR__ . '/app/inc/mc1_config.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/user_auth.php';

$page_title = 'Requests';
$active_nav = 'requests';
$use_charts = false;

require_once __DIR__ . '/app/inc/header.php';
?>

<style>
.req-card{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:16px;transition:border-color .3s}
.req-card:hover{border-color:rgba(20,184,166,.4)}
.req-card .req-meta{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-bottom:8px}
.req-card .req-title{font-size:15px;font-weight:600;color:var(--text);margin-bottom:4px}
.req-card .req-artist{font-size:13px;color:var(--teal)}
.req-card .req-msg{font-size:12px;color:var(--text-dim);margin-top:8px;font-style:italic;padding:8px 12px;background:rgba(255,255,255,.03);border-radius:var(--radius-sm);border-left:3px solid var(--teal)}
.req-card .req-from{font-size:11px;color:var(--muted)}
.req-card .req-time{font-size:11px;color:var(--muted)}
.req-card .req-acts{display:flex;gap:6px;margin-top:10px}
.req-grid{display:grid;gap:12px}
.ded-card{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:16px}
.ded-card:hover{border-color:rgba(239,68,68,.3)}
.ded-card .ded-to{font-size:14px;font-weight:600;color:var(--text)}
.ded-card .ded-from{font-size:12px;color:var(--teal)}
.ded-card .ded-msg{font-size:13px;color:var(--text-dim);margin:8px 0;font-style:italic;padding:10px 14px;background:rgba(255,255,255,.03);border-radius:var(--radius-sm);border-left:3px solid var(--red)}
.ded-card .ded-song{font-size:12px;color:var(--muted);margin-top:4px}
.status-pending{background:rgba(234,179,8,.12);color:var(--yellow)}
.status-approved{background:rgba(20,184,166,.12);color:var(--teal)}
.status-played{background:rgba(34,197,94,.12);color:var(--green)}
.status-rejected{background:rgba(239,68,68,.12);color:var(--red)}
.status-read{background:rgba(8,145,178,.12);color:var(--cyan)}
.filter-bar{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:14px;align-items:center}
.filter-btn{padding:5px 12px;border-radius:16px;font-size:12px;font-weight:600;border:1px solid var(--border);background:transparent;color:var(--text-dim);cursor:pointer;transition:all .15s}
.filter-btn:hover{border-color:var(--teal);color:var(--teal)}
.filter-btn.active{background:rgba(20,184,166,.12);border-color:var(--teal);color:var(--teal)}
.empty-queue{text-align:center;padding:40px 20px;color:var(--muted)}
.empty-queue i{font-size:36px;display:block;margin-bottom:12px;color:var(--border)}
</style>

<div class="sec-hdr">
  <div class="sec-title"><i class="fa-solid fa-hand" style="color:var(--teal);margin-right:8px"></i>Song Requests &amp; Dedications</div>
  <div style="display:flex;gap:8px;align-items:center">
    <a href="/request-widget.php" target="_blank" class="btn btn-secondary btn-sm"><i class="fa-solid fa-arrow-up-right-from-square"></i> Public Widget</a>
    <button class="btn btn-primary btn-sm" onclick="refreshAll()"><i class="fa-solid fa-arrows-rotate"></i> Refresh</button>
  </div>
</div>

<!-- Stats cards -->
<div class="card-grid grid-4" id="req-stats">
  <div class="stat-card">
    <div class="stat-icon teal"><i class="fa-solid fa-inbox fa-fw"></i></div>
    <div class="stat-label">Pending</div>
    <div class="stat-value" id="st-pending">--</div>
  </div>
  <div class="stat-card">
    <div class="stat-icon green"><i class="fa-solid fa-check fa-fw"></i></div>
    <div class="stat-label">Approved Today</div>
    <div class="stat-value" id="st-approved">--</div>
  </div>
  <div class="stat-card">
    <div class="stat-icon cyan"><i class="fa-solid fa-play fa-fw"></i></div>
    <div class="stat-label">Played Today</div>
    <div class="stat-value" id="st-played">--</div>
  </div>
  <div class="stat-card">
    <div class="stat-icon orange"><i class="fa-solid fa-heart fa-fw"></i></div>
    <div class="stat-label">Dedications Pending</div>
    <div class="stat-value" id="st-ded-pending">--</div>
  </div>
</div>

<!-- Tabs -->
<div class="card" style="margin-top:4px">
  <div class="tabs">
    <button class="tab-btn active" data-tab="queue">Pending Queue</button>
    <button class="tab-btn" data-tab="history">Request History</button>
    <button class="tab-btn" data-tab="dedications">Dedications</button>
  </div>

  <!-- Pending Queue Tab -->
  <div class="tab-pane active" id="tab-queue">
    <div id="queue-list">
      <div class="empty"><div class="spinner"></div></div>
    </div>
  </div>

  <!-- Request History Tab -->
  <div class="tab-pane" id="tab-history">
    <div class="filter-bar">
      <button class="filter-btn active" onclick="filterHistory(null,this)">All</button>
      <button class="filter-btn" onclick="filterHistory('pending',this)">Pending</button>
      <button class="filter-btn" onclick="filterHistory('approved',this)">Approved</button>
      <button class="filter-btn" onclick="filterHistory('played',this)">Played</button>
      <button class="filter-btn" onclick="filterHistory('rejected',this)">Rejected</button>
    </div>
    <div id="history-list">
      <div class="empty"><div class="spinner"></div></div>
    </div>
    <div id="history-pager" class="pagination" style="margin-top:12px"></div>
  </div>

  <!-- Dedications Tab -->
  <div class="tab-pane" id="tab-dedications">
    <div class="filter-bar">
      <button class="filter-btn active" onclick="filterDed(null,this)">All</button>
      <button class="filter-btn" onclick="filterDed('pending',this)">Pending</button>
      <button class="filter-btn" onclick="filterDed('approved',this)">Approved</button>
      <button class="filter-btn" onclick="filterDed('read',this)">Read</button>
      <button class="filter-btn" onclick="filterDed('rejected',this)">Rejected</button>
    </div>
    <div id="ded-list">
      <div class="empty"><div class="spinner"></div></div>
    </div>
    <div id="ded-pager" class="pagination" style="margin-top:12px"></div>
  </div>
</div>

<script>
/* We define all functions before DOMContentLoaded fires */
var historyFilter = null;
var historyPage   = 1;
var dedFilter     = null;
var dedPage       = 1;

function esc(s){ return String(s||'').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;'); }

function statusBadge(s) {
  return '<span class="badge status-' + esc(s) + '">' + esc(s) + '</span>';
}

function timeAgo(dt) {
  if (!dt) return '';
  var d = new Date(dt.replace(' ', 'T') + 'Z');
  var diff = Math.floor((Date.now() - d.getTime()) / 1000);
  if (diff < 60) return diff + 's ago';
  if (diff < 3600) return Math.floor(diff / 60) + 'm ago';
  if (diff < 86400) return Math.floor(diff / 3600) + 'h ago';
  return Math.floor(diff / 86400) + 'd ago';
}

/* ── We load the stats cards ── */
function loadStats() {
  mc1Api('POST', '/app/api/requests.php', { action: 'stats' }).then(function(d) {
    if (!d.ok) return;
    document.getElementById('st-pending').textContent    = d.pending;
    document.getElementById('st-approved').textContent   = d.approved;
    document.getElementById('st-played').textContent     = d.played;
    document.getElementById('st-ded-pending').textContent = d.ded_pending;
  });
}

/* ── We load the pending queue ── */
function loadQueue() {
  mc1Api('POST', '/app/api/requests.php', { action: 'list', status: 'pending', limit: 50 }).then(function(d) {
    var el = document.getElementById('queue-list');
    if (!d.ok || !d.rows || d.rows.length === 0) {
      el.innerHTML = '<div class="empty-queue"><i class="fa-solid fa-inbox"></i><p>No pending requests</p></div>';
      return;
    }
    var html = '<div class="req-grid">';
    d.rows.forEach(function(r) {
      html += '<div class="req-card">'
        + '<div class="req-meta">'
        + statusBadge(r.status)
        + '<span class="req-from"><i class="fa-solid fa-user" style="margin-right:4px"></i>' + esc(r.listener_name) + '</span>'
        + '<span class="req-time"><i class="fa-regular fa-clock" style="margin-right:4px"></i>' + timeAgo(r.created_at) + '</span>'
        + (r.track_id ? '<span class="badge badge-teal" style="font-size:10px"><i class="fa-solid fa-link" style="margin-right:3px"></i>Matched</span>' : '')
        + '</div>'
        + '<div class="req-title"><i class="fa-solid fa-music" style="color:var(--teal);margin-right:6px"></i>' + esc(r.track_title) + '</div>'
        + (r.track_artist ? '<div class="req-artist">' + esc(r.track_artist) + '</div>' : '')
        + (r.message ? '<div class="req-msg">' + esc(r.message) + '</div>' : '')
        + '<div class="req-acts">'
        + '<button class="btn btn-success btn-xs" onclick="reqApprove(' + r.id + ')"><i class="fa-solid fa-check"></i> Approve</button>'
        + '<button class="btn btn-primary btn-xs" onclick="reqPlayed(' + r.id + ')"><i class="fa-solid fa-play"></i> Played</button>'
        + '<button class="btn btn-danger btn-xs" onclick="reqReject(' + r.id + ')"><i class="fa-solid fa-xmark"></i> Reject</button>'
        + '</div>'
        + '</div>';
    });
    html += '</div>';
    el.innerHTML = html;
  }).catch(function() {
    document.getElementById('queue-list').innerHTML = '<div class="alert alert-error"><i class="fa-solid fa-xmark"></i> Failed to load queue</div>';
  });
}

/* ── We load the request history with pagination ── */
function loadHistory() {
  var payload = { action: 'list', page: historyPage, limit: 25 };
  if (historyFilter) payload.status = historyFilter;

  mc1Api('POST', '/app/api/requests.php', payload).then(function(d) {
    var el = document.getElementById('history-list');
    if (!d.ok || !d.rows || d.rows.length === 0) {
      el.innerHTML = '<div class="empty-queue"><i class="fa-solid fa-folder-open"></i><p>No requests found</p></div>';
      document.getElementById('history-pager').innerHTML = '';
      return;
    }
    var html = '<div class="tbl-wrap"><table><thead><tr><th>Title</th><th>Artist</th><th>Listener</th><th>Status</th><th>Time</th><th></th></tr></thead><tbody>';
    d.rows.forEach(function(r) {
      html += '<tr>'
        + '<td class="td-title">' + esc(r.track_title) + '</td>'
        + '<td>' + esc(r.track_artist || '-') + '</td>'
        + '<td>' + esc(r.listener_name) + '</td>'
        + '<td>' + statusBadge(r.status) + '</td>'
        + '<td style="font-size:11px;color:var(--muted)">' + timeAgo(r.created_at) + '</td>'
        + '<td class="td-acts">';
      if (r.status === 'pending') {
        html += '<button class="btn btn-success btn-xs" onclick="reqApprove(' + r.id + ')"><i class="fa-solid fa-check"></i></button>'
          + '<button class="btn btn-danger btn-xs" onclick="reqReject(' + r.id + ')"><i class="fa-solid fa-xmark"></i></button>';
      }
      if (r.status === 'approved') {
        html += '<button class="btn btn-primary btn-xs" onclick="reqPlayed(' + r.id + ')"><i class="fa-solid fa-play"></i></button>';
      }
      html += '</td></tr>';
    });
    html += '</tbody></table></div>';
    el.innerHTML = html;

    /* We render pagination */
    var pg = '';
    for (var i = 1; i <= d.pages; i++) {
      if (i === d.page) pg += '<span class="cur">' + i + '</span>';
      else pg += '<a href="#" onclick="historyPage=' + i + ';loadHistory();return false">' + i + '</a>';
    }
    document.getElementById('history-pager').innerHTML = pg;
  });
}

/* ── We load dedications ── */
function loadDedications() {
  var payload = { action: 'list_dedications', page: dedPage, limit: 25 };
  if (dedFilter) payload.status = dedFilter;

  mc1Api('POST', '/app/api/requests.php', payload).then(function(d) {
    var el = document.getElementById('ded-list');
    if (!d.ok || !d.rows || d.rows.length === 0) {
      el.innerHTML = '<div class="empty-queue"><i class="fa-solid fa-heart-crack"></i><p>No dedications found</p></div>';
      document.getElementById('ded-pager').innerHTML = '';
      return;
    }
    var html = '<div class="req-grid">';
    d.rows.forEach(function(r) {
      html += '<div class="ded-card">'
        + '<div style="display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-bottom:6px">'
        + statusBadge(r.status)
        + '<span style="font-size:11px;color:var(--muted)"><i class="fa-regular fa-clock" style="margin-right:3px"></i>' + timeAgo(r.created_at) + '</span>'
        + '</div>'
        + '<div class="ded-to"><i class="fa-solid fa-heart" style="color:var(--red);margin-right:6px"></i>To: ' + esc(r.dedication_to) + '</div>'
        + '<div class="ded-from">From: ' + esc(r.listener_name) + '</div>'
        + '<div class="ded-msg">' + esc(r.message) + '</div>'
        + (r.track_title ? '<div class="ded-song"><i class="fa-solid fa-music" style="margin-right:4px;color:var(--muted)"></i>' + esc(r.track_title) + '</div>' : '')
        + '<div style="display:flex;gap:6px;margin-top:8px">';
      if (r.status === 'pending') {
        html += '<button class="btn btn-success btn-xs" onclick="dedApprove(' + r.id + ')"><i class="fa-solid fa-check"></i> Approve</button>'
          + '<button class="btn btn-danger btn-xs" onclick="dedReject(' + r.id + ')"><i class="fa-solid fa-xmark"></i> Reject</button>';
      }
      html += '</div></div>';
    });
    html += '</div>';
    el.innerHTML = html;

    var pg = '';
    for (var i = 1; i <= d.pages; i++) {
      if (i === d.page) pg += '<span class="cur">' + i + '</span>';
      else pg += '<a href="#" onclick="dedPage=' + i + ';loadDedications();return false">' + i + '</a>';
    }
    document.getElementById('ded-pager').innerHTML = pg;
  });
}

/* ── We handle request actions ── */
window.reqApprove = function(id) {
  mc1Api('POST', '/app/api/requests.php', { action: 'approve', id: id }).then(function(d) {
    if (d.ok) { mc1Toast('Request approved', 'ok'); refreshAll(); }
    else mc1Toast(d.error || 'Failed', 'err');
  });
};
window.reqReject = function(id) {
  mc1Api('POST', '/app/api/requests.php', { action: 'reject', id: id }).then(function(d) {
    if (d.ok) { mc1Toast('Request rejected', 'ok'); refreshAll(); }
    else mc1Toast(d.error || 'Failed', 'err');
  });
};
window.reqPlayed = function(id) {
  mc1Api('POST', '/app/api/requests.php', { action: 'mark_played', id: id }).then(function(d) {
    if (d.ok) { mc1Toast('Marked as played', 'ok'); refreshAll(); }
    else mc1Toast(d.error || 'Failed', 'err');
  });
};
window.dedApprove = function(id) {
  mc1Api('POST', '/app/api/requests.php', { action: 'approve_dedication', id: id }).then(function(d) {
    if (d.ok) { mc1Toast('Dedication approved', 'ok'); loadDedications(); loadStats(); }
    else mc1Toast(d.error || 'Failed', 'err');
  });
};
window.dedReject = function(id) {
  mc1Api('POST', '/app/api/requests.php', { action: 'reject_dedication', id: id }).then(function(d) {
    if (d.ok) { mc1Toast('Dedication rejected', 'ok'); loadDedications(); loadStats(); }
    else mc1Toast(d.error || 'Failed', 'err');
  });
};

/* ── We handle filter changes ── */
window.filterHistory = function(status, btn) {
  historyFilter = status;
  historyPage = 1;
  btn.parentElement.querySelectorAll('.filter-btn').forEach(function(b){ b.classList.remove('active'); });
  btn.classList.add('active');
  loadHistory();
};
window.filterDed = function(status, btn) {
  dedFilter = status;
  dedPage = 1;
  btn.parentElement.querySelectorAll('.filter-btn').forEach(function(b){ b.classList.remove('active'); });
  btn.classList.add('active');
  loadDedications();
};

window.refreshAll = function() {
  loadStats();
  loadQueue();
  loadHistory();
  loadDedications();
};

/* We wait for DOMContentLoaded so mc1Api is defined */
document.addEventListener('DOMContentLoaded', function() {
  refreshAll();
  /* We auto-refresh the queue every 30 seconds */
  setInterval(function() { loadStats(); loadQueue(); }, 30000);
});
</script>

<?php require_once __DIR__ . '/app/inc/footer.php'; ?>
