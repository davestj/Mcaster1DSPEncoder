<?php
define('MC1_BOOT', true);
require_once __DIR__ . '/app/inc/auth.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/user_auth.php';

if (!mc1_is_authed()) {
    http_response_code(302);
    header('Location: /login');
    return;
}

$page_title = 'VoicTune';
$active_nav = 'voictune';
$use_charts = false;

require_once __DIR__ . '/app/inc/header.php';
?>

<style>
/* ── VoicTune page styles ─────────────────────────────────────────── */
.vt-top-row {
    display: grid;
    grid-template-columns: 100px 100px 160px 1fr;
    gap: 14px;
    align-items: stretch;
}
.vt-meter-box {
    background: var(--card);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 12px 10px;
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 6px;
}
.vt-meter-box .vt-label {
    font-size: 10px;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: .06em;
    color: var(--muted);
}
.vt-meter-canvas {
    display: block;
}
.vt-pitch-box {
    background: var(--card);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 14px 18px;
    display: flex;
    align-items: center;
    gap: 18px;
    min-height: 120px;
}
.vt-pitch-note {
    font-size: 48px;
    font-weight: 800;
    color: var(--teal);
    line-height: 1;
    min-width: 100px;
    text-align: center;
    font-family: 'SF Mono','Fira Code',monospace;
}
.vt-pitch-detail {
    flex: 1;
    display: flex;
    flex-direction: column;
    gap: 6px;
}
.vt-pitch-hz {
    font-size: 13px;
    color: var(--text-dim);
    font-variant-numeric: tabular-nums;
}
.vt-cents-bar {
    height: 8px;
    background: rgba(255,255,255,.06);
    border-radius: 4px;
    position: relative;
    overflow: visible;
}
.vt-cents-center {
    position: absolute;
    left: 50%;
    top: -2px;
    bottom: -2px;
    width: 2px;
    background: var(--muted);
    transform: translateX(-50%);
}
.vt-cents-indicator {
    position: absolute;
    top: -3px;
    width: 14px;
    height: 14px;
    border-radius: 50%;
    background: var(--green);
    transform: translateX(-50%);
    transition: left 0.08s ease, background 0.2s;
    box-shadow: 0 0 6px rgba(34,197,94,.5);
}
.vt-cents-labels {
    display: flex;
    justify-content: space-between;
    font-size: 9px;
    color: var(--muted);
}
.vt-confidence {
    font-size: 11px;
    color: var(--text-dim);
}
.vt-confidence-bar {
    height: 3px;
    background: rgba(255,255,255,.06);
    border-radius: 2px;
    overflow: hidden;
    margin-top: 2px;
}
.vt-confidence-fill {
    height: 100%;
    background: var(--teal);
    transition: width 0.15s;
}

/* ── Viz row ─────────────────────────────────────────────────────── */
.vt-viz-row {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 14px;
}
.vt-viz-card {
    background: var(--card);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 14px;
}
.vt-viz-card .vt-label {
    font-size: 11px;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: .06em;
    color: var(--muted);
    margin-bottom: 8px;
}
.vt-viz-canvas {
    width: 100%;
    height: 200px;
    display: block;
    border-radius: var(--radius-sm);
    background: #0a0f1e;
}

/* ── Coaching panel ─────────────────────────────────────────────── */
.vt-coaching {
    background: var(--card);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 14px 16px;
}
.vt-coaching .vt-label {
    font-size: 11px;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: .06em;
    color: var(--muted);
    margin-bottom: 10px;
}
.vt-tips-list {
    display: flex;
    flex-direction: column;
    gap: 8px;
    max-height: 180px;
    overflow-y: auto;
}
.vt-tip {
    display: flex;
    align-items: flex-start;
    gap: 10px;
    padding: 9px 12px;
    background: rgba(255,255,255,.03);
    border: 1px solid rgba(51,65,85,.45);
    border-radius: var(--radius-sm);
    font-size: 13px;
}
.vt-tip-icon {
    font-size: 14px;
    flex-shrink: 0;
    margin-top: 1px;
}
.vt-tip-icon.info { color: var(--cyan); }
.vt-tip-icon.suggestion { color: var(--teal); }
.vt-tip-icon.warning { color: var(--yellow); }
.vt-tip-icon.critical { color: var(--red); }
.vt-tip-body { flex: 1; }
.vt-tip-msg { color: var(--text); font-weight: 500; }
.vt-tip-sug { color: var(--text-dim); font-size: 12px; margin-top: 2px; }

