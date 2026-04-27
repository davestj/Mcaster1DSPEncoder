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

/* ── Command bar (top) ──────────────────────────────────────────── */
.vt-command-bar {
    background: var(--card);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 14px 18px;
    margin-bottom: 14px;
}
.vt-cmd-row {
    display: flex;
    align-items: center;
    gap: 12px;
    flex-wrap: wrap;
}
.vt-cmd-row + .vt-cmd-row {
    margin-top: 10px;
    padding-top: 10px;
    border-top: 1px solid rgba(51,65,85,.25);
}
.vt-conn-indicator {
    display: flex;
    align-items: center;
    gap: 6px;
    font-size: 12px;
    font-weight: 600;
}
.vt-conn-dot {
    width: 10px;
    height: 10px;
    border-radius: 50%;
    background: var(--muted);
    flex-shrink: 0;
    transition: background 0.3s;
}
.vt-conn-dot.online {
    background: #22c55e;
    box-shadow: 0 0 8px rgba(34,197,94,.6);
    animation: live-dot 1.4s ease-in-out infinite;
}
.vt-conn-dot.offline {
    background: #ef4444;
    box-shadow: 0 0 6px rgba(239,68,68,.4);
}
.vt-conn-label { color: var(--text); }
.vt-audio-status {
    font-size: 11px;
    color: var(--muted);
    display: flex;
    align-items: center;
    gap: 5px;
}
.vt-audio-status i { font-size: 10px; }

/* Mode buttons */
.vt-mode-group {
    display: flex;
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    overflow: hidden;
}
.vt-mode-btn {
    padding: 7px 14px;
    font-size: 12px;
    background: transparent;
    color: var(--text-dim);
    border: none;
    cursor: pointer;
    transition: all 0.15s;
    white-space: nowrap;
    display: flex;
    align-items: center;
    gap: 5px;
}
.vt-mode-btn + .vt-mode-btn { border-left: 1px solid var(--border); }
.vt-mode-btn.active {
    background: rgba(20,184,166,.15);
    color: var(--teal);
    font-weight: 600;
}
.vt-mode-btn:hover:not(.active) {
    background: rgba(255,255,255,.04);
    color: var(--text);
}

/* Session controls */
.vt-session-group {
    display: flex;
    align-items: center;
    gap: 8px;
}
.vt-session-timer {
    font-family: 'SF Mono','Fira Code',monospace;
    font-size: 15px;
    color: var(--teal);
    min-width: 60px;
}
.vt-session-label {
    font-size: 11px;
    color: var(--muted);
}

/* File analyze bar */
.vt-file-bar {
    display: none;
    align-items: center;
    gap: 8px;
}
.vt-file-bar.visible { display: flex; }
.vt-file-bar input[type="file"] { display: none; }
.vt-file-name {
    font-size: 12px;
    color: var(--text);
    flex: 1;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    max-width: 350px;
    padding: 5px 10px;
    background: rgba(255,255,255,.04);
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    font-family: monospace;
}
.vt-file-progress {
    display: none;
    align-items: center;
    gap: 8px;
    font-size: 12px;
    color: var(--teal);
}
.vt-file-progress.visible { display: flex; }

/* Device select bar */
.vt-device-bar {
    display: none;
    align-items: center;
    gap: 8px;
}
.vt-device-bar.visible { display: flex; }
.vt-device-bar .form-select {
    width: auto;
    min-width: 200px;
    font-size: 12px;
}

