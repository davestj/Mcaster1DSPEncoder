<?php
/**
 * voictune_coaching.php — Reusable coaching panel widget for VoicTune.
 *
 * Outputs an HTML panel with:
 *   - Coaching tips card list (JS polling)
 *   - AI Chat panel (text input + send + history)
 *   - "Analyze My Voice" button
 *   - "Suggest EQ" button with result bands
 *   - "Calibrate" button
 *   - Voice profile card
 *
 * All JS uses fetch() to VoicTune API (port 8350).
 * Include this file inside a page that has header.php already loaded.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/* VoicTune API base — default port 8350 */
$vt_port = 8350;
?>

<style>
/* ── VoicTune Coaching Panel ────────────────────────────────────────── */
.vtc-panel{display:flex;flex-direction:column;gap:16px}
.vtc-card{background:var(--card,#1e293b);border:1px solid var(--border,#334155);border-radius:var(--radius,10px);padding:16px;overflow:hidden}
.vtc-card h3{font-size:15px;font-weight:600;color:var(--teal,#14b8a6);margin-bottom:12px;display:flex;align-items:center;gap:8px}
.vtc-card h3 i{font-size:14px;opacity:.7}

/* Tips list */
.vtc-tips{list-style:none;padding:0;margin:0;max-height:260px;overflow-y:auto}
.vtc-tips li{padding:8px 10px;margin-bottom:6px;border-radius:var(--radius-sm,6px);font-size:13px;line-height:1.4;border-left:3px solid transparent}
.vtc-tips li.info{background:rgba(20,184,166,.08);border-left-color:var(--teal,#14b8a6)}
.vtc-tips li.suggestion{background:rgba(8,145,178,.08);border-left-color:var(--cyan,#0891b2)}
.vtc-tips li.warning{background:rgba(249,115,22,.08);border-left-color:var(--orange,#f97316)}
.vtc-tips li.critical{background:rgba(239,68,68,.08);border-left-color:var(--red,#ef4444)}
.vtc-tips li .tip-cat{font-weight:600;text-transform:uppercase;font-size:11px;letter-spacing:.5px;margin-bottom:2px;opacity:.7}
.vtc-tips li .tip-msg{color:var(--text,#e2e8f0)}
.vtc-tips li .tip-sug{color:var(--text-dim,#94a3b8);font-size:12px;margin-top:2px}

/* Voice profile card */
.vtc-profile-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px 16px}
.vtc-profile-item{display:flex;flex-direction:column}
.vtc-profile-item .label{font-size:11px;text-transform:uppercase;letter-spacing:.5px;color:var(--muted,#64748b)}
.vtc-profile-item .value{font-size:16px;font-weight:600;color:var(--text,#e2e8f0)}
.vtc-profile-item .value.voice-type{color:var(--teal,#14b8a6)}

/* AI Chat */
.vtc-chat-history{max-height:300px;overflow-y:auto;margin-bottom:12px;padding-right:4px}
.vtc-chat-msg{margin-bottom:10px;padding:8px 12px;border-radius:var(--radius-sm,6px);font-size:13px;line-height:1.5}
.vtc-chat-msg.user{background:rgba(20,184,166,.12);margin-left:20%;text-align:right}
.vtc-chat-msg.ai{background:var(--card2,#263348);margin-right:20%}
.vtc-chat-msg .sender{font-size:11px;font-weight:600;text-transform:uppercase;letter-spacing:.5px;color:var(--muted,#64748b);margin-bottom:2px}
.vtc-chat-input-row{display:flex;gap:8px}
.vtc-chat-input-row input{flex:1;background:var(--bg2,#1e293b);border:1px solid var(--border,#334155);border-radius:var(--radius-sm,6px);padding:8px 12px;color:var(--text,#e2e8f0);outline:none}
.vtc-chat-input-row input:focus{border-color:var(--teal,#14b8a6)}
.vtc-chat-input-row button{background:var(--teal,#14b8a6);color:#fff;border:none;border-radius:var(--radius-sm,6px);padding:8px 16px;font-weight:600;font-size:13px;white-space:nowrap}
.vtc-chat-input-row button:hover{background:var(--teal2,#0d9488)}
.vtc-chat-input-row button:disabled{opacity:.5;cursor:not-allowed}

/* Action buttons */
.vtc-actions{display:flex;flex-wrap:wrap;gap:8px;margin-top:8px}
.vtc-btn{background:var(--card2,#263348);color:var(--text,#e2e8f0);border:1px solid var(--border,#334155);border-radius:var(--radius-sm,6px);padding:8px 14px;font-size:13px;font-weight:500;display:flex;align-items:center;gap:6px;transition:all .15s}
.vtc-btn:hover{border-color:var(--teal,#14b8a6);background:rgba(20,184,166,.08)}
.vtc-btn:disabled{opacity:.5;cursor:not-allowed}
.vtc-btn.primary{background:var(--teal,#14b8a6);color:#fff;border-color:var(--teal,#14b8a6)}
.vtc-btn.primary:hover{background:var(--teal2,#0d9488)}

/* EQ suggestion */
.vtc-eq-result{margin-top:12px}
.vtc-eq-table{width:100%;border-collapse:collapse;font-size:12px}
.vtc-eq-table th{text-align:left;padding:4px 8px;border-bottom:1px solid var(--border,#334155);color:var(--muted,#64748b);font-weight:500;text-transform:uppercase;letter-spacing:.5px;font-size:11px}
.vtc-eq-table td{padding:4px 8px;border-bottom:1px solid rgba(51,65,85,.3)}
.vtc-eq-rationale{margin-top:8px;font-size:13px;color:var(--text-dim,#94a3b8);font-style:italic}

/* Analysis result */
.vtc-analysis{margin-top:12px;padding:12px;background:var(--bg2,#1e293b);border-radius:var(--radius-sm,6px);font-size:13px;line-height:1.6}
.vtc-analysis .suggestions{margin-top:8px;padding-left:16px}
.vtc-analysis .suggestions li{color:var(--text-dim,#94a3b8);margin-bottom:4px}

/* Spinner */
.vtc-spin{display:inline-block;width:14px;height:14px;border:2px solid rgba(255,255,255,.3);border-top-color:#fff;border-radius:50%;animation:vtcSpin .6s linear infinite}
@keyframes vtcSpin{to{transform:rotate(360deg)}}
</style>

<div class="vtc-panel" id="vtcPanel">

  <!-- Voice Profile Card -->
  <div class="vtc-card">
    <h3><i class="fas fa-user-circle"></i> Voice Profile</h3>
    <div class="vtc-profile-grid" id="vtcProfile">
      <div class="vtc-profile-item"><span class="label">Voice Type</span><span class="value voice-type" id="vtcVoiceType">--</span></div>
      <div class="vtc-profile-item"><span class="label">Fundamental</span><span class="value" id="vtcFundamental">-- Hz</span></div>
      <div class="vtc-profile-item"><span class="label">Avg LUFS</span><span class="value" id="vtcLufs">--</span></div>
      <div class="vtc-profile-item"><span class="label">Avg RMS</span><span class="value" id="vtcRms">-- dB</span></div>
      <div class="vtc-profile-item"><span class="label">Dynamic Range</span><span class="value" id="vtcDynamic">-- dB</span></div>
      <div class="vtc-profile-item"><span class="label">Profile</span><span class="value" id="vtcProfileName">--</span></div>
    </div>
    <div class="vtc-actions">
      <button class="vtc-btn primary" onclick="vtcCalibrate()" id="vtcCalibrateBtn"><i class="fas fa-crosshairs"></i> Calibrate</button>
    </div>
  </div>

  <!-- Coaching Tips -->
  <div class="vtc-card">
    <h3><i class="fas fa-lightbulb"></i> Coaching Tips</h3>
    <ul class="vtc-tips" id="vtcTipsList">
      <li class="info"><span class="tip-msg">Waiting for analysis data...</span></li>
    </ul>
  </div>

  <!-- AI Voice Analysis -->
  <div class="vtc-card">
    <h3><i class="fas fa-brain"></i> AI Voice Analysis</h3>
    <div class="vtc-actions">
      <button class="vtc-btn primary" onclick="vtcAnalyze()" id="vtcAnalyzeBtn"><i class="fas fa-microscope"></i> Analyze My Voice</button>
      <button class="vtc-btn" onclick="vtcSuggestEq()" id="vtcEqBtn"><i class="fas fa-sliders-h"></i> Suggest EQ</button>
    </div>
    <div id="vtcAnalysisResult" class="vtc-analysis" style="display:none"></div>
    <div id="vtcEqResult" class="vtc-eq-result" style="display:none"></div>
  </div>

  <!-- AI Coach Chat -->
  <div class="vtc-card">
    <h3><i class="fas fa-comments"></i> AI Coach</h3>
    <div class="vtc-chat-history" id="vtcChatHistory"></div>
    <div class="vtc-chat-input-row">
      <input type="text" id="vtcChatInput" placeholder="Ask your AI voice coach..." onkeydown="if(event.key==='Enter')vtcChatSend()">
      <button onclick="vtcChatSend()" id="vtcChatSendBtn">Send</button>
    </div>
  </div>

</div>

<script>
(function(){
    var VT_API = window.location.protocol + '//' + window.location.hostname + ':<?= (int)$vt_port ?>';
    var vtcTipsPollId = null;
    var vtcProfilePollId = null;

    /* ── Fetch helper ────────────────────────────────────────────── */
    function vtcFetch(path, opts) {
        opts = opts || {};
        opts.credentials = 'include';
        return fetch(VT_API + path, opts).then(function(r){ return r.json(); });
    }

    /* ── Tips polling ────────────────────────────────────────────── */
    function vtcPollTips() {
        vtcFetch('/api/v1/voictune/coaching/tips').then(function(d) {
            if (!d.ok) return;
            var list = document.getElementById('vtcTipsList');
            if (!list) return;
            if (!d.tips || d.tips.length === 0) {
                list.innerHTML = '<li class="info"><span class="tip-msg">No active coaching tips — keep speaking!</span></li>';
                return;
            }
            var html = '';
            for (var i = 0; i < d.tips.length; i++) {
                var t = d.tips[i];
                var cls = t.severity || 'info';
                html += '<li class="' + cls + '">' +
                    '<div class="tip-cat">' + esc(t.category || '') + '</div>' +
                    '<div class="tip-msg">' + esc(t.message || '') + '</div>';
                if (t.suggestion)
                    html += '<div class="tip-sug">' + esc(t.suggestion) + '</div>';
                html += '</li>';
            }
            list.innerHTML = html;
        }).catch(function(){});
    }

    /* ── Profile polling ─────────────────────────────────────────── */
    function vtcPollProfile() {
        vtcFetch('/api/v1/voictune/coaching/profile').then(function(d) {
            if (!d.ok) return;
            setText('vtcVoiceType', d.voice_type || '--');
            setText('vtcFundamental', fmtHz(d.fundamental_hz));
            setText('vtcLufs', fmtDb(d.avg_lufs));
            setText('vtcRms', fmtDb(d.avg_rms_db) + ' dB');
            setText('vtcDynamic', fmtDb(d.dynamic_range_db) + ' dB');
            setText('vtcProfileName', d.profile_name || 'Default');
        }).catch(function(){});
    }

    /* ── Calibrate ───────────────────────────────────────────────── */
    window.vtcCalibrate = function() {
        var btn = document.getElementById('vtcCalibrateBtn');
        if (btn) { btn.disabled = true; btn.innerHTML = '<span class="vtc-spin"></span> Calibrating...'; }

        var name = prompt('Profile name:', 'Default');
        if (!name) { resetBtn(btn, '<i class="fas fa-crosshairs"></i> Calibrate'); return; }

        vtcFetch('/api/v1/voictune/coaching/calibrate', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({profile_name: name})
        }).then(function(d) {
            resetBtn(btn, '<i class="fas fa-crosshairs"></i> Calibrate');
            if (d.ok && d.profile) {
                var p = d.profile;
                setText('vtcVoiceType', p.voice_type || '--');
                setText('vtcFundamental', fmtHz(p.fundamental_hz));
                setText('vtcLufs', fmtDb(p.avg_lufs));
                setText('vtcRms', fmtDb(p.avg_rms_db) + ' dB');
                setText('vtcProfileName', p.profile_name || name);
                vtcToast('Voice calibrated: ' + (p.voice_type || 'unknown'), 'success');
            } else {
                vtcToast(d.error || 'Calibration failed', 'error');
            }
        }).catch(function(e) {
            resetBtn(btn, '<i class="fas fa-crosshairs"></i> Calibrate');
            vtcToast('Calibration error: ' + e.message, 'error');
        });
    };

    /* ── Analyze My Voice ────────────────────────────────────────── */
    window.vtcAnalyze = function() {
        var btn = document.getElementById('vtcAnalyzeBtn');
        if (btn) { btn.disabled = true; btn.innerHTML = '<span class="vtc-spin"></span> Analyzing...'; }
        var resultDiv = document.getElementById('vtcAnalysisResult');

        vtcFetch('/api/v1/voictune/ai/analyze', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: '{}'
        }).then(function(d) {
            resetBtn(btn, '<i class="fas fa-microscope"></i> Analyze My Voice');
            if (!resultDiv) return;
            if (d.ok) {
                var html = '<strong>Voice Classification:</strong> ' + esc(d.voice_classification || '--');
                html += '<br><br>' + esc(d.analysis || '');
                if (d.suggestions && d.suggestions.length > 0) {
                    html += '<ul class="suggestions">';
                    for (var i = 0; i < d.suggestions.length; i++) {
                        html += '<li>' + esc(d.suggestions[i]) + '</li>';
                    }
                    html += '</ul>';
                }
                html += '<br><small style="color:var(--muted)">Latency: ' + (d.latency_ms || 0) + 'ms</small>';
                resultDiv.innerHTML = html;
                resultDiv.style.display = 'block';
            } else {
                resultDiv.innerHTML = '<span style="color:var(--red)">' + esc(d.error || 'Analysis failed') + '</span>';
                resultDiv.style.display = 'block';
            }
        }).catch(function(e) {
            resetBtn(btn, '<i class="fas fa-microscope"></i> Analyze My Voice');
            if (resultDiv) {
                resultDiv.innerHTML = '<span style="color:var(--red)">Error: ' + esc(e.message) + '</span>';
                resultDiv.style.display = 'block';
            }
        });
    };

    /* ── Suggest EQ ──────────────────────────────────────────────── */
    window.vtcSuggestEq = function() {
        var btn = document.getElementById('vtcEqBtn');
        if (btn) { btn.disabled = true; btn.innerHTML = '<span class="vtc-spin"></span> Thinking...'; }
        var resultDiv = document.getElementById('vtcEqResult');

        vtcFetch('/api/v1/voictune/ai/suggest-eq', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: '{}'
        }).then(function(d) {
            resetBtn(btn, '<i class="fas fa-sliders-h"></i> Suggest EQ');
            if (!resultDiv) return;
            if (d.ok) {
                var html = '';
                if (d.bands && d.bands.length > 0) {
                    html += '<table class="vtc-eq-table"><thead><tr><th>Freq</th><th>Gain</th><th>Q</th><th>Type</th></tr></thead><tbody>';
                    for (var i = 0; i < d.bands.length; i++) {
                        var b = d.bands[i];
                        html += '<tr><td>' + (b.freq_hz || '--') + ' Hz</td><td>' + (b.gain_db || 0) + ' dB</td><td>' + (b.q || '--') + '</td><td>' + esc(b.type || '--') + '</td></tr>';
                    }
                    html += '</tbody></table>';
                }
                if (d.rationale) {
                    html += '<div class="vtc-eq-rationale">' + esc(d.rationale) + '</div>';
                }
                html += '<br><small style="color:var(--muted)">Latency: ' + (d.latency_ms || 0) + 'ms</small>';
                resultDiv.innerHTML = html;
                resultDiv.style.display = 'block';
            } else {
                resultDiv.innerHTML = '<span style="color:var(--red)">' + esc(d.error || 'EQ suggestion failed') + '</span>';
                resultDiv.style.display = 'block';
            }
        }).catch(function(e) {
            resetBtn(btn, '<i class="fas fa-sliders-h"></i> Suggest EQ');
            if (resultDiv) {
                resultDiv.innerHTML = '<span style="color:var(--red)">Error: ' + esc(e.message) + '</span>';
                resultDiv.style.display = 'block';
            }
        });
    };

    /* ── AI Coach Chat ───────────────────────────────────────────── */
    window.vtcChatSend = function() {
        var input = document.getElementById('vtcChatInput');
        var btn = document.getElementById('vtcChatSendBtn');
        var history = document.getElementById('vtcChatHistory');
        if (!input || !input.value.trim()) return;

        var msg = input.value.trim();
        input.value = '';
        if (btn) btn.disabled = true;

        /* Append user message to chat */
        appendChat('user', msg);

        vtcFetch('/api/v1/voictune/ai/coach', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({message: msg})
        }).then(function(d) {
            if (btn) btn.disabled = false;
            if (d.ok && d.response) {
                appendChat('ai', d.response);
            } else {
                appendChat('ai', 'Error: ' + (d.error || 'No response'));
            }
        }).catch(function(e) {
            if (btn) btn.disabled = false;
            appendChat('ai', 'Error: ' + e.message);
        });
    };

    function appendChat(role, text) {
        var history = document.getElementById('vtcChatHistory');
        if (!history) return;
        var div = document.createElement('div');
        div.className = 'vtc-chat-msg ' + role;
        div.innerHTML = '<div class="sender">' + (role === 'user' ? 'You' : 'AI Coach') + '</div>' + esc(text);
        history.appendChild(div);
        history.scrollTop = history.scrollHeight;
    }

    /* ── Helpers ──────────────────────────────────────────────────── */
    function esc(s) {
        if (s === null || s === undefined) return '';
        return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;').replace(/'/g,'&#39;');
    }
    function setText(id, val) {
        var el = document.getElementById(id);
        if (el) el.textContent = val;
    }
    function fmtDb(v) {
        if (v === null || v === undefined || v <= -90) return '--';
        return Number(v).toFixed(1);
    }
    function fmtHz(v) {
        if (!v || v <= 0) return '-- Hz';
        return Number(v).toFixed(1) + ' Hz';
    }
    function resetBtn(btn, html) {
        if (btn) { btn.disabled = false; btn.innerHTML = html; }
    }
    function vtcToast(msg, type) {
        /* Use mc1Toast if available (when loaded inside encoder UI), otherwise console */
        if (typeof mc1Toast === 'function') {
            mc1Toast(msg, type);
        } else {
            console.log('[VoicTune] ' + type + ': ' + msg);
        }
    }

    /* ── Init ─────────────────────────────────────────────────────── */
    document.addEventListener('DOMContentLoaded', function() {
        vtcPollProfile();
        vtcPollTips();
        vtcProfilePollId = setInterval(vtcPollProfile, 10000);
        vtcTipsPollId = setInterval(vtcPollTips, 5000);
    });
})();
</script>
