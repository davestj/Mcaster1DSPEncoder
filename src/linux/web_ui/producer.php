<?php
/*
 * producer.php — Mcaster1 Video Producer
 *
 * Browser-based video production: 3 source slots (webcam, video file,
 * media library category) with WebGL preview + program output.
 * Transition effects: cut, fade, dissolve.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/app/inc/auth.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/user_auth.php';

$page_title = 'Video Producer';
$active_nav = 'producer';
$use_charts = false;

// Load categories for the media library source
$categories = [];
try {
    $categories = mc1_db('mcaster1_media')->query(
        'SELECT id, name FROM categories ORDER BY name'
    )->fetchAll();
} catch (Exception $e) {}

// Load saved scenes for current user
$scenes = [];
$user = function_exists('mc1_current_user') ? mc1_current_user() : null;
$userId = $user ? (int)($user['id'] ?? 0) : 0;
try {
    $scenes = mc1_db('mcaster1_encoder')->query(
        'SELECT id, scene_name, active_source, transition_type, transition_duration_ms, updated_at
         FROM producer_scenes WHERE user_id = ' . $userId . ' ORDER BY updated_at DESC LIMIT 20'
    )->fetchAll();
} catch (Exception $e) {}

require_once __DIR__ . '/app/inc/header.php';
?>

<style>
/* Producer layout */
.producer-grid {
    display: grid;
    grid-template-columns: 1fr 1fr 1fr 1.5fr;
    gap: 12px;
    margin-bottom: 16px;
}
.source-slot {
    background: var(--card);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 12px;
    display: flex;
    flex-direction: column;
    gap: 8px;
}
.source-slot.live-slot {
    border-color: rgba(20,184,166,.6);
    box-shadow: 0 0 0 1px rgba(20,184,166,.2), 0 4px 16px rgba(20,184,166,.08);
}
.source-slot .slot-label {
    display: flex;
    align-items: center;
    justify-content: space-between;
    font-size: 12px;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: .05em;
    color: var(--muted);
}
.source-badge {
    display: inline-flex;
    align-items: center;
    padding: 2px 7px;
    border-radius: 8px;
    font-size: 9px;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: .04em;
}
.source-badge.live-badge {
    background: rgba(20,184,166,.2);
    color: var(--teal);
}
.source-badge.cue-badge {
    background: rgba(234,179,8,.15);
    color: var(--yellow);
}
.source-badge.off-badge {
    background: rgba(100,116,139,.15);
    color: var(--muted);
}
.preview-wrap {
    position: relative;
    background: #000;
    border-radius: var(--radius-sm);
    overflow: hidden;
    aspect-ratio: 16/9;
}
.preview-wrap canvas {
    width: 100%;
    height: 100%;
    display: block;
}
.preview-overlay {
    position: absolute;
    top: 0; left: 0; right: 0; bottom: 0;
    display: flex;
    align-items: center;
    justify-content: center;
    color: var(--muted);
    font-size: 11px;
    pointer-events: none;
    background: rgba(0,0,0,.4);
    opacity: 1;
    transition: opacity .3s;
}
.preview-overlay.hidden { opacity: 0; }
.preview-wrap:hover .take-btn {
    opacity: 1;
}
.take-btn {
    position: absolute;
    bottom: 6px;
    left: 50%;
    transform: translateX(-50%);
    opacity: 0;
    transition: opacity .2s;
    z-index: 2;
}
.source-controls {
    display: flex;
    flex-direction: column;
    gap: 6px;
}
.source-controls .form-select,
.source-controls .form-input {
    padding: 5px 8px;
    font-size: 12px;
}

/* Program output */
.program-slot {
    background: var(--card);
    border: 2px solid var(--teal);
    border-radius: var(--radius);
    padding: 12px;
    display: flex;
    flex-direction: column;
    gap: 8px;
}
.program-slot .slot-label {
    display: flex;
    align-items: center;
    justify-content: space-between;
    font-size: 13px;
    font-weight: 700;
    color: var(--teal);
}
.program-preview {
    position: relative;
    background: #000;
    border-radius: var(--radius-sm);
    overflow: hidden;
    aspect-ratio: 16/9;
}
.program-preview canvas {
    width: 100%;
    height: 100%;
    display: block;
}
.program-label {
    position: absolute;
    top: 6px;
    left: 8px;
    background: rgba(239,68,68,.85);
    color: #fff;
    font-size: 9px;
    font-weight: 800;
    padding: 2px 7px;
    border-radius: 3px;
    letter-spacing: .08em;
    text-transform: uppercase;
}