/* ── Controls bar ────────────────────────────────────────────────── */
.vt-controls {
    background: var(--card);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 12px 16px;
    display: flex;
    align-items: center;
    gap: 12px;
    flex-wrap: wrap;
}
.vt-controls .form-select {
    width: auto;
    min-width: 180px;
}
.vt-session-timer {
    font-family: 'SF Mono','Fira Code',monospace;
    font-size: 14px;
    color: var(--teal);
    min-width: 60px;
}
.vt-status-dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    display: inline-block;
    background: var(--muted);
    transition: background 0.3s;
}
.vt-status-dot.active {
    background: var(--green);
    box-shadow: 0 0 6px var(--green);
    animation: live-dot 1.4s ease-in-out infinite;
}
.vt-status-dot.error {
    background: var(--red);
}
.vt-source-toggle {
    display: flex;
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    overflow: hidden;
}
.vt-source-btn {
    padding: 5px 12px;
    font-size: 12px;
    background: transparent;
    color: var(--text-dim);
    border: none;
    cursor: pointer;
    transition: all 0.15s;
    white-space: nowrap;
}
.vt-source-btn.active {
    background: rgba(20,184,166,.15);
    color: var(--teal);
    font-weight: 600;
}
.vt-source-btn:hover:not(.active) {
    background: rgba(255,255,255,.04);
    color: var(--text);
}

/* ── Responsive ──────────────────────────────────────────────────── */
@media (max-width: 860px) {
    .vt-top-row { grid-template-columns: 1fr 1fr; }
    .vt-viz-row { grid-template-columns: 1fr; }
}
@media (max-width: 580px) {
    .vt-top-row { grid-template-columns: 1fr; }
    .vt-controls { flex-direction: column; align-items: stretch; }
}
</style>

<!-- ── Page header ──────────────────────────────────────────────────── -->
<div class="sec-hdr">
    <div class="sec-title">
        <i class="fa-solid fa-microphone-lines" style="color:var(--teal);margin-right:8px"></i>
        VoicTune — Voice Analysis &amp; Coaching
    </div>
    <div style="display:flex;gap:8px;align-items:center">
        <span class="vt-status-dot" id="vt-status-dot"></span>
        <span id="vt-status-text" class="badge badge-gray">Offline</span>
        <span id="vt-ws-badge" class="badge badge-gray" style="display:none">WS: 0</span>
    </div>
</div>

<!-- ── Top row: meters + pitch ─────────────────────────────────────── -->
<div class="vt-top-row">
    <div class="vt-meter-box">
        <div class="vt-label">RMS</div>
        <canvas id="vt-rms-canvas" class="vt-meter-canvas" width="40" height="140"></canvas>
        <div id="vt-rms-val" style="font-size:11px;color:var(--text-dim);font-variant-numeric:tabular-nums">-96 dB</div>
    </div>
    <div class="vt-meter-box">
        <div class="vt-label">Peak</div>
        <canvas id="vt-peak-canvas" class="vt-meter-canvas" width="40" height="140"></canvas>
        <div id="vt-peak-val" style="font-size:11px;color:var(--text-dim);font-variant-numeric:tabular-nums">-96 dB</div>
    </div>
    <div class="vt-meter-box">
        <div class="vt-label">LUFS</div>
        <canvas id="vt-lufs-canvas" class="vt-meter-canvas" width="140" height="100"></canvas>
        <div id="vt-lufs-val" style="font-size:13px;font-weight:700;color:var(--text);font-variant-numeric:tabular-nums">-96.0</div>
    </div>
    <div class="vt-pitch-box">
        <div class="vt-pitch-note" id="vt-pitch-note">--</div>
        <div class="vt-pitch-detail">
            <div class="vt-pitch-hz" id="vt-pitch-hz">-- Hz</div>
            <div class="vt-cents-bar">
                <div class="vt-cents-center"></div>
                <div class="vt-cents-indicator" id="vt-cents-ind" style="left:50%"></div>
            </div>
            <div class="vt-cents-labels"><span>-50c</span><span>0</span><span>+50c</span></div>
            <div class="vt-confidence">
                Confidence: <span id="vt-pitch-conf">0%</span>
                <div class="vt-confidence-bar"><div class="vt-confidence-fill" id="vt-pitch-conf-bar" style="width:0"></div></div>
            </div>
        </div>
    </div>
</div>

<!-- ── Visualization row: scope + spectrum ──────────────────────────── -->
<div class="vt-viz-row">
    <div class="vt-viz-card">
        <div class="vt-label"><i class="fa-solid fa-wave-square" style="margin-right:4px"></i> Oscilloscope</div>
        <canvas id="vt-scope-canvas" class="vt-viz-canvas"></canvas>
    </div>
    <div class="vt-viz-card">
        <div class="vt-label"><i class="fa-solid fa-chart-bar" style="margin-right:4px"></i> Spectrum Analyzer</div>
        <canvas id="vt-spectrum-canvas" class="vt-viz-canvas"></canvas>
    </div>
