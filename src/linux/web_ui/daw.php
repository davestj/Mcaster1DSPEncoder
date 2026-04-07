<?php
/**
 * daw.php — Mcaster1 DSP Producer: Multi-Track Timeline Editor
 *
 * Cubasis/Audacity-style multi-track audio editor with WebGL waveform rendering,
 * Web Audio API playback, drag-drop clip management, and server-side ffmpeg mixdown.
 *
 * Phase:   DAW-2
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 *
 * No exit()/die() — uopz active.
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/app/inc/auth.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/user_auth.php';
require_once __DIR__ . '/app/inc/logger.php';

if (!mc1_is_authed()) {
    header('Location: /login');
    return;
}

$user = mc1_current_user();
if (!$user) { header('Location: /login'); return; }

$page_title = 'Multi-Track Editor';
$active_nav = 'daw';
$use_charts = false;

require_once __DIR__ . '/app/inc/header.php';
?>

<style>
/* ── DAW Layout ── */
.daw-wrap{display:flex;flex-direction:column;height:calc(100vh - var(--topbar-h) - 40px);min-height:500px}

/* ── Toolbar ── */
.daw-toolbar{display:flex;align-items:center;gap:8px;padding:8px 0;flex-wrap:wrap;flex-shrink:0}
.daw-toolbar .sep{width:1px;height:22px;background:var(--border);margin:0 4px;flex-shrink:0}

/* ── Transport ── */
.transport{display:flex;align-items:center;gap:4px}
.transport .btn{min-width:34px;justify-content:center;font-size:14px}
.transport .btn.active-rec{background:rgba(239,68,68,.25);color:var(--red);border-color:rgba(239,68,68,.45);animation:rec-pulse 1s ease-in-out infinite}
@keyframes rec-pulse{0%,100%{opacity:1}50%{opacity:.6}}
.time-display{font-family:'SF Mono','Fira Code',monospace;font-size:15px;font-weight:700;color:var(--teal);padding:4px 10px;background:rgba(0,0,0,.3);border:1px solid var(--border);border-radius:var(--radius-sm);min-width:180px;text-align:center;letter-spacing:.03em}

/* ── Zoom / Snap / BPM ── */
.daw-zoom{display:flex;align-items:center;gap:6px;font-size:12px;color:var(--text-dim)}
.daw-zoom input[type=range]{width:100px}
.daw-bpm{display:flex;align-items:center;gap:4px;font-size:12px;color:var(--text-dim)}
.daw-bpm input{width:50px;text-align:center;padding:3px 4px;background:rgba(255,255,255,.05);border:1px solid var(--border);border-radius:var(--radius-xs);color:var(--text);font-size:12px}

/* ── Timeline container ── */
.daw-body{display:flex;flex:1;overflow:hidden;border:1px solid var(--border);border-radius:var(--radius);background:var(--bg3)}