/* Control bar */
.producer-controls {
    background: var(--card);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 12px 16px;
    display: flex;
    align-items: center;
    gap: 16px;
    flex-wrap: wrap;
}
.ctrl-group {
    display: flex;
    align-items: center;
    gap: 8px;
}
.ctrl-group label {
    font-size: 11px;
    font-weight: 700;
    color: var(--text-dim);
    text-transform: uppercase;
    letter-spacing: .04em;
    white-space: nowrap;
}
.ctrl-divider {
    width: 1px;
    height: 28px;
    background: var(--border);
    flex-shrink: 0;
}
.trans-btn {
    padding: 5px 12px;
    font-size: 12px;
    font-weight: 600;
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    background: rgba(255,255,255,.05);
    color: var(--text-dim);
    cursor: pointer;
    transition: all .15s;
}
.trans-btn:hover { background: rgba(255,255,255,.1); color: var(--text); }
.trans-btn.active { background: rgba(20,184,166,.15); border-color: var(--teal); color: var(--teal); }
.rec-btn {
    padding: 6px 16px;
    border-radius: var(--radius-sm);
    font-size: 12px;
    font-weight: 700;
    border: none;
    cursor: pointer;
    display: inline-flex;
    align-items: center;
    gap: 6px;
    transition: all .15s;
}
.rec-btn.rec-off {
    background: rgba(239,68,68,.15);
    color: var(--red);
    border: 1px solid rgba(239,68,68,.3);
}
.rec-btn.rec-off:hover { background: rgba(239,68,68,.25); }
.rec-btn.rec-on {
    background: var(--red);
    color: #fff;
    animation: rec-pulse 1.2s ease-in-out infinite;
}
@keyframes rec-pulse {
    0%,100% { box-shadow: 0 0 0 0 rgba(239,68,68,.4); }
    50% { box-shadow: 0 0 0 6px rgba(239,68,68,0); }
}
.stream-btn {
    padding: 6px 16px;
    border-radius: var(--radius-sm);
    font-size: 12px;
    font-weight: 700;
    border: 1px solid rgba(20,184,166,.3);
    background: rgba(20,184,166,.12);
    color: var(--teal);
    cursor: pointer;
    display: inline-flex;
    align-items: center;
    gap: 6px;
    transition: all .15s;
}
.stream-btn:hover { background: rgba(20,184,166,.22); }
.stream-btn.streaming {
    background: var(--teal);
    color: #0f172a;
    font-weight: 800;
}

/* Scenes panel */
.scenes-panel {
    background: var(--card);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 14px 16px;
    margin-top: 12px;
}
.scenes-panel .sec-title {
    font-size: 13px;
    margin-bottom: 10px;
}
.scene-row {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 5px 0;
    border-bottom: 1px solid rgba(51,65,85,.35);
    font-size: 12px;
    color: var(--text-dim);
}
.scene-row:last-child { border-bottom: none; }
.scene-name { flex: 1; color: var(--text); font-weight: 500; }
.scene-date { font-size: 10px; color: var(--muted); }

/* File picker modal */
.file-modal-overlay {
    display: none;
    position: fixed;
    inset: 0;
    background: rgba(0,0,0,.6);
    z-index: 500;
    align-items: center;
    justify-content: center;
}
.file-modal-overlay.open { display: flex; }
.file-modal {
    background: var(--card);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 20px;
    width: 500px;
    max-width: 90vw;
    max-height: 70vh;
    overflow-y: auto;
}
.file-modal h3 {
    font-size: 15px;
    color: var(--text);
    margin-bottom: 12px;
}
.file-list-item {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 6px 8px;
    border-radius: var(--radius-sm);
    font-size: 12px;
    color: var(--text-dim);
    cursor: pointer;
    transition: background .1s;
}
.file-list-item:hover { background: rgba(255,255,255,.05); color: var(--text); }
.file-list-item i { color: var(--teal); width: 16px; text-align: center; }

