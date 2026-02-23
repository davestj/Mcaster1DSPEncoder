<?php
define('MC1_BOOT', true);
require_once __DIR__ . '/inc/auth.php';
require_once __DIR__ . '/inc/db.php';

// ── Query params ───────────────────────────────────────────────────────────
$page   = max(1, (int)($_GET['page'] ?? 1));
$search = trim($_GET['q'] ?? '');
$limit  = 50;
$offset = ($page - 1) * $limit;

// ── Database queries ───────────────────────────────────────────────────────
$tracks   = [];
$total    = 0;
$pages    = 1;
$db_error = '';

if (mc1_is_authed()) {
    try {
        $db = mc1_db('mcaster1_media');

        $where  = '';
        $params = [];
        if ($search !== '') {
            $like   = '%' . $search . '%';
            $where  = 'WHERE title LIKE ? OR artist LIKE ? OR album LIKE ? OR genre LIKE ?';
            $params = [$like, $like, $like, $like];
        }

        $cs = $db->prepare("SELECT COUNT(*) FROM tracks $where");
        $cs->execute($params);
        $total  = (int)$cs->fetchColumn();
        $pages  = max(1, (int)ceil($total / $limit));
        $page   = min($page, $pages);
        $offset = ($page - 1) * $limit;

        $ts = $db->prepare(
            "SELECT id, title, artist, album, genre, year, duration_ms,
                    bitrate_kbps, play_count, is_missing, rating, date_added
             FROM tracks
             $where
             ORDER BY artist, title
             LIMIT ? OFFSET ?"
        );
        $ts->execute(array_merge($params, [$limit, $offset]));
        $tracks = $ts->fetchAll();

    } catch (Exception $e) {
        $db_error = h($e->getMessage());
    }
}

// ── Page render ────────────────────────────────────────────────────────────
$page_title = 'Media Library';
$active_nav = 'media';
require_once __DIR__ . '/inc/header.php';
?>

<?php if (!mc1_is_authed()): ?>
  <div class="alert alert-danger">
    <i class="fa-solid fa-lock"></i>
    <span>Authentication required. <a href="/login">Log in</a>.</span>
  </div>
<?php else: ?>

<!-- Search / filter bar -->
<div class="card" style="margin-bottom:16px">
  <div class="card-body" style="padding:12px 16px">
    <form method="GET" action="/app/media.php" class="search-bar">
      <input type="text" name="q" class="form-input"
             placeholder="Search title, artist, album, genre…"
             value="<?= h($search) ?>">
      <button type="submit" class="btn btn-primary">
        <i class="fa-solid fa-magnifying-glass"></i> Search
      </button>
      <?php if ($search !== ''): ?>
        <a href="/app/media.php" class="btn btn-secondary" style="text-decoration:none">
          <i class="fa-solid fa-xmark"></i> Clear
        </a>
      <?php endif; ?>
    </form>
  </div>
</div>

<?php if ($db_error !== ''): ?>
  <div class="alert alert-danger" style="margin-bottom:16px">
    <i class="fa-solid fa-triangle-exclamation"></i>
    <span>Database error: <?= $db_error ?></span>
  </div>
<?php endif; ?>

<!-- Tracks table -->
<div class="card">
  <div class="card-header">
    <span class="card-title">
      <i class="fa-solid fa-music"></i> Tracks
    </span>
    <span style="color:var(--muted);font-size:12px">
      <?= number_format($total) ?> track<?= $total !== 1 ? 's' : '' ?>
      <?= $search !== '' ? '(filtered)' : '' ?>
    </span>
  </div>
  <div class="card-body" style="padding:0">
    <div class="table-wrap">
      <table class="data-table">
        <thead>
          <tr>
            <th>#</th>
            <th>Title</th>
            <th>Artist</th>
            <th>Album</th>
            <th>Genre</th>
            <th>Year</th>
            <th>Duration</th>
            <th>kbps</th>
            <th>Plays</th>
            <th>Rating</th>
            <th>Status</th>
          </tr>
        </thead>
        <tbody>
          <?php if (empty($tracks)): ?>
            <tr>
              <td colspan="11"
                  style="text-align:center;color:var(--muted);padding:40px">
                <?= $search !== ''
                    ? 'No tracks match your search.'
                    : 'No tracks in library yet. Add tracks via <strong>POST /app/api/tracks.php</strong> with action=scan.' ?>
              </td>
            </tr>
          <?php else: ?>
            <?php foreach ($tracks as $i => $t): ?>
              <tr>
                <td class="mono" style="color:var(--muted);font-size:11px">
                  <?= $offset + $i + 1 ?>
                </td>
                <td><?= h($t['title'] ?? '(no title)') ?></td>
                <td style="color:var(--text-dim)"><?= h($t['artist'] ?? '—') ?></td>
                <td style="color:var(--text-dim)"><?= h($t['album']  ?? '—') ?></td>
                <td style="color:var(--muted)"><?= h($t['genre']  ?? '—') ?></td>
                <td style="color:var(--muted)"><?= h((string)($t['year'] ?? '')) ?></td>
                <td class="mono" style="color:var(--text-dim)">
                  <?= $t['duration_ms'] ? mc1_format_duration((int)$t['duration_ms']) : '—' ?>
                </td>
                <td style="color:var(--muted)">
                  <?= $t['bitrate_kbps'] ? (int)$t['bitrate_kbps'] : '—' ?>
                </td>
                <td class="mono"><?= (int)($t['play_count'] ?? 0) ?></td>
                <td style="color:var(--yellow)">
                  <?php
                  $rating = (int)($t['rating'] ?? 3);
                  echo str_repeat('★', $rating) . str_repeat('☆', 5 - $rating);
                  ?>
                </td>
                <td>
                  <?php if ($t['is_missing']): ?>
                    <span class="badge badge-error">Missing</span>
                  <?php else: ?>
                    <span class="badge badge-live">OK</span>
                  <?php endif; ?>
                </td>
              </tr>
            <?php endforeach; ?>
          <?php endif; ?>
        </tbody>
      </table>
    </div>

    <!-- Pagination -->
    <?php if ($pages > 1): ?>
      <div class="pagination">
        <?php if ($page > 1): ?>
          <a href="?<?= http_build_query(['q' => $search, 'page' => $page - 1]) ?>">
            <i class="fa-solid fa-chevron-left"></i>
          </a>
        <?php endif; ?>
        <?php
        $prev = 0;
        for ($p = 1; $p <= $pages; $p++):
            if ($p !== 1 && $p !== $pages && abs($p - $page) > 2) continue;
            if ($prev && $p - $prev > 1): ?>
              <span class="ellipsis">…</span>
            <?php endif;
            if ($p === $page): ?>
              <span class="current"><?= $p ?></span>
            <?php else: ?>
              <a href="?<?= http_build_query(['q' => $search, 'page' => $p]) ?>"><?= $p ?></a>
            <?php endif;
            $prev = $p;
        endfor; ?>
        <?php if ($page < $pages): ?>
          <a href="?<?= http_build_query(['q' => $search, 'page' => $page + 1]) ?>">
            <i class="fa-solid fa-chevron-right"></i>
          </a>
        <?php endif; ?>
      </div>
    <?php endif; ?>
  </div>
</div>

<?php endif; // mc1_is_authed ?>

<?php require_once __DIR__ . '/inc/footer.php'; ?>
