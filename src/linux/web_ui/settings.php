<?php
/**
 * settings.php — Settings & Administration
 *
 * File:    src/linux/web_ui/settings.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-02-23
 * Purpose: We display and manage server settings, streaming server roster,
 *          encoder slot configurations, and database administration.
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We use first-person plural in all comments
 *  - We use h() for all user data rendered into HTML
 *  - We use mc1Api() from footer.php for all JSON calls
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/app/inc/auth.php';
require_once __DIR__ . '/app/inc/mc1_config.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/icons.php';
require_once __DIR__ . '/app/inc/user_auth.php';

$page_title = 'Settings';
$active_nav = 'settings';
$use_charts = false;

$user      = mc1_current_user();
$can_admin = $user && !empty($user['can_admin']);

/* ── SSL certificate info ─────────────────────────────────────────────────── */
$cert_info = [];
$cert_file = MC1_SSL_CERT;
if (is_readable($cert_file)) {
    $cdata = file_get_contents($cert_file);
    if ($cdata) {
        $parsed = openssl_x509_parse($cdata);
        if ($parsed) {
            $cert_info = [
                'cn'         => $parsed['subject']['CN'] ?? '—',
                'issuer'     => $parsed['issuer']['O'] ?? ($parsed['issuer']['CN'] ?? '—'),
                'valid_from' => isset($parsed['validFrom_time_t']) ? date('Y-m-d', $parsed['validFrom_time_t']) : '—',
                'valid_to'   => isset($parsed['validTo_time_t'])   ? date('Y-m-d', $parsed['validTo_time_t'])   : '—',
                'expires_in' => isset($parsed['validTo_time_t'])
                    ? max(0, (int)(($parsed['validTo_time_t'] - time()) / 86400)) : 0,
            ];
        }
    }
}

/* ── Admin-only data ──────────────────────────────────────────────────────── */
$db_users          = [];
$db_roles          = [];
$db_sizes          = [];
$enc_configs       = [];
$streaming_servers = [];

if ($can_admin) {
    try {
        $edb      = mc1_db('mcaster1_encoder');
        $db_users = $edb->query(
            'SELECT u.id, u.username, u.display_name, u.email, u.is_active, u.last_login,
                    u.role_id, r.label as role_label, r.name as role_name
             FROM users u JOIN roles r ON r.id = u.role_id ORDER BY u.id'
        )->fetchAll();
        $db_roles = $edb->query('SELECT * FROM roles ORDER BY id')->fetchAll();
    } catch (Exception $e) {}

    try {
        $db_sizes = mc1_db('information_schema')->query(
            "SELECT table_schema as db_name,
             ROUND(SUM(data_length+index_length)/1024/1024,2) as size_mb,
             COUNT(*) as table_count
             FROM tables
             WHERE table_schema IN ('mcaster1_media','mcaster1_metrics','mcaster1_encoder')
             GROUP BY table_schema ORDER BY table_schema"
        )->fetchAll();
    } catch (Exception $e) {}

    try {
        $enc_configs = mc1_db('mcaster1_encoder')->query(
            'SELECT id, slot_id, name, codec, bitrate_kbps, sample_rate, channels,
                    server_host, server_port, server_mount, server_user,
                    server_protocol, playlist_path, input_type, station_name, genre,
                    website_url, volume, shuffle, repeat_all,
                    eq_enabled, eq_preset, agc_enabled, crossfade_enabled, crossfade_duration,
                    is_active
             FROM encoder_configs ORDER BY slot_id'
        )->fetchAll();
    } catch (Exception $e) {}

    try {
        $streaming_servers = mc1_db('mcaster1_encoder')->query(
            'SELECT id, name, server_type, host, port, mount_point, stat_username,
                    ssl_enabled, is_active, display_order,
                    last_checked, last_status, last_listeners
             FROM streaming_servers
             ORDER BY display_order ASC, name ASC'
        )->fetchAll();
    } catch (Exception $e) {}
}

require_once __DIR__ . '/app/inc/header.php';
?>

<style>
<?= mc1_icons_css() ?>