/* ── Top row: meters + pitch ────────────────────────────────────── */
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
.vt-meter-range {
    display: flex;
    justify-content: space-between;
    width: 100%;
    font-size: 8px;
    color: var(--muted);
    padding: 0 2px;
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
    max-height: 220px;
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
.vt-tip-sug { color: var(--text-dim); font-size: 12px; margin-top: 2px; white-space: pre-wrap; }

/* ── WebGL / 2D toggle ──────────────────────────────────────────── */
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

/* ── Idle overlay ───────────────────────────────────────────────── */
.vt-idle-msg {
    text-align: center;
    padding: 20px;
    color: var(--muted);
    font-size: 13px;
    line-height: 1.6;
}
.vt-idle-msg i { display: block; font-size: 28px; margin-bottom: 8px; color: var(--teal); opacity: .5; }

/* ── Responsive ──────────────────────────────────────────────────── */
@media (max-width: 860px) {
    .vt-top-row { grid-template-columns: 1fr 1fr; }
    .vt-viz-row { grid-template-columns: 1fr; }
}
@media (max-width: 580px) {
    .vt-top-row { grid-template-columns: 1fr; }
    .vt-cmd-row { flex-direction: column; align-items: stretch; }
}
</style>

<!-- ── Command Bar (prominent, always at top) ──────────────────────── -->
<div class="vt-command-bar">
    <!-- Row 1: Connection + Version + Audio status -->
    <div class="vt-cmd-row">
        <div class="vt-conn-indicator">
            <div class="vt-conn-dot" id="vt-conn-dot"></div>
            <span class="vt-conn-label" id="vt-conn-label">Checking...</span>
        </div>
        <div class="vt-audio-status" id="vt-audio-status">
            <i class="fa-solid fa-volume-xmark"></i> <span>No Audio Input</span>
        </div>
        <div style="flex:1"></div>
        <div class="vt-source-toggle" id="vt-webgl-toggle" style="display:none">
            <button class="vt-source-btn" id="vt-viz-2d" onclick="vtSetVizMode('2d')">
                <i class="fa-solid fa-display"></i> 2D
            </button>
            <button class="vt-source-btn" id="vt-viz-webgl" onclick="vtSetVizMode('webgl')">
                <i class="fa-solid fa-cube"></i> WebGL
            </button>
        </div>
    </div>

    <!-- Row 2: Mode selector + Session controls -->
    <div class="vt-cmd-row">
        <div class="vt-mode-group">
            <button class="vt-mode-btn active" id="vt-mode-file" onclick="vtSetMode('file')">
                <i class="fa-solid fa-folder-open"></i> Analyze File
            </button>
            <button class="vt-mode-btn" id="vt-mode-browser" onclick="vtSetMode('browser')">
                <i class="fa-solid fa-microphone"></i> Browser Mic
            </button>
            <button class="vt-mode-btn" id="vt-mode-server" onclick="vtSetMode('server')">
                <i class="fa-solid fa-server"></i> Server Mic
            </button>
            <button class="vt-mode-btn" id="vt-mode-demo" onclick="vtSetMode('demo')">
                <i class="fa-solid fa-play-circle"></i> Demo
            </button>
        </div>
        <div style="flex:1"></div>
        <div class="vt-session-group">
            <button class="btn btn-success btn-sm" id="vt-start-btn" onclick="vtStartSession()" style="min-width:120px">
                <i class="fa-solid fa-play"></i> Start Session
            </button>
            <button class="btn btn-danger btn-sm" id="vt-stop-btn" onclick="vtStopSession()" disabled style="min-width:90px">
                <i class="fa-solid fa-stop"></i> Stop
            </button>
            <div class="vt-session-timer" id="vt-session-timer">0:00</div>
            <span class="vt-session-label" id="vt-session-label">Not Active</span>
        </div>
    </div>

    <!-- Row 3a: File mode bar (shown when mode=file) -->
    <div class="vt-cmd-row vt-file-bar visible" id="vt-file-bar">
        <button class="btn btn-sm btn-secondary" id="vt-pick-media" onclick="vtPickMediaFile()">
            <i class="fa-solid fa-folder-open"></i> Media Library
        </button>
        <button class="btn btn-sm btn-secondary" id="vt-pick-upload" onclick="document.getElementById('vt-file-input').click()">
            <i class="fa-solid fa-upload"></i> Upload File
        </button>
        <input type="file" id="vt-file-input" accept="audio/*">
        <div class="vt-file-name" id="vt-file-name">No file selected</div>
        <button class="btn btn-sm btn-primary" id="vt-analyze-btn" onclick="vtAnalyzeFile()" disabled>
            <i class="fa-solid fa-waveform-lines"></i> Analyze
        </button>
        <div class="vt-file-progress" id="vt-file-progress">
            <div class="spinner" style="width:14px;height:14px;border-width:2px"></div>
            <span id="vt-file-progress-text">Decoding...</span>
        </div>
    </div>

    <!-- Row 3b: Device select bar (shown when mode=browser or server) -->
    <div class="vt-cmd-row vt-device-bar" id="vt-device-bar">
        <label style="font-size:11px;color:var(--text-dim);white-space:nowrap">Input Device:</label>
        <select class="form-select" id="vt-device-select" title="Input device">
            <option value="-1">Loading devices...</option>
        </select>
    </div>
</div>

<!-- ── Page header ──────────────────────────────────────────────────── -->
<div class="sec-hdr">
    <div class="sec-title">
        <i class="fa-solid fa-microphone-lines" style="color:var(--teal);margin-right:8px"></i>
        VoicTune -- Voice Analysis &amp; Coaching
    </div>
    <div style="display:flex;gap:8px;align-items:center">
        <span id="vt-ws-badge" class="badge badge-gray" style="display:none">WS: 0</span>
    </div>
</div>

<!-- ── Top row: meters + pitch ─────────────────────────────────────── -->
<div class="vt-top-row">
    <div class="vt-meter-box">
        <div class="vt-label">RMS</div>
        <canvas id="vt-rms-canvas" class="vt-meter-canvas" width="40" height="140"></canvas>
        <div id="vt-rms-val" style="font-size:11px;color:var(--text-dim);font-variant-numeric:tabular-nums">-96 dB</div>
        <div class="vt-meter-range"><span>-60</span><span>-18</span><span>0</span></div>
    </div>
    <div class="vt-meter-box">
        <div class="vt-label">Peak</div>
        <canvas id="vt-peak-canvas" class="vt-meter-canvas" width="40" height="140"></canvas>
        <div id="vt-peak-val" style="font-size:11px;color:var(--text-dim);font-variant-numeric:tabular-nums">-96 dB</div>
        <div class="vt-meter-range"><span>-60</span><span>-18</span><span>0</span></div>
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
        <div class="vt-idle-msg" id="vt-idle-msg">
            <i class="fa-solid fa-microphone-lines"></i>
            Start a session, analyze a file, or try Demo mode to begin.<br>
            <span style="font-size:11px">Choose a mode from the control bar above.</span>
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

<!-- ── Voice Effects Panel ─────────────────────────────────────────── -->
<div class="vt-coaching" style="margin-top:14px">
    <div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:10px">
        <div class="vt-label" style="margin-bottom:0"><i class="fa-solid fa-wand-magic-sparkles" style="margin-right:4px;color:#f59e0b"></i> Voice Effects</div>
        <span id="vfx-status" style="font-size:11px;color:var(--muted)"></span>
    </div>

    <!-- File selector -->
    <div style="display:flex;gap:8px;align-items:center;margin-bottom:14px">
        <label style="font-size:12px;color:var(--text-dim);white-space:nowrap">Audio File:</label>
        <input type="text" id="vfx-file-path" placeholder="/path/to/audio.wav"
               style="flex:1;padding:6px 10px;background:rgba(255,255,255,.05);border:1px solid var(--border);border-radius:var(--radius-sm,4px);color:var(--text);font-size:12px;outline:none;font-family:monospace">
        <button class="btn btn-xs btn-secondary" id="vfx-browse-btn" title="Browse media library">
            <i class="fa-solid fa-folder-open"></i>
        </button>
    </div>

    <!-- Tabbed effects -->
    <div style="display:flex;gap:4px;margin-bottom:12px;flex-wrap:wrap" id="vfx-tabs">
        <button class="vt-source-btn active" data-vfx-tab="debreath" onclick="vfxTab('debreath')">De-Breath</button>
        <button class="vt-source-btn" data-vfx-tab="voicechg" onclick="vfxTab('voicechg')">Voice Changer</button>
        <button class="vt-source-btn" data-vfx-tab="autotune" onclick="vfxTab('autotune')">Auto-Tune</button>
        <button class="vt-source-btn" data-vfx-tab="noisegate" onclick="vfxTab('noisegate')">Noise Gate</button>
        <button class="vt-source-btn" data-vfx-tab="deesser" onclick="vfxTab('deesser')">De-Esser</button>
    </div>

    <!-- De-Breath panel -->
    <div class="vfx-panel" id="vfx-panel-debreath">
        <div style="display:grid;grid-template-columns:1fr 1fr;gap:10px">
            <label style="font-size:11px;color:var(--text-dim)">Threshold (dB)
                <input type="range" id="vfx-db-threshold" min="-60" max="-10" value="-30" style="width:100%">
                <span id="vfx-db-threshold-val" style="font-size:10px;color:var(--muted)">-30 dB</span>
            </label>
            <label style="font-size:11px;color:var(--text-dim)">Max Reduction (dB)
                <input type="range" id="vfx-db-reduction" min="-40" max="0" value="-20" style="width:100%">
                <span id="vfx-db-reduction-val" style="font-size:10px;color:var(--muted)">-20 dB</span>
            </label>
        </div>
        <label style="font-size:11px;color:var(--text-dim);display:block;margin-top:6px">Min Breath Duration (ms)
            <input type="range" id="vfx-db-minms" min="50" max="800" value="100" style="width:100%">
            <span id="vfx-db-minms-val" style="font-size:10px;color:var(--muted)">100 ms</span>
        </label>
        <div style="display:flex;gap:8px;margin-top:10px">
            <button class="btn btn-xs btn-secondary" onclick="vfxProcess('de-breath',true)"><i class="fa-solid fa-play"></i> Preview (10s)</button>
            <button class="btn btn-xs btn-primary" onclick="vfxProcess('de-breath',false)"><i class="fa-solid fa-check"></i> Apply to Full File</button>
        </div>
    </div>

    <!-- Voice Changer panel -->
    <div class="vfx-panel" id="vfx-panel-voicechg" style="display:none">
        <div style="display:grid;grid-template-columns:1fr 1fr;gap:10px">
            <label style="font-size:11px;color:var(--text-dim)">Effect
                <select id="vfx-vc-effect" class="form-select" style="width:100%;margin-top:4px;font-size:12px">
                    <option value="deeper">Deeper</option>
                    <option value="higher">Higher</option>
                    <option value="robot">Robot</option>
                    <option value="whisper">Whisper</option>
                    <option value="radio">Radio</option>
                    <option value="telephone">Telephone</option>
                    <option value="chipmunk">Chipmunk</option>
                    <option value="darth_vader">Darth Vader</option>
                </select>
            </label>
            <label style="font-size:11px;color:var(--text-dim)">Intensity
                <input type="range" id="vfx-vc-intensity" min="0" max="100" value="50" style="width:100%">
                <span id="vfx-vc-intensity-val" style="font-size:10px;color:var(--muted)">50%</span>
            </label>
        </div>
        <div style="display:flex;gap:8px;margin-top:10px">
            <button class="btn btn-xs btn-secondary" onclick="vfxProcess('voice-change',true)"><i class="fa-solid fa-play"></i> Preview (10s)</button>
            <button class="btn btn-xs btn-primary" onclick="vfxProcess('voice-change',false)"><i class="fa-solid fa-check"></i> Apply to Full File</button>
        </div>
    </div>

    <!-- Auto-Tune panel -->
    <div class="vfx-panel" id="vfx-panel-autotune" style="display:none">
        <div style="display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px">
            <label style="font-size:11px;color:var(--text-dim)">Key
                <select id="vfx-at-key" class="form-select" style="width:100%;margin-top:4px;font-size:12px">
                    <option>C</option><option>C#</option><option>D</option><option>D#</option>
                    <option>E</option><option>F</option><option>F#</option><option>G</option>
                    <option>G#</option><option>A</option><option>A#</option><option>B</option>
                </select>
            </label>
            <label style="font-size:11px;color:var(--text-dim)">Scale
                <select id="vfx-at-scale" class="form-select" style="width:100%;margin-top:4px;font-size:12px">
                    <option value="major">Major</option>
                    <option value="minor">Minor</option>
                    <option value="chromatic">Chromatic</option>
                </select>
            </label>
            <label style="font-size:11px;color:var(--text-dim)">Strength
                <input type="range" id="vfx-at-strength" min="0" max="100" value="80" style="width:100%">
                <span id="vfx-at-strength-val" style="font-size:10px;color:var(--muted)">80%</span>
            </label>
        </div>
        <label style="font-size:11px;color:var(--text-dim);display:block;margin-top:6px">Correction Speed (ms)
            <input type="range" id="vfx-at-speed" min="5" max="500" value="50" style="width:100%">
            <span id="vfx-at-speed-val" style="font-size:10px;color:var(--muted)">50 ms</span>
        </label>
        <div style="display:flex;gap:8px;margin-top:10px">
            <button class="btn btn-xs btn-secondary" onclick="vfxProcess('auto-tune',true)"><i class="fa-solid fa-play"></i> Preview (10s)</button>
            <button class="btn btn-xs btn-primary" onclick="vfxProcess('auto-tune',false)"><i class="fa-solid fa-check"></i> Apply to Full File</button>
        </div>
    </div>

    <!-- Noise Gate panel -->
    <div class="vfx-panel" id="vfx-panel-noisegate" style="display:none">
        <div style="display:grid;grid-template-columns:1fr 1fr;gap:10px">
            <label style="font-size:11px;color:var(--text-dim)">Threshold (dB)
                <input type="range" id="vfx-ng-threshold" min="-80" max="0" value="-40" style="width:100%">
                <span id="vfx-ng-threshold-val" style="font-size:10px;color:var(--muted)">-40 dB</span>
            </label>
            <label style="font-size:11px;color:var(--text-dim)">Attack (ms)
                <input type="range" id="vfx-ng-attack" min="1" max="500" value="5" style="width:100%">
                <span id="vfx-ng-attack-val" style="font-size:10px;color:var(--muted)">5 ms</span>
            </label>
            <label style="font-size:11px;color:var(--text-dim)">Release (ms)
                <input type="range" id="vfx-ng-release" min="1" max="2000" value="100" style="width:100%">
                <span id="vfx-ng-release-val" style="font-size:10px;color:var(--muted)">100 ms</span>
            </label>
            <label style="font-size:11px;color:var(--text-dim)">Hold (ms)
                <input type="range" id="vfx-ng-hold" min="0" max="1000" value="50" style="width:100%">
                <span id="vfx-ng-hold-val" style="font-size:10px;color:var(--muted)">50 ms</span>
            </label>
        </div>
        <div style="display:flex;gap:8px;margin-top:10px">
            <button class="btn btn-xs btn-secondary" onclick="vfxProcess('noise-gate',true)"><i class="fa-solid fa-play"></i> Preview (10s)</button>
            <button class="btn btn-xs btn-primary" onclick="vfxProcess('noise-gate',false)"><i class="fa-solid fa-check"></i> Apply to Full File</button>
        </div>
    </div>

    <!-- De-Esser panel -->
    <div class="vfx-panel" id="vfx-panel-deesser" style="display:none">
        <div style="display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px">
            <label style="font-size:11px;color:var(--text-dim)">Center Freq (Hz)
                <input type="range" id="vfx-de-freq" min="2000" max="12000" value="6500" style="width:100%">
                <span id="vfx-de-freq-val" style="font-size:10px;color:var(--muted)">6500 Hz</span>
            </label>
            <label style="font-size:11px;color:var(--text-dim)">Threshold (dB)
                <input type="range" id="vfx-de-threshold" min="-40" max="0" value="-20" style="width:100%">
                <span id="vfx-de-threshold-val" style="font-size:10px;color:var(--muted)">-20 dB</span>
            </label>
            <label style="font-size:11px;color:var(--text-dim)">Ratio
                <input type="range" id="vfx-de-ratio" min="1" max="20" value="4" style="width:100%">
                <span id="vfx-de-ratio-val" style="font-size:10px;color:var(--muted)">4:1</span>
            </label>
        </div>
        <div style="display:flex;gap:8px;margin-top:10px">
            <button class="btn btn-xs btn-secondary" onclick="vfxProcess('de-esser',true)"><i class="fa-solid fa-play"></i> Preview (10s)</button>
            <button class="btn btn-xs btn-primary" onclick="vfxProcess('de-esser',false)"><i class="fa-solid fa-check"></i> Apply to Full File</button>
        </div>
    </div>

    <!-- Processed file result -->
    <div id="vfx-result" style="display:none;margin-top:10px;padding:8px 12px;background:rgba(34,197,94,.08);border:1px solid rgba(34,197,94,.2);border-radius:var(--radius-sm,4px);font-size:12px;color:var(--green)">
        <i class="fa-solid fa-check-circle"></i>
        <span id="vfx-result-text">Processed successfully.</span>
        <span id="vfx-result-path" style="display:block;font-family:monospace;font-size:11px;color:var(--text-dim);margin-top:4px"></span>
        <span id="vfx-result-time" style="font-size:10px;color:var(--muted)"></span>
    </div>
</div>

<?php require_once __DIR__ . '/app/inc/media_picker.php'; ?>

<script src="/js/webgl-viz.js"></script>
<script src="/js/voictune-viz.js"></script>
<script src="/js/voictune-audio.js"></script>

<script>
(function(){
'use strict';

/* ── VoicTune API base (proxied through admin on same origin) ── */
var VT_BASE = '';  /* same origin -- proxy via /api/v1/proxy/ */
var POLL_MS = 80;          /* ~12 Hz data polling */
var TIPS_POLL_MS = 3000;   /* coaching tips every 3s */

var sessionActive = false;
var sessionStart  = 0;
var sessionTimer  = null;
var pollTimer     = null;
var tipsPollTimer = null;
var currentMode   = 'file';  /* 'file', 'browser', 'server', 'demo' */
var vtOnline      = false;

/* ── Browser-side analysis state ────────────────────────────── */
var fileAudioCtx     = null;
var fileAnalyser     = null;
var fileSourceNode   = null;
var fileAudioBuffer  = null;
var fileIsPlaying    = false;
var fileAnimFrame    = null;
var selectedFile     = null;   /* File object or {id, file_path} from media library */
var selectedTrackId  = null;

/* ── VoicTune fetch helper (same-origin via admin proxy) ─────── */
function vtApi(method, path, body) {
    var proxyPath = path;
    if (path.indexOf('/api/v1/voictune/') === 0)
        proxyPath = '/api/v1/proxy/voictune/' + path.substring('/api/v1/voictune/'.length);
    else if (path.indexOf('/api/v1/ai/') === 0)
        proxyPath = '/api/v1/proxy/ai/' + path.substring('/api/v1/ai/'.length);
    var opts = {
        method: method,
        headers: {'Content-Type': 'application/json'},
        credentials: 'include'
    };
    if (body !== undefined && body !== null) opts.body = JSON.stringify(body);
    return fetch(VT_BASE + proxyPath, opts).then(function(r) {
        return r.json().then(function(d) { d._status = r.status; return d; });
    });
}

/* ── Cached DOM elements ─────────────────────────────────────── */
var elRmsCanvas, elPeakCanvas, elLufsCanvas, elScopeCanvas, elSpectrumCanvas;
var elRmsVal, elPeakVal, elLufsVal;
var elPitchNote, elPitchHz, elCentsInd, elPitchConf, elPitchConfBar;
var elTipsList, elDeviceSelect;
var elStartBtn, elStopBtn, elSessionTimer, elSessionLabel;
var elConnDot, elConnLabel, elAudioStatus, elWsBadge;
var elFileBar, elDeviceBar, elFileName, elAnalyzeBtn, elFileProgress, elFileProgressText;
var elIdleMsg;

/* ── Viz engine instance ─────────────────────────────────────── */
var viz = null;

/* ── WebGL visualization instances ─────────────────────────── */
var vizMode = '2d';
var glSpectrum = null;
var glSpectrogram = null;
var glWaveform = null;
var glRmsMeter = null;
var glPeakMeter = null;

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
    elSessionLabel   = document.getElementById('vt-session-label');
    elConnDot        = document.getElementById('vt-conn-dot');
    elConnLabel      = document.getElementById('vt-conn-label');
    elAudioStatus    = document.getElementById('vt-audio-status');
    elWsBadge        = document.getElementById('vt-ws-badge');
    elFileBar        = document.getElementById('vt-file-bar');
    elDeviceBar      = document.getElementById('vt-device-bar');
    elFileName       = document.getElementById('vt-file-name');
    elAnalyzeBtn     = document.getElementById('vt-analyze-btn');
    elFileProgress   = document.getElementById('vt-file-progress');
    elFileProgressText = document.getElementById('vt-file-progress-text');
    elIdleMsg        = document.getElementById('vt-idle-msg');

    /* Size canvases to their CSS box */
    sizeCanvas(elScopeCanvas);
    sizeCanvas(elSpectrumCanvas);

    viz = new VoicTuneViz();

    /* Draw idle grids immediately */
    drawIdleCanvases();

    /* WebGL visualization setup */
    if (window.WebGLViz && WebGLViz.isWebGLAvailable()) {
        var toggle = document.getElementById('vt-webgl-toggle');
        if (toggle) toggle.style.display = '';
        if (WebGLViz.getWebGLPref()) {
            vtSetVizMode('webgl');
        } else {
            vtSetVizMode('2d');
        }
    }

    /* File input listener */
    var fileInput = document.getElementById('vt-file-input');
    if (fileInput) {
        fileInput.addEventListener('change', function() {
            if (fileInput.files && fileInput.files[0]) {
                selectedFile = fileInput.files[0];
                selectedTrackId = null;
                elFileName.textContent = selectedFile.name;
                elAnalyzeBtn.disabled = false;
            }
        });
    }

    /* VFX browse button -> media picker */
    var vfxBrowse = document.getElementById('vfx-browse-btn');
    if (vfxBrowse) {
        vfxBrowse.addEventListener('click', function() {
            mc1MediaPicker.open({
                type: 'audio',
                onSelect: function(track) {
                    var pathInput = document.getElementById('vfx-file-path');
                    if (pathInput) pathInput.value = track.file_path || '';
                }
            });
        });
    }

    checkVtStatus();
    setInterval(checkVtStatus, 10000);

    /* Set initial mode */
    vtSetMode('file');

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
        if (!sessionActive && !fileIsPlaying) drawIdleCanvases();
    }, 200);
});

