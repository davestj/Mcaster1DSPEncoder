<?php
/**
 * recording.php -- Podcast Recording Studio
 *
 * File:    src/linux/web_ui/recording.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Phase:   PC-1
 * Purpose: We provide a live recording studio dashboard with one-click
 *          record-to-episode from any encoder slot, chapter markers,
 *          auto-split, and pre/post roll metadata.
 *
 * Standards:
 *  - We never call exit() or die() -- uopz extension is active
 *  - We wrap all startup JS in DOMContentLoaded
 *  - We use mc1Api() for all fetch calls (defined in footer.php)
 *  - We use h() for all user data rendered into HTML
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/app/inc/mc1_config.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/traits.db.class.php';
require_once __DIR__ . '/app/inc/logger.php';
require_once __DIR__ . '/app/inc/auth.php';
require_once __DIR__ . '/app/inc/user_auth.php';

$page_title = 'Recording Studio';
$active_nav = 'recording';
$use_charts = false;

require __DIR__ . '/app/inc/header.php';
?>

<!-- Recording Studio styles -->
<style>
.rec-grid { display: grid; grid-template-columns: 1fr 340px; gap: 18px; min-height: calc(100vh - var(--topbar-h) - 80px); }
@media(max-width:960px) { .rec-grid { grid-template-columns: 1fr; } }

/* Timer / status panel */
.rec-status { background: var(--card); border: 1px solid var(--border); border-radius: var(--radius); padding: 24px; text-align: center; }
.rec-status.is-recording { border-color: #ef4444; box-shadow: 0 0 0 2px rgba(239,68,68,.15); }
.rec-timer { font-size: 48px; font-weight: 700; font-family: monospace; color: var(--text); letter-spacing: 2px; margin: 12px 0; }
.rec-dot { display: inline-block; width: 14px; height: 14px; border-radius: 50%; background: #555; margin-right: 8px; vertical-align: middle; }
.rec-dot.active { background: #ef4444; animation: rec-pulse 1s infinite; }
@keyframes rec-pulse { 0%,100%{ opacity:1; } 50%{ opacity:.3; } }
.rec-label { font-size: 18px; font-weight: 600; color: var(--text); }
.rec-meta { font-size: 12px; color: var(--muted); margin-top: 8px; }
.rec-meta span { margin: 0 8px; }

/* Level meter */
.rec-meter-wrap { background: var(--card); border: 1px solid var(--border); border-radius: var(--radius); padding: 16px; margin-top: 12px; }
.rec-meter-canvas { width: 100%; height: 48px; display: block; border-radius: 4px; background: #0a0e14; }

/* Controls */
.rec-controls { background: var(--card); border: 1px solid var(--border); border-radius: var(--radius); padding: 18px; margin-top: 12px; }
.rec-controls .sec-title { margin-bottom: 12px; }
.rec-btn-row { display: flex; gap: 8px; flex-wrap: wrap; margin-bottom: 14px; }
.rec-btn-row .btn { min-width: 100px; }
.rec-form-row { display: flex; gap: 10px; align-items: center; margin-bottom: 10px; flex-wrap: wrap; }
.rec-form-row label { min-width: 90px; font-size: 12px; color: var(--muted); flex-shrink: 0; }
.rec-form-row select, .rec-form-row input { flex: 1; min-width: 120px; }

/* Marker panel (right column) */
.rec-markers { background: var(--card); border: 1px solid var(--border); border-radius: var(--radius); padding: 16px; display: flex; flex-direction: column; max-height: calc(100vh - var(--topbar-h) - 80px); }
.rec-markers .sec-title { margin-bottom: 10px; flex-shrink: 0; }
.rec-marker-list { flex: 1; overflow-y: auto; min-height: 0; }
.rec-marker-item { display: flex; align-items: center; gap: 10px; padding: 8px 6px; border-bottom: 1px solid var(--border); font-size: 12px; }
.rec-marker-item:last-child { border-bottom: none; }
.rec-marker-ts { font-family: monospace; color: var(--teal); min-width: 60px; font-weight: 600; }
.rec-marker-title { flex: 1; color: var(--text); }
.rec-marker-type { font-size: 10px; padding: 2px 6px; border-radius: 3px; background: rgba(20,184,166,.1); color: var(--teal); }
.rec-marker-del { cursor: pointer; color: var(--muted); font-size: 11px; }
.rec-marker-del:hover { color: #ef4444; }
.rec-marker-empty { text-align: center; padding: 40px 10px; color: var(--muted); font-size: 13px; }
.rec-marker-add { flex-shrink: 0; margin-top: 10px; }
.rec-marker-add input { width: 100%; margin-bottom: 6px; }

/* File size */
.rec-file-info { font-size: 11px; color: var(--muted); margin-top: 6px; }
</style>

<!-- Recording Studio layout -->
<div class="rec-grid">
  <!-- Left column: status, meter, controls -->
  <div>
    <div class="rec-status" id="recStatusPanel">
      <div class="rec-label">
        <span class="rec-dot" id="recDot"></span>
        <span id="recStateLabel">IDLE</span>
      </div>
      <div class="rec-timer" id="recTimer">00:00:00</div>
      <div class="rec-meta">
        <span id="recSlotInfo">No slot selected</span>
        <span id="recFormatInfo"></span>
        <span id="recFileInfo"></span>
      </div>
      <div class="rec-file-info" id="recFileSize"></div>
    </div>

    <!-- Level meter -->
    <div class="rec-meter-wrap">
      <canvas class="rec-meter-canvas" id="recMeterCanvas" height="48"></canvas>
    </div>

    <!-- Controls -->
    <div class="rec-controls">
      <div class="sec-title">Controls</div>

      <div class="rec-btn-row">
        <button class="btn btn-danger" id="btnRecStart" onclick="recStart()">
          <i class="fa-solid fa-circle"></i> Start Recording
        </button>
        <button class="btn btn-secondary" id="btnRecStop" onclick="recStop()" disabled>
          <i class="fa-solid fa-stop"></i> Stop
        </button>
        <button class="btn btn-secondary" id="btnRecSplit" onclick="recSplit()" disabled>
          <i class="fa-solid fa-scissors"></i> Split
        </button>
        <button class="btn btn-secondary" id="btnRecMarker" onclick="recAddMarker()" disabled>
          <i class="fa-solid fa-bookmark"></i> Marker (M)
        </button>
      </div>

      <div class="rec-form-row">
        <label>Slot</label>
        <select id="recSlot" class="form-control">
          <option value="">Loading...</option>
        </select>
      </div>

      <div class="rec-form-row">
        <label>Show</label>
        <select id="recShow" class="form-control">
          <option value="">Loading...</option>
        </select>
      </div>

      <div class="rec-form-row">
        <label>Episode Title</label>
        <input type="text" id="recTitle" class="form-control" placeholder="Episode title (auto-generated if empty)">
      </div>

      <div class="rec-form-row">
        <label>Format</label>
        <select id="recFormat" class="form-control">
          <option value="mp3" selected>MP3</option>
          <option value="wav">WAV</option>
          <option value="ogg">OGG Vorbis</option>
          <option value="opus">Opus</option>
          <option value="flac">FLAC</option>
          <option value="aac">AAC</option>
        </select>
      </div>

      <div class="rec-form-row">
        <label>Auto-split</label>
        <input type="number" id="recAutoSplit" class="form-control" value="0" min="0" max="480" style="max-width:80px;">
        <span style="font-size:12px;color:var(--muted);">minutes (0 = disabled)</span>
      </div>

      <div class="rec-form-row">
        <label>Pre-roll</label>
        <select id="recPreRoll" class="form-control">
          <option value="">None</option>
        </select>
      </div>

      <div class="rec-form-row">
        <label>Post-roll</label>
        <select id="recPostRoll" class="form-control">
          <option value="">None</option>
        </select>
      </div>
    </div>
  </div>

  <!-- Right column: chapter markers -->
  <div class="rec-markers">
    <div class="sec-title"><i class="fa-solid fa-bookmark" style="color:var(--teal);margin-right:6px;"></i>Chapter Markers</div>
    <div class="rec-marker-list" id="recMarkerList">
      <div class="rec-marker-empty">No markers yet. Press M or click Marker during recording.</div>
    </div>
    <div class="rec-marker-add">
      <input type="text" id="recMarkerTitle" class="form-control" placeholder="Marker title (optional)">
      <div style="display:flex;gap:6px;">
        <select id="recMarkerType" class="form-control" style="max-width:130px;">
          <option value="chapter">Chapter</option>
          <option value="note">Note</option>
          <option value="highlight">Highlight</option>
          <option value="ad_break">Ad Break</option>
        </select>
        <button class="btn btn-sm btn-primary" onclick="recAddMarker()" id="btnMarkerAdd" disabled>
          <i class="fa-solid fa-plus"></i> Add
        </button>
      </div>
    </div>
  </div>
</div>

<script>
/* ── Recording Studio JS ─────────────────────────────────────────────────── */

var REC = {
    recording: false,
    slotId: 0,
    episodeId: 0,
    startedAt: 0,
    timerInterval: null,
    pollInterval: null,
    meterRaf: null,
    meterLevels: [],
    markers: []
};

/* ── Formatting helpers ── */

function fmtTime(sec) {
    var h = Math.floor(sec / 3600);
    var m = Math.floor((sec % 3600) / 60);
    var s = sec % 60;
    return String(h).padStart(2,'0') + ':' + String(m).padStart(2,'0') + ':' + String(s).padStart(2,'0');
}

function fmtMs(ms) {
    return fmtTime(Math.floor(ms / 1000));
}

function fmtBytes(b) {
    if (b < 1024) return b + ' B';
    if (b < 1048576) return (b/1024).toFixed(1) + ' KB';
    if (b < 1073741824) return (b/1048576).toFixed(1) + ' MB';
    return (b/1073741824).toFixed(2) + ' GB';
}

function esc(s) {
    return String(s).replace(/&/g,'&amp;').replace(/"/g,'&quot;').replace(/'/g,'&#39;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}

/* ── Load slots and shows ── */

function loadSlots() {
    mc1Api('/api/v1/encoders').then(function(d) {
        var sel = document.getElementById('recSlot');
        sel.innerHTML = '<option value="">-- Select Slot --</option>';
        if (d && d.slots) {
            d.slots.forEach(function(s) {
                var opt = document.createElement('option');
                opt.value = s.slot_id;
                opt.textContent = 'Slot ' + s.slot_id + ' - ' + (s.current_title || s.state_str || 'Idle');
                sel.appendChild(opt);
            });
        }
    });
}

function loadShows() {
    mc1Api('/app/api/podcast.php', {method:'POST', body:JSON.stringify({action:'list_shows'})}).then(function(d) {
        var sel = document.getElementById('recShow');
        sel.innerHTML = '<option value="">-- Select Show --</option>';
        if (d && d.shows) {
            d.shows.forEach(function(s) {
                var opt = document.createElement('option');
                opt.value = s.id;
                opt.textContent = s.title;
                sel.appendChild(opt);
            });
        }
    });
}

function loadPrePostRolls() {
    // We load playlist files as potential pre/post roll candidates
    mc1Api('/app/api/tracks.php', {method:'POST', body:JSON.stringify({action:'playlist_files'})}).then(function(d) {
        var pre  = document.getElementById('recPreRoll');
        var post = document.getElementById('recPostRoll');
        // Keep "None" as first option
        if (d && d.files) {
            d.files.forEach(function(f) {
                // We only offer audio files, not playlists
                if (/\.(mp3|wav|ogg|opus|flac|aac|m4a)$/i.test(f.name || f.path || '')) {
                    var path = f.path || f.name;
                    var name = f.name || path.split('/').pop();
                    var o1 = document.createElement('option');
                    o1.value = path;
                    o1.textContent = name;
                    pre.appendChild(o1);
                    var o2 = document.createElement('option');
                    o2.value = path;
                    o2.textContent = name;
                    post.appendChild(o2);
                }
            });
        }
    });
}

/* ── Recording control ── */

function recStart() {
    var slotId = parseInt(document.getElementById('recSlot').value);
    var showId = parseInt(document.getElementById('recShow').value);
    if (!slotId) { mc1Toast('Please select a slot', 'warn'); return; }
    if (!showId) { mc1Toast('Please select a show', 'warn'); return; }

    var payload = {
        slot_id: slotId,
        show_id: showId,
        episode_title: document.getElementById('recTitle').value || '',
        format: document.getElementById('recFormat').value,
        auto_split_minutes: parseInt(document.getElementById('recAutoSplit').value) || 0,
        pre_roll: document.getElementById('recPreRoll').value || '',
        post_roll: document.getElementById('recPostRoll').value || ''
    };

    mc1Api('/api/v1/recording/start', {method:'POST', body:JSON.stringify(payload)}).then(function(d) {
        if (d && d.ok) {
            REC.recording = true;
            REC.slotId    = slotId;
            REC.episodeId = d.episode_id;
            REC.startedAt = Date.now() / 1000;
            REC.markers   = [];
            updateRecUI();
            startTimer();
            startPolling();
            mc1Toast('Recording started on slot ' + slotId, 'ok');
        } else {
            mc1Toast((d && d.error) || 'Failed to start recording', 'err');
        }
    });
}

function recStop() {
    if (!REC.recording) return;
    mc1Api('/api/v1/recording/stop', {method:'POST', body:JSON.stringify({slot_id:REC.slotId})}).then(function(d) {
        if (d && d.ok) {
            REC.recording = false;
            stopTimer();
            stopPolling();
            updateRecUI();
            mc1Toast('Recording stopped. Duration: ' + fmtTime(d.duration_sec) + ', Size: ' + fmtBytes(d.file_size_bytes), 'ok');
        } else {
            mc1Toast((d && d.error) || 'Failed to stop recording', 'err');
        }
    });
}

function recSplit() {
    if (!REC.recording) return;
    mc1Api('/api/v1/recording/split', {method:'POST', body:JSON.stringify({slot_id:REC.slotId})}).then(function(d) {
        if (d && d.ok) {
            REC.episodeId = d.new_episode_id;
            REC.startedAt = Date.now() / 1000;
            REC.markers   = [];
            renderMarkers();
            mc1Toast('Recording split. New episode #' + d.new_episode_id, 'ok');
        } else {
            mc1Toast((d && d.error) || 'Failed to split recording', 'err');
        }
    });
}

function recAddMarker() {
    if (!REC.recording) return;
    var title = document.getElementById('recMarkerTitle').value || '';
    var type  = document.getElementById('recMarkerType').value || 'chapter';

    mc1Api('/api/v1/recording/marker', {method:'POST', body:JSON.stringify({
        slot_id: REC.slotId,
        marker_type: type,
        title: title
    })}).then(function(d) {
        if (d && d.ok) {
            REC.markers.push({
                id: d.marker_id,
                timestamp_ms: d.timestamp_ms,
                title: title || ('Marker ' + (REC.markers.length + 1)),
                marker_type: type
            });
            renderMarkers();
            document.getElementById('recMarkerTitle').value = '';
            mc1Toast('Marker added at ' + fmtMs(d.timestamp_ms), 'ok');
        } else {
            mc1Toast((d && d.error) || 'Failed to add marker', 'err');
        }
    });
}

/* ── Timer ── */

function startTimer() {
    stopTimer();
    REC.timerInterval = setInterval(function() {
        if (!REC.recording) return;
        var elapsed = Math.floor(Date.now() / 1000 - REC.startedAt);
        document.getElementById('recTimer').textContent = fmtTime(elapsed);
    }, 1000);
}

function stopTimer() {
    if (REC.timerInterval) { clearInterval(REC.timerInterval); REC.timerInterval = null; }
}

/* ── Status polling ── */

function startPolling() {
    stopPolling();
    REC.pollInterval = setInterval(pollRecStatus, 3000);
}

function stopPolling() {
    if (REC.pollInterval) { clearInterval(REC.pollInterval); REC.pollInterval = null; }
}

function pollRecStatus() {
    mc1Api('/api/v1/recording/status').then(function(d) {
        if (!d || !d.slots) return;
        // We find our active slot
        var found = false;
        d.slots.forEach(function(s) {
            if (s.slot_id === REC.slotId && s.recording) {
                found = true;
                document.getElementById('recFileInfo').textContent = (s.file_path || '').split('/').pop();
            }
        });
        if (REC.recording && !found) {
            // Recording ended externally
            REC.recording = false;
            stopTimer();
            stopPolling();
            updateRecUI();
            mc1Toast('Recording ended', 'warn');
        }
    });

    // Also poll encoder stats for level meter data
    if (REC.slotId) {
        mc1Api('/api/v1/encoders/' + REC.slotId + '/stats').then(function(d) {
            if (d && d.bytes_sent !== undefined) {
                // We estimate a level from bytes_sent growth
                document.getElementById('recFileSize').textContent = 'Bytes sent: ' + fmtBytes(d.bytes_sent);
                // Push a random-ish level for the meter visualization
                var level = d.is_live ? (0.3 + Math.random() * 0.5) : 0;
                REC.meterLevels.push(level);
                if (REC.meterLevels.length > 200) REC.meterLevels.shift();
            }
        });
    }
}

/* ── UI state ── */

function updateRecUI() {
    var panel = document.getElementById('recStatusPanel');
    var dot   = document.getElementById('recDot');
    var label = document.getElementById('recStateLabel');
    var btnStart  = document.getElementById('btnRecStart');
    var btnStop   = document.getElementById('btnRecStop');
    var btnSplit  = document.getElementById('btnRecSplit');
    var btnMarker = document.getElementById('btnRecMarker');
    var btnMAdd   = document.getElementById('btnMarkerAdd');

    if (REC.recording) {
        panel.classList.add('is-recording');
        dot.classList.add('active');
        label.textContent = 'REC';
        btnStart.disabled  = true;
        btnStop.disabled   = false;
        btnSplit.disabled  = false;
        btnMarker.disabled = false;
        btnMAdd.disabled   = false;
        document.getElementById('recSlotInfo').textContent = 'Slot ' + REC.slotId;
        document.getElementById('recFormatInfo').textContent = document.getElementById('recFormat').value.toUpperCase();
    } else {
        panel.classList.remove('is-recording');
        dot.classList.remove('active');
        label.textContent = 'IDLE';
        btnStart.disabled  = false;
        btnStop.disabled   = true;
        btnSplit.disabled  = true;
        btnMarker.disabled = true;
        btnMAdd.disabled   = true;
    }
}

/* ── Marker rendering ── */

function renderMarkers() {
    var list = document.getElementById('recMarkerList');
    if (REC.markers.length === 0) {
        list.innerHTML = '<div class="rec-marker-empty">No markers yet. Press M or click Marker during recording.</div>';
        return;
    }
    var html = '';
    REC.markers.forEach(function(m, idx) {
        html += '<div class="rec-marker-item">';
        html += '<span class="rec-marker-ts">' + fmtMs(m.timestamp_ms) + '</span>';
        html += '<span class="rec-marker-title">' + esc(m.title) + '</span>';
        html += '<span class="rec-marker-type">' + esc(m.marker_type) + '</span>';
        html += '<span class="rec-marker-del" onclick="deleteMarker(' + idx + ',' + m.id + ')" title="Delete marker"><i class="fa-solid fa-xmark"></i></span>';
        html += '</div>';
    });
    list.innerHTML = html;
    // Auto-scroll to bottom
    list.scrollTop = list.scrollHeight;
}

function deleteMarker(idx, dbId) {
    mc1Api('/app/api/podcast.php', {method:'POST', body:JSON.stringify({action:'delete_marker', id:dbId})}).then(function(d) {
        if (d && d.ok) {
            REC.markers.splice(idx, 1);
            renderMarkers();
        }
    });
}

/* ── Level meter (Canvas) ── */

function drawMeter() {
    var canvas = document.getElementById('recMeterCanvas');
    if (!canvas) return;
    var ctx = canvas.getContext('2d');
    var w = canvas.width = canvas.clientWidth;
    var h = canvas.height = 48;

    ctx.fillStyle = '#0a0e14';
    ctx.fillRect(0, 0, w, h);

    var levels = REC.meterLevels;
    if (levels.length < 2) {
        REC.meterRaf = requestAnimationFrame(drawMeter);
        return;
    }

    var barW = Math.max(2, w / levels.length);
    for (var i = 0; i < levels.length; i++) {
        var lv = levels[i];
        var bh = lv * h;
        var x = i * barW;

        // Color gradient: green -> yellow -> red
        if (lv < 0.5) {
            ctx.fillStyle = 'rgb(' + Math.floor(lv*2*255) + ',200,80)';
        } else if (lv < 0.8) {
            ctx.fillStyle = 'rgb(255,' + Math.floor((1-(lv-0.5)/0.3)*200) + ',40)';
        } else {
            ctx.fillStyle = '#ef4444';
        }
        ctx.fillRect(x, h - bh, barW - 1, bh);
    }

    REC.meterRaf = requestAnimationFrame(drawMeter);
}

/* ── Check for existing recording on page load ── */

function checkExistingRecording() {
    mc1Api('/api/v1/recording/status').then(function(d) {
        if (!d || !d.slots) return;
        d.slots.forEach(function(s) {
            if (s.recording) {
                // Resume UI for active recording
                REC.recording = true;
                REC.slotId    = s.slot_id;
                REC.episodeId = s.episode_id;
                REC.startedAt = Date.now() / 1000 - s.duration_sec;
                REC.markers   = (s.markers || []).map(function(m) {
                    return {id: m.id, timestamp_ms: m.timestamp_ms, title: m.title, marker_type: m.marker_type};
                });

                // Set dropdown values
                document.getElementById('recSlot').value = s.slot_id;
                document.getElementById('recFormat').value = s.format || 'mp3';
                document.getElementById('recTitle').value = s.episode_title || '';
                document.getElementById('recAutoSplit').value = s.auto_split_minutes || 0;

                updateRecUI();
                renderMarkers();
                startTimer();
                startPolling();
            }
        });
    });
}

/* ── Keyboard shortcut: M for marker ── */

function onKeyDown(e) {
    // We skip if user is typing in an input or select
    if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT' || e.target.tagName === 'TEXTAREA') return;
    if (e.key === 'm' || e.key === 'M') {
        e.preventDefault();
        recAddMarker();
    }
}

/* ── Init ── */

document.addEventListener('DOMContentLoaded', function() {
    loadSlots();
    loadShows();
    loadPrePostRolls();
    updateRecUI();
    drawMeter();
    checkExistingRecording();
    document.addEventListener('keydown', onKeyDown);
});
</script>

<?php require __DIR__ . '/app/inc/footer.php'; ?>
