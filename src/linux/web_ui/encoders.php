<?php
define('MC1_BOOT', true);
require_once __DIR__ . '/app/inc/auth.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/user_auth.php';

$page_title = 'Encoders';
$active_nav = 'encoders';
$use_charts = false;

// Load encoder configs from MySQL — always available even if C++ is offline
$db_configs = [];
try {
    $db_configs = mc1_db('mcaster1_encoder')->query(
        'SELECT id, slot_id, name, codec, bitrate_kbps, sample_rate, channels,
                server_host, server_port, server_mount, station_name, genre,
                volume, shuffle, repeat_all, input_type, playlist_path, is_active
         FROM encoder_configs
         ORDER BY slot_id'
    )->fetchAll();
} catch (Exception $e) {}

require_once __DIR__ . '/app/inc/header.php';
?>

<div class="sec-hdr">
  <div class="sec-title"><i class="fa-solid fa-tower-broadcast" style="color:var(--teal);margin-right:8px"></i>Encoder Controls &amp; DSP</div>
  <div style="display:flex;gap:8px;align-items:center;flex-wrap:wrap">
    <span id="api-status" class="badge badge-gray" title="C++ API connection status">API: —</span>
    <button class="btn btn-secondary btn-sm" onclick="refreshLive()">
      <i class="fa-solid fa-rotate-right"></i> Refresh
    </button>
    <button class="btn btn-secondary btn-sm" onclick="openCopyModal()" style="margin-right:2px">
      <i class="fa-solid fa-copy"></i> Copy Encoder
    </button>
    <button class="btn btn-primary btn-sm" onclick="openWizard()">
      <i class="fa-solid fa-plus"></i> Add Encoder
    </button>
    <a href="/settings.php" class="btn btn-secondary btn-sm">
      <i class="fa-solid fa-gear"></i> Configure Slots
    </a>
  </div>
</div>

<?php if (empty($db_configs)): ?>
<div class="card">
  <div class="empty">
    <i class="fa-solid fa-tower-broadcast" style="font-size:32px;margin-bottom:10px;display:block;color:var(--border)"></i>
    <p>No encoder slots configured. <a href="/settings.php">Add encoder configs in Settings.</a></p>
  </div>
</div>

<?php else: ?>

<?php
$colors = ['#14b8a6','#0891b2','#a78bfa'];
foreach ($db_configs as $idx => $cfg):
    $slot    = (int)$cfg['slot_id'];
    $vol_pct = (int)round(($cfg['volume'] ?? 1.0) * 100);
    $color   = $colors[$idx % 3] ?? '#14b8a6';