@media(max-width:1100px) {
    .producer-grid { grid-template-columns: 1fr 1fr; }
    .program-slot { grid-column: 1 / -1; }
}
@media(max-width:640px) {
    .producer-grid { grid-template-columns: 1fr; }
}
</style>

<!-- Producer Grid -->
<div class="sec-hdr">
    <div class="sec-title">
        <i class="fa-solid fa-tv" style="color:var(--teal);margin-right:8px"></i>Video Production
    </div>
    <div style="display:flex;gap:8px;align-items:center">
        <select id="scene-select" class="form-select" style="width:auto;font-size:12px;padding:4px 8px">
            <option value="">-- Scene --</option>
            <?php foreach ($scenes as $sc): ?>
            <option value="<?= (int)$sc['id'] ?>"><?= h($sc['scene_name']) ?></option>
            <?php endforeach; ?>
        </select>
        <button class="btn btn-secondary btn-xs" onclick="saveScene()"><i class="fa-solid fa-floppy-disk"></i> Save</button>
    </div>
</div>

<div class="producer-grid">
    <!-- Source 1: Webcam -->
    <div class="source-slot" id="source-slot-0">
        <div class="slot-label">
            <span>Source 1</span>
            <span class="source-badge off-badge" id="badge-0">OFF</span>
        </div>
        <div class="preview-wrap">
            <canvas id="preview-0"></canvas>
            <div class="preview-overlay" id="overlay-0"><i class="fa-solid fa-video-slash"></i>&nbsp; No source</div>
            <button class="btn btn-primary btn-xs take-btn" onclick="takeSource(0)">TAKE</button>
        </div>
        <div class="source-controls">
            <select class="form-select" id="type-0" onchange="onTypeChange(0)">
                <option value="none">-- Select Type --</option>
                <option value="webcam" selected>Webcam</option>
                <option value="file">Video File</option>
                <option value="library">Media Library</option>
            </select>
            <div id="ctrl-webcam-0">
                <select class="form-select" id="camera-0" onchange="onCameraChange(0)" style="margin-bottom:4px">
                    <option value="">Detecting cameras...</option>
                </select>
                <select class="form-select" id="res-0" onchange="onResChange(0)">
                    <option value="480p">480p (SD)</option>
                    <option value="720p" selected>720p (HD)</option>
                    <option value="1080p">1080p (Full HD)</option>
                </select>
            </div>
            <div id="ctrl-file-0" style="display:none">
                <button class="btn btn-secondary btn-xs" onclick="openFilePicker(0)"><i class="fa-solid fa-folder-open"></i> Browse</button>
                <span id="file-name-0" style="font-size:11px;color:var(--text-dim);margin-left:6px"></span>
            </div>
            <div id="ctrl-library-0" style="display:none">
                <select class="form-select" id="category-0" onchange="onCategoryChange(0)">
                    <option value="">-- Category --</option>
                    <?php foreach ($categories as $cat): ?>
                    <option value="<?= (int)$cat['id'] ?>"><?= h($cat['name']) ?></option>
                    <?php endforeach; ?>
                </select>
            </div>
        </div>
    </div>

    <!-- Source 2: File -->
    <div class="source-slot" id="source-slot-1">
        <div class="slot-label">
            <span>Source 2</span>
            <span class="source-badge off-badge" id="badge-1">OFF</span>
        </div>
        <div class="preview-wrap">
            <canvas id="preview-1"></canvas>
            <div class="preview-overlay" id="overlay-1"><i class="fa-solid fa-video-slash"></i>&nbsp; No source</div>
            <button class="btn btn-primary btn-xs take-btn" onclick="takeSource(1)">TAKE</button>
        </div>
        <div class="source-controls">
            <select class="form-select" id="type-1" onchange="onTypeChange(1)">
                <option value="none">-- Select Type --</option>
                <option value="webcam">Webcam</option>
                <option value="file" selected>Video File</option>
                <option value="library">Media Library</option>
            </select>
            <div id="ctrl-webcam-1" style="display:none">
                <select class="form-select" id="camera-1" onchange="onCameraChange(1)" style="margin-bottom:4px">
                    <option value="">Detecting cameras...</option>
                </select>
                <select class="form-select" id="res-1" onchange="onResChange(1)">
                    <option value="480p">480p (SD)</option>
                    <option value="720p" selected>720p (HD)</option>
                    <option value="1080p">1080p (Full HD)</option>
                </select>
            </div>
            <div id="ctrl-file-1">
                <button class="btn btn-secondary btn-xs" onclick="openFilePicker(1)"><i class="fa-solid fa-folder-open"></i> Browse</button>
                <span id="file-name-1" style="font-size:11px;color:var(--text-dim);margin-left:6px"></span>
            </div>
            <div id="ctrl-library-1" style="display:none">
                <select class="form-select" id="category-1" onchange="onCategoryChange(1)">
                    <option value="">-- Category --</option>
                    <?php foreach ($categories as $cat): ?>
                    <option value="<?= (int)$cat['id'] ?>"><?= h($cat['name']) ?></option>
                    <?php endforeach; ?>
                </select>
            </div>
        </div>
    </div>

    <!-- Source 3: Library -->
    <div class="source-slot" id="source-slot-2">
        <div class="slot-label">
            <span>Source 3</span>
            <span class="source-badge off-badge" id="badge-2">OFF</span>
        </div>
        <div class="preview-wrap">
            <canvas id="preview-2"></canvas>
            <div class="preview-overlay" id="overlay-2"><i class="fa-solid fa-video-slash"></i>&nbsp; No source</div>
            <button class="btn btn-primary btn-xs take-btn" onclick="takeSource(2)">TAKE</button>
        </div>
        <div class="source-controls">
            <select class="form-select" id="type-2" onchange="onTypeChange(2)">
                <option value="none">-- Select Type --</option>
                <option value="webcam">Webcam</option>
                <option value="file">Video File</option>
                <option value="library" selected>Media Library</option>
            </select>
            <div id="ctrl-webcam-2" style="display:none">
                <select class="form-select" id="camera-2" onchange="onCameraChange(2)" style="margin-bottom:4px">
                    <option value="">Detecting cameras...</option>
                </select>
                <select class="form-select" id="res-2" onchange="onResChange(2)">
                    <option value="480p">480p (SD)</option>
                    <option value="720p" selected>720p (HD)</option>
                    <option value="1080p">1080p (Full HD)</option>
                </select>
            </div>
            <div id="ctrl-file-2" style="display:none">
                <button class="btn btn-secondary btn-xs" onclick="openFilePicker(2)"><i class="fa-solid fa-folder-open"></i> Browse</button>
                <span id="file-name-2" style="font-size:11px;color:var(--text-dim);margin-left:6px"></span>
            </div>
            <div id="ctrl-library-2">
                <select class="form-select" id="category-2" onchange="onCategoryChange(2)">
                    <option value="">-- Category --</option>
                    <?php foreach ($categories as $cat): ?>
                    <option value="<?= (int)$cat['id'] ?>"><?= h($cat['name']) ?></option>
                    <?php endforeach; ?>
                </select>
            </div>
        </div>
    </div>

    <!-- Program Output -->
    <div class="program-slot">
        <div class="slot-label">
            <span><i class="fa-solid fa-display" style="margin-right:6px"></i>Program Output</span>
            <span class="source-badge live-badge" id="pgm-badge">IDLE</span>
        </div>
        <div class="program-preview">
            <canvas id="program-canvas"></canvas>
            <div class="program-label" id="pgm-live-label" style="display:none">LIVE</div>
        </div>
        <div style="font-size:11px;color:var(--muted);display:flex;align-items:center;gap:8px">
            <span id="pgm-source-label">No active source</span>
            <span id="pgm-res-label"></span>
        </div>
    </div>
