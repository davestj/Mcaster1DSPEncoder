<?php
/**
 * daw.php — Mcaster1 DSP Producer: Multi-Track Timeline Editor
 *
 * Cubasis/Audacity-style multi-track audio editor with WebGL waveform rendering,
 * Web Audio API playback, drag-drop clip management, and server-side ffmpeg mixdown.
 *
 * Phase:   DAW-4
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
/* ── Recording button pulse ── */
#btn-record.recording { animation: rec-pulse 1s ease-in-out infinite; background:#ef4444 !important; color:#fff !important; border-color:#ef4444 !important; }
@keyframes rec-pulse { 0%,100%{opacity:1} 50%{opacity:.5} }
/* Recording panel as a centered modal overlay — impossible to miss */
.rec-modal-bg { position:fixed; inset:0; background:rgba(0,0,0,.7); z-index:8000; display:none; align-items:center; justify-content:center; }
.rec-modal-bg.open { display:flex; }
.rec-modal { background:var(--bg2); border:2px solid #ef4444; border-radius:12px; width:min(400px,95vw); max-height:90vh; overflow-y:auto; box-shadow:0 0 40px rgba(239,68,68,.3); }

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

/* ── Effects Panel (slide-out from right) ── */
.fx-panel{position:fixed;top:0;right:0;width:320px;height:100vh;background:var(--bg2);border-left:1px solid var(--border);z-index:400;transform:translateX(100%);transition:transform .25s ease;overflow-y:auto;box-shadow:-4px 0 20px rgba(0,0,0,.5)}
.fx-panel.open{transform:translateX(0)}
.fx-panel-header{display:flex;align-items:center;gap:8px;padding:12px 14px;border-bottom:1px solid var(--border);background:rgba(0,0,0,.2)}
.fx-panel-header h4{flex:1;margin:0;font-size:13px;font-weight:700;color:var(--teal)}
.fx-panel-close{cursor:pointer;color:var(--muted);font-size:14px}
.fx-panel-close:hover{color:var(--text)}
.fx-chain{padding:8px}
.fx-card{border:1px solid var(--border);border-radius:var(--radius-sm);margin-bottom:8px;background:rgba(255,255,255,.02)}
.fx-card-header{display:flex;align-items:center;gap:6px;padding:6px 10px;cursor:move;border-bottom:1px solid rgba(255,255,255,.04)}
.fx-card-header .fx-type{font-size:11px;font-weight:700;text-transform:uppercase;letter-spacing:.06em;color:var(--teal);flex:1}
.fx-card-header .fx-remove{color:var(--red);font-size:11px;cursor:pointer;opacity:.5}
.fx-card-header .fx-remove:hover{opacity:1}
.fx-card-body{padding:8px 10px;display:flex;flex-wrap:wrap;gap:8px}
.fx-param{display:flex;flex-direction:column;gap:2px;min-width:70px}
.fx-param label{font-size:9px;font-weight:600;text-transform:uppercase;letter-spacing:.04em;color:var(--muted)}
.fx-param input[type=range]{width:80px;height:4px}
.fx-param .fx-val{font-size:10px;color:var(--text-dim);font-family:monospace}
.fx-add-row{padding:8px;text-align:center}

/* ── Aux Bus Panel ── */
.aux-panel{position:fixed;top:0;right:0;width:300px;height:100vh;background:var(--bg2);border-left:1px solid var(--border);z-index:400;transform:translateX(100%);transition:transform .25s ease;overflow-y:auto;box-shadow:-4px 0 20px rgba(0,0,0,.5)}
.aux-panel.open{transform:translateX(0)}
.aux-bus-card{border:1px solid var(--border);border-radius:var(--radius-sm);margin:8px;padding:10px;background:rgba(255,255,255,.02)}
.aux-bus-card h5{font-size:12px;font-weight:700;color:var(--text);margin:0 0 8px}
.aux-send-row{display:flex;align-items:center;gap:6px;margin-bottom:4px;font-size:11px;color:var(--text-dim)}
.aux-send-row span{width:60px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.aux-send-row input[type=range]{flex:1;height:3px}
.aux-send-row .send-val{width:30px;text-align:right;font-family:monospace;font-size:10px}

/* ── Master Bus Section ── */
.daw-master{display:flex;align-items:center;gap:10px;padding:6px 12px;background:rgba(0,0,0,.2);border-top:1px solid var(--border);flex-shrink:0}
.master-label{font-size:10px;font-weight:700;text-transform:uppercase;letter-spacing:.06em;color:var(--muted)}
.master-fader{width:120px;height:4px}
.master-vol-val{font-size:11px;font-family:monospace;color:var(--text-dim);min-width:40px;text-align:center}
#master-meter-canvas{width:40px;height:50px;border:1px solid var(--border);border-radius:3px;background:rgba(0,0,0,.3)}
.master-lufs{font-size:10px;font-family:monospace;color:var(--text-dim);min-width:65px;text-align:center}
.master-limiter-btn{font-size:9px;padding:2px 6px;letter-spacing:.04em}
.master-limiter-btn.bypassed{background:rgba(239,68,68,.2) !important;color:var(--red) !important;border-color:rgba(239,68,68,.4) !important}

/* ── Track FX Button ── */
.track-fx-btn{width:22px;height:22px;border:1px solid var(--border);border-radius:3px;background:rgba(255,255,255,.04);color:var(--text-dim);font-size:9px;font-weight:700;display:flex;align-items:center;justify-content:center;cursor:pointer;transition:all .15s}
.track-fx-btn:hover{background:rgba(255,255,255,.1);color:var(--text)}
.track-fx-btn.has-fx{background:rgba(20,184,166,.15);color:var(--teal);border-color:rgba(20,184,166,.4)}
.track-fx-btn.frozen-fx{opacity:.4;cursor:not-allowed;pointer-events:none}

/* ── Track Denoise / Freeze Buttons ── */
.track-denoise-btn.has-nr{background:rgba(168,85,247,.2);color:#a855f7;border-color:rgba(168,85,247,.4)}
.track-freeze-btn.frozen{background:rgba(96,165,250,.25);color:#60a5fa;border-color:rgba(96,165,250,.5);animation:freeze-glow 2s ease-in-out infinite}
@keyframes freeze-glow{0%,100%{box-shadow:0 0 4px rgba(96,165,250,.3)}50%{box-shadow:0 0 8px rgba(96,165,250,.6)}}

/* ── Marker / Region List Panel ── */
.markers-panel{max-height:200px;overflow-y:auto;margin-top:6px}
.marker-row,.region-row{display:flex;align-items:center;gap:6px;padding:4px 6px;font-size:11px;color:var(--text-dim);border-bottom:1px solid rgba(255,255,255,.04);cursor:pointer}
.marker-row:hover,.region-row:hover{background:rgba(255,255,255,.04)}
.marker-row .m-color,.region-row .r-color{width:8px;height:8px;border-radius:2px;flex-shrink:0}
.marker-row .m-name,.region-row .r-name{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.marker-row .m-time,.region-row .r-time{font-family:monospace;font-size:10px;color:var(--muted)}
.marker-row .m-del,.region-row .r-del{color:var(--red);cursor:pointer;opacity:.4;font-size:10px}
.marker-row .m-del:hover,.region-row .r-del:hover{opacity:1}
</style>

<!-- Toolbar -->
<div class="daw-wrap" id="daw-root">
  <div class="daw-toolbar">
    <div class="transport">
      <button class="btn btn-secondary btn-sm" id="btn-rewind" title="Rewind"><i class="fa-solid fa-backward-step"></i></button>
      <button class="btn btn-primary btn-sm" id="btn-play" title="Play"><i class="fa-solid fa-play"></i></button>
      <button class="btn btn-secondary btn-sm" id="btn-stop" title="Stop"><i class="fa-solid fa-stop"></i></button>
      <button class="btn btn-secondary btn-sm" id="btn-record" title="Record to Track" onclick="openRecordPanel()"><i class="fa-solid fa-circle" style="color:#ef4444"></i> REC</button>
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
    <button class="btn btn-secondary btn-sm" id="btn-buses" title="Aux Buses"><i class="fa-solid fa-diagram-project"></i> Buses</button>
    <div class="sep"></div>
    <button class="btn btn-secondary btn-sm" id="btn-add-marker" title="Add Marker (M key)"><i class="fa-solid fa-flag"></i></button>
    <button class="btn btn-secondary btn-sm" id="btn-prev-marker" title="Previous Marker"><i class="fa-solid fa-chevron-left"></i></button>
    <button class="btn btn-secondary btn-sm" id="btn-next-marker" title="Next Marker"><i class="fa-solid fa-chevron-right"></i></button>
    <button class="btn btn-secondary btn-sm" id="btn-marker-list" title="Markers / Regions"><i class="fa-solid fa-list"></i></button>
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

  <!-- Master bus -->
  <div class="daw-master">
    <span class="master-label">Master</span>
    <input type="range" class="master-fader" id="master-fader" min="0" max="200" value="100" title="Master Volume">
    <span class="master-vol-val" id="master-vol-val">100%</span>
    <canvas id="master-meter-canvas" width="40" height="50"></canvas>
    <span class="master-lufs" id="master-lufs">-- LUFS</span>
    <button class="btn btn-secondary master-limiter-btn" id="btn-limiter-bypass" title="Limiter Bypass">LIM</button>
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
  <div class="daw-ctx-item" data-action="timestretch"><i class="fa-solid fa-clock fa-fw"></i> Time Stretch...</div>
  <div class="daw-ctx-item" data-action="pitchshift"><i class="fa-solid fa-music fa-fw"></i> Pitch Shift...</div>
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

<!-- Record to Track modal -->
<div class="rec-modal-bg" id="rec-modal-bg" onclick="if(event.target===this)closeRecordPanel()">
<div class="rec-modal" id="record-panel">
  <div style="display:flex;align-items:center;justify-content:space-between;padding:14px 16px;border-bottom:1px solid var(--border)">
    <div style="display:flex;align-items:center;gap:8px">
      <i class="fa-solid fa-microphone fa-fw" style="color:#ef4444;font-size:18px"></i>
      <h4 style="margin:0;font-size:15px;font-weight:700">Record to Track</h4>
    </div>
    <button class="btn btn-secondary btn-xs" onclick="closeRecordPanel()"><i class="fa-solid fa-xmark"></i></button>
  </div>
  <div style="padding:12px">
    <!-- Input Source -->
    <div style="margin-bottom:12px">
      <label style="font-size:11px;color:var(--muted);display:block;margin-bottom:4px">Audio Input</label>
      <select class="form-select" id="rec-device" style="width:100%;font-size:11px;padding:5px 8px">
        <option value="">Detecting devices...</option>
      </select>
    </div>

    <!-- Target Track -->
    <div style="margin-bottom:12px">
      <label style="font-size:11px;color:var(--muted);display:block;margin-bottom:4px">Record to Track</label>
      <select class="form-select" id="rec-track" style="width:100%;font-size:11px;padding:5px 8px">
        <option value="_new">+ New Track</option>
      </select>
    </div>

    <!-- Format -->
    <div style="display:flex;gap:8px;margin-bottom:12px">
      <div style="flex:1">
        <label style="font-size:11px;color:var(--muted);display:block;margin-bottom:4px">Format</label>
        <select class="form-select" id="rec-format" style="width:100%;font-size:11px;padding:5px 8px">
          <option value="wav">WAV (Lossless)</option>
          <option value="webm">WebM (Opus)</option>
        </select>
      </div>
      <div style="flex:1">
        <label style="font-size:11px;color:var(--muted);display:block;margin-bottom:4px">Mode</label>
        <select class="form-select" id="rec-mode" style="width:100%;font-size:11px;padding:5px 8px">
          <option value="at_cursor">At Cursor</option>
          <option value="at_end">At End</option>
          <option value="replace">Replace Selection</option>
        </select>
      </div>
    </div>

    <!-- Monitor toggle -->
    <div style="margin-bottom:12px">
      <label style="font-size:11px;cursor:pointer;display:flex;align-items:center;gap:6px">
        <input type="checkbox" id="rec-monitor" checked style="accent-color:var(--teal)">
        Monitor input (hear yourself)
      </label>
    </div>

    <!-- Level Meter -->
    <div style="margin-bottom:12px">
      <label style="font-size:11px;color:var(--muted);display:block;margin-bottom:4px">Input Level</label>
      <div style="background:var(--bg);border:1px solid var(--border);border-radius:4px;height:20px;overflow:hidden;position:relative">
        <div id="rec-level-bar" style="height:100%;width:0%;background:linear-gradient(90deg,#22c55e,#f59e0b,#ef4444);transition:width 50ms"></div>
        <span id="rec-level-db" style="position:absolute;right:4px;top:2px;font-size:9px;color:var(--text)">-∞ dB</span>
      </div>
    </div>

    <!-- Timer -->
    <div id="rec-timer" style="text-align:center;font-size:28px;font-weight:700;font-variant-numeric:tabular-nums;color:var(--muted);margin:12px 0">
      00:00.0
    </div>

    <!-- Controls -->
    <div style="display:flex;gap:8px;justify-content:center;margin-bottom:12px">
      <button class="btn btn-sm" id="btn-rec-start" onclick="startDawRecording()" style="background:#ef4444;color:#fff;border:none;padding:6px 20px;font-weight:700">
        <i class="fa-solid fa-circle"></i> Record
      </button>
      <button class="btn btn-sm btn-secondary" id="btn-rec-stop" onclick="stopDawRecording()" disabled style="padding:6px 20px">
        <i class="fa-solid fa-stop"></i> Stop
      </button>
    </div>

    <!-- Status -->
    <div id="rec-status" style="text-align:center;font-size:11px;color:var(--muted)">
      Select an input device and click Record
    </div>
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
        <option value="wav">WAV</option>
        <option value="flac">FLAC</option>
        <option value="ogg">Ogg Vorbis</option>
        <option value="aac">AAC</option>
        <option value="opus">Opus</option>
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
    <div class="form-group" id="export-quality-group" style="display:none">
      <label class="form-label">Quality</label>
      <select class="form-select" id="export-quality">
        <option value="0">0 (Lowest)</option>
        <option value="2">2</option>
        <option value="5" selected>5 (Default)</option>
        <option value="7">7</option>
        <option value="8">8 (Highest)</option>
        <option value="10">10 (Max)</option>
      </select>
    </div>
    <div class="form-group" id="export-bit-depth-group" style="display:none">
      <label class="form-label">Bit Depth</label>
      <select class="form-select" id="export-bit-depth">
        <option value="16" selected>16-bit</option>
        <option value="24">24-bit</option>
      </select>
    </div>
    <div class="form-group">
      <label class="form-label">Project Name</label>
      <input class="form-input" id="export-name" value="">
    </div>
    <div class="form-group">
      <label style="display:flex;align-items:center;gap:6px;font-size:12px;color:var(--text-dim);cursor:pointer">
        <input type="checkbox" id="export-stems"> Export each track as a separate stem file
      </label>
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

<!-- Library browser — uses the full media picker component -->
<?php include 'app/inc/media_picker.php'; ?>

<!-- Track Effects Panel (slide-out) -->
<div class="fx-panel" id="fx-panel">
  <div class="fx-panel-header">
    <i class="fa-solid fa-sliders fa-fw" style="color:var(--teal)"></i>
    <h4 id="fx-panel-title">Track Effects</h4>
    <span class="fx-panel-close" id="fx-panel-close"><i class="fa-solid fa-xmark"></i></span>
  </div>
  <div class="fx-chain" id="fx-chain">
    <!-- Effect cards injected by JS -->
  </div>
  <div class="fx-add-row">
    <select class="form-select" id="fx-add-type" style="width:140px;display:inline-block;font-size:11px;padding:4px 6px">
      <option value="">Add Effect...</option>
      <option value="eq">EQ (3-Band)</option>
      <option value="compressor">Compressor</option>
      <option value="reverb">Reverb</option>
      <option value="delay">Delay</option>
      <option value="gain">Gain</option>
    </select>
  </div>
</div>

<!-- Aux Bus Panel (slide-out) -->
<div class="aux-panel" id="aux-panel">
  <div class="fx-panel-header">
    <i class="fa-solid fa-diagram-project fa-fw" style="color:var(--teal)"></i>
    <h4>Aux Buses</h4>
    <span class="fx-panel-close" id="aux-panel-close"><i class="fa-solid fa-xmark"></i></span>
  </div>
  <div id="aux-bus-list" style="padding:8px">
    <!-- Bus cards injected by JS -->
  </div>
  <div class="fx-add-row">
    <select class="form-select" id="aux-add-type" style="width:140px;display:inline-block;font-size:11px;padding:4px 6px">
      <option value="">Add Aux Bus...</option>
      <option value="reverb">Reverb Bus</option>
      <option value="delay">Delay Bus</option>
      <option value="compressor">Compressor Bus</option>
    </select>
  </div>
</div>

<!-- Time Stretch Dialog -->
<div class="daw-modal-bg" id="modal-timestretch">
  <div class="daw-modal" style="width:360px">
    <h3><i class="fa-solid fa-clock fa-fw" style="color:var(--teal)"></i> Time Stretch <span class="close-btn" id="close-timestretch"><i class="fa-solid fa-xmark"></i></span></h3>
    <div class="form-group">
      <label class="form-label">Stretch Factor</label>
      <input type="range" class="form-range" id="ts-factor" min="50" max="200" value="100" style="width:100%">
      <div style="display:flex;justify-content:space-between;font-size:11px;color:var(--muted)">
        <span>0.5x (slower)</span>
        <span id="ts-factor-val" style="font-weight:700;color:var(--teal)">1.00x</span>
        <span>2.0x (faster)</span>
      </div>
    </div>
    <div style="margin-top:14px;display:flex;gap:8px;justify-content:flex-end">
      <button class="btn btn-secondary btn-sm" onclick="document.getElementById('modal-timestretch').classList.remove('open')">Cancel</button>
      <button class="btn btn-primary btn-sm" id="btn-do-timestretch"><i class="fa-solid fa-check"></i> Apply</button>
    </div>
  </div>
</div>

<!-- Pitch Shift Dialog -->
<div class="daw-modal-bg" id="modal-pitchshift">
  <div class="daw-modal" style="width:360px">
    <h3><i class="fa-solid fa-music fa-fw" style="color:var(--teal)"></i> Pitch Shift <span class="close-btn" id="close-pitchshift"><i class="fa-solid fa-xmark"></i></span></h3>
    <div class="form-group">
      <label class="form-label">Semitones</label>
      <input type="range" class="form-range" id="ps-semitones" min="-12" max="12" value="0" step="1" style="width:100%">
      <div style="display:flex;justify-content:space-between;font-size:11px;color:var(--muted)">
        <span>-12</span>
        <span id="ps-semitones-val" style="font-weight:700;color:var(--teal)">0 st</span>
        <span>+12</span>
      </div>
    </div>
    <div style="margin-top:14px;display:flex;gap:8px;justify-content:flex-end">
      <button class="btn btn-secondary btn-sm" onclick="document.getElementById('modal-pitchshift').classList.remove('open')">Cancel</button>
      <button class="btn btn-primary btn-sm" id="btn-do-pitchshift"><i class="fa-solid fa-check"></i> Apply</button>
    </div>
  </div>
</div>

<!-- Denoise Track Dialog -->
<div class="daw-modal-bg" id="modal-denoise">
  <div class="daw-modal" style="width:400px">
    <h3><i class="fa-solid fa-wand-magic-sparkles fa-fw" style="color:var(--teal)"></i> Denoise Track <span class="close-btn" id="close-denoise"><i class="fa-solid fa-xmark"></i></span></h3>
    <p style="font-size:12px;color:var(--text-dim);margin-bottom:12px">
      Step 1: Select a silent section on the timeline, then capture the noise profile.<br>
      Step 2: Adjust strength and apply noise reduction to all clips on this track.
    </p>
    <div class="form-group">
      <label class="form-label">Noise Print</label>
      <div style="display:flex;gap:8px;align-items:center">
        <button class="btn btn-secondary btn-sm" id="btn-capture-noise"><i class="fa-solid fa-microphone"></i> Capture from Selection</button>
        <span id="noise-print-status" style="font-size:11px;color:var(--muted)">No noise print</span>
      </div>
    </div>
    <div class="form-group">
      <label class="form-label">Reduction Strength</label>
      <input type="range" class="form-range" id="nr-strength" min="0" max="200" value="100" style="width:100%">
      <div style="display:flex;justify-content:space-between;font-size:11px;color:var(--muted)">
        <span>0 (off)</span>
        <span id="nr-strength-val" style="font-weight:700;color:var(--teal)">1.0</span>
        <span>2.0 (max)</span>
      </div>
    </div>
    <div style="margin-top:14px;display:flex;gap:8px;justify-content:flex-end">
      <button class="btn btn-secondary btn-sm" id="btn-restore-audio"><i class="fa-solid fa-rotate-left"></i> Restore Original</button>
      <button class="btn btn-primary btn-sm" id="btn-apply-denoise"><i class="fa-solid fa-check"></i> Apply Denoise</button>
    </div>
  </div>
</div>

<!-- Markers / Regions List Panel -->
<div class="daw-modal-bg" id="modal-markers">
  <div class="daw-modal" style="width:420px">
    <h3><i class="fa-solid fa-flag fa-fw" style="color:var(--teal)"></i> Markers &amp; Regions <span class="close-btn" id="close-markers"><i class="fa-solid fa-xmark"></i></span></h3>

    <div style="margin-bottom:10px">
      <div style="display:flex;align-items:center;gap:6px;margin-bottom:6px">
        <strong style="font-size:12px;color:var(--text)">Markers</strong>
        <button class="btn btn-secondary btn-xs" id="btn-add-marker-modal" title="Add marker at playhead"><i class="fa-solid fa-plus"></i></button>
      </div>
      <div class="markers-panel" id="marker-list-panel">
        <div style="text-align:center;padding:10px;color:var(--muted);font-size:11px">No markers</div>
      </div>
    </div>

    <div>
      <div style="display:flex;align-items:center;gap:6px;margin-bottom:6px">
        <strong style="font-size:12px;color:var(--text)">Regions</strong>
        <button class="btn btn-secondary btn-xs" id="btn-add-region-modal" title="Add region (2s from playhead)"><i class="fa-solid fa-plus"></i></button>
      </div>
      <div class="markers-panel" id="region-list-panel">
        <div style="text-align:center;padding:10px;color:var(--muted);font-size:11px">No regions</div>
      </div>
    </div>
  </div>
</div>

<script src="/js/daw-waveform.js"></script>
<script src="/js/daw-engine.js"></script>

<script>
/* ── DAW Mic Recording Module ──────────────────────────────────────────── */
var _recStream = null, _recCtx = null, _recAnalyser = null, _recRecorder = null;
var _recChunks = [], _recStartTime = 0, _recTimerInt = null, _recLevelInt = null;

function openRecordPanel() {
    document.getElementById('rec-modal-bg').classList.add('open');
    _enumRecDevices();
    _populateRecTracks();
}
function closeRecordPanel() {
    stopDawRecording();
    document.getElementById('rec-modal-bg').classList.remove('open');
}

function _enumRecDevices() {
    var sel = document.getElementById('rec-device');
    sel.innerHTML = '<option value="">Detecting...</option>';
    navigator.mediaDevices.enumerateDevices().then(function(devs) {
        var mics = devs.filter(function(d){ return d.kind === 'audioinput'; });
        sel.innerHTML = '';
        if (!mics.length) { sel.innerHTML = '<option value="">No microphones found</option>'; return; }
        mics.forEach(function(d) {
            var opt = document.createElement('option');
            opt.value = d.deviceId;
            opt.textContent = d.label || ('Mic ' + d.deviceId.substring(0,8));
            sel.appendChild(opt);
        });
    }).catch(function() {
        sel.innerHTML = '<option value="">Permission denied</option>';
    });
    navigator.mediaDevices.addEventListener('devicechange', _enumRecDevices);
}

function _populateRecTracks() {
    var sel = document.getElementById('rec-track');
    sel.innerHTML = '<option value="_new">+ New Track</option>';
    if (window._daw && window._daw.tracks) {
        window._daw.tracks.forEach(function(t) {
            var opt = document.createElement('option');
            opt.value = t.id;
            opt.textContent = t.name;
            sel.appendChild(opt);
        });
    }
}

function startDawRecording() {
    var deviceId = document.getElementById('rec-device').value;
    if (!deviceId) { mc1Toast('Select a microphone first', 'err'); return; }

    var constraints = {
        audio: {
            deviceId: { exact: deviceId },
            sampleRate: 48000,
            channelCount: 1,
            echoCancellation: false,
            noiseSuppression: false,
            autoGainControl: false
        }
    };

    navigator.mediaDevices.getUserMedia(constraints).then(function(stream) {
        _recStream = stream;
        _recCtx = new (window.AudioContext || window.webkitAudioContext)({sampleRate: 48000});
        var src = _recCtx.createMediaStreamSource(stream);

        /* Level meter via analyser */
        _recAnalyser = _recCtx.createAnalyser();
        _recAnalyser.fftSize = 256;
        src.connect(_recAnalyser);

        /* Monitor (hear yourself) */
        if (document.getElementById('rec-monitor').checked) {
            src.connect(_recCtx.destination);
        }

        /* MediaRecorder */
        var fmt = document.getElementById('rec-format').value;
        var mimeType = fmt === 'wav' ? 'audio/webm;codecs=pcm' : 'audio/webm;codecs=opus';
        if (!MediaRecorder.isTypeSupported(mimeType)) mimeType = 'audio/webm';
        _recRecorder = new MediaRecorder(stream, { mimeType: mimeType });
        _recChunks = [];
        _recRecorder.ondataavailable = function(e) { if (e.data.size > 0) _recChunks.push(e.data); };
        _recRecorder.onstop = function() { _onRecordingComplete(); };
        _recRecorder.start(500); /* 500ms chunks */

        _recStartTime = Date.now();
        document.getElementById('btn-rec-start').disabled = true;
        document.getElementById('btn-rec-stop').disabled = false;
        document.getElementById('rec-status').textContent = 'Recording...';
        document.getElementById('rec-status').style.color = '#ef4444';

        /* Timer */
        _recTimerInt = setInterval(function() {
            var elapsed = (Date.now() - _recStartTime) / 1000;
            var m = Math.floor(elapsed / 60);
            var s = Math.floor(elapsed % 60);
            var ms = Math.floor((elapsed % 1) * 10);
            document.getElementById('rec-timer').textContent =
                (m < 10 ? '0' : '') + m + ':' + (s < 10 ? '0' : '') + s + '.' + ms;
        }, 100);

        /* Level meter */
        var data = new Uint8Array(_recAnalyser.frequencyBinCount);
        _recLevelInt = setInterval(function() {
            _recAnalyser.getByteFrequencyData(data);
            var sum = 0; for (var i = 0; i < data.length; i++) sum += data[i];
            var avg = sum / data.length;
            var pct = Math.min(100, (avg / 128) * 100);
            var db = avg > 0 ? (20 * Math.log10(avg / 255)).toFixed(1) : '-∞';
            document.getElementById('rec-level-bar').style.width = pct + '%';
            document.getElementById('rec-level-db').textContent = db + ' dB';
        }, 50);

        /* Pulse the record button */
        document.getElementById('btn-record').classList.add('recording');

    }).catch(function(err) {
        mc1Toast('Mic access denied: ' + err.message, 'err');
    });
}

function stopDawRecording() {
    if (_recRecorder && _recRecorder.state !== 'inactive') {
        _recRecorder.stop();
    }
    if (_recTimerInt) { clearInterval(_recTimerInt); _recTimerInt = null; }
    if (_recLevelInt) { clearInterval(_recLevelInt); _recLevelInt = null; }
    if (_recStream) { _recStream.getTracks().forEach(function(t){ t.stop(); }); _recStream = null; }
    if (_recCtx) { _recCtx.close().catch(function(){}); _recCtx = null; }
    document.getElementById('btn-rec-start').disabled = false;
    document.getElementById('btn-rec-stop').disabled = true;
    document.getElementById('btn-record').classList.remove('recording');
    document.getElementById('rec-level-bar').style.width = '0%';
    document.getElementById('rec-level-db').textContent = '-∞ dB';
}

function _onRecordingComplete() {
    if (!_recChunks.length) { document.getElementById('rec-status').textContent = 'No audio recorded'; return; }
    var blob = new Blob(_recChunks, { type: _recChunks[0].type });
    document.getElementById('rec-status').textContent = 'Processing ' + (blob.size / 1024).toFixed(0) + ' KB...';
    document.getElementById('rec-status').style.color = 'var(--teal)';

    /* Decode the recorded audio into an AudioBuffer */
    var reader = new FileReader();
    reader.onload = function() {
        var decodeCtx = new (window.AudioContext || window.webkitAudioContext)();
        decodeCtx.decodeAudioData(reader.result).then(function(audioBuffer) {
            /* Add as clip to the selected track */
            var trackId = document.getElementById('rec-track').value;
            var mode = document.getElementById('rec-mode').value;

            if (!window._daw) { mc1Toast('DAW not initialized', 'err'); return; }

            if (trackId === '_new') {
                window._daw.addTrack('Recording ' + new Date().toLocaleTimeString());
                var tracks = window._daw.tracks;
                trackId = tracks[tracks.length - 1].id;
            }

            var startTime = 0;
            if (mode === 'at_cursor') {
                startTime = window._daw.currentTime || 0;
            } else if (mode === 'at_end') {
                startTime = window._daw.duration || 0;
            }

            /* Create a clip from the recorded buffer */
            var clip = {
                id: 'rec_' + Date.now(),
                audioBuffer: audioBuffer,
                startTime: startTime,
                duration: audioBuffer.duration,
                offset: 0,
                fadeIn: 0.05,
                fadeOut: 0.05,
                gainEnvelope: []
            };

            /* Find the track and add the clip */
            var track = window._daw.tracks.find(function(t){ return t.id === trackId; });
            if (track) {
                track.clips.push(clip);
                window._daw._computePeaks(clip);
                window._daw.render();
                mc1Toast('Recording added to ' + track.name + ' (' + audioBuffer.duration.toFixed(1) + 's)', 'ok');
            }

            document.getElementById('rec-status').textContent = 'Recording added — ' + audioBuffer.duration.toFixed(1) + 's';
            decodeCtx.close().catch(function(){});
        }).catch(function(err) {
            mc1Toast('Failed to decode recording: ' + err.message, 'err');
            document.getElementById('rec-status').textContent = 'Decode failed';
        });
    };
    reader.readAsArrayBuffer(blob);
}

(function(){
    var daw = null;

    document.addEventListener('DOMContentLoaded', function(){
        daw = new DawEngine('daw-root');
        window._daw = daw; /* expose for recording module */
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

        // Load from library — uses the full media picker component
        document.getElementById('btn-load-library').addEventListener('click', function(){
            if (window.mc1MediaPicker) {
                mc1MediaPicker.open({
                    type: 'audio',
                    onSelect: function(track) {
                        if (track && track.id) {
                            daw.addClipFromLibrary(track.id, track.title || track.file_path || 'Track');
                        }
                    }
                });
            }
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
            var fmt = this.value;
            var showBitrate = (fmt === 'mp3' || fmt === 'ogg' || fmt === 'aac' || fmt === 'opus');
            var showQuality = (fmt === 'flac' || fmt === 'ogg');
            var showBitDepth = (fmt === 'wav');
            document.getElementById('export-bitrate-group').style.display = showBitrate ? '' : 'none';
            document.getElementById('export-quality-group').style.display = showQuality ? '' : 'none';
            document.getElementById('export-bit-depth-group').style.display = showBitDepth ? '' : 'none';
            // Update bitrate options for different codecs
            var brSel = document.getElementById('export-bitrate');
            if (fmt === 'aac') {
                brSel.innerHTML = '<option value="64k">64 kbps</option><option value="128k" selected>128 kbps</option><option value="256k">256 kbps</option>';
            } else if (fmt === 'opus') {
                brSel.innerHTML = '<option value="48k">48 kbps</option><option value="96k">96 kbps</option><option value="128k" selected>128 kbps</option>';
            } else {
                brSel.innerHTML = '<option value="128k">128 kbps</option><option value="192k" selected>192 kbps</option><option value="256k">256 kbps</option><option value="320k">320 kbps</option>';
            }
        });
        document.getElementById('btn-do-export').addEventListener('click', function(){ daw.exportMixdown(); });

        // Master fader
        document.getElementById('master-fader').addEventListener('input', function(){
            var vol = parseFloat(this.value) / 100;
            daw.setMasterVolume(vol);
            document.getElementById('master-vol-val').textContent = this.value + '%';
        });

        // Limiter bypass
        document.getElementById('btn-limiter-bypass').addEventListener('click', function(){
            var bp = !daw.masterLimiterBypass;
            daw.setMasterLimiterBypass(bp);
            this.classList.toggle('bypassed', bp);
            mc1Toast('Master limiter ' + (bp ? 'bypassed' : 'active'), 'ok');
        });

        // Effects panel
        document.getElementById('fx-panel-close').addEventListener('click', function(){
            document.getElementById('fx-panel').classList.remove('open');
        });
        document.getElementById('fx-add-type').addEventListener('change', function(){
            var type = this.value;
            if (!type) return;
            this.value = '';
            var tid = document.getElementById('fx-panel').dataset.trackId;
            if (!tid) return;
            var defaults = {
                eq: {lowFreq:200, lowGain:0, midFreq:1000, midGain:0, midQ:1, highFreq:5000, highGain:0},
                compressor: {threshold:-18, knee:10, ratio:4, attack:0.01, release:0.15},
                reverb: {mix:0.3, decay:2.0},
                delay: {time:0.3, feedback:0.4, mix:0.3},
                gain: {gain:1.0}
            };
            daw.addTrackEffect(tid, type, defaults[type] || {});
            renderFxPanel(tid);
            updateTrackFxButtons();
        });

        // Aux bus panel
        document.getElementById('btn-buses').addEventListener('click', function(){
            document.getElementById('aux-panel').classList.toggle('open');
            renderAuxPanel();
        });
        document.getElementById('aux-panel-close').addEventListener('click', function(){
            document.getElementById('aux-panel').classList.remove('open');
        });
        document.getElementById('aux-add-type').addEventListener('change', function(){
            var type = this.value;
            if (!type) return;
            this.value = '';
            var defaults = {
                reverb: {mix:0.5, decay:2.0},
                delay: {time:0.3, feedback:0.4, mix:0.5},
                compressor: {threshold:-12, knee:10, ratio:4, attack:0.01, release:0.15}
            };
            daw.createAuxBus(type.charAt(0).toUpperCase() + type.slice(1) + ' Bus', type, defaults[type] || {});
            renderAuxPanel();
        });

        // Context menu
        document.querySelectorAll('#ctx-menu .daw-ctx-item').forEach(function(el){
            el.addEventListener('click', function(){
                var act = el.dataset.action;
                if (act === 'timestretch') {
                    document.getElementById('ts-factor').value = 100;
                    document.getElementById('ts-factor-val').textContent = '1.00x';
                    document.getElementById('modal-timestretch').classList.add('open');
                } else if (act === 'pitchshift') {
                    document.getElementById('ps-semitones').value = 0;
                    document.getElementById('ps-semitones-val').textContent = '0 st';
                    document.getElementById('modal-pitchshift').classList.add('open');
                } else {
                    daw.handleContextAction(act);
                }
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
            if (e.code === 'KeyM' && !e.ctrlKey && !e.altKey) { e.preventDefault(); daw.addMarker(daw.playPos); }
        });

        // ── Marker / Region toolbar buttons ──
        document.getElementById('btn-add-marker').addEventListener('click', function(){
            daw.addMarker(daw.playPos);
        });
        document.getElementById('btn-prev-marker').addEventListener('click', function(){
            daw.jumpToPrevMarker();
        });
        document.getElementById('btn-next-marker').addEventListener('click', function(){
            daw.jumpToNextMarker();
        });
        document.getElementById('btn-marker-list').addEventListener('click', function(){
            document.getElementById('modal-markers').classList.add('open');
            renderMarkerList();
        });
        document.getElementById('close-markers').addEventListener('click', function(){
            document.getElementById('modal-markers').classList.remove('open');
        });
        document.getElementById('btn-add-marker-modal').addEventListener('click', function(){
            daw.addMarker(daw.playPos);
            renderMarkerList();
        });
        document.getElementById('btn-add-region-modal').addEventListener('click', function(){
            daw.addRegion(daw.playPos, daw.playPos + 2, 'Region');
            renderMarkerList();
        });

        // ── Time Stretch Dialog ──
        document.getElementById('ts-factor').addEventListener('input', function(){
            var v = (parseFloat(this.value) / 100).toFixed(2);
            document.getElementById('ts-factor-val').textContent = v + 'x';
        });
        document.getElementById('close-timestretch').addEventListener('click', function(){
            document.getElementById('modal-timestretch').classList.remove('open');
        });
        document.getElementById('btn-do-timestretch').addEventListener('click', function(){
            var factor = parseFloat(document.getElementById('ts-factor').value) / 100;
            if (daw.ctxClip) {
                daw.stretchClip(daw.ctxClip.id, factor);
            } else if (daw.selectedClip) {
                daw.stretchClip(daw.selectedClip, factor);
            }
            document.getElementById('modal-timestretch').classList.remove('open');
        });

        // ── Pitch Shift Dialog ──
        document.getElementById('ps-semitones').addEventListener('input', function(){
            var v = parseInt(this.value);
            document.getElementById('ps-semitones-val').textContent = (v >= 0 ? '+' : '') + v + ' st';
        });
        document.getElementById('close-pitchshift').addEventListener('click', function(){
            document.getElementById('modal-pitchshift').classList.remove('open');
        });
        document.getElementById('btn-do-pitchshift').addEventListener('click', function(){
            var semitones = parseInt(document.getElementById('ps-semitones').value);
            if (daw.ctxClip) {
                daw.pitchShiftClip(daw.ctxClip.id, semitones);
            } else if (daw.selectedClip) {
                daw.pitchShiftClip(daw.selectedClip, semitones);
            }
            document.getElementById('modal-pitchshift').classList.remove('open');
        });

        // ── Denoise Dialog ──
        var denoiseTrackId = null;
        window._openDenoiseDialog = function(trackId) {
            denoiseTrackId = trackId;
            var hasNP = !!daw.trackNoisePrints[trackId];
            document.getElementById('noise-print-status').textContent = hasNP ? 'Noise print captured' : 'No noise print';
            document.getElementById('nr-strength').value = 100;
            document.getElementById('nr-strength-val').textContent = '1.0';
            document.getElementById('modal-denoise').classList.add('open');
        };
        document.getElementById('close-denoise').addEventListener('click', function(){
            document.getElementById('modal-denoise').classList.remove('open');
        });
        document.getElementById('nr-strength').addEventListener('input', function(){
            document.getElementById('nr-strength-val').textContent = (parseFloat(this.value) / 100).toFixed(1);
        });
        document.getElementById('btn-capture-noise').addEventListener('click', function(){
            if (!denoiseTrackId) return;
            // Use playPos as center, capture 1 second
            var start = Math.max(0, daw.playPos - 0.5);
            var end = daw.playPos + 0.5;
            daw.captureTrackNoisePrint(denoiseTrackId, start, end);
            document.getElementById('noise-print-status').textContent =
                daw.trackNoisePrints[denoiseTrackId] ? 'Noise print captured' : 'Failed';
        });
        document.getElementById('btn-apply-denoise').addEventListener('click', function(){
            if (!denoiseTrackId) return;
            var strength = parseFloat(document.getElementById('nr-strength').value) / 100;
            daw.applyTrackNoiseReduction(denoiseTrackId, strength);
            document.getElementById('modal-denoise').classList.remove('open');
        });
        document.getElementById('btn-restore-audio').addEventListener('click', function(){
            if (!denoiseTrackId) return;
            daw.restoreTrackOriginalAudio(denoiseTrackId);
            document.getElementById('noise-print-status').textContent = 'No noise print';
        });
    });

    function renderMarkerList() {
        if (!daw) return;
        // Markers
        var mp = document.getElementById('marker-list-panel');
        if (daw.markers.length === 0) {
            mp.innerHTML = '<div style="text-align:center;padding:10px;color:var(--muted);font-size:11px">No markers</div>';
        } else {
            mp.innerHTML = '';
            daw.markers.forEach(function(m) {
                var row = document.createElement('div');
                row.className = 'marker-row';
                row.innerHTML = '<div class="m-color" style="background:' + esc(m.color) + '"></div>' +
                    '<span class="m-name">' + esc(m.name) + '</span>' +
                    '<span class="m-time">' + fmtTime(m.time) + '</span>' +
                    '<span class="m-del" data-id="' + esc(m.id) + '"><i class="fa-solid fa-trash"></i></span>';
                row.addEventListener('click', function(e) {
                    if (e.target.closest('.m-del')) return;
                    daw.seek(m.time);
                });
                row.querySelector('.m-del').addEventListener('click', function(e) {
                    e.stopPropagation();
                    daw.removeMarker(m.id);
                    renderMarkerList();
                });
                mp.appendChild(row);
            });
        }
        // Regions
        var rp = document.getElementById('region-list-panel');
        if (daw.regions.length === 0) {
            rp.innerHTML = '<div style="text-align:center;padding:10px;color:var(--muted);font-size:11px">No regions</div>';
        } else {
            rp.innerHTML = '';
            daw.regions.forEach(function(r) {
                var row = document.createElement('div');
                row.className = 'region-row';
                row.innerHTML = '<div class="r-color" style="background:' + esc(r.color) + '"></div>' +
                    '<span class="r-name">' + esc(r.name) + '</span>' +
                    '<span class="r-time">' + fmtTime(r.startTime) + ' - ' + fmtTime(r.endTime) + '</span>' +
                    '<span class="r-del" data-id="' + esc(r.id) + '"><i class="fa-solid fa-trash"></i></span>';
                row.addEventListener('click', function(e) {
                    if (e.target.closest('.r-del')) return;
                    daw.seek(r.startTime);
                });
                row.querySelector('.r-del').addEventListener('click', function(e) {
                    e.stopPropagation();
                    daw.removeRegion(r.id);
                    renderMarkerList();
                });
                rp.appendChild(row);
            });
        }
    }

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

    /* ── FX Panel Rendering ── */
    function openFxPanel(trackId) {
        var panel = document.getElementById('fx-panel');
        var track = daw._getTrack(trackId);
        panel.dataset.trackId = trackId;
        document.getElementById('fx-panel-title').textContent = (track ? track.name : 'Track') + ' Effects';
        panel.classList.add('open');
        renderFxPanel(trackId);
    }
    // Expose for track panel buttons
    window._openFxPanel = openFxPanel;

    function renderFxPanel(trackId) {
        var container = document.getElementById('fx-chain');
        var effects = daw.getTrackEffects(trackId);
        container.innerHTML = '';
        if (effects.length === 0) {
            container.innerHTML = '<div style="text-align:center;padding:20px;color:var(--muted);font-size:12px">No effects. Add one below.</div>';
            return;
        }
        effects.forEach(function(fx, idx) {
            var card = document.createElement('div');
            card.className = 'fx-card';
            card.dataset.index = idx;

            var header = document.createElement('div');
            header.className = 'fx-card-header';
            header.innerHTML = '<span class="fx-type">' + esc(fx.type) + '</span>' +
                '<span class="fx-remove" data-idx="' + idx + '" title="Remove"><i class="fa-solid fa-trash"></i></span>';
            card.appendChild(header);

            var body = document.createElement('div');
            body.className = 'fx-card-body';
            var params = fx.params;

            if (fx.type === 'eq') {
                body.appendChild(makeFxSlider(trackId, idx, 'lowGain', 'Low', params.lowGain || 0, -12, 12, 0.5, 'dB'));
                body.appendChild(makeFxSlider(trackId, idx, 'midGain', 'Mid', params.midGain || 0, -12, 12, 0.5, 'dB'));
                body.appendChild(makeFxSlider(trackId, idx, 'highGain', 'High', params.highGain || 0, -12, 12, 0.5, 'dB'));
                body.appendChild(makeFxSlider(trackId, idx, 'midFreq', 'Freq', params.midFreq || 1000, 200, 8000, 50, 'Hz'));
                body.appendChild(makeFxSlider(trackId, idx, 'midQ', 'Q', params.midQ || 1, 0.1, 10, 0.1, ''));
            } else if (fx.type === 'compressor') {
                body.appendChild(makeFxSlider(trackId, idx, 'threshold', 'Thresh', params.threshold, -60, 0, 1, 'dB'));
                body.appendChild(makeFxSlider(trackId, idx, 'ratio', 'Ratio', params.ratio, 1, 20, 0.5, ':1'));
                body.appendChild(makeFxSlider(trackId, idx, 'attack', 'Attack', params.attack, 0.001, 0.5, 0.001, 's'));
                body.appendChild(makeFxSlider(trackId, idx, 'release', 'Release', params.release, 0.01, 1, 0.01, 's'));
                body.appendChild(makeFxSlider(trackId, idx, 'knee', 'Knee', params.knee, 0, 40, 1, 'dB'));
            } else if (fx.type === 'reverb') {
                body.appendChild(makeFxSlider(trackId, idx, 'mix', 'Mix', params.mix, 0, 1, 0.01, ''));
                body.appendChild(makeFxSlider(trackId, idx, 'decay', 'Decay', params.decay, 0.1, 8, 0.1, 's'));
            } else if (fx.type === 'delay') {
                body.appendChild(makeFxSlider(trackId, idx, 'time', 'Time', params.time, 0.01, 2, 0.01, 's'));
                body.appendChild(makeFxSlider(trackId, idx, 'feedback', 'FB', params.feedback, 0, 0.95, 0.01, ''));
                body.appendChild(makeFxSlider(trackId, idx, 'mix', 'Mix', params.mix, 0, 1, 0.01, ''));
            } else if (fx.type === 'gain') {
                body.appendChild(makeFxSlider(trackId, idx, 'gain', 'Gain', params.gain, 0, 2, 0.01, ''));
            }

            card.appendChild(body);
            container.appendChild(card);
        });

        // Bind remove buttons
        container.querySelectorAll('.fx-remove').forEach(function(el) {
            el.addEventListener('click', function(e) {
                e.stopPropagation();
                var i = parseInt(el.dataset.idx);
                daw.removeTrackEffect(trackId, i);
                renderFxPanel(trackId);
                updateTrackFxButtons();
            });
        });
    }

    function makeFxSlider(trackId, fxIdx, paramKey, label, value, min, max, step, unit) {
        var wrap = document.createElement('div');
        wrap.className = 'fx-param';
        var uid = 'fxp-' + trackId + '-' + fxIdx + '-' + paramKey;
        wrap.innerHTML = '<label>' + esc(label) + '</label>' +
            '<input type="range" id="' + uid + '" min="' + min + '" max="' + max + '" step="' + step + '" value="' + value + '">' +
            '<span class="fx-val" id="' + uid + '-v">' + parseFloat(value).toFixed(2) + (unit ? ' ' + unit : '') + '</span>';
        setTimeout(function() {
            var inp = document.getElementById(uid);
            var valEl = document.getElementById(uid + '-v');
            if (!inp) return;
            inp.addEventListener('input', function() {
                var v = parseFloat(this.value);
                valEl.textContent = v.toFixed(2) + (unit ? ' ' + unit : '');
                var update = {};
                update[paramKey] = v;
                daw.updateTrackEffect(trackId, fxIdx, update);
            });
        }, 30);
        return wrap;
    }

    function updateTrackFxButtons() {
        if (!daw) return;
        document.querySelectorAll('.track-fx-btn').forEach(function(btn) {
            var tid = btn.dataset.track;
            var chain = daw.trackEffects[tid] || [];
            btn.classList.toggle('has-fx', chain.length > 0);
            btn.title = chain.length > 0 ? chain.length + ' effect(s)' : 'Add effects';
        });
    }

    /* ── Aux Bus Panel Rendering ── */
    function renderAuxPanel() {
        var container = document.getElementById('aux-bus-list');
        container.innerHTML = '';
        if (daw.auxBuses.length === 0) {
            container.innerHTML = '<div style="text-align:center;padding:20px;color:var(--muted);font-size:12px">No aux buses. Add one below.</div>';
            return;
        }
        daw.auxBuses.forEach(function(bus, bi) {
            var card = document.createElement('div');
            card.className = 'aux-bus-card';

            var hdr = '<div style="display:flex;align-items:center;gap:6px;margin-bottom:8px">' +
                '<h5 style="flex:1;margin:0">' + esc(bus.name) + '</h5>' +
                '<span style="color:var(--muted);font-size:10px">' + bus.effectType + '</span>' +
                '<span class="fx-remove" data-bus="' + bi + '" title="Remove bus" style="cursor:pointer;color:var(--red);font-size:11px;opacity:.5"><i class="fa-solid fa-trash"></i></span>' +
                '</div>';
            card.innerHTML = hdr;

            // Send level sliders for each track
            daw.tracks.forEach(function(track) {
                var sendLevel = 0;
                if (bus.sendGains[track.id]) sendLevel = bus.sendGains[track.id].level || 0;
                var row = document.createElement('div');
                row.className = 'aux-send-row';
                var sid = 'aux-send-' + bus.id + '-' + track.id;
                row.innerHTML = '<span>' + esc(track.name) + '</span>' +
                    '<input type="range" id="' + sid + '" min="0" max="100" value="' + Math.round(sendLevel * 100) + '">' +
                    '<span class="send-val" id="' + sid + '-v">' + Math.round(sendLevel * 100) + '%</span>';
                card.appendChild(row);
                setTimeout(function() {
                    var inp = document.getElementById(sid);
                    var valEl = document.getElementById(sid + '-v');
                    if (!inp) return;
                    inp.addEventListener('input', function() {
                        var lvl = parseFloat(this.value) / 100;
                        valEl.textContent = Math.round(lvl * 100) + '%';
                        daw.setAuxSend(track.id, bus.id, lvl);
                    });
                }, 30);
            });

            container.appendChild(card);
        });

        // Remove bus handlers
        container.querySelectorAll('.fx-remove[data-bus]').forEach(function(el) {
            el.addEventListener('click', function(e) {
                e.stopPropagation();
                var idx = parseInt(el.dataset.bus);
                if (idx >= 0 && idx < daw.auxBuses.length) {
                    daw.removeAuxBus(daw.auxBuses[idx].id);
                    renderAuxPanel();
                }
            });
        });
    }
})();
</script>

<?php require_once __DIR__ . '/app/inc/footer.php'; ?>