/* ══════════════════════════════════════════════════════════════════
 * Idle Canvas Drawing -- grid lines + labels even when no data
 * ══════════════════════════════════════════════════════════════════ */
function drawIdleCanvases() {
    drawIdleScope();
    drawIdleSpectrum();
}

function drawIdleScope() {
    if (!elScopeCanvas) return;
    var ctx = elScopeCanvas.getContext('2d');
    var w = elScopeCanvas.width, h = elScopeCanvas.height;
    var dpr = window.devicePixelRatio || 1;
    ctx.clearRect(0, 0, w, h);

    /* Background */
    ctx.fillStyle = '#0a0f1e';
    ctx.fillRect(0, 0, w, h);

    /* Grid lines */
    ctx.strokeStyle = 'rgba(255,255,255,.06)';
    ctx.lineWidth = 1;

    /* Horizontal grid: -1.0, -0.5, 0, +0.5, +1.0 */
    var labels = ['+1.0', '+0.5', '0', '-0.5', '-1.0'];
    for (var i = 0; i < 5; i++) {
        var y = Math.round(h * i / 4) + 0.5;
        ctx.beginPath();
        ctx.moveTo(0, y);
        ctx.lineTo(w, y);
        ctx.stroke();
    }

    /* Center line (brighter) */
    ctx.strokeStyle = 'rgba(20,184,166,.2)';
    ctx.beginPath();
    ctx.moveTo(0, h / 2);
    ctx.lineTo(w, h / 2);
    ctx.stroke();

    /* Vertical grid: time markers */
    ctx.strokeStyle = 'rgba(255,255,255,.04)';
    for (var x = 0; x < w; x += Math.round(w / 8)) {
        ctx.beginPath();
        ctx.moveTo(x + 0.5, 0);
        ctx.lineTo(x + 0.5, h);
        ctx.stroke();
    }

    /* Labels */
    ctx.fillStyle = 'rgba(255,255,255,.25)';
    ctx.font = (9 * dpr) + 'px monospace';
    for (var j = 0; j < labels.length; j++) {
        ctx.fillText(labels[j], 4 * dpr, h * j / 4 + 12 * dpr);
    }
}