</div>

<!-- Control Bar -->
<div class="producer-controls">
    <div class="ctrl-group">
        <label>Transition</label>
        <button class="trans-btn active" id="trans-cut" onclick="setTransition('cut')">Cut</button>
        <button class="trans-btn" id="trans-fade" onclick="setTransition('fade')">Fade</button>
        <button class="trans-btn" id="trans-dissolve" onclick="setTransition('dissolve')">Dissolve</button>
    </div>
    <div class="ctrl-divider"></div>
    <div class="ctrl-group">
        <label>Duration</label>
        <input type="number" class="form-input" id="trans-duration" value="500" min="100" max="5000" step="100"
               style="width:70px;padding:4px 6px;font-size:12px" onchange="setTransitionDuration()">
        <span style="font-size:11px;color:var(--muted)">ms</span>
    </div>
    <div class="ctrl-divider"></div>
    <div class="ctrl-group">
        <button class="rec-btn rec-off" id="rec-btn" onclick="toggleRecord()">
            <i class="fa-solid fa-circle"></i> Record
        </button>
        <button class="stream-btn" id="stream-btn" onclick="toggleStream()">
            <i class="fa-solid fa-tower-broadcast"></i> Stream
        </button>
    </div>
    <div style="flex:1"></div>
    <div class="ctrl-group">
        <span id="rec-timer" style="font-size:12px;color:var(--muted);font-variant-numeric:tabular-nums"></span>
    </div>