/* ── Track list (left panel) ── */
.daw-tracks{width:200px;min-width:160px;flex-shrink:0;border-right:1px solid var(--border);overflow-y:auto;background:var(--bg2)}
.track-header{padding:6px 10px;font-size:10px;font-weight:700;text-transform:uppercase;letter-spacing:.08em;color:var(--muted);border-bottom:1px solid var(--border);display:flex;align-items:center;justify-content:space-between}
.track-panel{border-bottom:1px solid var(--border);padding:8px 10px;min-height:80px;display:flex;flex-direction:column;gap:4px;transition:background .15s}
.track-panel:hover{background:rgba(255,255,255,.02)}
.track-panel.selected{background:rgba(20,184,166,.06);border-left:2px solid var(--teal)}
.track-name-row{display:flex;align-items:center;gap:6px}
.track-color{width:10px;height:10px;border-radius:2px;flex-shrink:0;cursor:pointer}
.track-name{font-size:12px;font-weight:600;color:var(--text);flex:1;min-width:0;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;cursor:text}
.track-name:focus{outline:none;background:rgba(255,255,255,.06);padding:0 4px;border-radius:2px}
.track-btns{display:flex;gap:3px}
.track-btn{width:22px;height:22px;border:1px solid var(--border);border-radius:3px;background:rgba(255,255,255,.04);color:var(--text-dim);font-size:9px;font-weight:700;display:flex;align-items:center;justify-content:center;cursor:pointer;transition:all .15s}
.track-btn:hover{background:rgba(255,255,255,.1);color:var(--text)}
.track-btn.muted{background:rgba(239,68,68,.2);color:var(--red);border-color:rgba(239,68,68,.4)}
.track-btn.soloed{background:rgba(234,179,8,.2);color:var(--yellow);border-color:rgba(234,179,8,.4)}
.track-vol-row{display:flex;align-items:center;gap:4px;font-size:10px;color:var(--muted)}
.track-vol-row input[type=range]{flex:1;height:3px}
.track-pan-row{display:flex;align-items:center;gap:4px;font-size:10px;color:var(--muted)}
.track-pan-row input[type=range]{flex:1;height:3px}
.track-del{opacity:0;font-size:11px;color:var(--red);cursor:pointer;transition:opacity .15s}
.track-panel:hover .track-del{opacity:.7}
.track-del:hover{opacity:1 !important}

/* ── Timeline (right scrollable area) ── */
.daw-timeline{flex:1;display:flex;flex-direction:column;overflow:hidden;position:relative}

/* Time ruler */
.daw-ruler{height:24px;flex-shrink:0;background:rgba(0,0,0,.2);border-bottom:1px solid var(--border);position:relative;overflow:hidden}
.daw-ruler canvas{width:100%;height:100%}

/* Waveform area */
.daw-canvas-wrap{flex:1;position:relative;overflow:auto}
.daw-canvas-wrap canvas{display:block}
.daw-overlay{position:absolute;top:0;left:0;pointer-events:none}

/* Playhead */
.daw-playhead{position:absolute;top:0;width:2px;background:var(--red);z-index:20;pointer-events:none;box-shadow:0 0 6px rgba(239,68,68,.5)}

/* Clip tooltip */
.clip-tooltip{position:absolute;padding:3px 8px;background:rgba(0,0,0,.85);border:1px solid var(--border);border-radius:4px;font-size:11px;color:var(--text);pointer-events:none;white-space:nowrap;z-index:30;display:none}

/* Scrollbar at bottom */
.daw-hscroll{height:14px;flex-shrink:0;overflow-x:auto;overflow-y:hidden;background:rgba(0,0,0,.15);border-top:1px solid var(--border)}
.daw-hscroll-inner{height:1px}

/* ── Bottom bar ── */
.daw-bottom{display:flex;align-items:center;gap:8px;padding:8px 0;flex-shrink:0;flex-wrap:wrap}

/* ── Project list modal ── */
.daw-modal-bg{position:fixed;inset:0;background:rgba(0,0,0,.6);z-index:500;display:none;align-items:center;justify-content:center}
.daw-modal-bg.open{display:flex}
.daw-modal{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:20px;width:520px;max-height:80vh;overflow-y:auto}
.daw-modal h3{font-size:16px;font-weight:700;color:var(--text);margin-bottom:14px;display:flex;align-items:center;gap:8px}
.daw-modal .close-btn{margin-left:auto;cursor:pointer;color:var(--muted);font-size:16px}
.daw-modal .close-btn:hover{color:var(--text)}
.project-row{display:flex;align-items:center;gap:10px;padding:8px 10px;border:1px solid var(--border);border-radius:var(--radius-sm);margin-bottom:6px;cursor:pointer;transition:background .15s}
.project-row:hover{background:rgba(255,255,255,.04)}
.project-row .pname{flex:1;font-size:13px;font-weight:600;color:var(--text)}
.project-row .pdate{font-size:11px;color:var(--muted)}
.project-row .pdel{color:var(--red);font-size:12px;cursor:pointer;opacity:.5}
.project-row .pdel:hover{opacity:1}

