<?php
/**
 * schedule.php — Clockwheel Schedule Manager
 *
 * 24-hour grid: rows=hours (0-23), columns=encoder slots.
 * Click a cell to assign a playlist for that slot at that hour.
 * Supports day-of-week filtering and manual overrides.
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/app/inc/auth.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/user_auth.php';

$page_title = 'Schedule';
$active_nav = 'schedule';

/* We load slots for column headers */
$slots = [];
try {
    $pdo = mc1_db('mcaster1_encoder');
    $slots = $pdo->query("SELECT slot_id, name, clockwheel_enabled FROM encoder_configs ORDER BY slot_id")->fetchAll(PDO::FETCH_ASSOC);
} catch (Exception $e) {}

require_once __DIR__ . '/app/inc/header.php';
?>

<style>
.sched-page { padding:24px; }
.sched-header { display:flex; align-items:center; justify-content:space-between; margin-bottom:20px; flex-wrap:wrap; gap:12px; }
.sched-header h2 { margin:0; font-size:1.4rem; color:var(--text); }
.sched-toolbar { display:flex; gap:8px; }
.sched-toolbar select { background:var(--bg3); border:1px solid var(--border); color:var(--text); padding:6px 10px; border-radius:var(--radius-xs); font-size:.8rem; }
.sched-toolbar button { background:var(--teal); border:none; color:#fff; padding:7px 14px; border-radius:var(--radius-xs); cursor:pointer; font-size:.8rem; font-weight:600; }

/* ── 24-Hour Grid ─────────────────────────────── */
.sched-grid { overflow-x:auto; }
.sched-grid table { border-collapse:collapse; width:100%; min-width:600px; }
.sched-grid th { padding:8px 6px; font-size:.7rem; color:var(--muted); text-transform:uppercase; border-bottom:1px solid var(--border); background:var(--bg2); position:sticky; top:0; z-index:1; }
.sched-grid td { padding:0; border:1px solid var(--border2); height:36px; }
.sched-grid .hour-label { width:60px; min-width:60px; padding:6px 8px; font-family:var(--font-mono); font-size:.8rem; color:var(--text-dim); background:var(--bg2); text-align:center; border-right:1px solid var(--border); }
.sched-cell { width:100%; height:100%; padding:4px 6px; cursor:pointer; font-size:.7rem; color:var(--text-dim); transition:background .15s; display:flex; align-items:center; overflow:hidden; white-space:nowrap; text-overflow:ellipsis; min-height:34px; }
.sched-cell:hover { background:var(--bg3); }
.sched-cell.assigned { background:var(--teal-glow); color:var(--teal); font-weight:600; }
.sched-cell.current-hour { border-left:3px solid var(--teal); }
.sched-cell .sched-x { margin-left:auto; opacity:0; cursor:pointer; color:var(--red); padding:2px 4px; font-size:.7rem; }
.sched-cell:hover .sched-x { opacity:1; }

/* ── Assign Modal ─────────────────────────────── */
.sched-modal-bg { display:none; position:fixed; inset:0; background:rgba(0,0,0,.6); z-index:100; align-items:center; justify-content:center; }
.sched-modal-bg.open { display:flex; }
.sched-modal { background:var(--card); border:1px solid var(--border); border-radius:var(--radius); padding:24px; width:400px; max-width:90vw; }
.sched-modal h3 { margin:0 0 16px; color:var(--text); font-size:1rem; }
.sched-modal label { display:block; font-size:.75rem; color:var(--text-dim); margin-bottom:4px; }
.sched-modal select, .sched-modal input { width:100%; background:var(--bg3); border:1px solid var(--border); color:var(--text); padding:8px; border-radius:var(--radius-xs); font-size:.85rem; margin-bottom:12px; }
.sched-modal-actions { display:flex; gap:8px; justify-content:flex-end; }
.sched-modal-actions button { padding:8px 16px; border-radius:var(--radius-xs); cursor:pointer; font-size:.85rem; border:none; }
.sched-modal-actions .btn-cancel { background:var(--bg3); border:1px solid var(--border); color:var(--text-dim); }
.sched-modal-actions .btn-save { background:var(--teal); color:#fff; font-weight:600; }
</style>

<div class="sched-page">
  <div class="sched-header">
    <h2><i class="fa-solid fa-clock" style="color:var(--teal);margin-right:8px"></i>Clockwheel Schedule</h2>
    <div class="sched-toolbar">
      <select id="sched-dow">
        <option value="-1">Every Day</option>
        <option value="0">Sunday</option>
        <option value="1">Monday</option>
        <option value="2">Tuesday</option>
        <option value="3">Wednesday</option>
        <option value="4">Thursday</option>
        <option value="5">Friday</option>
        <option value="6">Saturday</option>
      </select>
      <button onclick="loadSchedule()"><i class="fa-solid fa-rotate"></i> Refresh</button>
    </div>
  </div>

  <div class="sched-grid">
    <table>
      <thead>
        <tr>
          <th>Hour</th>
          <?php foreach ($slots as $s): ?>
          <th>Slot <?= (int)$s['slot_id'] ?><br><span style="font-weight:400;font-size:.6rem"><?= h($s['name']) ?></span></th>
          <?php endforeach; ?>
        </tr>
      </thead>
      <tbody id="sched-body"></tbody>
    </table>
  </div>
</div>

<!-- Assign Modal -->
<div class="sched-modal-bg" id="sched-modal-bg" onclick="if(event.target===this)closeModal()">
  <div class="sched-modal">
    <h3 id="modal-title">Assign Playlist</h3>
    <label>Playlist File</label>
    <select id="modal-playlist"></select>
    <label>Name / Label (optional)</label>
    <input type="text" id="modal-name" placeholder="e.g. Rock Hour">
    <input type="hidden" id="modal-slot">
    <input type="hidden" id="modal-hour">
    <div class="sched-modal-actions">
      <button class="btn-cancel" onclick="closeModal()">Cancel</button>
      <button class="btn-save" onclick="saveAssignment()">Save</button>
    </div>
  </div>
</div>

<script>
var SLOTS = <?= json_encode(array_map(function($s){ return ['slot_id'=>(int)$s['slot_id'],'name'=>$s['name']]; }, $slots)) ?>;
var scheduleData = [];
var playlistFiles = [];
var dbPlaylists = [];

function loadSchedule() {
  mc1Api('POST', '/app/api/schedule.php', {action:'get_schedule'}).then(function(d) {
    if (!d || !d.ok) return;
    scheduleData = d.schedule || [];
    renderGrid();
  });
}

function loadPlaylists() {
  mc1Api('POST', '/app/api/schedule.php', {action:'get_playlists'}).then(function(d) {
    if (!d || !d.ok) return;
    dbPlaylists = d.playlists || [];
    playlistFiles = d.files || [];
  });
}

function renderGrid() {
  var dow = parseInt(document.getElementById('sched-dow').value);
  var now = new Date().getHours();
  var html = '';

  for (var h = 0; h < 24; h++) {
    var label = (h < 10 ? '0' : '') + h + ':00';
    html += '<tr><td class="hour-label">' + label + '</td>';
    for (var si = 0; si < SLOTS.length; si++) {
      var slot = SLOTS[si].slot_id;
      /* We find assignment for this slot+hour+dow */
      var entry = null;
      for (var i = 0; i < scheduleData.length; i++) {
        var e = scheduleData[i];
        if (parseInt(e.slot_id) === slot && parseInt(e.hour) === h) {
          if (dow === -1 || parseInt(e.day_of_week) === dow || parseInt(e.day_of_week) === -1) {
            entry = e;
            break;
          }
        }
      }
      var cls = 'sched-cell' + (entry ? ' assigned' : '') + (h === now ? ' current-hour' : '');
      var text = entry ? (entry.name || entry.playlist_path || 'Assigned') : '';
      var xBtn = entry ? '<span class="sched-x" onclick="event.stopPropagation();deleteEntry(' + entry.id + ')">&times;</span>' : '';
      html += '<td><div class="' + cls + '" onclick="openModal(' + slot + ',' + h + ')">' + text + xBtn + '</div></td>';
    }
    html += '</tr>';
  }
  document.getElementById('sched-body').innerHTML = html;
}

function openModal(slot, hour) {
  document.getElementById('modal-slot').value = slot;
  document.getElementById('modal-hour').value = hour;
  document.getElementById('modal-title').textContent = 'Assign Playlist — Slot ' + slot + ' @ ' + (hour < 10 ? '0' : '') + hour + ':00';
  document.getElementById('modal-name').value = '';

  /* We populate the playlist dropdown */
  var sel = document.getElementById('modal-playlist');
  sel.innerHTML = '<option value="">— Select —</option>';
  dbPlaylists.forEach(function(p) {
    sel.innerHTML += '<option value="playlist:' + p.id + '">' + p.name + ' (DB)</option>';
  });
  playlistFiles.forEach(function(f) {
    sel.innerHTML += '<option value="file:' + f.path + '">' + f.name + '</option>';
  });

  document.getElementById('sched-modal-bg').classList.add('open');
}

function closeModal() {
  document.getElementById('sched-modal-bg').classList.remove('open');
}

function saveAssignment() {
  var slot = parseInt(document.getElementById('modal-slot').value);
  var hour = parseInt(document.getElementById('modal-hour').value);
  var dow  = parseInt(document.getElementById('sched-dow').value);
  var name = document.getElementById('modal-name').value;
  var val  = document.getElementById('modal-playlist').value;

  var playlist_id = 0, playlist_path = '';
  if (val.startsWith('playlist:')) playlist_id = parseInt(val.split(':')[1]);
  else if (val.startsWith('file:')) playlist_path = val.substring(5);

  if (!playlist_id && !playlist_path) { mc1Toast('Select a playlist', 'warn'); return; }

  mc1Api('POST', '/app/api/schedule.php', {
    action: 'save_assignment',
    slot_id: slot, hour: hour, day_of_week: dow,
    name: name, playlist_id: playlist_id, playlist_path: playlist_path
  }).then(function(d) {
    if (d && d.ok) { mc1Toast('Saved', 'ok'); closeModal(); loadSchedule(); }
    else mc1Toast((d && d.error) || 'Save failed', 'err');
  });
}

function deleteEntry(id) {
  mc1Api('POST', '/app/api/schedule.php', {action:'delete_assignment', id:id}).then(function(d) {
    if (d && d.ok) { mc1Toast('Removed', 'ok'); loadSchedule(); }
  });
}

document.addEventListener('DOMContentLoaded', function() {
  loadSchedule();
  loadPlaylists();
  document.getElementById('sched-dow').addEventListener('change', renderGrid);
  /* We refresh every 60 seconds to update current-hour highlight */
  setInterval(function(){ renderGrid(); }, 60000);
});
</script>

<?php require_once __DIR__ . '/app/inc/footer.php'; ?>
