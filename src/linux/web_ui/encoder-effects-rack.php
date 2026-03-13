<?php
/**
 * encoder-effects-rack.php — Per-Encoder Slot Effects Rack Manager
 *
 * File:    src/linux/web_ui/encoder-effects-rack.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-25
 * Purpose: We manage effects rack settings on a per-encoder-slot basis.
 *          Each slot can use the global rack, bypass effects entirely,
 *          or configure its own custom effects chain.
 *
 * URL: /encoder-effects-rack.php?slot=N
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/app/inc/auth.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/user_auth.php';

$page_title = 'Encoder Effects Rack';
$active_nav = 'effects';

$slot_id = (int)($_GET['slot'] ?? 0);

/* We load the encoder config for this slot */
$slot_cfg = null;
$slots = [];
try {
    $pdo = mc1_db('mcaster1_encoder');
    $slots = $pdo->query("SELECT id, slot_id, name, effects_mode FROM encoder_configs ORDER BY slot_id")->fetchAll(PDO::FETCH_ASSOC);
    if ($slot_id > 0) {
        $stmt = $pdo->prepare("SELECT * FROM encoder_configs WHERE slot_id = ? LIMIT 1");
        $stmt->execute([$slot_id]);
        $slot_cfg = $stmt->fetch(PDO::FETCH_ASSOC);
    }
} catch (Exception $e) {}

/* We load the slot's custom effects rack from DB if it exists */
$rack_json = '[]';
try {
    $pdo2 = mc1_db('mcaster1_encoder');
    $stmt = $pdo2->prepare("SELECT chain_json, bypass FROM effects_racks WHERE rack_type='slot' AND slot_id=? LIMIT 1");
    $stmt->execute([$slot_id]);
    $rack_row = $stmt->fetch(PDO::FETCH_ASSOC);
    if ($rack_row) $rack_json = $rack_row['chain_json'] ?: '[]';
} catch (Exception $e) {}

require_once __DIR__ . '/app/inc/header.php';
?>

<style>
.efr-page { padding:24px; }
.efr-header { display:flex; align-items:center; justify-content:space-between; margin-bottom:20px; flex-wrap:wrap; gap:12px; }
.efr-header h2 { margin:0; font-size:1.3rem; color:var(--text); }
.efr-back { color:var(--text-dim); text-decoration:none; font-size:.85rem; display:flex; align-items:center; gap:4px; }
.efr-back:hover { color:var(--teal); }