function drawIdleSpectrum() {
    if (!elSpectrumCanvas) return;
    var ctx = elSpectrumCanvas.getContext('2d');
    var w = elSpectrumCanvas.width, h = elSpectrumCanvas.height;
    var dpr = window.devicePixelRatio || 1;
    ctx.clearRect(0, 0, w, h);

    /* Background */
    ctx.fillStyle = '#0a0f1e';
    ctx.fillRect(0, 0, w, h);

    /* Horizontal grid: dB markers */
    ctx.strokeStyle = 'rgba(255,255,255,.06)';
    ctx.lineWidth = 1;
    var dbLabels = ['0 dB', '-12', '-24', '-36', '-48', '-60'];
    for (var i = 0; i < dbLabels.length; i++) {
        var y = Math.round(h * i / (dbLabels.length - 1)) + 0.5;
        ctx.beginPath();
        ctx.moveTo(0, y);
        ctx.lineTo(w, y);
        ctx.stroke();
    }
    ctx.fillStyle = 'rgba(255,255,255,.2)';
    ctx.font = (8 * dpr) + 'px monospace';
    for (var di = 0; di < dbLabels.length; di++) {
        ctx.fillText(dbLabels[di], 4 * dpr, h * di / (dbLabels.length - 1) + 10 * dpr);
    }

    /* Frequency labels at bottom (log scale) */
    var freqs = [
        {hz: 20, label: '20'},
        {hz: 100, label: '100'},
        {hz: 1000, label: '1k'},
        {hz: 10000, label: '10k'},
        {hz: 20000, label: '20k'}
    ];
    var sr = 48000;
    ctx.fillStyle = 'rgba(255,255,255,.25)';
    ctx.font = (9 * dpr) + 'px monospace';
    var logMin = Math.log10(20);
    var logMax = Math.log10(sr / 2);
    for (var fi = 0; fi < freqs.length; fi++) {
        var logF = Math.log10(freqs[fi].hz);
        var xPos = ((logF - logMin) / (logMax - logMin)) * w;
        /* Vertical line */
        ctx.strokeStyle = 'rgba(255,255,255,.06)';
        ctx.beginPath();
        ctx.moveTo(Math.round(xPos) + 0.5, 0);
        ctx.lineTo(Math.round(xPos) + 0.5, h);
        ctx.stroke();
        /* Label */
        ctx.fillStyle = 'rgba(255,255,255,.3)';
        ctx.fillText(freqs[fi].label, xPos + 2 * dpr, h - 4 * dpr);
    }
}

/* ══════════════════════════════════════════════════════════════════
 * VoicTune daemon health check
 * ══════════════════════════════════════════════════════════════════ */
function checkVtStatus() {
    vtApi('GET', '/api/v1/voictune/health').then(function(d) {
        if (d && d.ok) {
            vtOnline = true;
            elConnDot.className = 'vt-conn-dot online';
            elConnLabel.textContent = 'VoicTune Online (v' + (d.version || '?') + ')';
            elConnLabel.style.color = '#22c55e';

            if (!sessionActive && d.session_active) {
                sessionActive = true;
                sessionStart = Date.now() - ((d.session_elapsed_sec || 0) * 1000);
                elStartBtn.disabled = true;
                elStopBtn.disabled = false;
                elSessionLabel.textContent = 'Active';
                elSessionLabel.style.color = '#22c55e';
                startPolling();
            }

            if (d.ws_clients > 0) {
                elWsBadge.style.display = '';
                elWsBadge.textContent = 'WS: ' + d.ws_clients;
            }

            /* Load devices if in server mode */
            if (currentMode === 'server') loadDevices();
        } else {
            setOffline();
        }
    }).catch(function() {
        setOffline();
    });
}

function setOffline() {
    vtOnline = false;
    elConnDot.className = 'vt-conn-dot offline';
    elConnLabel.textContent = 'VoicTune Offline';
    elConnLabel.style.color = '#ef4444';
}