</div>

<!-- File Picker Modal -->
<div class="file-modal-overlay" id="file-modal">
    <div class="file-modal">
        <div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:14px">
            <h3 style="margin:0">Select Video File</h3>
            <button class="btn-icon" onclick="closeFilePicker()"><i class="fa-solid fa-xmark"></i></button>
        </div>
        <input type="text" class="form-input" id="file-search" placeholder="Search video files..."
               style="margin-bottom:10px" oninput="searchFiles()">
        <div id="file-list" style="max-height:300px;overflow-y:auto"></div>
    </div>
</div>

<script src="/js/webgl-video.js"></script>
<script src="/js/video-producer.js"></script>

<script>
/* ── Producer page controller ──────────────────────────────────────── */

var producer = null;
var filePickerSlot = -1;
var videoFiles = [];
var recStartTime = 0;
var recTimerInterval = null;

/* ── Initialization ────────────────────────────────────────────────── */

function initProducer() {
    var canvases = [
        document.getElementById('preview-0'),
        document.getElementById('preview-1'),
        document.getElementById('preview-2')
    ];
    var pgmCanvas = document.getElementById('program-canvas');

    producer = new Mc1VideoProducer.VideoProducer({
        sourceCanvases: canvases,
        programCanvas: pgmCanvas
    });

    // Enumerate cameras for all webcam slots
    refreshCameras();

    // Listen for device changes
    producer.onDeviceChange(function(devices) {
        populateCameraSelects(devices);
    });

    // Initialize default source types
    onTypeChange(0);
    onTypeChange(1);
    onTypeChange(2);
}

/* ── Camera enumeration ──────────────────────────────────────────── */

function refreshCameras() {
    producer.enumerateVideoDevices().then(function(devices) {
        populateCameraSelects(devices);
        // Store device info for API
        storeDeviceInfo(devices);
    }).catch(function(e) {
        console.warn('Camera enumeration failed:', e);
    });
}

function populateCameraSelects(devices) {
    for (var i = 0; i < 3; i++) {
        var sel = document.getElementById('camera-' + i);
        if (!sel) continue;
        var current = sel.value;
        sel.innerHTML = '';
        if (devices.length === 0) {
            sel.innerHTML = '<option value="">No cameras detected</option>';
            continue;
        }
        for (var j = 0; j < devices.length; j++) {
            var d = devices[j];
            var opt = document.createElement('option');
            opt.value = d.deviceId;
            opt.textContent = d.label || ('Camera ' + (j + 1));
            sel.appendChild(opt);
        }
        if (current) sel.value = current;
    }
}

function storeDeviceInfo(devices) {
    var info = devices.map(function(d) {
        return { deviceId: d.deviceId, label: d.label, groupId: d.groupId };
    });
    mc1Api('PUT', '/api/v1/producer/devices', { devices: info }).catch(function() {});
}

/* ── Source type switching ────────────────────────────────────────── */

