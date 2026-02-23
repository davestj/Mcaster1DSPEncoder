<?php
// header.php — Shared HTML page header for PHP app pages.
// Caller must set $page_title and $active_nav before including.
// Caller must also have already included auth.php and db.php.

if (!defined('MC1_BOOT')) {
    http_response_code(403);
    exit('403 Forbidden');
}

$page_title = $page_title ?? 'Mcaster1';
$active_nav = $active_nav ?? '';
?>
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title><?= h($page_title) ?> — Mcaster1 DSP Encoder</title>
  <link rel="stylesheet" href="/style.css">
  <link rel="stylesheet"
        href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.5.1/css/all.min.css"
        crossorigin="anonymous" referrerpolicy="no-referrer">
  <style>
    /* PHP pages: anchor nav-items inherit sidebar colour, not default link blue */
    a.nav-item { color: inherit; text-decoration: none; display: flex; }
    a.nav-item:hover { color: var(--text); }
    a.nav-item.active { color: var(--teal); }
    .search-bar { display:flex; gap:8px; margin-bottom:16px; }
    .search-bar .form-input { flex:1; }
    .pagination { display:flex; gap:6px; align-items:center;
                  justify-content:flex-end; padding:12px 16px; }
    .pagination a, .pagination span {
      display:inline-flex; align-items:center; justify-content:center;
      min-width:32px; height:32px; padding:0 8px;
      border:1px solid var(--border2); border-radius:var(--radius-sm);
      font-size:12px; color:var(--text-dim);
    }
    .pagination a { text-decoration:none; }
    .pagination a:hover { border-color:var(--teal); color:var(--teal); }
    .pagination span.current { background:rgba(20,184,166,.15);
                                border-color:var(--teal); color:var(--teal); }
    .pagination span.ellipsis { border:none; color:var(--muted); }
  </style>
</head>
<body class="dashboard-page">

<!-- ── Top bar ──────────────────────────────────────────────────────────── -->
<header class="topbar">
  <div class="topbar-brand">
    <div class="topbar-logo">M<span style="opacity:.5">1</span></div>
    <div>
      <div class="topbar-name">Mcaster1</div>
      <div class="topbar-sub">DSP Encoder</div>
    </div>
  </div>
  <div class="topbar-sep"></div>
  <div class="topbar-title"><?= h($page_title) ?></div>
  <div class="topbar-status"></div>
  <a href="/dashboard" class="btn btn-secondary btn-sm"
     style="text-decoration:none">
    <i class="fa-solid fa-gauge"></i> Dashboard
  </a>
</header>

<!-- ── Body ─────────────────────────────────────────────────────────────── -->
<div class="dash-body">

  <!-- Sidebar -->
  <nav class="sidebar">
    <div class="sidebar-section-label">Monitor</div>
    <a class="nav-item" href="/dashboard">
      <i class="fa-solid fa-gauge"></i> Dashboard
    </a>

    <div class="sidebar-section-label">Library</div>
    <a class="nav-item <?= $active_nav === 'media' ? 'active' : '' ?>"
       href="/app/media.php">
      <i class="fa-solid fa-music"></i> Media Library
    </a>
    <a class="nav-item <?= $active_nav === 'playlists' ? 'active' : '' ?>"
       href="/app/playlists.php">
      <i class="fa-solid fa-list-ul"></i> Playlists
    </a>

    <div class="sidebar-section-label">Analytics</div>
    <a class="nav-item <?= $active_nav === 'metrics' ? 'active' : '' ?>"
       href="/app/metrics.php">
      <i class="fa-solid fa-chart-line"></i> Listener Metrics
    </a>

    <div class="sidebar-spacer"></div>
    <div class="sidebar-footer">
      <div style="font-size:11px;color:var(--muted)">
        <i class="fa-solid fa-circle-info" style="margin-right:4px"></i>
        v1.2.0 &nbsp;&middot;&nbsp; ICY 2.2
      </div>
    </div>
  </nav>

  <!-- Main content -->
  <main class="main-content">