/* ── Streaming server status dot ── */
.srv-dot { display:inline-block; width:8px; height:8px; border-radius:50%; margin-right:5px; flex-shrink:0; }
.srv-dot.online  { background:#22c55e; box-shadow:0 0 6px rgba(34,197,94,.5); }
.srv-dot.offline { background:#ef4444; }
.srv-dot.unknown { background:#64748b; }
</style>

<div class="sec-hdr">
  <div class="sec-title"><i class="fa-solid fa-gear" style="color:var(--teal);margin-right:8px"></i>Settings &amp; Administration</div>
</div>

<!-- ── Top info cards ── -->
<div class="card-grid grid-2">

  <!-- Admin Server -->
  <div class="card">
    <div class="card-hdr"><div class="card-title"><i class="fa-solid fa-server fa-fw"></i> Admin Server</div></div>
    <div id="srv-info"><div class="empty"><div class="spinner"></div></div></div>
  </div>

  <!-- SSL Certificate -->
  <div class="card">
    <div class="card-hdr"><div class="card-title"><i class="fa-solid fa-shield-halved fa-fw"></i> SSL Certificate</div></div>
    <?php if (!empty($cert_info)): ?>
    <table style="width:100%;border-collapse:collapse">
      <?php
      $cert_rows = [
        ['Common Name', '<span class="td-mono">'.h($cert_info['cn']).'</span>'],
        ['Issuer',      h($cert_info['issuer'])],
        ['Valid From',  h($cert_info['valid_from'])],
        ['Expires',     '<span class="badge '.($cert_info['expires_in']<30?'badge-red':'badge-green').'">'.h($cert_info['valid_to']).' ('.h($cert_info['expires_in']).' days)</span>'],
      ];
      foreach ($cert_rows as $row): ?>
      <tr>
        <td style="padding:7px 0;color:var(--muted);font-size:11px;width:110px;font-weight:700;text-transform:uppercase;letter-spacing:.04em"><?= $row[0] ?></td>
        <td style="padding:7px 0;font-size:13px;color:var(--text)"><?= $row[1] ?></td>
      </tr>
      <?php endforeach; ?>
    </table>
    <?php else: ?>
    <div class="alert alert-warn"><i class="fa-solid fa-triangle-exclamation"></i> Certificate not readable: <?= h($cert_file) ?></div>
    <?php endif; ?>
  </div>

  <!-- API Token -->
  <div class="card">
    <div class="card-hdr"><div class="card-title"><i class="fa-solid fa-key fa-fw"></i> API Token</div></div>
    <div class="alert alert-info" style="margin-bottom:12px">
      <i class="fa-solid fa-circle-info"></i>
      Use <code>X-API-Token: &lt;token&gt;</code> header for script/curl access without a browser session.
      After generating, also paste this value into <code>http-admin: api-token:</code> in your YAML config and restart the encoder for C++ endpoints to accept it.
    </div>
    <div id="api-tok-wrap">
      <div id="api-tok-display" style="margin-bottom:10px"><div class="empty"><div class="spinner"></div></div></div>
      <div style="display:flex;gap:8px;flex-wrap:wrap">
        <button class="btn btn-primary btn-sm" id="api-tok-gen-btn" onclick="apiTokGenerate()"><i class="fa-solid fa-rotate"></i> Generate New Token</button>
        <button class="btn btn-secondary btn-sm" id="api-tok-copy-btn" onclick="apiTokCopy()" style="display:none"><i class="fa-solid fa-copy"></i> Copy</button>
      </div>
      <div id="api-tok-updated" style="font-size:11px;color:var(--muted);margin-top:6px"></div>
    </div>
  </div>

</div><!-- /card-grid -->

<?php if ($can_admin): ?>

<!-- ══════════════════════════════════════════════════════════════════════════
     STREAMING SERVERS
     ══════════════════════════════════════════════════════════════════════════ -->
<div class="card" style="margin-top:4px">
  <div class="card-hdr">
    <div class="card-title">
      <i class="fa-solid fa-satellite-dish fa-fw"></i> Streaming Servers
      <span class="badge badge-red" style="margin-left:6px">Admin Only</span>
    </div>
    <div style="display:flex;gap:8px;align-items:center">
      <button class="btn btn-secondary btn-sm" onclick="srvRefreshStats()"><i class="fa-solid fa-arrows-rotate"></i> Refresh Stats</button>
      <button class="btn btn-primary btn-sm" onclick="srvNew()"><i class="fa-solid fa-plus"></i> Add Server</button>
    </div>
  </div>

  <!-- Server table -->
  <?php if (empty($streaming_servers)): ?>
  <div class="empty">
    <i class="fa-solid fa-satellite-dish" style="font-size:28px;margin-bottom:10px;display:block;color:var(--border)"></i>
    <p>No streaming servers configured yet. Click <strong>Add Server</strong> to add an Icecast2, Shoutcast, Steamcast, or Mcaster1DNAS target.</p>
  </div>
  <?php else: ?>
  <div class="tbl-wrap">
    <table>
      <thead>
        <tr><th>Name</th><th>Type</th><th>Host</th><th>Mount</th><th>Status</th><th>Listeners</th><th>Checked</th><th></th></tr>
      </thead>
      <tbody id="srv-tbody">
        <?php foreach ($streaming_servers as $sidx => $srv): ?>
        <tr id="srv-row-<?= (int)$srv['id'] ?>">
          <td style="font-weight:600;color:var(--text)">
            <?= h($srv['name']) ?>
            <a href="<?= ($srv['ssl_enabled']?'https://':'http://').h($srv['host'].':'.$srv['port']) ?>"
               target="_blank" rel="noopener noreferrer"
               title="Open <?= h($srv['host'].':'.$srv['port']) ?>"
               style="color:var(--muted);margin-left:5px;font-size:11px;text-decoration:none">
              <i class="fa-solid fa-arrow-up-right-from-square"></i>
            </a>
          </td>
          <td><?= mc1_server_badge($srv['server_type']) ?></td>
          <td class="td-mono" style="font-size:11px">
            <?= h($srv['host'] . ':' . $srv['port']) ?>
          </td>
          <td class="td-mono" style="font-size:11px"><?= h($srv['mount_point'] ?: '—') ?></td>
          <td id="srv-status-<?= (int)$srv['id'] ?>">
            <?php
            $st = $srv['last_status'] ?: 'unknown';
            echo '<span style="display:inline-flex;align-items:center">'
               . '<span class="srv-dot ' . h($st) . '"></span>'
               . '<span class="badge ' . ($st === 'online' ? 'badge-green' : ($st === 'offline' ? 'badge-red' : 'badge-gray')) . '">'
               . h(ucfirst($st)) . '</span></span>';
            ?>
          </td>
          <td id="srv-listeners-<?= (int)$srv['id'] ?>" style="font-weight:600;color:var(--teal)"><?= (int)$srv['last_listeners'] ?></td>
          <td style="font-size:11px;color:var(--muted)">
            <?= $srv['last_checked'] ? h(date('m/d H:i', strtotime($srv['last_checked']))) : '—' ?>
          </td>
          <td class="td-acts">
            <button class="btn btn-secondary btn-xs" onclick="srvEdit(MC1_SERVERS[<?= $sidx ?>])" title="Edit"><i class="fa-solid fa-pen"></i></button>
            <button class="btn btn-secondary btn-xs" id="srv-statsb-<?= (int)$srv['id'] ?>" onclick="srvStats(<?= (int)$srv['id'] ?>,<?= h(json_encode($srv['name'])) ?>)" title="Detailed server stats"><i class="fa-solid fa-chart-bar"></i></button>
            <button class="btn btn-danger btn-xs" onclick="srvDel(<?= (int)$srv['id'] ?>,<?= h(json_encode($srv['name'])) ?>)" title="Delete"><i class="fa-solid fa-trash"></i></button>
          </td>
        </tr>
        <?php endforeach; ?>
      </tbody>
    </table>
  </div>
  <?php endif; ?>
</div>

<!-- ══════════════════════════════════════════════════════════════════════════
     ENCODER SLOT CONFIGURATIONS
     ══════════════════════════════════════════════════════════════════════════ -->
<div class="card" style="margin-top:4px">
  <div class="card-hdr">
    <div class="card-title"><i class="fa-solid fa-tower-broadcast fa-fw"></i> Encoder Slot Configurations <span class="badge badge-red" style="margin-left:6px">Admin Only</span></div>
    <button class="btn btn-primary btn-sm" onclick="cfgNew()"><i class="fa-solid fa-plus"></i> Add Slot</button>
  </div>

  <!-- Inline add/edit form (hidden by default) -->
  <div id="cfg-form" style="display:none;background:rgba(255,255,255,.03);border:1px solid var(--border);border-radius:var(--radius-sm);padding:18px;margin-bottom:16px">
    <input type="hidden" id="cf-id" value="0">
    <div id="cf-form-title" style="font-size:12px;font-weight:700;color:var(--teal);margin-bottom:14px;text-transform:uppercase;letter-spacing:.06em">New Encoder Slot</div>
    <div class="form-row form-row-2">
      <div class="form-group">
        <label class="form-label">Slot ID <span style="color:var(--red)">*</span></label>
        <input class="form-input" id="cf-slot" type="number" min="1" max="99" placeholder="1">
        <span class="form-hint">Unique slot number (1–99)</span>
      </div>
      <div class="form-group">
        <label class="form-label">Display Name <span style="color:var(--red)">*</span></label>
        <input class="form-input" id="cf-name" placeholder="Classic Rock 128k">
      </div>
      <div class="form-group">
        <label class="form-label">Input Type</label>
        <select class="form-select" id="cf-input">
          <option value="playlist">Playlist File</option>
          <option value="device">Audio Device</option>
          <option value="url">Stream URL</option>
        </select>
      </div>
      <div class="form-group">
        <label class="form-label">Playlist / Source Path</label>
        <input class="form-input" id="cf-playlist" placeholder="/var/www/mcaster1.com/audio/playlists/all.m3u">
      </div>
      <div class="form-group">
        <label class="form-label">Codec</label>
        <select class="form-select" id="cf-codec">
          <option value="mp3">MP3</option>
          <option value="opus">Opus</option>
          <option value="vorbis">Ogg Vorbis</option>
          <option value="aac_lc">AAC-LC</option>
          <option value="aac_he">HE-AAC v1 (AAC+)</option>
          <option value="aac_hev2">HE-AAC v2 (AAC++)</option>
          <option value="aac_eld">AAC-ELD</option>
          <option value="flac">FLAC (lossless)</option>
        </select>
      </div>
      <div class="form-group">
        <label class="form-label">Bitrate (kbps)</label>
        <input class="form-input" id="cf-br" type="number" min="32" max="320" value="128">
      </div>
      <div class="form-group">
        <label class="form-label">Sample Rate</label>
        <select class="form-select" id="cf-sr">
          <option value="44100">44100 Hz</option>
          <option value="48000">48000 Hz</option>
          <option value="22050">22050 Hz</option>
        </select>
      </div>
      <div class="form-group">
        <label class="form-label">Channels</label>
        <select class="form-select" id="cf-ch">
          <option value="2">Stereo (2)</option>
          <option value="1">Mono (1)</option>
        </select>
      </div>
      <div class="form-group">
        <label class="form-label">Server Host <span style="color:var(--red)">*</span></label>
        <input class="form-input" id="cf-host" placeholder="dnas.mcaster1.com">
      </div>
      <div class="form-group">
        <label class="form-label">Server Port</label>
        <input class="form-input" id="cf-port" type="number" min="1" max="65535" value="8000">
      </div>
      <div class="form-group">
        <label class="form-label">Mount Point</label>
        <input class="form-input" id="cf-mount" placeholder="/live">
      </div>
      <div class="form-group">
        <label class="form-label">Username</label>
        <input class="form-input" id="cf-user" value="source">
      </div>
      <div class="form-group">
        <label class="form-label">Protocol</label>
        <select class="form-select" id="cf-proto">
          <option value="icecast2">Icecast2</option>
          <option value="shoutcast1">Shoutcast v1</option>
          <option value="shoutcast2">Shoutcast v2</option>
          <option value="mcaster1">Mcaster1DNAS</option>
        </select>
      </div>
      <div class="form-group">
        <label class="form-label">Server Password</label>
        <input type="password" class="form-input" id="cf-pass" placeholder="Leave blank to keep existing">
      </div>
      <div class="form-group">
        <label class="form-label">Station Name</label>
        <input class="form-input" id="cf-sname" placeholder="My Radio Station">
      </div>
      <div class="form-group">
        <label class="form-label">Genre</label>
        <input class="form-input" id="cf-genre" placeholder="Rock">
      </div>
      <div class="form-group">
        <label class="form-label">EQ Preset</label>
        <select class="form-select" id="cf-eq-preset">
          <option value="flat">Flat (No EQ)</option>
          <option value="classic_rock">Classic Rock</option>
          <option value="country">Country</option>
          <option value="modern_rock">Modern Rock</option>
          <option value="broadcast">Broadcast</option>
          <option value="spoken_word">Spoken Word</option>
        </select>
      </div>
      <div class="form-group">
        <label class="form-label">Crossfade Duration (s)</label>
        <input class="form-input" id="cf-xf-dur" type="number" min="1" max="10" step="0.5" value="3">
      </div>
      <div class="form-group">
        <label class="form-label">Volume (0–200%)</label>
        <input class="form-input" id="cf-vol" type="number" min="0" max="200" value="100">
        <span class="form-hint">100% = unity gain · 200% = max boost</span>
      </div>
    </div>
    <div style="display:flex;gap:16px;align-items:center;margin-top:4px;flex-wrap:wrap">
      <label class="toggle-wrap"><label class="toggle"><input type="checkbox" id="cf-eq-en"><span class="toggle-slider"></span></label>&nbsp;EQ</label>
      <label class="toggle-wrap"><label class="toggle"><input type="checkbox" id="cf-agc"><span class="toggle-slider"></span></label>&nbsp;AGC</label>
      <label class="toggle-wrap"><label class="toggle"><input type="checkbox" id="cf-xf" checked><span class="toggle-slider"></span></label>&nbsp;Crossfade</label>
      <label class="toggle-wrap"><label class="toggle"><input type="checkbox" id="cf-shuffle" checked><span class="toggle-slider"></span></label>&nbsp;Shuffle</label>
      <label class="toggle-wrap"><label class="toggle"><input type="checkbox" id="cf-repeat" checked><span class="toggle-slider"></span></label>&nbsp;Repeat All</label>
      <label class="toggle-wrap"><label class="toggle"><input type="checkbox" id="cf-active" checked><span class="toggle-slider"></span></label>&nbsp;Active</label>
    </div>
    <div style="display:flex;gap:8px;margin-top:16px">
      <button class="btn btn-primary btn-sm" onclick="cfgSave()"><i class="fa-solid fa-check"></i> Save Configuration</button>
      <button class="btn btn-secondary btn-sm" onclick="cfgHide()">Cancel</button>
    </div>
  </div>

  <!-- Configs table -->
  <?php if (empty($enc_configs)): ?>
  <div class="empty">
    <i class="fa-solid fa-tower-broadcast" style="font-size:28px;margin-bottom:10px;display:block;color:var(--border)"></i>
    <p>No encoder slots configured yet. Click <strong>Add Slot</strong> to create one.</p>
  </div>
  <?php else: ?>
  <div class="tbl-wrap">
    <table>
      <thead>
        <tr><th>Slot</th><th>Name</th><th>Codec</th><th>Bitrate</th><th>Protocol</th><th>Server / Mount</th><th>Playlist</th><th>Status</th><th></th></tr>
      </thead>
      <tbody id="cfg-tbody">
        <?php foreach ($enc_configs as $cidx => $c): ?>
        <tr id="cfg-row-<?= (int)$c['id'] ?>">
          <td><span class="badge badge-teal"><?= (int)$c['slot_id'] ?></span></td>
          <td style="font-weight:600;color:var(--text)"><?= h($c['name']) ?></td>
          <td><?= mc1_codec_badge($c['codec'] ?? 'mp3') ?></td>
          <td><?= (int)$c['bitrate_kbps'] ?> kbps</td>
          <td><?= mc1_server_badge($c['server_protocol'] ?? 'icecast2', true) ?></td>
          <td class="td-mono" style="font-size:11px"><?= h($c['server_host'].':'.$c['server_port'].$c['server_mount']) ?></td>
          <td style="max-width:140px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;font-size:11px;color:var(--muted)" title="<?= h($c['playlist_path'] ?? '') ?>"><?= $c['playlist_path'] ? h(basename($c['playlist_path'])) : '—' ?></td>
          <td id="cfg-status-<?= (int)$c['slot_id'] ?>">
            <?= (int)$c['is_active'] ? '<span class="badge badge-green">Active</span>' : '<span class="badge badge-gray">Inactive</span>' ?>
            <span id="cfg-live-<?= (int)$c['slot_id'] ?>"></span>
          </td>
          <td class="td-acts">
            <a href="/edit_encoders.php?id=<?= (int)$c['id'] ?>" class="btn btn-secondary btn-xs" title="Edit"><i class="fa-solid fa-pen"></i></a>
            <button class="btn btn-danger btn-xs" onclick="cfgDel(<?= (int)$c['id'] ?>,<?= h(json_encode($c['name'])) ?>)" title="Delete"><i class="fa-solid fa-trash"></i></button>
          </td>
        </tr>
        <?php endforeach; ?>
      </tbody>
    </table>
  </div>
  <?php endif; ?>
</div>

<!-- ══════════════════════════════════════════════════════════════════════════
     DATABASE ADMINISTRATION
     ══════════════════════════════════════════════════════════════════════════ -->
<div class="card" style="margin-top:4px">
  <div class="card-hdr">
    <div class="card-title"><i class="fa-solid fa-database fa-fw"></i> Database Administration <span class="badge badge-red" style="margin-left:6px">Admin Only</span></div>
  </div>

  <!-- DB size stat cards -->
  <?php if (!empty($db_sizes)): ?>
  <div class="card-grid grid-3" style="margin-bottom:16px">
    <?php foreach ($db_sizes as $ds): ?>
    <div class="stat-card">
      <div class="stat-icon cyan"><i class="fa-solid fa-database fa-fw"></i></div>
      <div class="stat-label"><?= h($ds['db_name']) ?></div>
      <div class="stat-value"><?= $ds['size_mb'] ?> <span style="font-size:14px;color:var(--muted)">MB</span></div>
      <div class="stat-sub"><?= $ds['table_count'] ?> tables</div>
    </div>
    <?php endforeach; ?>
  </div>
  <?php endif; ?>

  <!-- Tab bar — we use data-tab so footer.php auto-init does NOT conflict (db tabs use custom dbTab()) -->
  <div style="display:flex;gap:0;border-bottom:1px solid var(--border);margin-bottom:16px">
    <button class="tab-btn active" id="dbtab-btn-users"  onclick="dbTab('users',this)">Users</button>
    <button class="tab-btn"        id="dbtab-btn-roles"  onclick="dbTab('roles',this)">Roles</button>
    <button class="tab-btn"        id="dbtab-btn-tables" onclick="dbTab('tables',this)">Tables</button>
  </div>

  <!-- Users tab -->
  <div id="dbtab-users">
    <div style="display:flex;justify-content:flex-end;margin-bottom:10px">
      <button class="btn btn-primary btn-sm" onclick="toggleAddUser()"><i class="fa-solid fa-plus"></i> Add User</button>
    </div>
    <div id="add-user-wrap" style="display:none;background:rgba(255,255,255,.03);border:1px solid var(--border);border-radius:var(--radius-sm);padding:16px;margin-bottom:14px">
      <div class="form-row form-row-2">
        <div class="form-group"><label class="form-label">Username</label><input class="form-input" id="au-user" placeholder="username"></div>
        <div class="form-group"><label class="form-label">Display Name</label><input class="form-input" id="au-name" placeholder="Full Name"></div>
        <div class="form-group"><label class="form-label">Email</label><input class="form-input" id="au-email" placeholder="email@example.com"></div>
        <div class="form-group"><label class="form-label">Password</label><input type="password" class="form-input" id="au-pass" placeholder="Min 6 characters"></div>
        <div class="form-group"><label class="form-label">Role</label>
          <select class="form-select" id="au-role">
            <?php foreach ($db_roles as $r): ?>
            <option value="<?= (int)$r['id'] ?>"><?= h($r['label']) ?></option>
            <?php endforeach; ?>
          </select>
        </div>
      </div>
      <div style="display:flex;gap:8px;margin-top:4px">
        <button class="btn btn-primary btn-sm" onclick="addUser()"><i class="fa-solid fa-check"></i> Create User</button>
        <button class="btn btn-secondary btn-sm" onclick="toggleAddUser()">Cancel</button>
      </div>
    </div>

    <!-- Edit user form — only admins reach this tab so this is admin-only by gate -->
    <div id="edit-user-wrap" style="display:none;background:rgba(255,255,255,.03);border:1px solid var(--teal);border-radius:var(--radius-sm);padding:16px;margin-bottom:14px">
      <input type="hidden" id="eu-id" value="0">
      <div id="eu-form-title" style="font-size:12px;font-weight:700;color:var(--teal);margin-bottom:12px;text-transform:uppercase;letter-spacing:.06em">Edit User</div>
      <div class="form-row form-row-2">
        <div class="form-group">
          <label class="form-label">Username <span style="color:var(--red)">*</span></label>
          <input class="form-input" id="eu-user" placeholder="username" autocomplete="off">
        </div>
        <div class="form-group">
          <label class="form-label">Display Name</label>
          <input class="form-input" id="eu-name" placeholder="Full Name" autocomplete="off">
        </div>
        <div class="form-group">
          <label class="form-label">Email</label>
          <input class="form-input" id="eu-email" placeholder="email@example.com" autocomplete="off">
        </div>
        <div class="form-group">
          <label class="form-label">Role</label>
          <select class="form-select" id="eu-role">
            <?php foreach ($db_roles as $r): ?>
            <option value="<?= (int)$r['id'] ?>"><?= h($r['label']) ?></option>
            <?php endforeach; ?>
          </select>
        </div>
        <div class="form-group">
          <label class="form-label">New Password <span style="color:var(--muted);font-weight:400">(leave blank to keep current)</span></label>
          <input type="password" class="form-input" id="eu-pass" placeholder="Leave blank to keep current" autocomplete="new-password">
        </div>
        <div class="form-group">
          <label class="form-label">Confirm New Password</label>
          <input type="password" class="form-input" id="eu-pass2" placeholder="Repeat new password" autocomplete="new-password">
        </div>
      </div>
      <div style="display:flex;gap:8px;margin-top:4px">
        <button class="btn btn-primary btn-sm" onclick="updateUser()"><i class="fa-solid fa-check"></i> Save Changes</button>
        <button class="btn btn-secondary btn-sm" onclick="editUserHide()">Cancel</button>
      </div>
    </div>
    <div class="tbl-wrap">
      <table>
        <thead><tr><th>#</th><th>Username</th><th>Display Name</th><th>Email</th><th>Role</th><th>Status</th><th>Last Login</th><th></th></tr></thead>
        <tbody id="users-tbody">
          <?php foreach ($db_users as $u): ?>
          <tr id="ur-<?= $u['id'] ?>">
            <td style="color:var(--muted);font-size:11px"><?= (int)$u['id'] ?></td>
            <td style="font-weight:600;color:var(--text)"><?= h($u['username']) ?></td>
            <td><?= h($u['display_name'] ?? '') ?></td>
            <td class="td-mono" style="font-size:11px"><?= h($u['email'] ?? '') ?></td>
            <td><span class="badge badge-teal"><?= h($u['role_label']) ?></span></td>
            <td><?= $u['is_active']
              ? '<span class="badge badge-green">Active</span>'
              : '<span class="badge badge-gray">Inactive</span>' ?></td>
            <td style="font-size:11px;color:var(--muted)"><?= h($u['last_login'] ?? 'Never') ?></td>
            <td class="td-acts">
              <?php
              $eu_data = h(json_encode([
                  'id'           => (int)$u['id'],
                  'username'     => $u['username'],
                  'display_name' => $u['display_name'] ?? '',
                  'email'        => $u['email'] ?? '',
                  'role_id'      => (int)$u['role_id'],
              ]));
              ?>
              <button class="btn btn-secondary btn-xs" onclick='editUser(<?= $eu_data ?>)' title="Edit user"><i class="fa-solid fa-pen"></i></button>
              <button class="btn btn-secondary btn-xs" onclick="resetPw(<?= (int)$u['id'] ?>,<?= h(json_encode($u['username'])) ?>)" title="Reset password"><i class="fa-solid fa-key"></i></button>
              <button class="btn btn-<?= $u['is_active'] ? 'warn' : 'success' ?> btn-xs" onclick="toggleUser(<?= (int)$u['id'] ?>,<?= $u['is_active'] ? 0 : 1 ?>)" title="<?= $u['is_active'] ? 'Deactivate' : 'Activate' ?>"><i class="fa-solid fa-<?= $u['is_active'] ? 'ban' : 'check' ?>"></i></button>
              <?php if ((int)$u['id'] !== (int)($user['id'] ?? 0)): ?>
              <button class="btn btn-danger btn-xs" onclick="delUser(<?= (int)$u['id'] ?>,<?= h(json_encode($u['username'])) ?>)"><i class="fa-solid fa-trash"></i></button>
              <?php endif; ?>
            </td>
          </tr>
          <?php endforeach; ?>
        </tbody>
      </table>
    </div>
  </div>

  <!-- Roles tab -->
  <div id="dbtab-roles" style="display:none">
    <div class="tbl-wrap">
      <table>
        <thead><tr><th>Role</th><th>Label</th><th>Admin</th><th>Encoder</th><th>Playlist</th><th>Metadata</th><th>Library</th><th>Metrics</th><th>Podcast</th><th>Schedule</th></tr></thead>
        <tbody>
          <?php foreach ($db_roles as $r): ?>
          <tr>
            <td class="td-mono"><?= h($r['name']) ?></td>
            <td style="font-weight:600;color:var(--text)"><?= h($r['label']) ?></td>
            <?php foreach (['can_admin','can_encode_control','can_playlist','can_metadata','can_media_library','can_metrics','can_podcast','can_schedule'] as $cap): ?>
            <td><span class="badge <?= $r[$cap] ? 'badge-green' : 'badge-gray' ?>"><?= $r[$cap] ? 'Yes' : 'No' ?></span></td>
            <?php endforeach; ?>
          </tr>
          <?php endforeach; ?>
        </tbody>
      </table>
    </div>
  </div>

  <!-- Tables tab -->
  <div id="dbtab-tables" style="display:none">
    <div id="tbl-stats"><div class="empty"><div class="spinner"></div></div></div>
  </div>

</div>
<?php endif; // can_admin ?>

<!-- ══════════════════════════════════════════════════════════════════════
     STREAMING SERVER ADD / EDIT MODAL
     ══════════════════════════════════════════════════════════════════════ -->
<div id="srv-modal" style="display:none;position:fixed;inset:0;z-index:8100;background:rgba(0,0,0,.65);backdrop-filter:blur(4px);overflow-y:auto;padding:24px 16px">
  <div style="max-width:680px;margin:0 auto;background:var(--card);border:1px solid var(--border);border-radius:var(--radius);box-shadow:0 20px 60px rgba(0,0,0,.6)">

    <!-- Modal header -->
    <div style="display:flex;align-items:center;justify-content:space-between;padding:18px 24px;border-bottom:1px solid var(--border)">
      <div id="sf-modal-title" style="font-size:16px;font-weight:700;color:var(--text)">Add Streaming Server</div>
      <button onclick="srvModalClose()" style="background:none;border:none;color:var(--muted);font-size:20px;cursor:pointer;padding:4px 8px;line-height:1">&times;</button>
    </div>

    <!-- Modal body: form fields -->
    <div style="padding:20px 24px">
      <input type="hidden" id="sf-id" value="0">
      <div class="form-row form-row-2">
        <div class="form-group">
          <label class="form-label">Display Name <span style="color:var(--red)">*</span></label>
          <input class="form-input" id="sf-name" placeholder="My DNAS Server" autocomplete="off">
        </div>
        <div class="form-group">
          <label class="form-label">Server Type <span style="color:var(--red)">*</span></label>
          <select class="form-select" id="sf-type">
            <option value="icecast2">Icecast2</option>
            <option value="shoutcast1">Shoutcast v1</option>
            <option value="shoutcast2">Shoutcast v2</option>
            <option value="steamcast">Steamcast</option>
            <option value="mcaster1dnas">Mcaster1DNAS</option>
          </select>
        </div>
        <div class="form-group">
          <label class="form-label">Host <span style="color:var(--red)">*</span></label>
          <input class="form-input" id="sf-host" placeholder="dnas.mcaster1.com" autocomplete="off">
        </div>
        <div class="form-group">
          <label class="form-label">Port</label>
          <input class="form-input" id="sf-port" type="number" min="1" max="65535" value="9443">
        </div>
        <div class="form-group">
          <label class="form-label">Mount Point</label>
          <input class="form-input" id="sf-mount" placeholder="/live (optional)" autocomplete="off">
        </div>
        <div class="form-group">
          <label class="form-label">Display Order</label>
          <input class="form-input" id="sf-order" type="number" min="0" max="255" value="0">
          <span class="form-hint">Lower numbers appear first</span>
        </div>
        <div class="form-group">
          <label class="form-label">Stats Username</label>
          <input class="form-input" id="sf-uname" placeholder="admin" autocomplete="off">
          <span class="form-hint">Leave blank if not required</span>
        </div>
        <div class="form-group">
          <label class="form-label">Stats Password</label>
          <input type="password" class="form-input" id="sf-pass" placeholder="Leave blank to keep existing" autocomplete="new-password">
        </div>
      </div>
      <div style="display:flex;gap:20px;align-items:center;margin-top:12px">
        <label class="toggle-wrap"><label class="toggle"><input type="checkbox" id="sf-ssl"><span class="toggle-slider"></span></label>&nbsp;SSL / HTTPS</label>
        <label class="toggle-wrap"><label class="toggle"><input type="checkbox" id="sf-active" checked><span class="toggle-slider"></span></label>&nbsp;Active</label>
      </div>
      <div id="sf-test-result" style="margin-top:12px;display:none"></div>
    </div>

    <!-- Modal footer -->
    <div style="padding:14px 24px;border-top:1px solid var(--border);display:flex;gap:8px;align-items:center;flex-wrap:wrap">
      <button class="btn btn-primary btn-sm" onclick="srvSave()"><i class="fa-solid fa-check"></i> Save Server</button>
      <button class="btn btn-secondary btn-sm" onclick="srvTestForm()"><i class="fa-solid fa-plug-circle-check"></i> Test Connection</button>
      <button class="btn btn-secondary btn-sm" onclick="srvModalClose()"><i class="fa-solid fa-xmark"></i> Cancel</button>
    </div>

  </div>
</div>

<!-- ══════════════════════════════════════════════════════════════════════
     SERVER STATS MODAL
     ══════════════════════════════════════════════════════════════════════ -->
<div id="srv-stats-modal" style="display:none;position:fixed;inset:0;z-index:8000;background:rgba(0,0,0,.65);backdrop-filter:blur(4px);overflow-y:auto;padding:24px 16px">
  <div style="max-width:1000px;margin:0 auto;background:var(--card);border:1px solid var(--border);border-radius:var(--radius);box-shadow:0 20px 60px rgba(0,0,0,.6)">

    <!-- Modal header -->
    <div style="display:flex;align-items:center;justify-content:space-between;padding:18px 24px;border-bottom:1px solid var(--border)">
      <div>
        <div id="sm-title" style="font-size:16px;font-weight:700;color:var(--text)">Server Stats</div>
        <div id="sm-subtitle" style="font-size:11px;color:var(--muted);margin-top:2px"></div>
      </div>
      <button onclick="srvStatsClose()" style="background:none;border:none;color:var(--muted);font-size:20px;cursor:pointer;padding:4px 8px;line-height:1">&times;</button>
    </div>

    <!-- Modal body -->
    <div id="sm-body" style="padding:20px 24px">
      <div class="empty"><div class="spinner"></div> Fetching server stats…</div>
    </div>

    <!-- Modal footer -->
    <div style="padding:14px 24px;border-top:1px solid var(--border);display:flex;align-items:center;justify-content:space-between">
      <div id="sm-footer-note" style="font-size:11px;color:var(--muted)"></div>
      <button class="btn btn-secondary btn-sm" onclick="srvStatsClose()"><i class="fa-solid fa-xmark"></i> Close</button>
    </div>
  </div>
</div>

<script>
(function(){

function esc(s){ return String(s||'').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;'); }
function kv(k,v){ return '<tr><td style="padding:6px 0;color:var(--muted);font-size:11px;font-weight:700;text-transform:uppercase;letter-spacing:.04em;width:110px">'+k+'</td><td style="padding:6px 0;font-size:13px;color:var(--text)">'+v+'</td></tr>'; }

/* ── All startup API calls go inside DOMContentLoaded so mc1Api is defined ── */
document.addEventListener('DOMContentLoaded', function() {

  /* Admin server status card */
  mc1Api('GET','/api/v1/status').then(function(d){
    var el = document.getElementById('srv-info');
    el.innerHTML = '<table style="width:100%;border-collapse:collapse">'
      + kv('Version','<span class="td-mono">'+(d.version||'—')+'</span>')
      + kv('Platform', d.platform||'linux')
      + kv('Uptime','<span class="badge badge-teal">'+(d.uptime||'—')+'</span>')
      + kv('ICY', d.icy_version||'2.2')
      + kv('Slots Live','<span class="badge '+(d.encoders_connected>0?'badge-green':'badge-gray')+'">'+(d.encoders_connected||0)+'/'+(d.encoders_total||0)+'</span>')
      + '</table>';
  }).catch(function(){
    var el = document.getElementById('srv-info');
    if (el) el.innerHTML = '<div class="alert alert-error"><i class="fa-solid fa-xmark"></i> Could not reach admin server API</div>';
  });

  /* API Token card — we load from MySQL (authoritative store) */
  apiTokLoad();

<?php if ($can_admin): ?>
  /* ── Restore DB admin tab from mc1State ── */
  var savedDbTab = mc1State.get('settings', 'dbtab', 'users');
  if (savedDbTab && savedDbTab !== 'users') {
    var dbBtn = document.getElementById('dbtab-btn-' + savedDbTab);
    if (dbBtn) dbTab(savedDbTab, dbBtn);
  }

  /* ── Auto-refresh server stats on page load ── */
  srvRefreshStats();
  setInterval(srvRefreshStats, 30000);

  /* ── Poll live encoder slot states (Online/Offline/Reconnecting) ── */
  pollEncoderStates();
  setInterval(pollEncoderStates, 10000);
<?php endif; ?>

}); /* end DOMContentLoaded */

<?php if ($can_admin): ?>

/* ─────────────────────────────────────────────────────────────────────────
 * We define MC1_SERVERS and MC1_ENC_CONFIGS as PHP-injected JS arrays
 * for the inline edit forms.
 * ─────────────────────────────────────────────────────────────────────────*/
var MC1_SERVERS     = <?= json_encode(array_values($streaming_servers), JSON_UNESCAPED_SLASHES) ?>;
var MC1_ENC_CONFIGS = <?= json_encode(array_values($enc_configs),       JSON_UNESCAPED_SLASHES) ?>;

/* ══════════════════════════════════════════════════════════════════════════
 * STREAMING SERVERS JS
 * ══════════════════════════════════════════════════════════════════════════ */

/* We open the server modal in Add mode */
window.srvNew = function() {
  document.getElementById('sf-id').value    = '0';
  document.getElementById('sf-modal-title').textContent = 'Add Streaming Server';
  document.getElementById('sf-name').value  = '';
  document.getElementById('sf-host').value  = '';
  document.getElementById('sf-port').value  = '9443';
  document.getElementById('sf-mount').value = '';
  document.getElementById('sf-uname').value = '';
  document.getElementById('sf-pass').value  = '';
  document.getElementById('sf-order').value = '0';
  document.getElementById('sf-type').value  = 'icecast2';
  document.getElementById('sf-ssl').checked    = false;
  document.getElementById('sf-active').checked = true;
  document.getElementById('sf-test-result').style.display = 'none';
  document.getElementById('srv-modal').style.display = 'block';
  document.body.style.overflow = 'hidden';
};

/* We open the server modal in Edit mode, pre-populated with the row's data */
window.srvEdit = function(s) {
  if (!s) return;
  document.getElementById('sf-id').value    = s.id;
  document.getElementById('sf-modal-title').textContent = 'Edit Server — ' + s.name;
  document.getElementById('sf-name').value  = s.name || '';
  document.getElementById('sf-type').value  = s.server_type || 'icecast2';
  document.getElementById('sf-host').value  = s.host || '';
  document.getElementById('sf-port').value  = s.port || 9443;
  document.getElementById('sf-mount').value = s.mount_point || '';
  document.getElementById('sf-uname').value = s.stat_username || '';
  document.getElementById('sf-pass').value  = ''; /* We never pre-fill passwords */
  document.getElementById('sf-order').value = s.display_order || 0;
  document.getElementById('sf-ssl').checked    = !!s.ssl_enabled;
  document.getElementById('sf-active').checked = s.is_active !== false && s.is_active !== 0;
  document.getElementById('sf-test-result').style.display = 'none';
  document.getElementById('srv-modal').style.display = 'block';
  document.body.style.overflow = 'hidden';
};

/* We keep srvHide as an alias so any legacy callers still work */
window.srvHide = function() { srvModalClose(); };

window.srvModalClose = function() {
  document.getElementById('srv-modal').style.display = 'none';
  document.body.style.overflow = '';
};

/* ── We map codec labels to their CSS badge class ── */
var CODEC_CSS = {
  'MP3':'cbadge-mp3','Vorbis':'cbadge-vorbis','Ogg':'cbadge-vorbis',
  'Opus':'cbadge-opus','FLAC':'cbadge-flac',
  'AAC':'cbadge-aac','AAC-LC':'cbadge-aac',
  'AAC+':'cbadge-aache','AAC++':'cbadge-aachev2','AAC-ELD':'cbadge-aaceld',
};
function codecBadge(c) {
  var cls = CODEC_CSS[c] || 'cbadge-unknown';
  return '<span class="mc1-badge '+cls+'">'+esc(c||'—')+'</span>';
}

/* ── srvStats(id, name) — open the stats modal and fetch deep data ── */
window.srvStats = function(id, name) {
  var modal = document.getElementById('srv-stats-modal');
  var body  = document.getElementById('sm-body');
  var title = document.getElementById('sm-title');
  var sub   = document.getElementById('sm-subtitle');
  var note  = document.getElementById('sm-footer-note');
  title.textContent = 'Server Stats — ' + (name || 'Server #'+id);
  sub.textContent   = '';
  note.textContent  = '';
  body.innerHTML    = '<div class="empty"><div class="spinner"></div> Fetching live data from server…</div>';
  modal.style.display = 'block';
  document.body.style.overflow = 'hidden';

  mc1Api('POST', '/app/api/serverstats.php', {action:'get_stats', id:id}).then(function(d) {
    if (!d.ok) {
      body.innerHTML = '<div class="alert alert-error"><i class="fa-solid fa-xmark"></i> '
        + esc(d.error || 'Could not fetch server stats') + '</div>';
      return;
    }
    sub.textContent = d.global.server_id + ' • ' + d.global.host + ':' + (d.global.host ? '' : '');
    note.textContent = 'Fetched in ' + d.fetch_ms + 'ms • ' + d.sources.length + ' mount(s) • '
      + (d.our_mounts||[]).length + ' ours';
    body.innerHTML = renderSrvStatsHtml(d);
  }).catch(function(e) {
    body.innerHTML = '<div class="alert alert-error"><i class="fa-solid fa-xmark"></i> Request failed</div>';
  });
};

window.srvStatsClose = function() {
  document.getElementById('srv-stats-modal').style.display = 'none';
  document.body.style.overflow = '';
};

/* We close modals when clicking their backdrop directly, or on Escape */
document.addEventListener('click', function(e) {
  if (e.target === document.getElementById('srv-stats-modal')) srvStatsClose();
  if (e.target === document.getElementById('srv-modal'))       srvModalClose();
});
document.addEventListener('keydown', function(e) {
  if (e.key === 'Escape') { srvStatsClose(); srvModalClose(); }
});

/* ── renderSrvStatsHtml(d) — We build the modal body HTML from API response ── */
function renderSrvStatsHtml(d) {
  var g = d.global;
  var html = '';

  /* ── Global health card ── */
  html += '<div style="display:grid;grid-template-columns:repeat(auto-fill,minmax(140px,1fr));gap:10px;margin-bottom:20px">';
  html += statPill('Listeners',    g._listeners + ' / ' + g._max_list, g._listeners>0?'teal':'gray');
  html += statPill('Sources',      g._sources + ' active',             g._sources>0?'green':'gray');
  html += statPill('Outgoing',     g._out_kbps + ' kbps',             g._out_kbps>0?'teal':'gray');
  html += statPill('Total Sent',   g._out_mb + ' MB',                 'gray');
  html += statPill('Total Read',   g._in_mb + ' MB',                  'gray');
  html += statPill('Uptime',       g._uptime_str,                     'green');
  html += statPill('Build',        esc(g.build||'—'),                 'gray');
  html += statPill('Fetch',        g._fetch_ms + ' ms',               g._fetch_ms<100?'green':'warn');
  html += '</div>';

  /* ── Server info row ── */
  html += '<div style="font-size:11px;color:var(--muted);margin-bottom:16px;padding:8px 12px;background:rgba(255,255,255,.03);border-radius:6px;border:1px solid var(--border)">'
    + '<span style="margin-right:16px"><b>Server:</b> '+esc(g.server_id||'—')+'</span>'
    + '<span style="margin-right:16px"><b>Host:</b> '+esc(g.host||'—')+'</span>'
    + '<span style="margin-right:16px"><b>Started:</b> '+esc(g.server_start||'—')+'</span>'
    + '<span><b>Banned IPs:</b> '+esc(g.banned_IPs||'0')+'</span>'
    + '</div>';

  /* ── Per-mount table ── */
  var ours    = d.sources.filter(function(s){ return s._ours; });
  var others  = d.sources.filter(function(s){ return !s._ours; });
  var ordered = ours.concat(others);

  html += '<div style="font-size:12px;font-weight:700;color:var(--teal);margin-bottom:8px;text-transform:uppercase;letter-spacing:.06em">'
    + d.sources.length + ' Mounts';
  if (ours.length) html += ' <span style="color:var(--muted);font-weight:400">(' + ours.length + ' ours, ' + others.length + ' external)</span>';
  html += '</div>';

  html += '<div class="tbl-wrap" style="max-height:400px;overflow-y:auto"><table>';
  html += '<thead><tr>'
    + '<th>Mount</th><th>Codec</th><th>Station</th>'
    + '<th>Status</th><th style="text-align:right">Lsnrs</th>'
    + '<th style="text-align:right">In kbps</th><th style="text-align:right">Out kbps</th>'
    + '<th>Uptime</th><th>Now Playing</th>'
    + '</tr></thead><tbody>';

  ordered.forEach(function(s) {
    var rowStyle = s._ours ? 'border-left:3px solid var(--teal);background:rgba(20,184,166,.04);' : '';
    var online   = s._online;
    var status   = online
      ? '<span style="display:inline-flex;align-items:center"><span class="srv-dot online"></span><span class="badge badge-green">Live</span></span>'
      : '<span style="display:inline-flex;align-items:center"><span class="srv-dot offline"></span><span class="badge badge-gray">Offline</span></span>';

    var oursBadge = s._ours
      ? '<span class="badge badge-teal" style="font-size:9px;margin-left:4px">OURS</span>' : '';

    var title = esc(s.title || '—');
    if (title.length > 42) title = title.substring(0, 42) + '…';

    var peak  = s._peak ? ' <span style="color:var(--muted);font-size:10px">pk:'+s._peak+'</span>' : '';
    var outK  = s._out_kbps > 0 ? '<span style="color:var(--teal)">'+s._out_kbps+'</span>' : '<span style="color:var(--muted)">0</span>';

    html += '<tr style="'+rowStyle+'">'
      + '<td class="td-mono" style="font-size:11px;white-space:nowrap">'+esc(s._mount)+oursBadge+'</td>'
      + '<td>'+codecBadge(s._codec)+'</td>'
      + '<td style="font-size:11px;max-width:120px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap" title="'+esc(s.server_name||'')+'">'+esc(s.server_name||'—')+'</td>'
      + '<td>'+status+'</td>'
      + '<td style="text-align:right;font-weight:600;color:var(--teal)">'+s._listeners+peak+'</td>'
      + '<td style="text-align:right;font-size:12px">'+s._in_kbps+'</td>'
      + '<td style="text-align:right;font-size:12px">'+outK+'</td>'
      + '<td style="font-size:11px;color:var(--muted);white-space:nowrap">'+esc(s._uptime)+'</td>'
      + '<td style="font-size:11px;color:var(--text);max-width:180px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap" title="'+esc(s.title||'')+'">'+title+'</td>'
      + '</tr>';
  });

  html += '</tbody></table></div>';

  /* ── Bandwidth summary below table ── */
  var totalIn  = d.sources.reduce(function(sum,s){return sum+s._in_kbps;},0);
  var totalOut = d.sources.reduce(function(sum,s){return sum+s._out_kbps;},0);
  var totalL   = d.sources.reduce(function(sum,s){return sum+s._listeners;},0);
  var ourIn    = ours.reduce(function(sum,s){return sum+s._in_kbps;},0);
  var ourOut   = ours.reduce(function(sum,s){return sum+s._out_kbps;},0);
  html += '<div style="margin-top:12px;display:flex;gap:12px;flex-wrap:wrap;font-size:12px;color:var(--muted)">';
  html += '<span><b style="color:var(--text)">All mounts:</b> '+totalIn+' kbps in / '+totalOut+' kbps out / '+totalL+' listeners</span>';
  if (ours.length) {
    html += '<span style="color:var(--teal)"><b>Ours:</b> '+ourIn+' kbps in / '+ourOut+' kbps out</span>';
  }
  html += '</div>';

  return html;
}

function statPill(label, value, color) {
  var colors = {teal:'var(--teal)',green:'#4ade80',gray:'var(--muted)',warn:'#f59e0b',red:'#f87171'};
  var c = colors[color] || 'var(--muted)';
  return '<div style="background:rgba(255,255,255,.03);border:1px solid var(--border);border-radius:8px;padding:10px 12px;min-width:0">'
    +'<div style="font-size:10px;color:var(--muted);text-transform:uppercase;letter-spacing:.05em;font-weight:700;margin-bottom:4px">'+esc(label)+'</div>'
    +'<div style="font-size:13px;font-weight:700;color:'+c+';white-space:nowrap;overflow:hidden;text-overflow:ellipsis">'+esc(String(value))+'</div>'
    +'</div>';
}

window.srvSave = function() {
  var id   = parseInt(document.getElementById('sf-id').value) || 0;
  var name = document.getElementById('sf-name').value.trim();
  var host = document.getElementById('sf-host').value.trim();
  var type = document.getElementById('sf-type').value;
  if (!name || !host) {
    mc1Toast('Name and host are required', 'warn');
    return;
  }
  var body = {
    action:        id ? 'update' : 'add',
    id:            id,
    name:          name,
    server_type:   type,
    host:          host,
    port:          parseInt(document.getElementById('sf-port').value) || 9443,
    mount_point:   document.getElementById('sf-mount').value.trim(),
    stat_username: document.getElementById('sf-uname').value.trim(),
    display_order: parseInt(document.getElementById('sf-order').value) || 0,
    ssl_enabled:   document.getElementById('sf-ssl').checked,
    is_active:     document.getElementById('sf-active').checked,
  };
  var pass = document.getElementById('sf-pass').value;
  if (pass) body.stat_password = pass;
  mc1Api('POST', '/app/api/servers.php', body).then(function(d) {
    if (d.ok !== false) {
      mc1Toast(id ? 'Server updated' : 'Server added', 'ok');
      srvModalClose();
      setTimeout(function(){ window.location.reload(); }, 600);
    } else {
      mc1Toast(d.error || 'Save failed', 'err');
    }
  }).catch(function(){ mc1Toast('Request failed', 'err'); });
};

window.srvDel = function(id, name) {
  if (!confirm('Delete streaming server "'+name+'"?\nThis only removes the DB entry — it does not disconnect any live encoders.')) return;
  mc1Api('POST', '/app/api/servers.php', {action:'delete', id:id}).then(function(d) {
    if (d.ok !== false) {
      mc1Toast('Server deleted', 'ok');
      var row = document.getElementById('srv-row-'+id);
      if (row) row.remove();
    } else {
      mc1Toast(d.error || 'Delete failed', 'err');
    }
  }).catch(function(){ mc1Toast('Request failed', 'err'); });
};

/* We test the form's current values without saving */
window.srvTestForm = function() {
  var host = document.getElementById('sf-host').value.trim();
  if (!host) { mc1Toast('Enter a host to test', 'warn'); return; }
  var res = document.getElementById('sf-test-result');
  res.style.display = 'block';
  res.innerHTML = '<div class="empty"><div class="spinner"></div> Testing connection…</div>';
  mc1Api('POST', '/app/api/servers.php', {
    action:        'test',
    server_type:   document.getElementById('sf-type').value,
    host:          host,
    port:          parseInt(document.getElementById('sf-port').value) || 9443,
    mount_point:   document.getElementById('sf-mount').value.trim(),
    stat_username: document.getElementById('sf-uname').value.trim(),
    stat_password: document.getElementById('sf-pass').value,
    ssl_enabled:   document.getElementById('sf-ssl').checked,
  }).then(function(d) {
    if (d.ok) {
      res.innerHTML = '<div class="alert alert-success"><i class="fa-solid fa-circle-check"></i> Connected — '
        + esc(String(d.listeners)) + ' listener' + (d.listeners===1?'':'s')
        + ' <span style="color:var(--muted);font-size:11px">('+ d.response_ms +'ms)</span></div>';
    } else {
      res.innerHTML = '<div class="alert alert-error"><i class="fa-solid fa-xmark"></i> '
        + esc(d.error || 'Connection failed') + '</div>';
    }
  }).catch(function(){
    res.innerHTML = '<div class="alert alert-error"><i class="fa-solid fa-xmark"></i> Request failed</div>';
  });
};

/* We test a saved server by id and update the table row status in-place */
window.srvTestById = function(id) {
  var btn = document.getElementById('srv-test-'+id);
  if (btn) { btn.disabled = true; btn.innerHTML = '<i class="fa-solid fa-spinner fa-spin"></i>'; }
  mc1Api('POST', '/app/api/servers.php', {action:'get_stats', id:id}).then(function(d) {
    if (btn) { btn.disabled = false; btn.innerHTML = '<i class="fa-solid fa-plug-circle-check"></i>'; }
    if (d.ok && d.stats && d.stats.length) {
      updateSrvRow(d.stats[0]);
    }
  }).catch(function(){
    if (btn) { btn.disabled = false; btn.innerHTML = '<i class="fa-solid fa-plug-circle-check"></i>'; }
  });
};

/* We refresh stats for all active servers and update the table in place */
window.srvRefreshStats = function() {
  mc1Api('POST', '/app/api/servers.php', {action:'get_stats'}).then(function(d) {
    if (d.ok && Array.isArray(d.stats)) {
      d.stats.forEach(updateSrvRow);
    }
  }).catch(function(){});
};

/* We update a single server row's status and listener cells */
function updateSrvRow(stat) {
  var stEl = document.getElementById('srv-status-'+stat.id);
  var liEl = document.getElementById('srv-listeners-'+stat.id);
  if (stEl) {
    var cls  = stat.status === 'online' ? 'online' : 'offline';
    var bcls = stat.status === 'online' ? 'badge-green' : 'badge-red';
    stEl.innerHTML = '<span style="display:inline-flex;align-items:center">'
      + '<span class="srv-dot '+cls+'"></span>'
      + '<span class="badge '+bcls+'">'+esc(stat.status.charAt(0).toUpperCase()+stat.status.slice(1))+'</span></span>';
  }
  if (liEl) liEl.textContent = stat.listeners || 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * ENCODER CONFIGS JS
 * ══════════════════════════════════════════════════════════════════════════ */

/* We fetch live slot states from the C++ API and update each config row's
 * status cell with an ONLINE/OFFLINE/RECONNECTING indicator badge. */
window.pollEncoderStates = function() {
  mc1Api('GET', '/api/v1/encoders').then(function(slots) {
    if (!Array.isArray(slots)) return;
    slots.forEach(function(slot) {
      var el = document.getElementById('cfg-live-' + slot.slot_id);
      if (!el) return;
      var st = (slot.state || '').toLowerCase();
      var html = '';
      if (st === 'live') {
        html = '<span class="badge badge-green" style="margin-left:4px">'
          + '<span class="srv-dot online" style="width:6px;height:6px;margin-right:3px;vertical-align:middle"></span>Online</span>';
      } else if (st === 'reconnecting') {
        html = '<span class="badge badge-warn" style="margin-left:4px;background:rgba(245,158,11,.18);color:#f59e0b">Reconnecting</span>';
      } else {
        /* idle, stopping, error, or any unexpected state — we show Offline */
        html = '<span class="badge badge-gray" style="margin-left:4px">Offline</span>';
      }
      el.innerHTML = html;
    });
  }).catch(function() {});
};

window.cfgNew = function() {
  document.getElementById('cf-id').value = '0';
  document.getElementById('cf-form-title').textContent = 'New Encoder Slot';
  ['cf-slot','cf-name','cf-playlist','cf-host','cf-sname','cf-genre','cf-pass'].forEach(function(id){
    var el = document.getElementById(id); if (el) el.value = '';
  });
  document.getElementById('cf-input').value    = 'playlist';
  document.getElementById('cf-codec').value    = 'mp3';
  document.getElementById('cf-br').value       = '128';
  document.getElementById('cf-sr').value       = '44100';
  document.getElementById('cf-ch').value       = '2';
  document.getElementById('cf-port').value     = '8000';
  document.getElementById('cf-mount').value    = '/live';
  document.getElementById('cf-user').value     = 'source';
  document.getElementById('cf-vol').value      = '100';
  document.getElementById('cf-proto').value    = 'icecast2';
  document.getElementById('cf-eq-preset').value = 'flat';
  document.getElementById('cf-xf-dur').value   = '3';
  document.getElementById('cf-eq-en').checked  = false;
  document.getElementById('cf-agc').checked    = false;
  document.getElementById('cf-xf').checked     = true;
  document.getElementById('cf-shuffle').checked = true;
  document.getElementById('cf-repeat').checked  = true;
  document.getElementById('cf-active').checked  = true;
  document.getElementById('cfg-form').style.display = 'block';
  document.getElementById('cfg-form').scrollIntoView({behavior:'smooth',block:'nearest'});
};

window.cfgEdit = function(c) {
  if (!c) return;
  document.getElementById('cf-id').value    = c.id;
  document.getElementById('cf-form-title').textContent = 'Edit Slot '+c.slot_id+' \u2014 '+c.name;
  document.getElementById('cf-slot').value  = c.slot_id;
  document.getElementById('cf-name').value  = c.name || '';
  document.getElementById('cf-input').value = c.input_type || 'playlist';
  document.getElementById('cf-playlist').value = c.playlist_path || '';
  document.getElementById('cf-codec').value = c.codec || 'mp3';
  document.getElementById('cf-br').value    = c.bitrate_kbps || 128;
  document.getElementById('cf-sr').value    = c.sample_rate  || 44100;
  document.getElementById('cf-ch').value    = c.channels     || 2;
  document.getElementById('cf-host').value  = c.server_host  || '';
  document.getElementById('cf-port').value  = c.server_port  || 8000;
  document.getElementById('cf-mount').value = c.server_mount || '/live';
  document.getElementById('cf-user').value  = c.server_user  || 'source';
  document.getElementById('cf-pass').value  = ''; /* We never pre-fill passwords */
  document.getElementById('cf-sname').value = c.station_name || '';
  document.getElementById('cf-genre').value = c.genre || '';
  document.getElementById('cf-vol').value   = Math.round((parseFloat(c.volume) || 1.0) * 100);
  document.getElementById('cf-proto').value = c.server_protocol || 'icecast2';
  document.getElementById('cf-eq-preset').value = c.eq_preset || 'flat';
  document.getElementById('cf-xf-dur').value    = parseFloat(c.crossfade_duration) || 3;
  document.getElementById('cf-eq-en').checked   = !!parseInt(c.eq_enabled);
  document.getElementById('cf-agc').checked     = !!parseInt(c.agc_enabled);
  document.getElementById('cf-xf').checked      = !!parseInt(c.crossfade_enabled);
  document.getElementById('cf-shuffle').checked = !!parseInt(c.shuffle);
  document.getElementById('cf-repeat').checked  = !!parseInt(c.repeat_all);
  document.getElementById('cf-active').checked  = !!parseInt(c.is_active);
  document.getElementById('cfg-form').style.display = 'block';
  document.getElementById('cfg-form').scrollIntoView({behavior:'smooth',block:'nearest'});
};

window.cfgHide = function() {
  document.getElementById('cfg-form').style.display = 'none';
};

window.cfgSave = function() {
  var id   = parseInt(document.getElementById('cf-id').value) || 0;
  var slot = parseInt(document.getElementById('cf-slot').value) || 0;
  var name = document.getElementById('cf-name').value.trim();
  var host = document.getElementById('cf-host').value.trim();
  if (!slot || !name || !host) {
    mc1Toast('Slot ID, name, and server host are required', 'warn');
    return;
  }
  var body = {
    action:              'save_config',
    id:                  id,
    slot_id:             slot,
    name:                name,
    input_type:          document.getElementById('cf-input').value,
    playlist_path:       document.getElementById('cf-playlist').value.trim(),
    codec:               document.getElementById('cf-codec').value,
    bitrate_kbps:        parseInt(document.getElementById('cf-br').value)    || 128,
    sample_rate:         parseInt(document.getElementById('cf-sr').value)    || 44100,
    channels:            parseInt(document.getElementById('cf-ch').value)    || 2,
    server_host:         host,
    server_port:         parseInt(document.getElementById('cf-port').value)  || 8000,
    server_mount:        document.getElementById('cf-mount').value.trim()    || '/live',
    server_user:         document.getElementById('cf-user').value.trim()     || 'source',
    station_name:        document.getElementById('cf-sname').value.trim(),
    genre:               document.getElementById('cf-genre').value.trim(),
    volume:              (parseInt(document.getElementById('cf-vol').value) || 100) / 100,
    server_protocol:     document.getElementById('cf-proto').value,
    eq_preset:           document.getElementById('cf-eq-preset').value,
    crossfade_duration:  parseFloat(document.getElementById('cf-xf-dur').value) || 3,
    eq_enabled:          document.getElementById('cf-eq-en').checked ? 1 : 0,
    agc_enabled:         document.getElementById('cf-agc').checked   ? 1 : 0,
    crossfade_enabled:   document.getElementById('cf-xf').checked    ? 1 : 0,
    shuffle:             document.getElementById('cf-shuffle').checked ? 1 : 0,
    repeat_all:          document.getElementById('cf-repeat').checked  ? 1 : 0,
    is_active:           document.getElementById('cf-active').checked  ? 1 : 0,
  };
  var pass = document.getElementById('cf-pass').value;
  if (pass) body.server_pass = pass;
  mc1Api('POST', '/app/api/encoders.php', body).then(function(d) {
    if (d.ok !== false) {
      mc1Toast(id ? 'Encoder config updated' : 'Encoder slot created', 'ok');
      cfgHide();
      setTimeout(function(){ window.location.reload(); }, 700);
    } else {
      mc1Toast(d.error || 'Save failed', 'err');
    }
  }).catch(function(){ mc1Toast('Request failed', 'err'); });
};

window.cfgDel = function(id, name) {
  if (!confirm('Delete encoder config "'+name+'"?\nThis cannot be undone.')) return;
  mc1Api('POST', '/app/api/encoders.php', {action:'delete_config', id:id}).then(function(d) {
    if (d.ok !== false) {
      mc1Toast('Config deleted', 'ok');
      var row = document.getElementById('cfg-row-'+id);
      if (row) row.remove();
    } else {
      mc1Toast(d.error || 'Delete failed', 'err');
    }
  }).catch(function(){ mc1Toast('Request failed', 'err'); });
};

/* ══════════════════════════════════════════════════════════════════════════
 * DB ADMIN TABS JS — We save active tab to mc1State for persistence
 * ══════════════════════════════════════════════════════════════════════════ */

window.dbTab = function(tab, btn) {
  ['users','roles','tables'].forEach(function(t) {
    var p = document.getElementById('dbtab-'+t);
    if (p) p.style.display = (t === tab ? 'block' : 'none');
  });
  document.querySelectorAll('[id^="dbtab-btn-"]').forEach(function(b){ b.classList.remove('active'); });
  if (btn) btn.classList.add('active');
  /* We persist the active tab across page navigations */
  mc1State.set('settings', 'dbtab', tab);
  if (tab === 'tables') loadTblStats();
};

window.toggleAddUser = function() {
  var w = document.getElementById('add-user-wrap');
  w.style.display = w.style.display === 'none' ? 'block' : 'none';
};

window.addUser = function() {
  var u = document.getElementById('au-user').value.trim();
  var p = document.getElementById('au-pass').value;
  if (!u || !p || p.length < 6) { mc1Toast('Username and password (min 6) required','warn'); return; }
  mc1Api('POST', '/app/api/encoders.php', {
    action:       'add_user',
    username:     u,
    password:     p,
    display_name: document.getElementById('au-name').value.trim(),
    email:        document.getElementById('au-email').value.trim(),
    role_id:      parseInt(document.getElementById('au-role').value),
  }).then(function(d) {
    if (d.ok !== false) { mc1Toast('User created','ok'); toggleAddUser(); window.location.reload(); }
    else mc1Toast(d.error || 'Error','err');
  });
};

window.delUser = function(id, name) {
  if (!confirm('Delete user '+name+'?')) return;
  mc1Api('POST', '/app/api/encoders.php', {action:'delete_user', id:id}).then(function(d) {
    if (d.ok !== false) { mc1Toast('Deleted','ok'); var r=document.getElementById('ur-'+id); if(r)r.remove(); }
    else mc1Toast(d.error || 'Error','err');
  });
};

window.toggleUser = function(id, active) {
  mc1Api('POST', '/app/api/encoders.php', {action:'toggle_user', id:id, is_active:active}).then(function(d) {
    if (d.ok !== false) { mc1Toast(active?'Activated':'Deactivated','ok'); window.location.reload(); }
    else mc1Toast(d.error || 'Error','err');
  });
};

window.resetPw = function(id, name) {
  var pw = prompt('New password for '+name+' (min 6 chars):');
  if (pw === null) return;
  if (!pw || pw.length < 6) { mc1Toast('Password too short','warn'); return; }
  mc1Api('POST', '/app/api/encoders.php', {action:'reset_password', id:id, password:pw}).then(function(d) {
    if (d.ok !== false) mc1Toast('Password updated','ok');
    else mc1Toast(d.error || 'Error','err');
  });
};

/* ── Edit user: populate the edit form with the row's current values ── */
window.editUser = function(u) {
  if (!u) return;
  document.getElementById('eu-id').value    = u.id;
  document.getElementById('eu-form-title').textContent = 'Edit User — ' + u.username;
  document.getElementById('eu-user').value  = u.username     || '';
  document.getElementById('eu-name').value  = u.display_name || '';
  document.getElementById('eu-email').value = u.email        || '';
  document.getElementById('eu-role').value  = u.role_id      || 2;
  document.getElementById('eu-pass').value  = '';
  document.getElementById('eu-pass2').value = '';
  /* We close the add-user form if it is open */
  document.getElementById('add-user-wrap').style.display = 'none';
  var w = document.getElementById('edit-user-wrap');
  w.style.display = 'block';
  w.scrollIntoView({behavior: 'smooth', block: 'nearest'});
};

window.editUserHide = function() {
  document.getElementById('edit-user-wrap').style.display = 'none';
};

window.updateUser = function() {
  var id    = parseInt(document.getElementById('eu-id').value) || 0;
  var uname = document.getElementById('eu-user').value.trim();
  var pass  = document.getElementById('eu-pass').value;
  var pass2 = document.getElementById('eu-pass2').value;
  if (!id)    { mc1Toast('No user selected', 'warn');                         return; }
  if (!uname) { mc1Toast('Username is required', 'warn');                     return; }
  if (pass && pass !== pass2) { mc1Toast('Passwords do not match', 'warn');   return; }
  if (pass && pass.length < 6){ mc1Toast('Password must be 6+ characters', 'warn'); return; }
  var body = {
    action:       'update_user',
    id:           id,
    username:     uname,
    display_name: document.getElementById('eu-name').value.trim(),
    email:        document.getElementById('eu-email').value.trim(),
    role_id:      parseInt(document.getElementById('eu-role').value) || 2,
  };
  if (pass) body.password = pass;
  mc1Api('POST', '/app/api/encoders.php', body).then(function(d) {
    if (d.ok !== false) {
      mc1Toast('User updated', 'ok');
      editUserHide();
      setTimeout(function(){ window.location.reload(); }, 600);
    } else {
      mc1Toast(d.error || 'Update failed', 'err');
    }
  }).catch(function(){ mc1Toast('Request failed', 'err'); });
};

function loadTblStats() {
  var el = document.getElementById('tbl-stats');
  mc1Api('POST', '/app/api/encoders.php', {action:'db_stats'}).then(function(d) {
    if (!d.ok) { el.innerHTML='<div class="alert alert-error">'+esc(d.error||'Error')+'</div>'; return; }
    var rows = (d.tables||[]).map(function(t) {
      return '<tr><td class="td-mono" style="font-size:11px">'+esc(t.table_schema)+'</td>'
        +'<td style="font-weight:600;color:var(--text)">'+esc(t.table_name)+'</td>'
        +'<td>'+esc(String(t.row_count||0))+'</td>'
        +'<td>'+esc(String(t.size_kb||0))+' KB</td></tr>';
    }).join('');
    el.innerHTML = '<div class="tbl-wrap"><table><thead><tr><th>Database</th><th>Table</th><th>Rows</th><th>Size</th></tr></thead><tbody>'+rows+'</tbody></table></div>';
  });
}

<?php endif; // can_admin ?>

/* ── API Token card helpers — available to all logged-in users ── */

var _apiTokValue = '';

window.apiTokLoad = function() {
  var disp = document.getElementById('api-tok-display');
  mc1Api('POST', '/app/api/encoders.php', {action:'get_token'}).then(function(d) {
    apiTokRender(d.token || '', d.updated_at || '');
  }).catch(function() {
    if (disp) disp.innerHTML = '<div class="alert alert-error"><i class="fa-solid fa-xmark"></i> Could not load token</div>';
  });
};

window.apiTokGenerate = function() {
  var btn = document.getElementById('api-tok-gen-btn');
  if (btn) { btn.disabled = true; btn.innerHTML = '<i class="fa-solid fa-spinner fa-spin"></i> Generating…'; }
  mc1Api('POST', '/app/api/encoders.php', {action:'generate_token'}).then(function(d) {
    if (btn) { btn.disabled = false; btn.innerHTML = '<i class="fa-solid fa-rotate"></i> Generate New Token'; }
    if (d.ok !== false) {
      apiTokRender(d.token || '', '', d.wrote_yaml, d.yaml_note, d.yaml_path);
      if (d.wrote_yaml) {
        mc1Toast('Token generated — saved to DB and YAML config. Restart encoder to apply.', 'ok');
      } else {
        mc1Toast('Token saved to database. YAML not updated: ' + (d.yaml_note || 'unknown reason'), 'warn');
      }
    } else {
      mc1Toast(d.error || 'Generate failed', 'err');
    }
  }).catch(function() {
    if (btn) { btn.disabled = false; btn.innerHTML = '<i class="fa-solid fa-rotate"></i> Generate New Token'; }
    mc1Toast('Request failed', 'err');
  });
};

function apiTokRender(token, updated_at, wrote_yaml, yaml_note, yaml_path) {
  _apiTokValue = token;
  var disp = document.getElementById('api-tok-display');
  var copy = document.getElementById('api-tok-copy-btn');
  var upd  = document.getElementById('api-tok-updated');
  if (disp) {
    disp.innerHTML = token
      ? '<div class="code-block" style="word-break:break-all;letter-spacing:.04em;font-size:12px">'+esc(token)+'</div>'
      : '<div style="color:var(--muted);font-size:13px;padding:6px 0">No token set — click <strong>Generate New Token</strong> to create one.</div>';
  }
  if (copy) copy.style.display = token ? 'inline-flex' : 'none';
  /* We show updated_at and YAML write status if this came from a generate call */
  var updText = updated_at ? 'Last generated: ' + updated_at : '';
  if (wrote_yaml === true) {
    updText += (updText ? ' · ' : '') + '<span style="color:#4ade80"><i class="fa-solid fa-circle-check"></i> Written to '
      + esc(yaml_path || 'YAML config') + ' — restart encoder to apply</span>';
  } else if (wrote_yaml === false) {
    updText += (updText ? ' · ' : '') + '<span style="color:#f59e0b"><i class="fa-solid fa-triangle-exclamation"></i> YAML not updated'
      + (yaml_note ? ': ' + esc(yaml_note) : '') + '</span>';
  }
  if (upd) upd.innerHTML = updText;
}

window.apiTokCopy = function() {
  if (!_apiTokValue) return;
  navigator.clipboard.writeText(_apiTokValue).then(function() {
    mc1Toast('Token copied to clipboard', 'ok');
  }).catch(function() {
    /* We fall back to a prompt for environments where clipboard API is unavailable */
    prompt('Copy this token:', _apiTokValue);
  });
};

})();
</script>

<?php require_once __DIR__ . '/app/inc/footer.php'; ?>
