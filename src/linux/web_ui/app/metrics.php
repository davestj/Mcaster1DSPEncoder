<?php
define('MC1_BOOT', true);
require_once __DIR__ . '/inc/auth.php';
require_once __DIR__ . '/inc/db.php';

// ── Database queries ───────────────────────────────────────────────────────
$db_error    = '';
$daily_stats = [];
$sessions    = [];
$top_tracks  = [];
$summary     = ['total_sessions' => 0, 'unique_ips' => 0,
                'total_hours'    => 0.0, 'avg_listen_min' => 0.0];

if (mc1_is_authed()) {
    try {
        $db  = mc1_db('mcaster1_metrics');

        $row = $db->query(
            "SELECT COUNT(*)                              AS total_sessions,
                    COUNT(DISTINCT client_ip)             AS unique_ips,
                    COALESCE(SUM(duration_sec)/3600,  0) AS total_hours,
                    COALESCE(AVG(duration_sec)/60,    0) AS avg_listen_min
             FROM listener_sessions
             WHERE disconnected_at IS NOT NULL"
        )->fetch();
        if ($row) $summary = $row;

        $daily_stats = $db->query(
            "SELECT stat_date, mount, peak_listeners, total_sessions, total_hours
             FROM daily_stats
             ORDER BY stat_date DESC
             LIMIT 30"
        )->fetchAll();

        $sessions = $db->query(
            "SELECT client_ip, user_agent, stream_mount,
                    connected_at, disconnected_at, duration_sec, bytes_sent
             FROM listener_sessions
             ORDER BY connected_at DESC
             LIMIT 50"
        )->fetchAll();

    } catch (Exception $e) {
        $db_error = h($e->getMessage());
    }

    try {
        $mdb        = mc1_db('mcaster1_media');
        $top_tracks = $mdb->query(
            "SELECT title, artist, play_count, last_played_at
             FROM tracks
             WHERE play_count > 0
             ORDER BY play_count DESC
             LIMIT 20"
        )->fetchAll();
    } catch (Exception $e) {
        // No plays yet — non-fatal
    }
}

// ── Page render ────────────────────────────────────────────────────────────
$page_title = 'Listener Metrics';
$active_nav = 'metrics';
require_once __DIR__ . '/inc/header.php';
?>

<?php if (!mc1_is_authed()): ?>
  <div class="alert alert-danger">
    <i class="fa-solid fa-lock"></i>
    <span>Authentication required. <a href="/login">Log in</a>.</span>
  </div>
<?php else: ?>

<?php if ($db_error !== ''): ?>
  <div class="alert alert-danger" style="margin-bottom:16px">
    <i class="fa-solid fa-triangle-exclamation"></i>
    <span>Database error: <?= $db_error ?></span>
  </div>
<?php endif; ?>

<!-- Summary stat cards -->
<div class="stat-grid" style="margin-bottom:20px">
  <div class="stat-card">
    <div class="stat-icon stat-icon-teal">
      <i class="fa-solid fa-headphones"></i>
    </div>
    <div>
      <div class="stat-value"><?= number_format((int)$summary['total_sessions']) ?></div>
      <div class="stat-label">Total Sessions</div>
    </div>
  </div>
  <div class="stat-card">
    <div class="stat-icon stat-icon-cyan">
      <i class="fa-solid fa-globe"></i>
    </div>
    <div>
      <div class="stat-value"><?= number_format((int)$summary['unique_ips']) ?></div>
      <div class="stat-label">Unique IPs</div>
    </div>
  </div>
  <div class="stat-card">
    <div class="stat-icon stat-icon-green">
      <i class="fa-regular fa-clock"></i>
    </div>
    <div>
      <div class="stat-value mono">
        <?= number_format((float)$summary['total_hours'], 1) ?> h
      </div>
      <div class="stat-label">Total Listen Hours</div>
    </div>
  </div>
  <div class="stat-card">
    <div class="stat-icon stat-icon-yellow">
      <i class="fa-solid fa-stopwatch"></i>
    </div>
    <div>
      <div class="stat-value mono">
        <?= number_format((float)$summary['avg_listen_min'], 1) ?> min
      </div>
      <div class="stat-label">Avg Listen Duration</div>
    </div>
  </div>
</div>

