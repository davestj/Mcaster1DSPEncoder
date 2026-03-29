<?php
/**
 * remote-guest.php -- Remote Recording Guest Join Page
 *
 * File:    src/linux/web_ui/remote-guest.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Phase:   PC-7
 * Purpose: We provide a public (NO auth required) page for remote podcast
 *          recording guests. They join via invite code, grant microphone access,
 *          record locally via MediaRecorder API, and upload their track.
 *
 * This page is self-contained (no header.php/footer.php) — like the podcast
 * public pages. It serves from /join/{code} via a C++ route.
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We wrap all startup JS in DOMContentLoaded
 *  - We use plain fetch() (not mc1Api — no auth)
 *  - Public page — NO mc1session required
 */
$session_code = $_GET['code'] ?? '';
?>
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Join Recording Session — Mcaster1</title>
<link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.5.1/css/all.min.css" crossorigin="anonymous" referrerpolicy="no-referrer">
<style>
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;font-size:14px;background:#0f172a;color:#e2e8f0;line-height:1.5;min-height:100vh;display:flex;flex-direction:column;align-items:center;justify-content:flex-start}
:root{
  --bg:#0f172a;--card:#1e293b;--border:#334155;
  --teal:#14b8a6;--teal2:#0d9488;
  --text:#e2e8f0;--text-dim:#94a3b8;--muted:#64748b;
  --red:#ef4444;--green:#22c55e;--yellow:#eab308;--orange:#f97316;
  --radius:10px;
}
a{color:var(--teal);text-decoration:none}
button{cursor:pointer;font-family:inherit}
input{font-family:inherit;font-size:14px}

