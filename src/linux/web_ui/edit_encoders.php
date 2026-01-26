<?php
/**
 * edit_encoders.php — Full-page encoder slot editor.
 *
 * We load an encoder config from MySQL and present a comprehensive tabbed
 * form covering all encoder_configs columns. Saves go to app/api/encoders.php
 * (MySQL only). Live DSP changes go directly from JS to the C++ API.
 *
 * Access: ?id=N (edit by encoder_configs.id)
 *         ?slot=N (edit by slot_id — resolved to id)
 *         ?new=1  (blank form for a new slot)
 *
 * No exit()/die() — uopz active.
 *
 * @author  Dave St. John <davestj@gmail.com>
 * @version 1.4.0
 * @since   2026-02-24
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/app/inc/auth.php';
require_once __DIR__ . '/app/inc/mc1_config.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/user_auth.php';
require_once __DIR__ . '/app/inc/edit.encoders.class.php';

$page_title = 'Edit Encoder';
$active_nav = 'encoders';
$use_charts = false;

$user      = mc1_current_user();
$can_admin = $user && !empty($user['can_admin']);

/* We resolve which config to load */
$cfg    = null;
$is_new = isset($_GET['new']) && $_GET['new'] === '1';

if (!$is_new) {
    $req_id   = (int)($_GET['id']   ?? 0);
    $req_slot = (int)($_GET['slot'] ?? 0);

    if ($req_id > 0) {
        $cfg = EditEncoders::load($req_id);
    } elseif ($req_slot > 0) {
        $cfg = EditEncoders::load_by_slot($req_slot);
    }
}

/* We treat missing config the same as a new slot */
if (!$cfg && !$is_new) {
    $is_new = true;
    $cfg    = null; // blank form
}

/* We fetch all existing configs for the slot-ID uniqueness hint */
$all_configs = EditEncoders::all();
$existing_slots = array_column($all_configs, 'slot_id');
$editing_id     = $cfg ? (int)$cfg['id'] : 0;

/* We default empty fields so the template never throws undefined-key notices */
$c = $cfg ?? [];
$def = fn(string $k, mixed $d) => $c[$k] ?? $d;

require_once __DIR__ . '/app/inc/header.php';
?>

<style>
.etab-bar     { display:flex; gap:0; border-bottom:1px solid var(--border); margin-bottom:20px; overflow-x:auto; }
.etab-btn     { padding:9px 16px; font-size:12px; font-weight:600; color:var(--muted); background:none; border:none;
                border-bottom:2px solid transparent; cursor:pointer; white-space:nowrap;
                text-transform:uppercase; letter-spacing:.05em; transition:color .15s,border-color .15s; }
.etab-btn:hover  { color:var(--text); }
.etab-btn.active { color:var(--teal); border-bottom-color:var(--teal); }
.etab-panel      { display:none; }
.etab-panel.active { display:block; }
.field-hint   { font-size:11px; color:var(--muted); margin-top:3px; }
.section-hdr  { font-size:10px; font-weight:700; text-transform:uppercase; letter-spacing:.08em;
                color:var(--muted); margin-bottom:12px; padding-bottom:6px;
                border-bottom:1px solid rgba(51,65,85,.4); }
.pass-wrap    { position:relative; }
.pass-wrap .form-input { padding-right:38px; }
.pass-toggle  { position:absolute; right:10px; top:50%; transform:translateY(-50%);
                background:none; border:none; color:var(--muted); cursor:pointer; font-size:13px; }
.preset-card  { background:var(--bg2); border:1px solid var(--border); border-radius:8px;
                padding:10px 12px; text-align:center; cursor:pointer;
                transition:border-color .15s, background .15s; display:flex;
                flex-direction:column; align-items:center; gap:2px; }