function onTypeChange(slot) {
    var type = document.getElementById('type-' + slot).value;
    var src = producer.getSource(slot);

    // Show/hide control panels
    var panels = ['webcam', 'file', 'library'];
    for (var i = 0; i < panels.length; i++) {
        var el = document.getElementById('ctrl-' + panels[i] + '-' + slot);
        if (el) el.style.display = (panels[i] === type) ? '' : 'none';
    }

    // Stop current source
    if (src) src.stop();

    var overlay = document.getElementById('overlay-' + slot);

    if (type === 'webcam') {
        var camSel = document.getElementById('camera-' + slot);
        var resSel = document.getElementById('res-' + slot);
        var res = Mc1VideoProducer.RESOLUTIONS[resSel.value] || Mc1VideoProducer.RESOLUTIONS['720p'];
        var deviceId = camSel.value || undefined;
        src.setWebcam(deviceId, res).then(function() {
            if (overlay) overlay.classList.add('hidden');
            updateBadges();
        }).catch(function(e) {
            console.error('Webcam error:', e);
            if (overlay) { overlay.classList.remove('hidden'); overlay.innerHTML = '<i class="fa-solid fa-exclamation-triangle"></i>&nbsp; ' + (e.name || e.message); }
            mc1Toast('Camera access denied: ' + e.message, 'err');
        });
    } else if (type === 'file') {
        if (overlay) { overlay.classList.remove('hidden'); overlay.innerHTML = '<i class="fa-solid fa-film"></i>&nbsp; Click Browse to select'; }
    } else if (type === 'library') {
        if (overlay) { overlay.classList.remove('hidden'); overlay.innerHTML = '<i class="fa-solid fa-photo-film"></i>&nbsp; Select a category'; }
    } else {
        if (overlay) { overlay.classList.remove('hidden'); overlay.innerHTML = '<i class="fa-solid fa-video-slash"></i>&nbsp; No source'; }
    }
    updateBadges();
}

function onCameraChange(slot) {
    var type = document.getElementById('type-' + slot).value;
    if (type === 'webcam') onTypeChange(slot);
}

function onResChange(slot) {
    var type = document.getElementById('type-' + slot).value;
    if (type === 'webcam') onTypeChange(slot);
}

/* ── Category (media library) ─────────────────────────────────────── */

function onCategoryChange(slot) {
    var catId = document.getElementById('category-' + slot).value;
    if (!catId) return;
    var src = producer.getSource(slot);
    var overlay = document.getElementById('overlay-' + slot);

    mc1Api('POST', '/app/api/tracks.php', {
        action: 'list_category_tracks',
        category_id: parseInt(catId)
    }).then(function(d) {
        if (d && d.tracks && d.tracks.length > 0) {
            // Filter to video files only
            var videoTracks = d.tracks.filter(function(t) {
                return t.file_path && /\.(mp4|webm|mkv|avi|mov)$/i.test(t.file_path);
            });
            if (videoTracks.length > 0) {
                src.setMediaLibrary(videoTracks);
                if (overlay) overlay.classList.add('hidden');
                updateBadges();
            } else {
                if (overlay) { overlay.classList.remove('hidden'); overlay.innerHTML = '<i class="fa-solid fa-exclamation-triangle"></i>&nbsp; No video files in category'; }
                mc1Toast('No video files found in this category', 'warn');
            }
        } else {
            if (overlay) { overlay.classList.remove('hidden'); overlay.innerHTML = '<i class="fa-solid fa-inbox"></i>&nbsp; Category is empty'; }
        }
    }).catch(function() {
        mc1Toast('Failed to load category tracks', 'err');
    });
}

/* ── File picker ──────────────────────────────────────────────────── */

function openFilePicker(slot) {
    filePickerSlot = slot;
    document.getElementById('file-modal').classList.add('open');
    document.getElementById('file-search').value = '';
    loadVideoFiles();
}

function closeFilePicker() {
    document.getElementById('file-modal').classList.remove('open');
    filePickerSlot = -1;
}

function loadVideoFiles() {
    mc1Api('POST', '/app/api/tracks.php', { action: 'search', query: '', limit: 200 })
    .then(function(d) {
        if (d && d.tracks) {
            videoFiles = d.tracks.filter(function(t) {
                return t.file_path && /\.(mp4|webm|mkv|avi|mov)$/i.test(t.file_path);
            });
        } else {
            videoFiles = [];
        }
        renderFileList(videoFiles);
    }).catch(function() {
        videoFiles = [];
        renderFileList([]);
    });
}