function updateAudioStatus(msg, hasInput) {
    if (!elAudioStatus) return;
    var icon = hasInput ? 'fa-volume-high' : 'fa-volume-xmark';
    var color = hasInput ? 'var(--teal)' : 'var(--muted)';
    elAudioStatus.innerHTML = '<i class="fa-solid ' + icon + '" style="color:' + color + '"></i> <span>' + esc(msg) + '</span>';
}

/* ══════════════════════════════════════════════════════════════════
 * Mode Switching
 * ══════════════════════════════════════════════════════════════════ */
window.vtSetMode = function(mode) {
    currentMode = mode;
    /* Update mode buttons */
    ['file', 'browser', 'server', 'demo'].forEach(function(m) {
        var btn = document.getElementById('vt-mode-' + m);
        if (btn) btn.className = 'vt-mode-btn' + (m === mode ? ' active' : '');
    });

    /* Show/hide bars */
    elFileBar.className = 'vt-cmd-row vt-file-bar' + (mode === 'file' ? ' visible' : '');
    elDeviceBar.className = 'vt-cmd-row vt-device-bar' + ((mode === 'browser' || mode === 'server') ? ' visible' : '');

    /* Update Start button label */
    if (mode === 'file') {
        elStartBtn.innerHTML = '<i class="fa-solid fa-play"></i> Start Session';
        updateAudioStatus('File Analysis Mode', false);
    } else if (mode === 'browser') {
        elStartBtn.innerHTML = '<i class="fa-solid fa-microphone"></i> Start Session';
        updateAudioStatus('Browser Mic', false);
        vtAudioEnumerateBrowserDevices(elDeviceSelect);
    } else if (mode === 'server') {
        elStartBtn.innerHTML = '<i class="fa-solid fa-play"></i> Start Session';
        updateAudioStatus(vtOnline ? 'Server Mic' : 'Server Offline', vtOnline);
        loadDevices();
    } else if (mode === 'demo') {
        elStartBtn.innerHTML = '<i class="fa-solid fa-play-circle"></i> Run Demo';
        updateAudioStatus('Demo Mode', false);
    }

    /* Stop file playback if switching away from file mode */
    if (mode !== 'file' && fileIsPlaying) {
        stopFileAnalysis();
    }
};

