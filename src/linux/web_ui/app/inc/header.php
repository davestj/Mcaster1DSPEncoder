<?php
// header.php — Universal page shell for all Mcaster1 PHP pages.
// Variables consumed (set before require):
//   $page_title  string — shown in <title> and topbar
//   $active_nav  string — dashboard|encoders|media|playlists|mediaplayer|metrics|settings|profile
//   $use_charts  bool   — if true, includes Chart.js CDN <script>

if (!defined('MC1_BOOT')) {
    http_response_code(403);
    echo '403 Forbidden';
    return;
}

$page_title = $page_title ?? 'Dashboard';
$active_nav = $active_nav ?? '';
$use_charts = $use_charts ?? false;

$user       = function_exists('mc1_current_user') ? mc1_current_user() : null;
$display    = $user ? ($user['display_name'] ?: $user['username']) : 'Guest';
$role_label = $user ? ($user['role_label'] ?? '') : '';
$can_admin  = $user && !empty($user['can_admin']);
?>
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title><?= h($page_title) ?> — Mcaster1 DSP Encoder</title>
<link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.5.1/css/all.min.css" crossorigin="anonymous" referrerpolicy="no-referrer">
<?php if ($use_charts): ?>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.2/dist/chart.umd.min.js"></script>
<?php endif; ?>
<style>
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
html,body{height:100%;overflow:hidden}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;font-size:14px;background:#0f172a;color:#e2e8f0;line-height:1.5}
:root{
  --bg:#0f172a;--bg2:#1e293b;--bg3:#162032;
  --card:#1e293b;--card2:#263348;
  --border:#334155;--border2:#2d3f55;
  --teal:#14b8a6;--teal2:#0d9488;--teal-glow:rgba(20,184,166,.18);
  --cyan:#0891b2;
  --text:#e2e8f0;--text-dim:#94a3b8;--muted:#64748b;
  --red:#ef4444;--orange:#f97316;--green:#22c55e;--yellow:#eab308;
  --radius:10px;--radius-sm:6px;--radius-xs:4px;
  --sidebar-w:220px;--topbar-h:56px;
}
a{color:var(--teal);text-decoration:none}a:hover{color:var(--teal2)}
button{cursor:pointer;font-family:inherit}
input,select,textarea{font-family:inherit;font-size:14px}
::-webkit-scrollbar{width:6px;height:6px}
::-webkit-scrollbar-track{background:transparent}
::-webkit-scrollbar-thumb{background:var(--border);border-radius:3px}
::-webkit-scrollbar-thumb:hover{background:var(--muted)}

/* Layout */
.layout{display:grid;grid-template-columns:var(--sidebar-w) 1fr;grid-template-rows:var(--topbar-h) 1fr;height:100vh;overflow:hidden}

