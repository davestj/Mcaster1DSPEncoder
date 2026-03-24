<?php
/**
 * request-widget.php — Public Song Request & Dedication Widget
 *
 * File:    src/linux/web_ui/request-widget.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Purpose: We provide a clean, mobile-friendly public page for listeners to submit
 *          song requests and dedications. No auth required. Rate-limited by IP.
 *          Includes a simple math CAPTCHA (no external dependencies).
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We use first-person plural in all comments
 *  - This is a PUBLIC page — no header.php/footer.php (admin dark theme)
 *  - Light, clean theme suitable for public-facing use
 */

/* We generate a simple math CAPTCHA challenge */
$ca = rand(2, 15);
$cb = rand(1, 10);
$captcha_expected = $ca + $cb;
$captcha_question = "$ca + $cb";
?>
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Song Request — Mcaster1 Radio</title>
<link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.5.1/css/all.min.css" crossorigin="anonymous" referrerpolicy="no-referrer">
<style>
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;font-size:15px;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px;color:#333}
.widget-container{background:#fff;border-radius:16px;box-shadow:0 20px 60px rgba(0,0,0,.2);max-width:520px;width:100%;overflow:hidden}
.widget-header{background:linear-gradient(135deg,#1e293b 0%,#334155 100%);color:#fff;padding:24px 28px;text-align:center}
.widget-header h1{font-size:20px;font-weight:700;margin-bottom:4px;display:flex;align-items:center;justify-content:center;gap:10px}
.widget-header p{font-size:13px;color:rgba(255,255,255,.7);margin:0}
.widget-body{padding:24px 28px}
.tabs{display:flex;border-bottom:2px solid #e2e8f0;margin-bottom:20px}
.tab-btn{flex:1;padding:10px;background:none;border:none;font-size:14px;font-weight:600;color:#94a3b8;cursor:pointer;border-bottom:2px solid transparent;margin-bottom:-2px;transition:all .2s}
.tab-btn:hover{color:#475569}
.tab-btn.active{color:#6366f1;border-bottom-color:#6366f1}
.tab-pane{display:none}.tab-pane.active{display:block}
.form-group{margin-bottom:16px}
.form-label{display:block;font-size:13px;font-weight:600;color:#475569;margin-bottom:5px}
.form-label .req{color:#ef4444}
.form-input,.form-textarea{width:100%;padding:10px 14px;border:1.5px solid #e2e8f0;border-radius:8px;font-size:14px;color:#1e293b;background:#f8fafc;outline:none;transition:border-color .2s,box-shadow .2s}
.form-input:focus,.form-textarea:focus{border-color:#6366f1;box-shadow:0 0 0 3px rgba(99,102,241,.12);background:#fff}
.form-textarea{resize:vertical;min-height:80px}
.form-row{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.captcha-row{display:flex;align-items:center;gap:12px;background:#f1f5f9;border-radius:8px;padding:12px 14px;margin-bottom:16px}
.captcha-q{font-size:16px;font-weight:700;color:#1e293b;white-space:nowrap}
.captcha-eq{font-size:16px;color:#64748b;margin:0 4px}
.captcha-input{width:70px;padding:8px 10px;border:1.5px solid #cbd5e1;border-radius:6px;font-size:16px;text-align:center;outline:none;background:#fff}
.captcha-input:focus{border-color:#6366f1;box-shadow:0 0 0 2px rgba(99,102,241,.15)}
.btn-submit{width:100%;padding:12px;background:linear-gradient(135deg,#6366f1 0%,#8b5cf6 100%);color:#fff;border:none;border-radius:10px;font-size:15px;font-weight:700;cursor:pointer;transition:transform .15s,box-shadow .15s;display:flex;align-items:center;justify-content:center;gap:8px}
.btn-submit:hover{transform:translateY(-1px);box-shadow:0 6px 20px rgba(99,102,241,.35)}
.btn-submit:active{transform:translateY(0)}
.btn-submit:disabled{opacity:.6;cursor:not-allowed;transform:none;box-shadow:none}
.msg{padding:12px 16px;border-radius:8px;font-size:14px;margin-bottom:16px;display:none;align-items:center;gap:8px}
.msg.ok{display:flex;background:#ecfdf5;border:1px solid #a7f3d0;color:#065f46}
.msg.err{display:flex;background:#fef2f2;border:1px solid #fecaca;color:#991b1b}
.powered{text-align:center;padding:14px;font-size:11px;color:#94a3b8;border-top:1px solid #f1f5f9}
.powered a{color:#6366f1;text-decoration:none}
@media(max-width:500px){.form-row{grid-template-columns:1fr}.widget-body{padding:18px 20px}.widget-header{padding:18px 20px}}
</style>
</head>
<body>

<div class="widget-container">
  <div class="widget-header">
    <h1><i class="fa-solid fa-music"></i> Song Request</h1>
    <p>Request a song or send a dedication to our DJ</p>
  </div>

  <div class="widget-body">
    <!-- We show result messages here -->
    <div class="msg" id="msg-ok"><i class="fa-solid fa-circle-check"></i> <span id="msg-ok-text"></span></div>
    <div class="msg" id="msg-err"><i class="fa-solid fa-circle-xmark"></i> <span id="msg-err-text"></span></div>

    <!-- We use tabs for Request vs Dedication -->
    <div class="tabs" id="rw-tabs">
      <button class="tab-btn active" data-tab="request" onclick="rwTab('request',this)">
        <i class="fa-solid fa-hand"></i> Song Request
      </button>
      <button class="tab-btn" data-tab="dedication" onclick="rwTab('dedication',this)">
        <i class="fa-solid fa-heart"></i> Dedication
      </button>
    </div>

    <!-- Song Request Form -->
    <div class="tab-pane active" id="tab-request">
      <div class="form-row">
        <div class="form-group">
          <label class="form-label">Your Name</label>
          <input class="form-input" id="rq-name" placeholder="Anonymous" maxlength="128">
        </div>
        <div class="form-group">
          <label class="form-label">Email <span style="color:#94a3b8;font-weight:400">(optional)</span></label>
          <input class="form-input" id="rq-email" type="email" placeholder="your@email.com" maxlength="255">
        </div>
      </div>
      <div class="form-group">
        <label class="form-label">Song Title <span class="req">*</span></label>
        <input class="form-input" id="rq-title" placeholder="Enter the song title" maxlength="255" required>
      </div>
      <div class="form-group">
        <label class="form-label">Artist <span style="color:#94a3b8;font-weight:400">(optional)</span></label>
        <input class="form-input" id="rq-artist" placeholder="Artist name" maxlength="255">
      </div>
      <div class="form-group">
        <label class="form-label">Message <span style="color:#94a3b8;font-weight:400">(optional)</span></label>
        <textarea class="form-textarea" id="rq-message" placeholder="Any special message for the DJ?" maxlength="500"></textarea>
      </div>

      <div class="captcha-row">
        <span class="captcha-q"><?= $captcha_question ?></span>
        <span class="captcha-eq">=</span>
        <input class="captcha-input" id="rq-captcha" type="number" placeholder="?" autocomplete="off">
        <input type="hidden" id="rq-captcha-expected" value="<?= $captcha_expected ?>">
      </div>

      <button class="btn-submit" id="rq-submit" onclick="submitRequest()">
        <i class="fa-solid fa-paper-plane"></i> Submit Request
      </button>
    </div>

    <!-- Dedication Form -->
    <div class="tab-pane" id="tab-dedication">
      <div class="form-group">
        <label class="form-label">Your Name <span class="req">*</span></label>
        <input class="form-input" id="dd-name" placeholder="Your name" maxlength="128" required>
      </div>
      <div class="form-group">
        <label class="form-label">Dedicated To <span class="req">*</span></label>
        <input class="form-input" id="dd-to" placeholder="Who is this dedication for?" maxlength="255" required>
      </div>
      <div class="form-group">
        <label class="form-label">Your Message <span class="req">*</span></label>
        <textarea class="form-textarea" id="dd-message" placeholder="Write your dedication message..." maxlength="1000" required></textarea>
      </div>
      <div class="form-group">
        <label class="form-label">Song Title <span style="color:#94a3b8;font-weight:400">(optional)</span></label>
        <input class="form-input" id="dd-title" placeholder="Request a specific song" maxlength="255">
      </div>

      <div class="captcha-row">
        <span class="captcha-q"><?= $captcha_question ?></span>
        <span class="captcha-eq">=</span>
        <input class="captcha-input" id="dd-captcha" type="number" placeholder="?" autocomplete="off">
        <input type="hidden" id="dd-captcha-expected" value="<?= $captcha_expected ?>">
      </div>

      <button class="btn-submit" id="dd-submit" onclick="submitDedication()">
        <i class="fa-solid fa-heart"></i> Send Dedication
      </button>
    </div>
  </div>

  <div class="powered">
    Powered by <a href="https://mcaster1.com" target="_blank">Mcaster1 DSP Encoder</a>
  </div>
</div>

<script>
(function(){

  /* We handle tab switching */
  window.rwTab = function(tab, btn) {
    document.querySelectorAll('#rw-tabs .tab-btn').forEach(function(b){ b.classList.remove('active'); });
    btn.classList.add('active');
    document.querySelectorAll('.tab-pane').forEach(function(p){ p.classList.remove('active'); });
    var pane = document.getElementById('tab-' + tab);
    if (pane) pane.classList.add('active');
    hideMsg();
  };

  function showMsg(type, text) {
    var okEl  = document.getElementById('msg-ok');
    var errEl = document.getElementById('msg-err');
    okEl.style.display  = 'none';
    errEl.style.display = 'none';
    if (type === 'ok') {
      document.getElementById('msg-ok-text').textContent = text;
      okEl.style.display = 'flex';
    } else {
      document.getElementById('msg-err-text').textContent = text;
      errEl.style.display = 'flex';
    }
  }

  function hideMsg() {
    document.getElementById('msg-ok').style.display  = 'none';
    document.getElementById('msg-err').style.display = 'none';
  }

  function apiPost(data) {
    return fetch('/app/api/requests.php', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(data)
    }).then(function(r) { return r.json(); });
  }

  window.submitRequest = function() {
    var title   = document.getElementById('rq-title').value.trim();
    var captcha = document.getElementById('rq-captcha').value.trim();
    var expected = document.getElementById('rq-captcha-expected').value;

    if (!title) { showMsg('err', 'Please enter a song title.'); return; }
    if (!captcha) { showMsg('err', 'Please answer the math question.'); return; }

    var btn = document.getElementById('rq-submit');
    btn.disabled = true;
    btn.innerHTML = '<i class="fa-solid fa-spinner fa-spin"></i> Submitting...';

    apiPost({
      action:           'submit',
      listener_name:    document.getElementById('rq-name').value.trim(),
      listener_email:   document.getElementById('rq-email').value.trim(),
      track_title:      title,
      track_artist:     document.getElementById('rq-artist').value.trim(),
      message:          document.getElementById('rq-message').value.trim(),
      captcha_answer:   parseInt(captcha),
      captcha_expected: parseInt(expected)
    }).then(function(d) {
      btn.disabled = false;
      btn.innerHTML = '<i class="fa-solid fa-paper-plane"></i> Submit Request';
      if (d.ok) {
        showMsg('ok', d.message || 'Request submitted!');
        document.getElementById('rq-title').value   = '';
        document.getElementById('rq-artist').value  = '';
        document.getElementById('rq-message').value = '';
        document.getElementById('rq-captcha').value = '';
      } else {
        showMsg('err', d.error || 'Failed to submit request.');
      }
    }).catch(function() {
      btn.disabled = false;
      btn.innerHTML = '<i class="fa-solid fa-paper-plane"></i> Submit Request';
      showMsg('err', 'Network error. Please try again.');
    });
  };

  window.submitDedication = function() {
    var name    = document.getElementById('dd-name').value.trim();
    var to      = document.getElementById('dd-to').value.trim();
    var message = document.getElementById('dd-message').value.trim();
    var captcha = document.getElementById('dd-captcha').value.trim();
    var expected = document.getElementById('dd-captcha-expected').value;

    if (!name || !to || !message) { showMsg('err', 'Name, recipient, and message are all required.'); return; }
    if (!captcha) { showMsg('err', 'Please answer the math question.'); return; }

    var btn = document.getElementById('dd-submit');
    btn.disabled = true;
    btn.innerHTML = '<i class="fa-solid fa-spinner fa-spin"></i> Submitting...';

    apiPost({
      action:           'submit_dedication',
      listener_name:    name,
      dedication_to:    to,
      message:          message,
      track_title:      document.getElementById('dd-title').value.trim(),
      captcha_answer:   parseInt(captcha),
      captcha_expected: parseInt(expected)
    }).then(function(d) {
      btn.disabled = false;
      btn.innerHTML = '<i class="fa-solid fa-heart"></i> Send Dedication';
      if (d.ok) {
        showMsg('ok', d.message || 'Dedication submitted!');
        document.getElementById('dd-to').value      = '';
        document.getElementById('dd-message').value = '';
        document.getElementById('dd-title').value   = '';
        document.getElementById('dd-captcha').value = '';
      } else {
        showMsg('err', d.error || 'Failed to submit dedication.');
      }
    }).catch(function() {
      btn.disabled = false;
      btn.innerHTML = '<i class="fa-solid fa-heart"></i> Send Dedication';
      showMsg('err', 'Network error. Please try again.');
    });
  };

})();
</script>
</body>
</html>