.preset-card:hover { border-color:var(--teal); background:var(--bg3); }
.badge-orange { background:rgba(251,146,60,.15); color:#fb923c; border:1px solid rgba(251,146,60,.3); }
</style>

<!-- Page header -->
<div class="sec-hdr">
  <div class="sec-title">
    <a href="/encoders.php" class="btn btn-secondary btn-sm" style="margin-right:8px"><i class="fa-solid fa-arrow-left"></i></a>
    <i class="fa-solid fa-tower-broadcast" style="color:var(--teal);margin-right:8px"></i>
    <?= $is_new ? 'New Encoder Slot' : 'Edit Encoder: ' . h($def('name', 'Slot ' . $def('slot_id', ''))) ?>
    <?php if (!$is_new): ?>
    <span class="badge badge-gray" style="margin-left:8px">Slot <?= (int)$def('slot_id', 0) ?></span>
    <?php endif; ?>
  </div>
  <div style="display:flex;gap:8px">
    <?php if (!$is_new): ?>
    <button class="btn btn-secondary btn-sm" id="btn-apply-dsp" onclick="applyDspLive()">
      <i class="fa-solid fa-bolt"></i> Apply DSP Live
    </button>
    <?php endif; ?>
    <?php if ($can_admin && !$is_new): ?>
    <button class="btn btn-danger btn-sm" onclick="deleteConfig()">
      <i class="fa-solid fa-trash"></i> Delete Slot
    </button>
    <?php endif; ?>
    <button class="btn btn-primary btn-sm" onclick="saveConfig()">
      <i class="fa-solid fa-floppy-disk"></i> Save Changes
    </button>
  </div>
</div>

<?php if (!$can_admin): ?>
<div class="alert alert-warn"><i class="fa-solid fa-triangle-exclamation"></i> You need admin privileges to save changes to encoder configurations.</div>
<?php endif; ?>

<!-- Status banner (shown after save) -->
<div id="save-result" style="display:none;margin-bottom:12px"></div>

<div class="card">

  <!-- Tab bar -->
  <div class="etab-bar">
    <button class="etab-btn active" onclick="etab('basic',this)"><i class="fa-solid fa-plug fa-fw"></i> Basic</button>
    <button class="etab-btn" onclick="etab('audio',this)"><i class="fa-solid fa-music fa-fw"></i> Audio</button>
    <button class="etab-btn" onclick="etab('server',this)"><i class="fa-solid fa-server fa-fw"></i> Server</button>
    <button class="etab-btn" onclick="etab('station',this)"><i class="fa-solid fa-radio fa-fw"></i> Station &amp; ICY2</button>
    <button class="etab-btn" onclick="etab('dsp',this)"><i class="fa-solid fa-sliders fa-fw"></i> DSP</button>
    <button class="etab-btn" onclick="etab('archive',this)"><i class="fa-solid fa-box-archive fa-fw"></i> Archive</button>
  </div>

  <!-- ── Tab 1: Basic ─────────────────────────────────────────────── -->
  <div id="etab-basic" class="etab-panel active">
    <div class="form-row form-row-2">

      <div class="form-group">
        <label class="form-label">Slot ID <span style="color:var(--red)">*</span></label>
        <input class="form-input" id="ef-slot" type="number" min="1" max="99"
               value="<?= (int)$def('slot_id', '') ?>"
               <?= !$is_new ? 'readonly style="opacity:.6"' : '' ?>>
        <div class="field-hint">
          Unique number 1–99.
          <?php if ($existing_slots): ?>
          Existing: <?= implode(', ', $existing_slots) ?>.
          <?php endif; ?>
        </div>
      </div>

      <div class="form-group">
        <label class="form-label">Display Name <span style="color:var(--red)">*</span></label>
        <input class="form-input" id="ef-name" value="<?= h($def('name', '')) ?>" placeholder="Classic Rock 128k">
      </div>

      <div class="form-group">
        <label class="form-label">Input Type</label>
        <select class="form-select" id="ef-input" onchange="inputTypeChange()">
          <?php foreach (EditEncoders::INPUT_TYPES as $v => $lbl): ?>
          <option value="<?= $v ?>" <?= $def('input_type','playlist') === $v ? 'selected' : '' ?>><?= h($lbl) ?></option>
          <?php endforeach; ?>
        </select>
      </div>

      <div class="form-group" id="ef-playlist-grp">
        <label class="form-label">Playlist / Source Path</label>
        <input class="form-input" id="ef-playlist"
               value="<?= h($def('playlist_path', '')) ?>"
               placeholder="/var/www/mcaster1.com/audio/playlists/rock.m3u">
        <div class="field-hint">Full path to .m3u, .pls, .xspf, .txt, or relay URL.</div>
      </div>

      <div class="form-group" id="ef-device-grp" style="display:none">
        <label class="form-label">Device Index</label>
        <input class="form-input" id="ef-device" type="number" min="-1"
               value="<?= (int)$def('device_index', -1) ?>">
        <div class="field-hint">-1 = default device. <a href="#" onclick="loadDevices();return false">Detect devices</a>.</div>
        <div id="ef-device-list" style="margin-top:6px"></div>
      </div>

      <div class="form-group">
        <label class="form-label">Description</label>
        <textarea class="form-textarea" id="ef-desc" rows="2" placeholder="Optional notes about this encoder slot"><?= h($def('description', '')) ?></textarea>
      </div>

      <div class="form-group">
        <label class="form-label">Status</label>
        <label style="display:flex;align-items:center;gap:8px;cursor:pointer;margin-top:6px">
          <input type="checkbox" id="ef-active" <?= $def('is_active', 1) ? 'checked' : '' ?> style="accent-color:var(--teal)">
          <span style="font-size:13px">Active (encoder slot enabled)</span>
        </label>
      </div>

    </div>

    <!-- Auto-Start -->
    <div class="section-hdr" style="margin-top:16px">Auto-Start on Launch</div>
    <div class="form-row form-row-2">

      <div class="form-group">
        <label style="display:flex;align-items:center;gap:8px;cursor:pointer;font-size:13px;margin-top:4px">
          <input type="checkbox" id="ef-auto-start" <?= $def('auto_start', 0) ? 'checked' : '' ?> style="accent-color:var(--teal)" onchange="document.getElementById('ef-auto-delay-grp').style.display=this.checked?'':'none'">
          Automatically start this encoder when the service starts
        </label>
        <div class="field-hint">When checked the encoder starts after the configured delay — useful for unmanned 24/7 stations.</div>
      </div>

      <div class="form-group" id="ef-auto-delay-grp" <?= $def('auto_start', 0) ? '' : 'style="display:none"' ?>>
        <label class="form-label">Auto-Start Delay (seconds)</label>
        <input class="form-input" id="ef-auto-delay" type="number" min="0" max="300"
               value="<?= (int)$def('auto_start_delay', 0) ?>" style="max-width:120px">
        <div class="field-hint">0 = immediate. Use a staggered delay (e.g. 5 / 10 / 15 s) when running multiple auto-start slots to avoid a simultaneous connection spike.</div>
      </div>

    </div>
  </div>

  <!-- ── Tab 2: Audio ─────────────────────────────────────────────── -->
  <div id="etab-audio" class="etab-panel">

    <!-- Quality Presets -->
    <div class="section-hdr">Quick Quality Presets <span style="font-weight:400;text-transform:none;letter-spacing:0;color:var(--muted)">— click to auto-fill all audio settings</span></div>
    <div id="preset-grid" style="display:grid;grid-template-columns:repeat(auto-fill,minmax(190px,1fr));gap:8px;margin-bottom:20px">
      <?php
      // We define broadcast-quality presets that cover the most common webcasting scenarios.
      $presets = [
        'talk_am'    => ['label'=>'AM Talk Radio',    'sub'=>'32k MP3 mono, 22kHz',           'icon'=>'fa-microphone'],
        'fm_radio'   => ['label'=>'FM Radio',         'sub'=>'128k MP3 stereo, 44.1kHz',      'icon'=>'fa-tower-broadcast'],
        'fm_hd'      => ['label'=>'HD Radio',         'sub'=>'192k AAC-LC stereo, 48kHz',     'icon'=>'fa-tower-broadcast'],
        'mobile_lo'  => ['label'=>'Mobile Low',       'sub'=>'64k HE-AAC v1, saves data',     'icon'=>'fa-mobile-screen'],
        'mobile_std' => ['label'=>'Mobile Standard',  'sub'=>'96k Opus VBR, 48kHz',           'icon'=>'fa-mobile-screen'],
        'desktop_std'=> ['label'=>'Desktop Standard', 'sub'=>'192k MP3 CBR, 44.1kHz',         'icon'=>'fa-desktop'],
        'desktop_hq' => ['label'=>'Desktop HQ',       'sub'=>'320k MP3 CBR, 44.1kHz',         'icon'=>'fa-desktop'],
        'home_th'    => ['label'=>'Home Theatre',     'sub'=>'FLAC lossless, 48kHz stereo',   'icon'=>'fa-tv'],
        'hi_def'     => ['label'=>'Hi-Def Studio',    'sub'=>'FLAC lossless, 96kHz',          'icon'=>'fa-compact-disc'],
        'live_tv'    => ['label'=>'Live TV Audio',    'sub'=>'192k AAC-LC, 48kHz',            'icon'=>'fa-film'],
        'web_budget' => ['label'=>'Webcaster Budget', 'sub'=>'~128k Vorbis VBR Q5',           'icon'=>'fa-wifi'],
        'web_std'    => ['label'=>'Webcaster Std',    'sub'=>'128k Opus VBR, excellent quality','icon'=>'fa-wifi'],
        'ultra_low'  => ['label'=>'Ultra Low BW',     'sub'=>'32k HE-AAC v2, mobile-only',   'icon'=>'fa-signal'],
      ];
      foreach ($presets as $key => $p): ?>
      <button type="button" class="preset-card" onclick="applyPreset('<?= $key ?>')" title="<?= h($p['sub']) ?>">
        <i class="fa-solid <?= $p['icon'] ?>" style="font-size:16px;color:var(--teal);margin-bottom:4px"></i>
        <div style="font-size:12px;font-weight:600;color:var(--text)"><?= h($p['label']) ?></div>
        <div style="font-size:10px;color:var(--muted);margin-top:2px"><?= h($p['sub']) ?></div>
      </button>
      <?php endforeach; ?>
    </div>

    <!-- Codec + Encode Mode -->
    <div class="section-hdr">Codec &amp; Encoding Mode</div>
    <div class="form-row form-row-2">

      <div class="form-group">
        <label class="form-label">Codec <span style="color:var(--red)">*</span></label>
        <select class="form-select" id="ef-codec" onchange="codecChanged()">
          <?php foreach (EditEncoders::CODEC_LABELS as $v => $lbl): ?>
          <option value="<?= $v ?>" <?= $def('codec', 'mp3') === $v ? 'selected' : '' ?>><?= h($lbl) ?></option>
          <?php endforeach; ?>
        </select>
      </div>

      <div class="form-group" id="ef-mode-grp">
        <label class="form-label">Encoding Mode</label>
        <select class="form-select" id="ef-mode" onchange="codecChanged()">
          <?php foreach (EditEncoders::ENCODE_MODES as $v => $lbl): ?>
          <option value="<?= $v ?>" <?= $def('encode_mode', 'cbr') === $v ? 'selected' : '' ?>><?= h($lbl) ?></option>
          <?php endforeach; ?>
        </select>
        <div class="field-hint" id="ef-mode-hint"></div>
      </div>

    </div>

    <!-- Bitrate (hidden for Vorbis/FLAC/VBR-only codecs) -->
    <div class="form-row form-row-2" id="ef-bitrate-row">
      <div class="form-group">
        <label class="form-label">Bitrate (kbps)</label>
        <select class="form-select" id="ef-bitrate">
          <!-- We populate this via codecChanged() on load -->
        </select>
        <div class="field-hint" id="ef-bitrate-hint"></div>
      </div>
    </div>

    <!-- Quality / Compression slider (Vorbis VBR, FLAC compression, MP3 VBR quality) -->
    <div class="form-row form-row-2" id="ef-quality-row" style="display:none">
      <div class="form-group">
        <label class="form-label" id="ef-quality-lbl">Quality</label>
        <div style="display:flex;align-items:center;gap:12px;flex-wrap:wrap">
          <input type="range" id="ef-quality-slider" min="0" max="10" step="1"
                 value="<?= (int)$def('quality', 5) ?>"
                 oninput="document.getElementById('ef-quality-num').value=this.value;updateQualityHint()">
          <input type="number" id="ef-quality-num" min="0" max="10" style="width:52px;padding:3px 6px;font-size:12px"
                 value="<?= (int)$def('quality', 5) ?>"
                 oninput="document.getElementById('ef-quality-slider').value=this.value;updateQualityHint()">
        </div>
        <div class="field-hint" id="ef-quality-hint"></div>
      </div>
    </div>

    <!-- Sample Rate + Channels -->
    <div class="section-hdr" style="margin-top:4px">Sample Rate &amp; Channels</div>
    <div class="form-row form-row-2">

      <div class="form-group">
        <label class="form-label">Sample Rate</label>
        <select class="form-select" id="ef-srate">
          <!-- Populated by codecChanged() -->
        </select>
        <div class="field-hint" id="ef-srate-hint"></div>
      </div>

      <div class="form-group">
        <label class="form-label">Channel Mode</label>
        <select class="form-select" id="ef-chanmode" onchange="syncChannels()">
          <?php foreach (EditEncoders::CHANNEL_MODES as $v => $lbl): ?>
          <option value="<?= $v ?>" <?= $def('channel_mode', 'joint') === $v ? 'selected' : '' ?>><?= h($lbl) ?></option>
          <?php endforeach; ?>
        </select>
        <div class="field-hint" id="ef-chan-hint"></div>
        <!-- We keep a hidden channels int synced with channel_mode for the payload -->
        <input type="hidden" id="ef-channels" value="<?= (int)$def('channels', 2) ?>">
      </div>

    </div>

    <!-- Volume + Playback -->
    <div class="section-hdr" style="margin-top:4px">Volume &amp; Playback</div>
    <div class="form-row form-row-2">

      <div class="form-group">
        <label class="form-label">Volume (0–200%)</label>
        <div class="range-wrap">
          <i class="fa-solid fa-volume-low" style="font-size:12px"></i>
          <input type="range" id="ef-vol-slider" min="0" max="200"
                 value="<?= (int)round($def('volume', 1.0) * 100) ?>"
                 oninput="document.getElementById('ef-vol-num').value=this.value">
          <input type="number" id="ef-vol-num" min="0" max="200" style="width:60px;padding:3px 6px;font-size:12px"
                 value="<?= (int)round($def('volume', 1.0) * 100) ?>"
                 oninput="document.getElementById('ef-vol-slider').value=this.value">
          <span style="font-size:11px;color:var(--muted)">%</span>
        </div>
        <div class="field-hint">100% = unity gain.</div>
      </div>

      <div class="form-group" style="display:flex;flex-direction:column;gap:10px;justify-content:center">
        <label style="display:flex;align-items:center;gap:8px;cursor:pointer;font-size:13px">
          <input type="checkbox" id="ef-shuffle" <?= $def('shuffle', 0) ? 'checked' : '' ?> style="accent-color:var(--teal)"> Shuffle playlist
        </label>
        <label style="display:flex;align-items:center;gap:8px;cursor:pointer;font-size:13px">
          <input type="checkbox" id="ef-repeat" <?= $def('repeat_all', 1) ? 'checked' : '' ?> style="accent-color:var(--teal)"> Repeat All
        </label>
      </div>

    </div>
  </div>

  <!-- ── Tab 3: Server ─────────────────────────────────────────────── -->
  <div id="etab-server" class="etab-panel">
    <div class="form-row form-row-2">

      <div class="form-group">
        <label class="form-label">Protocol <span style="color:var(--red)">*</span></label>
        <select class="form-select" id="ef-proto">
          <?php foreach (EditEncoders::PROTOCOL_LABELS as $v => $lbl): ?>
          <option value="<?= $v ?>" <?= $def('server_protocol', 'icecast2') === $v ? 'selected' : '' ?>><?= h($lbl) ?></option>
          <?php endforeach; ?>
        </select>
      </div>

      <div class="form-group">
        <label class="form-label">Server Host <span style="color:var(--red)">*</span></label>
        <input class="form-input" id="ef-host"
               value="<?= h($def('server_host', '')) ?>" placeholder="dnas.mcaster1.com">
      </div>

      <div class="form-group">
        <label class="form-label">Port <span style="color:var(--red)">*</span></label>
        <input class="form-input" id="ef-port" type="number" min="1" max="65535"
               value="<?= (int)$def('server_port', 9443) ?>">
      </div>

      <div class="form-group">
        <label class="form-label">Mount Point <span style="color:var(--red)">*</span></label>
        <input class="form-input" id="ef-mount"
               value="<?= h($def('server_mount', '/live')) ?>" placeholder="/live">
      </div>

      <div class="form-group">
        <label class="form-label">Source Username</label>
        <input class="form-input" id="ef-user"
               value="<?= h($def('server_user', 'source')) ?>">
      </div>

      <div class="form-group">
        <label class="form-label">Source Password</label>
        <div class="pass-wrap">
          <input type="password" class="form-input" id="ef-pass"
                 placeholder="<?= $editing_id > 0 ? 'Leave blank to keep existing password' : 'Password' ?>"
                 autocomplete="new-password">
          <button type="button" class="pass-toggle" onclick="var i=document.getElementById('ef-pass');i.type=i.type==='password'?'text':'password'">
            <i class="fa-solid fa-eye"></i>
          </button>
        </div>
        <?php if ($editing_id > 0): ?>
        <div class="field-hint">Leave blank to keep the current stored password.</div>
        <?php endif; ?>
      </div>

    </div>

    <!-- Quick presets -->
    <div class="form-group" style="margin-top:4px">
      <label class="form-label">Quick Server Presets</label>
      <div style="display:flex;gap:8px;flex-wrap:wrap;margin-top:4px">
        <button type="button" class="btn btn-secondary btn-sm" onclick="fillServer('dnas.mcaster1.com',9443,'source','mcaster1')">
          <i class="fa-solid fa-satellite-dish"></i> Mcaster1 DNAS
        </button>
        <button type="button" class="btn btn-secondary btn-sm" onclick="fillServer('localhost',8000,'source','icecast2')">
          <i class="fa-solid fa-broadcast-tower"></i> Local Icecast2
        </button>
        <button type="button" class="btn btn-secondary btn-sm" onclick="fillServer('localhost',8000,'admin','shoutcast2')">
          <i class="fa-solid fa-broadcast-tower"></i> Local Shoutcast
        </button>
      </div>
    </div>

    <!-- Auto-Reconnect & SLEEP -->
    <div class="section-hdr" style="margin-top:20px">Auto-Reconnect &amp; Failover</div>
    <div class="alert alert-info" style="margin-bottom:14px;font-size:12px">
      <i class="fa-solid fa-circle-info"></i>
      When a connection drops, the encoder waits <strong>Reconnect Interval</strong> seconds then retries.
      After <strong>Max Attempts</strong> consecutive failures it enters <strong>SLEEP</strong> status — a persistent
      "soft-offline" state that flags the slot as needing attention without marking it as an error.
      SLEEP requires a manual <em>Wake</em> action or a service restart. Set Max Attempts to <strong>0</strong> for unlimited retries (retry forever).
    </div>
    <div class="form-row form-row-2">

      <div class="form-group">
        <label style="display:flex;align-items:center;gap:8px;cursor:pointer;font-size:13px;margin-top:4px">
          <input type="checkbox" id="ef-auto-reconnect" <?= $def('auto_reconnect', 1) ? 'checked' : '' ?> style="accent-color:var(--teal)">
          Enable Auto-Reconnect on disconnect
        </label>
        <div class="field-hint">When unchecked the slot goes OFFLINE immediately on first disconnect and stays there.</div>
      </div>

      <div class="form-group">
        <label class="form-label">Reconnect Interval (seconds)</label>
        <input class="form-input" id="ef-reconnect-interval" type="number" min="1" max="300"
               value="<?= (int)$def('reconnect_interval', 5) ?>" style="max-width:120px">
        <div class="field-hint">Time between reconnect attempts. 5 s is suitable for most servers.</div>
      </div>

      <div class="form-group">
        <label class="form-label">Max Reconnect Attempts</label>
        <input class="form-input" id="ef-reconnect-max" type="number" min="0" max="999"
               value="<?= (int)$def('reconnect_max_attempts', 0) ?>" style="max-width:120px">
        <div class="field-hint">
          <strong>0</strong> = retry forever (recommended for 24/7 stations).<br>
          e.g. <strong>12</strong> + 5 s interval = 1 minute of retries then SLEEP.
        </div>
      </div>

      <div class="form-group">
        <label class="form-label">SLEEP Behaviour</label>
        <div style="font-size:12px;color:var(--muted);line-height:1.5;margin-top:6px">
          After max attempts: slot enters <span class="badge badge-orange" style="font-size:10px">SLEEP</span> status.<br>
          The stream is halted. Logging captures the exact failure time and attempt count.<br>
          Use <strong>Wake</strong> from the Encoders page or restart the service to resume.
        </div>
      </div>

    </div>
  </div>

  <!-- ── Tab 4: Station & ICY2 ─────────────────────────────────────── -->
  <div id="etab-station" class="etab-panel">
    <div class="section-hdr">Station Info</div>
    <div class="form-row form-row-2">

      <div class="form-group">
        <label class="form-label">Station Name</label>
        <input class="form-input" id="ef-sname"
               value="<?= h($def('station_name', '')) ?>" placeholder="My Radio Station">
      </div>

      <div class="form-group">
        <label class="form-label">Genre</label>
        <input class="form-input" id="ef-genre"
               value="<?= h($def('genre', '')) ?>" placeholder="Rock, Pop, Country…">
      </div>

      <div class="form-group">
        <label class="form-label">Website URL</label>
        <input class="form-input" id="ef-website"
               value="<?= h($def('website_url', '')) ?>" placeholder="https://example.com">
      </div>

    </div>

    <div class="section-hdr" style="margin-top:12px">ICY2 Extended Metadata</div>
    <div class="form-row form-row-2">

      <div class="form-group">
        <label class="form-label"><i class="fa-brands fa-twitter fa-fw"></i> Twitter / X Handle</label>
        <input class="form-input" id="ef-icy-twitter"
               value="<?= h($def('icy2_twitter', '')) ?>" placeholder="@mystation">
      </div>

      <div class="form-group">
        <label class="form-label"><i class="fa-brands fa-facebook fa-fw"></i> Facebook Page</label>
        <input class="form-input" id="ef-icy-fb"
               value="<?= h($def('icy2_facebook', '')) ?>" placeholder="mystation">
      </div>

      <div class="form-group">
        <label class="form-label"><i class="fa-brands fa-instagram fa-fw"></i> Instagram Handle</label>
        <input class="form-input" id="ef-icy-ig"
               value="<?= h($def('icy2_instagram', '')) ?>" placeholder="mystation">
      </div>

      <div class="form-group">
        <label class="form-label"><i class="fa-solid fa-envelope fa-fw"></i> Contact Email</label>
        <input class="form-input" id="ef-icy-email"
               value="<?= h($def('icy2_email', '')) ?>" placeholder="studio@mystation.com">
      </div>

      <div class="form-group">
        <label class="form-label">Language (ISO 639-1)</label>
        <input class="form-input" id="ef-icy-lang" maxlength="5"
               value="<?= h($def('icy2_language', 'en')) ?>" placeholder="en">
      </div>

      <div class="form-group">
        <label class="form-label">Country (ISO 3166-1 alpha-2)</label>
        <input class="form-input" id="ef-icy-country" maxlength="2"
               value="<?= h($def('icy2_country', 'US')) ?>" placeholder="US">
      </div>

      <div class="form-group">
        <label class="form-label">City</label>
        <input class="form-input" id="ef-icy-city"
               value="<?= h($def('icy2_city', '')) ?>" placeholder="Seattle, WA">
      </div>

      <div class="form-group">
        <label class="form-label">Public Directory Listing</label>
        <label style="display:flex;align-items:center;gap:8px;margin-top:6px;cursor:pointer;font-size:13px">
          <input type="checkbox" id="ef-icy-public" <?= $def('icy2_is_public', 1) ? 'checked' : '' ?> style="accent-color:var(--teal)">
          List stream in public directories (YP/dir.xiph.org)
        </label>
      </div>

    </div>
  </div>

  <!-- ── Tab 5: DSP ─────────────────────────────────────────────────── -->
  <div id="etab-dsp" class="etab-panel">
    <?php if (!$is_new): ?>
    <div class="alert alert-info" style="margin-bottom:16px">
      <i class="fa-solid fa-circle-info"></i>
      <strong>Save Changes</strong> updates the MySQL config (used on next encoder start).
      <strong>Apply DSP Live</strong> (top-right button) pushes the DSP settings to the running
      encoder immediately via the C++ API — no restart required.
    </div>
    <?php endif; ?>

    <div style="display:grid;grid-template-columns:1fr 1fr;gap:20px">

      <div>
        <div class="section-hdr">Equalizer</div>

        <div class="dsp-row">
          <div class="dsp-lbl">EQ Enabled<small>10-band parametric EQ</small></div>
          <label class="toggle">
            <input type="checkbox" id="ef-eq-en" <?= $def('eq_enabled', 0) ? 'checked' : '' ?>>
            <span class="toggle-slider"></span>
          </label>
        </div>

        <div class="dsp-row">
          <div class="dsp-lbl">EQ Preset<small>Frequency profile</small></div>
          <select class="form-select" id="ef-eq-preset" style="width:150px;padding:5px 8px;font-size:12px">
            <?php foreach (EditEncoders::EQ_PRESETS as $v => $lbl): ?>
            <option value="<?= $v ?>" <?= $def('eq_preset', 'flat') === $v ? 'selected' : '' ?>><?= h($lbl) ?></option>
            <?php endforeach; ?>
          </select>
        </div>
      </div>

      <div>
        <div class="section-hdr">Dynamics & Crossfade</div>

        <div class="dsp-row">
          <div class="dsp-lbl">AGC<small>Auto Gain Control / Compressor</small></div>
          <label class="toggle">
            <input type="checkbox" id="ef-agc" <?= $def('agc_enabled', 0) ? 'checked' : '' ?>>
            <span class="toggle-slider"></span>
          </label>
        </div>

        <div class="dsp-row">
          <div class="dsp-lbl">Crossfade<small>Equal-power fade between tracks</small></div>
          <label class="toggle">
            <input type="checkbox" id="ef-xf-en" <?= $def('crossfade_enabled', 1) ? 'checked' : '' ?>>
            <span class="toggle-slider"></span>
          </label>
        </div>

        <div class="dsp-row">
          <div class="dsp-lbl">Crossfade Duration<small id="ef-xf-lbl"><?= number_format((float)$def('crossfade_duration', 3.0), 1) ?>s</small></div>
          <input type="range" min="1" max="10" step="0.5"
                 value="<?= number_format((float)$def('crossfade_duration', 3.0), 1) ?>"
                 id="ef-xf-dur"
                 oninput="document.getElementById('ef-xf-lbl').textContent=parseFloat(this.value).toFixed(1)+'s'"
                 style="width:120px">
        </div>
      </div>

    </div>

    <?php if (!$is_new): ?>
    <div style="margin-top:16px;padding-top:12px;border-top:1px solid var(--border)">
      <div class="section-hdr">Live DSP Status</div>
      <div id="dsp-live-status" style="font-size:12px;color:var(--muted)">Click <strong>Apply DSP Live</strong> to push current settings to the running encoder.</div>
    </div>
    <?php endif; ?>
  </div>

  <!-- ── Tab 6: Archive ─────────────────────────────────────────────── -->
  <div id="etab-archive" class="etab-panel">
    <div class="alert alert-info" style="margin-bottom:16px">
      <i class="fa-solid fa-circle-info"></i>
      Archive writes a simultaneous WAV + MP3 copy of the broadcast stream to disk.
      Requires sufficient disk space. Files are named <code>YYYYMMDD_HHMMSS_{slot}.mp3</code>.
    </div>

    <div class="form-group">
      <label style="display:flex;align-items:center;gap:8px;cursor:pointer;font-size:13px;margin-bottom:12px">
        <input type="checkbox" id="ef-arch-en" <?= $def('archive_enabled', 0) ? 'checked' : '' ?> style="accent-color:var(--teal)">
        Enable broadcast archive recording
      </label>
    </div>

    <div class="form-group">
      <label class="form-label">Archive Directory</label>
      <input class="form-input" id="ef-arch-dir"
             value="<?= h($def('archive_dir', '')) ?>"
             placeholder="/var/www/mcaster1.com/archive">
      <div class="field-hint">Full path to a writable directory. We create date subdirectories automatically.</div>
    </div>
  </div>

</div><!-- /card -->

<!-- Bottom action bar -->
<div style="display:flex;gap:8px;justify-content:flex-end;margin-top:12px;align-items:center">
  <a href="/settings.php" class="btn btn-secondary btn-sm"><i class="fa-solid fa-gear"></i> Back to Settings</a>
  <a href="/encoders.php" class="btn btn-secondary btn-sm"><i class="fa-solid fa-tower-broadcast"></i> Encoder Controls</a>
  <?php if ($can_admin && !$is_new): ?>
  <button class="btn btn-danger btn-sm" onclick="deleteConfig()"><i class="fa-solid fa-trash"></i> Delete Slot</button>
  <?php endif; ?>
  <button class="btn btn-primary" onclick="saveConfig()"><i class="fa-solid fa-floppy-disk"></i> Save Changes</button>
</div>

<script>
(function () {
'use strict';

var EDITING_ID   = <?= $editing_id ?>;
var EDITING_SLOT = <?= (int)$def('slot_id', 0) ?>;
var IS_NEW       = <?= $is_new ? 'true' : 'false' ?>;

// ── Tab switching ──────────────────────────────────────────────────────────

window.etab = function (name, btn) {
    document.querySelectorAll('.etab-panel').forEach(function (p) { p.classList.remove('active'); });
    document.querySelectorAll('.etab-btn').forEach(function (b) { b.classList.remove('active'); });
    var panel = document.getElementById('etab-' + name);
    if (panel) panel.classList.add('active');
    if (btn) btn.classList.add('active');
};

// ── Input type visibility ──────────────────────────────────────────────────

window.inputTypeChange = function () {
    var v = document.getElementById('ef-input').value;
    document.getElementById('ef-playlist-grp').style.display = (v === 'playlist' || v === 'url') ? '' : 'none';
    document.getElementById('ef-device-grp').style.display   = v === 'device' ? '' : 'none';
    document.getElementById('ef-playlist').placeholder        = v === 'url'
        ? 'http://stream.example.com:8000/live'
        : '/var/www/mcaster1.com/audio/playlists/rock.m3u';
};

// We run it once on load to reflect the stored input type
document.addEventListener('DOMContentLoaded', function () {
    inputTypeChange();
    codecChanged();   // We initialise the codec-aware Audio tab controls on load.

    // We load the live DSP state from C++ to pre-fill the DSP tab with current values
    if (!IS_NEW && EDITING_SLOT > 0) {
        mc1Api('GET', '/api/v1/encoders/' + EDITING_SLOT + '/dsp').then(function (dsp) {
            if (!dsp || dsp.ok === false) return;
            if (dsp.eq_enabled  !== undefined) document.getElementById('ef-eq-en').checked  = !!dsp.eq_enabled;
            if (dsp.agc_enabled !== undefined) document.getElementById('ef-agc').checked    = !!dsp.agc_enabled;
            if (dsp.crossfade_enabled !== undefined) document.getElementById('ef-xf-en').checked = !!dsp.crossfade_enabled;
            if (dsp.eq_preset   !== undefined && dsp.eq_preset) document.getElementById('ef-eq-preset').value = dsp.eq_preset;
            if (dsp.crossfade_duration !== undefined) {
                document.getElementById('ef-xf-dur').value = dsp.crossfade_duration;
                document.getElementById('ef-xf-lbl').textContent = parseFloat(dsp.crossfade_duration).toFixed(1) + 's';
            }
            var stat = document.getElementById('dsp-live-status');
            if (stat) stat.textContent = 'DSP loaded from running encoder (Slot ' + EDITING_SLOT + ').';
        }).catch(function () {});
    }
});

// ── Server preset ──────────────────────────────────────────────────────────

window.fillServer = function (host, port, user, proto) {
    document.getElementById('ef-host').value  = host;
    document.getElementById('ef-port').value  = port;
    document.getElementById('ef-user').value  = user;
    document.getElementById('ef-proto').value = proto;
};

// ── PortAudio device loader ────────────────────────────────────────────────

window.loadDevices = function () {
    var el = document.getElementById('ef-device-list');
    el.innerHTML = '<span style="font-size:12px;color:var(--muted)">Loading…</span>';
    mc1Api('GET', '/api/v1/devices').then(function (d) {
        if (!d || !Array.isArray(d.devices) || !d.devices.length) {
            el.innerHTML = '<span style="font-size:12px;color:var(--muted)">No devices found (encoder offline?)</span>';
            return;
        }
        el.innerHTML = d.devices.map(function (dev) {
            return '<div style="display:flex;align-items:center;gap:10px;padding:5px 0;border-bottom:1px solid rgba(51,65,85,.3)">'
                + '<span style="font-size:11px;color:var(--muted);width:20px;text-align:right">' + dev.index + '</span>'
                + '<span style="font-size:12px">' + esc(dev.name) + '</span>'
                + '<button class="btn btn-secondary btn-xs" onclick="document.getElementById(\'ef-device\').value=' + dev.index + '" style="margin-left:auto">Use</button>'
                + '</div>';
        }).join('');
    }).catch(function () {
        el.innerHTML = '<span style="font-size:12px;color:var(--muted)">API offline</span>';
    });
};

// ── Build payload ──────────────────────────────────────────────────────────

function buildPayload() {
    return {
        action:                  'save_config',
        id:                      EDITING_ID,
        slot_id:                 parseInt(document.getElementById('ef-slot').value),
        name:                    document.getElementById('ef-name').value.trim(),
        input_type:              document.getElementById('ef-input').value,
        playlist_path:           document.getElementById('ef-playlist').value.trim(),
        device_index:            parseInt(document.getElementById('ef-device').value) || -1,
        description:             document.getElementById('ef-desc').value.trim(),
        is_active:               document.getElementById('ef-active').checked ? 1 : 0,
        // Auto-start
        auto_start:              document.getElementById('ef-auto-start').checked ? 1 : 0,
        auto_start_delay:        parseInt(document.getElementById('ef-auto-delay').value) || 0,
        // Audio
        codec:                   document.getElementById('ef-codec').value,
        encode_mode:             document.getElementById('ef-mode').value,
        bitrate_kbps:            parseInt(document.getElementById('ef-bitrate').value) || 128,
        sample_rate:             parseInt(document.getElementById('ef-srate').value) || 44100,
        channels:                parseInt(document.getElementById('ef-channels').value) || 2,
        channel_mode:            document.getElementById('ef-chanmode').value,
        quality:                 parseInt(document.getElementById('ef-quality-num').value) || 5,
        volume:                  parseInt(document.getElementById('ef-vol-num').value) / 100,
        shuffle:                 document.getElementById('ef-shuffle').checked ? 1 : 0,
        repeat_all:              document.getElementById('ef-repeat').checked ? 1 : 0,
        // Server
        server_protocol:         document.getElementById('ef-proto').value,
        server_host:             document.getElementById('ef-host').value.trim(),
        server_port:             parseInt(document.getElementById('ef-port').value),
        server_mount:            document.getElementById('ef-mount').value.trim(),
        server_user:             document.getElementById('ef-user').value.trim(),
        server_pass:             document.getElementById('ef-pass').value,
        // Reconnect
        auto_reconnect:          document.getElementById('ef-auto-reconnect').checked ? 1 : 0,
        reconnect_interval:      parseInt(document.getElementById('ef-reconnect-interval').value) || 5,
        reconnect_max_attempts:  parseInt(document.getElementById('ef-reconnect-max').value) || 0,
        // Station & ICY2
        station_name:            document.getElementById('ef-sname').value.trim(),
        genre:                   document.getElementById('ef-genre').value.trim(),
        website_url:             document.getElementById('ef-website').value.trim(),
        icy2_twitter:            document.getElementById('ef-icy-twitter').value.trim(),
        icy2_facebook:           document.getElementById('ef-icy-fb').value.trim(),
        icy2_instagram:          document.getElementById('ef-icy-ig').value.trim(),
        icy2_email:              document.getElementById('ef-icy-email').value.trim(),
        icy2_language:           document.getElementById('ef-icy-lang').value.trim(),
        icy2_country:            document.getElementById('ef-icy-country').value.trim(),
        icy2_city:               document.getElementById('ef-icy-city').value.trim(),
        icy2_is_public:          document.getElementById('ef-icy-public').checked ? 1 : 0,
        // DSP
        eq_enabled:              document.getElementById('ef-eq-en').checked ? 1 : 0,
        eq_preset:               document.getElementById('ef-eq-preset').value,
        agc_enabled:             document.getElementById('ef-agc').checked ? 1 : 0,
        crossfade_enabled:       document.getElementById('ef-xf-en').checked ? 1 : 0,
        crossfade_duration:      parseFloat(document.getElementById('ef-xf-dur').value),
        // Archive
        archive_enabled:         document.getElementById('ef-arch-en').checked ? 1 : 0,
        archive_dir:             document.getElementById('ef-arch-dir').value.trim(),
    };
}

// ── Save to MySQL ──────────────────────────────────────────────────────────

window.saveConfig = function () {
    var banner = document.getElementById('save-result');
    banner.style.display = 'none';

    mc1Api('POST', '/app/api/encoders.php', buildPayload()).then(function (d) {
        if (d && d.ok) {
            mc1Toast('Encoder slot saved', 'ok');
            if (IS_NEW && d.id) {
                // We redirect to the edit page for the newly created slot
                window.location.href = '/edit_encoders.php?id=' + d.id;
            } else {
                banner.style.display  = 'block';
                banner.innerHTML      = '<div class="alert alert-success"><i class="fa-solid fa-circle-check"></i> Configuration saved successfully.</div>';
                EDITING_ID = d.id || EDITING_ID; // update if this was an insert
            }
        } else {
            var msg = (d && d.error) ? d.error : 'Save failed';
            mc1Toast(msg, 'err');
            banner.style.display = 'block';
            banner.innerHTML     = '<div class="alert alert-error"><i class="fa-solid fa-xmark"></i> ' + esc(msg) + '</div>';
        }
    }).catch(function () {
        mc1Toast('Request failed', 'err');
    });
};

// ── Apply DSP to running encoder (direct C++ API — no PHP proxy) ──────────

window.applyDspLive = function () {
    if (!EDITING_SLOT) { mc1Toast('Slot ID not set', 'warn'); return; }
    var stat = document.getElementById('dsp-live-status');
    mc1Api('PUT', '/api/v1/encoders/' + EDITING_SLOT + '/dsp', {
        eq_enabled:         document.getElementById('ef-eq-en').checked,
        eq_preset:          document.getElementById('ef-eq-preset').value,
        agc_enabled:        document.getElementById('ef-agc').checked,
        crossfade_enabled:  document.getElementById('ef-xf-en').checked,
        crossfade_duration: parseFloat(document.getElementById('ef-xf-dur').value)
    }).then(function (d) {
        if (d && d.ok !== false) {
            mc1Toast('DSP applied to Slot ' + EDITING_SLOT, 'ok');
            if (stat) stat.textContent = 'DSP applied live at ' + new Date().toLocaleTimeString();
        } else {
            mc1Toast((d && d.error) || 'DSP update failed', 'err');
        }
    }).catch(function () { mc1Toast('Encoder API offline', 'err'); });
};

// ── Delete ──────────────────────────────────────────────────────────────

window.deleteConfig = function () {
    var name = document.getElementById('ef-name').value || 'this slot';
    if (!confirm('Delete encoder slot "' + name + '" from the database?\n\nThe encoder must be stopped first. This cannot be undone.')) return;
    mc1Api('POST', '/app/api/encoders.php', {action: 'delete_config', id: EDITING_ID}).then(function (d) {
        if (d && d.ok) {
            mc1Toast('Encoder slot deleted', 'ok');
            window.location.href = '/settings.php';
        } else {
            mc1Toast((d && d.error) || 'Delete failed', 'err');
        }
    }).catch(function () { mc1Toast('Request failed', 'err'); });
};

// ── Codec-aware audio settings ─────────────────────────────────────────────

// We define valid bitrates, sample rates, and capabilities per codec.
var CODEC_DEFS = {
    mp3:     { bitrates:[32,40,48,56,64,80,96,112,128,160,192,224,256,320], srates:[22050,32000,44100,48000], modes:['cbr','vbr','abr'],  hasQuality:false, brLabel:'Bitrate (kbps)',   brHint:'Standard LAME CBR bitrates. Use 128–320 for music, 32–64 for talk.', srHint:'44,100 Hz is standard for music. 22,050 Hz for talk/voice.' },
    vorbis:  { bitrates:[],                                                  srates:[22050,44100,48000],      modes:['vbr'],              hasQuality:true,  brLabel:'',                 brHint:'', srHint:'Ogg Vorbis is VBR-only. Use quality slider, not bitrate.', qLbl:'VBR Quality (0–10)', qHint:'0≈64 kbps · 3≈96 · 5≈128 · 7≈192 · 9≈256 · 10≈320+ kbps (average)' },
    opus:    { bitrates:[32,48,64,96,128,160,192,256,320],                   srates:[8000,12000,16000,24000,48000], modes:['cbr','vbr'],   hasQuality:false, brLabel:'Bitrate (kbps)',   brHint:'64–192 kbps recommended for music. Opus at 96k ≈ MP3 at 192k quality.', srHint:'Opus resamples all input to 48 kHz internally. 48000 is optimal.' },
    flac:    { bitrates:[],                                                  srates:[44100,48000,88200,96000], modes:['cbr'],              hasQuality:true,  brLabel:'',                 brHint:'', srHint:'FLAC is lossless. 44,100 or 48,000 Hz for standard; 88,200/96,000 for hi-res.', qLbl:'Compression Level (0–8)', qHint:'0=fastest encode/largest file · 5=default · 8=slowest/smallest. Audio quality is identical at all levels.' },
    aac_lc:  { bitrates:[64,80,96,112,128,160,192,224,256,320],              srates:[22050,32000,44100,48000], modes:['cbr'],              hasQuality:false, brLabel:'Bitrate (kbps)',   brHint:'128–192 kbps recommended for broadcast quality AAC-LC.', srHint:'' },
    aac_he:  { bitrates:[24,32,40,48,56,64,80,96,112,128],                   srates:[32000,44100,48000],      modes:['cbr'],              hasQuality:false, brLabel:'Bitrate (kbps)',   brHint:'HE-AAC v1 (SBR): 48–96 kbps stereo is the sweet spot. Sounds like 128+ kbps AAC-LC.', srHint:'' },
    aac_hev2:{ bitrates:[16,24,32,40,48,56,64],                              srates:[32000,44100,48000],      modes:['cbr'],              hasQuality:false, brLabel:'Bitrate (kbps)',   brHint:'HE-AAC v2 (SBR+PS): 24–48 kbps. Best at low bitrates. ⚠ Channels LOCKED to stereo — PS requires 2-channel input.', srHint:'', lockStereo:true },
    aac_eld: { bitrates:[24,32,40,48,64,80,96,128,160,192],                  srates:[32000,44100,48000],      modes:['cbr'],              hasQuality:false, brLabel:'Bitrate (kbps)',   brHint:'AAC-ELD (low-delay SBR): 48–96 kbps. 512-sample granule — optimised for low-latency live input.', srHint:'' },
};

// We track the current saved sample rate so codecChanged() can re-select it if compatible.
var SAVED_SRATE   = <?= (int)$def('sample_rate', 44100) ?>;
var SAVED_BITRATE = <?= (int)$def('bitrate_kbps', 128) ?>;

window.syncChannels = function () {
    var cm = document.getElementById('ef-chanmode').value;
    document.getElementById('ef-channels').value = (cm === 'mono') ? 1 : 2;
};

function updateQualityHint() {
    var codec = document.getElementById('ef-codec').value;
    var def   = CODEC_DEFS[codec];
    var hint  = document.getElementById('ef-quality-hint');
    if (hint && def) hint.textContent = def.qHint || '';
}

window.codecChanged = function () {
    var codec  = document.getElementById('ef-codec').value;
    var mode   = document.getElementById('ef-mode').value;
    var def    = CODEC_DEFS[codec] || CODEC_DEFS.mp3;

    // Encode mode selector: show only supported modes
    var modeEl  = document.getElementById('ef-mode');
    var modeSel = modeEl.value;
    Array.from(modeEl.options).forEach(function (o) {
        o.hidden = (def.modes.indexOf(o.value) === -1);
    });
    if (def.modes.indexOf(modeSel) === -1) modeEl.value = def.modes[0];
    mode = modeEl.value;
    document.getElementById('ef-mode-grp').style.display = (def.modes.length <= 1) ? 'none' : '';

    // Mode hint
    var mhint = document.getElementById('ef-mode-hint');
    if (mhint) {
        if (codec === 'mp3' && mode === 'vbr') mhint.textContent = 'VBR quality is set by the Quality slider below (0=smallest, 9=best). Bitrate varies per frame.';
        else if (codec === 'mp3' && mode === 'abr') mhint.textContent = 'ABR targets an average bitrate. Quality algorithm set by Quality slider.';
        else mhint.textContent = '';
    }

    // Bitrate selector: show/hide and populate
    var brRow  = document.getElementById('ef-bitrate-row');
    var brEl   = document.getElementById('ef-bitrate');
    var showBr = def.bitrates.length > 0 && !(codec === 'mp3' && mode === 'vbr');
    brRow.style.display = showBr ? '' : 'none';
    if (showBr) {
        var curBr = parseInt(brEl.value) || SAVED_BITRATE;
        brEl.innerHTML = def.bitrates.map(function (br) {
            return '<option value="' + br + '"' + (curBr === br ? ' selected' : '') + '>' + br + ' kbps</option>';
        }).join('');
        // We snap to nearest valid bitrate if saved value not in list
        if (!def.bitrates.includes(curBr)) {
            var nearest = def.bitrates.reduce(function (a, b) { return Math.abs(b - curBr) < Math.abs(a - curBr) ? b : a; });
            brEl.value = nearest;
        }
        document.getElementById('ef-bitrate-hint').textContent = def.brHint || '';
    }

    // Quality slider: show for Vorbis, FLAC, and MP3 VBR
    var showQ = def.hasQuality || (codec === 'mp3' && mode === 'vbr') || (codec === 'mp3' && mode === 'abr');
    document.getElementById('ef-quality-row').style.display = showQ ? '' : 'none';
    if (showQ) {
        var qlbl = document.getElementById('ef-quality-lbl');
        var qmax = (codec === 'flac') ? 8 : 10;
        document.getElementById('ef-quality-slider').max = qmax;
        document.getElementById('ef-quality-num').max    = qmax;
        if (qlbl) qlbl.textContent = def.qLbl || (codec === 'mp3' ? 'VBR Quality (0=smallest → 9=best)' : 'Quality');
        updateQualityHint();
    }

    // Sample rate selector: show only codec-valid rates
    var srEl  = document.getElementById('ef-srate');
    var allRates = { 8000:'8,000 Hz', 12000:'12,000 Hz', 16000:'16,000 Hz', 22050:'22,050 Hz', 24000:'24,000 Hz',
                     32000:'32,000 Hz', 44100:'44,100 Hz (CD)', 48000:'48,000 Hz (Pro)', 88200:'88,200 Hz', 96000:'96,000 Hz (Hi-Res)' };
    var curSr = parseInt(srEl.value) || SAVED_SRATE;
    srEl.innerHTML = def.srates.map(function (sr) {
        return '<option value="' + sr + '"' + (curSr === sr ? ' selected' : '') + '>' + (allRates[sr] || sr + ' Hz') + '</option>';
    }).join('');
    if (!def.srates.includes(curSr)) srEl.value = def.srates[def.srates.length > 2 ? def.srates.length - 2 : 0];
    document.getElementById('ef-srate-hint').textContent = def.srHint || '';

    // Channel mode: lock to stereo for HE-AAC v2 (PS requires stereo input)
    var chanSel = document.getElementById('ef-chanmode');
    var chanHint= document.getElementById('ef-chan-hint');
    if (def.lockStereo) {
        chanSel.value = 'stereo';
        chanSel.disabled = true;
        document.getElementById('ef-channels').value = 2;
        if (chanHint) chanHint.textContent = '⚠ Locked to Stereo — HE-AAC v2 Parametric Stereo requires 2-channel input.';
    } else {
        chanSel.disabled = false;
        if (chanHint) chanHint.textContent = codec === 'mp3' ? 'Joint Stereo is recommended for MP3 — better quality at the same bitrate.' : '';
        syncChannels();
    }
};

// ── Quality presets ──────────────────────────────────────────────────────────

// We define all presets with explicit codec/bitrate/samplerate/quality values.
var QUALITY_PRESETS = {
    talk_am:    { codec:'mp3',     encode_mode:'cbr', bitrate_kbps:32,  sample_rate:22050, channel_mode:'mono',  quality:5, note:'AM Talk Radio' },
    fm_radio:   { codec:'mp3',     encode_mode:'cbr', bitrate_kbps:128, sample_rate:44100, channel_mode:'joint', quality:5, note:'FM Radio' },
    fm_hd:      { codec:'aac_lc',  encode_mode:'cbr', bitrate_kbps:192, sample_rate:48000, channel_mode:'joint', quality:5, note:'HD Radio / Digital' },
    mobile_lo:  { codec:'aac_he',  encode_mode:'cbr', bitrate_kbps:64,  sample_rate:44100, channel_mode:'joint', quality:5, note:'Mobile Low Data' },
    mobile_std: { codec:'opus',    encode_mode:'vbr', bitrate_kbps:96,  sample_rate:48000, channel_mode:'joint', quality:5, note:'Mobile Standard' },
    desktop_std:{ codec:'mp3',     encode_mode:'cbr', bitrate_kbps:192, sample_rate:44100, channel_mode:'joint', quality:5, note:'Desktop Standard' },
    desktop_hq: { codec:'mp3',     encode_mode:'cbr', bitrate_kbps:320, sample_rate:44100, channel_mode:'joint', quality:5, note:'Desktop High Quality' },
    home_th:    { codec:'flac',    encode_mode:'cbr', bitrate_kbps:0,   sample_rate:48000, channel_mode:'stereo',quality:5, note:'Home Theatre Lossless' },
    hi_def:     { codec:'flac',    encode_mode:'cbr', bitrate_kbps:0,   sample_rate:96000, channel_mode:'stereo',quality:0, note:'Hi-Def Studio' },
    live_tv:    { codec:'aac_lc',  encode_mode:'cbr', bitrate_kbps:192, sample_rate:48000, channel_mode:'stereo',quality:5, note:'Live TV Audio' },
    web_budget: { codec:'vorbis',  encode_mode:'vbr', bitrate_kbps:0,   sample_rate:44100, channel_mode:'joint', quality:5, note:'Webcaster Budget ~128k' },
    web_std:    { codec:'opus',    encode_mode:'vbr', bitrate_kbps:128, sample_rate:48000, channel_mode:'joint', quality:5, note:'Webcaster Standard' },
    ultra_low:  { codec:'aac_hev2',encode_mode:'cbr', bitrate_kbps:32,  sample_rate:44100, channel_mode:'stereo',quality:5, note:'Ultra Low Bandwidth' },
};

window.applyPreset = function (key) {
    var p = QUALITY_PRESETS[key];
    if (!p) return;

    // We set codec first, then trigger codecChanged() to repopulate dependent dropdowns.
    document.getElementById('ef-codec').value = p.codec;
    codecChanged();

    // Mode
    document.getElementById('ef-mode').value = p.encode_mode || 'cbr';

    // Bitrate (if visible)
    var brEl = document.getElementById('ef-bitrate');
    if (p.bitrate_kbps && !brEl.parentElement.parentElement.style.display) {
        var def = CODEC_DEFS[p.codec] || {};
        if (def.bitrates && def.bitrates.includes(p.bitrate_kbps)) {
            brEl.value = p.bitrate_kbps;
        } else if (def.bitrates && def.bitrates.length) {
            // Snap to nearest
            var nearest = def.bitrates.reduce(function (a, b) {
                return Math.abs(b - p.bitrate_kbps) < Math.abs(a - p.bitrate_kbps) ? b : a;
            });
            brEl.value = nearest;
        }
    }

    // Sample rate
    var srEl = document.getElementById('ef-srate');
    var srExists = Array.from(srEl.options).some(function (o) { return parseInt(o.value) === p.sample_rate; });
    if (srExists) srEl.value = p.sample_rate;

    // Channel mode
    if (!CODEC_DEFS[p.codec].lockStereo) {
        document.getElementById('ef-chanmode').value = p.channel_mode || 'joint';
        syncChannels();
    }

    // Quality
    document.getElementById('ef-quality-slider').value = p.quality;
    document.getElementById('ef-quality-num').value    = p.quality;
    updateQualityHint();

    mc1Toast('Preset applied: ' + p.note, 'ok');

    // We re-run codecChanged to refresh encode mode visibility after setting codec
    codecChanged();
};

// ── Shared helpers ─────────────────────────────────────────────────────────

function esc(s) {
    return String(s || '').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

})();
</script>

<?php require_once __DIR__ . '/app/inc/footer.php'; ?>