function searchFiles() {
    var q = document.getElementById('file-search').value.toLowerCase();
    var filtered = videoFiles.filter(function(t) {
        return (t.title || '').toLowerCase().indexOf(q) >= 0 ||
               (t.file_path || '').toLowerCase().indexOf(q) >= 0;
    });
    renderFileList(filtered);
}

function renderFileList(files) {
    var container = document.getElementById('file-list');
    if (files.length === 0) {
        container.innerHTML = '<div class="empty" style="padding:20px"><i class="fa-solid fa-film fa-fw"></i><p>No video files found</p></div>';
        return;
    }
    var html = '';
    for (var i = 0; i < files.length; i++) {
        var f = files[i];
        var name = esc(f.title || f.file_path.split('/').pop());
        html += '<div class="file-list-item" onclick="selectFile(' + f.id + ',' + esc(JSON.stringify(f.title || f.file_path.split('/').pop())) + ')">'
              + '<i class="fa-solid fa-film"></i>'
              + '<span style="flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap">' + name + '</span>'
              + '</div>';
    }
    container.innerHTML = html;
}

function selectFile(trackId, name) {
    if (filePickerSlot < 0) return;
    var src = producer.getSource(filePickerSlot);
    var overlay = document.getElementById('overlay-' + filePickerSlot);
    var url = '/app/api/audio.php?id=' + trackId;
    src.setVideoFile(url);
    if (overlay) overlay.classList.add('hidden');
    document.getElementById('file-name-' + filePickerSlot).textContent = name;
    closeFilePicker();
    updateBadges();
}

function esc(s) {
    var d = document.createElement('div');
    d.textContent = s;
    return d.innerHTML;
}

/* ── Take / source selection ──────────────────────────────────────── */

function takeSource(idx) {
    producer.setActiveSource(idx);
    updateBadges();
    updateProgramInfo();
}

function updateBadges() {
    for (var i = 0; i < 3; i++) {
        var badge = document.getElementById('badge-' + i);
        var slot = document.getElementById('source-slot-' + i);
        var src = producer.getSource(i);
        if (!badge || !slot) continue;

        if (i === producer.activeSourceIdx) {
            badge.className = 'source-badge live-badge';
            badge.textContent = 'LIVE';
            slot.classList.add('live-slot');
        } else if (src && src.type !== 'none') {
            badge.className = 'source-badge cue-badge';
            badge.textContent = 'CUE';
            slot.classList.remove('live-slot');
        } else {
            badge.className = 'source-badge off-badge';
            badge.textContent = 'OFF';
            slot.classList.remove('live-slot');
        }
    }
}

function updateProgramInfo() {
    var pgmBadge = document.getElementById('pgm-badge');
    var pgmLabel = document.getElementById('pgm-live-label');
    var pgmSrc   = document.getElementById('pgm-source-label');
    if (producer.activeSourceIdx >= 0) {
        var src = producer.getSource(producer.activeSourceIdx);
        pgmBadge.className = 'source-badge live-badge';
        pgmBadge.textContent = 'LIVE';
        pgmLabel.style.display = '';
        var types = { webcam: 'Webcam', file: 'Video File', library: 'Media Library' };
        pgmSrc.textContent = 'Source ' + (producer.activeSourceIdx + 1) + ' - ' + (types[src.type] || 'Unknown');
    } else {
        pgmBadge.className = 'source-badge off-badge';
        pgmBadge.textContent = 'IDLE';
        pgmLabel.style.display = 'none';
        pgmSrc.textContent = 'No active source';
    }
}

/* ── Transitions ──────────────────────────────────────────────────── */

function setTransition(type) {
    producer.setTransition(type);
    document.getElementById('trans-cut').classList.toggle('active', type === 'cut');
    document.getElementById('trans-fade').classList.toggle('active', type === 'fade');
    document.getElementById('trans-dissolve').classList.toggle('active', type === 'dissolve');
}

function setTransitionDuration() {
    var val = parseInt(document.getElementById('trans-duration').value) || 500;
    producer.setTransition(producer.transitionType, val);
}

/* ── Recording ────────────────────────────────────────────────────── */