<div class="two-col" style="margin-bottom:20px">

  <!-- Daily stats -->
  <div class="card">
    <div class="card-header">
      <span class="card-title">
        <i class="fa-solid fa-calendar-days"></i> Daily Stats (Last 30 Days)
      </span>
    </div>
    <div class="card-body" style="padding:0">
      <div class="table-wrap">
        <table class="data-table">
          <thead>
            <tr><th>Date</th><th>Mount</th><th>Peak</th><th>Sessions</th><th>Hours</th></tr>
          </thead>
          <tbody>
            <?php if (empty($daily_stats)): ?>
              <tr><td colspan="5" style="text-align:center;color:var(--muted);padding:32px">
                No daily stats recorded yet.
              </td></tr>
            <?php else: ?>
              <?php foreach ($daily_stats as $d): ?>
                <tr>
                  <td class="mono" style="font-size:12px"><?= h($d['stat_date']) ?></td>
                  <td style="color:var(--teal);font-size:12px"><?= h($d['mount'] ?? '—') ?></td>
                  <td><?= (int)($d['peak_listeners'] ?? 0) ?></td>
                  <td><?= (int)($d['total_sessions'] ?? 0) ?></td>
                  <td class="mono"><?= number_format((float)($d['total_hours'] ?? 0), 1) ?></td>
                </tr>
              <?php endforeach; ?>
            <?php endif; ?>
          </tbody>
        </table>
      </div>
    </div>
  </div>

  <!-- Top tracks -->
  <div class="card">
    <div class="card-header">
      <span class="card-title">
        <i class="fa-solid fa-fire"></i> Most Played Tracks
      </span>
    </div>
    <div class="card-body" style="padding:0">
      <div class="table-wrap">
        <table class="data-table">
          <thead>
            <tr><th>#</th><th>Title</th><th>Artist</th><th>Plays</th></tr>
          </thead>
          <tbody>
            <?php if (empty($top_tracks)): ?>
              <tr><td colspan="4" style="text-align:center;color:var(--muted);padding:32px">
                No plays recorded yet.
              </td></tr>
            <?php else: ?>
              <?php foreach ($top_tracks as $i => $t): ?>
                <tr>
                  <td style="color:var(--muted)"><?= $i + 1 ?></td>
                  <td><?= h($t['title'] ?? '(no title)') ?></td>
                  <td style="color:var(--text-dim)"><?= h($t['artist'] ?? '—') ?></td>
                  <td class="mono"><?= (int)$t['play_count'] ?></td>
                </tr>
              <?php endforeach; ?>
            <?php endif; ?>
          </tbody>
        </table>
      </div>
    </div>
  </div>

</div>

<!-- Recent sessions -->
<div class="card">
  <div class="card-header">
    <span class="card-title">
      <i class="fa-solid fa-list-ul"></i> Recent Listener Sessions
    </span>
  </div>
  <div class="card-body" style="padding:0">
    <div class="table-wrap">
      <table class="data-table">
        <thead>
          <tr>
            <th>IP Address</th><th>Mount</th><th>Connected</th>
            <th>Duration</th><th>Bytes</th><th>User Agent</th>
          </tr>
        </thead>
        <tbody>
          <?php if (empty($sessions)): ?>
            <tr><td colspan="6" style="text-align:center;color:var(--muted);padding:32px">
              No listener sessions recorded yet.
            </td></tr>
          <?php else: ?>
            <?php foreach ($sessions as $s): ?>
              <tr>
                <td class="mono" style="font-size:12px"><?= h($s['client_ip'] ?? '—') ?></td>
                <td style="color:var(--teal);font-size:12px"><?= h($s['stream_mount'] ?? '—') ?></td>
                <td style="font-size:12px;color:var(--text-dim)"><?= h($s['connected_at'] ?? '—') ?></td>
                <td class="mono">
                  <?php $dur = (int)($s['duration_sec'] ?? 0);
                  echo $dur > 0 ? floor($dur/60) . ':' . sprintf('%02d', $dur % 60) : '—'; ?>
                </td>
                <td class="mono" style="font-size:12px">
                  <?php $b = (int)($s['bytes_sent'] ?? 0);
                  if ($b > 1048576) echo number_format($b/1048576, 1) . ' MB';
                  elseif ($b > 1024) echo number_format($b/1024, 0) . ' KB';
                  else echo $b > 0 ? $b . ' B' : '—'; ?>
                </td>
                <td style="font-size:11px;color:var(--muted);max-width:200px;
                           overflow:hidden;text-overflow:ellipsis;white-space:nowrap">
                  <?= h(substr($s['user_agent'] ?? '', 0, 80)) ?>
                </td>
              </tr>
            <?php endforeach; ?>
          <?php endif; ?>
        </tbody>
      </table>
    </div>
  </div>
</div>

<?php endif; // mc1_is_authed ?>

<?php require_once __DIR__ . '/inc/footer.php'; ?>
