<?php
/**
 * daemon_monitor.php -- Reusable daemon health monitor widget
 *
 * File:    src/linux/web_ui/app/inc/daemon_monitor.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Purpose: Displays status cards for all 4 system daemons (Admin, Encoder,
 *          VoicTune, Producer). Mode-aware: only shows daemons relevant to
 *          the current app mode. Designed to be included in dashboard.php.
 *
 * Standards:
 *   - No exit()/die() -- uopz extension active
 *   - h() on all user-derived data
 *   - DOMContentLoaded for JS init
 */

if (!defined('MC1_BOOT')) {
    http_response_code(403);
    echo '403 Forbidden';
    return;
}

/**
 * Check a daemon's health by HTTP request with 2s timeout
 */
function mc1_check_daemon($name, $host, $port, $path) {
    $ctx = stream_context_create(['http' => [
        'timeout' => 2,
        'method'  => 'GET',
        'header'  => "Accept: application/json\r\n",
    ]]);
    $url = "http://{$host}:{$port}{$path}";
    $resp = @file_get_contents($url, false, $ctx);
    if ($resp === false) {
        return ['status' => 'down', 'name' => $name, 'port' => $port];
    }
    $data = json_decode($resp, true);
    if (!is_array($data)) {
        return ['status' => 'running', 'name' => $name, 'port' => $port, 'data' => []];
    }
    return ['status' => 'running', 'name' => $name, 'port' => $port, 'data' => $data];
}

/**
 * Get process info from /proc if available
 */
function mc1_proc_info($search_name) {
    $result = ['pid' => null, 'rss_mb' => null, 'uptime' => null, 'cpu_pct' => null];
    $output = [];
    @exec("pgrep -f " . escapeshellarg($search_name) . " 2>/dev/null", $output);
    if (empty($output)) {
        return $result;
    }
    $pid = (int)trim($output[0]);
    $result['pid'] = $pid;

    /* RSS from /proc/PID/status */
    $status_file = "/proc/{$pid}/status";
    if (is_readable($status_file)) {
        $lines = @file($status_file, FILE_IGNORE_NEW_LINES);
        if (is_array($lines)) {
            foreach ($lines as $line) {
                if (strpos($line, 'VmRSS:') === 0) {
                    $kb = (int)preg_replace('/[^0-9]/', '', $line);
                    $result['rss_mb'] = round($kb / 1024, 1);
                    break;
                }
            }
        }
    }

    /* Uptime from /proc/PID/stat field 22 (starttime in clock ticks) */
    $stat_file = "/proc/{$pid}/stat";
    if (is_readable($stat_file)) {
        $stat_raw = @file_get_contents($stat_file);
        if ($stat_raw !== false) {
            /* Skip past the comm field (may contain spaces/parens) */
            $after_comm = strrchr($stat_raw, ')');
            if ($after_comm) {
                $fields = preg_split('/\s+/', trim(substr($after_comm, 2)));
                /* Field 20 after comm-close = starttime (index 19 in 0-based after ') ') */
                if (isset($fields[19])) {
                    $starttime_ticks = (float)$fields[19];
                    $uptime_raw = @file_get_contents('/proc/uptime');
                    if ($uptime_raw !== false) {
                        $sys_uptime = (float)$uptime_raw;
                        $clk_tck = 100; /* sysconf(_SC_CLK_TCK) is almost always 100 on Linux */
                        $proc_start_sec = $starttime_ticks / $clk_tck;
                        $result['uptime'] = max(0, (int)($sys_uptime - $proc_start_sec));
                    }
                }
            }
        }
    }

    return $result;
}

/**
 * Format seconds to human-readable uptime
 */
function mc1_fmt_uptime($seconds) {
    if ($seconds === null) return '--';
    $d = (int)($seconds / 86400);
    $h = (int)(($seconds % 86400) / 3600);
    $m = (int)(($seconds % 3600) / 60);
    if ($d > 0) return $d . 'd ' . $h . 'h';
    if ($h > 0) return $h . 'h ' . $m . 'm';
    return $m . 'm';
}