function toggleRecord() {
    var btn = document.getElementById('rec-btn');
    if (!producer.isRecording) {
        producer.startRecording();
        btn.className = 'rec-btn rec-on';
        btn.innerHTML = '<i class="fa-solid fa-stop"></i> Stop';
        recStartTime = Date.now();
        recTimerInterval = setInterval(updateRecTimer, 1000);
        mc1Toast('Recording started');
    } else {
        producer.stopRecording().then(function(blob) {
            btn.className = 'rec-btn rec-off';
            btn.innerHTML = '<i class="fa-solid fa-circle"></i> Record';
            clearInterval(recTimerInterval);
            document.getElementById('rec-timer').textContent = '';

            // Download recording
            var a = document.createElement('a');
            a.href = URL.createObjectURL(blob);
            a.download = 'producer-' + new Date().toISOString().replace(/[:.]/g, '-') + '.webm';
            a.click();
            URL.revokeObjectURL(a.href);
            mc1Toast('Recording saved: ' + (blob.size / 1048576).toFixed(1) + ' MB');
        });
    }
}

function updateRecTimer() {
    var elapsed = Math.floor((Date.now() - recStartTime) / 1000);
    var h = Math.floor(elapsed / 3600);
    var m = Math.floor((elapsed % 3600) / 60);
    var s = elapsed % 60;
    var txt = (h > 0 ? h + ':' : '') + (m < 10 ? '0' : '') + m + ':' + (s < 10 ? '0' : '') + s;
    document.getElementById('rec-timer').textContent = txt;
}

/* ── Stream (placeholder for future integration) ─────────────────── */

function toggleStream() {
    var btn = document.getElementById('stream-btn');
    if (!producer.isStreaming) {
        producer.isStreaming = true;
        btn.classList.add('streaming');
        btn.innerHTML = '<i class="fa-solid fa-tower-broadcast"></i> Live';
        mc1Toast('Streaming started (browser-side capture active)');
    } else {
        producer.isStreaming = false;
        btn.classList.remove('streaming');
        btn.innerHTML = '<i class="fa-solid fa-tower-broadcast"></i> Stream';
        mc1Toast('Streaming stopped');
    }
}

/* ── Scene save/load ─────────────────────────────────────────────── */

function saveScene() {
    var sceneSel = document.getElementById('scene-select');
    var name = prompt('Scene name:', sceneSel.selectedOptions[0] ? sceneSel.selectedOptions[0].text : 'Default');
    if (!name) return;
    var config = producer.getSceneConfig();
    config.scene_name = name;

    mc1Api('PUT', '/api/v1/producer/scenes', config).then(function(d) {
        if (d && d.ok) {
            mc1Toast('Scene saved: ' + name);
            // Reload to update scene list
            setTimeout(function() { window.location.reload(); }, 800);
        } else {
            mc1Toast('Failed to save scene', 'err');
        }
    }).catch(function() {
        mc1Toast('Failed to save scene', 'err');
    });
}

function loadScene() {
    var sceneId = document.getElementById('scene-select').value;
    if (!sceneId) return;
    mc1Api('GET', '/api/v1/producer/scenes?id=' + sceneId).then(function(d) {
        if (d && d.scene) {
            producer.loadSceneConfig(d.scene);
            mc1Toast('Scene loaded');
        }
    }).catch(function() {
        mc1Toast('Failed to load scene', 'err');
    });
}

/* ── DOMContentLoaded ─────────────────────────────────────────────── */

document.addEventListener('DOMContentLoaded', function() {
    initProducer();
    document.getElementById('scene-select').addEventListener('change', loadScene);

    // Close file modal on overlay click
    document.getElementById('file-modal').addEventListener('click', function(e) {
        if (e.target === this) closeFilePicker();
    });

    // Keyboard shortcuts
    document.addEventListener('keydown', function(e) {
        // 1/2/3 = take source (when not in input)
        if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT' || e.target.tagName === 'TEXTAREA') return;
        if (e.key === '1') takeSource(0);
        if (e.key === '2') takeSource(1);
        if (e.key === '3') takeSource(2);
    });
});
</script>

<?php require_once __DIR__ . '/app/inc/footer.php'; ?>