/* ── Load devices from VoicTune daemon ───────────────────────── */
function loadDevices() {
    vtApi('GET', '/api/v1/voictune/devices').then(function(d) {
        if (!d || !d.ok) return;
        elDeviceSelect.innerHTML = '';
        var inputs = d.inputs || [];
        if (inputs.length === 0) {
            elDeviceSelect.innerHTML = '<option value="-1">No input devices</option>';
            updateAudioStatus('No Input Devices', false);
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
        updateAudioStatus(inputs.length + ' device(s) available', true);
    }).catch(function() {
        elDeviceSelect.innerHTML = '<option value="-1">VoicTune offline</option>';
        updateAudioStatus('VoicTune Offline', false);
    });
}

/* ══════════════════════════════════════════════════════════════════
 * File Analysis Mode -- Web Audio API (works without daemon)
 * ══════════════════════════════════════════════════════════════════ */

/* Open media library picker */
window.vtPickMediaFile = function() {
    mc1MediaPicker.open({
        type: 'audio',
        onSelect: function(track) {
            selectedFile = null;
            selectedTrackId = track.id;
            elFileName.textContent = (track.title || track.file_path || 'Unknown');
            elAnalyzeBtn.disabled = false;
        }
    });
};

/* Analyze the selected file */
window.vtAnalyzeFile = function() {
    if (!selectedFile && !selectedTrackId) {
        mc1Toast('Select a file first', 'warn');
        return;
    }

    elAnalyzeBtn.disabled = true;
    elFileProgress.className = 'vt-file-progress visible';
    elFileProgressText.textContent = 'Loading audio...';

    var loadPromise;

    if (selectedFile instanceof File) {
        /* Local file upload -- decode via Web Audio API */
        loadPromise = selectedFile.arrayBuffer();
    } else if (selectedTrackId) {
        /* Media library track -- fetch via audio API */
        loadPromise = fetch('/app/api/audio.php?id=' + selectedTrackId, {credentials: 'include'})
            .then(function(r) {
                if (!r.ok) throw new Error('Fetch failed: ' + r.status);
                return r.arrayBuffer();
            });
    } else {
        elAnalyzeBtn.disabled = false;
        elFileProgress.className = 'vt-file-progress';
        return;
    }

    loadPromise.then(function(arrayBuf) {
        elFileProgressText.textContent = 'Decoding...';
        if (!fileAudioCtx) {
            fileAudioCtx = new (window.AudioContext || window.webkitAudioContext)({sampleRate: 48000});
        }
        return fileAudioCtx.decodeAudioData(arrayBuf);
    }).then(function(buffer) {
        fileAudioBuffer = buffer;
        elFileProgressText.textContent = 'Analyzing...';
        startFileAnalysis(buffer);
    }).catch(function(err) {
        elAnalyzeBtn.disabled = false;
        elFileProgress.className = 'vt-file-progress';
        mc1Toast('Failed to decode audio: ' + (err.message || err), 'err');
    });
};

function startFileAnalysis(buffer) {
    if (fileIsPlaying) stopFileAnalysis();

    /* Create audio graph: source -> analyser -> destination */
    fileSourceNode = fileAudioCtx.createBufferSource();
    fileSourceNode.buffer = buffer;

    fileAnalyser = fileAudioCtx.createAnalyser();
    fileAnalyser.fftSize = 4096;
    fileAnalyser.smoothingTimeConstant = 0.8;

    fileSourceNode.connect(fileAnalyser);
    fileAnalyser.connect(fileAudioCtx.destination);

    fileIsPlaying = true;
    sessionActive = true;
    sessionStart = Date.now();
    elStartBtn.disabled = true;
    elStopBtn.disabled = false;
    elSessionLabel.textContent = 'Analyzing';
    elSessionLabel.style.color = 'var(--teal)';
    elIdleMsg.style.display = 'none';

    /* Start session timer */
    sessionTimer = setInterval(updateSessionTimer, 1000);

    /* Hide progress, show duration info */
    var dur = buffer.duration;
    elFileProgress.className = 'vt-file-progress visible';
    elFileProgressText.textContent = 'Playing (' + formatDuration(dur) + ')';
    updateAudioStatus('Analyzing: ' + formatDuration(dur), true);

    fileSourceNode.start(0);

    /* When playback ends, generate coaching tips */
    fileSourceNode.onended = function() {
        elFileProgressText.textContent = 'Analysis complete';
        generateFileCoachingTips();
    };

    /* Start rAF-driven local analysis loop */
    analyzeFileFrame();
}

function analyzeFileFrame() {
    if (!fileIsPlaying || !fileAnalyser) return;

    /* Read time-domain data for oscilloscope */
    var timeDomain = new Float32Array(fileAnalyser.fftSize);
    fileAnalyser.getFloatTimeDomainData(timeDomain);
    latestWaveform = Array.from(timeDomain.slice(0, 1024));

    /* Read frequency data for spectrum */
    var freqData = new Float32Array(fileAnalyser.frequencyBinCount);
    fileAnalyser.getFloatFrequencyData(freqData);
    /* Convert to magnitude 0..1 (from dB -100..0) */
    var bins = [];
    for (var i = 0; i < freqData.length; i++) {
        bins.push(Math.max(0, (freqData[i] + 100) / 100));
    }
    latestSpectrum = bins;

    /* Compute RMS, Peak from time domain */
    var rmsSum = 0, peak = 0;
    for (var s = 0; s < timeDomain.length; s++) {
        var sample = timeDomain[s];
        rmsSum += sample * sample;
        var abs = Math.abs(sample);
        if (abs > peak) peak = abs;
    }
    var rmsDb = 20 * Math.log10(Math.sqrt(rmsSum / timeDomain.length) || 0.000001);
    var peakDb = 20 * Math.log10(peak || 0.000001);

    /* Simple pitch detection via autocorrelation on time domain */
    var pitchResult = detectPitch(timeDomain, fileAudioCtx.sampleRate);

    latestMeters = {
        rms_db: Math.max(-96, rmsDb),
        peak_db: Math.max(-96, peakDb),
        peak_hold_db: Math.max(-96, peakDb),
        lufs: Math.max(-96, rmsDb - 0.691), /* rough LUFS approximation */
        pitch_hz: pitchResult.hz,
        note: pitchResult.note,
        cents: pitchResult.cents,
        confidence: pitchResult.confidence
    };

    fileAnimFrame = requestAnimationFrame(analyzeFileFrame);
}

/* Simple autocorrelation pitch detection */
function detectPitch(buf, sampleRate) {
    var SIZE = buf.length;
    var maxSamples = Math.floor(SIZE / 2);
    var bestOffset = -1, bestCorrelation = 0;
    var rms = 0;
    for (var i = 0; i < SIZE; i++) rms += buf[i] * buf[i];
    rms = Math.sqrt(rms / SIZE);
    if (rms < 0.01) return {hz: 0, note: '--', cents: 0, confidence: 0};

    var correlations = new Float32Array(maxSamples);
    for (var offset = 50; offset < maxSamples; offset++) {
        var correlation = 0;
        for (var j = 0; j < maxSamples; j++) {
            correlation += Math.abs(buf[j] - buf[j + offset]);
        }
        correlation = 1 - (correlation / maxSamples);
        correlations[offset] = correlation;
        if (correlation > bestCorrelation) {
            bestCorrelation = correlation;
            bestOffset = offset;
        }
    }

    if (bestCorrelation < 0.6 || bestOffset < 1) {
        return {hz: 0, note: '--', cents: 0, confidence: 0};
    }

    var hz = sampleRate / bestOffset;
    /* Clamp to human voice range */
    if (hz < 60 || hz > 1200) return {hz: 0, note: '--', cents: 0, confidence: 0};

    var noteNum = 12 * (Math.log2(hz / 440));
    var noteIdx = Math.round(noteNum) + 69;
    var noteNames = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'];
    var noteName = noteNames[noteIdx % 12] + Math.floor(noteIdx / 12 - 1);
    var cents = Math.round((noteNum - Math.round(noteNum)) * 100);

    return {
        hz: Math.round(hz * 10) / 10,
        note: noteName,
        cents: cents,
        confidence: Math.min(1, bestCorrelation)
    };
}

function stopFileAnalysis() {
    if (fileSourceNode) {
        try { fileSourceNode.stop(); } catch(e) {}
        fileSourceNode.disconnect();
        fileSourceNode = null;
    }
    if (fileAnalyser) {
        fileAnalyser.disconnect();
        fileAnalyser = null;
    }
    if (fileAnimFrame) {
        cancelAnimationFrame(fileAnimFrame);
        fileAnimFrame = null;
    }
    fileIsPlaying = false;
    elFileProgress.className = 'vt-file-progress';
    updateAudioStatus('File Analysis Mode', false);
}

function generateFileCoachingTips() {
    if (!latestMeters) return;
    var m = latestMeters;
    var tips = [];

    /* Generate rule-based coaching tips from the analysis */
    if (m.rms_db < -40) {
        tips.push({severity: 'warning', message: 'Low signal level detected',
            suggestion: 'Average RMS is ' + m.rms_db.toFixed(1) + ' dB. Move closer to the microphone or increase gain.'});
    } else if (m.rms_db > -6) {
        tips.push({severity: 'critical', message: 'Signal is very hot',
            suggestion: 'RMS at ' + m.rms_db.toFixed(1) + ' dB. Risk of clipping. Reduce gain or move back from the mic.'});
    } else {
        tips.push({severity: 'info', message: 'Good signal level',
            suggestion: 'RMS at ' + m.rms_db.toFixed(1) + ' dB -- within broadcast range (-24 to -12 dB is ideal).'});
    }

    if (m.peak_db > -1) {
        tips.push({severity: 'critical', message: 'Clipping detected',
            suggestion: 'Peak at ' + m.peak_db.toFixed(1) + ' dB. Reduce input level to prevent digital distortion.'});
    }

    if (m.lufs && m.lufs > -14) {
        tips.push({severity: 'warning', message: 'Loudness exceeds broadcast standard',
            suggestion: 'LUFS at ' + m.lufs.toFixed(1) + '. Target is -16 LUFS for podcast, -14 for streaming.'});
    }

    if (m.pitch_hz > 0 && m.confidence > 0.5) {
        tips.push({severity: 'info', message: 'Detected pitch: ' + m.note + ' (' + m.pitch_hz + ' Hz)',
            suggestion: 'Pitch confidence: ' + Math.round(m.confidence * 100) + '%. Cents offset: ' + m.cents + 'c.'});

        if (m.pitch_hz < 100) {
            tips.push({severity: 'suggestion', message: 'Low-pitched voice detected',
                suggestion: 'Consider boosting presence around 2-5 kHz for clarity.'});
        } else if (m.pitch_hz > 300) {
            tips.push({severity: 'suggestion', message: 'Higher-pitched voice detected',
                suggestion: 'A mild cut around 2-4 kHz can reduce harshness; boost low-mids (200-400 Hz) for warmth.'});
        }
    }

    if (tips.length === 0) {
        tips.push({severity: 'info', message: 'Analysis complete', suggestion: 'No specific issues detected.'});
    }

    renderTips(tips);

    /* Also try AI coaching if VoicTune daemon is available */
    if (vtOnline) {
        vtApi('POST', '/api/v1/ai/coaching', {
            meters: latestMeters,
            context: 'file_analysis'
        }).then(function(d) {
            if (d && d.ok && d.response && d.response.message) {
                var content = d.response.message.content || '';
                tips.push({severity: 'suggestion', message: 'AI Coach Analysis', suggestion: content});
                renderTips(tips);
            }
        }).catch(function(){});
    }
}

function formatDuration(sec) {
    var m = Math.floor(sec / 60);
    var s = Math.floor(sec % 60);
    return m + ':' + (s < 10 ? '0' : '') + s;
}

/* ══════════════════════════════════════════════════════════════════
 * Demo Mode -- generate synthetic audio for showcase
 * ══════════════════════════════════════════════════════════════════ */
function startDemo() {
    if (fileIsPlaying) stopFileAnalysis();

    if (!fileAudioCtx) {
        fileAudioCtx = new (window.AudioContext || window.webkitAudioContext)({sampleRate: 48000});
    }

    /* Generate 15 seconds of a synthetic voice-like signal */
    var sr = fileAudioCtx.sampleRate;
    var dur = 15;
    var buffer = fileAudioCtx.createBuffer(1, sr * dur, sr);
    var data = buffer.getChannelData(0);

    /* Synthesize a rich tone that changes pitch (simulating speech) */
    var phase = 0;
    for (var i = 0; i < data.length; i++) {
        var t = i / sr;
        /* Base pitch modulation: varies between ~100 Hz and ~220 Hz */
        var pitchMod = 150 + 50 * Math.sin(2 * Math.PI * 0.3 * t) + 20 * Math.sin(2 * Math.PI * 1.7 * t);
        /* Amplitude modulation (simulating syllables) */
        var ampMod = 0.3 + 0.2 * Math.sin(2 * Math.PI * 3.5 * t) * Math.max(0, Math.sin(2 * Math.PI * 0.8 * t));
        /* Harmonics */
        phase += 2 * Math.PI * pitchMod / sr;
        var sample = Math.sin(phase) * 0.6
                   + Math.sin(phase * 2) * 0.25
                   + Math.sin(phase * 3) * 0.12
                   + Math.sin(phase * 5) * 0.04;
        /* Add subtle noise for breathiness */
        sample += (Math.random() * 2 - 1) * 0.02;
        data[i] = sample * ampMod * 0.5;
    }

    startFileAnalysis(buffer);
    mc1Toast('Demo started -- synthetic voice signal (15s)', 'ok');
}

/* ══════════════════════════════════════════════════════════════════
 * Session controls
 * ══════════════════════════════════════════════════════════════════ */
window.vtStartSession = function() {
    if (currentMode === 'file') {
        vtAnalyzeFile();
        return;
    }

    if (currentMode === 'demo') {
        startDemo();
        return;
    }

    /* Server or Browser mic modes */
    if (currentMode === 'server' && !vtOnline) {
        mc1Toast('VoicTune daemon is offline. Try Browser Mic or Analyze File mode.', 'warn');
        return;
    }

    var devIdx = parseInt(elDeviceSelect.value);
    if (devIdx >= 0 && currentMode === 'server') {
        vtApi('PUT', '/api/v1/voictune/device', {device_index: devIdx});
    }

    vtApi('POST', '/api/v1/voictune/session/start', {name: 'VoicTune Session'}).then(function(d) {
        if (d && d.ok) {
            sessionActive = true;
            sessionStart = Date.now();
            elStartBtn.disabled = true;
            elStopBtn.disabled = false;
            elSessionLabel.textContent = 'Active';
            elSessionLabel.style.color = '#22c55e';
            elIdleMsg.style.display = 'none';
            mc1Toast('Session started', 'ok');
            startPolling();
        } else {
            mc1Toast('Session start failed: ' + (d && d.error ? d.error : 'unknown'), 'err');
        }
    }).catch(function() {
        mc1Toast('VoicTune daemon unreachable. Try Browser Mic or Analyze File mode.', 'err');
    });

    if (currentMode === 'browser') {
        vtAudioStart(devIdx);
        updateAudioStatus('Browser Mic Active', true);
    }
};

window.vtStopSession = function() {
    if (currentMode === 'file' || currentMode === 'demo') {
        stopFileAnalysis();
        sessionActive = false;
        elStartBtn.disabled = false;
        elStopBtn.disabled = true;
        elSessionLabel.textContent = 'Stopped';
        elSessionLabel.style.color = 'var(--muted)';
        if (sessionTimer) { clearInterval(sessionTimer); sessionTimer = null; }
        latestMeters = null;
        latestWaveform = null;
        latestSpectrum = null;
        drawIdleCanvases();
        mc1Toast('Analysis stopped', 'ok');
        return;
    }

    vtApi('POST', '/api/v1/voictune/session/stop').then(function(d) {
        sessionActive = false;
        elStartBtn.disabled = false;
        elStopBtn.disabled = true;
        elSessionLabel.textContent = 'Stopped';
        elSessionLabel.style.color = 'var(--muted)';
        stopPolling();
        mc1Toast('Session stopped', 'ok');
    }).catch(function() {
        mc1Toast('Failed to stop session', 'err');
    });

    if (currentMode === 'browser') {
        vtAudioStop();
        updateAudioStatus('Browser Mic', false);
    }
};

/* ── WebGL / 2D toggle ──────────────────────────────────────── */
window.vtSetVizMode = function(mode) {
    vizMode = mode;
    var btn2d   = document.getElementById('vt-viz-2d');
    var btnGL   = document.getElementById('vt-viz-webgl');
    if (btn2d) btn2d.className = 'vt-source-btn' + (mode === '2d' ? ' active' : '');
    if (btnGL) btnGL.className = 'vt-source-btn' + (mode === 'webgl' ? ' active' : '');

    if (mode === 'webgl' && window.WebGLViz && WebGLViz.isWebGLAvailable()) {
        WebGLViz.setWebGLPref(true);
        if (!glSpectrum && elSpectrumCanvas) {
            sizeCanvas(elSpectrumCanvas);
            glSpectrum = new WebGLViz.Spectrum3D(elSpectrumCanvas);
        }
        if (!glWaveform && elScopeCanvas) {
            sizeCanvas(elScopeCanvas);
            glWaveform = new WebGLViz.Waveform(elScopeCanvas);
        }
        if (!glRmsMeter && elRmsCanvas) {
            glRmsMeter = new WebGLViz.VUMeter(elRmsCanvas);
        }
        if (!glPeakMeter && elPeakCanvas) {
            glPeakMeter = new WebGLViz.VUMeter(elPeakCanvas);
        }
    } else {
        if (window.WebGLViz) WebGLViz.setWebGLPref(false);
        if (glSpectrum) { glSpectrum.destroy(); glSpectrum = null; }
        if (glWaveform) { glWaveform.destroy(); glWaveform = null; }
        if (glRmsMeter) { glRmsMeter.destroy(); glRmsMeter = null; }
        if (glPeakMeter) { glPeakMeter.destroy(); glPeakMeter = null; }
        sizeCanvas(elScopeCanvas);
        sizeCanvas(elSpectrumCanvas);
        if (!sessionActive && !fileIsPlaying) drawIdleCanvases();
    }
};

/* ── Data polling (daemon modes) ────────────────────────────── */
function startPolling() {
    if (pollTimer) return;
    pollTimer    = setInterval(pollData, POLL_MS);
    tipsPollTimer = setInterval(pollTips, TIPS_POLL_MS);
    if (!sessionTimer) sessionTimer = setInterval(updateSessionTimer, 1000);
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
    if (elIdleMsg) elIdleMsg.style.display = 'none';
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

        /* Meters -- WebGL LED or Canvas 2D */
        if (vizMode === 'webgl' && glRmsMeter && glPeakMeter) {
            var rmsNorm = Math.max(0, Math.min(1, ((m.rms_db || -96) + 60) / 60));
            var peakNorm = Math.max(0, Math.min(1, ((m.peak_db || -96) + 60) / 60));
            var peakHoldNorm = Math.max(0, Math.min(1, ((m.peak_hold_db || -96) + 60) / 60));
            glRmsMeter.setLevel(rmsNorm, peakHoldNorm);
            glRmsMeter.draw();
            glPeakMeter.setLevel(peakNorm, peakHoldNorm);
            glPeakMeter.draw();
        } else {
            viz.drawMeter(elRmsCanvas, m.rms_db || -96, m.peak_hold_db || -96, 'RMS');
            viz.drawMeter(elPeakCanvas, m.peak_db || -96, m.peak_hold_db || -96, 'Peak');
        }
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
        elPitchNote.style.color = conf > 0.5 ? 'var(--teal)' : 'var(--muted)';
    }

    if (latestWaveform) {
        if (vizMode === 'webgl' && glWaveform) {
            glWaveform.setData(latestWaveform);
            glWaveform.draw();
        } else if (viz) {
            viz.drawOscilloscope(elScopeCanvas, latestWaveform);
        }
    }
    if (latestSpectrum) {
        if (vizMode === 'webgl' && glSpectrum) {
            glSpectrum.update(latestSpectrum, 48000);
            glSpectrum.draw();
        } else if (viz) {
            viz.drawSpectrum(elSpectrumCanvas, latestSpectrum, 48000);
        }
    }

    requestAnimationFrame(renderLoop);
}