/* We define daemon configurations */
$daemon_defs = [
    'admin' => [
        'name'   => 'Admin',
        'port'   => 8330,
        'host'   => '127.0.0.1',
        'path'   => '/api/v1/status',
        'proc'   => 'mcaster1-dsp-encoder-admin',
        'icon'   => 'fa-server',
        'modes'  => ['broadcast','dj','podcast','producer','all'],
    ],
    'encoder' => [
        'name'   => 'Encoder',
        'port'   => 8331,
        'host'   => '127.0.0.1',
        'path'   => '',
        'proc'   => 'mcaster1-dsp-encoder --config',
        'proc_only' => true,
        'icon'   => 'fa-tower-broadcast',
        'modes'  => ['broadcast','dj','podcast','producer','all'],
    ],
    'voictune' => [
        'name'   => 'VoicTune',
        'port'   => 8350,
        'host'   => '127.0.0.1',
        'path'   => '/api/v1/voictune/health',
        'proc'   => 'mcaster1-voictune',
        'icon'   => 'fa-microphone-lines',
        'modes'  => ['podcast','producer','all'],
    ],
    'producer' => [
        'name'   => 'Producer',
        'port'   => 8360,
        'host'   => '127.0.0.1',
        'path'   => '/api/v1/producer/health',
        'proc'   => 'mcaster1-producer',
        'icon'   => 'fa-tv',
        'modes'  => ['producer','all'],
    ],
];

/* Admin is always running (it serves this page) */
$admin_proc = mc1_proc_info($daemon_defs['admin']['proc']);
$daemon_results = [];
foreach ($daemon_defs as $key => $def) {
    $proc = mc1_proc_info($def['proc']);
    if ($key === 'admin') {
        /* Admin is always considered running since it serves this page */
        $daemon_results[$key] = [
            'status'  => 'running',
            'name'    => $def['name'],
            'port'    => $def['port'],
            'icon'    => $def['icon'],
            'modes'   => $def['modes'],
            'rss_mb'  => $proc['rss_mb'],
            'uptime'  => $proc['uptime'],
            'pid'     => $proc['pid'],
            'version' => null,
        ];
    } else {
        $health = null;
        $is_proc_only = !empty($def['proc_only']);
        if ($proc['pid'] !== null && !$is_proc_only && !empty($def['path'])) {
            $health = mc1_check_daemon($def['name'], $def['host'], $def['port'], $def['path']);
        }
        $status = 'not_configured';
        if ($proc['pid'] !== null && ($is_proc_only || ($health && $health['status'] === 'running'))) {
            $status = 'running';
        } elseif ($proc['pid'] !== null) {
            $status = 'down'; /* process exists but not responding */
        }
        $daemon_results[$key] = [
            'status'  => $status,
            'name'    => $def['name'],
            'port'    => $def['port'],
            'icon'    => $def['icon'],
            'modes'   => $def['modes'],
            'rss_mb'  => $proc['rss_mb'],
            'uptime'  => $proc['uptime'],
            'pid'     => $proc['pid'],
            'version' => ($health && isset($health['data']['version'])) ? $health['data']['version'] : null,
        ];
    }
}
?>

<div class="card" id="daemon-monitor-card">
  <div class="card-hdr">
    <div class="card-title"><i class="fa-solid fa-heartbeat fa-fw" style="color:var(--teal)"></i> System Daemons</div>
    <span class="badge badge-gray" id="daemon-count-badge">
      <?= count(array_filter($daemon_results, function($d){ return $d['status'] === 'running'; })) ?> / <?= count($daemon_results) ?> running
    </span>
  </div>
  <div style="display:grid;grid-template-columns:repeat(4,1fr);gap:10px" id="daemon-grid">
    <?php foreach ($daemon_results as $key => $d):
      $color_map = ['running' => 'var(--green)', 'down' => 'var(--red)', 'not_configured' => 'var(--muted)'];
      $status_color = $color_map[$d['status']] ?? 'var(--muted)';
      $status_label = ['running' => 'Running', 'down' => 'DOWN', 'not_configured' => 'Not started'][$d['status']] ?? 'Unknown';
      $modes_attr = implode(',', $d['modes']);
    ?>
    <div class="daemon-card" data-daemon="<?= h($key) ?>" data-daemon-modes="<?= h($modes_attr) ?>"
         style="background:var(--bg3);border:1px solid var(--border);border-radius:var(--radius-sm);padding:12px;text-align:center;position:relative;transition:border-color .3s<?= $d['status'] === 'running' ? ';border-color:rgba(34,197,94,.3)' : ($d['status'] === 'down' ? ';border-color:rgba(239,68,68,.3)' : '') ?>">
      <div style="display:flex;align-items:center;justify-content:center;gap:6px;margin-bottom:8px">
        <span style="width:8px;height:8px;border-radius:50%;background:<?= $status_color ?>;display:inline-block;flex-shrink:0<?= $d['status'] === 'running' ? ';box-shadow:0 0 6px ' . $status_color : '' ?>"></span>
        <span style="font-weight:700;font-size:13px;color:var(--text)"><i class="fa-solid <?= h($d['icon']) ?>" style="margin-right:4px;opacity:.6"></i> <?= h($d['name']) ?></span>
      </div>
      <div style="font-size:11px;color:var(--muted);margin-bottom:4px">:<?= (int)$d['port'] ?></div>
      <?php if ($d['rss_mb'] !== null): ?>
      <div style="font-size:11px;color:var(--text-dim)"><?= $d['rss_mb'] ?> MB</div>
      <?php endif; ?>
      <div style="font-size:11px;color:<?= $status_color ?>;font-weight:600;margin-top:4px">
        <?php if ($d['status'] === 'running' && $d['uptime'] !== null): ?>
          up <?= mc1_fmt_uptime($d['uptime']) ?>
        <?php else: ?>
          <?= h($status_label) ?>
        <?php endif; ?>
      </div>
      <?php if ($d['version']): ?>
      <div style="font-size:10px;color:var(--muted);margin-top:2px">v<?= h($d['version']) ?></div>
      <?php endif; ?>
    </div>
    <?php endforeach; ?>
  </div>