/* Topbar */
.topbar{grid-column:1/-1;display:flex;align-items:center;gap:12px;padding:0 20px;background:var(--bg2);border-bottom:1px solid var(--border);height:var(--topbar-h);z-index:100}
.topbar-brand{display:flex;align-items:center;gap:10px;min-width:calc(var(--sidebar-w) - 40px)}
.topbar-logo{width:32px;height:32px;border-radius:8px;background:linear-gradient(135deg,var(--teal),var(--cyan));display:flex;align-items:center;justify-content:center;font-size:14px;color:#fff;flex-shrink:0}
.topbar-name{font-weight:700;font-size:15px;color:var(--text)}
.topbar-sub{font-size:10px;color:var(--muted);line-height:1}
.topbar-divider{width:1px;height:28px;background:var(--border);margin:0 4px;flex-shrink:0}
.topbar-page{font-size:15px;font-weight:600;color:var(--text)}
.topbar-right{margin-left:auto;display:flex;align-items:center;gap:10px}
.topbar-user{display:flex;align-items:center;gap:8px}
.topbar-avatar{width:30px;height:30px;border-radius:50%;background:linear-gradient(135deg,var(--teal),var(--cyan));display:flex;align-items:center;justify-content:center;font-size:13px;font-weight:700;color:#fff;flex-shrink:0}
.topbar-uname{font-size:13px;color:var(--text);line-height:1.3}
.topbar-urole{font-size:10px;color:var(--muted)}
.hamburger{display:none;background:none;border:none;color:var(--text-dim);font-size:18px;padding:6px;line-height:1;cursor:pointer}

/* Sidebar */
.sidebar{background:var(--bg2);border-right:1px solid var(--border);overflow-y:auto;padding:8px 0;display:flex;flex-direction:column}
.nav-section{padding:14px 14px 4px;font-size:10px;font-weight:700;letter-spacing:.1em;text-transform:uppercase;color:var(--muted)}
.nav-item{display:flex;align-items:center;gap:9px;padding:8px 16px;color:var(--text-dim);font-size:13px;border-left:3px solid transparent;text-decoration:none;transition:all .15s}
.nav-item:hover{background:rgba(255,255,255,.04);color:var(--text);text-decoration:none}
.nav-item.active{background:var(--teal-glow);color:var(--teal);border-left-color:var(--teal)}
.nav-item .fa-fw{width:16px;text-align:center;flex-shrink:0;font-size:13px}
.sidebar-spacer{flex:1}
.sidebar-footer{padding:12px 14px;border-top:1px solid var(--border);margin-top:8px}
.dnas-pill{display:flex;align-items:center;gap:6px;font-size:11px;color:var(--text-dim);padding:5px 9px;background:rgba(255,255,255,.04);border-radius:var(--radius-sm);margin-bottom:8px}
.dnas-dot{width:7px;height:7px;border-radius:50%;background:var(--muted);flex-shrink:0;transition:background .4s}
.dnas-dot.live{background:var(--green);box-shadow:0 0 6px var(--green)}
.dnas-dot.err{background:var(--red)}
.ver-info{font-size:10px;color:var(--muted);line-height:1.9}

/* Main */
.main{overflow-y:auto;padding:20px 24px;display:flex;flex-direction:column;gap:18px}

/* Cards */
.card{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:18px 20px}
.card-hdr{display:flex;align-items:center;justify-content:space-between;margin-bottom:14px;gap:12px}
.card-title{font-size:15px;font-weight:600;color:var(--text);display:flex;align-items:center;gap:8px}
.card-title .fa-fw{color:var(--teal)}
.card-grid{display:grid;gap:14px}
.grid-2{grid-template-columns:repeat(2,1fr)}
.grid-3{grid-template-columns:repeat(3,1fr)}
.grid-4{grid-template-columns:repeat(4,1fr)}

/* Stat cards */
.stat-card{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:14px 18px}
.stat-icon{width:38px;height:38px;border-radius:8px;display:flex;align-items:center;justify-content:center;font-size:17px;margin-bottom:8px}
.stat-icon.teal{background:rgba(20,184,166,.14);color:var(--teal)}
.stat-icon.cyan{background:rgba(8,145,178,.14);color:var(--cyan)}
.stat-icon.green{background:rgba(34,197,94,.12);color:var(--green)}
.stat-icon.orange{background:rgba(249,115,22,.12);color:var(--orange)}
.stat-icon.red{background:rgba(239,68,68,.12);color:var(--red)}
.stat-label{font-size:10px;font-weight:700;text-transform:uppercase;letter-spacing:.07em;color:var(--muted)}
.stat-value{font-size:24px;font-weight:700;color:var(--text);line-height:1.2;margin:2px 0}
.stat-sub{font-size:11px;color:var(--text-dim)}

/* Encoder slot cards */
.slot-card{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:16px;transition:border-color .3s,box-shadow .3s}
.slot-card.live{border-color:rgba(20,184,166,.6);box-shadow:0 0 0 1px rgba(20,184,166,.2),0 4px 20px rgba(20,184,166,.08);animation:live-card-pulse 2.5s ease-in-out infinite}
.slot-card.error{border-color:rgba(239,68,68,.45);box-shadow:0 0 0 1px rgba(239,68,68,.15)}
.slot-card.connecting{border-color:rgba(234,179,8,.45);animation:conn-card-pulse 1.2s ease-in-out infinite}
.slot-card.sleep{border-color:rgba(251,146,60,.45);box-shadow:0 0 0 1px rgba(251,146,60,.15)}
@keyframes live-card-pulse{0%,100%{border-color:rgba(20,184,166,.45);box-shadow:0 0 0 1px rgba(20,184,166,.1)}50%{border-color:rgba(20,184,166,.85);box-shadow:0 0 0 2px rgba(20,184,166,.25),0 4px 24px rgba(20,184,166,.15)}}
@keyframes conn-card-pulse{0%,100%{border-color:rgba(234,179,8,.3)}50%{border-color:rgba(234,179,8,.7)}}
.slot-hdr{display:flex;align-items:center;gap:9px;margin-bottom:10px}
.slot-num{width:26px;height:26px;border-radius:5px;background:rgba(255,255,255,.06);display:flex;align-items:center;justify-content:center;font-weight:700;font-size:12px;flex-shrink:0;color:var(--text-dim)}
.slot-name{font-weight:600;font-size:13px;color:var(--text);flex:1;min-width:0;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.state-badge{display:inline-flex;align-items:center;gap:5px;padding:2px 8px;border-radius:10px;font-size:10px;font-weight:700;text-transform:uppercase;letter-spacing:.05em;flex-shrink:0}
.state-badge::before{content:'';width:5px;height:5px;border-radius:50%;flex-shrink:0}
.state-badge.idle{background:rgba(100,116,139,.2);color:var(--muted)}
.state-badge.idle::before{background:var(--muted)}
.state-badge.live{background:rgba(20,184,166,.2);color:var(--teal);font-size:11px;padding:3px 10px}
.state-badge.live::before{background:var(--teal);animation:live-dot 1.4s ease-in-out infinite;box-shadow:0 0 5px var(--teal)}
@keyframes live-dot{0%,100%{opacity:1;transform:scale(1)}50%{opacity:.4;transform:scale(.7)}}
.state-badge.connecting{background:rgba(234,179,8,.18);color:var(--yellow)}
.state-badge.connecting::before{background:var(--yellow);animation:live-dot 0.8s ease-in-out infinite}
.state-badge.error{background:rgba(239,68,68,.18);color:var(--red)}
.state-badge.error::before{background:var(--red)}
.state-badge.sleep{background:rgba(251,146,60,.18);color:#fb923c}
.state-badge.sleep::before{background:#fb923c;animation:live-dot 2s ease-in-out infinite}
.track-line{font-size:12px;color:var(--text-dim);margin-bottom:8px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;min-height:15px}
.prog-wrap{height:3px;background:rgba(255,255,255,.07);border-radius:2px;margin-bottom:10px;overflow:hidden}
.prog-bar{height:100%;background:var(--teal);border-radius:2px;transition:width .4s linear}
.slot-meta{display:flex;gap:10px;font-size:11px;color:var(--muted);margin-bottom:10px;flex-wrap:wrap}
.slot-acts{display:flex;gap:6px;flex-wrap:wrap}

/* Buttons */
.btn{display:inline-flex;align-items:center;gap:6px;padding:7px 14px;border-radius:var(--radius-sm);font-size:13px;font-weight:500;border:none;transition:all .15s;white-space:nowrap;cursor:pointer;text-decoration:none}
.btn:hover{text-decoration:none}
.btn-sm{padding:5px 11px;font-size:12px}
.btn-xs{padding:3px 8px;font-size:11px}
.btn-primary{background:var(--teal);color:#0f172a;font-weight:600}
.btn-primary:hover{background:var(--teal2);color:#0f172a}
.btn-secondary{background:rgba(255,255,255,.07);color:var(--text-dim);border:1px solid var(--border)}
.btn-secondary:hover{background:rgba(255,255,255,.12);color:var(--text)}
.btn-danger{background:rgba(239,68,68,.12);color:var(--red);border:1px solid rgba(239,68,68,.25)}
.btn-danger:hover{background:rgba(239,68,68,.22)}
.btn-success{background:rgba(34,197,94,.12);color:var(--green);border:1px solid rgba(34,197,94,.2)}
.btn-success:hover{background:rgba(34,197,94,.22)}
.btn-warn{background:rgba(234,179,8,.12);color:var(--yellow);border:1px solid rgba(234,179,8,.2)}
.btn-icon{padding:5px 8px;background:rgba(255,255,255,.05);color:var(--text-dim);border:1px solid var(--border);border-radius:var(--radius-sm)}
.btn-icon:hover{background:rgba(255,255,255,.12);color:var(--text)}
.btn[disabled],.btn:disabled{opacity:.4;cursor:not-allowed;pointer-events:none}

/* Tables */
.tbl-wrap{overflow-x:auto}
table{width:100%;border-collapse:collapse}
th{font-size:10px;font-weight:700;text-transform:uppercase;letter-spacing:.07em;color:var(--muted);padding:7px 12px;text-align:left;border-bottom:1px solid var(--border);background:rgba(255,255,255,.02);white-space:nowrap}
td{padding:8px 12px;border-bottom:1px solid rgba(51,65,85,.45);font-size:13px;color:var(--text-dim);vertical-align:middle}
tr:last-child td{border-bottom:none}
tbody tr:hover td{background:rgba(255,255,255,.025);color:var(--text)}
.td-title{color:var(--text);font-weight:500;max-width:240px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.td-mono{font-family:'SF Mono','Fira Code',monospace;font-size:11px;color:var(--teal)}
.td-acts{display:flex;gap:5px;justify-content:flex-end}

/* Forms */
.form-group{display:flex;flex-direction:column;gap:4px;margin-bottom:12px}
.form-label{font-size:11px;font-weight:700;color:var(--text-dim);text-transform:uppercase;letter-spacing:.05em}
.form-input,.form-select,.form-textarea{width:100%;padding:8px 11px;background:rgba(255,255,255,.05);border:1px solid var(--border);border-radius:var(--radius-sm);color:var(--text);font-size:13px;outline:none;transition:border-color .15s}
.form-input:focus,.form-select:focus,.form-textarea:focus{border-color:var(--teal);background:rgba(20,184,166,.05)}
.form-select option{background:#1e293b;color:var(--text)}
.form-textarea{resize:vertical;min-height:72px}
.form-row{display:grid;gap:12px}
.form-row-2{grid-template-columns:1fr 1fr}
.form-hint{font-size:11px;color:var(--muted);margin-top:2px}
.form-inline{display:flex;gap:8px;align-items:flex-end}

/* Tabs */
.tabs{display:flex;gap:0;border-bottom:1px solid var(--border);margin-bottom:16px}
.tab-btn{padding:9px 16px;background:none;border:none;color:var(--text-dim);font-size:13px;font-weight:500;cursor:pointer;border-bottom:2px solid transparent;margin-bottom:-1px;transition:color .15s,border-color .15s}
.tab-btn:hover{color:var(--text)}
.tab-btn.active{color:var(--teal);border-bottom-color:var(--teal)}
.tab-pane{display:none}
.tab-pane.active{display:block}

/* Chart */
.chart-wrap{position:relative;height:220px}
.chart-wrap canvas{max-height:220px}

/* Badges */
.badge{display:inline-flex;align-items:center;padding:2px 8px;border-radius:10px;font-size:11px;font-weight:600;white-space:nowrap}
.badge-teal{background:rgba(20,184,166,.15);color:var(--teal)}
.badge-blue{background:rgba(8,145,178,.15);color:var(--cyan)}
.badge-green{background:rgba(34,197,94,.12);color:var(--green)}
.badge-red{background:rgba(239,68,68,.12);color:var(--red)}
.badge-gray{background:rgba(100,116,139,.14);color:var(--muted)}
.badge-orange{background:rgba(249,115,22,.12);color:var(--orange)}
.badge-yellow{background:rgba(234,179,8,.12);color:var(--yellow)}

/* Toggle */
.toggle-wrap{display:flex;align-items:center;gap:8px;font-size:13px;color:var(--text-dim)}
.toggle{position:relative;display:inline-block;width:36px;height:20px;flex-shrink:0}
.toggle input{opacity:0;width:0;height:0}
.toggle-slider{position:absolute;inset:0;background:var(--border);border-radius:10px;cursor:pointer;transition:.2s}
.toggle-slider::before{content:'';position:absolute;height:14px;width:14px;left:3px;bottom:3px;background:#fff;border-radius:50%;transition:.2s}
.toggle input:checked+.toggle-slider{background:var(--teal)}
.toggle input:checked+.toggle-slider::before{transform:translateX(16px)}

/* Range */
.range-wrap{display:flex;align-items:center;gap:10px;color:var(--text-dim);font-size:12px}
input[type=range]{-webkit-appearance:none;flex:1;height:4px;background:var(--border);border-radius:2px;outline:none}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:14px;height:14px;border-radius:50%;background:var(--teal);cursor:pointer}

/* Alerts */
.alert{padding:9px 13px;border-radius:var(--radius-sm);font-size:13px;display:flex;align-items:center;gap:8px}
.alert-info{background:rgba(8,145,178,.1);border:1px solid rgba(8,145,178,.2);color:#7dd3fc}
.alert-warn{background:rgba(234,179,8,.1);border:1px solid rgba(234,179,8,.2);color:var(--yellow)}
.alert-error{background:rgba(239,68,68,.1);border:1px solid rgba(239,68,68,.2);color:#fca5a5}
.alert-success{background:rgba(34,197,94,.1);border:1px solid rgba(34,197,94,.2);color:#86efac}

/* Pagination */
.pagination{display:flex;gap:4px;align-items:center;justify-content:flex-end;padding:10px 0;flex-wrap:wrap}
.pagination a,.pagination span{display:inline-flex;align-items:center;justify-content:center;min-width:28px;height:28px;padding:0 6px;border:1px solid var(--border);border-radius:var(--radius-xs);font-size:12px;color:var(--text-dim);text-decoration:none;transition:all .15s}
.pagination a:hover{border-color:var(--teal);color:var(--teal)}
.pagination .cur{background:rgba(20,184,166,.14);border-color:var(--teal);color:var(--teal)}
.pagination .ellipsis{border:none}

/* Search row */
.search-row{display:flex;gap:8px;margin-bottom:14px;flex-wrap:wrap}
.search-row .form-input{flex:1;min-width:140px}

/* Section header */
.sec-hdr{display:flex;align-items:center;justify-content:space-between;margin-bottom:14px;gap:12px;flex-wrap:wrap}
.sec-title{font-size:15px;font-weight:700;color:var(--text)}

/* Empty state */
.empty{text-align:center;padding:40px 20px;color:var(--muted)}
.empty .fa-fw{font-size:32px;margin-bottom:10px;display:block;color:var(--border)}

/* Code */
.code-block{background:rgba(0,0,0,.3);border:1px solid var(--border);border-radius:var(--radius-sm);padding:9px 13px;font-family:'SF Mono','Fira Code',monospace;font-size:12px;color:var(--teal);overflow-x:auto;word-break:break-all}

/* DSP controls */
.dsp-row{display:flex;align-items:center;justify-content:space-between;padding:9px 0;border-bottom:1px solid rgba(51,65,85,.45);gap:12px}
.dsp-row:last-child{border-bottom:none}
.dsp-lbl{font-size:13px;color:var(--text-dim)}
.dsp-lbl small{display:block;font-size:10px;color:var(--muted);margin-top:2px}

/* Spinner */
.spinner{display:inline-block;width:16px;height:16px;border:2px solid rgba(20,184,166,.25);border-top-color:var(--teal);border-radius:50%;animation:spin .7s linear infinite}
@keyframes spin{to{transform:rotate(360deg)}}

/* Responsive */
@media(max-width:860px){
  .layout{grid-template-columns:1fr}
  .sidebar{display:none;position:fixed;top:var(--topbar-h);left:0;bottom:0;width:var(--sidebar-w);z-index:200;box-shadow:4px 0 20px rgba(0,0,0,.5)}
  .sidebar.open{display:flex}
  .hamburger{display:block}
  .grid-3,.grid-4{grid-template-columns:repeat(2,1fr)}
}
@media(max-width:580px){
  .grid-2,.grid-3,.grid-4{grid-template-columns:1fr}
  .main{padding:14px}
  .topbar{padding:0 12px;gap:8px}
}
</style>
</head>
<body>
<div class="layout">

<!-- Topbar -->
<header class="topbar">
  <button class="hamburger" onclick="document.querySelector('.sidebar').classList.toggle('open')" aria-label="Toggle menu">
    <i class="fa-solid fa-bars"></i>
  </button>
  <div class="topbar-brand">
    <div class="topbar-logo"><i class="fa-solid fa-wave-square"></i></div>
    <div>
      <div class="topbar-name">Mcaster1</div>
      <div class="topbar-sub">DSP Encoder</div>
    </div>
  </div>
  <div class="topbar-divider"></div>
  <div class="topbar-page"><?= h($page_title) ?></div>
  <div class="topbar-right">
    <!-- Live encoder status pill — updated by JS polling, visible to all users -->
    <div id="enc-live-pill" style="display:none;align-items:center;gap:6px;background:rgba(20,184,166,.12);border:1px solid rgba(20,184,166,.35);border-radius:20px;padding:4px 10px;font-size:11px;font-weight:700;color:var(--teal);cursor:pointer" onclick="window.location.href='/dashboard.php'" title="Live encoder streams — click to view dashboard">
      <span style="width:7px;height:7px;border-radius:50%;background:var(--teal);display:inline-block;animation:live-dot 1.4s ease-in-out infinite;box-shadow:0 0 6px var(--teal)"></span>
      <span id="enc-live-label">0 Live</span>
    </div>
    <a class="topbar-user" href="/profile.php" title="View your profile" style="text-decoration:none;cursor:pointer">
      <div class="topbar-avatar"><?= h(strtoupper(substr($display, 0, 1))) ?></div>
      <div>
        <div class="topbar-uname"><?= h($display) ?></div>
        <div class="topbar-urole"><?= h($role_label) ?></div>
      </div>
    </a>
    <button class="btn btn-secondary btn-sm" onclick="mc1Logout()">
      <i class="fa-solid fa-arrow-right-from-bracket"></i>
    </button>
  </div>
</header>

<!-- Sidebar -->
<nav class="sidebar">
  <div class="nav-section">Monitor</div>
  <a class="nav-item <?= $active_nav === 'dashboard' ? 'active' : '' ?>" href="/dashboard.php">
    <i class="fa-solid fa-gauge fa-fw"></i> Dashboard
  </a>
  <a class="nav-item <?= $active_nav === 'encoders' ? 'active' : '' ?>" href="/encoders.php">
    <i class="fa-solid fa-tower-broadcast fa-fw"></i> Encoders
  </a>

  <div class="nav-section">Library</div>
  <a class="nav-item <?= $active_nav === 'media' ? 'active' : '' ?>" href="/media.php">
    <i class="fa-solid fa-music fa-fw"></i> Media Library
  </a>
  <a class="nav-item <?= $active_nav === 'playlists' ? 'active' : '' ?>" href="/playlists.php">
    <i class="fa-solid fa-list-ul fa-fw"></i> Playlists
  </a>
  <a class="nav-item <?= $active_nav === 'mediaplayer' ? 'active' : '' ?>" href="/mediaplayer.php">
    <i class="fa-solid fa-headphones fa-fw"></i> Media Player
  </a>

  <div class="nav-section">Analytics</div>
  <a class="nav-item <?= $active_nav === 'metrics' ? 'active' : '' ?>" href="/metrics.php">
    <i class="fa-solid fa-chart-line fa-fw"></i> Listener Metrics
  </a>

  <div class="nav-section">System</div>
  <a class="nav-item <?= $active_nav === 'settings' ? 'active' : '' ?>" href="/settings.php">
    <i class="fa-solid fa-gear fa-fw"></i> Settings
  </a>
  <a class="nav-item <?= $active_nav === 'profile' ? 'active' : '' ?>" href="/profile.php">
    <i class="fa-solid fa-circle-user fa-fw"></i> My Profile
  </a>

  <div class="sidebar-spacer"></div>
  <div class="sidebar-footer">
    <div class="dnas-pill" id="dnas-pill">
      <div class="dnas-dot" id="dnas-dot"></div>
      <span id="dnas-txt">DNAS…</span>
    </div>
    <div class="ver-info">
      <span style="color:var(--teal)">v1.4.0</span> &middot; ICY&nbsp;2.2 &middot; Linux<br>
      &copy; <?= date('Y') ?> Mcaster1
    </div>
  </div>
</nav>

<!-- Main -->
<main class="main">
