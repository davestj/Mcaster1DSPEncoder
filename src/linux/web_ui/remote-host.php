<?php
/**
 * remote-host.php -- Remote Recording Session Host View
 *
 * File:    src/linux/web_ui/remote-host.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Phase:   PC-7
 * Purpose: We provide the host's control panel for a remote podcast recording
 *          session — participant management, recording controls, chat, level
 *          meters, and invite link sharing.
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

$page_title = 'Remote Recording';
$active_nav = 'recording';
$use_charts = false;

require __DIR__ . '/app/inc/header.php';
?>

<style>
/* Remote Host styles */
.rh-grid { display: grid; grid-template-columns: 240px 1fr 300px; gap: 14px; min-height: calc(100vh - var(--topbar-h) - 80px); }
@media(max-width:1100px) { .rh-grid { grid-template-columns: 200px 1fr 260px; } }
@media(max-width:860px) { .rh-grid { grid-template-columns: 1fr; } }

/* Panels */
.rh-panel { background: var(--card); border: 1px solid var(--border); border-radius: var(--radius); padding: 14px; display: flex; flex-direction: column; }
.rh-panel-title { font-size: 13px; font-weight: 700; color: var(--text); margin-bottom: 10px; display: flex; align-items: center; gap: 7px; }
.rh-panel-title i { color: var(--teal); font-size: 14px; }