?>
<div class="card" id="ep-<?= $slot ?>">
  <!-- Card header: slot number, name, state badge, action buttons -->
  <div class="card-hdr">
    <div class="card-title">
      <span style="width:26px;height:26px;border-radius:5px;background:rgba(255,255,255,.06);display:inline-flex;align-items:center;justify-content:center;font-weight:700;font-size:12px;color:<?= $color ?>;border:1px solid <?= $color ?>30;flex-shrink:0"><?= $slot ?></span>
      <?= h($cfg['name']) ?>
      <span class="state-badge idle" id="es-<?= $slot ?>">OFFLINE</span>
    </div>
    <div style="display:flex;gap:6px" id="ea-<?= $slot ?>">
      <button class="btn btn-success btn-sm" onclick="slotAct(<?= $slot ?>,'start')">
        <i class="fa-solid fa-play"></i> Start
      </button>
      <a href="/edit_encoders.php?id=<?= (int)$cfg['id'] ?>" class="btn btn-icon btn-sm" title="Configure this slot">
        <i class="fa-solid fa-gear"></i>
      </a>
    </div>
  </div>

  <!-- Static info row from MySQL — always visible -->
  <div style="display:flex;gap:14px;font-size:11px;color:var(--muted);margin-bottom:10px;padding-bottom:10px;border-bottom:1px solid rgba(51,65,85,.45);flex-wrap:wrap">
    <span><i class="fa-solid fa-compact-disc fa-fw" style="opacity:.5"></i> <?= h(strtoupper($cfg['codec'] ?? 'mp3')) ?></span>
    <?php if ($cfg['bitrate_kbps']): ?>
    <span><?= (int)$cfg['bitrate_kbps'] ?> kbps</span>
    <?php endif; ?>
    <span><?= (int)(($cfg['sample_rate'] ?? 44100) / 1000) ?> kHz · <?= (int)$cfg['channels'] === 1 ? 'Mono' : 'Stereo' ?></span>
    <span><i class="fa-solid fa-link fa-fw" style="opacity:.5"></i> <?= h($cfg['server_host'].':'.$cfg['server_port'].$cfg['server_mount']) ?></span>
    <?php if ($cfg['playlist_path']): ?>
    <span title="<?= h($cfg['playlist_path']) ?>"><i class="fa-solid fa-list fa-fw" style="opacity:.5"></i> <?= h(basename($cfg['playlist_path'])) ?></span>
    <?php endif; ?>
  </div>

  <!-- Live info row — updated by JS polling -->
  <div style="display:flex;gap:14px;font-size:11px;color:var(--muted);margin-bottom:8px;flex-wrap:wrap">
    <span id="et-<?= $slot ?>"><i class="fa-solid fa-music fa-fw" style="opacity:.5"></i> No track loaded</span>
    <span id="eu-<?= $slot ?>"></span>
    <span id="eb-<?= $slot ?>"></span>
  </div>

  <!-- Track progress bar -->
  <div class="prog-wrap" style="margin-bottom:16px">
    <div class="prog-bar" id="pb-<?= $slot ?>" style="width:0;background:<?= $color ?>"></div>
  </div>

  <!-- DSP + Metadata/Volume grid -->
  <div style="display:grid;grid-template-columns:1fr 1fr;gap:20px">

    <!-- Left: DSP Controls -->
    <div>
      <div style="font-size:10px;font-weight:700;text-transform:uppercase;letter-spacing:.08em;color:var(--muted);margin-bottom:10px">DSP Chain</div>

      <div class="dsp-row">
        <div class="dsp-lbl">EQ Enabled<small>Equalizer on/off</small></div>
        <label class="toggle"><input type="checkbox" id="eq-en-<?= $slot ?>" onchange="applyDsp(<?= $slot ?>)"><span class="toggle-slider"></span></label>
      </div>

      <div class="dsp-row">
        <div class="dsp-lbl">EQ Preset<small>Frequency profile</small></div>
        <select class="form-select" id="eq-preset-<?= $slot ?>" style="width:140px;padding:5px 8px;font-size:12px" onchange="applyDsp(<?= $slot ?>)">
          <option value="flat">Flat (No EQ)</option>
          <option value="classic_rock">Classic Rock</option>
          <option value="country">Country</option>
          <option value="modern_rock">Modern Rock</option>
          <option value="broadcast">Broadcast</option>
          <option value="spoken_word">Spoken Word</option>
        </select>
      </div>

      <div class="dsp-row">
        <div class="dsp-lbl">AGC<small>Auto Gain Control</small></div>
        <label class="toggle"><input type="checkbox" id="agc-en-<?= $slot ?>" onchange="applyDsp(<?= $slot ?>)"><span class="toggle-slider"></span></label>
      </div>

      <div class="dsp-row">
        <div class="dsp-lbl">Crossfade<small id="xfl-<?= $slot ?>">3s duration</small></div>
        <div style="display:flex;gap:8px;align-items:center">
          <label class="toggle"><input type="checkbox" id="xf-en-<?= $slot ?>" onchange="applyDsp(<?= $slot ?>)"><span class="toggle-slider"></span></label>
          <input type="range" min="1" max="10" step=".5" value="3" id="xf-dur-<?= $slot ?>"
            oninput="document.getElementById('xfl-<?= $slot ?>').textContent=this.value+'s duration'"
            onchange="applyDsp(<?= $slot ?>)" style="width:80px">
        </div>
      </div>
    </div>

    <!-- Right: Push Metadata + Volume -->
    <div>
      <div style="font-size:10px;font-weight:700;text-transform:uppercase;letter-spacing:.08em;color:var(--muted);margin-bottom:10px">Push Metadata</div>
      <div class="form-group">
        <label class="form-label">Title</label>
        <input class="form-input" id="mt-<?= $slot ?>" placeholder="Track title">
      </div>
      <div class="form-group">
        <label class="form-label">Artist</label>
        <input class="form-input" id="ma-<?= $slot ?>" placeholder="Artist name">
      </div>
      <div class="form-group">
        <label class="form-label">Volume</label>
        <div class="range-wrap">
          <i class="fa-solid fa-volume-low" style="font-size:12px"></i>
          <input type="range" min="0" max="200" value="<?= $vol_pct ?>"
            oninput="document.getElementById('vv-<?= $slot ?>').textContent=this.value+'%'"
            onchange="setVol(<?= $slot ?>,this)">
          <span id="vv-<?= $slot ?>" style="font-size:11px;min-width:32px"><?= $vol_pct ?>%</span>
        </div>
      </div>
      <button class="btn btn-primary btn-sm" onclick="pushMeta(<?= $slot ?>)">
        <i class="fa-solid fa-paper-plane"></i> Push Metadata
      </button>
    </div>

  </div><!-- /grid -->
</div><!-- /card -->

<?php endforeach; ?>
<?php endif; ?>

<!-- Copy Encoder Modal -->
<div id="copy-modal" style="display:none;position:fixed;inset:0;z-index:800;background:rgba(1,11,15,.82);backdrop-filter:blur(4px);align-items:center;justify-content:center">
  <div style="background:var(--card);border:1px solid var(--border2);border-radius:var(--radius);width:min(480px,96vw);box-shadow:0 8px 40px rgba(0,0,0,.7)">
    <div style="display:flex;align-items:center;justify-content:space-between;padding:18px 22px;border-bottom:1px solid var(--border)">
      <div>
        <div style="font-weight:700;font-size:15px;color:var(--text)"><i class="fa-solid fa-copy"></i> Copy Encoder</div>
        <div style="font-size:11px;color:var(--muted);margin-top:3px">Select a source slot — a full copy will be created and opened for editing.</div>
      </div>
      <button class="btn btn-icon btn-sm" onclick="closeCopyModal()"><i class="fa-solid fa-xmark"></i></button>
    </div>
    <div style="padding:22px">
      <label style="font-size:12px;color:var(--muted);display:block;margin-bottom:6px">Source Encoder Slot</label>
      <select id="copy-src-sel" class="form-control" style="width:100%">
        <option value="">— Loading slots… —</option>
      </select>
      <div id="copy-new-slot" style="margin-top:12px;font-size:12px;color:var(--muted)"></div>
      <div id="copy-err" style="margin-top:10px;color:var(--danger);font-size:12px;display:none"></div>
    </div>
    <div style="display:flex;align-items:center;justify-content:flex-end;gap:8px;padding:14px 22px;border-top:1px solid var(--border);background:var(--bg2)">
      <button class="btn btn-secondary btn-sm" onclick="closeCopyModal()">Cancel</button>
      <button class="btn btn-primary btn-sm" id="copy-btn" onclick="doCopy()">
        <i class="fa-solid fa-copy"></i> Copy &amp; Edit
      </button>
    </div>
  </div>
