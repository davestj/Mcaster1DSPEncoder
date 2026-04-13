<?php
/*
 * producer.php -- Mcaster1 Video Producer
 *
 * Browser-based video production: 3 source slots (webcam, video file,
 * media library category) with WebGL preview + program output.
 * Full video switcher: PGM/PVW bus, T-bar crossfader, auto-transitions,
 * chroma key, PIP, color correction, lower-third overlay.
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
/* Producer layout -- 6 sources in 2 rows of 3 + program output on right */
.producer-grid {
    display: grid;
    grid-template-columns: 1fr 1fr 1fr 1.5fr;
    grid-template-rows: auto auto;
    gap: 12px;
    margin-bottom: 16px;
}
.program-slot {
    grid-row: 1 / 3;
    grid-column: 4;
}
.source-slot {
    background: var(--card);
    border: 2px solid var(--border);
    border-radius: var(--radius);
    padding: 12px;
    display: flex;
    flex-direction: column;
    gap: 8px;
    transition: border-color .2s, box-shadow .2s;
}
.source-slot.pgm-slot {
    border-color: rgba(239,68,68,.7);
    box-shadow: 0 0 0 1px rgba(239,68,68,.2), 0 4px 16px rgba(239,68,68,.08);
}
.source-slot.pvw-slot {
    border-color: rgba(234,179,8,.6);
    box-shadow: 0 0 0 1px rgba(234,179,8,.15), 0 4px 16px rgba(234,179,8,.06);
}
/* Tally lights */
.tally-pgm {
    position: absolute;
    top: 4px; right: 4px;
    background: rgba(239,68,68,.90);
    color: #fff;
    font-size: 8px;
    font-weight: 800;
    padding: 2px 6px;
    border-radius: 3px;
    letter-spacing: .08em;
    z-index: 3;
    animation: tally-pulse 1.5s ease-in-out infinite;
}
.tally-pvw {
    position: absolute;
    top: 4px; right: 4px;
    background: rgba(34,197,94,.85);
    color: #fff;
    font-size: 8px;
    font-weight: 800;
    padding: 2px 6px;
    border-radius: 3px;
    letter-spacing: .08em;
    z-index: 3;
}
@keyframes tally-pulse {
    0%,100% { opacity: 1; }
    50% { opacity: .6; }
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
.source-badge.pgm-badge {
    background: rgba(239,68,68,.2);
    color: var(--red);
}
.source-badge.pvw-badge {
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
.preview-wrap:hover .bus-btns { opacity: 1; }
.bus-btns {
    position: absolute;
    bottom: 6px;
    left: 50%;
    transform: translateX(-50%);
    opacity: 0;
    transition: opacity .2s;
    z-index: 2;
    display: flex;
    gap: 4px;
}
.bus-btn {
    padding: 3px 10px;
    font-size: 10px;
    font-weight: 800;
    border: none;
    border-radius: 3px;
    cursor: pointer;
    letter-spacing: .06em;
    transition: all .15s;
}
.bus-btn.pgm-btn {
    background: rgba(239,68,68,.8);
    color: #fff;
}
.bus-btn.pgm-btn:hover { background: rgba(239,68,68,1); }
.bus-btn.pvw-btn {
    background: rgba(234,179,8,.8);
    color: #1a1a2e;
}
.bus-btn.pvw-btn:hover { background: rgba(234,179,8,1); }
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

/* T-bar crossfader */
.tbar-container {
    background: var(--card);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 12px 16px;
    margin-bottom: 12px;
    display: flex;
    align-items: center;
    gap: 16px;
}
.tbar-label {
    font-size: 11px;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: .04em;
    color: var(--muted);
    white-space: nowrap;
}
.tbar-slider-wrap {
    flex: 1;
    display: flex;
    align-items: center;
    gap: 10px;
}
.tbar-track {
    flex: 1;
    height: 32px;
    background: rgba(30,41,59,.8);
    border-radius: 4px;
    position: relative;
    cursor: pointer;
    border: 1px solid var(--border);
}
.tbar-fill {
    position: absolute;
    top: 0; left: 0; bottom: 0;
    background: linear-gradient(90deg, rgba(239,68,68,.4), rgba(234,179,8,.4));
    border-radius: 4px 0 0 4px;
    pointer-events: none;
    transition: width 0.05s;
}
.tbar-handle {
    position: absolute;
    top: -2px; bottom: -2px;
    width: 8px;
    background: var(--text);
    border-radius: 3px;
    cursor: grab;
    box-shadow: 0 0 4px rgba(0,0,0,.3);
    transition: left 0.05s;
}
.tbar-handle:active { cursor: grabbing; }
.tbar-bus-label {
    font-size: 10px;
    font-weight: 700;
    color: var(--muted);
    text-transform: uppercase;
    width: 36px;
    text-align: center;
}
.tbar-bus-label.pgm-color { color: var(--red); }
.tbar-bus-label.pvw-color { color: var(--yellow); }

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
    padding: 5px 10px;
    font-size: 11px;
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
.auto-btn {
    padding: 5px 14px;
    font-size: 12px;
    font-weight: 700;
    border: 1px solid rgba(20,184,166,.4);
    border-radius: var(--radius-sm);
    background: rgba(20,184,166,.15);
    color: var(--teal);
    cursor: pointer;
    transition: all .15s;
}
.auto-btn:hover { background: rgba(20,184,166,.25); }
.auto-btn:disabled { opacity: .4; cursor: default; }
.cut-btn {
    padding: 5px 14px;
    font-size: 12px;
    font-weight: 700;
    border: 1px solid rgba(239,68,68,.4);
    border-radius: var(--radius-sm);
    background: rgba(239,68,68,.15);
    color: var(--red);
    cursor: pointer;
    transition: all .15s;
}
.cut-btn:hover { background: rgba(239,68,68,.25); }
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

/* Switcher panels row */
.switcher-panels {
    display: grid;
    grid-template-columns: 1fr 1fr 1fr;
    gap: 12px;
    margin-top: 12px;
}
.sw-panel {
    background: var(--card);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 14px 16px;
}
.sw-panel .sec-title {
    font-size: 12px;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: .04em;
    color: var(--teal);
    margin-bottom: 10px;
    display: flex;
    align-items: center;
    gap: 6px;
}
.sw-row {
    display: flex;
    align-items: center;
    gap: 8px;
    margin-bottom: 8px;
}
.sw-row:last-child { margin-bottom: 0; }
.sw-row label {
    font-size: 11px;
    color: var(--text-dim);
    width: 72px;
    flex-shrink: 0;
}
.sw-row input[type="range"] {
    flex: 1;
    accent-color: var(--teal);
    height: 4px;
}
.sw-row .val-label {
    font-size: 10px;
    color: var(--muted);
    width: 36px;
    text-align: right;
    font-variant-numeric: tabular-nums;
}
.sw-toggle {
    display: flex;
    align-items: center;
    gap: 8px;
    margin-bottom: 8px;
}
.sw-toggle label {
    font-size: 11px;
    color: var(--text-dim);
    cursor: pointer;
}

/* Lower third panel */
.lt-panel {
    grid-column: 1 / -1;
}
.lt-row {
    display: flex;
    align-items: center;
    gap: 8px;
    margin-bottom: 8px;
}
.lt-row:last-child { margin-bottom: 0; }
.lt-row label {
    font-size: 11px;
    color: var(--text-dim);
    width: 72px;
    flex-shrink: 0;
}
.lt-row .form-input,
.lt-row .form-select {
    font-size: 12px;
    padding: 4px 8px;
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

/* Scene preset buttons bar */
.scene-presets-bar {
    display: flex;
    gap: 6px;
    margin-top: 12px;
    padding: 10px 14px;
    background: var(--card);
    border: 1px solid var(--border);
    border-radius: var(--radius);
}
.scene-preset-btn {
    width: 44px;
    height: 36px;
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    background: rgba(255,255,255,.03);
    color: var(--muted);
    font-size: 12px;
    font-weight: 700;
    cursor: pointer;
    transition: all .15s;
    display: flex;
    align-items: center;
    justify-content: center;
}
.scene-preset-btn:hover { background: rgba(255,255,255,.08); color: var(--text); }
.scene-preset-btn.active { background: rgba(20,184,166,.15); border-color: var(--teal); color: var(--teal); }
.scene-preset-btn.saved { border-color: rgba(234,179,8,.4); color: var(--yellow); }
.scene-preset-btn .preset-label {
    font-size: 8px;
    display: block;
    line-height: 1;
    margin-top: 1px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    max-width: 40px;
}

/* Overlays panel */
.overlays-panel {
    background: var(--card);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 14px 16px;
    margin-top: 12px;
}
.overlays-panel .sec-title {
    font-size: 12px;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: .04em;
    color: var(--teal);
    margin-bottom: 10px;
    cursor: pointer;
    display: flex;
    align-items: center;
    gap: 6px;
}
.overlay-section {
    border-bottom: 1px solid rgba(51,65,85,.3);
    padding: 10px 0;
}
.overlay-section:last-child { border-bottom: none; }
.overlay-section .sub-title {
    font-size: 11px;
    font-weight: 700;
    color: var(--text-dim);
    text-transform: uppercase;
    letter-spacing: .04em;
    margin-bottom: 8px;
}

/* Color picker small */
input[type="color"].sm-color {
    width: 28px;
    height: 24px;
    padding: 0;
    border: 1px solid var(--border);
    border-radius: 3px;
    cursor: pointer;
    background: transparent;
}

/* Vodcast recording panel */
.vodcast-panel {
    background: var(--card);
    border: 1px solid rgba(168,85,247,.3);
    border-radius: var(--radius);
    padding: 14px 16px;
    margin-top: 12px;
}
.vodcast-panel .sec-title {
    font-size: 12px;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: .04em;
    color: #a855f7;
    margin-bottom: 10px;
    display: flex;
    align-items: center;
    gap: 6px;
}
.vodcast-row {
    display: flex;
    align-items: center;
    gap: 8px;
    margin-bottom: 8px;
}
.vodcast-row label {
    font-size: 11px;
    color: var(--text-dim);
    width: 80px;
    flex-shrink: 0;
}
.vodcast-row .form-input,
.vodcast-row .form-select {
    font-size: 12px;
    padding: 5px 8px;
}
.vodcast-stats {
    display: grid;
    grid-template-columns: 1fr 1fr 1fr;
    gap: 6px 12px;
    font-size: 12px;
    margin-top: 10px;
    padding: 10px;
    background: rgba(30,41,59,.5);
    border-radius: var(--radius-sm);
}
.vodcast-stats span.vl { color: var(--muted); }
.vodcast-stats span.vv { color: var(--text-dim); font-variant-numeric: tabular-nums; }
.vodcast-rec-btn {
    padding: 6px 16px;
    border-radius: var(--radius-sm);
    font-size: 12px;
    font-weight: 700;
    border: 1px solid rgba(168,85,247,.3);
    background: rgba(168,85,247,.12);
    color: #a855f7;
    cursor: pointer;
    display: inline-flex;
    align-items: center;
    gap: 6px;
    transition: all .15s;
}
.vodcast-rec-btn:hover { background: rgba(168,85,247,.22); }
.vodcast-rec-btn.active {
    background: #a855f7;
    color: #fff;
    border-color: #a855f7;
    animation: rec-pulse 1.2s ease-in-out infinite;
}
.vodcast-rec-btn:disabled { opacity: .4; cursor: default; }

/* Thumbnail modal */
.thumb-preview { max-width: 320px; border-radius: var(--radius-sm); margin-top: 8px; }

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

@media(max-width:1200px) {
    .producer-grid { grid-template-columns: 1fr 1fr 1fr; grid-template-rows: auto; }
    .program-slot { grid-column: 1 / -1; grid-row: auto; }
    .switcher-panels { grid-template-columns: 1fr 1fr; }
}
@media(max-width:900px) {
    .producer-grid { grid-template-columns: 1fr 1fr; }
    .program-slot { grid-column: 1 / -1; }
    .switcher-panels { grid-template-columns: 1fr; }
}
@media(max-width:640px) {
    .producer-grid { grid-template-columns: 1fr; }
}
</style>

<!-- Producer Grid -->
<div class="sec-hdr">
    <div class="sec-title">
        <i class="fa-solid fa-tv" style="color:var(--teal);margin-right:8px"></i>Video Production Switcher
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
    <!-- Source 1 -->
    <div class="source-slot" id="source-slot-0">
        <div class="slot-label">
            <span>Source 1</span>
            <span class="source-badge off-badge" id="badge-0">OFF</span>
        </div>
        <div class="preview-wrap">
            <canvas id="preview-0"></canvas>
            <div class="preview-overlay" id="overlay-0"><i class="fa-solid fa-video-slash"></i>&nbsp; No source</div>
            <div class="tally-pgm" id="tally-pgm-0" style="display:none">ON AIR</div>
            <div class="tally-pvw" id="tally-pvw-0" style="display:none">NEXT</div>
            <div class="bus-btns">
                <button class="bus-btn pgm-btn" onclick="setPGM(0)">PGM</button>
                <button class="bus-btn pvw-btn" onclick="setPVW(0)">PVW</button>
            </div>
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
                <select class="form-select" id="audio-device-0" style="margin-top:4px;font-size:11px">
                    <option value="">-- Audio Device --</option>
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

    <!-- Source 2 -->
    <div class="source-slot" id="source-slot-1">
        <div class="slot-label">
            <span>Source 2</span>
            <span class="source-badge off-badge" id="badge-1">OFF</span>
        </div>
        <div class="preview-wrap">
            <canvas id="preview-1"></canvas>
            <div class="preview-overlay" id="overlay-1"><i class="fa-solid fa-video-slash"></i>&nbsp; No source</div>
            <div class="tally-pgm" id="tally-pgm-1" style="display:none">ON AIR</div>
            <div class="tally-pvw" id="tally-pvw-1" style="display:none">NEXT</div>
            <div class="bus-btns">
                <button class="bus-btn pgm-btn" onclick="setPGM(1)">PGM</button>
                <button class="bus-btn pvw-btn" onclick="setPVW(1)">PVW</button>
            </div>
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
                <select class="form-select" id="audio-device-1" style="margin-top:4px;font-size:11px">
                    <option value="">-- Audio Device --</option>
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

    <!-- Source 3 -->
    <div class="source-slot" id="source-slot-2">
        <div class="slot-label">
            <span>Source 3</span>
            <span class="source-badge off-badge" id="badge-2">OFF</span>
        </div>
        <div class="preview-wrap">
            <canvas id="preview-2"></canvas>
            <div class="preview-overlay" id="overlay-2"><i class="fa-solid fa-video-slash"></i>&nbsp; No source</div>
            <div class="tally-pgm" id="tally-pgm-2" style="display:none">ON AIR</div>
            <div class="tally-pvw" id="tally-pvw-2" style="display:none">NEXT</div>
            <div class="bus-btns">
                <button class="bus-btn pgm-btn" onclick="setPGM(2)">PGM</button>
                <button class="bus-btn pvw-btn" onclick="setPVW(2)">PVW</button>
            </div>
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
                <select class="form-select" id="audio-device-2" style="margin-top:4px;font-size:11px">
                    <option value="">-- Audio Device --</option>
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

    <!-- Source 4 (Camera 4) -->
    <div class="source-slot" id="source-slot-3">
        <div class="slot-label">
            <span>Source 4</span>
            <span class="source-badge off-badge" id="badge-3">OFF</span>
        </div>
        <div class="preview-wrap">
            <canvas id="preview-3"></canvas>
            <div class="preview-overlay" id="overlay-3"><i class="fa-solid fa-video-slash"></i>&nbsp; No source</div>
            <div class="tally-pgm" id="tally-pgm-3" style="display:none">ON AIR</div>
            <div class="tally-pvw" id="tally-pvw-3" style="display:none">NEXT</div>
            <div class="bus-btns">
                <button class="bus-btn pgm-btn" onclick="setPGM(3)">PGM</button>
                <button class="bus-btn pvw-btn" onclick="setPVW(3)">PVW</button>
            </div>
        </div>
        <div class="source-controls">
            <select class="form-select" id="type-3" onchange="onTypeChange(3)">
                <option value="none" selected>-- Select Type --</option>
                <option value="webcam">Webcam</option>
                <option value="file">Video File</option>
                <option value="library">Media Library</option>
            </select>
            <div id="ctrl-webcam-3" style="display:none">
                <select class="form-select" id="camera-3" onchange="onCameraChange(3)" style="margin-bottom:4px">
                    <option value="">Detecting cameras...</option>
                </select>
                <select class="form-select" id="res-3" onchange="onResChange(3)">
                    <option value="480p">480p (SD)</option>
                    <option value="720p" selected>720p (HD)</option>
                    <option value="1080p">1080p (Full HD)</option>
                </select>
            </div>
            <div id="ctrl-file-3" style="display:none">
                <button class="btn btn-secondary btn-xs" onclick="openFilePicker(3)"><i class="fa-solid fa-folder-open"></i> Browse</button>
                <span id="file-name-3" style="font-size:11px;color:var(--text-dim);margin-left:6px"></span>
            </div>
            <div id="ctrl-library-3" style="display:none">
                <select class="form-select" id="category-3" onchange="onCategoryChange(3)">
                    <option value="">-- Category --</option>
                    <?php foreach ($categories as $cat): ?>
                    <option value="<?= (int)$cat['id'] ?>"><?= h($cat['name']) ?></option>
                    <?php endforeach; ?>
                </select>
            </div>
        </div>
    </div>

    <!-- Source 5 (Video File) -->
    <div class="source-slot" id="source-slot-4">
        <div class="slot-label">
            <span>Source 5</span>
            <span class="source-badge off-badge" id="badge-4">OFF</span>
        </div>
        <div class="preview-wrap">
            <canvas id="preview-4"></canvas>
            <div class="preview-overlay" id="overlay-4"><i class="fa-solid fa-video-slash"></i>&nbsp; No source</div>
            <div class="tally-pgm" id="tally-pgm-4" style="display:none">ON AIR</div>
            <div class="tally-pvw" id="tally-pvw-4" style="display:none">NEXT</div>
            <div class="bus-btns">
                <button class="bus-btn pgm-btn" onclick="setPGM(4)">PGM</button>
                <button class="bus-btn pvw-btn" onclick="setPVW(4)">PVW</button>
            </div>
        </div>
        <div class="source-controls">
            <select class="form-select" id="type-4" onchange="onTypeChange(4)">
                <option value="none" selected>-- Select Type --</option>
                <option value="webcam">Webcam</option>
                <option value="file">Video File</option>
                <option value="library">Media Library</option>
            </select>
            <div id="ctrl-webcam-4" style="display:none">
                <select class="form-select" id="camera-4" onchange="onCameraChange(4)" style="margin-bottom:4px">
                    <option value="">Detecting cameras...</option>
                </select>
                <select class="form-select" id="res-4" onchange="onResChange(4)">
                    <option value="480p">480p (SD)</option>
                    <option value="720p" selected>720p (HD)</option>
                    <option value="1080p">1080p (Full HD)</option>
                </select>
                <select class="form-select" id="audio-device-4" style="margin-top:4px;font-size:11px">
                    <option value="">-- Audio Device --</option>
                </select>
            </div>
            <div id="ctrl-file-4" style="display:none">
                <button class="btn btn-secondary btn-xs" onclick="openFilePicker(4)"><i class="fa-solid fa-folder-open"></i> Browse</button>
                <span id="file-name-4" style="font-size:11px;color:var(--text-dim);margin-left:6px"></span>
            </div>
            <div id="ctrl-library-4" style="display:none">
                <select class="form-select" id="category-4" onchange="onCategoryChange(4)">
                    <option value="">-- Category --</option>
                    <?php foreach ($categories as $cat): ?>
                    <option value="<?= (int)$cat['id'] ?>"><?= h($cat['name']) ?></option>
                    <?php endforeach; ?>
                </select>
            </div>
        </div>
    </div>

    <!-- Source 6 (Media Library Auto-Play) -->
    <div class="source-slot" id="source-slot-5">
        <div class="slot-label">
            <span>Source 6</span>
            <span class="source-badge off-badge" id="badge-5">OFF</span>
        </div>
        <div class="preview-wrap">
            <canvas id="preview-5"></canvas>
            <div class="preview-overlay" id="overlay-5"><i class="fa-solid fa-video-slash"></i>&nbsp; No source</div>
            <div class="tally-pgm" id="tally-pgm-5" style="display:none">ON AIR</div>
            <div class="tally-pvw" id="tally-pvw-5" style="display:none">NEXT</div>
            <div class="bus-btns">
                <button class="bus-btn pgm-btn" onclick="setPGM(5)">PGM</button>
                <button class="bus-btn pvw-btn" onclick="setPVW(5)">PVW</button>
            </div>
        </div>
        <div class="source-controls">
            <select class="form-select" id="type-5" onchange="onTypeChange(5)">
                <option value="none" selected>-- Select Type --</option>
                <option value="webcam">Webcam</option>
                <option value="file">Video File</option>
                <option value="library">Media Library</option>
            </select>
            <div id="ctrl-webcam-5" style="display:none">
                <select class="form-select" id="camera-5" onchange="onCameraChange(5)" style="margin-bottom:4px">
                    <option value="">Detecting cameras...</option>
                </select>
                <select class="form-select" id="res-5" onchange="onResChange(5)">
                    <option value="480p">480p (SD)</option>
                    <option value="720p" selected>720p (HD)</option>
                    <option value="1080p">1080p (Full HD)</option>
                </select>
                <select class="form-select" id="audio-device-5" style="margin-top:4px;font-size:11px">
                    <option value="">-- Audio Device --</option>
                </select>
            </div>
            <div id="ctrl-file-5" style="display:none">
                <button class="btn btn-secondary btn-xs" onclick="openFilePicker(5)"><i class="fa-solid fa-folder-open"></i> Browse</button>
                <span id="file-name-5" style="font-size:11px;color:var(--text-dim);margin-left:6px"></span>
            </div>
            <div id="ctrl-library-5" style="display:none">
                <select class="form-select" id="category-5" onchange="onCategoryChange(5)">
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
            <span class="source-badge pgm-badge" id="pgm-badge">IDLE</span>
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

<!-- T-bar Crossfader -->
<div class="tbar-container">
    <div style="display:flex;align-items:center;gap:8px">
        <label class="tbar-label">T-Bar</label>
        <label style="font-size:10px;color:var(--text-dim);display:flex;align-items:center;gap:4px;cursor:pointer">
            <input type="checkbox" id="tbar-manual" onchange="toggleTbarMode()"> Manual
        </label>
    </div>
    <div class="tbar-slider-wrap">
        <span class="tbar-bus-label pgm-color">PGM</span>
        <div class="tbar-track" id="tbar-track">
            <div class="tbar-fill" id="tbar-fill" style="width:0%"></div>
            <div class="tbar-handle" id="tbar-handle" style="left:0%"></div>
        </div>
        <span class="tbar-bus-label pvw-color">PVW</span>
    </div>
    <div class="ctrl-divider"></div>
    <button class="cut-btn" onclick="doCut()"><i class="fa-solid fa-scissors"></i> CUT</button>
    <button class="auto-btn" id="auto-trans-btn" onclick="doAutoTransition()"><i class="fa-solid fa-play"></i> AUTO</button>
    <div class="ctrl-divider"></div>
    <label style="font-size:10px;color:var(--text-dim);display:flex;align-items:center;gap:4px;cursor:pointer;white-space:nowrap">
        <input type="checkbox" id="afv-toggle" onchange="toggleAudioFollowsVideo()">
        <i class="fa-solid fa-microphone" style="font-size:11px"></i> Audio Follows Video
    </label>
</div>

<!-- Control Bar -->
<div class="producer-controls">
    <div class="ctrl-group">
        <label>Transition</label>
        <button class="trans-btn active" id="trans-cut" onclick="setTransition('cut')">Cut</button>
        <button class="trans-btn" id="trans-fade" onclick="setTransition('fade')">Fade</button>
        <button class="trans-btn" id="trans-dissolve" onclick="setTransition('dissolve')">Dissolve</button>
        <button class="trans-btn" id="trans-wipe_left" onclick="setTransition('wipe_left')">Wipe L</button>
        <button class="trans-btn" id="trans-wipe_right" onclick="setTransition('wipe_right')">Wipe R</button>
        <button class="trans-btn" id="trans-wipe_circle" onclick="setTransition('wipe_circle')">Circle</button>
        <button class="trans-btn" id="trans-zoom" onclick="setTransition('zoom')">Zoom</button>
        <button class="trans-btn" id="trans-slide" onclick="setTransition('slide')">Slide</button>
        <button class="trans-btn" id="trans-stinger" onclick="setTransition('stinger')">Stinger</button>
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
        <button class="vodcast-rec-btn" id="vodcast-btn" onclick="toggleVodcastPanel()">
            <i class="fa-solid fa-video"></i> Vodcast <i class="fa-solid fa-caret-down" style="font-size:10px;margin-left:2px"></i>
        </button>
        <div style="position:relative;display:inline-block">
            <button class="stream-btn" id="stream-btn" onclick="toggleStreamPanel()">
                <i class="fa-solid fa-tower-broadcast"></i> Stream <i class="fa-solid fa-caret-down" style="font-size:10px;margin-left:2px"></i>
            </button>
            <span id="stream-live-badge" style="display:none;background:var(--red);color:#fff;font-size:9px;font-weight:800;
                  padding:2px 6px;border-radius:8px;letter-spacing:.06em;position:absolute;top:-6px;right:-8px;
                  animation:rec-pulse 1.2s ease-in-out infinite">LIVE</span>
        </div>
        <div style="position:relative;display:inline-block">
            <button class="btn btn-secondary btn-xs" id="captions-btn" onclick="toggleCaptionsPanel()"
                    style="font-size:11px;padding:5px 10px;border-radius:6px">
                <i class="fa-solid fa-closed-captioning"></i> CC <i class="fa-solid fa-caret-down" style="font-size:10px;margin-left:2px"></i>
            </button>
            <span id="cc-live-badge" style="display:none;background:var(--teal);color:#fff;font-size:8px;font-weight:800;
                  padding:1px 5px;border-radius:6px;letter-spacing:.06em;position:absolute;top:-5px;right:-6px">ON</span>
        </div>
    </div>
    <div style="flex:1"></div>
    <div class="ctrl-group">
        <span id="rec-timer" style="font-size:12px;color:var(--muted);font-variant-numeric:tabular-nums"></span>
    </div>
</div>

<!-- Stream Panel (dropdown) -->
<div id="stream-panel" style="display:none;margin-top:12px">
    <div class="sw-panel" style="border-color:rgba(20,184,166,.3)">
        <div class="sec-title"><i class="fa-solid fa-tower-broadcast"></i> Live Stream Configuration</div>
        <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px">
            <!-- Left: target selection + settings -->
            <div>
                <div class="sw-row">
                    <label>Target</label>
                    <select class="form-select" id="stream-target" style="flex:1;font-size:11px;padding:4px 8px"
                            onchange="onStreamTargetChange()">
                        <option value="">-- Select RTMP Target --</option>
                    </select>
                </div>
                <div class="sw-row">
                    <label>Audio Src</label>
                    <select class="form-select" id="stream-audio-slot" style="flex:1;font-size:11px;padding:4px 8px">
                        <option value="0">No Audio</option>
                        <option value="1">Encoder Slot 1</option>
                        <option value="2">Encoder Slot 2</option>
                        <option value="3">Encoder Slot 3</option>
                    </select>
                </div>
                <div class="sw-row">
                    <label>Codec</label>
                    <select class="form-select" id="stream-codec" style="flex:1;font-size:11px;padding:4px 8px">
                        <option value="webm">VP9 + Opus (WebM / Icecast)</option>
                        <option value="rtmp" selected>H264 + AAC (RTMP / Twitch / YouTube)</option>
                    </select>
                </div>
                <div class="sw-row">
                    <label>Bitrate</label>
                    <select class="form-select" id="stream-bitrate" style="flex:1;font-size:11px;padding:4px 8px">
                        <option value="1000000">480p @ 1000 kbps</option>
                        <option value="2500000" selected>720p @ 2500 kbps</option>
                        <option value="4500000">1080p @ 4500 kbps</option>
                    </select>
                </div>
                <div style="margin-top:10px;display:flex;gap:8px">
                    <button class="auto-btn" id="stream-start-btn" onclick="startVideoStream()"
                            style="padding:6px 16px;font-size:12px">
                        <i class="fa-solid fa-play"></i> Start Stream
                    </button>
                    <button class="cut-btn" id="stream-stop-btn" onclick="stopVideoStream()" style="display:none;padding:6px 16px;font-size:12px">
                        <i class="fa-solid fa-stop"></i> Stop Stream
                    </button>
                </div>
            </div>
            <!-- Right: stream health / status -->
            <div>
                <div style="font-size:11px;font-weight:700;text-transform:uppercase;letter-spacing:.04em;color:var(--teal);margin-bottom:8px">
                    Stream Health
                </div>
                <div style="display:grid;grid-template-columns:1fr 1fr;gap:6px 12px;font-size:12px">
                    <div>
                        <span style="color:var(--muted)">Status:</span>
                        <span id="stream-status-label" style="color:var(--text-dim)">Idle</span>
                    </div>
                    <div>
                        <span style="color:var(--muted)">Duration:</span>
                        <span id="stream-duration-label" style="color:var(--text-dim);font-variant-numeric:tabular-nums">00:00:00</span>
                    </div>
                    <div>
                        <span style="color:var(--muted)">Uploaded:</span>
                        <span id="stream-bytes-label" style="color:var(--text-dim)">0 MB</span>
                    </div>
                    <div>
                        <span style="color:var(--muted)">Chunks:</span>
                        <span id="stream-chunks-label" style="color:var(--text-dim);font-variant-numeric:tabular-nums">0</span>
                    </div>
                    <div>
                        <span style="color:var(--muted)">Target:</span>
                        <span id="stream-target-label" style="color:var(--text-dim)">--</span>
                    </div>
                    <div>
                        <span style="color:var(--muted)">Server:</span>
                        <span id="stream-server-status" style="color:var(--text-dim)">--</span>
                    </div>
                </div>
                <div style="margin-top:10px">
                    <div style="font-size:10px;color:var(--muted);margin-bottom:4px">Buffer (estimated)</div>
                    <div style="background:rgba(30,41,59,.8);border-radius:3px;height:8px;overflow:hidden;border:1px solid var(--border)">
                        <div id="stream-buffer-bar" style="height:100%;background:var(--teal);width:0%;transition:width .3s"></div>
                    </div>
                </div>
            </div>
        </div>
    </div>
</div>

<!-- Captions Panel (dropdown) -->
<div id="captions-panel" style="display:none;margin-top:12px">
    <div class="sw-panel" style="border-color:rgba(20,184,166,.3)">
        <div class="sec-title"><i class="fa-solid fa-closed-captioning"></i> Closed Captions</div>
        <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px">
            <!-- Left: controls -->
            <div>
                <div class="sw-row">
                    <label>Mode</label>
                    <div style="display:flex;gap:6px;flex:1">
                        <button class="btn btn-xs" id="cc-auto-btn" onclick="ccStartAutoTranscribe()"
                                style="font-size:11px;padding:4px 10px">
                            <i class="fa-solid fa-wand-magic-sparkles"></i> Auto-Transcribe
                        </button>
                        <button class="btn btn-xs btn-secondary" id="cc-stop-btn" onclick="ccStopAutoTranscribe()"
                                style="font-size:11px;padding:4px 10px;display:none">
                            <i class="fa-solid fa-stop"></i> Stop
                        </button>
                    </div>
                </div>
                <div class="sw-row">
                    <label>Import</label>
                    <div style="display:flex;gap:6px;flex:1">
                        <button class="btn btn-xs btn-secondary" onclick="ccImportFile()"
                                style="font-size:11px;padding:4px 10px">
                            <i class="fa-solid fa-file-import"></i> SRT / VTT
                        </button>
                        <input type="file" id="cc-import-input" accept=".srt,.vtt" style="display:none" onchange="ccHandleImport(this)">
                    </div>
                </div>
                <div class="sw-row">
                    <label>Export</label>
                    <div style="display:flex;gap:6px;flex:1">
                        <button class="btn btn-xs btn-secondary" onclick="ccExport('srt')" style="font-size:11px;padding:4px 10px">
                            <i class="fa-solid fa-download"></i> SRT
                        </button>
                        <button class="btn btn-xs btn-secondary" onclick="ccExport('vtt')" style="font-size:11px;padding:4px 10px">
                            <i class="fa-solid fa-download"></i> VTT
                        </button>
                    </div>
                </div>
                <div class="sw-row">
                    <label>Language</label>
                    <select class="form-select" id="cc-language" style="flex:1;font-size:11px;padding:4px 8px">
                        <option value="en" selected>English</option>
                        <option value="es">Spanish</option>
                        <option value="fr">French</option>
                        <option value="de">German</option>
                        <option value="pt">Portuguese</option>
                        <option value="ja">Japanese</option>
                        <option value="ko">Korean</option>
                        <option value="zh">Chinese</option>
                    </select>
                </div>
                <div class="sw-row">
                    <label style="white-space:nowrap">Show on Video</label>
                    <label style="display:flex;align-items:center;gap:4px;cursor:pointer;font-size:11px;color:var(--text-dim)">
                        <input type="checkbox" id="cc-show-overlay" onchange="ccToggleOverlay()"> Enable overlay
                    </label>
                </div>
                <div class="sw-row">
                    <label>Position</label>
                    <select class="form-select" id="cc-position" style="flex:1;font-size:11px;padding:4px 8px" onchange="ccUpdateStyle()">
                        <option value="bottom" selected>Bottom</option>
                        <option value="top">Top</option>
                    </select>
                </div>
                <div class="sw-row">
                    <label>Font Size</label>
                    <input type="range" id="cc-fontsize" min="14" max="48" value="28" style="flex:1" onchange="ccUpdateStyle()">
                    <span id="cc-fontsize-val" style="font-size:11px;color:var(--muted);min-width:28px;text-align:right">28</span>
                </div>
                <div class="sw-row">
                    <label>BG Opacity</label>
                    <input type="range" id="cc-bg-opacity" min="0" max="100" value="70" style="flex:1" onchange="ccUpdateStyle()">
                    <span id="cc-bg-opacity-val" style="font-size:11px;color:var(--muted);min-width:28px;text-align:right">70%</span>
                </div>
                <div class="sw-row">
                    <label style="white-space:nowrap">Burn into Video</label>
                    <label style="display:flex;align-items:center;gap:4px;cursor:pointer;font-size:11px;color:var(--text-dim)">
                        <input type="checkbox" id="cc-burn"> Include in recording/export
                    </label>
                </div>
            </div>
            <!-- Right: caption display / editor -->
            <div>
                <div style="font-size:11px;font-weight:600;color:var(--text);margin-bottom:6px;display:flex;align-items:center;gap:6px">
                    <i class="fa-solid fa-list"></i> Captions
                    <span id="cc-cue-count" style="font-size:10px;color:var(--muted)">(0 cues)</span>
                    <button class="btn btn-xs btn-secondary" onclick="ccClearAll()" style="margin-left:auto;font-size:10px;padding:2px 8px">
                        <i class="fa-solid fa-trash"></i> Clear
                    </button>
                </div>
                <div id="cc-cue-list" style="max-height:220px;overflow-y:auto;background:rgba(0,0,0,.2);border:1px solid var(--border);
                     border-radius:var(--radius-sm);padding:4px;font-size:11px;font-family:'SF Mono','Fira Code',monospace">
                    <div style="color:var(--muted);text-align:center;padding:20px">No captions yet</div>
                </div>
                <div id="cc-live-preview" style="margin-top:8px;padding:8px;background:rgba(0,0,0,.5);border-radius:var(--radius-sm);
                     min-height:40px;text-align:center;color:#fff;font-size:14px;font-weight:600;display:none">
                </div>
            </div>
        </div>
    </div>
</div>

<!-- Vodcast Recording Panel (dropdown) -->
<div id="vodcast-panel" style="display:none;margin-top:12px">
    <div class="vodcast-panel">
        <div class="sec-title"><i class="fa-solid fa-video"></i> Record Vodcast Episode</div>
        <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px">
            <div>
                <div class="vodcast-row">
                    <label>Show</label>
                    <select class="form-select" id="vodcast-show" style="flex:1">
                        <option value="">-- Select Podcast Show --</option>
                    </select>
                </div>
                <div class="vodcast-row">
                    <label>Title</label>
                    <input type="text" class="form-input" id="vodcast-title" style="flex:1" placeholder="Episode title">
                </div>
                <div class="vodcast-row">
                    <label>Format</label>
                    <select class="form-select" id="vodcast-format" style="flex:1">
                        <option value="webm">WebM (VP9+Opus, native)</option>
                        <option value="mp4">MP4 (H264+AAC, transcoded)</option>
                    </select>
                </div>
                <div class="vodcast-row">
                    <label>Audio Src</label>
                    <select class="form-select" id="vodcast-audio-slot" style="flex:1">
                        <option value="0">No Audio</option>
                        <option value="1">Encoder Slot 1</option>
                        <option value="2">Encoder Slot 2</option>
                        <option value="3">Encoder Slot 3</option>
                    </select>
                </div>
                <div style="margin-top:10px;display:flex;gap:8px">
                    <button class="vodcast-rec-btn" id="vodcast-start-btn" onclick="startVodcast()">
                        <i class="fa-solid fa-circle"></i> Start Recording
                    </button>
                    <button class="vodcast-rec-btn active" id="vodcast-stop-btn" onclick="stopVodcast()" style="display:none">
                        <i class="fa-solid fa-stop"></i> Stop &amp; Create Episode
                    </button>
                </div>
            </div>
            <div>
                <div style="font-size:11px;font-weight:700;text-transform:uppercase;letter-spacing:.04em;color:#a855f7;margin-bottom:8px">
                    Recording Status
                </div>
                <div class="vodcast-stats">
                    <div><span class="vl">Status:</span></div>
                    <div><span class="vv" id="vodcast-status">Idle</span></div>
                    <div></div>
                    <div><span class="vl">Duration:</span></div>
                    <div><span class="vv" id="vodcast-duration">00:00:00</span></div>
                    <div></div>
                    <div><span class="vl">Uploaded:</span></div>
                    <div><span class="vv" id="vodcast-bytes">0 MB</span></div>
                    <div></div>
                    <div><span class="vl">Chunks:</span></div>
                    <div><span class="vv" id="vodcast-chunks">0</span></div>
                    <div></div>
                    <div><span class="vl">Format:</span></div>
                    <div><span class="vv" id="vodcast-fmt">--</span></div>
                    <div></div>
                </div>
                <div id="vodcast-result" style="display:none;margin-top:10px;padding:10px;background:rgba(16,185,129,.08);border-radius:var(--radius-sm);border:1px solid rgba(16,185,129,.2)">
                    <div style="font-size:12px;font-weight:600;color:var(--teal);margin-bottom:6px"><i class="fa-solid fa-check"></i> Episode Created</div>
                    <div style="font-size:11px;color:var(--text-dim)" id="vodcast-result-info"></div>
                    <div style="margin-top:8px;display:flex;gap:6px">
                        <button class="btn btn-secondary btn-xs" id="vodcast-thumb-btn" onclick="showThumbnailPicker()">
                            <i class="fa-solid fa-image"></i> Set Thumbnail
                        </button>
                        <a class="btn btn-secondary btn-xs" id="vodcast-edit-link" href="#" target="_blank">
                            <i class="fa-solid fa-pen"></i> Edit Episode
                        </a>
                    </div>
                </div>
            </div>
        </div>
    </div>
</div>

<!-- Vodcast Thumbnail Modal -->
<div id="vodcast-thumb-modal" style="display:none;position:fixed;inset:0;background:rgba(0,0,0,.6);z-index:500;align-items:center;justify-content:center">
    <div style="background:var(--bg2);border:1px solid var(--border);border-radius:var(--radius);padding:24px;width:440px;max-width:95vw">
        <div style="font-size:15px;font-weight:700;color:var(--text);margin-bottom:14px;display:flex;align-items:center;gap:8px">
            <i class="fa-solid fa-image" style="color:#a855f7"></i> Extract Thumbnail
        </div>
        <div style="margin-bottom:12px">
            <label style="font-size:12px;color:var(--text-dim);display:block;margin-bottom:4px">Timestamp (HH:MM:SS or seconds)</label>
            <input type="text" class="form-input" id="vodcast-thumb-time" value="00:00:30" placeholder="00:00:30" style="width:160px">
        </div>
        <button class="btn btn-primary btn-sm" onclick="extractThumbnail()"><i class="fa-solid fa-camera"></i> Extract Frame</button>
        <div id="vodcast-thumb-preview" style="margin-top:10px"></div>
        <div style="display:flex;gap:8px;justify-content:flex-end;margin-top:14px">
            <button class="btn btn-secondary" onclick="closeThumbnailModal()">Close</button>
        </div>
    </div>
</div>

<!-- Switcher Panels: Chroma Key, PIP, Color Correction -->
<div class="switcher-panels">
    <!-- Chroma Key Panel -->
    <div class="sw-panel">
        <div class="sec-title"><i class="fa-solid fa-eye-dropper"></i> Chroma Key</div>
        <div class="sw-toggle">
            <input type="checkbox" id="chroma-enable" onchange="updateChromaKey()">
            <label for="chroma-enable">Enable chroma key</label>
        </div>
        <div class="sw-row">
            <label>Source</label>
            <select class="form-select" id="chroma-source" onchange="updateChromaKey()" style="flex:1;font-size:11px;padding:3px 6px">
                <option value="0">Source 1</option>
                <option value="1">Source 2</option>
                <option value="2">Source 3</option>
                <option value="3">Source 4</option>
                <option value="4">Source 5</option>
                <option value="5">Source 6</option>
            </select>
        </div>
        <div class="sw-row">
            <label>Key Color</label>
            <input type="color" class="sm-color" id="chroma-color" value="#00ff00" onchange="updateChromaKey()">
            <span class="val-label" id="chroma-color-label">#00ff00</span>
        </div>
        <div class="sw-row">
            <label>Tolerance</label>
            <input type="range" id="chroma-tolerance" min="0" max="100" value="30" oninput="updateChromaKey()">
            <span class="val-label" id="chroma-tol-val">0.30</span>
        </div>
        <div class="sw-row">
            <label>Softness</label>
            <input type="range" id="chroma-softness" min="0" max="50" value="10" oninput="updateChromaKey()">
            <span class="val-label" id="chroma-soft-val">0.10</span>
        </div>
    </div>

    <!-- PIP Panel -->
    <div class="sw-panel">
        <div class="sec-title"><i class="fa-solid fa-clone"></i> Picture-in-Picture</div>
        <div class="sw-toggle">
            <input type="checkbox" id="pip-enable" onchange="updatePIP()">
            <label for="pip-enable">Enable PIP overlay</label>
        </div>
        <div class="sw-row">
            <label>Overlay Src</label>
            <select class="form-select" id="pip-source" onchange="updatePIP()" style="flex:1;font-size:11px;padding:3px 6px">
                <option value="0">Source 1</option>
                <option value="1">Source 2</option>
                <option value="2">Source 3</option>
                <option value="3">Source 4</option>
                <option value="4">Source 5</option>
                <option value="5">Source 6</option>
            </select>
        </div>
        <div class="sw-row">
            <label>Position</label>
            <select class="form-select" id="pip-position" onchange="updatePIP()" style="flex:1;font-size:11px;padding:3px 6px">
                <option value="tl">Top Left</option>
                <option value="tr">Top Right</option>
                <option value="bl">Bottom Left</option>
                <option value="br" selected>Bottom Right</option>
            </select>
        </div>
        <div class="sw-row">
            <label>Size</label>
            <select class="form-select" id="pip-size" onchange="updatePIP()" style="flex:1;font-size:11px;padding:3px 6px">
                <option value="25">25%</option>
                <option value="33" selected>33%</option>
                <option value="50">50%</option>
            </select>
        </div>
    </div>

    <!-- Color Correction Panel -->
    <div class="sw-panel">
        <div class="sec-title"><i class="fa-solid fa-palette"></i> Color Correction</div>
        <div class="sw-row">
            <label>Source</label>
            <select class="form-select" id="cc-source" onchange="loadColorCorrection()" style="flex:1;font-size:11px;padding:3px 6px">
                <option value="0">Source 1</option>
                <option value="1">Source 2</option>
                <option value="2">Source 3</option>
                <option value="3">Source 4</option>
                <option value="4">Source 5</option>
                <option value="5">Source 6</option>
            </select>
        </div>
        <div class="sw-row">
            <label>Brightness</label>
            <input type="range" id="cc-brightness" min="-100" max="100" value="0" oninput="updateColorCorrection()">
            <span class="val-label" id="cc-bri-val">0.00</span>
        </div>
        <div class="sw-row">
            <label>Contrast</label>
            <input type="range" id="cc-contrast" min="0" max="200" value="100" oninput="updateColorCorrection()">
            <span class="val-label" id="cc-con-val">1.00</span>
        </div>
        <div class="sw-row">
            <label>Saturation</label>
            <input type="range" id="cc-saturation" min="0" max="200" value="100" oninput="updateColorCorrection()">
            <span class="val-label" id="cc-sat-val">1.00</span>
        </div>
        <div class="sw-row">
            <label>Hue</label>
            <input type="range" id="cc-hue" min="-314" max="314" value="0" oninput="updateColorCorrection()">
            <span class="val-label" id="cc-hue-val">0.00</span>
        </div>
        <div style="margin-top:6px">
            <button class="btn btn-secondary btn-xs" onclick="resetColorCorrection()"><i class="fa-solid fa-undo"></i> Reset</button>
        </div>
    </div>

    <!-- Lower Third Panel -->
    <div class="sw-panel lt-panel">
        <div class="sec-title"><i class="fa-solid fa-closed-captioning"></i> Lower Third Overlay</div>
        <div class="lt-row">
            <label>Text</label>
            <input type="text" class="form-input" id="lt-text" placeholder="Enter overlay text..." style="flex:1">
        </div>
        <div class="lt-row">
            <label>Font Size</label>
            <input type="number" class="form-input" id="lt-fontsize" value="36" min="12" max="72" step="2" style="width:60px">
            <label style="width:auto;margin-left:12px">BG Color</label>
            <input type="color" class="sm-color" id="lt-bgcolor" value="#0f172a">
            <label style="width:auto;margin-left:12px">Text Color</label>
            <input type="color" class="sm-color" id="lt-textcolor" value="#ffffff">
            <label style="width:auto;margin-left:12px">Accent</label>
            <input type="color" class="sm-color" id="lt-accentcolor" value="#14b8a6">
        </div>
        <div class="lt-row">
            <label>Duration</label>
            <input type="number" class="form-input" id="lt-duration" value="5000" min="0" max="60000" step="500" style="width:80px">
            <span style="font-size:10px;color:var(--muted);margin-left:4px">ms (0=manual hide)</span>
            <div style="flex:1"></div>
            <button class="auto-btn" onclick="showLowerThird()" style="padding:4px 12px;font-size:11px"><i class="fa-solid fa-play"></i> Show</button>
            <button class="btn btn-secondary btn-xs" onclick="hideLowerThird()"><i class="fa-solid fa-stop"></i> Hide</button>
        </div>
    </div>
</div>

<!-- Overlays Panel (collapsible) -->
<div class="overlays-panel" id="overlays-panel">
    <div class="sec-title" onclick="toggleOverlaysPanel()" style="cursor:pointer">
        <i class="fa-solid fa-layer-group"></i> Overlays &amp; Graphics
        <i class="fa-solid fa-chevron-down" id="overlays-chevron" style="margin-left:auto;font-size:10px;transition:transform .2s"></i>
    </div>
    <div id="overlays-body">
        <!-- Logo / Watermark -->
        <div class="overlay-section">
            <div class="sub-title"><i class="fa-solid fa-image" style="margin-right:4px"></i> Logo / Watermark</div>
            <div class="sw-row">
                <label style="width:72px">Image</label>
                <input type="file" id="logo-file" accept="image/png,image/gif,image/webp,image/svg+xml" style="font-size:11px;flex:1"
                       onchange="onLogoFileChange()">
            </div>
            <div class="sw-row">
                <label style="width:72px">Or URL</label>
                <input type="text" class="form-input" id="logo-url" placeholder="https://..." style="flex:1;font-size:11px;padding:3px 6px">
                <button class="btn btn-secondary btn-xs" onclick="loadLogoFromURL()">Load</button>
            </div>
            <div class="sw-row">
                <label style="width:72px">Position</label>
                <select class="form-select" id="logo-position" style="flex:1;font-size:11px;padding:3px 6px" onchange="updateLogoOverlay()">
                    <option value="tl">Top Left</option>
                    <option value="tr" selected>Top Right</option>
                    <option value="bl">Bottom Left</option>
                    <option value="br">Bottom Right</option>
                    <option value="center">Center</option>
                </select>
            </div>
            <div class="sw-row">
                <label style="width:72px">Opacity</label>
                <input type="range" id="logo-opacity" min="0" max="100" value="100" oninput="updateLogoOverlay()">
                <span class="val-label" id="logo-opacity-val">1.00</span>
            </div>
            <div class="sw-row">
                <label style="width:72px">Scale</label>
                <input type="range" id="logo-scale" min="10" max="200" value="100" oninput="updateLogoOverlay()">
                <span class="val-label" id="logo-scale-val">1.00</span>
            </div>
            <div style="display:flex;gap:6px;margin-top:6px">
                <button class="auto-btn" onclick="showLogoOverlay()" style="padding:4px 10px;font-size:11px"><i class="fa-solid fa-eye"></i> Show</button>
                <button class="btn btn-secondary btn-xs" onclick="hideLogoOverlay()"><i class="fa-solid fa-eye-slash"></i> Hide</button>
            </div>
        </div>

        <!-- Text Crawl (News Ticker) -->
        <div class="overlay-section">
            <div class="sub-title"><i class="fa-solid fa-text-width" style="margin-right:4px"></i> Text Crawl (Ticker)</div>
            <div class="sw-row">
                <label style="width:72px">Text</label>
                <input type="text" class="form-input" id="crawl-text" placeholder="Breaking news ticker text..." style="flex:1;font-size:11px;padding:3px 6px">
            </div>
            <div class="sw-row">
                <label style="width:72px">Speed</label>
                <input type="range" id="crawl-speed" min="1" max="10" value="3" oninput="updateCrawlLabel()">
                <span class="val-label" id="crawl-speed-val">3</span>
            </div>
            <div class="sw-row">
                <label style="width:72px">Font Size</label>
                <input type="number" class="form-input" id="crawl-fontsize" value="28" min="12" max="48" step="2" style="width:60px;font-size:11px;padding:3px 6px">
                <label style="width:auto;margin-left:8px">BG</label>
                <input type="color" class="sm-color" id="crawl-bgcolor" value="#0f172a">
                <label style="width:auto;margin-left:8px">Text</label>
                <input type="color" class="sm-color" id="crawl-textcolor" value="#ffffff">
            </div>
            <div style="display:flex;gap:6px;margin-top:6px">
                <button class="auto-btn" onclick="showTextCrawl()" style="padding:4px 10px;font-size:11px"><i class="fa-solid fa-play"></i> Start</button>
                <button class="btn btn-secondary btn-xs" onclick="hideTextCrawl()"><i class="fa-solid fa-stop"></i> Stop</button>
            </div>
        </div>

        <!-- Timer / Clock -->
        <div class="overlay-section">
            <div class="sub-title"><i class="fa-solid fa-clock" style="margin-right:4px"></i> Timer / Clock</div>
            <div class="sw-row">
                <label style="width:72px">Mode</label>
                <select class="form-select" id="timer-mode" style="flex:1;font-size:11px;padding:3px 6px" onchange="onTimerModeChange()">
                    <option value="clock">Real-Time Clock</option>
                    <option value="timer">Elapsed Timer</option>
                    <option value="countdown">Countdown</option>
                </select>
            </div>
            <div class="sw-row">
                <label style="width:72px">Format</label>
                <select class="form-select" id="timer-format" style="flex:1;font-size:11px;padding:3px 6px">
                    <option value="HH:MM:SS">HH:MM:SS</option>
                    <option value="MM:SS">MM:SS</option>
                </select>
            </div>
            <div class="sw-row" id="timer-countdown-row" style="display:none">
                <label style="width:72px">Start At</label>
                <input type="number" class="form-input" id="timer-countdown-sec" value="300" min="1" max="86400" style="width:80px;font-size:11px;padding:3px 6px">
                <span style="font-size:10px;color:var(--muted);margin-left:4px">seconds</span>
            </div>
            <div class="sw-row">
                <label style="width:72px">Position</label>
                <select class="form-select" id="timer-position" style="flex:1;font-size:11px;padding:3px 6px">
                    <option value="tl">Top Left</option>
                    <option value="tr" selected>Top Right</option>
                    <option value="bl">Bottom Left</option>
                    <option value="br">Bottom Right</option>
                </select>
            </div>
            <div style="display:flex;gap:6px;margin-top:6px">
                <button class="auto-btn" onclick="showTimerOverlay()" style="padding:4px 10px;font-size:11px"><i class="fa-solid fa-play"></i> Show</button>
                <button class="btn btn-secondary btn-xs" onclick="hideTimerOverlay()"><i class="fa-solid fa-stop"></i> Hide</button>
            </div>
        </div>

        <!-- Stinger Transition -->
        <div class="overlay-section">
            <div class="sub-title"><i class="fa-solid fa-wand-magic-sparkles" style="margin-right:4px"></i> Stinger Transition</div>
            <div class="sw-row">
                <label style="width:72px">Clip</label>
                <input type="file" id="stinger-file" accept="video/mp4,video/webm,video/quicktime" style="font-size:11px;flex:1"
                       onchange="onStingerFileChange()">
            </div>
            <div class="sw-row">
                <label style="width:72px">Status</label>
                <span id="stinger-status" style="font-size:11px;color:var(--muted)">No stinger loaded</span>
            </div>
        </div>
    </div>
</div>

<!-- Scene Presets Bar (1-8) -->
<div class="scene-presets-bar">
    <span style="font-size:11px;font-weight:700;color:var(--muted);text-transform:uppercase;letter-spacing:.04em;margin-right:8px;white-space:nowrap">
        <i class="fa-solid fa-bookmark" style="margin-right:4px"></i>Scenes
    </span>
    <?php for ($i = 1; $i <= 8; $i++): ?>
    <button class="scene-preset-btn" id="scene-btn-<?= $i ?>"
            onclick="recallScenePreset(<?= $i - 1 ?>)"
            oncontextmenu="saveScenePreset(<?= $i - 1 ?>, event); return false;"
            ondblclick="renameScenePreset(<?= $i - 1 ?>)"
            title="Click=recall, Right-click=save, Dbl-click=rename">
        <span><?= $i ?></span>
    </button>
    <?php endfor; ?>
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
<script src="/js/captions-engine.js"></script>

<script>
/* -- Producer page controller ---------------------------------------- */

var producer = null;
var filePickerSlot = -1;
var videoFiles = [];
var recStartTime = 0;
var recTimerInterval = null;
var NUM_SOURCES = 6;
var TRANS_TYPES = ['cut','fade','dissolve','wipe_left','wipe_right','wipe_circle','zoom','slide','stinger'];

/* -- Initialization -------------------------------------------------- */

function initProducer() {
    var canvases = [];
    for (var i = 0; i < NUM_SOURCES; i++) {
        canvases.push(document.getElementById('preview-' + i));
    }
    var pgmCanvas = document.getElementById('program-canvas');

    producer = new Mc1VideoProducer.VideoProducer({
        sourceCanvases: canvases,
        programCanvas: pgmCanvas
    });

    refreshCameras();
    refreshAudioDevices();

    producer.onDeviceChange(function(devices) {
        populateCameraSelects(devices);
    });

    for (var j = 0; j < NUM_SOURCES; j++) {
        onTypeChange(j);
    }

    initTbar();
    startTallyUpdate();
}

/* -- Camera enumeration ---------------------------------------------- */

function refreshCameras() {
    producer.enumerateVideoDevices().then(function(devices) {
        populateCameraSelects(devices);
        storeDeviceInfo(devices);
    }).catch(function(e) {
        console.warn('Camera enumeration failed:', e);
    });
}

function populateCameraSelects(devices) {
    for (var i = 0; i < NUM_SOURCES; i++) {
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

/* -- Source type switching -------------------------------------------- */

function onTypeChange(slot) {
    var type = document.getElementById('type-' + slot).value;
    var src = producer.getSource(slot);

    var panels = ['webcam', 'file', 'library'];
    for (var i = 0; i < panels.length; i++) {
        var el = document.getElementById('ctrl-' + panels[i] + '-' + slot);
        if (el) el.style.display = (panels[i] === type) ? '' : 'none';
    }

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

/* -- Category (media library) ---------------------------------------- */

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

/* -- File picker ----------------------------------------------------- */

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

/* -- PGM / PVW bus control ------------------------------------------- */

function setPGM(idx) {
    producer.setPGM(idx);
    updateBadges();
    updateProgramInfo();
}

function setPVW(idx) {
    producer.setPVW(idx);
    updateBadges();
    updateProgramInfo();
}

function doCut() {
    producer.cut();
    updateBadges();
    updateProgramInfo();
    updateTbarUI(0);
}

function doAutoTransition() {
    producer.autoTransition();
    // The transition will complete via animation; badges update in render loop
    // Poll to update badges during transition
    var poll = setInterval(function() {
        updateBadges();
        updateProgramInfo();
        if (!producer._transitioning) {
            clearInterval(poll);
            updateTbarUI(0);
        }
    }, 50);
}

/* Legacy compat */
function takeSource(idx) {
    producer.setActiveSource(idx);
    updateBadges();
    updateProgramInfo();
}

function updateBadges() {
    for (var i = 0; i < NUM_SOURCES; i++) {
        var badge = document.getElementById('badge-' + i);
        var slot = document.getElementById('source-slot-' + i);
        var src = producer.getSource(i);
        if (!badge || !slot) continue;

        slot.classList.remove('pgm-slot', 'pvw-slot');

        if (i === producer.pgmSourceIdx) {
            badge.className = 'source-badge pgm-badge';
            badge.textContent = 'PGM';
            slot.classList.add('pgm-slot');
        } else if (i === producer.pvwSourceIdx) {
            badge.className = 'source-badge pvw-badge';
            badge.textContent = 'PVW';
            slot.classList.add('pvw-slot');
        } else if (src && src.type !== 'none') {
            badge.className = 'source-badge off-badge';
            badge.textContent = 'RDY';
        } else {
            badge.className = 'source-badge off-badge';
            badge.textContent = 'OFF';
        }
    }
}

function updateProgramInfo() {
    var pgmBadge = document.getElementById('pgm-badge');
    var pgmLabel = document.getElementById('pgm-live-label');
    var pgmSrc   = document.getElementById('pgm-source-label');
    if (producer.pgmSourceIdx >= 0) {
        var src = producer.getSource(producer.pgmSourceIdx);
        pgmBadge.className = 'source-badge pgm-badge';
        pgmBadge.textContent = 'LIVE';
        pgmLabel.style.display = '';
        var types = { webcam: 'Camera', file: 'Video File', library: 'Media Library', none: 'None' };
        pgmSrc.textContent = 'Source ' + (producer.pgmSourceIdx + 1) + ' - ' + (types[src.type] || 'Unknown');
    } else {
        pgmBadge.className = 'source-badge off-badge';
        pgmBadge.textContent = 'IDLE';
        pgmLabel.style.display = 'none';
        pgmSrc.textContent = 'No active source';
    }
}

/* -- Transitions ----------------------------------------------------- */

function setTransition(type) {
    producer.setTransition(type);
    for (var i = 0; i < TRANS_TYPES.length; i++) {
        var btn = document.getElementById('trans-' + TRANS_TYPES[i]);
        if (btn) btn.classList.toggle('active', TRANS_TYPES[i] === type);
    }
}

function setTransitionDuration() {
    var val = parseInt(document.getElementById('trans-duration').value) || 500;
    producer.setTransition(producer.transitionType, val);
}

/* -- T-bar crossfader ------------------------------------------------ */

var tbarDragging = false;

function initTbar() {
    var track = document.getElementById('tbar-track');
    var handle = document.getElementById('tbar-handle');

    track.addEventListener('mousedown', function(e) {
        if (!producer.tbarMode) return;
        tbarDragging = true;
        updateTbarFromMouse(e, track);
    });

    document.addEventListener('mousemove', function(e) {
        if (!tbarDragging) return;
        updateTbarFromMouse(e, track);
    });

    document.addEventListener('mouseup', function() {
        tbarDragging = false;
    });

    // Touch support
    track.addEventListener('touchstart', function(e) {
        if (!producer.tbarMode) return;
        tbarDragging = true;
        updateTbarFromTouch(e, track);
    }, { passive: true });

    document.addEventListener('touchmove', function(e) {
        if (!tbarDragging) return;
        updateTbarFromTouch(e, track);
    }, { passive: true });

    document.addEventListener('touchend', function() {
        tbarDragging = false;
    });
}

function updateTbarFromMouse(e, track) {
    var rect = track.getBoundingClientRect();
    var x = (e.clientX - rect.left) / rect.width;
    x = Math.max(0, Math.min(1, x));
    producer.setTbarValue(x);
    updateTbarUI(x);
}

function updateTbarFromTouch(e, track) {
    if (!e.touches.length) return;
    var rect = track.getBoundingClientRect();
    var x = (e.touches[0].clientX - rect.left) / rect.width;
    x = Math.max(0, Math.min(1, x));
    producer.setTbarValue(x);
    updateTbarUI(x);
}

function updateTbarUI(value) {
    var pct = (value * 100).toFixed(1) + '%';
    document.getElementById('tbar-fill').style.width = pct;
    document.getElementById('tbar-handle').style.left = 'calc(' + pct + ' - 4px)';
}

function toggleTbarMode() {
    var manual = document.getElementById('tbar-manual').checked;
    producer.setTbarMode(manual);
    if (!manual) {
        updateTbarUI(0);
    }
}

/* -- Chroma Key ------------------------------------------------------ */

function updateChromaKey() {
    var enabled = document.getElementById('chroma-enable').checked;
    var srcIdx = parseInt(document.getElementById('chroma-source').value);
    var colorHex = document.getElementById('chroma-color').value;
    var tol = parseInt(document.getElementById('chroma-tolerance').value) / 100;
    var soft = parseInt(document.getElementById('chroma-softness').value) / 100;

    document.getElementById('chroma-color-label').textContent = colorHex;
    document.getElementById('chroma-tol-val').textContent = tol.toFixed(2);
    document.getElementById('chroma-soft-val').textContent = soft.toFixed(2);

    // Parse hex color to RGB 0-1
    var r = parseInt(colorHex.substring(1, 3), 16) / 255;
    var g = parseInt(colorHex.substring(3, 5), 16) / 255;
    var b = parseInt(colorHex.substring(5, 7), 16) / 255;

    var src = producer.getSource(srcIdx);
    if (src) {
        src.setChromaKey(enabled, [r, g, b], tol, soft);
    }
}

/* -- PIP ------------------------------------------------------------- */

function updatePIP() {
    var enabled = document.getElementById('pip-enable').checked;
    var srcIdx = parseInt(document.getElementById('pip-source').value);
    var position = document.getElementById('pip-position').value;
    var size = document.getElementById('pip-size').value;

    producer.setPIP(enabled, srcIdx, position, size);
}

/* -- Color Correction ------------------------------------------------ */

function loadColorCorrection() {
    var srcIdx = parseInt(document.getElementById('cc-source').value);
    var src = producer.getSource(srcIdx);
    if (!src) return;
    var cc = src.colorCorrection;
    document.getElementById('cc-brightness').value = Math.round(cc.brightness * 100);
    document.getElementById('cc-contrast').value = Math.round(cc.contrast * 100);
    document.getElementById('cc-saturation').value = Math.round(cc.saturation * 100);
    document.getElementById('cc-hue').value = Math.round(cc.hue * 100);
    updateCCLabels();
}

function updateColorCorrection() {
    var srcIdx = parseInt(document.getElementById('cc-source').value);
    var src = producer.getSource(srcIdx);
    if (!src) return;

    var bri = parseInt(document.getElementById('cc-brightness').value) / 100;
    var con = parseInt(document.getElementById('cc-contrast').value) / 100;
    var sat = parseInt(document.getElementById('cc-saturation').value) / 100;
    var hue = parseInt(document.getElementById('cc-hue').value) / 100;

    src.setColorCorrection('brightness', bri);
    src.setColorCorrection('contrast', con);
    src.setColorCorrection('saturation', sat);
    src.setColorCorrection('hue', hue);

    updateCCLabels();
}

function updateCCLabels() {
    var bri = parseInt(document.getElementById('cc-brightness').value) / 100;
    var con = parseInt(document.getElementById('cc-contrast').value) / 100;
    var sat = parseInt(document.getElementById('cc-saturation').value) / 100;
    var hue = parseInt(document.getElementById('cc-hue').value) / 100;
    document.getElementById('cc-bri-val').textContent = bri.toFixed(2);
    document.getElementById('cc-con-val').textContent = con.toFixed(2);
    document.getElementById('cc-sat-val').textContent = sat.toFixed(2);
    document.getElementById('cc-hue-val').textContent = hue.toFixed(2);
}

function resetColorCorrection() {
    var srcIdx = parseInt(document.getElementById('cc-source').value);
    var src = producer.getSource(srcIdx);
    if (!src) return;
    src.setColorCorrection('brightness', 0.0);
    src.setColorCorrection('contrast', 1.0);
    src.setColorCorrection('saturation', 1.0);
    src.setColorCorrection('hue', 0.0);
    document.getElementById('cc-brightness').value = 0;
    document.getElementById('cc-contrast').value = 100;
    document.getElementById('cc-saturation').value = 100;
    document.getElementById('cc-hue').value = 0;
    updateCCLabels();
}

/* -- Lower Third ----------------------------------------------------- */

function showLowerThird() {
    var text = document.getElementById('lt-text').value;
    if (!text) { mc1Toast('Enter overlay text', 'warn'); return; }

    var fontSize = parseInt(document.getElementById('lt-fontsize').value) || 36;
    var bgHex = document.getElementById('lt-bgcolor').value;
    var textColor = document.getElementById('lt-textcolor').value;
    var accentColor = document.getElementById('lt-accentcolor').value;
    var duration = parseInt(document.getElementById('lt-duration').value) || 0;

    // Convert bg hex to rgba
    var br = parseInt(bgHex.substring(1, 3), 16);
    var bg = parseInt(bgHex.substring(3, 5), 16);
    var bb = parseInt(bgHex.substring(5, 7), 16);
    var bgRgba = 'rgba(' + br + ',' + bg + ',' + bb + ',0.85)';

    producer.showLowerThird(text, {
        fontSize: fontSize,
        textColor: textColor,
        bgColor: bgRgba,
        accentColor: accentColor,
        holdDuration: duration
    });
    mc1Toast('Lower third shown');
}

function hideLowerThird() {
    producer.hideLowerThird();
}

/* -- Recording ------------------------------------------------------- */

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

/* -- Stream Engine --------------------------------------------------- */

var streamEngine = null;
var streamStatusTimer = null;
var streamTargets = [];

function toggleStreamPanel() {
    var panel = document.getElementById('stream-panel');
    if (panel.style.display === 'none') {
        panel.style.display = '';
        loadStreamTargets();
    } else {
        panel.style.display = 'none';
    }
}

function loadStreamTargets() {
    mc1Api('POST', '/app/api/rtmp.php', { action: 'list' }).then(function(d) {
        if (!d || !d.targets) return;
        streamTargets = d.targets.filter(function(t) { return t.video_enabled; });
        var sel = document.getElementById('stream-target');
        var current = sel.value;
        sel.innerHTML = '<option value="">-- Select RTMP Target --</option>';
        for (var i = 0; i < streamTargets.length; i++) {
            var t = streamTargets[i];
            var opt = document.createElement('option');
            opt.value = t.id;
            opt.textContent = t.name + ' (' + t.platform + ') - Slot ' + t.slot_id;
            sel.appendChild(opt);
        }
        if (current) sel.value = current;
    }).catch(function() {
        mc1Toast('Failed to load RTMP targets', 'err');
    });
}

function onStreamTargetChange() {
    var sel = document.getElementById('stream-target');
    var tid = parseInt(sel.value);
    var label = document.getElementById('stream-target-label');
    if (!tid) {
        label.textContent = '--';
        return;
    }
    for (var i = 0; i < streamTargets.length; i++) {
        if (streamTargets[i].id === tid) {
            label.textContent = streamTargets[i].name;
            return;
        }
    }
    label.textContent = '--';
}

function startVideoStream() {
    var targetId = parseInt(document.getElementById('stream-target').value);
    if (!targetId) {
        mc1Toast('Select an RTMP target first', 'warn');
        return;
    }

    var audioSlotId = parseInt(document.getElementById('stream-audio-slot').value) || 0;
    var codec = document.getElementById('stream-codec').value;
    var bitrate = parseInt(document.getElementById('stream-bitrate').value) || 2500000;

    if (!streamEngine) {
        streamEngine = new Mc1VideoProducer.StreamEngine(producer);
    }

    document.getElementById('stream-start-btn').disabled = true;
    document.getElementById('stream-status-label').textContent = 'Connecting...';
    document.getElementById('stream-status-label').style.color = 'var(--yellow)';

    streamEngine.startStreaming(targetId, {
        audioSlotId: audioSlotId,
        codec: codec,
        videoBitrate: bitrate
    }).then(function(d) {
        mc1Toast('Video stream started');
        document.getElementById('stream-start-btn').style.display = 'none';
        document.getElementById('stream-stop-btn').style.display = '';
        document.getElementById('stream-start-btn').disabled = false;
        document.getElementById('stream-status-label').textContent = 'LIVE';
        document.getElementById('stream-status-label').style.color = 'var(--red)';
        document.getElementById('stream-live-badge').style.display = '';
        document.getElementById('stream-server-status').textContent = 'PID ' + (d.pid || '?');
        document.getElementById('stream-server-status').style.color = 'var(--teal)';

        var btn = document.getElementById('stream-btn');
        btn.classList.add('streaming');
        btn.innerHTML = '<i class="fa-solid fa-tower-broadcast"></i> LIVE <i class="fa-solid fa-caret-down" style="font-size:10px;margin-left:2px"></i>';

        /* We start polling stream status */
        streamStatusTimer = setInterval(updateStreamStatus, 2000);
    }).catch(function(e) {
        mc1Toast('Stream start failed: ' + (e.message || e), 'err');
        document.getElementById('stream-start-btn').disabled = false;
        document.getElementById('stream-status-label').textContent = 'Error';
        document.getElementById('stream-status-label').style.color = 'var(--red)';
    });
}

function stopVideoStream() {
    if (!streamEngine) return;

    document.getElementById('stream-stop-btn').disabled = true;

    streamEngine.stopStreaming().then(function() {
        mc1Toast('Video stream stopped');
        resetStreamUI();
    }).catch(function(e) {
        mc1Toast('Stream stop error: ' + (e.message || e), 'err');
        resetStreamUI();
    });
}

function resetStreamUI() {
    if (streamStatusTimer) {
        clearInterval(streamStatusTimer);
        streamStatusTimer = null;
    }
    document.getElementById('stream-start-btn').style.display = '';
    document.getElementById('stream-start-btn').disabled = false;
    document.getElementById('stream-stop-btn').style.display = 'none';
    document.getElementById('stream-stop-btn').disabled = false;
    document.getElementById('stream-status-label').textContent = 'Idle';
    document.getElementById('stream-status-label').style.color = 'var(--text-dim)';
    document.getElementById('stream-duration-label').textContent = '00:00:00';
    document.getElementById('stream-bytes-label').textContent = '0 MB';
    document.getElementById('stream-chunks-label').textContent = '0';
    document.getElementById('stream-server-status').textContent = '--';
    document.getElementById('stream-server-status').style.color = 'var(--text-dim)';
    document.getElementById('stream-buffer-bar').style.width = '0%';
    document.getElementById('stream-live-badge').style.display = 'none';

    var btn = document.getElementById('stream-btn');
    btn.classList.remove('streaming');
    btn.innerHTML = '<i class="fa-solid fa-tower-broadcast"></i> Stream <i class="fa-solid fa-caret-down" style="font-size:10px;margin-left:2px"></i>';
}

function updateStreamStatus() {
    if (!streamEngine || !streamEngine.streaming) return;

    var st = streamEngine.getStreamStatus();

    /* We update duration */
    var elapsed = Math.floor(st.duration / 1000);
    var hh = Math.floor(elapsed / 3600);
    var mm = Math.floor((elapsed % 3600) / 60);
    var ss = elapsed % 60;
    var durStr = (hh < 10 ? '0' : '') + hh + ':' + (mm < 10 ? '0' : '') + mm + ':' + (ss < 10 ? '0' : '') + ss;
    document.getElementById('stream-duration-label').textContent = durStr;

    /* We update bytes */
    var mb = (st.bytesUploaded / 1048576).toFixed(1);
    document.getElementById('stream-bytes-label').textContent = mb + ' MB';

    /* We update chunk count */
    document.getElementById('stream-chunks-label').textContent = String(st.chunkIndex);

    /* We estimate buffer fullness as a fraction of the 2s chunk interval */
    var timeSinceLast = st.duration % 2000;
    var bufPct = Math.min(100, (timeSinceLast / 2000) * 100);
    document.getElementById('stream-buffer-bar').style.width = bufPct.toFixed(0) + '%';

    /* We also poll the server for ffmpeg process health */
    mc1Api('POST', '/app/api/producer.php', {
        action: 'stream_status',
        target_id: st.targetId
    }).then(function(d) {
        if (d && d.targets && d.targets.length > 0) {
            var t = d.targets[0];
            if (t.relay_running) {
                document.getElementById('stream-server-status').textContent = 'PID ' + t.relay_pid;
                document.getElementById('stream-server-status').style.color = 'var(--teal)';
            } else {
                document.getElementById('stream-server-status').textContent = 'Process Died';
                document.getElementById('stream-server-status').style.color = 'var(--red)';
                if (t.error_message) {
                    mc1Toast('Stream relay died: ' + t.error_message.substring(0, 100), 'err');
                }
            }
        }
    }).catch(function() {});
}

/* -- Vodcast Recording ----------------------------------------------- */

var vodcastRecorder = null;
var vodcastStatusTimer = null;
var vodcastEpisodeId = 0;

function toggleVodcastPanel() {
    var panel = document.getElementById('vodcast-panel');
    if (panel.style.display === 'none') {
        panel.style.display = '';
        loadVodcastShows();
    } else {
        panel.style.display = 'none';
    }
}

function loadVodcastShows() {
    mc1Api('POST', '/app/api/podcast.php', { action: 'list_shows' }).then(function(d) {
        if (!d || !d.ok || !d.shows) return;
        var sel = document.getElementById('vodcast-show');
        var current = sel.value;
        sel.innerHTML = '<option value="">-- Select Podcast Show --</option>';
        d.shows.forEach(function(s) {
            var opt = document.createElement('option');
            opt.value = s.id;
            opt.textContent = s.title;
            sel.appendChild(opt);
        });
        if (current) sel.value = current;
    }).catch(function() {});
}

function startVodcast() {
    var showId = parseInt(document.getElementById('vodcast-show').value);
    var title = document.getElementById('vodcast-title').value.trim();
    var format = document.getElementById('vodcast-format').value;
    var audioSlot = parseInt(document.getElementById('vodcast-audio-slot').value) || 0;

    if (!showId) { mc1Toast('Select a podcast show first', 'warn'); return; }
    if (!title) { mc1Toast('Enter an episode title', 'warn'); return; }

    if (!vodcastRecorder) {
        vodcastRecorder = new Mc1VideoProducer.VodcastRecorder(producer);
    }

    document.getElementById('vodcast-start-btn').disabled = true;
    document.getElementById('vodcast-status').textContent = 'Starting...';
    document.getElementById('vodcast-status').style.color = 'var(--yellow)';
    document.getElementById('vodcast-result').style.display = 'none';

    vodcastRecorder.startVodcastRecording(showId, title, format, {
        audioSlotId: audioSlot
    }).then(function(vodcastId) {
        mc1Toast('Vodcast recording started');
        document.getElementById('vodcast-start-btn').style.display = 'none';
        document.getElementById('vodcast-stop-btn').style.display = '';
        document.getElementById('vodcast-start-btn').disabled = false;
        document.getElementById('vodcast-status').textContent = 'Recording';
        document.getElementById('vodcast-status').style.color = 'var(--red)';
        document.getElementById('vodcast-fmt').textContent = format.toUpperCase();

        var btn = document.getElementById('vodcast-btn');
        btn.classList.add('active');
        btn.innerHTML = '<i class="fa-solid fa-video"></i> REC <i class="fa-solid fa-caret-down" style="font-size:10px;margin-left:2px"></i>';

        vodcastStatusTimer = setInterval(updateVodcastStatus, 1000);
    }).catch(function(e) {
        mc1Toast('Vodcast start failed: ' + (e.message || e), 'err');
        document.getElementById('vodcast-start-btn').disabled = false;
        document.getElementById('vodcast-status').textContent = 'Error';
        document.getElementById('vodcast-status').style.color = 'var(--red)';
    });
}

function stopVodcast() {
    if (!vodcastRecorder || !vodcastRecorder.recording) return;

    document.getElementById('vodcast-stop-btn').disabled = true;
    document.getElementById('vodcast-status').textContent = 'Finalizing...';
    document.getElementById('vodcast-status').style.color = 'var(--yellow)';

    vodcastRecorder.stopVodcastRecording().then(function(d) {
        if (vodcastStatusTimer) {
            clearInterval(vodcastStatusTimer);
            vodcastStatusTimer = null;
        }

        document.getElementById('vodcast-start-btn').style.display = '';
        document.getElementById('vodcast-start-btn').disabled = false;
        document.getElementById('vodcast-stop-btn').style.display = 'none';
        document.getElementById('vodcast-stop-btn').disabled = false;

        var btn = document.getElementById('vodcast-btn');
        btn.classList.remove('active');
        btn.innerHTML = '<i class="fa-solid fa-video"></i> Vodcast <i class="fa-solid fa-caret-down" style="font-size:10px;margin-left:2px"></i>';

        if (d && d.ok) {
            vodcastEpisodeId = d.episode_id || 0;
            document.getElementById('vodcast-status').textContent = 'Complete';
            document.getElementById('vodcast-status').style.color = 'var(--teal)';

            document.getElementById('vodcast-result').style.display = '';
            var durSec = d.duration_sec || 0;
            var szBytes = d.file_size || 0;
            var durH = Math.floor(durSec / 3600), durM = Math.floor((durSec % 3600) / 60), durS = durSec % 60;
            var durStr = (durH > 0 ? durH + ':' : '') + (durM < 10 ? '0' : '') + durM + ':' + (durS < 10 ? '0' : '') + durS;
            var szStr = szBytes < 1048576 ? (szBytes / 1024).toFixed(1) + ' KB' : (szBytes / 1048576).toFixed(1) + ' MB';
            var info = 'Episode #' + vodcastEpisodeId
                     + ' &middot; ' + (d.format || 'webm').toUpperCase()
                     + ' &middot; ' + durStr
                     + ' &middot; ' + szStr;
            document.getElementById('vodcast-result-info').innerHTML = info;
            document.getElementById('vodcast-edit-link').href = '/podcast.php';

            mc1Toast('Vodcast episode created: ' + (d.filename || ''));
        } else {
            document.getElementById('vodcast-status').textContent = 'Error';
            document.getElementById('vodcast-status').style.color = 'var(--red)';
            mc1Toast('Vodcast stop error: ' + (d ? d.error : 'Unknown'), 'err');
        }
    });
}

function updateVodcastStatus() {
    if (!vodcastRecorder || !vodcastRecorder.recording) return;

    var st = vodcastRecorder.getRecordingStatus();
    var elapsed = Math.floor(st.duration / 1000);
    var hh = Math.floor(elapsed / 3600);
    var mm = Math.floor((elapsed % 3600) / 60);
    var ss = elapsed % 60;
    document.getElementById('vodcast-duration').textContent =
        (hh < 10 ? '0' : '') + hh + ':' + (mm < 10 ? '0' : '') + mm + ':' + (ss < 10 ? '0' : '') + ss;
    document.getElementById('vodcast-bytes').textContent = (st.bytesUploaded / 1048576).toFixed(1) + ' MB';
    document.getElementById('vodcast-chunks').textContent = String(st.chunkIndex);
}

/* -- Vodcast Thumbnail ----------------------------------------------- */

function showThumbnailPicker() {
    if (!vodcastEpisodeId) { mc1Toast('No episode created yet', 'warn'); return; }
    document.getElementById('vodcast-thumb-preview').innerHTML = '';
    document.getElementById('vodcast-thumb-modal').style.display = 'flex';
}

function closeThumbnailModal() {
    document.getElementById('vodcast-thumb-modal').style.display = 'none';
}

function extractThumbnail() {
    if (!vodcastEpisodeId) return;
    var timestamp = document.getElementById('vodcast-thumb-time').value || '00:00:30';

    document.getElementById('vodcast-thumb-preview').innerHTML = '<span class="spinner"></span> Extracting...';

    mc1Api('POST', '/app/api/producer.php', {
        action: 'get_vodcast_thumbnail',
        episode_id: vodcastEpisodeId,
        timestamp: timestamp
    }).then(function(d) {
        if (d && d.ok) {
            document.getElementById('vodcast-thumb-preview').innerHTML =
                '<div style="font-size:12px;color:var(--teal);margin-bottom:6px"><i class="fa-solid fa-check"></i> Thumbnail saved: '
                + esc(d.filename) + ' (' + (d.size < 1024 ? d.size + ' B' : (d.size / 1024).toFixed(1) + ' KB') + ')</div>';
            mc1Toast('Thumbnail extracted successfully');
        } else {
            document.getElementById('vodcast-thumb-preview').innerHTML =
                '<div style="font-size:12px;color:var(--red)">' + esc(d ? d.error : 'Failed') + '</div>';
        }
    }).catch(function() {
        document.getElementById('vodcast-thumb-preview').innerHTML =
            '<div style="font-size:12px;color:var(--red)">Network error</div>';
    });
}

/* -- Scene save/load ------------------------------------------------- */

function saveScene() {
    var sceneSel = document.getElementById('scene-select');
    var name = prompt('Scene name:', sceneSel.selectedOptions[0] ? sceneSel.selectedOptions[0].text : 'Default');
    if (!name) return;
    var config = producer.getSceneConfig();
    config.scene_name = name;

    mc1Api('PUT', '/api/v1/producer/scenes', config).then(function(d) {
        if (d && d.ok) {
            mc1Toast('Scene saved: ' + name);
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
            // Sync UI
            setTransition(d.scene.transition_type || 'cut');
            if (d.scene.transition_duration_ms) document.getElementById('trans-duration').value = d.scene.transition_duration_ms;
            if (d.scene.pip_enabled !== undefined) document.getElementById('pip-enable').checked = !!d.scene.pip_enabled;
            if (d.scene.pip_source !== undefined) document.getElementById('pip-source').value = d.scene.pip_source;
            if (d.scene.pip_position) document.getElementById('pip-position').value = d.scene.pip_position;
            if (d.scene.pip_size) document.getElementById('pip-size').value = d.scene.pip_size;
            updatePIP();
            updateBadges();
            updateProgramInfo();
            mc1Toast('Scene loaded');
        }
    }).catch(function() {
        mc1Toast('Failed to load scene', 'err');
    });
}

/* -- Audio Follows Video --------------------------------------------- */

function toggleAudioFollowsVideo() {
    var enabled = document.getElementById('afv-toggle').checked;
    producer.setAudioFollowsVideo(enabled);
    mc1Toast(enabled ? 'Audio follows video enabled' : 'Audio follows video disabled');
}

function refreshAudioDevices() {
    producer.enumerateAudioDevices().then(function(devices) {
        for (var i = 0; i < NUM_SOURCES; i++) {
            var sel = document.getElementById('audio-device-' + i);
            if (!sel) continue;
            sel.innerHTML = '<option value="">-- Audio Device --</option>';
            for (var j = 0; j < devices.length; j++) {
                var opt = document.createElement('option');
                opt.value = devices[j].deviceId;
                opt.textContent = devices[j].label || ('Mic ' + (j + 1));
                sel.appendChild(opt);
            }
            sel.onchange = (function(slot) {
                return function() { onAudioDeviceChange(slot); };
            })(i);
        }
    }).catch(function() {});
}

function onAudioDeviceChange(slot) {
    var sel = document.getElementById('audio-device-' + slot);
    if (!sel) return;
    var src = producer.getSource(slot);
    if (src) src.setAudioDevice(sel.value);
}

/* -- Tally Lights ---------------------------------------------------- */

var tallyTimer = null;

function startTallyUpdate() {
    tallyTimer = setInterval(updateTallyLights, 200);
}

function updateTallyLights() {
    if (!producer) return;
    var state = producer.getTallyState();
    for (var i = 0; i < state.length; i++) {
        var pgmEl = document.getElementById('tally-pgm-' + i);
        var pvwEl = document.getElementById('tally-pvw-' + i);
        if (pgmEl) pgmEl.style.display = state[i].pgm ? '' : 'none';
        if (pvwEl) pvwEl.style.display = state[i].pvw ? '' : 'none';
    }
}

/* -- Overlay Controls: Logo ------------------------------------------ */

function onLogoFileChange() {
    var input = document.getElementById('logo-file');
    if (!input.files || !input.files[0]) return;
    producer.loadLogoFile(input.files[0]).then(function() {
        mc1Toast('Logo loaded');
    }).catch(function() {
        mc1Toast('Failed to load logo image', 'err');
    });
}

function loadLogoFromURL() {
    var url = document.getElementById('logo-url').value.trim();
    if (!url) { mc1Toast('Enter a URL', 'warn'); return; }
    producer.loadLogoImage(url).then(function() {
        mc1Toast('Logo loaded from URL');
    }).catch(function() {
        mc1Toast('Failed to load logo from URL', 'err');
    });
}

function updateLogoOverlay() {
    var opacity = parseInt(document.getElementById('logo-opacity').value) / 100;
    var scale = parseInt(document.getElementById('logo-scale').value) / 100;
    var position = document.getElementById('logo-position').value;
    document.getElementById('logo-opacity-val').textContent = opacity.toFixed(2);
    document.getElementById('logo-scale-val').textContent = scale.toFixed(2);
    producer.logoOverlay.opacity = opacity;
    producer.logoOverlay.scale = scale;
    producer.logoOverlay.position = position;
    if (producer.logoOverlay.isVisible()) {
        producer.logoOverlay._render();
    }
}

function showLogoOverlay() {
    if (!producer.logoOverlay.loaded) {
        mc1Toast('Load a logo image first', 'warn');
        return;
    }
    var opacity = parseInt(document.getElementById('logo-opacity').value) / 100;
    var scale = parseInt(document.getElementById('logo-scale').value) / 100;
    var position = document.getElementById('logo-position').value;
    producer.showLogo({ opacity: opacity, scale: scale, position: position });
    mc1Toast('Logo overlay shown');
}

function hideLogoOverlay() {
    producer.hideLogo();
    mc1Toast('Logo overlay hidden');
}

/* -- Overlay Controls: Text Crawl ------------------------------------ */

function updateCrawlLabel() {
    document.getElementById('crawl-speed-val').textContent = document.getElementById('crawl-speed').value;
}

function showTextCrawl() {
    var text = document.getElementById('crawl-text').value;
    if (!text) { mc1Toast('Enter ticker text', 'warn'); return; }
    var speed = parseInt(document.getElementById('crawl-speed').value) || 3;
    var fontSize = parseInt(document.getElementById('crawl-fontsize').value) || 28;
    var bgHex = document.getElementById('crawl-bgcolor').value;
    var textColor = document.getElementById('crawl-textcolor').value;
    var br = parseInt(bgHex.substring(1, 3), 16);
    var bg = parseInt(bgHex.substring(3, 5), 16);
    var bb = parseInt(bgHex.substring(5, 7), 16);
    var bgRgba = 'rgba(' + br + ',' + bg + ',' + bb + ',0.85)';
    producer.showTextCrawl(text, {
        speed: speed,
        fontSize: fontSize,
        bgColor: bgRgba,
        textColor: textColor
    });
    mc1Toast('Text crawl started');
}

function hideTextCrawl() {
    producer.hideTextCrawl();
    mc1Toast('Text crawl stopped');
}

/* -- Overlay Controls: Timer / Clock --------------------------------- */

function onTimerModeChange() {
    var mode = document.getElementById('timer-mode').value;
    document.getElementById('timer-countdown-row').style.display = (mode === 'countdown') ? '' : 'none';
}

function showTimerOverlay() {
    var mode = document.getElementById('timer-mode').value;
    var format = document.getElementById('timer-format').value;
    var position = document.getElementById('timer-position').value;
    var opts = { mode: mode, format: format, position: position };
    if (mode === 'countdown') {
        opts.countdownSeconds = parseInt(document.getElementById('timer-countdown-sec').value) || 300;
    }
    producer.showTimer(opts);
    mc1Toast('Timer overlay shown');
}

function hideTimerOverlay() {
    producer.hideTimer();
    mc1Toast('Timer overlay hidden');
}

/* -- Overlay Controls: Stinger --------------------------------------- */

function onStingerFileChange() {
    var input = document.getElementById('stinger-file');
    if (!input.files || !input.files[0]) return;
    var status = document.getElementById('stinger-status');
    status.textContent = 'Loading...';
    status.style.color = 'var(--yellow)';
    producer.loadStingerFile(input.files[0]).then(function() {
        status.textContent = 'Stinger ready: ' + input.files[0].name;
        status.style.color = 'var(--teal)';
        mc1Toast('Stinger transition loaded');
    }).catch(function() {
        status.textContent = 'Failed to load stinger';
        status.style.color = 'var(--red)';
        mc1Toast('Failed to load stinger clip', 'err');
    });
}

/* -- Overlays Panel Toggle ------------------------------------------- */

function toggleOverlaysPanel() {
    var body = document.getElementById('overlays-body');
    var chevron = document.getElementById('overlays-chevron');
    if (body.style.display === 'none') {
        body.style.display = '';
        chevron.style.transform = '';
    } else {
        body.style.display = 'none';
        chevron.style.transform = 'rotate(-90deg)';
    }
}

/* -- Scene Presets (1-8 bar) ----------------------------------------- */

function recallScenePreset(idx) {
    var loaded = producer.loadScenePreset(idx);
    if (loaded) {
        updateBadges();
        updateProgramInfo();
        updateScenePresetButtons();
        mc1Toast('Scene ' + (idx + 1) + ' recalled');
        // Sync UI from loaded config
        setTransition(producer.transitionType);
        document.getElementById('trans-duration').value = producer.transitionDuration;
        document.getElementById('pip-enable').checked = producer.pip.enabled;
        document.getElementById('pip-source').value = producer.pip.sourceIdx;
        document.getElementById('pip-position').value = producer.pip.position;
        document.getElementById('pip-size').value = producer.pip.size;
        document.getElementById('afv-toggle').checked = producer.audioFollowsVideo;
        updatePIP();
    } else {
        mc1Toast('Scene ' + (idx + 1) + ' is empty -- right-click to save', 'warn');
    }
}

function saveScenePreset(idx, e) {
    if (e) e.preventDefault();
    producer.saveScenePreset(idx);
    updateScenePresetButtons();
    mc1Toast('Scene ' + (idx + 1) + ' saved');
}

function renameScenePreset(idx) {
    var current = producer.getScenePresetName(idx);
    var name = prompt('Rename scene preset ' + (idx + 1) + ':', current);
    if (name !== null && name.trim()) {
        producer.renameScenePreset(idx, name.trim());
        updateScenePresetButtons();
    }
}

function updateScenePresetButtons() {
    for (var i = 0; i < 8; i++) {
        var btn = document.getElementById('scene-btn-' + (i + 1));
        if (!btn) continue;
        if (producer.isScenePresetSaved(i)) {
            btn.classList.add('saved');
            btn.title = producer.getScenePresetName(i) + ' (Click=recall, Right-click=save, Dbl-click=rename)';
        } else {
            btn.classList.remove('saved');
            btn.title = 'Empty (Right-click to save current scene)';
        }
    }
}

/* -- Captions -------------------------------------------------------- */

var captionsEngine = null;
var ccOverlayCanvas = null;
var ccOverlayEnabled = false;

function initCaptions() {
    captionsEngine = new Mc1CaptionsEngine.CaptionsEngine({
        language: document.getElementById('cc-language').value || 'en',
        onCueAdded: function(cue, idx) {
            ccRefreshCueList();
        }
    });
    ccOverlayCanvas = document.createElement('canvas');
    ccOverlayCanvas.width = 1280;
    ccOverlayCanvas.height = 720;
}

function toggleCaptionsPanel() {
    var panel = document.getElementById('captions-panel');
    panel.style.display = panel.style.display === 'none' ? '' : 'none';
}

function ccStartAutoTranscribe() {
    if (!captionsEngine) initCaptions();
    captionsEngine.language = document.getElementById('cc-language').value || 'en';

    /* Grab audio from PGM source or use mic */
    var pgmSrc = producer && producer.pgmSourceIdx >= 0 ? producer.sources[producer.pgmSourceIdx] : null;
    var stream = (pgmSrc && pgmSrc.stream) ? pgmSrc.stream : null;

    if (!stream) {
        /* Fall back to default mic */
        navigator.mediaDevices.getUserMedia({ audio: true, video: false }).then(function(s) {
            captionsEngine.startLiveTranscription(s);
            ccShowLive(true);
        }).catch(function(e) {
            mc1Toast('Cannot access microphone: ' + e.message, 'err');
        });
        return;
    }

    captionsEngine.startLiveTranscription(stream);
    ccShowLive(true);
}

function ccStopAutoTranscribe() {
    if (captionsEngine) {
        captionsEngine.stopLiveTranscription();
    }
    ccShowLive(false);
}

function ccShowLive(on) {
    document.getElementById('cc-auto-btn').style.display = on ? 'none' : '';
    document.getElementById('cc-stop-btn').style.display = on ? '' : 'none';
    document.getElementById('cc-live-badge').style.display = on ? '' : 'none';
}

function ccImportFile() {
    document.getElementById('cc-import-input').click();
}

function ccHandleImport(input) {
    if (!input.files || !input.files[0]) return;
    if (!captionsEngine) initCaptions();

    var file = input.files[0];
    var reader = new FileReader();
    reader.onload = function() {
        var text = reader.result;
        if (file.name.toLowerCase().endsWith('.vtt')) {
            captionsEngine.loadVTT(text);
        } else {
            captionsEngine.loadSRT(text);
        }
        ccRefreshCueList();
        mc1Toast('Imported ' + captionsEngine.cues.length + ' caption cues');
    };
    reader.readAsText(file);
    input.value = '';
}

function ccExport(format) {
    if (!captionsEngine || captionsEngine.cues.length === 0) {
        mc1Toast('No captions to export', 'warn');
        return;
    }
    var text = format === 'vtt' ? captionsEngine.exportVTT() : captionsEngine.exportSRT();
    var blob = new Blob([text], { type: 'text/plain;charset=utf-8' });
    var url = URL.createObjectURL(blob);
    var a = document.createElement('a');
    a.href = url;
    a.download = 'captions.' + format;
    document.body.appendChild(a);
    a.click();
    setTimeout(function() { document.body.removeChild(a); URL.revokeObjectURL(url); }, 100);
}

function ccToggleOverlay() {
    ccOverlayEnabled = document.getElementById('cc-show-overlay').checked;
    if (!ccOverlayEnabled) {
        document.getElementById('cc-live-preview').style.display = 'none';
    }
}

function ccUpdateStyle() {
    var fs = document.getElementById('cc-fontsize').value;
    document.getElementById('cc-fontsize-val').textContent = fs;
    var op = document.getElementById('cc-bg-opacity').value;
    document.getElementById('cc-bg-opacity-val').textContent = op + '%';

    if (captionsEngine) {
        captionsEngine.style.fontSize = parseInt(fs);
        captionsEngine.style.position = document.getElementById('cc-position').value;
        captionsEngine.style.bgColor = 'rgba(0,0,0,' + (parseInt(op) / 100).toFixed(2) + ')';
    }
}

function ccClearAll() {
    if (captionsEngine) captionsEngine.clearCues();
    ccRefreshCueList();
}

function ccRefreshCueList() {
    var list = document.getElementById('cc-cue-list');
    var count = document.getElementById('cc-cue-count');
    if (!captionsEngine || captionsEngine.cues.length === 0) {
        list.innerHTML = '<div style="color:var(--muted);text-align:center;padding:20px">No captions yet</div>';
        count.textContent = '(0 cues)';
        return;
    }
    count.textContent = '(' + captionsEngine.cues.length + ' cues)';
    var html = '';
    for (var i = 0; i < captionsEngine.cues.length; i++) {
        var c = captionsEngine.cues[i];
        var ts = ccFmtTime(c.start) + ' - ' + ccFmtTime(c.end);
        html += '<div style="display:flex;gap:6px;padding:3px 4px;border-bottom:1px solid rgba(51,65,85,.3);cursor:pointer" '
              + 'onclick="ccEditCue(' + i + ')" title="Click to edit">'
              + '<span style="color:var(--teal);min-width:110px;flex-shrink:0">' + esc(ts) + '</span>'
              + '<span style="color:var(--text-dim);overflow:hidden;text-overflow:ellipsis;white-space:nowrap">'
              + esc(c.text) + '</span>'
              + '<button onclick="event.stopPropagation();ccDeleteCue(' + i + ')" style="margin-left:auto;background:none;border:none;'
              + 'color:var(--muted);cursor:pointer;padding:0 2px;font-size:10px" title="Delete"><i class="fa-solid fa-xmark"></i></button>'
              + '</div>';
    }
    list.innerHTML = html;
}

function ccEditCue(idx) {
    if (!captionsEngine || idx < 0 || idx >= captionsEngine.cues.length) return;
    var c = captionsEngine.cues[idx];
    var newText = prompt('Edit caption text:', c.text);
    if (newText !== null) {
        captionsEngine.editCue(idx, newText);
        ccRefreshCueList();
    }
}

function ccDeleteCue(idx) {
    if (!captionsEngine) return;
    captionsEngine.deleteCue(idx);
    ccRefreshCueList();
}

function ccFmtTime(sec) {
    var m = Math.floor(sec / 60);
    var s = Math.floor(sec % 60);
    var ms = Math.round((sec - Math.floor(sec)) * 1000);
    return (m < 10 ? '0' : '') + m + ':' + (s < 10 ? '0' : '') + s + '.' + (ms < 10 ? '00' : ms < 100 ? '0' : '') + ms;
}

function esc(s) {
    var d = document.createElement('div');
    d.textContent = s;
    return d.innerHTML;
}

/**
 * Called from the producer render loop to composite captions
 * onto the program output when overlay is enabled.
 */
function ccRenderOverlay(programTime) {
    if (!ccOverlayEnabled || !captionsEngine || !ccOverlayCanvas) return null;
    var cue = captionsEngine.getCueAtTime(programTime);
    var preview = document.getElementById('cc-live-preview');

    if (!cue) {
        if (preview) preview.style.display = 'none';
        /* Clear canvas */
        var ctx = ccOverlayCanvas.getContext('2d');
        ctx.clearRect(0, 0, ccOverlayCanvas.width, ccOverlayCanvas.height);
        return null;
    }

    /* Show in the preview box */
    if (preview) {
        preview.style.display = '';
        preview.textContent = cue.text;
    }

    /* Render onto the overlay canvas for WebGL compositing */
    captionsEngine.renderCaptionFrame(ccOverlayCanvas, cue.text);

    return {
        canvas: ccOverlayCanvas,
        rect: captionsEngine.style.position === 'top'
            ? { x: 0.05, y: 0.02, w: 0.9, h: 0.12 }
            : { x: 0.05, y: 0.82, w: 0.9, h: 0.12 },
        alpha: 1.0
    };
}

/* -- DOMContentLoaded ------------------------------------------------ */

document.addEventListener('DOMContentLoaded', function() {
    initProducer();
    initCaptions();
    document.getElementById('scene-select').addEventListener('change', loadScene);

    document.getElementById('file-modal').addEventListener('click', function(e) {
        if (e.target === this) closeFilePicker();
    });

    // Keyboard shortcuts
    document.addEventListener('keydown', function(e) {
        if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT' || e.target.tagName === 'TEXTAREA') return;
        // 1-6 = PVW source
        if (e.key === '1') { setPVW(0); return; }
        if (e.key === '2') { setPVW(1); return; }
        if (e.key === '3') { setPVW(2); return; }
        if (e.key === '4') { setPVW(3); return; }
        if (e.key === '5') { setPVW(4); return; }
        if (e.key === '6') { setPVW(5); return; }
        // Spacebar = auto transition
        if (e.key === ' ') { e.preventDefault(); doAutoTransition(); return; }
        // Enter = cut
        if (e.key === 'Enter') { doCut(); return; }
        // Shift+1-6 = PGM source (direct)
        if (e.shiftKey && e.code === 'Digit1') { setPGM(0); return; }
        if (e.shiftKey && e.code === 'Digit2') { setPGM(1); return; }
        if (e.shiftKey && e.code === 'Digit3') { setPGM(2); return; }
        if (e.shiftKey && e.code === 'Digit4') { setPGM(3); return; }
        if (e.shiftKey && e.code === 'Digit5') { setPGM(4); return; }
        if (e.shiftKey && e.code === 'Digit6') { setPGM(5); return; }
    });
});
</script>

<?php require_once __DIR__ . '/app/inc/footer.php'; ?>
