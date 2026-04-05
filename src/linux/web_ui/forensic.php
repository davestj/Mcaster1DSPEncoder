<?php
/**
 * forensic.php — Forensic Audio Spectral Analysis
 *
 * File:    src/linux/web_ui/forensic.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Phase:   FA-1
 * Purpose: We provide a deep spectral analysis page targeting paranormal investigators,
 *          forensic analysts, and audio researchers. We use WebGL 2.0 for high-resolution
 *          spectrogram rendering with configurable FFT sizes up to 65536, multiple window
 *          functions, color maps, frequency scales, region selection, filtered playback,
 *          annotations, and optional Ollama AI analysis.
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

/* Compare mode */
.compare-container{display:none;grid-template-columns:1fr 1fr;gap:14px}
.compare-container.active{display:grid}

/* Loading overlay */
.forensic-loading{position:absolute;top:0;left:0;right:0;bottom:0;background:rgba(8,12,24,.85);display:none;align-items:center;justify-content:center;z-index:10;flex-direction:column;gap:10px}
.forensic-loading.show{display:flex}
.forensic-loading .spinner{width:32px;height:32px;border-width:3px}
.forensic-loading span{font-size:13px;color:var(--text-dim)}

/* Minimap */
.minimap{position:absolute;bottom:26px;right:6px;width:120px;height:40px;border:1px solid rgba(255,255,255,.15);background:rgba(0,0,0,.5);z-index:5;border-radius:3px;overflow:hidden}
.minimap canvas{width:100%;height:100%}
.minimap-viewport{position:absolute;top:0;bottom:0;border:1px solid var(--teal);background:rgba(20,184,166,.1);pointer-events:none}

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
        <div style="display:flex;gap:6px;margin-top:8px">
            <button class="btn btn-secondary btn-xs" onclick="forensic.exportAnnotations()"><i class="fa-solid fa-download"></i> Export JSON</button>
            <button class="btn btn-secondary btn-xs" onclick="forensic.exportReport()"><i class="fa-solid fa-file-lines"></i> Export Report</button>
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
        </div>
        <div class="ai-row">
            <button class="btn btn-secondary btn-sm" onclick="forensic.aiAnalyze()"><i class="fa-solid fa-robot"></i> AI Analyze Selection</button>
            <button class="btn btn-secondary btn-sm" onclick="forensic.aiDescribe()"><i class="fa-solid fa-comment-dots"></i> Describe Audio</button>
        </div>
        <div id="ai-result" style="display:none;margin-top:8px;padding:8px;background:rgba(20,184,166,.06);border:1px solid rgba(20,184,166,.15);border-radius:var(--radius-xs);font-size:12px;color:var(--text-dim);max-height:120px;overflow-y:auto"></div>
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

    /* Keyboard shortcuts */
    document.addEventListener('keydown', function(e) {
        if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA' || e.target.tagName === 'SELECT') return;
        if (e.code === 'Space') { e.preventDefault(); forensic.togglePlay(); }
        if (e.code === 'KeyL') { forensic.toggleLoop(); }
        if (e.code === 'KeyR') { forensic.playReverse(); }
        if (e.code === 'Escape') { forensic.closeAnnotationModal(); }
        if (e.code === 'Delete' || e.code === 'Backspace') { forensic.clearSelection(); }
    });
});
</script>

<?php require_once __DIR__ . '/app/inc/footer.php'; ?>
