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

  /* ── i18n JS helper ── */
  window.mc1i18n = {
    strings: <?= json_encode(function_exists('mc1_i18n_strings') ? mc1_i18n_strings() : new stdClass()) ?>,
    lang: '<?= function_exists('mc1_i18n_lang') ? mc1_i18n_lang() : 'en' ?>',
    t: function(key, replacements) {
      var str = this.strings[key] || key;
      if (replacements) {
        for (var k in replacements) {
          if (replacements.hasOwnProperty(k)) {
            str = str.replace('{' + k + '}', replacements[k]);
          }
        }
      }
      return str;
    }
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

  /* ── Active Users: Heartbeat + Presence ── */
  var _mc1ChatTarget = null;   // { user_id, username, display_name }
  var _mc1PrevUserIds = {};    // track who was seen last poll for "new join" pulse
  var _mc1UnreadCounts = {};   // user_id → unread count

  function _mc1GetPage() {
    return (window.location.pathname.replace(/\.php$/, '').replace(/^\//, '') || 'dashboard');
  }

  function _mc1Heartbeat() {
    mc1Api('POST', '/app/api/auth.php', {action:'heartbeat', page: _mc1GetPage()}).catch(function(){});
  }

  function _mc1RoleClass(canAdmin, roleName) {
    if (canAdmin) return 'admin';
    if (roleName === 'guest') return 'guest';
    return 'user';
  }

  function _mc1Initial(name) {
    return (name || '?').charAt(0).toUpperCase();
  }

  function _mc1EscHtml(s) {
    var d = document.createElement('div');
    d.textContent = s;
    return d.innerHTML;
  }

  function _mc1PageLabel(page) {
    if (!page) return '';
    return page.replace(/[-_]/g, ' ').replace(/\b\w/g, function(c){ return c.toUpperCase(); });
  }

  function _mc1PollActiveUsers() {
    mc1Api('POST', '/app/api/auth.php', {action:'active_users'}).then(function(d) {
      if (!d || !d.ok) return;
      var users = d.users || [];
      _mc1RenderAvatarStrip(users);
      _mc1RenderUserList(users);
      _mc1CheckCollabNotice(users);
      // Track new joins for pulse animation
      var newIds = {};
      for (var i = 0; i < users.length; i++) newIds[users[i].user_id] = true;
      _mc1PrevUserIds = newIds;
    }).catch(function(){});
  }

  function _mc1PollMessages() {
    mc1Api('POST', '/app/api/auth.php', {action:'get_messages'}).then(function(d) {
      if (!d || !d.ok) return;
      // Update unread counts per sender
      _mc1UnreadCounts = {};
      var msgs = d.messages || [];
      for (var i = 0; i < msgs.length; i++) {
        var fid = msgs[i].from_user_id;
        _mc1UnreadCounts[fid] = (_mc1UnreadCounts[fid] || 0) + 1;
      }
      // Show toast for new messages
      if (d.unread_count > 0) {
        var latestMsg = msgs[0];
        if (latestMsg && !latestMsg.is_mine) {
          var msgAge = (Date.now() - new Date(latestMsg.created_at).getTime());
          if (msgAge < 35000) {
            mc1Toast((latestMsg.from_display_name || latestMsg.from_username) + ': ' + latestMsg.message.substring(0, 80), 'info');
          }
        }
      }
      // Re-render avatar strip to update unread badges
      _mc1PollActiveUsers();
      // If chat open, refresh conversation
      if (_mc1ChatTarget) _mc1LoadConversation(_mc1ChatTarget.user_id);
    }).catch(function(){});
  }

  function _mc1RenderAvatarStrip(users) {
    var strip = document.getElementById('active-users-strip');
    if (!strip) return;
    // Filter out self
    var others = [];
    for (var i = 0; i < users.length; i++) {
      if (!users[i].is_self) others.push(users[i]);
    }
    if (others.length === 0) {
      strip.innerHTML = '';
      return;
    }
    var html = '';
    var max = 5;
    var shown = Math.min(others.length, max);
    for (var i = 0; i < shown; i++) {
      var u = others[i];
      var cls = _mc1RoleClass(u.can_admin, u.role_name);
      var isNew = !_mc1PrevUserIds[u.user_id];
      var unread = _mc1UnreadCounts[u.user_id] || 0;
      html += '<div class="au-avatar ' + cls + '" onclick="mc1ShowUserPopover(' + u.user_id + ')" title="' + _mc1EscHtml(u.display_name) + '">';
      html += _mc1Initial(u.display_name);
      if (isNew) html += '<span class="au-avatar-badge"></span>';
      if (unread > 0) html += '<span class="au-unread">' + (unread > 9 ? '9+' : unread) + '</span>';
      html += '<span class="au-tooltip">' + _mc1EscHtml(u.display_name) + ' &mdash; ' + _mc1EscHtml(_mc1PageLabel(u.current_page)) + '</span>';
      html += '</div>';
    }
    if (others.length > max) {
      html += '<div class="au-overflow" onclick="mc1ToggleUsersPopover()">+' + (others.length - max) + '</div>';
    }
    strip.innerHTML = html;
  }

  function _mc1RenderUserList(users) {
    var list = document.getElementById('active-users-list');
    if (!list) return;
    if (users.length === 0) {
      list.innerHTML = '<div style="padding:16px;text-align:center;color:var(--muted);font-size:12px">No active users</div>';
      return;
    }
    var html = '';
    for (var i = 0; i < users.length; i++) {
      var u = users[i];
      var cls = _mc1RoleClass(u.can_admin, u.role_name);
      var unread = _mc1UnreadCounts[u.user_id] || 0;
      html += '<div style="display:flex;align-items:center;gap:10px;padding:7px 14px;cursor:pointer;transition:background .1s" onmouseover="this.style.background=\'rgba(255,255,255,.04)\'" onmouseout="this.style.background=\'transparent\'">';
      html += '<div class="au-avatar ' + cls + '" style="margin-left:0;width:30px;height:30px;font-size:11px">' + _mc1Initial(u.display_name) + '</div>';
      html += '<div style="flex:1;min-width:0">';
      html += '<div style="font-size:12px;font-weight:600;color:var(--text)">' + _mc1EscHtml(u.display_name);
      if (u.is_self) html += ' <span style="font-size:9px;color:var(--muted)">(you)</span>';
      html += '</div>';
      html += '<div style="font-size:10px;color:var(--muted)">' + _mc1EscHtml(_mc1PageLabel(u.current_page)) + '</div>';
      html += '</div>';
      if (!u.is_self) {
        html += '<button onclick="event.stopPropagation();mc1OpenChat(' + u.user_id + ',' + _mc1EscHtml(JSON.stringify(u.username)) + ',' + _mc1EscHtml(JSON.stringify(u.display_name)) + ',' + (u.can_admin ? 'true' : 'false') + ')" style="background:rgba(255,255,255,.06);border:1px solid var(--border);border-radius:var(--radius-sm);padding:3px 8px;color:var(--text-dim);font-size:10px;cursor:pointer;white-space:nowrap" title="Send message"><i class="fa-solid fa-comment"></i>';
        if (unread > 0) html += ' <span style="color:var(--red);font-weight:700">' + unread + '</span>';
        html += '</button>';
      }
      html += '</div>';
    }
    list.innerHTML = html;
  }

  function _mc1CheckCollabNotice(users) {
    var notice = document.getElementById('collab-notice');
    if (!notice) return;
    var myPage = _mc1GetPage();
    var samePageUsers = [];
    for (var i = 0; i < users.length; i++) {
      if (!users[i].is_self && users[i].current_page === myPage) {
        samePageUsers.push(users[i]);
      }
    }
    if (samePageUsers.length === 0) {
      notice.style.display = 'none';
      return;
    }
    var names = [];
    for (var i = 0; i < samePageUsers.length; i++) {
      names.push(samePageUsers[i].display_name);
    }
    var pageLabel = _mc1PageLabel(myPage);
    // Special warnings for control pages
    var isControlPage = /^(encoders|daw|producer|mixer|crossfader)/.test(myPage);
    var icon = isControlPage ? '<i class="fa-solid fa-triangle-exclamation" style="margin-right:4px"></i>' : '<i class="fa-solid fa-users" style="margin-right:4px"></i>';
    var verb = isControlPage ? 'is also controlling' : 'is also viewing';
    notice.innerHTML = icon + _mc1EscHtml(names.join(', ')) + ' ' + verb + ' ' + _mc1EscHtml(pageLabel);
    notice.style.display = 'block';
    notice.style.color = isControlPage ? 'var(--yellow)' : 'var(--text-dim)';
    notice.style.background = isControlPage ? 'rgba(234,179,8,.06)' : 'rgba(255,255,255,.02)';
  }

  window.mc1ToggleUsersPopover = function() {
    var pop = document.getElementById('active-users-popover');
    if (!pop) return;
    pop.style.display = pop.style.display === 'none' ? 'block' : 'none';
  };

  window.mc1ShowUserPopover = function(userId) {
    var pop = document.getElementById('active-users-popover');
    if (!pop) return;
    pop.style.display = 'block';
  };

  window.mc1OpenChat = function(userId, username, displayName, canAdmin) {
    _mc1ChatTarget = {user_id: userId, username: username, display_name: displayName, can_admin: canAdmin};
    var section = document.getElementById('chat-section');
    var avatarEl = document.getElementById('chat-target-avatar');
    var nameEl = document.getElementById('chat-target-name');
    if (!section) return;
    section.style.display = 'block';
    if (avatarEl) {
      avatarEl.className = 'au-avatar ' + (canAdmin ? 'admin' : 'user');
      avatarEl.style.marginLeft = '0';
      avatarEl.textContent = _mc1Initial(displayName);
    }
    if (nameEl) nameEl.textContent = displayName;
    var pop = document.getElementById('active-users-popover');
    if (pop) pop.style.display = 'block';
    _mc1LoadConversation(userId);
    var input = document.getElementById('chat-input');
    if (input) input.focus();
  };

  window.mc1CloseChat = function() {
    _mc1ChatTarget = null;
    var section = document.getElementById('chat-section');
    if (section) section.style.display = 'none';
  };

  function _mc1LoadConversation(userId) {
    mc1Api('POST', '/app/api/auth.php', {action:'get_messages', with_user_id: userId}).then(function(d) {
      if (!d || !d.ok) return;
      var container = document.getElementById('chat-messages');
      if (!container) return;
      var msgs = (d.messages || []).reverse(); // oldest first
      if (msgs.length === 0) {
        container.innerHTML = '<div style="text-align:center;color:var(--muted);font-size:11px;padding:20px">No messages yet</div>';
        return;
      }
      var html = '';
      for (var i = 0; i < msgs.length; i++) {
        var m = msgs[i];
        var align = m.is_mine ? 'flex-end' : 'flex-start';
        var bg = m.is_mine ? 'rgba(20,184,166,.15)' : 'rgba(255,255,255,.06)';
        var tc = m.is_mine ? 'var(--teal)' : 'var(--text)';
        var time = '';
        try {
          var dt = new Date(m.created_at);
          time = dt.toLocaleTimeString([], {hour:'2-digit',minute:'2-digit'});
        } catch(e){}
        html += '<div style="align-self:' + align + ';max-width:85%">';
        html += '<div style="background:' + bg + ';color:' + tc + ';padding:5px 10px;border-radius:8px;font-size:12px;word-break:break-word">' + _mc1EscHtml(m.message) + '</div>';
        html += '<div style="font-size:9px;color:var(--muted);margin-top:1px;text-align:' + (m.is_mine ? 'right' : 'left') + '">' + time + '</div>';
        html += '</div>';
      }
      container.innerHTML = html;
      container.scrollTop = container.scrollHeight;
    }).catch(function(){});
  }

  window.mc1SendChat = function() {
    if (!_mc1ChatTarget) return;
    var input = document.getElementById('chat-input');
    if (!input) return;
    var msg = input.value.trim();
    if (msg === '') return;
    input.value = '';
    mc1Api('POST', '/app/api/auth.php', {action:'send_message', to_user_id: _mc1ChatTarget.user_id, message: msg}).then(function(d) {
      if (d && d.ok) {
        _mc1LoadConversation(_mc1ChatTarget.user_id);
      } else {
        mc1Toast('Failed to send message', 'err');
      }
    }).catch(function(){
      mc1Toast('Failed to send message', 'err');
    });
  };

  // Close popover on outside click
  document.addEventListener('click', function(e) {
    var pop = document.getElementById('active-users-popover');
    var strip = document.getElementById('active-users-strip');
    if (pop && pop.style.display !== 'none' && !pop.contains(e.target) && strip && !strip.contains(e.target)) {
      pop.style.display = 'none';
    }
  });

  // Start heartbeat + polling on DOMContentLoaded
  document.addEventListener('DOMContentLoaded', function() {
    _mc1Heartbeat();
    _mc1PollActiveUsers();
    _mc1PollMessages();
    setInterval(_mc1Heartbeat, 30000);
    setInterval(_mc1PollActiveUsers, 30000);
    setInterval(_mc1PollMessages, 30000);
  });

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