</div>

<!-- ── Coaching tips ───────────────────────────────────────────────── -->
<div class="vt-coaching">
    <div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:10px">
        <div class="vt-label" style="margin-bottom:0"><i class="fa-solid fa-lightbulb" style="margin-right:4px;color:var(--yellow)"></i> Coaching Tips</div>
        <button class="btn btn-secondary btn-xs" id="vt-ai-coach-btn" title="Ask AI coach for advice">
            <i class="fa-solid fa-wand-magic-sparkles"></i> AI Coach
        </button>
    </div>
    <div class="vt-tips-list" id="vt-tips-list">
        <div class="vt-tip">
            <div class="vt-tip-icon info"><i class="fa-solid fa-circle-info"></i></div>
            <div class="vt-tip-body">
                <div class="vt-tip-msg">Start a session to receive coaching tips</div>
                <div class="vt-tip-sug">Select a microphone and click Start Session below.</div>
            </div>
        </div>
    </div>
    <div style="display:flex;align-items:center;gap:10px;margin-top:12px;padding-top:10px;border-top:1px solid rgba(51,65,85,.3)">
        <i class="fa-solid fa-robot" style="color:var(--teal);font-size:.9rem;flex-shrink:0"></i>
        <input type="text" id="vt-ai-input"
               placeholder="Ask AI... e.g., 'Why does my voice sound muddy?'"
               style="flex:1;padding:7px 12px;background:rgba(255,255,255,.05);border:1px solid var(--border);border-radius:var(--radius-sm,4px);color:var(--text);font-size:.8rem;outline:none"
               autocomplete="off">
        <button class="btn btn-sm btn-primary" id="vt-ai-send" style="flex-shrink:0">
            <i class="fa-solid fa-paper-plane"></i>
        </button>
    </div>
</div>

<!-- ── Content Analysis ────────────────────────────────────────────── -->
<div class="vt-coaching" style="margin-top:14px">
    <div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:10px">
        <div class="vt-label" style="margin-bottom:0"><i class="fa-solid fa-file-lines" style="margin-right:4px;color:#a78bfa"></i> Content Analysis</div>
        <button class="btn btn-sm" id="vt-content-analyze-btn" style="background:rgba(139,92,246,.2);color:#a78bfa;border:1px solid rgba(139,92,246,.35)">
            <i class="fa-solid fa-wand-magic-sparkles"></i> Analyze Session
        </button>
    </div>
    <div id="vt-content-input" style="margin-bottom:10px">
        <textarea id="vt-content-transcript" class="form-textarea" rows="3"
                  placeholder="Paste a transcript here, or click Analyze Session to analyze the current session..."
                  style="font-size:12px;background:rgba(255,255,255,.04);border:1px solid var(--border);color:var(--text);width:100%;resize:vertical"></textarea>
    </div>
    <div id="vt-content-results" style="display:none">
        <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:12px">
            <div>
                <div style="font-size:10px;font-weight:700;text-transform:uppercase;color:var(--muted);margin-bottom:6px">Summary</div>
                <div id="vt-ca-summary" style="font-size:13px;color:var(--text);line-height:1.5;padding:8px 10px;background:rgba(255,255,255,.03);border-radius:4px;min-height:40px"></div>
            </div>
            <div>
                <div style="font-size:10px;font-weight:700;text-transform:uppercase;color:var(--muted);margin-bottom:6px">Title Suggestion</div>
                <div id="vt-ca-title" style="font-size:14px;font-weight:600;color:var(--teal);padding:8px 10px;background:rgba(255,255,255,.03);border-radius:4px;min-height:40px;display:flex;align-items:center"></div>
            </div>
        </div>
        <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:12px">
            <div>
                <div style="font-size:10px;font-weight:700;text-transform:uppercase;color:var(--muted);margin-bottom:6px">Topics</div>
                <div id="vt-ca-topics" style="display:flex;flex-wrap:wrap;gap:4px"></div>
            </div>
            <div>
                <div style="font-size:10px;font-weight:700;text-transform:uppercase;color:var(--muted);margin-bottom:6px">Tags</div>
                <div id="vt-ca-tags" style="display:flex;flex-wrap:wrap;gap:4px"></div>
            </div>
        </div>
        <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px">
            <div>
                <div style="font-size:10px;font-weight:700;text-transform:uppercase;color:var(--muted);margin-bottom:6px">Pace Analysis</div>
                <div id="vt-ca-pace" style="font-size:12px;color:var(--text-dim);padding:8px 10px;background:rgba(255,255,255,.03);border-radius:4px;min-height:40px"></div>
            </div>
            <div>
                <div style="font-size:10px;font-weight:700;text-transform:uppercase;color:var(--muted);margin-bottom:6px">Filler Words</div>
                <div id="vt-ca-filler" style="font-size:12px;color:var(--text-dim);padding:8px 10px;background:rgba(255,255,255,.03);border-radius:4px;min-height:40px"></div>
            </div>
        </div>
        <div id="vt-ca-latency" style="font-size:10px;color:var(--muted);margin-top:8px;text-align:right"></div>
    </div>