</div>

<!-- Add Encoder Wizard Modal -->
<div id="enc-wizard" style="display:none;position:fixed;inset:0;z-index:800;background:rgba(1,11,15,.82);backdrop-filter:blur(4px);align-items:center;justify-content:center">
  <div style="background:var(--card);border:1px solid var(--border2);border-radius:var(--radius);width:min(600px,96vw);max-height:90vh;overflow-y:auto;box-shadow:0 8px 40px rgba(0,0,0,.7)">

    <!-- Wizard header -->
    <div style="display:flex;align-items:center;justify-content:space-between;padding:18px 22px;border-bottom:1px solid var(--border)">
      <div>
        <div style="font-weight:700;font-size:15px;color:var(--text)"><i class="fa-solid fa-tower-broadcast" style="color:var(--teal);margin-right:8px"></i>Add Encoder</div>
        <div style="font-size:11px;color:var(--muted);margin-top:3px" id="wiz-step-lbl">Step 1 of 4 — Basic Setup</div>
      </div>
      <button class="btn btn-icon btn-sm" onclick="closeWizard()"><i class="fa-solid fa-xmark"></i></button>
    </div>

    <!-- Step indicators -->
    <div style="display:flex;padding:14px 22px 0;gap:6px" id="wiz-steps-bar">
      <?php foreach ([1=>'Basic',2=>'Audio',3=>'Server',4=>'Station'] as $n=>$lbl): ?>
      <div id="wsb-<?= $n ?>" style="flex:1;text-align:center;font-size:10px;font-weight:700;text-transform:uppercase;letter-spacing:.06em;padding-bottom:10px;border-bottom:2px solid <?= $n===1 ? 'var(--teal)' : 'var(--border)' ?>;color:<?= $n===1 ? 'var(--teal)' : 'var(--muted)' ?>;transition:color .2s,border-color .2s">
        <div style="font-size:12px;font-weight:700;margin-bottom:3px"><?= $n ?></div>
        <?= h($lbl) ?>
      </div>
      <?php endforeach; ?>
    </div>

    <!-- Wizard body -->
    <div style="padding:22px">

      <!-- Step 1: Basic -->
      <div id="wiz-s1">
        <div class="form-row" style="margin-bottom:16px">
          <div class="form-group">
            <label class="form-label"><i class="fa-solid fa-hashtag"></i> Slot Number</label>
            <input type="number" class="form-input" id="w-slot" min="1" max="99" placeholder="13" required>
            <small style="color:var(--muted);font-size:11px">Must be unique. Current slots: <?= implode(', ', array_column($db_configs, 'slot_id')) ?: 'none' ?>.</small>
          </div>
          <div class="form-group">
            <label class="form-label"><i class="fa-solid fa-tag"></i> Name</label>
            <input type="text" class="form-input" id="w-name" placeholder="My Station — MP3 128k" required>
          </div>
        </div>
        <div class="form-group" style="margin-bottom:16px">
          <label class="form-label"><i class="fa-solid fa-plug"></i> Input Type</label>
          <select class="form-select" id="w-input" onchange="wizInputChange()">
            <option value="playlist">Playlist (M3U/PLS/TXT file)</option>
            <option value="url">Stream URL (relay)</option>
            <option value="device">Audio Device (PortAudio)</option>
          </select>
        </div>
        <div class="form-group" id="w-playlist-grp">
          <label class="form-label"><i class="fa-solid fa-list"></i> Playlist Path</label>
          <input type="text" class="form-input" id="w-playlist" placeholder="/var/www/mcaster1.com/audio/playlists/my-playlist.m3u">
          <small style="color:var(--muted);font-size:11px">Full path to .m3u, .pls, .xspf, or .txt file on disk.</small>
        </div>
        <div class="form-group" id="w-url-grp" style="display:none">
          <label class="form-label"><i class="fa-solid fa-satellite-dish"></i> Source URL</label>
          <input type="text" class="form-input" id="w-url" placeholder="http://stream.example.com:8000/live">
        </div>
        <div class="form-group" id="w-dev-grp" style="display:none">
          <label class="form-label"><i class="fa-solid fa-microphone"></i> Device Index</label>
          <input type="number" class="form-input" id="w-dev" min="-1" value="-1">
          <small style="color:var(--muted);font-size:11px">Use <a href="#" onclick="loadDevList();return false">Load Devices</a> to find index, or -1 for default.</small>
          <div id="w-dev-list" style="margin-top:8px"></div>
        </div>
      </div>

      <!-- Step 2: Audio -->
      <div id="wiz-s2" style="display:none">
        <div class="form-row" style="margin-bottom:16px">
          <div class="form-group">
            <label class="form-label"><i class="fa-solid fa-file-audio"></i> Codec</label>
            <select class="form-select" id="w-codec">
              <option value="mp3">MP3</option>
              <option value="opus">Opus</option>
              <option value="vorbis">Vorbis/Ogg</option>
              <option value="aac_lc">AAC-LC</option>
              <option value="aac_he">HE-AAC v1 (AAC+)</option>
              <option value="aac_hev2">HE-AAC v2 (AAC++)</option>
              <option value="aac_eld">AAC-ELD</option>
              <option value="flac">FLAC (lossless)</option>
            </select>
          </div>
          <div class="form-group">
            <label class="form-label"><i class="fa-solid fa-gauge-simple"></i> Bitrate (kbps)</label>
            <select class="form-select" id="w-bitrate">
              <option value="32">32</option>
              <option value="48">48</option>
              <option value="64">64</option>
              <option value="96">96</option>
              <option value="128" selected>128</option>
              <option value="192">192</option>
              <option value="256">256</option>
              <option value="320">320</option>
            </select>
          </div>
        </div>
        <div class="form-row" style="margin-bottom:16px">
          <div class="form-group">
            <label class="form-label"><i class="fa-solid fa-wave-square"></i> Sample Rate</label>
            <select class="form-select" id="w-srate">
              <option value="22050">22,050 Hz</option>
              <option value="44100" selected>44,100 Hz (CD)</option>
              <option value="48000">48,000 Hz (Pro)</option>
            </select>
          </div>
          <div class="form-group">
            <label class="form-label"><i class="fa-solid fa-headphones"></i> Channels</label>
            <select class="form-select" id="w-chan">
              <option value="2" selected>Stereo (2)</option>
              <option value="1">Mono (1)</option>
            </select>
          </div>
        </div>
        <div class="form-group">
          <label class="form-label"><i class="fa-solid fa-volume-high"></i> Initial Volume</label>
          <div class="range-wrap">
            <i class="fa-solid fa-volume-low" style="font-size:12px"></i>
            <input type="range" min="0" max="200" value="100" id="w-vol" oninput="document.getElementById('w-vol-v').textContent=this.value+'%'">
            <span id="w-vol-v" style="font-size:11px;min-width:36px">100%</span>
          </div>
        </div>
      </div>

      <!-- Step 3: Server -->
      <div id="wiz-s3" style="display:none">
        <div class="form-row" style="margin-bottom:16px">
          <div class="form-group">
            <label class="form-label"><i class="fa-solid fa-server"></i> Server Host</label>
            <input type="text" class="form-input" id="w-host" placeholder="dnas.mcaster1.com" required>
          </div>
          <div class="form-group">
            <label class="form-label"><i class="fa-solid fa-network-wired"></i> Port</label>
            <input type="number" class="form-input" id="w-port" value="9443" min="1" max="65535" required>
          </div>
        </div>
        <div class="form-row" style="margin-bottom:16px">
          <div class="form-group">
            <label class="form-label"><i class="fa-solid fa-link"></i> Mount Point</label>
            <input type="text" class="form-input" id="w-mount" placeholder="/live" required>
          </div>
          <div class="form-group">
            <label class="form-label"><i class="fa-solid fa-user"></i> Source User</label>
            <input type="text" class="form-input" id="w-user" value="source">
          </div>
        </div>
        <div class="form-group" style="margin-bottom:16px">
          <label class="form-label"><i class="fa-solid fa-key"></i> Source Password</label>
          <div class="input-wrap">
            <input type="password" class="form-input" id="w-pass" placeholder="hackme" autocomplete="new-password">
            <button type="button" class="input-toggle" onclick="var i=document.getElementById('w-pass');i.type=i.type==='password'?'text':'password'"><i class="fa-solid fa-eye"></i></button>
          </div>
        </div>
        <div class="form-group">
          <label class="form-label">Quick Presets</label>
          <div style="display:flex;gap:8px;flex-wrap:wrap">
            <button type="button" class="btn btn-secondary btn-sm" onclick="fillServer('dnas.mcaster1.com',9443,'source')">Mcaster1 DNAS</button>
            <button type="button" class="btn btn-secondary btn-sm" onclick="fillServer('localhost',8000,'source')">Local Icecast</button>
            <button type="button" class="btn btn-secondary btn-sm" onclick="fillServer('localhost',8000,'admin')">Local Shoutcast</button>
          </div>
        </div>
      </div>

      <!-- Step 4: Station Info + DSP defaults -->
      <div id="wiz-s4" style="display:none">
        <div class="form-row" style="margin-bottom:16px">
          <div class="form-group">
            <label class="form-label"><i class="fa-solid fa-radio"></i> Station Name</label>
            <input type="text" class="form-input" id="w-sname" placeholder="My Radio Station">
          </div>
          <div class="form-group">
            <label class="form-label"><i class="fa-solid fa-music"></i> Genre</label>
            <input type="text" class="form-input" id="w-genre" placeholder="Rock, Pop, Talk…">
          </div>
        </div>
        <div class="form-group" style="margin-bottom:16px">
          <label class="form-label"><i class="fa-solid fa-globe"></i> Website URL</label>
          <input type="text" class="form-input" id="w-website" placeholder="https://example.com">
        </div>
        <div style="border-top:1px solid rgba(51,65,85,.5);padding-top:14px;margin-top:4px;margin-bottom:14px">
          <div style="font-size:10px;font-weight:700;text-transform:uppercase;letter-spacing:.08em;color:var(--muted);margin-bottom:12px">DSP Defaults</div>
          <div class="dsp-row">
            <div class="dsp-lbl">EQ Enabled<small>Apply EQ on this slot</small></div>
            <label class="toggle"><input type="checkbox" id="w-eq-en"><span class="toggle-slider"></span></label>
          </div>
          <div class="dsp-row">
            <div class="dsp-lbl">EQ Preset<small>Starting frequency profile</small></div>
            <select class="form-select" id="w-eq-preset" style="width:140px;padding:5px 8px;font-size:12px">
              <option value="flat">Flat</option>
              <option value="classic_rock">Classic Rock</option>
              <option value="country">Country</option>
              <option value="modern_rock">Modern Rock</option>
              <option value="broadcast">Broadcast</option>
              <option value="spoken_word">Spoken Word</option>
            </select>
          </div>
          <div class="dsp-row">
            <div class="dsp-lbl">AGC<small>Auto Gain Control</small></div>
            <label class="toggle"><input type="checkbox" id="w-agc-en"><span class="toggle-slider"></span></label>
          </div>
          <div class="dsp-row">
            <div class="dsp-lbl">Crossfade<small>Auto crossfade between tracks</small></div>
            <label class="toggle"><input type="checkbox" id="w-xf-en" checked><span class="toggle-slider"></span></label>
          </div>
          <div class="dsp-row">
            <div class="dsp-lbl">Crossfade Duration<small>Fade length in seconds</small></div>
            <div style="display:flex;align-items:center;gap:8px">
              <input type="range" min="1" max="10" step=".5" value="3" id="w-xf-dur" oninput="document.getElementById('w-xf-lbl').textContent=this.value+'s'" style="width:80px">
              <span id="w-xf-lbl" style="font-size:12px;color:var(--muted);min-width:28px">3s</span>
            </div>
          </div>
          <div class="dsp-row">
            <div class="dsp-lbl">Shuffle<small>Random track order</small></div>
            <label class="toggle"><input type="checkbox" id="w-shuffle"><span class="toggle-slider"></span></label>
          </div>
          <div class="dsp-row">
            <div class="dsp-lbl">Repeat All<small>Loop playlist</small></div>
            <label class="toggle"><input type="checkbox" id="w-repeat" checked><span class="toggle-slider"></span></label>
          </div>
        </div>
        <div id="wiz-result"></div>
      </div>

    </div><!-- /body -->

    <!-- Wizard footer -->
    <div style="display:flex;align-items:center;justify-content:space-between;padding:14px 22px;border-top:1px solid var(--border);background:var(--bg2)">
      <button class="btn btn-secondary btn-sm" id="wiz-back" style="visibility:hidden" onclick="wizStep(-1)">
        <i class="fa-solid fa-arrow-left"></i> Back
      </button>
      <div style="font-size:12px;color:var(--muted)" id="wiz-foot-lbl">Complete all fields to create a new encoder slot.</div>
      <button class="btn btn-primary btn-sm" id="wiz-next" onclick="wizStep(1)">
        Next <i class="fa-solid fa-arrow-right"></i>
      </button>
    </div>
  </div>