/* ── Context menu ── */
.daw-ctx{position:fixed;background:var(--card);border:1px solid var(--border);border-radius:var(--radius-sm);padding:4px 0;z-index:600;display:none;min-width:160px;box-shadow:0 4px 20px rgba(0,0,0,.5)}
.daw-ctx-item{padding:6px 14px;font-size:12px;color:var(--text-dim);cursor:pointer;display:flex;align-items:center;gap:8px}
.daw-ctx-item:hover{background:rgba(255,255,255,.06);color:var(--text)}
.daw-ctx-item.danger{color:var(--red)}
.daw-ctx-sep{height:1px;background:var(--border);margin:3px 0}

/* ── Drop zone overlay ── */
.daw-dropzone{position:absolute;inset:0;background:rgba(20,184,166,.08);border:2px dashed var(--teal);border-radius:var(--radius);display:none;align-items:center;justify-content:center;font-size:15px;font-weight:700;color:var(--teal);z-index:100}
.daw-dropzone.active{display:flex}

/* ── Automation mode button ── */
.btn-auto-active{background:rgba(249,115,22,.2) !important;color:#f97316 !important;border-color:rgba(249,115,22,.5) !important}

/* ── Clip Properties Panel ── */
.clip-props{position:fixed;bottom:60px;right:20px;width:240px;background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:10px;z-index:50;box-shadow:0 4px 20px rgba(0,0,0,.4)}
.clip-props-title{font-size:11px;font-weight:700;text-transform:uppercase;letter-spacing:.06em;color:var(--teal);margin-bottom:8px;display:flex;align-items:center;gap:6px}
.clip-props-row{display:flex;align-items:center;gap:6px;margin-bottom:5px;font-size:11px;color:var(--text-dim)}
.clip-props-row label{width:55px;flex-shrink:0;font-weight:600}
.clip-props-row input.form-input{padding:3px 6px;font-size:11px}
.clip-props-row .text-muted{color:var(--muted);font-size:10px}

/* ── Tap Tempo button ── */
#btn-tap-tempo{font-size:10px;font-weight:700;letter-spacing:.04em;padding:3px 8px;min-width:34px}
</style>

<!-- Toolbar -->
<div class="daw-wrap" id="daw-root">
  <div class="daw-toolbar">
    <div class="transport">
      <button class="btn btn-secondary btn-sm" id="btn-rewind" title="Rewind"><i class="fa-solid fa-backward-step"></i></button>
      <button class="btn btn-primary btn-sm" id="btn-play" title="Play"><i class="fa-solid fa-play"></i></button>
      <button class="btn btn-secondary btn-sm" id="btn-stop" title="Stop"><i class="fa-solid fa-stop"></i></button>
      <button class="btn btn-secondary btn-sm" id="btn-record" title="Record (placeholder)"><i class="fa-solid fa-circle" style="color:#ef4444"></i></button>
    </div>
    <div class="time-display" id="time-display">00:00:00.000 / 00:00:00.000</div>
    <div class="sep"></div>
    <div class="daw-zoom">
      <i class="fa-solid fa-magnifying-glass-minus" style="font-size:11px"></i>
      <input type="range" id="zoom-slider" min="10" max="500" value="100" title="Pixels per second">
      <i class="fa-solid fa-magnifying-glass-plus" style="font-size:11px"></i>
      <span id="zoom-label">100 px/s</span>
    </div>
    <div class="sep"></div>
    <div class="daw-bpm">
      <span>BPM</span>
      <input type="number" id="bpm-input" value="120" min="20" max="300" step="0.1">
      <button class="btn btn-secondary btn-xs" id="btn-tap-tempo" title="Tap Tempo (click 4+ times)">TAP</button>
    </div>
    <div class="sep"></div>
    <button class="btn btn-secondary btn-sm" id="btn-automation" title="Toggle Automation Mode"><i class="fa-solid fa-chart-line"></i> Auto</button>
    <select class="form-select" id="xfade-mode" style="width:85px;padding:4px 6px;font-size:11px" title="Crossfade mode">
      <option value="auto" selected>XF Auto</option>
      <option value="manual">XF Manual</option>
    </select>
    <div class="sep"></div>
    <select class="form-select" id="snap-select" style="width:90px;padding:4px 6px;font-size:11px" title="Snap to grid">
      <option value="0">No Snap</option>
      <option value="bar">1 Bar</option>
      <option value="beat">Beat</option>
      <option value="1/2">1/2</option>
      <option value="1/4">1/4</option>
      <option value="1/8">1/8</option>
      <option value="1/16">1/16</option>
      <option value="0.5">0.5s</option>
      <option value="1" selected>1s</option>
      <option value="5">5s</option>
    </select>
  </div>

  <!-- Main body: track list + timeline -->
  <div class="daw-body">
    <!-- Track list panel -->
    <div class="daw-tracks" id="track-list">
      <div class="track-header">
        <span>Tracks</span>
        <button class="btn btn-secondary btn-xs" id="btn-add-track" title="Add Track"><i class="fa-solid fa-plus"></i></button>
      </div>
      <!-- Track panels injected by JS -->
    </div>

    <!-- Timeline area -->
    <div class="daw-timeline" id="timeline-area">
      <div class="daw-ruler" id="ruler-area"><canvas id="ruler-canvas"></canvas></div>
      <div class="daw-canvas-wrap" id="canvas-wrap">
        <canvas id="daw-waveform-canvas"></canvas>
        <canvas id="daw-overlay-canvas" class="daw-overlay"></canvas>
        <div class="daw-playhead" id="playhead"></div>
        <div class="clip-tooltip" id="clip-tooltip"></div>
        <div class="daw-dropzone" id="drop-zone">Drop audio files here</div>
      </div>
      <div class="daw-hscroll" id="hscroll"><div class="daw-hscroll-inner" id="hscroll-inner"></div></div>
    </div>
  </div>

  <!-- Bottom bar -->
  <div class="daw-bottom">
    <button class="btn btn-secondary btn-sm" id="btn-add-track2"><i class="fa-solid fa-plus"></i> Add Track</button>
    <button class="btn btn-secondary btn-sm" id="btn-load-library"><i class="fa-solid fa-folder-open"></i> Load from Library</button>
    <button class="btn btn-secondary btn-sm" id="btn-export"><i class="fa-solid fa-file-export"></i> Export Mixdown</button>
    <div style="flex:1"></div>
    <button class="btn btn-secondary btn-sm" id="btn-projects"><i class="fa-solid fa-folder"></i> Projects</button>
    <button class="btn btn-primary btn-sm" id="btn-save"><i class="fa-solid fa-floppy-disk"></i> Save Project</button>
  </div>
</div>

<!-- Context menu -->
<div class="daw-ctx" id="ctx-menu">
  <div class="daw-ctx-item" data-action="split"><i class="fa-solid fa-scissors fa-fw"></i> Split at Playhead</div>
  <div class="daw-ctx-item" data-action="duplicate"><i class="fa-solid fa-clone fa-fw"></i> Duplicate</div>
  <div class="daw-ctx-item" data-action="copy"><i class="fa-solid fa-copy fa-fw"></i> Copy (Ctrl+C)</div>
  <div class="daw-ctx-item" data-action="merge"><i class="fa-solid fa-compress fa-fw"></i> Merge with Next</div>
  <div class="daw-ctx-sep"></div>
  <div class="daw-ctx-item" data-action="fadein"><i class="fa-solid fa-arrow-trend-up fa-fw"></i> Fade In (0.5s)</div>
  <div class="daw-ctx-item" data-action="fadeout"><i class="fa-solid fa-arrow-trend-down fa-fw"></i> Fade Out (0.5s)</div>
  <div class="daw-ctx-item" data-action="clearenv"><i class="fa-solid fa-eraser fa-fw"></i> Clear Gain Envelope</div>
  <div class="daw-ctx-sep"></div>
  <div class="daw-ctx-item danger" data-action="delete"><i class="fa-solid fa-trash fa-fw"></i> Delete Clip</div>
</div>

<!-- Clip Properties Panel -->
<div id="clip-props-panel" class="clip-props" style="display:none">
  <div class="clip-props-title"><i class="fa-solid fa-sliders fa-fw"></i> Clip Properties</div>
  <div class="clip-props-row">
    <label>Name</label>
    <input class="form-input" id="cp-name" style="flex:1">
  </div>
  <div class="clip-props-row">
    <label>Fade In</label>
    <input class="form-input" id="cp-fadein" type="number" step="0.05" min="0" style="width:60px"> <span class="text-muted">s</span>
  </div>
  <div class="clip-props-row">
    <label>Fade Out</label>
    <input class="form-input" id="cp-fadeout" type="number" step="0.05" min="0" style="width:60px"> <span class="text-muted">s</span>
  </div>
  <div class="clip-props-row">
    <label>Color</label>
    <input type="color" id="cp-color" style="width:32px;height:22px;border:none;background:none;cursor:pointer">
  </div>
  <button class="btn btn-primary btn-xs" id="btn-apply-props"><i class="fa-solid fa-check"></i> Apply</button>
</div>

<!-- Project list modal -->
<div class="daw-modal-bg" id="modal-projects">
  <div class="daw-modal">
    <h3><i class="fa-solid fa-folder fa-fw" style="color:var(--teal)"></i> Projects <span class="close-btn" id="close-projects"><i class="fa-solid fa-xmark"></i></span></h3>
    <div id="project-list">
      <div style="text-align:center;padding:20px;color:var(--muted)"><i class="fa-solid fa-spinner fa-spin"></i> Loading...</div>
    </div>
    <div style="margin-top:12px">
      <button class="btn btn-primary btn-sm" id="btn-new-project"><i class="fa-solid fa-plus"></i> New Project</button>
    </div>
  </div>
</div>

<!-- Export modal -->
<div class="daw-modal-bg" id="modal-export">
  <div class="daw-modal">
    <h3><i class="fa-solid fa-file-export fa-fw" style="color:var(--teal)"></i> Export Mixdown <span class="close-btn" id="close-export"><i class="fa-solid fa-xmark"></i></span></h3>
    <div class="form-group">
      <label class="form-label">Format</label>
      <select class="form-select" id="export-format">
        <option value="mp3">MP3</option>
        <option value="wav">WAV (PCM 16-bit)</option>
        <option value="flac">FLAC</option>
        <option value="ogg">Ogg Vorbis</option>
      </select>
    </div>
    <div class="form-group" id="export-bitrate-group">
      <label class="form-label">Bitrate</label>
      <select class="form-select" id="export-bitrate">
        <option value="128k">128 kbps</option>
        <option value="192k" selected>192 kbps</option>
        <option value="256k">256 kbps</option>
        <option value="320k">320 kbps</option>
      </select>
    </div>
    <div class="form-group">
      <label class="form-label">Project Name</label>
      <input class="form-input" id="export-name" value="">
    </div>
    <div style="margin-top:14px;display:flex;gap:8px;justify-content:flex-end">
      <button class="btn btn-secondary btn-sm" onclick="document.getElementById('modal-export').classList.remove('open')">Cancel</button>
      <button class="btn btn-primary btn-sm" id="btn-do-export"><i class="fa-solid fa-download"></i> Export</button>
    </div>
    <div id="export-progress" style="display:none;margin-top:12px">
      <div class="alert alert-info"><i class="fa-solid fa-spinner fa-spin"></i> <span id="export-status">Exporting...</span></div>
    </div>
  </div>
</div>

<!-- Library browser modal -->
<div class="daw-modal-bg" id="modal-library">
  <div class="daw-modal" style="width:600px">
    <h3><i class="fa-solid fa-music fa-fw" style="color:var(--teal)"></i> Load from Library <span class="close-btn" id="close-library"><i class="fa-solid fa-xmark"></i></span></h3>
    <div class="search-row" style="margin-bottom:10px">
      <input class="form-input" id="lib-search" placeholder="Search tracks...">
      <button class="btn btn-primary btn-sm" id="btn-lib-search"><i class="fa-solid fa-search"></i></button>
    </div>
    <div id="lib-results" style="max-height:400px;overflow-y:auto">
      <div style="text-align:center;padding:20px;color:var(--muted)">Search for tracks to add to your project</div>
    </div>
  </div>
</div>

<script src="/js/daw-waveform.js"></script>
<script src="/js/daw-engine.js"></script>

<script>
(function(){
    var daw = null;

    document.addEventListener('DOMContentLoaded', function(){
        daw = new DawEngine('daw-root');
        window._daw = daw; // expose for debugging

        // Transport buttons
        document.getElementById('btn-play').addEventListener('click', function(){
            if (daw.playing) daw.pause(); else daw.play();
        });
        document.getElementById('btn-stop').addEventListener('click', function(){ daw.stop(); });
        document.getElementById('btn-rewind').addEventListener('click', function(){ daw.seek(0); });

        // Zoom slider
        document.getElementById('zoom-slider').addEventListener('input', function(){
            daw.setZoom(parseFloat(this.value));
            document.getElementById('zoom-label').textContent = this.value + ' px/s';
        });

        // BPM
        document.getElementById('bpm-input').addEventListener('change', function(){
            daw.bpm = parseFloat(this.value) || 120;
        });

        // Snap
        document.getElementById('snap-select').addEventListener('change', function(){
            daw.snapMode = this.value;
        });

        // Tap Tempo
        document.getElementById('btn-tap-tempo').addEventListener('click', function(){
            daw.tapTempo();
        });

        // Automation mode toggle
        document.getElementById('btn-automation').addEventListener('click', function(){
            daw.automationMode = !daw.automationMode;
            this.classList.toggle('btn-auto-active', daw.automationMode);
            mc1Toast('Automation mode: ' + (daw.automationMode ? 'ON' : 'OFF'), 'ok');
        });

        // Crossfade mode
        document.getElementById('xfade-mode').addEventListener('change', function(){
            daw.crossfadeMode = this.value;
        });

        // Clip properties apply button
        document.getElementById('btn-apply-props').addEventListener('click', function(){
            daw._applyClipProps();
        });

        // Add track buttons
        document.getElementById('btn-add-track').addEventListener('click', function(){ daw.addTrack(); });
        document.getElementById('btn-add-track2').addEventListener('click', function(){ daw.addTrack(); });

        // Load from library
        document.getElementById('btn-load-library').addEventListener('click', function(){
            document.getElementById('modal-library').classList.add('open');
        });
        document.getElementById('close-library').addEventListener('click', function(){
            document.getElementById('modal-library').classList.remove('open');
        });
        document.getElementById('btn-lib-search').addEventListener('click', doLibSearch);
        document.getElementById('lib-search').addEventListener('keydown', function(e){
            if (e.key === 'Enter') doLibSearch();
        });

        // Save
        document.getElementById('btn-save').addEventListener('click', function(){ daw.saveProject(); });

        // Projects modal
        document.getElementById('btn-projects').addEventListener('click', function(){
            document.getElementById('modal-projects').classList.add('open');
            daw.loadProjectList();
        });
        document.getElementById('close-projects').addEventListener('click', function(){
            document.getElementById('modal-projects').classList.remove('open');
        });
        document.getElementById('btn-new-project').addEventListener('click', function(){
            daw.newProject();
            document.getElementById('modal-projects').classList.remove('open');
        });

        // Export
        document.getElementById('btn-export').addEventListener('click', function(){
            document.getElementById('export-name').value = daw.projectName || 'Untitled';
            document.getElementById('modal-export').classList.add('open');
        });
        document.getElementById('close-export').addEventListener('click', function(){
            document.getElementById('modal-export').classList.remove('open');
        });
        document.getElementById('export-format').addEventListener('change', function(){
            var show = (this.value === 'mp3' || this.value === 'ogg');
            document.getElementById('export-bitrate-group').style.display = show ? '' : 'none';
        });
        document.getElementById('btn-do-export').addEventListener('click', function(){ daw.exportMixdown(); });

        // Context menu
        document.querySelectorAll('#ctx-menu .daw-ctx-item').forEach(function(el){
            el.addEventListener('click', function(){
                var act = el.dataset.action;
                daw.handleContextAction(act);
                document.getElementById('ctx-menu').style.display = 'none';
            });
        });
        document.addEventListener('click', function(){ document.getElementById('ctx-menu').style.display = 'none'; });

        // Close modals on backdrop click
        document.querySelectorAll('.daw-modal-bg').forEach(function(bg){
            bg.addEventListener('click', function(e){
                if (e.target === bg) bg.classList.remove('open');
            });
        });

        // Keyboard shortcuts
        document.addEventListener('keydown', function(e){
            if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA' || e.target.tagName === 'SELECT') return;
            if (e.code === 'Space') { e.preventDefault(); if (daw.playing) daw.pause(); else daw.play(); }
            if (e.code === 'Home') { e.preventDefault(); daw.seek(0); }
            if (e.code === 'Delete' || e.code === 'Backspace') { e.preventDefault(); daw.deleteSelectedClip(); }
            if (e.ctrlKey && e.code === 'KeyC') { e.preventDefault(); daw.copyClip(); }
            if (e.ctrlKey && e.code === 'KeyV') { e.preventDefault(); daw.pasteClip(); }
            if (e.ctrlKey && e.code === 'KeyS') { e.preventDefault(); daw.saveProject(); }
            if (e.ctrlKey && e.code === 'KeyZ') { e.preventDefault(); daw.undo(); }
            if (e.ctrlKey && e.code === 'KeyY') { e.preventDefault(); daw.redo(); }
        });
    });

    function doLibSearch() {
        var q = document.getElementById('lib-search').value.trim();
        if (!q) return;
        var res = document.getElementById('lib-results');
        res.innerHTML = '<div style="text-align:center;padding:20px;color:var(--muted)"><i class="fa-solid fa-spinner fa-spin"></i> Searching...</div>';
        mc1Api('POST', '/app/api/tracks.php', {action:'search', query:q, limit:50}).then(function(d){
            if (!d.ok || !d.tracks || d.tracks.length === 0) {
                res.innerHTML = '<div style="text-align:center;padding:20px;color:var(--muted)">No tracks found</div>';
                return;
            }
            var html = '<table><thead><tr><th>Title</th><th>Artist</th><th>Duration</th><th></th></tr></thead><tbody>';
            d.tracks.forEach(function(t){
                var dur = t.duration_ms ? fmtTime(t.duration_ms / 1000) : '--:--';
                html += '<tr>'
                    + '<td class="td-title">' + esc(t.title || t.file_path || '') + '</td>'
                    + '<td>' + esc(t.artist || '') + '</td>'
                    + '<td class="td-mono">' + dur + '</td>'
                    + '<td><button class="btn btn-primary btn-xs" onclick="window._daw.addClipFromLibrary(' + t.id + ', ' + esc(JSON.stringify(t.title || 'Track')) + ')">'
                    + '<i class="fa-solid fa-plus"></i></button></td>'
                    + '</tr>';
            });
            html += '</tbody></table>';
            res.innerHTML = html;
        }).catch(function(){ res.innerHTML = '<div class="alert alert-error">Search failed</div>'; });
    }

    function fmtTime(s) {
        var m = Math.floor(s / 60);
        var sec = Math.floor(s % 60);
        return (m < 10 ? '0' : '') + m + ':' + (sec < 10 ? '0' : '') + sec;
    }

    function esc(s) {
        var d = document.createElement('div');
        d.appendChild(document.createTextNode(s));
        return d.innerHTML;
    }
})();
</script>

<?php require_once __DIR__ . '/app/inc/footer.php'; ?>