/* ══════════════════════════════════════════════════════════════
 * Voice Effects (VFX) Panel
 * ══════════════════════════════════════════════════════════════ */

/* Tab switching */
window.vfxTab = function(tab) {
    var tabs = document.querySelectorAll('[data-vfx-tab]');
    tabs.forEach(function(t) {
        t.className = 'vt-source-btn' + (t.getAttribute('data-vfx-tab') === tab ? ' active' : '');
    });
    var panels = document.querySelectorAll('.vfx-panel');
    panels.forEach(function(p) { p.style.display = 'none'; });
    var panel = document.getElementById('vfx-panel-' + tab);
    if (panel) panel.style.display = '';
};

/* Slider value display helpers */
function vfxBindSlider(id, valId, suffix) {
    var el = document.getElementById(id);
    var valEl = document.getElementById(valId);
    if (el && valEl) {
        el.addEventListener('input', function() {
            valEl.textContent = el.value + (suffix || '');
        });
    }
}

document.addEventListener('DOMContentLoaded', function() {
    /* Bind all slider displays */
    vfxBindSlider('vfx-db-threshold', 'vfx-db-threshold-val', ' dB');
    vfxBindSlider('vfx-db-reduction', 'vfx-db-reduction-val', ' dB');
    vfxBindSlider('vfx-db-minms', 'vfx-db-minms-val', ' ms');
    vfxBindSlider('vfx-vc-intensity', 'vfx-vc-intensity-val', '%');
    vfxBindSlider('vfx-at-strength', 'vfx-at-strength-val', '%');
    vfxBindSlider('vfx-at-speed', 'vfx-at-speed-val', ' ms');
    vfxBindSlider('vfx-ng-threshold', 'vfx-ng-threshold-val', ' dB');
    vfxBindSlider('vfx-ng-attack', 'vfx-ng-attack-val', ' ms');
    vfxBindSlider('vfx-ng-release', 'vfx-ng-release-val', ' ms');
    vfxBindSlider('vfx-ng-hold', 'vfx-ng-hold-val', ' ms');
    vfxBindSlider('vfx-de-freq', 'vfx-de-freq-val', ' Hz');
    vfxBindSlider('vfx-de-threshold', 'vfx-de-threshold-val', ' dB');

    /* De-esser ratio slider needs :1 suffix */
    var deRatio = document.getElementById('vfx-de-ratio');
    var deRatioVal = document.getElementById('vfx-de-ratio-val');
    if (deRatio && deRatioVal) {
        deRatio.addEventListener('input', function() {
            deRatioVal.textContent = deRatio.value + ':1';
        });
    }
});