</div>

<script>
(function(){
var SLOT_COLORS  = ['#14b8a6','#0891b2','#a78bfa'];
// We map slot_id → encoder_configs.id so the dynamic gear link always targets the right row.
var SLOT_CFG_IDS = <?= json_encode(array_column($db_configs, 'id', 'slot_id')) ?>;

function updateApiStatus(online) {
  var el = document.getElementById('api-status');
  if (!el) return;
  el.textContent = online ? 'API: Online' : 'API: Offline';
  el.className   = 'badge ' + (online ? 'badge-green' : 'badge-red');
}

function fmtUp(s) {
  if (!s) return '';
  var h=Math.floor(s/3600), m=Math.floor((s%3600)/60), ss=s%60;
  if (h>0) return h+'h '+m+'m';
  if (m>0) return m+'m '+ss+'s';
  return ss+'s';
}
function sc(state){ return ({'live':'live','idle':'idle','connecting':'connecting','reconnecting':'connecting','sleep':'sleep','error':'error','stopping':'idle','starting':'connecting'})[(state||'').toLowerCase()]||'idle'; }
function esc(s){ return String(s||'').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;'); }

// Overlay live C++ state onto an existing PHP-rendered card
function updateSlotLive(enc, idx) {
  var id   = enc.slot_id;
  var card = document.getElementById('ep-'+id);
  if (!card) return;

  var cls   = sc(enc.state);
  var color = SLOT_COLORS[idx % SLOT_COLORS.length];

  // State badge
  var sb = document.getElementById('es-'+id);
  if (sb) { sb.textContent = (enc.state||'idle').toUpperCase(); sb.className = 'state-badge '+cls; }

  // Card border color
  card.className = 'card ' + (cls !== 'idle' ? 'slot-card '+cls : '');

  // Live track info
  var track = [enc.track_title, enc.track_artist].filter(Boolean).join(' \u2014 ') || 'No track loaded';
  var et = document.getElementById('et-'+id);
  if (et) et.innerHTML = '<i class="fa-solid fa-music fa-fw" style="opacity:.5"></i> '+esc(track);

  // Progress bar
  var pct = (enc.duration_ms && enc.position_ms)
    ? Math.min(100, Math.round(enc.position_ms / enc.duration_ms * 100)) : 0;
  var pb = document.getElementById('pb-'+id);
  if (pb) { pb.style.width = pct+'%'; pb.style.background = color; }

  // Uptime
  var eu = document.getElementById('eu-'+id);
  if (eu) eu.innerHTML = enc.uptime_sec
    ? '<i class="fa-regular fa-clock fa-fw" style="opacity:.5"></i> '+fmtUp(enc.uptime_sec)
    : '';

  // Bytes sent
  var eb = document.getElementById('eb-'+id);
  if (eb) eb.innerHTML = enc.bytes_sent > 0
    ? '<i class="fa-solid fa-arrow-up fa-fw" style="opacity:.5"></i> '+Math.round(enc.bytes_sent/1024/1024*10)/10+' MB'
    : '';

  // Action buttons
  var ea = document.getElementById('ea-'+id);
  if (ea) {
    if (enc.is_live || cls === 'live' || cls === 'connecting') {
      ea.innerHTML =
        '<button class="btn btn-danger btn-sm" onclick="slotAct('+id+',\'stop\')"><i class="fa-solid fa-stop"></i> Stop</button>'
       +'<button class="btn btn-secondary btn-sm" onclick="slotAct('+id+',\'restart\')"><i class="fa-solid fa-rotate-right"></i></button>'
       +'<a href="/edit_encoders.php?id='+(SLOT_CFG_IDS[id]||0)+'" class="btn btn-icon btn-sm" title="Configure"><i class="fa-solid fa-gear"></i></a>';
    } else if (cls === 'sleep') {
      // SLEEP: encoder was live but exhausted reconnect attempts; show Wake button
      ea.innerHTML =
        '<button class="btn btn-warn btn-sm" onclick="slotAct('+id+',\'wake\')"><i class="fa-solid fa-bell"></i> Wake</button>'
       +'<button class="btn btn-success btn-sm" onclick="slotAct('+id+',\'start\')" title="Force fresh start"><i class="fa-solid fa-play"></i></button>'
       +'<a href="/edit_encoders.php?id='+(SLOT_CFG_IDS[id]||0)+'" class="btn btn-icon btn-sm" title="Configure"><i class="fa-solid fa-gear"></i></a>';
    } else {
      ea.innerHTML =
        '<button class="btn btn-success btn-sm" onclick="slotAct('+id+',\'start\')"><i class="fa-solid fa-play"></i> Start</button>'
       +'<a href="/edit_encoders.php?id='+(SLOT_CFG_IDS[id]||0)+'" class="btn btn-icon btn-sm" title="Configure"><i class="fa-solid fa-gear"></i></a>';
    }
  }
}

// Load DSP settings from C++ and populate controls (best-effort)
function loadDsp(slot) {
  mc1Api('GET', '/api/v1/encoders/'+slot+'/dsp').then(function(dsp){
    if (!dsp || dsp.ok === false) return;
    var eqEn  = document.getElementById('eq-en-'+slot);
    var eqPr  = document.getElementById('eq-preset-'+slot);
    var agcEn = document.getElementById('agc-en-'+slot);
    var xfEn  = document.getElementById('xf-en-'+slot);
    var xfDur = document.getElementById('xf-dur-'+slot);
    var xfLbl = document.getElementById('xfl-'+slot);
    if (eqEn)  eqEn.checked  = !!dsp.eq_enabled;
    if (eqPr && dsp.eq_preset) eqPr.value = dsp.eq_preset;
    if (agcEn) agcEn.checked = !!dsp.agc_enabled;
    if (xfEn)  xfEn.checked  = !!dsp.crossfade_enabled;
    if (xfDur && dsp.crossfade_duration) {
      xfDur.value = dsp.crossfade_duration;
      if (xfLbl) xfLbl.textContent = dsp.crossfade_duration + 's duration';
    }
  }).catch(function(){});
}

// Public actions
window.slotAct = function(slot, action) {
  mc1Api('POST', '/api/v1/encoders/'+slot+'/'+action).then(function(d){
    mc1Toast('Slot '+slot+' '+action+(d.ok===false?' failed':' OK'), d.ok===false?'err':'ok');
    setTimeout(refreshLive, 900);
  }).catch(function(){ mc1Toast('Encoder API unreachable','err'); });
};

window.applyDsp = function(slot) {
  mc1Api('PUT', '/api/v1/encoders/'+slot+'/dsp', {
    eq_enabled:         document.getElementById('eq-en-'+slot).checked,
    eq_preset:          document.getElementById('eq-preset-'+slot).value,
    agc_enabled:        document.getElementById('agc-en-'+slot).checked,
    crossfade_enabled:  document.getElementById('xf-en-'+slot).checked,
    crossfade_duration: parseFloat(document.getElementById('xf-dur-'+slot).value)
  }).then(function(d){
    mc1Toast(d.ok===false?(d.error||'DSP error'):'DSP updated', d.ok===false?'err':'ok');
  }).catch(function(){ mc1Toast('API offline — DSP not applied','err'); });
};

window.pushMeta = function(slot) {
  mc1Api('PUT', '/api/v1/metadata', {
    slot:   slot,
    title:  document.getElementById('mt-'+slot).value,
    artist: document.getElementById('ma-'+slot).value
  }).then(function(d){
    mc1Toast(d.ok===false?(d.error||'Error'):'Metadata pushed', d.ok===false?'err':'ok');
  }).catch(function(){ mc1Toast('API offline','err'); });
};

window.setVol = function(slot, inp) {
  mc1Api('PUT', '/api/v1/volume', {slot: slot, volume: inp.value / 100}).catch(function(){});
};

window.refreshLive = function() {
  mc1Api('GET', '/api/v1/encoders').then(function(encoders){
    if (!Array.isArray(encoders) || encoders.length === 0) {
      updateApiStatus(false);
      return;
    }
    updateApiStatus(true);
    encoders.forEach(function(enc, idx){ updateSlotLive(enc, idx); });
  }).catch(function(){ updateApiStatus(false); });
};

// Defer until DOMContentLoaded so footer.php's mc1Api is defined before we call it.
document.addEventListener('DOMContentLoaded', function() {
  refreshLive();
<?php foreach ($db_configs as $cfg): ?>
  loadDsp(<?= (int)$cfg['slot_id'] ?>);
<?php endforeach; ?>
  setInterval(refreshLive, 8000);
});

// ── Add Encoder Wizard ────────────────────────────────────────────────
var wizCurStep = 1;
var wizTotalSteps = 4;
var wizStepLabels = {1:'Step 1 of 4 — Basic Setup', 2:'Step 2 of 4 — Audio Settings', 3:'Step 3 of 4 — Server Setup', 4:'Step 4 of 4 — Station & DSP'};

window.openWizard = function() {
  wizCurStep = 1;
  wizRender();
  document.getElementById('enc-wizard').style.display = 'flex';
  document.getElementById('wiz-result').innerHTML = '';
};

window.closeWizard = function() {
  document.getElementById('enc-wizard').style.display = 'none';
};

document.getElementById('enc-wizard').addEventListener('click', function(e) {
  if (e.target === this) closeWizard();
});

// ── Copy Encoder Modal ────────────────────────────────────────────────────
var copyNextSlot = 0;

window.openCopyModal = function() {
  document.getElementById('copy-err').style.display = 'none';
  document.getElementById('copy-new-slot').textContent = '';
  document.getElementById('copy-src-sel').innerHTML = '<option value="">— Loading… —</option>';
  document.getElementById('copy-modal').style.display = 'flex';
  mc1Api('POST', '/app/api/encoders.php', {action:'list_configs'}).then(function(d) {
    var sel = document.getElementById('copy-src-sel');
    sel.innerHTML = '<option value="">— Select a slot —</option>';
    var maxSlot = 0;
    if (d && d.configs) {
      d.configs.forEach(function(c) {
        var opt = document.createElement('option');
        opt.value = c.id;
        opt.textContent = 'Slot ' + c.slot_id + ' \u2014 ' + c.name;
        sel.appendChild(opt);
        if (parseInt(c.slot_id) > maxSlot) maxSlot = parseInt(c.slot_id);
      });
    }
    copyNextSlot = maxSlot + 1;
    document.getElementById('copy-new-slot').textContent =
      'New copy will be assigned Slot ' + copyNextSlot + ' \u2014 you can change this in the editor.';
  });
};

window.closeCopyModal = function() {
  document.getElementById('copy-modal').style.display = 'none';
};

document.getElementById('copy-modal').addEventListener('click', function(e) {
  if (e.target === this) closeCopyModal();
});

window.doCopy = function() {
  var src = document.getElementById('copy-src-sel').value;
  var errEl = document.getElementById('copy-err');
  errEl.style.display = 'none';
  if (!src) {
    errEl.textContent = 'Please select a source slot.';
    errEl.style.display = 'block';
    return;
  }
  var btn = document.getElementById('copy-btn');
  btn.disabled = true;
  btn.innerHTML = '<i class="fa-solid fa-spinner fa-spin"></i> Copying\u2026';
  mc1Api('POST', '/app/api/encoders.php', {action:'copy_config', source_id: parseInt(src)}).then(function(d) {
    if (d && d.ok) {
      window.location = '/edit_encoders.php?id=' + d.id;
    } else {
      errEl.textContent = (d && d.error) ? d.error : 'Copy failed.';
      errEl.style.display = 'block';
      btn.disabled = false;
      btn.innerHTML = '<i class="fa-solid fa-copy"></i> Copy &amp; Edit';
    }
  }).catch(function() {
    errEl.textContent = 'Request failed — check your connection.';
    errEl.style.display = 'block';
    btn.disabled = false;
    btn.innerHTML = '<i class="fa-solid fa-copy"></i> Copy &amp; Edit';
  });
};

window.wizInputChange = function() {
  var v = document.getElementById('w-input').value;
  document.getElementById('w-playlist-grp').style.display = v === 'playlist' ? '' : 'none';
  document.getElementById('w-url-grp').style.display      = v === 'url'      ? '' : 'none';
  document.getElementById('w-dev-grp').style.display      = v === 'device'   ? '' : 'none';
};

window.fillServer = function(host, port, user) {
  document.getElementById('w-host').value = host;
  document.getElementById('w-port').value = port;
  document.getElementById('w-user').value = user;
};

window.loadDevList = function() {
  var el = document.getElementById('w-dev-list');
  el.innerHTML = '<span style="color:var(--muted);font-size:12px">Loading…</span>';
  mc1Api('GET', '/api/v1/devices').then(function(d) {
    if (!d || !Array.isArray(d.devices) || d.devices.length === 0) {
      el.innerHTML = '<span style="color:var(--muted);font-size:12px">No devices found (C++ offline?)</span>';
      return;
    }
    el.innerHTML = d.devices.map(function(dev) {
      return '<div style="display:flex;align-items:center;gap:10px;padding:6px 0;border-bottom:1px solid rgba(51,65,85,.3)">'
        + '<span style="font-size:11px;color:var(--muted);width:20px;text-align:right">' + dev.index + '</span>'
        + '<span style="font-size:12px;color:var(--text)">' + esc(dev.name) + '</span>'
        + '<button class="btn btn-secondary btn-xs" onclick="document.getElementById(\'w-dev\').value=' + dev.index + '" style="margin-left:auto">Select</button>'
        + '</div>';
    }).join('');
  }).catch(function() {
    el.innerHTML = '<span style="color:var(--muted);font-size:12px">API offline</span>';
  });
};

function wizValidate(step) {
  if (step === 1) {
    var slot = parseInt(document.getElementById('w-slot').value);
    if (!slot || slot < 1) { mc1Toast('Enter a valid slot number (1+)','err'); return false; }
    if (!document.getElementById('w-name').value.trim()) { mc1Toast('Enter a slot name','err'); return false; }
    return true;
  }
  if (step === 3) {
    if (!document.getElementById('w-host').value.trim()) { mc1Toast('Enter a server host','err'); return false; }
    if (!document.getElementById('w-mount').value.trim()) { mc1Toast('Enter a mount point (e.g. /live)','err'); return false; }
    return true;
  }
  return true;
}

function wizRender() {
  [1,2,3,4].forEach(function(n) {
    var pane = document.getElementById('wiz-s'+n);
    if (pane) pane.style.display = n === wizCurStep ? '' : 'none';
    var bar  = document.getElementById('wsb-'+n);
    if (bar) {
      bar.style.borderColor = n <= wizCurStep ? 'var(--teal)' : 'var(--border)';
      bar.style.color       = n <= wizCurStep ? 'var(--teal)' : 'var(--muted)';
    }
  });
  var lbl = document.getElementById('wiz-step-lbl');
  if (lbl) lbl.textContent = wizStepLabels[wizCurStep] || '';

  var back = document.getElementById('wiz-back');
  var next = document.getElementById('wiz-next');
  var foot = document.getElementById('wiz-foot-lbl');
  if (back) back.style.visibility = wizCurStep > 1 ? 'visible' : 'hidden';
  if (next) next.innerHTML = wizCurStep < wizTotalSteps
    ? 'Next <i class="fa-solid fa-arrow-right"></i>'
    : '<i class="fa-solid fa-floppy-disk"></i> Save Encoder';
  if (foot) {
    var labels = {1:'Fill in slot number, name and input type.',2:'Choose codec and audio quality.',3:'Enter the Icecast/DNAS server details.',4:'Optional station info + DSP defaults. Click Save to create.'};
    foot.textContent = labels[wizCurStep] || '';
  }
}

window.wizStep = function(dir) {
  if (dir > 0 && !wizValidate(wizCurStep)) return;
  if (dir > 0 && wizCurStep === wizTotalSteps) { wizSave(); return; }
  wizCurStep = Math.max(1, Math.min(wizTotalSteps, wizCurStep + dir));
  wizRender();
};

function wizSave() {
  var btn = document.getElementById('wiz-next');
  btn.disabled = true;
  btn.innerHTML = '<div class="spinner"></div> Saving…';

  var inputType = document.getElementById('w-input').value;
  var playlist  = inputType === 'playlist' ? document.getElementById('w-playlist').value.trim() : '';
  if (inputType === 'url') playlist = document.getElementById('w-url').value.trim();

  var payload = {
    action:           'save_config',
    slot_id:          parseInt(document.getElementById('w-slot').value),
    name:             document.getElementById('w-name').value.trim(),
    input_type:       inputType,
    playlist_path:    playlist,
    codec:            document.getElementById('w-codec').value,
    bitrate_kbps:     parseInt(document.getElementById('w-bitrate').value),
    sample_rate:      parseInt(document.getElementById('w-srate').value),
    channels:         parseInt(document.getElementById('w-chan').value),
    volume:           parseInt(document.getElementById('w-vol').value) / 100,
    server_host:      document.getElementById('w-host').value.trim(),
    server_port:      parseInt(document.getElementById('w-port').value),
    server_mount:     document.getElementById('w-mount').value.trim(),
    server_user:      document.getElementById('w-user').value.trim(),
    server_pass:      document.getElementById('w-pass').value,
    station_name:     document.getElementById('w-sname').value.trim(),
    genre:            document.getElementById('w-genre').value.trim(),
    website_url:      document.getElementById('w-website').value.trim(),
    eq_enabled:       document.getElementById('w-eq-en').checked ? 1 : 0,
    eq_preset:        document.getElementById('w-eq-preset').value,
    agc_enabled:      document.getElementById('w-agc-en').checked ? 1 : 0,
    crossfade_enabled: document.getElementById('w-xf-en').checked ? 1 : 0,
    crossfade_duration: parseFloat(document.getElementById('w-xf-dur').value),
    shuffle:          document.getElementById('w-shuffle').checked ? 1 : 0,
    repeat_all:       document.getElementById('w-repeat').checked ? 1 : 0,
    is_active:        1
  };

  mc1Api('POST', '/app/api/encoders.php', payload).then(function(d) {
    btn.disabled = false;
    btn.innerHTML = '<i class="fa-solid fa-floppy-disk"></i> Save Encoder';
    if (d && d.ok) {
      mc1Toast('Encoder slot ' + payload.slot_id + ' created', 'ok');
      // We redirect to the full editor so the user can fill in all extended fields.
      setTimeout(function() {
        window.location.href = '/edit_encoders.php?id=' + (d.id || 0);
      }, 600);
    } else {
      document.getElementById('wiz-result').innerHTML =
        '<div class="alert alert-error"><i class="fa-solid fa-xmark"></i> ' + esc((d && d.error) ? d.error : 'Save failed') + '</div>';
    }
  }).catch(function() {
    btn.disabled = false;
    btn.innerHTML = '<i class="fa-solid fa-floppy-disk"></i> Save Encoder';
    document.getElementById('wiz-result').innerHTML =
      '<div class="alert alert-error"><i class="fa-solid fa-xmark"></i> Request failed</div>';
  });
}

})();
</script>

<?php require_once __DIR__ . '/app/inc/footer.php'; ?>
