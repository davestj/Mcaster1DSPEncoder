<?php
if (!defined('MC1_BOOT')) {
    http_response_code(403);
    echo '403 Forbidden';
    return;
}
?>
</main><!-- /main -->
</div><!-- /layout -->

<!-- Toast -->
<div id="mc1-toast" style="display:none;position:fixed;bottom:20px;right:20px;z-index:9999;
  background:#1e293b;border:1px solid #334155;border-radius:8px;padding:10px 16px;
  align-items:center;gap:10px;box-shadow:0 4px 20px rgba(0,0,0,.5);
  font-size:13px;color:#e2e8f0;max-width:380px">
  <i id="mc1-toast-icon" class="fa-solid fa-circle-check" style="font-size:16px;flex-shrink:0;color:#14b8a6"></i>
  <span id="mc1-toast-msg"></span>
</div>

<script>
(function(){

  /* ── Client-side state persistence (localStorage, survives navigation + logout/login) ── */
  window.mc1State = (function(){
    var P = 'mc1.';
    function k(p, n) { return P + p + '.' + n; }
    return {
      get: function(page, name, def) {
        try {
          var v = localStorage.getItem(k(page, name));
          return v !== null ? JSON.parse(v) : def;
        } catch(e) { return def; }
      },
      set: function(page, name, val) {
        try { localStorage.setItem(k(page, name), JSON.stringify(val)); } catch(e) {}
      },
      del: function(page, name) {
        try { localStorage.removeItem(k(page, name)); } catch(e) {}
      }
    };
  })();

  /* ── Toast ── */
  var _toastTimer = null;
  window.mc1Toast = function(msg, type) {
    var el  = document.getElementById('mc1-toast');
    var ico = document.getElementById('mc1-toast-icon');
    var txt = document.getElementById('mc1-toast-msg');
    if (!el) return;
    ico.className = type === 'err'
      ? 'fa-solid fa-circle-xmark'
      : type === 'warn'
        ? 'fa-solid fa-triangle-exclamation'
        : 'fa-solid fa-circle-check';
    ico.style.color = type === 'err' ? '#ef4444' : type === 'warn' ? '#eab308' : '#14b8a6';
    txt.textContent = msg;
    el.style.display = 'flex';
    if (_toastTimer) clearTimeout(_toastTimer);
    _toastTimer = setTimeout(function(){ el.style.display = 'none'; }, 4000);
  };

  /* ── JSON API fetch wrapper ── */
  window.mc1Api = function(method, url, body) {
    var opts = {
      method: method,
      headers: {'Content-Type': 'application/json'},
      credentials: 'same-origin'
    };
    if (body !== undefined && body !== null) opts.body = JSON.stringify(body);
    return fetch(url, opts).then(function(r){
      return r.json().then(function(d){ d._status = r.status; return d; });
    });
  };

  /* ── Logout ── */
  window.mc1Logout = function() {
    mc1Api('POST', '/app/api/auth.php', {action:'logout'}).then(function(){
      mc1Api('POST', '/api/v1/auth/logout').then(function(){
        window.location.href = '/login';
      }).catch(function(){ window.location.href = '/login'; });
    }).catch(function(){
      mc1Api('POST', '/api/v1/auth/logout').finally(function(){
        window.location.href = '/login';
      });
    });
  };

  /* ── Auto-bootstrap PHP app session ── */
  (function(){
    // mc1app_session is httponly so not visible to JS; use sessionStorage flag
    if (sessionStorage.getItem('mc1php_ok')) return;
    mc1Api('POST', '/app/api/auth.php', {action:'auto_login'}).then(function(d){
      if (d && d.ok) {
        sessionStorage.setItem('mc1php_ok', '1');
        if (d.bootstrapped && (window.location.pathname.indexOf('settings') !== -1 ||
          window.location.pathname.indexOf('profile') !== -1)) {
          window.location.reload();
        }
      }
    }).catch(function(){});
  })();

  /* ── Confirm-delete buttons ── */
  document.querySelectorAll('[data-confirm]').forEach(function(el){
    el.addEventListener('click', function(e){
      if (!confirm(el.dataset.confirm)) e.preventDefault();
    });
  });

  /* ── Tab switching (auto-init) ── */
  document.querySelectorAll('.tabs').forEach(function(tabs){
    tabs.querySelectorAll('.tab-btn').forEach(function(btn){
      btn.addEventListener('click', function(){
        var target = btn.dataset.tab;
        tabs.querySelectorAll('.tab-btn').forEach(function(b){ b.classList.remove('active'); });
        btn.classList.add('active');
        var container = tabs.parentElement || document;
        container.querySelectorAll('.tab-pane').forEach(function(p){ p.classList.remove('active'); });
        var pane = container.querySelector('#tab-' + target);
        if (pane) pane.classList.add('active');
      });
    });
  });

  /* ── DNAS status pill ── */
  function checkDnas() {
    mc1Api('GET', '/api/v1/dnas/stats').then(function(d){
      var dot = document.getElementById('dnas-dot');
      var txt = document.getElementById('dnas-txt');
      if (!dot || !txt) return;
      // d.ok = true means DNAS responded (body contains XML)
      if (d && d.ok) {
        // Parse listener count from XML body (Icecast-style <listeners>N</listeners>)
        var listeners = 0;
        if (d.body) {
          var m = d.body.match(/<listeners>(\d+)<\/listeners>/i)
               || d.body.match(/<total_listeners>(\d+)<\/total_listeners>/i)
               || d.body.match(/"total_listeners"\s*:\s*(\d+)/);
          if (m) listeners = parseInt(m[1]);
        }
        if (d.total_listeners !== undefined) listeners = d.total_listeners;
        dot.className = 'dnas-dot live';
        txt.textContent = 'DNAS \u2022 ' + listeners + ' listener' + (listeners === 1 ? '' : 's');
      } else {
        dot.className = 'dnas-dot err';
        txt.textContent = 'DNAS offline';
      }
    }).catch(function(){
      var dot = document.getElementById('dnas-dot');
      var txt = document.getElementById('dnas-txt');
      if (dot) dot.className = 'dnas-dot err';
      if (txt) txt.textContent = 'DNAS offline';
    });
  }
  checkDnas();
  setInterval(checkDnas, 15000);

  /* ── Global live encoder status pill (topbar) — all users ── */
  (function(){
    var pill  = document.getElementById('enc-live-pill');
    var label = document.getElementById('enc-live-label');
    if (!pill || !label) return;
    function updateLivePill() {
      mc1Api('GET', '/api/v1/encoders').then(function(encoders) {
        if (!Array.isArray(encoders)) return;
        var liveCount = encoders.filter(function(e){ return (e.state||'').toLowerCase() === 'live'; }).length;
        var connCount = encoders.filter(function(e){ return (e.state||'').toLowerCase() === 'connecting' || (e.state||'').toLowerCase() === 'reconnecting'; }).length;
        var total = liveCount + connCount;
        if (liveCount > 0) {
          pill.style.display = 'flex';
          pill.style.background = 'rgba(20,184,166,.12)';
          pill.style.borderColor = 'rgba(20,184,166,.45)';
          pill.style.color = 'var(--teal)';
          label.textContent = liveCount + ' Live' + (connCount > 0 ? ' +' + connCount + ' Connecting' : '');
        } else if (connCount > 0) {
          pill.style.display = 'flex';
          pill.style.background = 'rgba(234,179,8,.1)';
          pill.style.borderColor = 'rgba(234,179,8,.35)';
          pill.style.color = '#eab308';
          label.textContent = connCount + ' Connecting…';
        } else {
          pill.style.display = 'none';
        }
      }).catch(function(){ pill.style.display = 'none'; });
    }
    updateLivePill();
    setInterval(updateLivePill, 8000);
  })();

  /* ── Close sidebar on outside click (mobile) ── */
  document.addEventListener('click', function(e){
    var sb = document.querySelector('.sidebar');
    var hb = document.querySelector('.hamburger');
    if (sb && sb.classList.contains('open') && !sb.contains(e.target) && hb && !hb.contains(e.target))
      sb.classList.remove('open');
  });
})();
</script>
</body>
</html>