</div>

<!-- ── Controls bar ────────────────────────────────────────────────── -->
<div class="vt-controls">
    <select class="form-select" id="vt-device-select" title="Input device">
        <option value="-1">Loading devices...</option>
    </select>

    <button class="btn btn-success btn-sm" id="vt-start-btn" onclick="vtStartSession()">
        <i class="fa-solid fa-play"></i> Start Session
    </button>
    <button class="btn btn-danger btn-sm" id="vt-stop-btn" onclick="vtStopSession()" disabled>
        <i class="fa-solid fa-stop"></i> Stop
    </button>

    <div class="vt-session-timer" id="vt-session-timer">0:00</div>

    <div style="flex:1"></div>

    <div class="vt-source-toggle">
        <button class="vt-source-btn active" id="vt-src-server" onclick="vtSetSource('server')">
            <i class="fa-solid fa-server"></i> Server Mic
        </button>
        <button class="vt-source-btn" id="vt-src-browser" onclick="vtSetSource('browser')">
            <i class="fa-solid fa-globe"></i> Browser Mic
        </button>
    </div>
</div>

<script src="/js/voictune-viz.js"></script>
<script src="/js/voictune-audio.js"></script>

<script>
(function(){
'use strict';

/* ── VoicTune API base (cross-origin to daemon on port 8350) ─── */
var VT_BASE = window.location.protocol + '//' + window.location.hostname + ':8350';
var POLL_MS = 80;          /* ~12 Hz data polling */
var TIPS_POLL_MS = 3000;   /* coaching tips every 3s */

var sessionActive = false;
var sessionStart  = 0;
var sessionTimer  = null;
var pollTimer     = null;
var tipsPollTimer = null;
var audioSource   = 'server';  /* 'server' or 'browser' */

/* ── VoicTune fetch helper (cross-origin with credentials) ──── */
function vtApi(method, path, body) {
    var opts = {
        method: method,
        headers: {'Content-Type': 'application/json'},
        credentials: 'include'
    };
    if (body !== undefined && body !== null) opts.body = JSON.stringify(body);
    return fetch(VT_BASE + path, opts).then(function(r) {
        return r.json().then(function(d) { d._status = r.status; return d; });
    });
}

/* ── Cached DOM elements ─────────────────────────────────────── */
var elRmsCanvas, elPeakCanvas, elLufsCanvas, elScopeCanvas, elSpectrumCanvas;
var elRmsVal, elPeakVal, elLufsVal;
var elPitchNote, elPitchHz, elCentsInd, elPitchConf, elPitchConfBar;
var elTipsList, elDeviceSelect;
var elStartBtn, elStopBtn, elSessionTimer;
var elStatusDot, elStatusText, elWsBadge;

/* ── Viz engine instance ─────────────────────────────────────── */
var viz = null;

/* ── Latest data from polls (for rAF interpolation) ────────── */
var latestMeters   = null;
var latestWaveform = null;
var latestSpectrum = null;

/* ── Init ────────────────────────────────────────────────────── */
document.addEventListener('DOMContentLoaded', function() {
    elRmsCanvas      = document.getElementById('vt-rms-canvas');
    elPeakCanvas     = document.getElementById('vt-peak-canvas');
    elLufsCanvas     = document.getElementById('vt-lufs-canvas');
    elScopeCanvas    = document.getElementById('vt-scope-canvas');
    elSpectrumCanvas = document.getElementById('vt-spectrum-canvas');
    elRmsVal         = document.getElementById('vt-rms-val');
    elPeakVal        = document.getElementById('vt-peak-val');
    elLufsVal        = document.getElementById('vt-lufs-val');
    elPitchNote      = document.getElementById('vt-pitch-note');
    elPitchHz        = document.getElementById('vt-pitch-hz');
    elCentsInd       = document.getElementById('vt-cents-ind');
    elPitchConf      = document.getElementById('vt-pitch-conf');
    elPitchConfBar   = document.getElementById('vt-pitch-conf-bar');
    elTipsList       = document.getElementById('vt-tips-list');
    elDeviceSelect   = document.getElementById('vt-device-select');
    elStartBtn       = document.getElementById('vt-start-btn');
    elStopBtn        = document.getElementById('vt-stop-btn');
    elSessionTimer   = document.getElementById('vt-session-timer');
    elStatusDot      = document.getElementById('vt-status-dot');
    elStatusText     = document.getElementById('vt-status-text');
    elWsBadge        = document.getElementById('vt-ws-badge');

    /* Size canvases to their CSS box */
    sizeCanvas(elScopeCanvas);
    sizeCanvas(elSpectrumCanvas);

    viz = new VoicTuneViz();

    loadDevices();
    checkVtStatus();
    checkActiveSession();
    setInterval(checkVtStatus, 10000);

    requestAnimationFrame(renderLoop);
});

function sizeCanvas(c) {
    if (!c) return;
    var r = c.getBoundingClientRect();
    c.width  = Math.round(r.width * (window.devicePixelRatio || 1));
    c.height = Math.round(r.height * (window.devicePixelRatio || 1));
}

/* ── Resize handler ──────────────────────────────────────────── */
var resizeTimeout;
window.addEventListener('resize', function() {
    clearTimeout(resizeTimeout);
    resizeTimeout = setTimeout(function() {
        sizeCanvas(elScopeCanvas);
        sizeCanvas(elSpectrumCanvas);
    }, 200);
});

/* ── VoicTune daemon health check ────────────────────────────── */
function checkVtStatus() {
    vtApi('GET', '/api/v1/voictune/health').then(function(d) {
        if (d && d.ok) {
            elStatusDot.className = 'vt-status-dot active';
            elStatusText.textContent = 'VoicTune v' + (d.version || '?');
            elStatusText.className = 'badge badge-green';

            /* Auto-detect active session and start polling if not already running */
            if (!sessionActive && d.session_active) {
                sessionActive = true;
                sessionStart = Date.now() - ((d.session_elapsed_sec || 0) * 1000);
                elStartBtn.disabled = true;
                elStopBtn.disabled = false;
                startPolling();
            }
        } else {
            setOffline();
        }
    }).catch(function() {
        setOffline();
    });
}

function setOffline() {
    elStatusDot.className = 'vt-status-dot error';
    elStatusText.textContent = 'VoicTune Offline';
    elStatusText.className = 'badge badge-red';
}

/* ── Check for active session on page load ───────────────────── */
function checkActiveSession() {
    vtApi('GET', '/api/v1/voictune/session/status').then(function(d) {
        if (d && d.ok && d.active && !sessionActive) {
            sessionActive = true;
            sessionStart = Date.now() - ((d.elapsed_sec || 0) * 1000);
            elStartBtn.disabled = true;
            elStopBtn.disabled = false;
            startPolling();
        }
    }).catch(function(){});
}

/* ── Load devices from VoicTune daemon ───────────────────────── */
function loadDevices() {
    vtApi('GET', '/api/v1/voictune/devices').then(function(d) {
        if (!d || !d.ok) return;
        elDeviceSelect.innerHTML = '';
        var inputs = d.inputs || [];
        if (inputs.length === 0) {
            elDeviceSelect.innerHTML = '<option value="-1">No input devices</option>';
            return;
        }
        inputs.forEach(function(dev) {
            var opt = document.createElement('option');
            opt.value = dev.index;
            var label = dev.name;
            if (dev.is_usb) label = '[USB] ' + label;
            if (dev.is_bluetooth) label = '[BT] ' + label;
            if (dev.is_default) label += ' (default)';
            opt.textContent = label;
            if (dev.index === d.active_device) opt.selected = true;
            elDeviceSelect.appendChild(opt);
        });
        if (d.ws_clients > 0) {
            elWsBadge.style.display = '';
            elWsBadge.textContent = 'WS: ' + d.ws_clients;
        }
    }).catch(function() {
        elDeviceSelect.innerHTML = '<option value="-1">VoicTune offline</option>';
    });
}

/* ── Session controls ────────────────────────────────────────── */
window.vtStartSession = function() {
    var devIdx = parseInt(elDeviceSelect.value);
    if (devIdx >= 0 && audioSource === 'server') {
        vtApi('PUT', '/api/v1/voictune/device', {device_index: devIdx});
    }

    vtApi('POST', '/api/v1/voictune/session/start', {name: 'VoicTune Session'}).then(function(d) {
        if (d && d.ok) {
            sessionActive = true;
            sessionStart = Date.now();
            elStartBtn.disabled = true;
            elStopBtn.disabled = false;
            mc1Toast('Session started', 'ok');
            startPolling();
        } else {
            mc1Toast('Session start failed: ' + (d && d.error ? d.error : 'unknown'), 'err');
        }
    }).catch(function() {
        mc1Toast('VoicTune daemon unreachable', 'err');
    });

    if (audioSource === 'browser') {
        vtAudioStart(devIdx);
    }
};

window.vtStopSession = function() {
    vtApi('POST', '/api/v1/voictune/session/stop').then(function(d) {
        sessionActive = false;
        elStartBtn.disabled = false;
        elStopBtn.disabled = true;
        stopPolling();
        mc1Toast('Session stopped', 'ok');
    }).catch(function() {
        mc1Toast('Failed to stop session', 'err');
    });

    if (audioSource === 'browser') {
        vtAudioStop();
    }
};

/* ── Audio source toggle ─────────────────────────────────────── */
window.vtSetSource = function(src) {
    audioSource = src;
    document.getElementById('vt-src-server').className = 'vt-source-btn' + (src === 'server' ? ' active' : '');
    document.getElementById('vt-src-browser').className = 'vt-source-btn' + (src === 'browser' ? ' active' : '');

    if (src === 'browser') {
        vtAudioEnumerateBrowserDevices(elDeviceSelect);
    } else {
        loadDevices();
        vtAudioStop();
    }
};

/* ── Data polling ────────────────────────────────────────────── */
function startPolling() {
    if (pollTimer) return;
    pollTimer    = setInterval(pollData, POLL_MS);
    tipsPollTimer = setInterval(pollTips, TIPS_POLL_MS);
    sessionTimer = setInterval(updateSessionTimer, 1000);
    pollData();
    pollTips();
}

function stopPolling() {
    if (pollTimer)     { clearInterval(pollTimer); pollTimer = null; }
    if (tipsPollTimer) { clearInterval(tipsPollTimer); tipsPollTimer = null; }
    if (sessionTimer)  { clearInterval(sessionTimer); sessionTimer = null; }
    elSessionTimer.textContent = '0:00';
}

function pollData() {
    vtApi('GET', '/api/v1/voictune/meters').then(function(d) {
        if (d && d.ok !== false) latestMeters = d;
    }).catch(function(){});

    vtApi('GET', '/api/v1/voictune/waveform').then(function(d) {
        if (d && d.ok) latestWaveform = d.samples || [];
    }).catch(function(){});

    vtApi('GET', '/api/v1/voictune/spectrum').then(function(d) {
        if (d && d.ok) latestSpectrum = d.bins || [];
    }).catch(function(){});
}

function pollTips() {
    vtApi('GET', '/api/v1/voictune/coaching/tips').then(function(d) {
        if (!d || !d.ok) return;
        renderTips(d.tips || []);
    }).catch(function(){});
}

function updateSessionTimer() {
    if (!sessionActive) return;
    var elapsed = Math.floor((Date.now() - sessionStart) / 1000);
    var m = Math.floor(elapsed / 60);
    var s = elapsed % 60;
    elSessionTimer.textContent = m + ':' + (s < 10 ? '0' : '') + s;
}

/* ── Render tips ─────────────────────────────────────────────── */
function renderTips(tips) {
    if (!elTipsList) return;
    if (tips.length === 0) {
        elTipsList.innerHTML = '<div class="vt-tip">'
            + '<div class="vt-tip-icon info"><i class="fa-solid fa-circle-info"></i></div>'
            + '<div class="vt-tip-body"><div class="vt-tip-msg">No tips yet</div>'
            + '<div class="vt-tip-sug">Keep speaking into the microphone for analysis.</div></div></div>';
        return;
    }
    var html = '';
    var icons = {
        info:       'fa-circle-info',
        suggestion: 'fa-lightbulb',
        warning:    'fa-triangle-exclamation',
        critical:   'fa-circle-exclamation'
    };
    tips.forEach(function(t) {
        var sev = t.severity || 'info';
        html += '<div class="vt-tip">'
            + '<div class="vt-tip-icon ' + esc(sev) + '"><i class="fa-solid ' + (icons[sev] || icons.info) + '"></i></div>'
            + '<div class="vt-tip-body">'
            + '<div class="vt-tip-msg">' + esc(t.message || '') + '</div>'
            + (t.suggestion ? '<div class="vt-tip-sug">' + esc(t.suggestion) + '</div>' : '')
            + '</div></div>';
    });
    elTipsList.innerHTML = html;
}

function esc(s) {
    return String(s || '').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

/* ── rAF render loop ─────────────────────────────────────────── */
function renderLoop() {
    if (viz && latestMeters) {
        var m = latestMeters;

        /* Meters */
        viz.drawMeter(elRmsCanvas, m.rms_db || -96, m.peak_hold_db || -96, 'RMS');
        viz.drawMeter(elPeakCanvas, m.peak_db || -96, m.peak_hold_db || -96, 'Peak');
        viz.drawLufsGauge(elLufsCanvas, m.lufs || -96, -16);

        elRmsVal.textContent  = (m.rms_db  != null ? m.rms_db.toFixed(1) : '-96') + ' dB';
        elPeakVal.textContent = (m.peak_db != null ? m.peak_db.toFixed(1) : '-96') + ' dB';
        elLufsVal.textContent = (m.lufs    != null ? m.lufs.toFixed(1)    : '-96.0');

        /* Pitch */
        elPitchNote.textContent = m.note || '--';
        elPitchHz.textContent   = m.pitch_hz ? m.pitch_hz.toFixed(1) + ' Hz' : '-- Hz';

        var cents = m.cents || 0;
        var centsPct = Math.max(0, Math.min(100, 50 + cents));
        elCentsInd.style.left = centsPct + '%';

        var absCents = Math.abs(cents);
        if (absCents <= 10) {
            elCentsInd.style.background = 'var(--green)';
            elCentsInd.style.boxShadow  = '0 0 6px rgba(34,197,94,.5)';
        } else if (absCents <= 25) {
            elCentsInd.style.background = 'var(--yellow)';
            elCentsInd.style.boxShadow  = '0 0 6px rgba(234,179,8,.5)';
        } else {
            elCentsInd.style.background = 'var(--red)';
            elCentsInd.style.boxShadow  = '0 0 6px rgba(239,68,68,.5)';
        }

        var conf = m.confidence || 0;
        elPitchConf.textContent    = Math.round(conf * 100) + '%';
        elPitchConfBar.style.width = (conf * 100) + '%';

        /* Note color: teal if confident, dim otherwise */
        elPitchNote.style.color = conf > 0.5 ? 'var(--teal)' : 'var(--muted)';
    }

    if (viz && latestWaveform) {
        viz.drawOscilloscope(elScopeCanvas, latestWaveform);
    }
    if (viz && latestSpectrum) {
        viz.drawSpectrum(elSpectrumCanvas, latestSpectrum, 48000);
    }

    requestAnimationFrame(renderLoop);
}

/* ── AI Coach button ─────────────────────────────────────────── */
document.addEventListener('DOMContentLoaded', function() {
    var aiBtn = document.getElementById('vt-ai-coach-btn');
    if (aiBtn) {
        aiBtn.addEventListener('click', function() {
            if (!latestMeters) {
                mc1Toast('No meter data available. Start a session first.', 'warn');
                return;
            }
            aiBtn.disabled = true;
            aiBtn.innerHTML = '<div class="spinner" style="width:12px;height:12px;border-width:2px"></div> Thinking...';
            vtApi('POST', '/api/v1/ai/coaching', {
                meters: latestMeters,
                context: 'live_session'
            }).then(function(d) {
                aiBtn.disabled = false;
                aiBtn.innerHTML = '<i class="fa-solid fa-wand-magic-sparkles"></i> AI Coach';
                if (d && d.ok && d.response && d.response.message) {
                    var content = d.response.message.content || '';
                    var tip = {severity:'suggestion', message:'AI Coach', suggestion: content};
                    var tips = [{severity:'suggestion', message:'AI Coach Advice', suggestion: content}];
                    renderTips(tips);
                } else if (d && d.error) {
                    mc1Toast('AI Coach: ' + d.error, 'err');
                }
            }).catch(function() {
                aiBtn.disabled = false;
                aiBtn.innerHTML = '<i class="fa-solid fa-wand-magic-sparkles"></i> AI Coach';
                mc1Toast('AI Coach unavailable', 'err');
            });
        });
    }

    /* ── AI text input at bottom of coaching panel ───────────── */
    var vtAiInput = document.getElementById('vt-ai-input');
    var vtAiSend  = document.getElementById('vt-ai-send');

    function sendVtAiQuery() {
        if (!vtAiInput) return;
        var query = vtAiInput.value.trim();
        if (!query) return;

        vtAiSend.disabled = true;
        vtAiSend.innerHTML = '<div class="spinner" style="width:12px;height:12px;border-width:2px"></div>';

        /* Build context with current metrics if available */
        var metricsCtx = '';
        if (latestMeters) {
            metricsCtx = '\n\nCurrent voice metrics: RMS=' + (latestMeters.rms_db || -96) + 'dB'
                + ', Peak=' + (latestMeters.peak_db || -96) + 'dB'
                + ', LUFS=' + (latestMeters.lufs || -96)
                + ', Pitch=' + (latestMeters.pitch_hz || 0) + 'Hz'
                + ', Note=' + (latestMeters.note || '--');
        }

        vtApi('POST', '/api/v1/ai/coaching', {
            meters: latestMeters || {},
            context: 'user_question',
            question: query + metricsCtx
        }).then(function(d) {
            vtAiSend.disabled = false;
            vtAiSend.innerHTML = '<i class="fa-solid fa-paper-plane"></i>';

            if (d && d.ok && d.response && d.response.message) {
                var content = d.response.message.content || '';
                renderTips([{severity:'suggestion', message: query, suggestion: content}]);
                vtAiInput.value = '';
            } else if (d && d.error) {
                mc1Toast('AI: ' + d.error, 'err');
            } else {
                mc1Toast('No response from AI', 'warn');
            }
        }).catch(function() {
            vtAiSend.disabled = false;
            vtAiSend.innerHTML = '<i class="fa-solid fa-paper-plane"></i>';
            mc1Toast('AI service unreachable', 'err');
        });
    }

    if (vtAiSend) {
        vtAiSend.addEventListener('click', sendVtAiQuery);
    }
    if (vtAiInput) {
        vtAiInput.addEventListener('keydown', function(e) {
            if (e.key === 'Enter') { e.preventDefault(); sendVtAiQuery(); }
        });
    }

    /* ── Content Analysis button ─────────────────────────────── */
    var caBtn = document.getElementById('vt-content-analyze-btn');
    if (caBtn) {
        caBtn.addEventListener('click', function() {
            var transcript = (document.getElementById('vt-content-transcript') || {}).value || '';
            var payload = {};
            if (transcript.trim()) {
                payload.transcript = transcript.trim();
            } else {
                /* Try to use current session — session_id is usually 1 for default */
                payload.session_id = 1;
            }

            caBtn.disabled = true;
            caBtn.innerHTML = '<div class="spinner" style="width:12px;height:12px;border-width:2px;display:inline-block;vertical-align:middle;margin-right:4px"></div> Analyzing...';

            vtApi('POST', '/api/v1/ai/content/analyze', payload).then(function(d) {
                caBtn.disabled = false;
                caBtn.innerHTML = '<i class="fa-solid fa-wand-magic-sparkles"></i> Analyze Session';

                if (!d || !d.ok) {
                    mc1Toast('Content analysis failed: ' + (d && d.error ? d.error : 'Unknown'), 'err');
                    return;
                }

                /* Show results */
                var results = document.getElementById('vt-content-results');
                results.style.display = '';

                document.getElementById('vt-ca-summary').textContent = d.summary || 'No summary available';
                document.getElementById('vt-ca-title').textContent = d.title_suggestion || '--';

                /* Topics as badges */
                var topicsEl = document.getElementById('vt-ca-topics');
                var topics = d.topics || [];
                topicsEl.innerHTML = topics.length
                    ? topics.map(function(t) {
                        return '<span style="padding:2px 8px;border-radius:10px;background:rgba(20,184,166,.15);color:var(--teal);font-size:11px">' + esc(t) + '</span>';
                    }).join('')
                    : '<span style="color:var(--muted);font-size:12px">None detected</span>';

                /* Tags as badges */
                var tagsEl = document.getElementById('vt-ca-tags');
                var tags = d.tags || [];
                tagsEl.innerHTML = tags.length
                    ? tags.map(function(t) {
                        return '<span style="padding:2px 8px;border-radius:10px;background:rgba(139,92,246,.15);color:#a78bfa;font-size:11px">' + esc(t) + '</span>';
                    }).join('')
                    : '<span style="color:var(--muted);font-size:12px">None detected</span>';

                /* Pace analysis */
                var paceEl = document.getElementById('vt-ca-pace');
                var pace = d.pace_analysis || {};
                if (pace.avg_wpm) {
                    paceEl.innerHTML = '<strong>' + pace.avg_wpm + ' WPM</strong> avg'
                        + (pace.variance ? ' (variance: ' + pace.variance + ')' : '')
                        + (pace.assessment ? '<br>' + esc(pace.assessment) : '');
                } else {
                    paceEl.textContent = 'No pace data available';
                }

                /* Filler words */
                var fillerEl = document.getElementById('vt-ca-filler');
                var filler = d.filler_words || {};
                if (filler.count !== undefined) {
                    fillerEl.innerHTML = '<strong>' + filler.count + '</strong> filler words'
                        + (filler.ratio ? ' (' + (filler.ratio * 100).toFixed(1) + '% of speech)' : '')
                        + (filler.examples && filler.examples.length
                            ? '<br><span style="color:var(--muted)">Common: ' + filler.examples.map(esc).join(', ') + '</span>'
                            : '');
                } else {
                    fillerEl.textContent = 'No filler word data available';
                }

                /* Latency */
                if (d.latency_ms) {
                    document.getElementById('vt-ca-latency').textContent = 'AI response: ' + d.latency_ms + 'ms';
                }
            }).catch(function() {
                caBtn.disabled = false;
                caBtn.innerHTML = '<i class="fa-solid fa-wand-magic-sparkles"></i> Analyze Session';
                mc1Toast('Content analysis service unreachable', 'err');
            });
        });
    }
});

})();
</script>

<?php require_once __DIR__ . '/app/inc/footer.php'; ?>