</div>

<script>
/* Daemon monitor: mode-aware visibility + 30s polling */
(function(){
  function updateDaemonVisibility() {
    var mode = localStorage.getItem('mc1_app_mode') || 'all';
    var cards = document.querySelectorAll('.daemon-card[data-daemon-modes]');
    for (var i = 0; i < cards.length; i++) {
      var modes = cards[i].getAttribute('data-daemon-modes').split(',');
      cards[i].style.display = (mode === 'all' || modes.indexOf(mode) !== -1) ? '' : 'none';
    }
    /* Adjust grid columns based on visible count */
    var visible = document.querySelectorAll('.daemon-card[data-daemon-modes]:not([style*="display: none"])');
    var grid = document.getElementById('daemon-grid');
    if (grid) {
      var cols = Math.min(4, visible.length || 1);
      grid.style.gridTemplateColumns = 'repeat(' + cols + ', 1fr)';
    }
  }

  /* Listen for mode changes from the mode selector */
  window.addEventListener('mc1-mode-changed', updateDaemonVisibility);

  document.addEventListener('DOMContentLoaded', function() {
    updateDaemonVisibility();

    /* Poll daemon status every 30s via API */
    function pollDaemons() {
      if (!window.mc1Api) return;
      mc1Api('POST', '/app/api/health.php', { action: 'daemon_status' }).then(function(d) {
        if (!d || !d.ok || !d.daemons) return;
        var running = 0, total = 0;
        for (var key in d.daemons) {
          total++;
          var dm = d.daemons[key];
          var card = document.querySelector('.daemon-card[data-daemon="' + key + '"]');
          if (!card) continue;
          if (dm.status === 'running') running++;

          var colorMap = { running: 'var(--green)', down: 'var(--red)', not_configured: 'var(--muted)' };
          var color = colorMap[dm.status] || 'var(--muted)';
          var dot = card.querySelector('span[style*="border-radius:50%"]');
          if (dot) {
            dot.style.background = color;
            dot.style.boxShadow = dm.status === 'running' ? '0 0 6px ' + color : 'none';
          }
          card.style.borderColor = dm.status === 'running' ? 'rgba(34,197,94,.3)' : (dm.status === 'down' ? 'rgba(239,68,68,.3)' : 'var(--border)');

          /* Update status text (last div child) */
          var divs = card.querySelectorAll('div');
          var lastDiv = divs[divs.length - 1];
          /* Find the status line - it has font-weight:600 */
          for (var i = 0; i < divs.length; i++) {
            if (divs[i].style.fontWeight === '600') {
              divs[i].style.color = color;
              if (dm.status === 'running' && dm.uptime !== null) {
                divs[i].textContent = 'up ' + fmtDaemonUp(dm.uptime);
              } else {
                divs[i].textContent = dm.status === 'down' ? 'DOWN' : 'Not started';
              }
              break;
            }
          }

          /* Update memory */
          if (dm.rss_mb !== null) {
            var memDivs = card.querySelectorAll('div[style*="color:var(--text-dim)"]');
            if (memDivs.length > 0) memDivs[0].textContent = dm.rss_mb + ' MB';
          }
        }
        var badge = document.getElementById('daemon-count-badge');
        if (badge) badge.textContent = running + ' / ' + total + ' running';
      }).catch(function() { /* daemon poll failed */ });
    }

    function fmtDaemonUp(s) {
      if (s === null) return '--';
      var d = Math.floor(s / 86400);
      var h = Math.floor((s % 86400) / 3600);
      var m = Math.floor((s % 3600) / 60);
      if (d > 0) return d + 'd ' + h + 'h';
      if (h > 0) return h + 'h ' + m + 'm';
      return m + 'm';
    }

    setInterval(pollDaemons, 30000);
  });
})();
</script>