/* Layout */
.rg-container { max-width: 600px; width: 100%; padding: 20px; }
.rg-header { text-align: center; padding: 30px 0 20px; }
.rg-logo { width: 48px; height: 48px; border-radius: 12px; background: linear-gradient(135deg,var(--teal),#0891b2); display: inline-flex; align-items: center; justify-content: center; font-size: 20px; color: #fff; margin-bottom: 12px; }
.rg-title { font-size: 20px; font-weight: 700; color: var(--text); margin-bottom: 4px; }
.rg-sub { font-size: 13px; color: var(--muted); }

/* Card */
.rg-card { background: var(--card); border: 1px solid var(--border); border-radius: var(--radius); padding: 20px; margin-bottom: 16px; }
.rg-card-title { font-size: 14px; font-weight: 700; color: var(--text); margin-bottom: 12px; display: flex; align-items: center; gap: 8px; }
.rg-card-title i { color: var(--teal); }

/* Form */
.rg-form-group { margin-bottom: 14px; }
.rg-form-group label { display: block; font-size: 11px; font-weight: 700; text-transform: uppercase; color: var(--muted); margin-bottom: 4px; letter-spacing: .05em; }
.rg-input { width: 100%; padding: 10px 14px; background: rgba(255,255,255,.05); border: 1px solid var(--border); border-radius: 6px; color: var(--text); font-size: 14px; outline: none; }
.rg-input:focus { border-color: var(--teal); background: rgba(20,184,166,.05); }

/* Button */
.rg-btn { display: inline-flex; align-items: center; gap: 8px; padding: 10px 20px; border-radius: 6px; font-size: 14px; font-weight: 600; border: none; cursor: pointer; transition: all .15s; }
.rg-btn-primary { background: var(--teal); color: #0f172a; }
.rg-btn-primary:hover { background: var(--teal2); }
.rg-btn-secondary { background: rgba(255,255,255,.07); color: var(--text-dim); border: 1px solid var(--border); }
.rg-btn-secondary:hover { background: rgba(255,255,255,.12); }
.rg-btn-danger { background: rgba(239,68,68,.12); color: var(--red); border: 1px solid rgba(239,68,68,.25); }
.rg-btn-danger:hover { background: rgba(239,68,68,.22); }
.rg-btn[disabled] { opacity: .4; cursor: not-allowed; pointer-events: none; }
.rg-btn-block { width: 100%; justify-content: center; }

/* Status badge */
.rg-status { display: inline-flex; align-items: center; gap: 6px; padding: 4px 10px; border-radius: 12px; font-size: 11px; font-weight: 700; text-transform: uppercase; }
.rg-status.waiting { background: rgba(20,184,166,.12); color: var(--teal); }
.rg-status.recording { background: rgba(239,68,68,.12); color: var(--red); }
.rg-status.ended { background: rgba(100,116,139,.12); color: var(--muted); }
.rg-status::before { content: ''; width: 6px; height: 6px; border-radius: 50%; }
.rg-status.waiting::before { background: var(--teal); }
.rg-status.recording::before { background: var(--red); animation: rg-pulse 1s infinite; }
.rg-status.ended::before { background: var(--muted); }
@keyframes rg-pulse { 0%,100%{opacity:1} 50%{opacity:.3} }

/* Level meter */
.rg-meter { margin: 12px 0; }
.rg-meter-canvas { width: 100%; height: 32px; display: block; border-radius: 4px; background: #0a0e14; }

/* Connection indicator */
.rg-conn { display: flex; align-items: center; gap: 8px; font-size: 13px; margin-bottom: 12px; }
.rg-conn-dot { width: 8px; height: 8px; border-radius: 50%; }
.rg-conn-dot.ok { background: var(--green); box-shadow: 0 0 5px var(--green); }
.rg-conn-dot.warn { background: var(--yellow); }
.rg-conn-dot.off { background: var(--muted); }

/* Chat */
.rg-chat { max-height: 250px; overflow-y: auto; margin-bottom: 8px; }
.rg-chat-msg { padding: 3px 0; border-bottom: 1px solid rgba(51,65,85,.3); font-size: 12px; }
.rg-chat-sender { font-weight: 600; color: var(--teal); }
.rg-chat-sender.host { color: var(--orange); }
.rg-chat-system { color: var(--yellow); font-style: italic; }
.rg-chat-input { display: flex; gap: 6px; }
.rg-chat-input input { flex: 1; }

/* Session info */
.rg-session-info { display: flex; align-items: center; gap: 12px; flex-wrap: wrap; margin-bottom: 12px; }
.rg-host-name { font-size: 13px; color: var(--text-dim); }

/* Toast */
.rg-toast { display: none; position: fixed; bottom: 20px; right: 20px; z-index: 9999; background: #1e293b; border: 1px solid #334155; border-radius: 8px; padding: 10px 16px; align-items: center; gap: 10px; box-shadow: 0 4px 20px rgba(0,0,0,.5); font-size: 13px; color: #e2e8f0; max-width: 380px; }

/* Participants mini-list */
.rg-parts { margin: 8px 0; }
.rg-part { display: flex; align-items: center; gap: 6px; font-size: 12px; padding: 3px 0; color: var(--text-dim); }
.rg-part-dot { width: 6px; height: 6px; border-radius: 50%; background: var(--green); }

/* Recording indicator */
.rg-rec-banner { display: none; background: rgba(239,68,68,.1); border: 1px solid rgba(239,68,68,.3); border-radius: var(--radius); padding: 12px 16px; text-align: center; margin-bottom: 12px; }
.rg-rec-banner.active { display: block; }
.rg-rec-timer { font-family: monospace; font-size: 24px; font-weight: 700; color: var(--red); }
.rg-rec-label { font-size: 11px; color: var(--red); font-weight: 700; text-transform: uppercase; letter-spacing: .1em; }
</style>
</head>
<body>
<div class="rg-container">

  <!-- Header -->
  <div class="rg-header">
    <div class="rg-logo"><i class="fa-solid fa-wave-square"></i></div>
    <div class="rg-title">Remote Recording</div>
    <div class="rg-sub">Mcaster1 Podcast Studio</div>
  </div>

  <!-- Join form (shown first) -->
  <div class="rg-card" id="rgJoinCard">
    <div class="rg-card-title"><i class="fa-solid fa-right-to-bracket"></i> Join Session</div>
    <div class="rg-form-group">
      <label>Session Code</label>
      <input type="text" id="rgCode" class="rg-input" placeholder="e.g., ABC12345" maxlength="32" value="<?php echo htmlspecialchars($session_code, ENT_QUOTES|ENT_HTML5, 'UTF-8'); ?>" style="text-transform:uppercase;letter-spacing:2px;font-family:monospace;font-size:18px;text-align:center;">
    </div>
    <div class="rg-form-group">
      <label>Your Name</label>
      <input type="text" id="rgName" class="rg-input" placeholder="Enter your name" maxlength="128">
    </div>
    <button class="rg-btn rg-btn-primary rg-btn-block" id="rgJoinBtn" onclick="rgJoin()">
      <i class="fa-solid fa-microphone"></i> Join Session
    </button>
    <div id="rgJoinError" style="color:var(--red);font-size:12px;margin-top:8px;display:none;"></div>
  </div>

  <!-- Session view (hidden until joined) -->
  <div id="rgSessionView" style="display:none;">

    <!-- Session info -->
    <div class="rg-card">
      <div class="rg-session-info">
        <span class="rg-status" id="rgStatus">waiting</span>
        <span style="font-weight:700;color:var(--text);" id="rgSessionTitle"></span>
        <span class="rg-host-name" id="rgHostName"></span>
      </div>

      <!-- Recording banner -->
      <div class="rg-rec-banner" id="rgRecBanner">
        <div class="rg-rec-label"><i class="fa-solid fa-circle" style="animation:rg-pulse 1s infinite;margin-right:4px;"></i> Recording</div>
        <div class="rg-rec-timer" id="rgRecTimer">00:00:00</div>
      </div>

      <!-- Connection status -->
      <div class="rg-conn">
        <div class="rg-conn-dot" id="rgConnDot"></div>
        <span id="rgConnText">Connecting...</span>
      </div>

      <!-- Audio level meter -->
      <div class="rg-meter">
        <canvas class="rg-meter-canvas" id="rgMeterCanvas" height="32"></canvas>
      </div>

      <!-- Controls -->
      <div style="display:flex;gap:8px;flex-wrap:wrap;margin-top:8px;">
        <button class="rg-btn rg-btn-secondary" id="rgMuteBtn" onclick="rgToggleMute()">
          <i class="fa-solid fa-microphone" id="rgMuteIcon"></i> <span id="rgMuteLabel">Mute</span>
        </button>
        <button class="rg-btn rg-btn-secondary" onclick="rgHandRaise()">
          <i class="fa-solid fa-hand"></i> Raise Hand
        </button>
        <span style="flex:1"></span>
        <button class="rg-btn rg-btn-danger" onclick="rgLeave()">
          <i class="fa-solid fa-right-from-bracket"></i> Leave
        </button>
      </div>
    </div>

    <!-- Participants -->
    <div class="rg-card">
      <div class="rg-card-title"><i class="fa-solid fa-users"></i> Participants</div>
      <div class="rg-parts" id="rgPartList"></div>
    </div>

    <!-- Chat -->
    <div class="rg-card">
      <div class="rg-card-title"><i class="fa-solid fa-comments"></i> Chat</div>
      <div class="rg-chat" id="rgChatMsgs"></div>
      <div class="rg-chat-input">
        <input type="text" id="rgChatInput" class="rg-input" placeholder="Type a message..." onkeydown="if(event.key==='Enter')rgSendChat()">
        <button class="rg-btn rg-btn-primary" onclick="rgSendChat()" style="padding:8px 14px;"><i class="fa-solid fa-paper-plane"></i></button>
      </div>
    </div>
  </div>

</div>

<!-- Toast -->
<div class="rg-toast" id="rg-toast">
  <i id="rg-toast-icon" class="fa-solid fa-circle-check" style="font-size:16px;flex-shrink:0;color:var(--teal)"></i>
  <span id="rg-toast-msg"></span>
</div>

<script>
/* ── Remote Guest JS ────────────────────────────────────────────────────── */

var RG = {
    sessionCode: '',
    participantId: 0,
    sessionId: 0,
    sessionStatus: 'waiting',
    connected: false,
    muted: false,
    recording: false,
    recStartedAt: 0,

    // We track polling and audio state
    heartbeatInterval: null,
    chatPollInterval: null,
    levelInterval: null,
    timerInterval: null,
    lastChatId: 0,

    // We use MediaRecorder for local recording
    mediaStream: null,
    mediaRecorder: null,
    audioContext: null,
    analyser: null,
    recordedChunks: [],
    meterRaf: null,
};

/* ── Toast ── */

var _rgToastTimer = null;
function rgToast(msg, type) {
    var el = document.getElementById('rg-toast');
    var ico = document.getElementById('rg-toast-icon');
    var txt = document.getElementById('rg-toast-msg');
    if (!el) return;
    ico.className = type === 'err' ? 'fa-solid fa-circle-xmark' : type === 'warn' ? 'fa-solid fa-triangle-exclamation' : 'fa-solid fa-circle-check';
    ico.style.color = type === 'err' ? '#ef4444' : type === 'warn' ? '#eab308' : '#14b8a6';
    txt.textContent = msg;
    el.style.display = 'flex';
    if (_rgToastTimer) clearTimeout(_rgToastTimer);
    _rgToastTimer = setTimeout(function(){ el.style.display = 'none'; }, 4000);
}

function esc(s) {
    return String(s).replace(/&/g,'&amp;').replace(/"/g,'&quot;').replace(/'/g,'&#39;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}

function fmtTime(sec) {
    var h = Math.floor(sec / 3600);
    var m = Math.floor((sec % 3600) / 60);
    var s = Math.floor(sec % 60);
    return String(h).padStart(2,'0') + ':' + String(m).padStart(2,'0') + ':' + String(s).padStart(2,'0');
}

/* ── API helper (plain fetch, no auth) ── */

function rgApi(action, data) {
    var payload = Object.assign({action: action}, data || {});
    return fetch('/api/v1/remote/guest', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(payload)
    }).then(function(r) { return r.json(); });
}

/* ── Join session ── */

function rgJoin() {
    var code = document.getElementById('rgCode').value.trim().toUpperCase();
    var name = document.getElementById('rgName').value.trim();
    var errEl = document.getElementById('rgJoinError');

    if (!code) { errEl.textContent = 'Please enter a session code'; errEl.style.display = ''; return; }
    if (!name) { errEl.textContent = 'Please enter your name'; errEl.style.display = ''; return; }
    errEl.style.display = 'none';

    document.getElementById('rgJoinBtn').disabled = true;

    rgApi('join_session', {session_code: code, name: name}).then(function(d) {
        if (d && d.ok) {
            RG.sessionCode    = code;
            RG.participantId  = d.participant_id;
            RG.sessionId      = d.session_id;
            RG.sessionStatus  = d.session_status;

            document.getElementById('rgJoinCard').style.display = 'none';
            document.getElementById('rgSessionView').style.display = '';
            document.getElementById('rgSessionTitle').textContent = d.session_title || 'Recording Session';

            rgRequestMicrophone();
            rgStartPolling();
            rgPollSession(); // We poll immediately to get participants
            rgToast('Joined session!', 'ok');
        } else {
            errEl.textContent = (d && d.error) || 'Failed to join session';
            errEl.style.display = '';
            document.getElementById('rgJoinBtn').disabled = false;
        }
    }).catch(function(e) {
        errEl.textContent = 'Network error: ' + e.message;
        errEl.style.display = '';
        document.getElementById('rgJoinBtn').disabled = false;
    });
}

/* ── Request microphone ── */

function rgRequestMicrophone() {
    if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
        rgToast('Microphone access not supported in this browser', 'err');
        return;
    }

    navigator.mediaDevices.getUserMedia({
        audio: {
            echoCancellation: true,
            noiseSuppression: true,
            autoGainControl: true,
            sampleRate: 48000
        },
        video: false
    }).then(function(stream) {
        RG.mediaStream = stream;
        RG.connected = true;

        // We set up AudioContext for level metering
        RG.audioContext = new (window.AudioContext || window.webkitAudioContext)();
        var source = RG.audioContext.createMediaStreamSource(stream);
        RG.analyser = RG.audioContext.createAnalyser();
        RG.analyser.fftSize = 256;
        source.connect(RG.analyser);

        rgUpdateConnStatus('connected', 'Microphone connected');
        rgDrawMeter();

    }).catch(function(err) {
        rgUpdateConnStatus('error', 'Microphone denied: ' + err.message);
        rgToast('Microphone access denied. Please allow microphone access to participate.', 'err');
    });
}

/* ── Level meter drawing ── */

function rgDrawMeter() {
    var canvas = document.getElementById('rgMeterCanvas');
    if (!canvas || !RG.analyser) {
        RG.meterRaf = requestAnimationFrame(rgDrawMeter);
        return;
    }

    var ctx = canvas.getContext('2d');
    var w = canvas.width = canvas.clientWidth;
    var h = canvas.height = 32;

    ctx.fillStyle = '#0a0e14';
    ctx.fillRect(0, 0, w, h);

    var data = new Uint8Array(RG.analyser.frequencyBinCount);
    RG.analyser.getByteFrequencyData(data);

    // We compute RMS level
    var sum = 0;
    for (var i = 0; i < data.length; i++) sum += (data[i] / 255) * (data[i] / 255);
    var rms = Math.sqrt(sum / data.length);

    // We store for sending to server
    RG._currentLevel = rms;

    if (RG.muted) rms = 0;

    // We draw a bar
    var barW = rms * w;
    var color = rms > 0.85 ? '#ef4444' : rms > 0.65 ? '#eab308' : '#14b8a6';
    ctx.fillStyle = color;
    ctx.fillRect(0, 0, barW, h);

    // We draw frequency bins as overlay
    var binW = w / data.length;
    ctx.fillStyle = 'rgba(255,255,255,.15)';
    for (var j = 0; j < data.length; j++) {
        var bh = (data[j] / 255) * h;
        ctx.fillRect(j * binW, h - bh, binW - 1, bh);
    }

    RG.meterRaf = requestAnimationFrame(rgDrawMeter);
}

/* ── Connection status ── */

function rgUpdateConnStatus(state, text) {
    var dot = document.getElementById('rgConnDot');
    var txt = document.getElementById('rgConnText');
    if (state === 'connected') {
        dot.className = 'rg-conn-dot ok';
        txt.textContent = text || 'Connected';
        txt.style.color = 'var(--green)';
    } else if (state === 'warn') {
        dot.className = 'rg-conn-dot warn';
        txt.textContent = text || 'Reconnecting...';
        txt.style.color = 'var(--yellow)';
    } else {
        dot.className = 'rg-conn-dot off';
        txt.textContent = text || 'Disconnected';
        txt.style.color = 'var(--muted)';
    }
}

/* ── Mute toggle ── */

function rgToggleMute() {
    RG.muted = !RG.muted;
    if (RG.mediaStream) {
        RG.mediaStream.getAudioTracks().forEach(function(t) { t.enabled = !RG.muted; });
    }
    document.getElementById('rgMuteIcon').className = RG.muted ? 'fa-solid fa-microphone-slash' : 'fa-solid fa-microphone';
    document.getElementById('rgMuteLabel').textContent = RG.muted ? 'Unmute' : 'Mute';
    var btn = document.getElementById('rgMuteBtn');
    if (RG.muted) {
        btn.style.background = 'rgba(239,68,68,.12)';
        btn.style.color = 'var(--red)';
        btn.style.borderColor = 'rgba(239,68,68,.25)';
    } else {
        btn.style.background = '';
        btn.style.color = '';
        btn.style.borderColor = '';
    }
}

/* ── Hand raise ── */

function rgHandRaise() {
    rgApi('hand_raise', {
        session_code: RG.sessionCode,
        participant_id: RG.participantId
    }).then(function(d) {
        if (d && d.ok) rgToast('Hand raised!', 'ok');
    });
}

/* ── Leave ── */

function rgLeave() {
    if (!confirm('Leave this session?')) return;
    rgStopAll();
    rgUploadRecording(); // We upload any recording in progress

    document.getElementById('rgSessionView').style.display = 'none';
    document.getElementById('rgJoinCard').style.display = '';
    document.getElementById('rgJoinBtn').disabled = false;
    rgToast('You have left the session.', 'ok');
}

/* ── Recording (MediaRecorder API) ── */

function rgStartRecording() {
    if (!RG.mediaStream || RG.recording) return;

    RG.recordedChunks = [];
    var options = { mimeType: 'audio/webm;codecs=opus' };
    // We fallback if WebM/Opus not supported
    if (!MediaRecorder.isTypeSupported(options.mimeType)) {
        options = { mimeType: 'audio/webm' };
        if (!MediaRecorder.isTypeSupported(options.mimeType)) {
            options = {};
        }
    }

    try {
        RG.mediaRecorder = new MediaRecorder(RG.mediaStream, options);
    } catch(e) {
        rgToast('MediaRecorder not supported: ' + e.message, 'err');
        return;
    }

    RG.mediaRecorder.ondataavailable = function(e) {
        if (e.data && e.data.size > 0) RG.recordedChunks.push(e.data);
    };

    RG.mediaRecorder.onstop = function() {
        rgUploadRecording();
    };

    RG.mediaRecorder.start(1000); // We collect data every second
    RG.recording = true;
    RG.recStartedAt = Date.now() / 1000;
    rgStartRecTimer();
}

function rgStopRecording() {
    if (!RG.recording || !RG.mediaRecorder) return;
    RG.recording = false;
    rgStopRecTimer();

    if (RG.mediaRecorder.state !== 'inactive') {
        RG.mediaRecorder.stop();
    }
}

function rgUploadRecording() {
    if (RG.recordedChunks.length === 0) return;

    var blob = new Blob(RG.recordedChunks, { type: RG.mediaRecorder ? RG.mediaRecorder.mimeType : 'audio/webm' });
    RG.recordedChunks = [];

    var duration = RG.recStartedAt > 0 ? Math.floor(Date.now() / 1000 - RG.recStartedAt) : 0;

    // We convert to base64 and send via JSON
    var reader = new FileReader();
    reader.onloadend = function() {
        var b64 = reader.result.split(',')[1]; // We strip the data URL prefix
        rgApi('upload_track', {
            session_code: RG.sessionCode,
            participant_id: RG.participantId,
            audio_base64: b64,
            format: 'webm',
            duration_sec: duration
        }).then(function(d) {
            if (d && d.ok) {
                rgToast('Recording uploaded successfully!', 'ok');
            } else {
                rgToast('Upload failed: ' + ((d && d.error) || 'Unknown error'), 'err');
            }
        }).catch(function(e) {
            rgToast('Upload error: ' + e.message, 'err');
        });
    };
    reader.readAsDataURL(blob);
}

/* ── Recording timer ── */

function rgStartRecTimer() {
    rgStopRecTimer();
    RG.timerInterval = setInterval(function() {
        if (!RG.recording) return;
        var elapsed = Math.floor(Date.now() / 1000 - RG.recStartedAt);
        var el = document.getElementById('rgRecTimer');
        if (el) el.textContent = fmtTime(elapsed);
    }, 1000);
}

function rgStopRecTimer() {
    if (RG.timerInterval) { clearInterval(RG.timerInterval); RG.timerInterval = null; }
}

/* ── Polling ── */

function rgStartPolling() {
    rgStopPolling();
    // We heartbeat every 3 seconds
    RG.heartbeatInterval = setInterval(rgHeartbeat, 3000);
    // We poll chat every 2 seconds
    RG.chatPollInterval = setInterval(rgPollChat, 2000);
    // We send levels every 500ms
    RG.levelInterval = setInterval(rgSendLevel, 500);
}

function rgStopPolling() {
    if (RG.heartbeatInterval) { clearInterval(RG.heartbeatInterval); RG.heartbeatInterval = null; }
    if (RG.chatPollInterval) { clearInterval(RG.chatPollInterval); RG.chatPollInterval = null; }
    if (RG.levelInterval) { clearInterval(RG.levelInterval); RG.levelInterval = null; }
}

function rgStopAll() {
    rgStopPolling();
    rgStopRecTimer();
    if (RG.meterRaf) { cancelAnimationFrame(RG.meterRaf); RG.meterRaf = null; }
    if (RG.mediaStream) {
        RG.mediaStream.getTracks().forEach(function(t) { t.stop(); });
        RG.mediaStream = null;
    }
    if (RG.audioContext) {
        RG.audioContext.close().catch(function(){});
        RG.audioContext = null;
    }
}

function rgHeartbeat() {
    rgApi('heartbeat', {
        session_code: RG.sessionCode,
        participant_id: RG.participantId
    }).then(function(d) {
        if (!d || !d.ok) return;

        var prevStatus = RG.sessionStatus;
        RG.sessionStatus = d.session_status;

        // We update status badge
        var badge = document.getElementById('rgStatus');
        if (badge) {
            badge.className = 'rg-status ' + d.session_status;
            badge.textContent = d.session_status;
        }

        // We detect recording start/stop transitions
        if (d.session_status === 'recording' && prevStatus !== 'recording') {
            rgStartRecording();
            document.getElementById('rgRecBanner').classList.add('active');
        }
        if (d.session_status !== 'recording' && prevStatus === 'recording') {
            rgStopRecording();
            document.getElementById('rgRecBanner').classList.remove('active');
        }
        if (d.session_status === 'ended') {
            rgStopAll();
            rgToast('Session has ended. Thank you for participating!', 'warn');
        }
    }).catch(function() {
        rgUpdateConnStatus('warn', 'Connection lost, retrying...');
    });
}

function rgPollSession() {
    rgApi('get_session_public', {
        session_code: RG.sessionCode
    }).then(function(d) {
        if (!d || !d.ok) return;
        document.getElementById('rgHostName').textContent = 'Host: ' + (d.host_name || '?');

        // We render participant list
        var list = document.getElementById('rgPartList');
        var html = '';
        (d.participants || []).forEach(function(p) {
            html += '<div class="rg-part">';
            html += '<div class="rg-part-dot"></div>';
            html += esc(p.name) + ' <span style="color:var(--muted);font-size:10px;">(' + esc(p.role) + ')</span>';
            html += '</div>';
        });
        list.innerHTML = html || '<div style="color:var(--muted);font-size:12px;">No participants yet</div>';
    });
}

function rgSendLevel() {
    if (!RG.connected) return;
    var level = RG._currentLevel || 0;
    if (RG.muted) level = 0;

    rgApi('update_level', {
        session_code: RG.sessionCode,
        participant_id: RG.participantId,
        level: level
    }).catch(function(){});
}

function rgPollChat() {
    rgApi('get_chat', {
        session_code: RG.sessionCode,
        participant_id: RG.participantId,
        since_id: RG.lastChatId
    }).then(function(d) {
        if (!d || !d.ok || !d.messages || d.messages.length === 0) return;

        var container = document.getElementById('rgChatMsgs');
        d.messages.forEach(function(m) {
            var div = document.createElement('div');
            div.className = 'rg-chat-msg';

            var isSystem = m.message.indexOf('**') === 0;
            if (isSystem) {
                div.innerHTML = '<span class="rg-chat-system">' + esc(m.message) + '</span>';
            } else {
                var senderCls = m.sender_role === 'host' ? 'host' : '';
                div.innerHTML = '<span class="rg-chat-sender ' + senderCls + '">' + esc(m.sender_name) + ':</span> ' + esc(m.message);
            }
            container.appendChild(div);
            RG.lastChatId = Math.max(RG.lastChatId, parseInt(m.id));
        });
        container.scrollTop = container.scrollHeight;
    }).catch(function(){});
}

function rgSendChat() {
    var input = document.getElementById('rgChatInput');
    var msg = input.value.trim();
    if (msg === '') return;

    rgApi('send_chat', {
        session_code: RG.sessionCode,
        participant_id: RG.participantId,
        message: msg
    }).then(function(d) {
        if (d && d.ok) {
            input.value = '';
            rgPollChat(); // We immediately poll
        }
    });
}

/* ── Init ── */

document.addEventListener('DOMContentLoaded', function() {
    // We auto-focus the code input if it has a value (came from invite URL),
    // otherwise focus the name input
    var codeInput = document.getElementById('rgCode');
    var nameInput = document.getElementById('rgName');
    if (codeInput.value.trim()) {
        nameInput.focus();
    } else {
        codeInput.focus();
    }
});
</script>
</body>
</html>