/* Process voice effect via VoicTune daemon */
window.vfxProcess = function(effectType, preview) {
    var filePath = (document.getElementById('vfx-file-path') || {}).value || '';
    if (!filePath.trim()) {
        mc1Toast('Enter an audio file path first', 'warn');
        return;
    }

    if (!vtOnline) {
        mc1Toast('VoicTune daemon is offline -- voice effects require the daemon', 'err');
        return;
    }

    var statusEl = document.getElementById('vfx-status');
    var resultEl = document.getElementById('vfx-result');
    resultEl.style.display = 'none';
    statusEl.textContent = 'Processing...';
    statusEl.style.color = 'var(--yellow)';

    var payload = { file_path: filePath.trim(), preview: !!preview };
    var endpoint = '';

    if (effectType === 'de-breath') {
        endpoint = '/api/v1/voictune/process/de-breath';
        payload.threshold_db = parseFloat(document.getElementById('vfx-db-threshold').value);
        payload.max_reduction_db = parseFloat(document.getElementById('vfx-db-reduction').value);
        payload.min_breath_ms = parseInt(document.getElementById('vfx-db-minms').value);
    } else if (effectType === 'voice-change') {
        endpoint = '/api/v1/voictune/process/voice-change';
        payload.effect = document.getElementById('vfx-vc-effect').value;
        payload.intensity = parseFloat(document.getElementById('vfx-vc-intensity').value) / 100.0;
    } else if (effectType === 'auto-tune') {
        endpoint = '/api/v1/voictune/process/auto-tune';
        payload.key = document.getElementById('vfx-at-key').value;
        payload.scale = document.getElementById('vfx-at-scale').value;
        payload.correction_strength = parseFloat(document.getElementById('vfx-at-strength').value) / 100.0;
        payload.speed_ms = parseFloat(document.getElementById('vfx-at-speed').value);
    } else if (effectType === 'noise-gate') {
        endpoint = '/api/v1/voictune/process/noise-gate';
        payload.threshold_db = parseFloat(document.getElementById('vfx-ng-threshold').value);
        payload.attack_ms = parseFloat(document.getElementById('vfx-ng-attack').value);
        payload.release_ms = parseFloat(document.getElementById('vfx-ng-release').value);
        payload.hold_ms = parseFloat(document.getElementById('vfx-ng-hold').value);
    } else if (effectType === 'de-esser') {
        endpoint = '/api/v1/voictune/process/de-esser';
        payload.frequency_hz = parseFloat(document.getElementById('vfx-de-freq').value);
        payload.threshold_db = parseFloat(document.getElementById('vfx-de-threshold').value);
        payload.ratio = parseFloat(document.getElementById('vfx-de-ratio').value);
    }

    vtApi('POST', endpoint, payload).then(function(d) {
        if (d && d.ok) {
            statusEl.textContent = 'Done';
            statusEl.style.color = 'var(--green)';
            resultEl.style.display = '';
            document.getElementById('vfx-result-text').textContent =
                effectType.replace(/-/g, ' ').replace(/\b\w/g, function(c) { return c.toUpperCase(); }) +
                (preview ? ' preview' : '') + ' applied successfully.';
            document.getElementById('vfx-result-path').textContent = d.file_path_processed || '';
            document.getElementById('vfx-result-time').textContent =
                d.latency_ms ? ('Processed in ' + (d.latency_ms / 1000).toFixed(1) + 's') : '';
            mc1Toast(effectType + (preview ? ' preview' : '') + ' complete', 'ok');
        } else {
            statusEl.textContent = 'Error';
            statusEl.style.color = 'var(--red)';
            mc1Toast('Voice effect failed: ' + (d && d.error ? d.error : 'Unknown'), 'err');
        }
    }).catch(function() {
        statusEl.textContent = 'Error';
        statusEl.style.color = 'var(--red)';
        mc1Toast('VoicTune daemon unreachable for voice effects', 'err');
    });
};

/* ── AI Coach button ─────────────────────────────────────────── */
document.addEventListener('DOMContentLoaded', function() {
    var aiBtn = document.getElementById('vt-ai-coach-btn');
    if (aiBtn) {
        aiBtn.addEventListener('click', function() {
            if (!latestMeters) {
                mc1Toast('No meter data available. Start a session or analyze a file first.', 'warn');
                return;
            }

            if (!vtOnline) {
                mc1Toast('AI Coach requires the VoicTune daemon (Ollama). Daemon is currently offline.', 'warn');
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
                    renderTips([{severity:'suggestion', message:'AI Coach Advice', suggestion: content}]);
                } else if (d && d.error) {
                    mc1Toast('AI Coach: ' + d.error, 'err');
                }
            }).catch(function() {
                aiBtn.disabled = false;
                aiBtn.innerHTML = '<i class="fa-solid fa-wand-magic-sparkles"></i> AI Coach';
                mc1Toast('AI Coach unavailable -- is Ollama running on the server?', 'err');
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

        if (!vtOnline) {
            mc1Toast('AI requires VoicTune daemon (Ollama). Daemon is offline.', 'warn');
            return;
        }

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
            mc1Toast('AI service unreachable -- is Ollama running?', 'err');
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
            if (!vtOnline) {
                mc1Toast('Content analysis requires VoicTune daemon (Ollama). Daemon is offline.', 'warn');
                return;
            }

            var transcript = (document.getElementById('vt-content-transcript') || {}).value || '';
            var payload = {};
            if (transcript.trim()) {
                payload.transcript = transcript.trim();
            } else {
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

                var results = document.getElementById('vt-content-results');
                results.style.display = '';

                document.getElementById('vt-ca-summary').textContent = d.summary || 'No summary available';
                document.getElementById('vt-ca-title').textContent = d.title_suggestion || '--';

                var topicsEl = document.getElementById('vt-ca-topics');
                var topics = d.topics || [];
                topicsEl.innerHTML = topics.length
                    ? topics.map(function(t) {
                        return '<span style="padding:2px 8px;border-radius:10px;background:rgba(20,184,166,.15);color:var(--teal);font-size:11px">' + esc(t) + '</span>';
                    }).join('')
                    : '<span style="color:var(--muted);font-size:12px">None detected</span>';

                var tagsEl = document.getElementById('vt-ca-tags');
                var tags = d.tags || [];
                tagsEl.innerHTML = tags.length
                    ? tags.map(function(t) {
                        return '<span style="padding:2px 8px;border-radius:10px;background:rgba(139,92,246,.15);color:#a78bfa;font-size:11px">' + esc(t) + '</span>';
                    }).join('')
                    : '<span style="color:var(--muted);font-size:12px">None detected</span>';

                var paceEl = document.getElementById('vt-ca-pace');
                var pace = d.pace_analysis || {};
                if (pace.avg_wpm) {
                    paceEl.innerHTML = '<strong>' + pace.avg_wpm + ' WPM</strong> avg'
                        + (pace.variance ? ' (variance: ' + pace.variance + ')' : '')
                        + (pace.assessment ? '<br>' + esc(pace.assessment) : '');
                } else {
                    paceEl.textContent = 'No pace data available';
                }

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