/* Session info bar */
.rh-session-bar { display: flex; align-items: center; gap: 12px; flex-wrap: wrap; margin-bottom: 14px; padding: 12px 16px; background: var(--card); border: 1px solid var(--border); border-radius: var(--radius); }
.rh-session-bar.is-recording { border-color: #ef4444; box-shadow: 0 0 0 2px rgba(239,68,68,.12); }
.rh-session-title { font-size: 16px; font-weight: 700; color: var(--text); }
.rh-session-code { font-family: monospace; font-size: 14px; color: var(--teal); background: rgba(20,184,166,.08); padding: 3px 10px; border-radius: 4px; letter-spacing: 2px; }
.rh-session-timer { font-family: monospace; font-size: 20px; font-weight: 700; color: var(--text); }
.rh-rec-dot { display: inline-block; width: 10px; height: 10px; border-radius: 50%; background: #555; margin-right: 4px; }
.rh-rec-dot.active { background: #ef4444; animation: rec-pulse 1s infinite; }
@keyframes rec-pulse { 0%,100%{opacity:1} 50%{opacity:.3} }
.rh-copy-btn { cursor: pointer; font-size: 12px; color: var(--teal); background: rgba(20,184,166,.08); border: 1px solid rgba(20,184,166,.25); border-radius: 4px; padding: 4px 10px; }
.rh-copy-btn:hover { background: rgba(20,184,166,.18); }

/* Participant list */
.rh-part-list { flex: 1; overflow-y: auto; min-height: 0; }
.rh-part-item { display: flex; align-items: center; gap: 8px; padding: 8px 6px; border-bottom: 1px solid var(--border); }
.rh-part-item:last-child { border-bottom: none; }
.rh-part-dot { width: 8px; height: 8px; border-radius: 50%; flex-shrink: 0; }
.rh-part-dot.connected { background: var(--green); box-shadow: 0 0 5px var(--green); }
.rh-part-dot.disconnected { background: var(--muted); }
.rh-part-dot.connecting { background: var(--yellow); animation: rec-pulse 1.2s infinite; }
.rh-part-name { flex: 1; font-size: 13px; color: var(--text); }
.rh-part-role { font-size: 10px; padding: 1px 6px; border-radius: 3px; background: rgba(20,184,166,.1); color: var(--teal); }
.rh-part-role.host { background: rgba(249,115,22,.12); color: var(--orange); }
.rh-part-hand { color: var(--yellow); font-size: 14px; animation: hand-wave 0.5s ease-in-out infinite alternate; }
@keyframes hand-wave { 0%{transform:rotate(-15deg)} 100%{transform:rotate(15deg)} }
.rh-part-mute { cursor: pointer; font-size: 12px; color: var(--muted); padding: 2px 6px; border-radius: 3px; background: rgba(255,255,255,.05); border: 1px solid var(--border); }
.rh-part-mute:hover { color: var(--red); border-color: rgba(239,68,68,.3); }

/* Level meter bar (horizontal) */
.rh-level-wrap { margin-top: 4px; height: 4px; background: rgba(255,255,255,.06); border-radius: 2px; overflow: hidden; }
.rh-level-bar { height: 100%; background: var(--teal); border-radius: 2px; transition: width 0.3s; width: 0; }
.rh-level-bar.hot { background: var(--yellow); }
.rh-level-bar.clip { background: var(--red); }

/* Recording controls */
.rh-rec-controls { display: flex; gap: 8px; flex-wrap: wrap; margin-bottom: 12px; }
.rh-track-list { flex: 1; overflow-y: auto; min-height: 0; font-size: 12px; }
.rh-track-item { display: flex; align-items: center; gap: 8px; padding: 6px 4px; border-bottom: 1px solid var(--border); }
.rh-track-name { flex: 1; color: var(--text-dim); }
.rh-track-db { font-family: monospace; color: var(--teal); min-width: 50px; text-align: right; }
.rh-track-meter { width: 120px; height: 6px; background: #0a0e14; border-radius: 3px; overflow: hidden; flex-shrink: 0; }
.rh-track-meter-fill { height: 100%; border-radius: 3px; transition: width 0.3s, background 0.3s; }

/* Chat */
.rh-chat-msgs { flex: 1; overflow-y: auto; min-height: 0; padding-right: 4px; }
.rh-chat-msg { padding: 4px 0; border-bottom: 1px solid rgba(51,65,85,.3); font-size: 12px; }
.rh-chat-sender { font-weight: 600; color: var(--teal); }
.rh-chat-sender.host { color: var(--orange); }
.rh-chat-text { color: var(--text-dim); }
.rh-chat-time { font-size: 10px; color: var(--muted); float: right; }
.rh-chat-system { color: var(--yellow); font-style: italic; }
.rh-chat-input { display: flex; gap: 6px; margin-top: 8px; flex-shrink: 0; }
.rh-chat-input input { flex: 1; }

/* Create session form */
.rh-create { max-width: 500px; margin: 40px auto; }
.rh-create .form-group { margin-bottom: 14px; }

/* Empty state */
.rh-no-session { text-align: center; padding: 60px 20px; color: var(--muted); }
.rh-no-session i { font-size: 48px; display: block; margin-bottom: 16px; color: var(--border); }
</style>

<!-- Session bar (hidden until session loaded) -->
<div class="rh-session-bar" id="rhSessionBar" style="display:none;">
  <span class="rh-rec-dot" id="rhRecDot"></span>
  <span class="rh-session-timer" id="rhTimer">00:00:00</span>
  <span class="rh-session-title" id="rhTitle"></span>
  <span class="rh-session-code" id="rhCode"></span>
  <button class="rh-copy-btn" id="rhCopyBtn" onclick="rhCopyInvite()"><i class="fa-solid fa-copy"></i> Copy Invite Link</button>
  <span style="flex:1"></span>
  <div class="rh-rec-controls">
    <button class="btn btn-danger btn-sm" id="btnRhRec" onclick="rhStartRec()"><i class="fa-solid fa-circle"></i> Start Recording</button>
    <button class="btn btn-secondary btn-sm" id="btnRhStop" onclick="rhStopRec()" disabled><i class="fa-solid fa-stop"></i> Stop</button>
    <button class="btn btn-secondary btn-sm" onclick="rhEndSession()"><i class="fa-solid fa-xmark"></i> End Session</button>
  </div>
</div>

<!-- Main 3-column layout (hidden until session loaded) -->
<div class="rh-grid" id="rhGrid" style="display:none;">
  <!-- Left: Participants -->
  <div class="rh-panel">
    <div class="rh-panel-title"><i class="fa-solid fa-users"></i> Participants <span id="rhPartCount" style="font-weight:400;color:var(--muted);font-size:11px;"></span></div>
    <div class="rh-part-list" id="rhPartList">
      <div style="text-align:center;padding:30px 8px;color:var(--muted);font-size:12px;">Waiting for guests to join...</div>
    </div>
  </div>

  <!-- Center: Recording controls + per-track levels -->
  <div class="rh-panel">
    <div class="rh-panel-title"><i class="fa-solid fa-waveform-lines"></i> Per-Track Levels</div>
    <div class="rh-track-list" id="rhTrackList">
      <div style="text-align:center;padding:30px 8px;color:var(--muted);font-size:12px;">Participants will appear here with level meters</div>
    </div>
  </div>

  <!-- Right: Chat -->
  <div class="rh-panel">
    <div class="rh-panel-title"><i class="fa-solid fa-comments"></i> Chat</div>
    <div class="rh-chat-msgs" id="rhChatMsgs">
      <div style="text-align:center;padding:30px 8px;color:var(--muted);font-size:12px;">Chat messages will appear here</div>
    </div>
    <div class="rh-chat-input">
      <input type="text" id="rhChatInput" class="form-input" placeholder="Type a message..." onkeydown="if(event.key==='Enter')rhSendChat()">
      <button class="btn btn-primary btn-sm" onclick="rhSendChat()"><i class="fa-solid fa-paper-plane"></i></button>
    </div>
  </div>
</div>

<!-- Create/Select session view (shown when no active session) -->
<div id="rhCreateView">
  <div class="rh-no-session" id="rhNoSession">
    <i class="fa-solid fa-satellite-dish"></i>
    <div style="font-size:16px;color:var(--text);margin-bottom:8px;">Remote Recording Studio</div>
    <div style="margin-bottom:20px;">Create a new session or resume an existing one</div>
    <button class="btn btn-primary" onclick="rhShowCreate()"><i class="fa-solid fa-plus"></i> New Session</button>
    <button class="btn btn-secondary" style="margin-left:8px;" onclick="rhShowHistory()"><i class="fa-solid fa-clock-rotate-left"></i> Session History</button>
  </div>

  <!-- Create session form (hidden) -->
  <div class="rh-create card" id="rhCreateForm" style="display:none;">
    <div class="card-title" style="margin-bottom:16px;"><i class="fa-solid fa-plus fa-fw"></i> Create Remote Recording Session</div>
    <div class="form-group">
      <label class="form-label">Session Title</label>
      <input type="text" id="rhNewTitle" class="form-input" value="Recording Session" placeholder="e.g., Interview with Jane">
    </div>
    <div class="form-group">
      <label class="form-label">Show (optional)</label>
      <select id="rhNewShow" class="form-select"><option value="">-- None --</option></select>
    </div>
    <div class="form-group">
      <label class="form-label">Max Participants</label>
      <input type="number" id="rhNewMax" class="form-input" value="4" min="2" max="10" style="max-width:100px;">
    </div>
    <div class="form-group">
      <label class="form-label">Audio Format</label>
      <select id="rhNewFormat" class="form-select" style="max-width:160px;">
        <option value="wav" selected>WAV (highest quality)</option>
        <option value="webm">WebM/Opus</option>
        <option value="mp3">MP3</option>
      </select>
    </div>
    <div class="form-group">
      <label class="form-label">Sample Rate</label>
      <select id="rhNewSR" class="form-select" style="max-width:160px;">
        <option value="48000" selected>48000 Hz</option>
        <option value="44100">44100 Hz</option>
      </select>
    </div>
    <div style="display:flex;gap:8px;margin-top:16px;">
      <button class="btn btn-primary" onclick="rhCreateSession()"><i class="fa-solid fa-satellite-dish"></i> Create Session</button>
      <button class="btn btn-secondary" onclick="document.getElementById('rhCreateForm').style.display='none';document.getElementById('rhNoSession').style.display='';">Cancel</button>
    </div>
  </div>

  <!-- Session history (hidden) -->
  <div class="card" id="rhHistory" style="display:none;max-width:700px;margin:20px auto;">
    <div class="card-hdr">
      <div class="card-title"><i class="fa-solid fa-clock-rotate-left fa-fw"></i> Session History</div>
      <button class="btn btn-secondary btn-sm" onclick="document.getElementById('rhHistory').style.display='none';document.getElementById('rhNoSession').style.display='';">Close</button>
    </div>
    <div class="tbl-wrap">
      <table>
        <thead><tr><th>Title</th><th>Code</th><th>Status</th><th>Participants</th><th>Created</th><th></th></tr></thead>
        <tbody id="rhHistoryBody"></tbody>
      </table>
    </div>
  </div>
</div>

<script>
/* ── Remote Host JS ─────────────────────────────────────────────────────── */

var RH = {
    sessionId: 0,
    sessionCode: '',
    participantId: 0, // host's participant_id
    status: 'waiting',
    startedAt: 0,
    timerInterval: null,
    pollInterval: null,
    chatPollInterval: null,
    lastChatId: 0,
    inviteUrl: '',
};

function esc(s) {
    return String(s).replace(/&/g,'&amp;').replace(/"/g,'&quot;').replace(/'/g,'&#39;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}

function fmtTime(sec) {
    var h = Math.floor(sec / 3600);
    var m = Math.floor((sec % 3600) / 60);
    var s = sec % 60;
    return String(h).padStart(2,'0') + ':' + String(m).padStart(2,'0') + ':' + String(s).padStart(2,'0');
}

function fmtBytes(b) {
    if (b < 1024) return b + ' B';
    if (b < 1048576) return (b/1024).toFixed(1) + ' KB';
    if (b < 1073741824) return (b/1048576).toFixed(1) + ' MB';
    return (b/1073741824).toFixed(2) + ' GB';
}

/* ── Show/hide views ── */

function rhShowCreate() {
    document.getElementById('rhNoSession').style.display = 'none';
    document.getElementById('rhHistory').style.display = 'none';
    document.getElementById('rhCreateForm').style.display = '';
}

function rhShowHistory() {
    document.getElementById('rhNoSession').style.display = 'none';
    document.getElementById('rhCreateForm').style.display = 'none';
    document.getElementById('rhHistory').style.display = '';
    rhLoadHistory();
}

/* ── Load shows for dropdown ── */

function rhLoadShows() {
    mc1Api('POST', '/app/api/podcast.php', {action:'list_shows'}).then(function(d) {
        var sel = document.getElementById('rhNewShow');
        if (!sel || !d || !d.shows) return;
        d.shows.forEach(function(s) {
            var opt = document.createElement('option');
            opt.value = s.id;
            opt.textContent = s.title;
            sel.appendChild(opt);
        });
    }).catch(function(){});
}

/* ── Create session ── */

function rhCreateSession() {
    var title  = document.getElementById('rhNewTitle').value || 'Recording Session';
    var showId = parseInt(document.getElementById('rhNewShow').value) || null;
    var max    = parseInt(document.getElementById('rhNewMax').value) || 4;
    var fmt    = document.getElementById('rhNewFormat').value;
    var sr     = parseInt(document.getElementById('rhNewSR').value) || 48000;

    mc1Api('POST', '/app/api/remote.php', {
        action: 'create_session',
        title: title,
        show_id: showId,
        max_participants: max,
        settings: { sample_rate: sr, channels: 1, format: fmt }
    }).then(function(d) {
        if (d && d.ok) {
            RH.sessionId      = d.session_id;
            RH.sessionCode    = d.session_code;
            RH.participantId  = d.host_participant_id;
            RH.inviteUrl      = d.invite_url;
            RH.status         = 'waiting';
            rhShowSession(title);
            mc1Toast('Session created! Share the invite link with your guests.', 'ok');
        } else {
            mc1Toast((d && d.error) || 'Failed to create session', 'err');
        }
    }).catch(function(e) {
        mc1Toast('Error creating session: ' + e.message, 'err');
    });
}

/* ── Show active session UI ── */

function rhShowSession(title) {
    document.getElementById('rhCreateView').style.display = 'none';
    document.getElementById('rhSessionBar').style.display = 'flex';
    document.getElementById('rhGrid').style.display = '';

    document.getElementById('rhTitle').textContent = title || 'Recording Session';
    document.getElementById('rhCode').textContent = RH.sessionCode;

    rhStartPolling();
}

/* ── Copy invite link ── */

function rhCopyInvite() {
    var url = RH.inviteUrl;
    if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(url).then(function() {
            mc1Toast('Invite link copied to clipboard!', 'ok');
        });
    } else {
        // Fallback
        var ta = document.createElement('textarea');
        ta.value = url;
        document.body.appendChild(ta);
        ta.select();
        document.execCommand('copy');
        document.body.removeChild(ta);
        mc1Toast('Invite link copied!', 'ok');
    }
}

/* ── Recording controls ── */

function rhStartRec() {
    mc1Api('POST', '/app/api/remote.php', {
        action: 'start_recording',
        session_id: RH.sessionId
    }).then(function(d) {
        if (d && d.ok) {
            RH.status = 'recording';
            RH.startedAt = Date.now() / 1000;
            rhUpdateRecUI();
            rhStartTimer();
            mc1Toast('Recording started! All participants are being recorded locally.', 'ok');
        } else {
            mc1Toast((d && d.error) || 'Failed to start recording', 'err');
        }
    });
}

function rhStopRec() {
    mc1Api('POST', '/app/api/remote.php', {
        action: 'stop_recording',
        session_id: RH.sessionId
    }).then(function(d) {
        if (d && d.ok) {
            RH.status = 'waiting';
            rhStopTimer();
            rhUpdateRecUI();
            var trackCount = (d.tracks || []).length;
            mc1Toast('Recording stopped. ' + trackCount + ' track(s) received.', 'ok');
        } else {
            mc1Toast((d && d.error) || 'Failed to stop recording', 'err');
        }
    });
}

function rhEndSession() {
    if (!confirm('End this session? All participants will be disconnected.')) return;
    mc1Api('POST', '/app/api/remote.php', {
        action: 'end_session',
        session_id: RH.sessionId
    }).then(function(d) {
        if (d && d.ok) {
            rhStopAll();
            mc1Toast('Session ended.', 'ok');
            // We reload to show create view
            setTimeout(function(){ window.location.reload(); }, 1000);
        }
    });
}

/* ── Timer ── */

function rhStartTimer() {
    rhStopTimer();
    RH.timerInterval = setInterval(function() {
        if (RH.status !== 'recording') return;
        var elapsed = Math.floor(Date.now() / 1000 - RH.startedAt);
        document.getElementById('rhTimer').textContent = fmtTime(elapsed);
    }, 1000);
}

function rhStopTimer() {
    if (RH.timerInterval) { clearInterval(RH.timerInterval); RH.timerInterval = null; }
}

function rhUpdateRecUI() {
    var bar    = document.getElementById('rhSessionBar');
    var dot    = document.getElementById('rhRecDot');
    var btnRec = document.getElementById('btnRhRec');
    var btnStp = document.getElementById('btnRhStop');

    if (RH.status === 'recording') {
        bar.classList.add('is-recording');
        dot.classList.add('active');
        btnRec.disabled = true;
        btnStp.disabled = false;
    } else {
        bar.classList.remove('is-recording');
        dot.classList.remove('active');
        btnRec.disabled = false;
        btnStp.disabled = true;
    }
}

/* ── Polling ── */

function rhStartPolling() {
    rhStopPolling();
    rhPollSession();
    RH.pollInterval = setInterval(rhPollSession, 2000);
    rhPollChat();
    RH.chatPollInterval = setInterval(rhPollChat, 2000);
}

function rhStopPolling() {
    if (RH.pollInterval) { clearInterval(RH.pollInterval); RH.pollInterval = null; }
    if (RH.chatPollInterval) { clearInterval(RH.chatPollInterval); RH.chatPollInterval = null; }
}

function rhStopAll() {
    rhStopTimer();
    rhStopPolling();
}

function rhPollSession() {
    mc1Api('POST', '/app/api/remote.php', {
        action: 'get_session',
        session_id: RH.sessionId
    }).then(function(d) {
        if (!d || !d.ok) return;

        var sess = d.session;
        var parts = d.participants || [];
        var levels = d.levels || {};

        // We update status if changed
        if (sess.status !== RH.status) {
            RH.status = sess.status;
            rhUpdateRecUI();
            if (sess.status === 'recording' && !RH.timerInterval) {
                RH.startedAt = new Date(sess.started_at).getTime() / 1000;
                rhStartTimer();
            }
        }

        // We update invite URL
        if (d.invite_url) RH.inviteUrl = d.invite_url;

        // We render participant list
        rhRenderParticipants(parts, levels);

        // We render track levels
        rhRenderTrackLevels(parts, levels);

        // We update count
        document.getElementById('rhPartCount').textContent = '(' + parts.length + ')';
    }).catch(function(){});
}

function rhRenderParticipants(parts, levels) {
    var list = document.getElementById('rhPartList');
    if (parts.length === 0) {
        list.innerHTML = '<div style="text-align:center;padding:30px 8px;color:var(--muted);font-size:12px;">Waiting for guests to join...</div>';
        return;
    }

    var html = '';
    parts.forEach(function(p) {
        var lv = levels[p.id] || {};
        var connected = p.is_connected == 1 || p.is_connected === true;
        var dotCls = connected ? 'connected' : 'disconnected';
        if (lv.updated_at && (Date.now()/1000 - lv.updated_at) < 6) dotCls = 'connected';

        html += '<div class="rh-part-item">';
        html += '<div class="rh-part-dot ' + dotCls + '"></div>';
        html += '<span class="rh-part-name">' + esc(p.name) + '</span>';
        if (lv.hand_raised) {
            html += '<span class="rh-part-hand" title="Hand raised"><i class="fa-solid fa-hand"></i></span>';
        }
        html += '<span class="rh-part-role ' + (p.role === 'host' ? 'host' : '') + '">' + esc(p.role) + '</span>';
        html += '</div>';

        // We add a level bar
        var lvl = lv.level || 0;
        var barCls = lvl > 0.85 ? 'clip' : lvl > 0.65 ? 'hot' : '';
        html += '<div class="rh-level-wrap"><div class="rh-level-bar ' + barCls + '" style="width:' + (lvl*100).toFixed(1) + '%"></div></div>';
    });
    list.innerHTML = html;
}

function rhRenderTrackLevels(parts, levels) {
    var list = document.getElementById('rhTrackList');
    if (parts.length === 0) {
        list.innerHTML = '<div style="text-align:center;padding:30px 8px;color:var(--muted);font-size:12px;">No participants yet</div>';
        return;
    }

    var html = '';
    parts.forEach(function(p) {
        var lv = levels[p.id] || {};
        var lvl = lv.level || 0;
        var db = lvl > 0 ? (20 * Math.log10(lvl)).toFixed(1) : '-inf';
        var pct = (lvl * 100).toFixed(1);
        var color = lvl > 0.85 ? '#ef4444' : lvl > 0.65 ? '#eab308' : '#14b8a6';

        html += '<div class="rh-track-item">';
        html += '<span class="rh-track-name">' + esc(p.name) + ' <span style="color:var(--muted);font-size:10px;">(' + esc(p.role) + ')</span></span>';
        html += '<div class="rh-track-meter"><div class="rh-track-meter-fill" style="width:' + pct + '%;background:' + color + '"></div></div>';
        html += '<span class="rh-track-db">' + db + ' dB</span>';
        html += '</div>';
    });
    list.innerHTML = html;
}

/* ── Chat ── */

function rhPollChat() {
    mc1Api('POST', '/app/api/remote.php', {
        action: 'get_chat',
        session_id: RH.sessionId,
        since_id: RH.lastChatId
    }).then(function(d) {
        if (!d || !d.ok || !d.messages || d.messages.length === 0) return;

        var container = document.getElementById('rhChatMsgs');
        // We clear the placeholder on first message
        if (RH.lastChatId === 0 && container.querySelector('div[style]')) {
            container.innerHTML = '';
        }

        d.messages.forEach(function(m) {
            var div = document.createElement('div');
            div.className = 'rh-chat-msg';

            var isSystem = m.message.indexOf('**') === 0;
            if (isSystem) {
                div.innerHTML = '<span class="rh-chat-system">' + esc(m.message) + '</span>';
            } else {
                var senderCls = m.sender_role === 'host' ? 'host' : '';
                div.innerHTML = '<span class="rh-chat-time">' + esc((m.sent_at||'').substr(11,5)) + '</span>' +
                    '<span class="rh-chat-sender ' + senderCls + '">' + esc(m.sender_name) + ':</span> ' +
                    '<span class="rh-chat-text">' + esc(m.message) + '</span>';
            }
            container.appendChild(div);
            RH.lastChatId = Math.max(RH.lastChatId, parseInt(m.id));
        });

        container.scrollTop = container.scrollHeight;
    }).catch(function(){});
}

function rhSendChat() {
    var input = document.getElementById('rhChatInput');
    var msg = input.value.trim();
    if (msg === '' || !RH.sessionId || !RH.participantId) return;

    mc1Api('POST', '/app/api/remote.php', {
        action: 'send_chat',
        session_id: RH.sessionId,
        participant_id: RH.participantId,
        message: msg
    }).then(function(d) {
        if (d && d.ok) {
            input.value = '';
            rhPollChat(); // We immediately poll for the new message
        }
    });
}

/* ── Session history ── */

function rhLoadHistory() {
    mc1Api('POST', '/app/api/remote.php', {action:'list_sessions'}).then(function(d) {
        var tbody = document.getElementById('rhHistoryBody');
        if (!d || !d.ok || !d.sessions || d.sessions.length === 0) {
            tbody.innerHTML = '<tr><td colspan="6" style="text-align:center;color:var(--muted);">No sessions found</td></tr>';
            return;
        }

        var html = '';
        d.sessions.forEach(function(s) {
            var statusBadge = s.status === 'recording'
                ? '<span class="badge badge-red">Recording</span>'
                : s.status === 'waiting'
                    ? '<span class="badge badge-teal">Waiting</span>'
                    : '<span class="badge badge-gray">Ended</span>';

            html += '<tr>';
            html += '<td class="td-title">' + esc(s.title) + '</td>';
            html += '<td class="td-mono">' + esc(s.session_code) + '</td>';
            html += '<td>' + statusBadge + '</td>';
            html += '<td>' + (s.participant_count || 0) + '</td>';
            html += '<td style="font-size:11px;">' + esc((s.created_at||'').substr(0,16)) + '</td>';
            html += '<td class="td-acts">';
            if (s.status !== 'ended') {
                html += '<button class="btn btn-primary btn-xs" onclick="rhResumeSession(' + s.id + ')">Resume</button>';
            }
            html += '</td>';
            html += '</tr>';
        });
        tbody.innerHTML = html;
    });
}

function rhResumeSession(sid) {
    mc1Api('POST', '/app/api/remote.php', {
        action: 'get_session',
        session_id: sid
    }).then(function(d) {
        if (!d || !d.ok) {
            mc1Toast((d && d.error) || 'Session not found', 'err');
            return;
        }
        var sess = d.session;
        RH.sessionId     = parseInt(sess.id);
        RH.sessionCode   = sess.session_code;
        RH.status        = sess.status;
        RH.inviteUrl     = d.invite_url;

        // We find the host participant_id
        (d.participants || []).forEach(function(p) {
            if (p.role === 'host') RH.participantId = parseInt(p.id);
        });

        if (sess.status === 'recording' && sess.started_at) {
            RH.startedAt = new Date(sess.started_at).getTime() / 1000;
            rhStartTimer();
        }

        rhShowSession(sess.title);
        rhUpdateRecUI();
    });
}

/* ── Init ── */

document.addEventListener('DOMContentLoaded', function() {
    rhLoadShows();
    // We check URL params for ?session_id=N
    var params = new URLSearchParams(window.location.search);
    var sid = parseInt(params.get('session_id'));
    if (sid > 0) {
        rhResumeSession(sid);
    }
});
</script>

<?php require __DIR__ . '/app/inc/footer.php'; ?>