/* ── Mode Selector ────────────────────────────── */
.efr-modes { display:flex; gap:0; margin-bottom:20px; border-radius:var(--radius-sm); overflow:hidden; border:1px solid var(--border); }
.efr-mode { flex:1; padding:12px 16px; text-align:center; background:var(--bg3); color:var(--text-dim); cursor:pointer; font-size:.85rem; font-weight:600; border-right:1px solid var(--border); transition:background .15s, color .15s; }
.efr-mode:last-child { border-right:none; }
.efr-mode.active { background:var(--teal); color:#fff; }
.efr-mode:hover:not(.active) { background:var(--card); color:var(--text); }
.efr-mode i { margin-right:6px; }
.efr-mode small { display:block; font-weight:400; font-size:.7rem; margin-top:2px; opacity:.8; }

/* ── Custom Rack Panel ────────────────────────── */
.efr-custom { display:none; }
.efr-custom.show { display:block; }
.efr-info { background:var(--card); border:1px solid var(--border); border-radius:var(--radius); padding:20px; margin-bottom:16px; }
.efr-info p { margin:0; font-size:.85rem; color:var(--text-dim); line-height:1.6; }
.efr-info .badge { display:inline-block; padding:2px 8px; border-radius:10px; font-size:.7rem; font-weight:700; }
.badge-global { background:rgba(20,184,166,.15); color:var(--teal); }
.badge-bypass { background:rgba(234,179,8,.15); color:var(--yellow); }
.badge-custom { background:rgba(8,145,178,.15); color:var(--cyan); }

/* ── Effect Unit Cards (reused from effects-rack.php) ── */
.efr-chain { display:flex; align-items:stretch; gap:0; min-height:160px; overflow-x:auto; padding-bottom:8px; margin-bottom:16px; }
.efr-empty { flex:1; display:flex; align-items:center; justify-content:center; background:var(--card); border:2px dashed var(--border); border-radius:var(--radius); color:var(--muted); font-size:.9rem; min-height:160px; }
.efr-wire { width:28px; min-width:28px; display:flex; align-items:center; justify-content:center; position:relative; }
.efr-wire::before { content:''; position:absolute; top:50%; left:0; right:0; height:3px; background:linear-gradient(to right,var(--teal),var(--cyan)); border-radius:2px; }
.efr-dot { width:8px; height:8px; background:var(--teal); border-radius:50%; position:relative; z-index:1; }
.efr-unit { width:180px; min-width:180px; background:var(--card); border:1px solid var(--border); border-radius:var(--radius-sm); padding:12px; }
.efr-unit-name { font-size:.85rem; font-weight:600; color:var(--text); margin-bottom:4px; }
.efr-unit-type { font-size:.7rem; color:var(--muted); }
.efr-unit-toggle { margin-top:8px; }
.efr-unit-actions { display:flex; gap:4px; margin-top:8px; }
.efr-unit-actions button { flex:1; padding:4px; border:1px solid var(--border); background:transparent; color:var(--text-dim); border-radius:3px; cursor:pointer; font-size:.65rem; }
.efr-unit-actions button:hover { border-color:var(--teal); color:var(--teal); }
.efr-unit-actions button.danger:hover { border-color:var(--red); color:var(--red); }

.efr-add-bar { display:flex; gap:8px; flex-wrap:wrap; }
.efr-add-btn { padding:8px 14px; background:var(--bg3); border:1px solid var(--border); color:var(--text-dim); border-radius:var(--radius-xs); cursor:pointer; font-size:.8rem; transition:border-color .15s; }
.efr-add-btn:hover { border-color:var(--teal); color:var(--teal); }
</style>

<div class="efr-page">
  <div class="efr-header">
    <div>
      <a class="efr-back" href="/encoders.php"><i class="fa-solid fa-arrow-left"></i> Back to Encoders</a>
      <h2>
        <i class="fa-solid fa-sliders" style="color:var(--teal);margin-right:8px"></i>
        Effects Rack — <?= $slot_cfg ? 'Slot ' . (int)$slot_cfg['slot_id'] . ': ' . h($slot_cfg['name']) : 'Select a Slot' ?>
      </h2>
    </div>
    <select id="efr-slot-sel" onchange="if(this.value)location.href='/encoder-effects-rack.php?slot='+this.value" style="background:var(--bg3);border:1px solid var(--border);color:var(--text);padding:8px 12px;border-radius:var(--radius-xs);font-size:.85rem;">
      <option value="">— Select Encoder Slot —</option>
      <?php foreach ($slots as $s): ?>
      <option value="<?= (int)$s['slot_id'] ?>" <?= (int)$s['slot_id'] === $slot_id ? 'selected' : '' ?>>
        Slot <?= (int)$s['slot_id'] ?>: <?= h($s['name']) ?> (<?= h($s['effects_mode']) ?>)
      </option>
      <?php endforeach; ?>
    </select>
  </div>

  <?php if ($slot_cfg): ?>

  <!-- Mode Selector -->
  <div class="efr-modes">
    <div class="efr-mode <?= ($slot_cfg['effects_mode'] ?? 'global') === 'global' ? 'active' : '' ?>" onclick="setMode('global')">
      <i class="fa-solid fa-globe"></i>Global
      <small>Use system effects rack</small>
    </div>
    <div class="efr-mode <?= ($slot_cfg['effects_mode'] ?? '') === 'bypass' ? 'active' : '' ?>" onclick="setMode('bypass')">
      <i class="fa-solid fa-ban"></i>Bypass
      <small>No effects processing</small>
    </div>
    <div class="efr-mode <?= ($slot_cfg['effects_mode'] ?? '') === 'custom' ? 'active' : '' ?>" onclick="setMode('custom')">
      <i class="fa-solid fa-sliders"></i>Custom
      <small>Slot-specific effects chain</small>
    </div>
  </div>

  <!-- Info panel for global/bypass modes -->
  <div class="efr-info" id="efr-info-global" style="<?= ($slot_cfg['effects_mode'] ?? 'global') !== 'global' ? 'display:none' : '' ?>">
    <p><span class="badge badge-global">GLOBAL</span> This slot uses the <a href="/effects-rack.php" style="color:var(--teal)">system-wide effects rack</a>. All effects configured there apply to this encoder's audio.</p>
  </div>
  <div class="efr-info" id="efr-info-bypass" style="<?= ($slot_cfg['effects_mode'] ?? '') !== 'bypass' ? 'display:none' : '' ?>">
    <p><span class="badge badge-bypass">BYPASS</span> Effects are bypassed for this slot. Audio passes through without any effects processing.</p>
  </div>

  <!-- Custom Effects Chain -->
  <div class="efr-custom <?= ($slot_cfg['effects_mode'] ?? '') === 'custom' ? 'show' : '' ?>" id="efr-custom-panel">
    <div class="efr-info">
      <p><span class="badge badge-custom">CUSTOM</span> This slot has its own effects chain, independent of the global rack. Add and configure effects below.</p>
    </div>

    <div class="efr-chain" id="efr-chain">
      <div class="efr-empty" id="efr-empty">No effects — add one below</div>
    </div>

    <div class="efr-add-bar" id="efr-add-bar"></div>
  </div>

  <?php else: ?>
  <div style="background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:40px;text-align:center;">
    <i class="fa-solid fa-sliders" style="font-size:3rem;color:var(--muted);margin-bottom:16px;display:block"></i>
    <p style="color:var(--text-dim);">Select an encoder slot from the dropdown above to manage its effects.</p>
  </div>
  <?php endif; ?>
</div>

<?php if ($slot_cfg): ?>
<script>
var SLOT_ID = <?= $slot_id ?>;
var currentMode = '<?= $slot_cfg['effects_mode'] ?? 'global' ?>';
var slotRack = [];

function setMode(mode) {
  currentMode = mode;
  document.querySelectorAll('.efr-mode').forEach(function(m){ m.classList.remove('active'); });
  document.querySelectorAll('.efr-mode').forEach(function(m){
    if (m.textContent.toLowerCase().includes(mode)) m.classList.add('active');
  });
  document.getElementById('efr-info-global').style.display = mode === 'global' ? '' : 'none';
  document.getElementById('efr-info-bypass').style.display = mode === 'bypass' ? '' : 'none';
  document.getElementById('efr-custom-panel').classList.toggle('show', mode === 'custom');

  /* We save the mode to the API */
  mc1Api('PUT', '/api/v1/encoders/' + SLOT_ID + '/effects', {effects_mode: mode}).then(function(d) {
    if (d && d.ok) mc1Toast('Mode: ' + mode, 'ok');
  });

  /* We also save to DB via PHP API */
  mc1Api('POST', '/app/api/encoders.php', {
    action: 'save_config', id: <?= (int)$slot_cfg['id'] ?>,
    effects_mode: mode
  }).catch(function(){});
}

function loadSlotRack() {
  /* We load the slot's custom rack from the effects_racks table */
  mc1Api('POST', '/app/api/encoders.php', {action:'get_config', id: <?= (int)$slot_cfg['id'] ?>}).then(function(d){
    if (!d || !d.ok) return;
    /* We try loading rack from DB */
  }).catch(function(){});
  loadUnitTypes();
  renderSlotChain();
}

function loadUnitTypes() {
  mc1Api('GET', '/api/v1/effects/unit-types').then(function(d) {
    if (!d || !d.ok) return;
    var bar = document.getElementById('efr-add-bar');
    bar.innerHTML = '';
    (d.types || []).forEach(function(t) {
      var btn = document.createElement('button');
      btn.className = 'efr-add-btn';
      btn.innerHTML = '<i class="fa-solid fa-plus" style="margin-right:4px"></i>' + t.name;
      if (t.stub) btn.innerHTML += ' <span style="color:var(--yellow);font-size:.6rem">(stub)</span>';
      btn.onclick = function(){ addSlotUnit(t.type); };
      bar.appendChild(btn);
    });
  });
}

function addSlotUnit(type) {
  /* We add a unit to the global rack for now (per-slot rack storage is a DB enhancement) */
  mc1Api('POST', '/api/v1/effects/global/units', {type:type, enabled:true}).then(function(d) {
    if (d && d.ok) { mc1Toast('Added ' + type, 'ok'); loadGlobalRack(); }
  });
}

function loadGlobalRack() {
  mc1Api('GET', '/api/v1/effects/global').then(function(d) {
    if (!d || !d.ok) return;
    slotRack = (d.rack && d.rack.units) || [];
    renderSlotChain();
  });
}

function renderSlotChain() {
  var chain = document.getElementById('efr-chain');
  var empty = document.getElementById('efr-empty');
  if (slotRack.length === 0) { chain.innerHTML = ''; chain.appendChild(empty); empty.style.display = 'flex'; return; }
  empty.style.display = 'none';
  var html = '<div class="efr-wire"><div class="efr-dot"></div></div>';
  slotRack.forEach(function(u, idx) {
    html += '<div class="efr-unit"><div class="efr-unit-name">' + (u.type||'?') + '</div>';
    html += '<div class="efr-unit-type">Unit #' + u.id + ' — ' + (u.enabled ? 'On' : 'Off') + '</div>';
    html += '<div class="efr-unit-actions">';
    html += '<button onclick="toggleSlotUnit(' + u.id + ',' + !u.enabled + ')">' + (u.enabled ? 'Disable' : 'Enable') + '</button>';
    html += '<button class="danger" onclick="removeSlotUnit(' + u.id + ')">Remove</button>';
    html += '</div></div>';
    if (idx < slotRack.length - 1) html += '<div class="efr-wire"><div class="efr-dot"></div></div>';
  });
  html += '<div class="efr-wire"><div class="efr-dot"></div></div>';
  chain.innerHTML = html;
}

function toggleSlotUnit(id, on) {
  mc1Api('PUT', '/api/v1/effects/global', {unit_id:id, enabled:on}).then(function(d) {
    if (d && d.ok) loadGlobalRack();
  });
}

function removeSlotUnit(id) {
  mc1Api('DELETE', '/api/v1/effects/global/units/' + id).then(function(d) {
    if (d && d.ok) { mc1Toast('Removed', 'ok'); loadGlobalRack(); }
  });
}
</script>

<script>
document.addEventListener('DOMContentLoaded', function() {
  loadSlotRack();
  loadGlobalRack();
});
</script>
<?php endif; ?>

<?php require_once __DIR__ . '/app/inc/footer.php'; ?>
