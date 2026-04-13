<?php
/**
 * forensic.php — Forensic Audio Spectral Analysis
 *
 * File:    src/linux/web_ui/forensic.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Phase:   FA-3
 * Purpose: We provide a deep spectral analysis page targeting paranormal investigators,
 *          forensic analysts, and audio researchers. We use WebGL 2.0 for high-resolution
 *          spectrogram rendering with configurable FFT sizes up to 65536, multiple window
 *          functions, color maps, frequency scales, region selection, filtered playback,
 *          annotations, optional Ollama AI analysis, spectral noise reduction, band
 *          isolation, amplitude envelope, WSOLA pitch-preserved speed change, spectrum
 *          peak detection, side-by-side compare mode with difference view, professional
 *          HTML report generation with chain-of-custody, AI spectrum analysis with
 *          frequency distribution context, automatic event detection, and stereo
 *          phase correlation goniometer display.
 *
 * Standards:
 *  - We never call exit() or die() -- uopz extension is active on this server
 *  - We wrap all startup JS in DOMContentLoaded
 *  - We use h() for all user data rendered into HTML
 *  - We use the dark navy/teal theme from header.php
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/app/inc/mc1_config.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/traits.db.class.php';
require_once __DIR__ . '/app/inc/logger.php';
require_once __DIR__ . '/app/inc/auth.php';
require_once __DIR__ . '/app/inc/user_auth.php';

if (!mc1_is_authed()) {
    http_response_code(302);
    header('Location: /login');
    return;
}

$page_title = 'Forensic Audio Analysis';
$active_nav = 'forensic';
$use_charts = false;
require_once __DIR__ . '/app/inc/header.php';
?>

<style>
/* ── Forensic page layout ── */
.forensic-toolbar{display:flex;align-items:center;gap:10px;padding:10px 14px;background:var(--card);border:1px solid var(--border);border-radius:var(--radius);flex-wrap:wrap}
.forensic-toolbar .file-info{font-size:12px;color:var(--text-dim);flex:1;min-width:200px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.forensic-toolbar .file-info strong{color:var(--text);margin-right:6px}

.forensic-body{display:grid;grid-template-columns:180px 1fr;gap:0;flex:1;min-height:0;overflow:hidden}

/* Spectrogram container */
.spectrogram-panel{display:flex;flex-direction:column;gap:0;overflow:hidden}
.spectrogram-wrap{position:relative;flex:1;min-height:200px;background:#080c18;border:1px solid var(--border);border-radius:var(--radius-sm);overflow:hidden}
.spectrogram-wrap canvas{position:absolute;top:0;left:0;width:100%;height:100%}
#spectrogram-overlay{z-index:2;pointer-events:auto;cursor:crosshair}
#spectrogram-canvas{z-index:1}
.spectrogram-cursor{position:absolute;bottom:0;left:0;right:0;padding:4px 8px;background:rgba(0,0,0,.7);font-size:11px;color:var(--teal);font-family:'SF Mono','Fira Code',monospace;z-index:3;pointer-events:none;display:flex;gap:16px}
.spectrogram-cursor span{white-space:nowrap}

/* Axis labels */
.freq-axis{position:absolute;top:0;left:0;bottom:0;width:48px;z-index:4;pointer-events:none;display:flex;flex-direction:column;justify-content:space-between;padding:4px 0}
.freq-axis span{font-size:9px;color:rgba(255,255,255,.5);text-align:right;padding-right:4px;font-family:'SF Mono','Fira Code',monospace}
.time-axis{position:absolute;bottom:22px;left:48px;right:0;height:16px;z-index:4;pointer-events:none;display:flex;justify-content:space-between;padding:0 4px}
.time-axis span{font-size:9px;color:rgba(255,255,255,.4);font-family:'SF Mono','Fira Code',monospace}

/* Waveform overview */
.waveform-panel{height:80px;background:var(--card);border:1px solid var(--border);border-radius:var(--radius-sm);position:relative;overflow:hidden}
.waveform-panel canvas{width:100%;height:100%}
.waveform-sel{position:absolute;top:0;bottom:0;background:rgba(20,184,166,.15);border-left:1px solid var(--teal);border-right:1px solid var(--teal);pointer-events:none;z-index:2}

/* Controls sidebar */
.forensic-controls{background:var(--card);border:1px solid var(--border);border-right:none;border-radius:var(--radius) 0 0 var(--radius);padding:12px;overflow-y:auto;display:flex;flex-direction:column;gap:14px}
.ctrl-group{display:flex;flex-direction:column;gap:4px}
.ctrl-label{font-size:10px;font-weight:700;text-transform:uppercase;letter-spacing:.07em;color:var(--muted)}
.ctrl-select{width:100%;padding:5px 8px;background:rgba(255,255,255,.05);border:1px solid var(--border);border-radius:var(--radius-xs);color:var(--text);font-size:12px;outline:none}
.ctrl-select:focus{border-color:var(--teal)}
.ctrl-select option{background:#1e293b;color:var(--text)}
.ctrl-range{width:100%;margin:2px 0}
.ctrl-val{font-size:11px;color:var(--text-dim);text-align:center;font-family:'SF Mono','Fira Code',monospace}

/* Bottom panels */
.forensic-bottom{display:grid;grid-template-columns:1fr 1fr;gap:14px}
.annotations-panel,.playback-panel{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:12px}
.annotations-panel .card-title,.playback-panel .card-title{font-size:13px;font-weight:600;color:var(--text);margin-bottom:10px;display:flex;align-items:center;gap:6px}
.anno-list{max-height:140px;overflow-y:auto;display:flex;flex-direction:column;gap:4px}
.anno-item{display:flex;align-items:center;gap:6px;font-size:11px;color:var(--text-dim);padding:4px 6px;background:rgba(255,255,255,.03);border-radius:var(--radius-xs);cursor:pointer}
.anno-item:hover{background:rgba(20,184,166,.08);color:var(--text)}
.anno-time{font-family:'SF Mono','Fira Code',monospace;color:var(--teal);min-width:60px}
.anno-freq{font-family:'SF Mono','Fira Code',monospace;color:var(--cyan);min-width:55px}
.anno-text{flex:1;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.anno-del{color:var(--red);cursor:pointer;opacity:.5;padding:2px}
.anno-del:hover{opacity:1}

/* Playback */
.transport-row{display:flex;gap:6px;align-items:center;flex-wrap:wrap;margin-bottom:8px}
.transport-row .btn{padding:5px 10px;font-size:12px}
.speed-select{padding:4px 6px;background:rgba(255,255,255,.05);border:1px solid var(--border);border-radius:var(--radius-xs);color:var(--text);font-size:11px;outline:none}
.speed-select option{background:#1e293b}
.filter-row{display:flex;gap:8px;align-items:center;margin-bottom:8px}
.ai-row{display:flex;gap:6px;align-items:center;margin-top:8px}

/* Selection info */
.sel-info{font-size:11px;color:var(--text-dim);padding:6px 8px;background:rgba(20,184,166,.06);border:1px solid rgba(20,184,166,.15);border-radius:var(--radius-xs);margin-top:6px;display:none}
.sel-info strong{color:var(--teal)}

/* Annotation input modal */
.anno-modal{position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,.6);z-index:5000;display:none;align-items:center;justify-content:center}
.anno-modal.show{display:flex}
.anno-modal-box{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:20px;width:380px;max-width:90vw}
.anno-modal-box h3{font-size:14px;font-weight:600;color:var(--text);margin-bottom:12px}

/* Enhancement panel */
.enhance-panel{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:12px;margin-top:14px}
.enhance-panel .card-title{font-size:13px;font-weight:600;color:var(--text);margin-bottom:10px;display:flex;align-items:center;gap:6px}
.enhance-row{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-bottom:8px}
.enhance-row .enhance-label{font-size:11px;color:var(--muted);min-width:95px;font-weight:600}
.enhance-row .btn{padding:4px 10px;font-size:11px}
.enhance-slider{width:100px}
.enhance-val{font-size:10px;color:var(--text-dim);font-family:'SF Mono','Fira Code',monospace;min-width:45px}
.enhance-check{display:flex;align-items:center;gap:6px;font-size:11px;color:var(--text-dim)}
.enhance-check input[type=checkbox]{accent-color:var(--teal)}

/* Compare mode */
.compare-container{display:none;grid-template-columns:1fr 1fr;gap:14px;margin-top:14px}
.compare-container.active{display:grid}
.compare-panel{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:12px;overflow:hidden}
.compare-panel .panel-title{font-size:12px;font-weight:600;color:var(--text);margin-bottom:8px;display:flex;align-items:center;gap:6px}
.compare-panel .panel-info{font-size:11px;color:var(--text-dim);margin-bottom:6px}
.compare-panel canvas{width:100%;height:200px;background:#080c18;border:1px solid var(--border);border-radius:var(--radius-sm)}
.compare-diff-panel{margin-top:14px;display:none}
.compare-diff-panel.active{display:block}

/* Loading overlay */
.forensic-loading{position:absolute;top:0;left:0;right:0;bottom:0;background:rgba(8,12,24,.85);display:none;align-items:center;justify-content:center;z-index:10;flex-direction:column;gap:10px}
.forensic-loading.show{display:flex}
.forensic-loading .spinner{width:32px;height:32px;border-width:3px}
.forensic-loading span{font-size:13px;color:var(--text-dim)}

/* Minimap */
.minimap{position:absolute;bottom:26px;right:6px;width:120px;height:40px;border:1px solid rgba(255,255,255,.15);background:rgba(0,0,0,.5);z-index:5;border-radius:3px;overflow:hidden}
.minimap canvas{width:100%;height:100%}
.minimap-viewport{position:absolute;top:0;bottom:0;border:1px solid var(--teal);background:rgba(20,184,166,.1);pointer-events:none}

/* Event list */
.event-panel{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:12px;margin-top:14px}
.event-panel .card-title{font-size:13px;font-weight:600;color:var(--text);margin-bottom:10px;display:flex;align-items:center;gap:6px}
.event-filter-row{display:flex;gap:4px;margin-bottom:8px;flex-wrap:wrap}
.event-filter-row .btn{padding:3px 8px;font-size:10px}
.event-list{max-height:180px;overflow-y:auto;display:flex;flex-direction:column;gap:3px}
.event-item{display:flex;align-items:center;gap:6px;font-size:11px;color:var(--text-dim);padding:4px 6px;background:rgba(255,255,255,.03);border-radius:var(--radius-xs);cursor:pointer;transition:background .15s}
.event-item:hover{background:rgba(20,184,166,.08);color:var(--text)}
.event-icon{font-size:13px;min-width:20px;text-align:center}
.event-time{font-family:'SF Mono','Fira Code',monospace;color:var(--teal);min-width:60px}
.event-label{flex:1;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.evt-silence{border-left:3px solid #94a3b8}
.evt-transient{border-left:3px solid #eab308}
.evt-tonal{border-left:3px solid #3b82f6}
.evt-click{border-left:3px solid #ef4444}

/* Goniometer */
.goniometer-panel{display:none;margin-top:8px}
.goniometer-panel .panel-label{font-size:10px;font-weight:700;text-transform:uppercase;letter-spacing:.07em;color:var(--muted);margin-bottom:4px}
#goniometer-canvas{width:200px;height:200px;border:1px solid var(--border);border-radius:var(--radius-sm);background:#080c18}

/* AI panel */
.ai-panel{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:12px;margin-top:14px}
.ai-panel .card-title{font-size:13px;font-weight:600;color:var(--text);margin-bottom:10px;display:flex;align-items:center;gap:6px}
.ai-btn-row{display:flex;gap:6px;align-items:center;flex-wrap:wrap}

/* Report modal */
.report-modal{position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,.6);z-index:5000;display:none;align-items:center;justify-content:center}
.report-modal.show{display:flex}
.report-modal-box{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:20px;width:420px;max-width:90vw}
.report-modal-box h3{font-size:14px;font-weight:600;color:var(--text);margin-bottom:12px}
.rpt-checks{display:flex;flex-direction:column;gap:6px;margin:12px 0}
.rpt-checks label{display:flex;align-items:center;gap:6px;font-size:12px;color:var(--text-dim)}
.rpt-checks input[type=checkbox]{accent-color:var(--teal)}

@media(max-width:860px){
  .forensic-body{grid-template-columns:1fr}
  .forensic-controls{flex-direction:row;flex-wrap:wrap;border-radius:var(--radius);border-right:1px solid var(--border)}
  .ctrl-group{min-width:120px;flex:1}
  .forensic-bottom{grid-template-columns:1fr}
}
</style>

<!-- Toolbar -->
<div class="forensic-toolbar">
    <div class="file-info" id="file-info">
        <strong>No file loaded</strong>
        <span id="file-meta"></span>
    </div>
    <input type="file" id="file-input" accept="audio/*" style="display:none">
    <button class="btn btn-primary btn-sm" onclick="document.getElementById('file-input').click()">
        <i class="fa-solid fa-folder-open"></i> Load File
    </button>
    <button class="btn btn-secondary btn-sm" onclick="mc1MediaPicker.open({type:'audio', onSelect:function(t){ forensic.loadFromLibrary(t.id, t.title); }})">
        <i class="fa-solid fa-database"></i> From Library
    </button>
    <button class="btn btn-secondary btn-sm" id="btn-record" onclick="forensic.toggleRecordLive()">
        <i class="fa-solid fa-circle" style="color:#ef4444"></i> Record Live
    </button>
    <button class="btn btn-secondary btn-sm" id="btn-compare" onclick="forensic.toggleCompare()">
        <i class="fa-solid fa-columns"></i> Compare Mode
    </button>
    <button class="btn btn-secondary btn-sm" onclick="forensic.saveAnalysis()">
        <i class="fa-solid fa-floppy-disk"></i> Save
    </button>
    <button class="btn btn-secondary btn-sm" onclick="forensic.loadAnalysis()">
        <i class="fa-solid fa-folder-tree"></i> Load Analysis
    </button>
    <button class="btn btn-secondary btn-sm" onclick="forensic.detectEvents()">
        <i class="fa-solid fa-magnifying-glass-chart"></i> Detect Events
    </button>
    <button class="btn btn-secondary btn-sm" onclick="forensic.exportSpecPNG()">
        <i class="fa-solid fa-image"></i> Export PNG
    </button>
</div>

<!-- Main body: controls + spectrogram -->
<div class="forensic-body">
    <!-- Controls sidebar -->
    <div class="forensic-controls">
        <div class="ctrl-group">
            <div class="ctrl-label">FFT Size</div>
            <select class="ctrl-select" id="ctl-fftsize" onchange="forensic.setFFTSize(+this.value)">
                <option value="256">256</option>
                <option value="512">512</option>
                <option value="1024">1024</option>
                <option value="2048">2048</option>
                <option value="4096" selected>4096</option>
                <option value="8192">8192</option>
                <option value="16384">16384</option>
                <option value="32768">32768</option>
                <option value="65536">65536</option>
            </select>
        </div>
        <div class="ctrl-group">
            <div class="ctrl-label">Window</div>
            <select class="ctrl-select" id="ctl-window" onchange="forensic.setWindow(this.value)">
                <option value="hann" selected>Hann</option>
                <option value="hamming">Hamming</option>
                <option value="blackman">Blackman</option>
                <option value="blackman-harris">Blackman-Harris</option>
                <option value="kaiser">Kaiser</option>
                <option value="rectangular">Rectangular</option>
            </select>
        </div>
        <div class="ctrl-group">
            <div class="ctrl-label">Color Map</div>
            <select class="ctrl-select" id="ctl-colormap" onchange="forensic.setColormap(this.value)">
                <option value="heat" selected>Heatmap</option>
                <option value="gray">Grayscale</option>
                <option value="rainbow">Rainbow</option>
                <option value="inferno">Inferno</option>
                <option value="ice">Ice</option>
            </select>
        </div>
        <div class="ctrl-group">
            <div class="ctrl-label">Freq Scale</div>
            <select class="ctrl-select" id="ctl-freqscale" onchange="forensic.setFreqScale(this.value)">
                <option value="log" selected>Logarithmic</option>
                <option value="linear">Linear</option>
                <option value="mel">Mel Scale</option>
                <option value="bark">Bark Scale</option>
            </select>
        </div>
        <div class="ctrl-group">
            <div class="ctrl-label">Gain</div>
            <input type="range" class="ctrl-range" id="ctl-gain" min="-30" max="30" value="0" step="1" oninput="forensic.setGain(+this.value); document.getElementById('gain-val').textContent=this.value+' dB'">
            <div class="ctrl-val" id="gain-val">0 dB</div>
        </div>
        <div class="ctrl-group">
            <div class="ctrl-label">Floor</div>
            <input type="range" class="ctrl-range" id="ctl-floor" min="-120" max="-20" value="-96" step="1" oninput="forensic.setFloor(+this.value); document.getElementById('floor-val').textContent=this.value+' dB'">
            <div class="ctrl-val" id="floor-val">-96 dB</div>
        </div>
        <div class="ctrl-group">
            <div class="ctrl-label">Hop Size</div>
            <select class="ctrl-select" id="ctl-hop" onchange="forensic.setHopRatio(+this.value)">
                <option value="0.25">1/4 (Fine)</option>
                <option value="0.5" selected>1/2 (Default)</option>
                <option value="0.75">3/4 (Fast)</option>
            </select>
        </div>
    </div>

    <!-- Spectrogram + waveform -->
    <div class="spectrogram-panel">
        <div class="spectrogram-wrap" id="spectrogram-wrap">
            <canvas id="spectrogram-canvas"></canvas>
            <canvas id="spectrogram-overlay"></canvas>
            <div class="freq-axis" id="freq-axis"></div>
            <div class="time-axis" id="time-axis"></div>
            <div class="spectrogram-cursor" id="cursor-readout" style="display:none">
                <span id="cursor-time">0:00.000</span>
                <span id="cursor-freq">0 Hz</span>
                <span id="cursor-mag">-96.0 dB</span>
            </div>
            <div class="minimap" id="minimap" style="display:none">
                <canvas id="minimap-canvas"></canvas>
                <div class="minimap-viewport" id="minimap-viewport"></div>
            </div>
            <div class="forensic-loading" id="loading-overlay">
                <div class="spinner"></div>
                <span id="loading-text">Computing spectrogram...</span>
            </div>
        </div>
        <div class="waveform-panel" id="waveform-panel">
            <canvas id="waveform-canvas"></canvas>
            <div class="waveform-sel" id="waveform-sel" style="display:none"></div>
        </div>
        <div class="sel-info" id="sel-info">
            Selection: <strong id="sel-time-range"></strong> | <strong id="sel-freq-range"></strong> | Peak: <strong id="sel-peak"></strong> | Avg: <strong id="sel-avg"></strong>
        </div>
    </div>
</div>

<!-- Bottom panels -->
<div class="forensic-bottom">
    <!-- Annotations -->
    <div class="annotations-panel">
        <div class="card-title"><i class="fa-solid fa-map-pin fa-fw" style="color:var(--teal)"></i> Annotations</div>
        <div class="anno-list" id="anno-list">
            <div class="empty" style="padding:16px"><i class="fa-solid fa-map-pin fa-fw"></i> Click spectrogram to add markers</div>
        </div>
        <div style="display:flex;gap:6px;margin-top:8px;flex-wrap:wrap">
            <button class="btn btn-secondary btn-xs" onclick="forensic.exportAnnotations()"><i class="fa-solid fa-download"></i> Export JSON</button>
            <button class="btn btn-primary btn-xs" onclick="forensic.exportReport()"><i class="fa-solid fa-file-lines"></i> Export Report</button>
            <button class="btn btn-secondary btn-xs" onclick="forensic.exportSpecPNG()"><i class="fa-solid fa-image"></i> Export PNG</button>
        </div>
    </div>

    <!-- Playback -->
    <div class="playback-panel">
        <div class="card-title"><i class="fa-solid fa-play fa-fw" style="color:var(--teal)"></i> Playback</div>
        <div class="transport-row">
            <button class="btn btn-secondary btn-sm" onclick="forensic.seekStart()" title="Start"><i class="fa-solid fa-backward-step"></i></button>
            <button class="btn btn-primary btn-sm" id="btn-play" onclick="forensic.togglePlay()" title="Play/Pause"><i class="fa-solid fa-play"></i></button>
            <button class="btn btn-secondary btn-sm" onclick="forensic.stop()" title="Stop"><i class="fa-solid fa-stop"></i></button>
            <button class="btn btn-secondary btn-sm" id="btn-loop" onclick="forensic.toggleLoop()" title="Loop Selection"><i class="fa-solid fa-repeat"></i></button>
            <button class="btn btn-secondary btn-sm" onclick="forensic.playReverse()" title="Reverse"><i class="fa-solid fa-backward"></i></button>
            <select class="speed-select" id="ctl-speed" onchange="forensic.setSpeed(+this.value)">
                <option value="0.25">0.25x</option>
                <option value="0.5">0.5x</option>
                <option value="0.75">0.75x</option>
                <option value="1" selected>1.0x</option>
                <option value="1.5">1.5x</option>
                <option value="2">2.0x</option>
                <option value="4">4.0x</option>
            </select>
        </div>
        <div class="filter-row">
            <span style="font-size:11px;color:var(--muted)">Filter:</span>
            <select class="speed-select" id="ctl-filter" onchange="forensic.setFilter(this.value)">
                <option value="none">None</option>
                <option value="bandpass">Band-pass (Selection)</option>
                <option value="lowpass">Low-pass</option>
                <option value="highpass">High-pass</option>
                <option value="notch">Notch</option>
            </select>
            <label class="enhance-check">
                <input type="checkbox" id="ctl-preserve-pitch" onchange="forensic.setPreservePitch(this.checked)">
                Preserve Pitch
            </label>
        </div>
        <!-- Goniometer (stereo phase correlation) -->
        <div class="goniometer-panel" id="goniometer-panel">
            <div class="panel-label">Phase Correlation</div>
            <canvas id="goniometer-canvas" width="200" height="200"></canvas>
        </div>
    </div>
</div>

<!-- AI Analysis Panel -->
<div class="ai-panel">
    <div class="card-title"><i class="fa-solid fa-robot fa-fw" style="color:var(--teal)"></i> AI Analysis</div>
    <div class="ai-btn-row">
        <button class="btn btn-secondary btn-sm" onclick="forensic.aiAnalyze()" id="btn-ai-analyze">
            <i class="fa-solid fa-crosshairs"></i> Analyze Selection
        </button>
        <button class="btn btn-secondary btn-sm" onclick="forensic.aiDescribe()">
            <i class="fa-solid fa-comment-dots"></i> Describe Audio
        </button>
    </div>
    <div id="ai-result" style="display:none;margin-top:8px;padding:8px;background:rgba(20,184,166,.06);border:1px solid rgba(20,184,166,.15);border-radius:var(--radius-xs);font-size:12px;color:var(--text-dim);max-height:160px;overflow-y:auto;position:relative"></div>
</div>

<!-- Event Detection Panel -->
<div class="event-panel">
    <div class="card-title"><i class="fa-solid fa-magnifying-glass-chart fa-fw" style="color:var(--teal)"></i> Detected Events</div>
    <div class="event-filter-row">
        <button class="btn btn-secondary btn-xs" onclick="forensic.filterEvents('all')">All</button>
        <button class="btn btn-secondary btn-xs" onclick="forensic.filterEvents('silence')">&#x1f507; Silence</button>
        <button class="btn btn-secondary btn-xs" onclick="forensic.filterEvents('transient')">&#x26a1; Transient</button>
        <button class="btn btn-secondary btn-xs" onclick="forensic.filterEvents('tonal')">&#x1f3b5; Tonal</button>
        <button class="btn btn-secondary btn-xs" onclick="forensic.filterEvents('click')">&#x1f4a5; Click/Pop</button>
    </div>
    <div class="event-list" id="event-list">
        <div class="empty" style="padding:12px;font-size:12px;color:var(--muted)">
            <i class="fa-solid fa-magnifying-glass fa-fw"></i> Click "Detect Events" to scan audio
        </div>
    </div>
</div>

<!-- Enhancement Tools -->
<div class="enhance-panel">
    <div class="card-title"><i class="fa-solid fa-wand-magic-sparkles fa-fw" style="color:var(--teal)"></i> Enhancement Tools</div>

    <!-- Noise Reduction -->
    <div class="enhance-row">
        <span class="enhance-label">Noise Reduction</span>
        <button class="btn btn-secondary btn-xs" onclick="forensic.captureNoisePrint()" title="Select a silence region first, then capture its noise profile">
            <i class="fa-solid fa-fingerprint"></i> Capture Print
        </button>
        <button class="btn btn-secondary btn-xs" onclick="forensic.applyNoiseReduction()" title="Subtract captured noise profile from the entire signal">
            <i class="fa-solid fa-broom"></i> Apply
        </button>
        <span style="font-size:10px;color:var(--muted);margin-left:4px">Strength:</span>
        <input type="range" class="enhance-slider" id="ctl-noise-strength" min="0" max="2" value="1" step="0.1"
            oninput="forensic.setNoiseStrength(+this.value); document.getElementById('noise-str-val').textContent=this.value+'x'">
        <span class="enhance-val" id="noise-str-val">1.0x</span>
    </div>

    <!-- Band Isolation -->
    <div class="enhance-row">
        <span class="enhance-label">Band Isolation</span>
        <button class="btn btn-secondary btn-xs" onclick="forensic.isolateBand()" title="Select a frequency range on the spectrogram, then isolate it">
            <i class="fa-solid fa-scissors"></i> Isolate Selected Range
        </button>
    </div>

    <!-- Amplitude Envelope -->
    <div class="enhance-row">
        <span class="enhance-label">Envelope</span>
        <button class="btn btn-secondary btn-xs" onclick="forensic.toggleEnvelope()" title="Show/hide RMS amplitude envelope on waveform">
            <i class="fa-solid fa-wave-square"></i> Show Envelope
        </button>
        <span style="font-size:10px;color:var(--muted);margin-left:4px">Window:</span>
        <input type="range" class="enhance-slider" id="ctl-env-window" min="10" max="500" value="50" step="10"
            oninput="forensic.setEnvelopeWindow(+this.value); document.getElementById('env-win-val').textContent=this.value+' ms'">
        <span class="enhance-val" id="env-win-val">50 ms</span>
    </div>

    <!-- Peak Detection -->
    <div class="enhance-row">
        <span class="enhance-label">Peak Detection</span>
        <button class="btn btn-secondary btn-xs" onclick="forensic.findPeaks()" title="Find strongest frequency peaks in selected region">
            <i class="fa-solid fa-mountain-sun"></i> Find Peaks
        </button>
        <button class="btn btn-secondary btn-xs" onclick="forensic.clearPeaks()" title="Clear peak markers">
            <i class="fa-solid fa-eraser"></i> Clear
        </button>
        <span style="font-size:10px;color:var(--muted);margin-left:4px">Threshold:</span>
        <input type="range" class="enhance-slider" id="ctl-peak-thresh" min="-96" max="0" value="-40" step="1"
            oninput="forensic.setPeakThreshold(+this.value); document.getElementById('peak-thresh-val').textContent=this.value+' dB'">
        <span class="enhance-val" id="peak-thresh-val">-40 dB</span>
        <span style="font-size:10px;color:var(--muted);margin-left:4px">Min Dist:</span>
        <input type="range" class="enhance-slider" id="ctl-peak-dist" min="10" max="2000" value="100" step="10"
            oninput="forensic.setPeakMinDistance(+this.value); document.getElementById('peak-dist-val').textContent=this.value+' Hz'">
        <span class="enhance-val" id="peak-dist-val">100 Hz</span>
    </div>

    <!-- Restore Original -->
    <div class="enhance-row">
        <span class="enhance-label">Original Audio</span>
        <button class="btn btn-secondary btn-xs" onclick="forensic.restoreOriginal()" title="Undo all processing and restore original audio">
            <i class="fa-solid fa-rotate-left"></i> Restore Original
        </button>
    </div>
</div>

<!-- Compare Mode Panels -->
<div class="compare-container" id="compare-container">
    <div class="compare-panel">
        <div class="panel-title"><i class="fa-solid fa-file-audio fa-fw" style="color:var(--teal)"></i> File A (Primary)</div>
        <div class="panel-info" id="compare-file-a-info">Load a file above</div>
        <canvas id="compare-canvas-a"></canvas>
    </div>
    <div class="compare-panel">
        <div class="panel-title"><i class="fa-solid fa-file-audio fa-fw" style="color:var(--cyan)"></i> File B</div>
        <div class="panel-info" id="compare-file-info">No file loaded</div>
        <input type="file" id="compare-file-input" accept="audio/*" style="display:none">
        <button class="btn btn-secondary btn-xs" onclick="document.getElementById('compare-file-input').click()" style="margin-bottom:6px">
            <i class="fa-solid fa-folder-open"></i> Load File B
        </button>
        <canvas id="compare-canvas-b"></canvas>
    </div>
</div>
<div class="compare-diff-panel" id="compare-diff-panel">
    <div class="compare-panel" style="width:100%">
        <div class="panel-title">
            <i class="fa-solid fa-not-equal fa-fw" style="color:var(--orange)"></i> Difference (|A - B|)
            <button class="btn btn-secondary btn-xs" style="margin-left:auto" onclick="forensic.toggleDiffView()">
                <i class="fa-solid fa-eye-slash"></i> Toggle
            </button>
        </div>
        <canvas id="compare-canvas-diff"></canvas>
    </div>
</div>

<!-- Report config modal -->
<div class="report-modal" id="report-modal">
    <div class="report-modal-box">
        <h3><i class="fa-solid fa-file-lines" style="color:var(--teal)"></i> Generate Forensic Report</h3>
        <div class="form-group">
            <label class="form-label">Analyst Name</label>
            <input type="text" class="form-input" id="report-analyst" placeholder="Your name">
        </div>
        <div class="form-group">
            <label class="form-label">Case Number</label>
            <input type="text" class="form-input" id="report-case" placeholder="Case #">
        </div>
        <div class="rpt-checks">
            <label><input type="checkbox" id="rpt-inc-meta" checked> File Metadata &amp; Analysis Settings</label>
            <label><input type="checkbox" id="rpt-inc-spec" checked> Spectrogram Screenshot</label>
            <label><input type="checkbox" id="rpt-inc-wave" checked> Waveform Overview</label>
            <label><input type="checkbox" id="rpt-inc-anno" checked> Annotations</label>
            <label><input type="checkbox" id="rpt-inc-events" checked> Detected Events</label>
            <label><input type="checkbox" id="rpt-inc-enhance" checked> Enhancement History</label>
        </div>
        <div style="display:flex;gap:8px;justify-content:flex-end;margin-top:12px">
            <button class="btn btn-secondary btn-sm" onclick="forensic.closeReportModal()">Cancel</button>
            <button class="btn btn-primary btn-sm" onclick="forensic.confirmReport()"><i class="fa-solid fa-file-export"></i> Generate</button>
        </div>
    </div>
</div>

<!-- Annotation input modal -->
<div class="anno-modal" id="anno-modal">
    <div class="anno-modal-box">
        <h3><i class="fa-solid fa-map-pin" style="color:var(--teal)"></i> Add Annotation</h3>
        <div class="form-group">
            <label class="form-label">Time</label>
            <input type="text" class="form-input" id="anno-time" readonly>
        </div>
        <div class="form-group">
            <label class="form-label">Frequency</label>
            <input type="text" class="form-input" id="anno-freq" readonly>
        </div>
        <div class="form-group">
            <label class="form-label">Note</label>
            <textarea class="form-textarea" id="anno-note" rows="3" placeholder="Describe the anomaly..."></textarea>
        </div>
        <div class="form-group">
            <label class="form-label">Color</label>
            <select class="form-select" id="anno-color">
                <option value="#14b8a6">Teal</option>
                <option value="#ef4444">Red</option>
                <option value="#eab308">Yellow</option>
                <option value="#22c55e">Green</option>
                <option value="#a855f7">Purple</option>
                <option value="#f97316">Orange</option>
            </select>
        </div>
        <div style="display:flex;gap:8px;justify-content:flex-end;margin-top:12px">
            <button class="btn btn-secondary btn-sm" onclick="forensic.closeAnnotationModal()">Cancel</button>
            <button class="btn btn-primary btn-sm" onclick="forensic.confirmAnnotation()">Add Marker</button>
        </div>
    </div>
</div>

<script src="/js/webgl-viz.js"></script>
<script src="/js/webgl-spectrogram-hq.js"></script>
<script src="/js/forensic-analyzer.js"></script>

<script>
var forensic;
document.addEventListener('DOMContentLoaded', function() {
    forensic = new ForensicAnalyzer({
        spectrogramCanvasId: 'spectrogram-canvas',
        overlayCanvasId: 'spectrogram-overlay',
        waveformCanvasId: 'waveform-canvas',
        minimapCanvasId: 'minimap-canvas'
    });

    document.getElementById('file-input').addEventListener('change', function(e) {
        if (e.target.files && e.target.files[0]) {
            forensic.loadFile(e.target.files[0]);
        }
    });

    /* Compare file input */
    document.getElementById('compare-file-input').addEventListener('change', function(e) {
        if (e.target.files && e.target.files[0]) {
            forensic.loadCompareFile(e.target.files[0]);
        }
    });

    /* Keyboard shortcuts */
    document.addEventListener('keydown', function(e) {
        if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA' || e.target.tagName === 'SELECT') return;
        if (e.code === 'Space') { e.preventDefault(); forensic.togglePlay(); }
        if (e.code === 'KeyL') { forensic.toggleLoop(); }
        if (e.code === 'KeyR') { forensic.playReverse(); }
        if (e.code === 'Escape') { forensic.closeAnnotationModal(); }
        if (e.code === 'Delete' || e.code === 'Backspace') { forensic.clearSelection(); }
        if (e.code === 'KeyN') { forensic.captureNoisePrint(); }
        if (e.code === 'KeyP') { forensic.findPeaks(); }
        if (e.code === 'KeyE') { forensic.toggleEnvelope(); }
        if (e.code === 'KeyO') { forensic.restoreOriginal(); }
        if (e.code === 'KeyD') { forensic.detectEvents(); }
        if (e.code === 'KeyG') { forensic.exportReport(); }
    });
});
</script>

<?php require_once __DIR__ . '/app/inc/media_picker.php'; ?>

<script>
/* We add a loadFromLibrary method to the forensic analyzer so the media
 * picker can feed it a track from the DB without a local file upload. */
document.addEventListener('DOMContentLoaded', function() {
    if (typeof forensic !== 'undefined' && forensic) {
        forensic.loadFromLibrary = function(trackId, title) {
            mc1Toast('Loading "' + (title || 'track') + '" from library...', 'info');
            fetch('/app/api/audio.php?id=' + trackId).then(function(r) {
                if (!r.ok) throw new Error('HTTP ' + r.status);
                return r.arrayBuffer();
            }).then(function(buf) {
                var blob = new Blob([buf]);
                var file = new File([blob], (title || 'track') + '.wav', {type: 'audio/wav'});
                forensic.loadFile(file);
            }).catch(function(e) {
                mc1Toast('Failed to load from library: ' + e.message, 'err');
            });
        };
    }
});
</script>

<?php require_once __DIR__ . '/app/inc/footer.php'; ?>
