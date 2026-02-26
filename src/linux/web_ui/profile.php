<?php
/**
 * profile.php — User Profile Page
 *
 * File:    src/linux/web_ui/profile.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-02-24
 * Purpose: We show the current user's profile: display name, email, bio,
 *          active sessions with session expiry info, and password change.
 *          Admins also see a per-user session TTL override control.
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We use h() on all user-derived HTML output
 *  - We use first-person plural in all comments
 *
 * CHANGELOG:
 *  2026-02-24  v1.0.0  Initial implementation
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/app/inc/auth.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/user_auth.php';
require_once __DIR__ . '/app/inc/logger.php';
require_once __DIR__ . '/app/inc/profile.usr.class.php';

$page_title = 'My Profile';
$active_nav = 'profile';
$use_charts = false;

/* We redirect to login if no PHP session yet — footer.php will bridge it */
$user = mc1_current_user();

/* We preload profile and session expiry for the page */
$profile       = $user ? Mc1UserProfile::getProfile((int)$user['id']) : null;
$current_hash  = '';
$cookie_val    = $_COOKIE['mc1app_session'] ?? '';
if ($cookie_val !== '') $current_hash = mc1_hash_token($cookie_val);
$session_expiry = $profile ? Mc1UserProfile::getSessionExpiry($current_hash) : null;

require_once __DIR__ . '/app/inc/header.php';
?>

<?php if ($user && $profile): ?>

<?php
/* We show a session expiry warning if the session expires in <= 7 days */
$warn_expiry = $session_expiry && !$session_expiry['never_expires']
              && $session_expiry['days_remaining'] <= 7;
if ($warn_expiry): ?>
<div class="alert alert-warn" style="margin-bottom:14px">
  <i class="fa-solid fa-triangle-exclamation"></i>
  Your login session expires in <strong><?= (int)$session_expiry['days_remaining'] ?> day<?= $session_expiry['days_remaining'] === 1 ? '' : 's' ?></strong>
  (<?= h($session_expiry['expires_at']) ?>).
  Contact an administrator to extend it, or log out and back in to renew.
</div>
<?php endif; ?>

<!-- Profile header -->
<div class="card" style="padding:20px 24px;display:flex;align-items:center;gap:20px;flex-wrap:wrap">
  <div style="width:64px;height:64px;border-radius:50%;background:linear-gradient(135deg,var(--teal),var(--cyan));display:flex;align-items:center;justify-content:center;font-size:28px;font-weight:700;color:#fff;flex-shrink:0">
    <?= h(strtoupper(substr($profile['display_name'] ?: $profile['username'], 0, 1))) ?>
  </div>
  <div style="flex:1;min-width:200px">
    <div style="font-size:20px;font-weight:700;color:var(--text);line-height:1.2">
      <?= h($profile['display_name'] ?: $profile['username']) ?>
    </div>
    <div style="font-size:13px;color:var(--muted);margin-top:4px">
      @<?= h($profile['username']) ?> &mdash;
      <span class="badge badge-teal"><?= h($profile['role_label']) ?></span>
    </div>
    <?php if ($profile['bio']): ?>
    <div style="font-size:13px;color:var(--text-dim);margin-top:8px"><?= h($profile['bio']) ?></div>
    <?php endif; ?>
  </div>
  <div style="text-align:right;flex-shrink:0">
    <?php if ($session_expiry): ?>
    <div style="font-size:11px;color:var(--muted)">Session</div>
    <?php if ($session_expiry['never_expires']): ?>
    <div class="badge badge-teal" style="font-size:12px">Never expires</div>
    <?php else: ?>
    <div class="badge <?= $session_expiry['days_remaining'] <= 3 ? 'badge-red' : ($session_expiry['days_remaining'] <= 7 ? 'badge-orange' : 'badge-green') ?>" style="font-size:12px">
      <?= (int)$session_expiry['days_remaining'] ?>d remaining
    </div>
    <?php endif; ?>
    <?php endif; ?>
    <div style="font-size:11px;color:var(--muted);margin-top:4px">
      Member since <?= h(substr($profile['created_at'] ?? '', 0, 10)) ?>
    </div>
  </div>
</div>

<!-- Tabs -->
<div class="card" style="padding:0">
  <div class="tabs" style="padding:0 20px;margin-bottom:0;border-bottom:1px solid var(--border)">
    <button class="tab-btn active" data-tab="info" onclick="profileTab('info',this)"><i class="fa-solid fa-user fa-fw"></i> Profile Info</button>
    <button class="tab-btn" data-tab="password" onclick="profileTab('password',this)"><i class="fa-solid fa-key fa-fw"></i> Change Password</button>
    <button class="tab-btn" data-tab="sessions" onclick="profileTab('sessions',this)"><i class="fa-solid fa-laptop fa-fw"></i> Sessions</button>
    <?php if (!empty($user['can_admin'])): ?>
    <button class="tab-btn" data-tab="admin" onclick="profileTab('admin',this)"><i class="fa-solid fa-shield-halved fa-fw"></i> Admin</button>
    <?php endif; ?>
  </div>

  <!-- ── Tab: Profile Info ─────────────────────────────────────────────── -->
  <div id="tab-info" class="tab-pane active" style="padding:20px 24px">
    <div class="form-row form-row-2">
      <div class="form-group">
        <label class="form-label">Username</label>
        <input class="form-input" value="<?= h($profile['username']) ?>" disabled style="opacity:.6;cursor:not-allowed">
        <span class="form-hint">Username cannot be changed</span>
      </div>
      <div class="form-group">
        <label class="form-label">Display Name</label>
        <input class="form-input" id="pf-name" value="<?= h($profile['display_name'] ?? '') ?>" placeholder="Your full name">
      </div>
      <div class="form-group">
        <label class="form-label">Email Address</label>
        <input class="form-input" id="pf-email" type="email" value="<?= h($profile['email'] ?? '') ?>" placeholder="you@example.com">
      </div>
      <div class="form-group">
        <label class="form-label">Role</label>
        <input class="form-input" value="<?= h($profile['role_label']) ?>" disabled style="opacity:.6;cursor:not-allowed">
        <span class="form-hint">Role is assigned by an administrator</span>
      </div>
    </div>
    <div class="form-group">
      <label class="form-label">Bio</label>
      <textarea class="form-textarea" id="pf-bio" rows="3" placeholder="Tell us a little about yourself..."><?= h($profile['bio'] ?? '') ?></textarea>
    </div>
    <button class="btn btn-primary" onclick="profileSave()">
      <i class="fa-solid fa-check"></i> Save Profile
    </button>
  </div>

  <!-- ── Tab: Change Password ──────────────────────────────────────────── -->
  <div id="tab-password" class="tab-pane" style="padding:20px 24px">
    <div style="max-width:420px">
      <div class="form-group">
        <label class="form-label">Current Password</label>
        <input class="form-input" id="pw-old" type="password" placeholder="Enter your current password" autocomplete="current-password">
      </div>
      <div class="form-group">
        <label class="form-label">New Password</label>
        <input class="form-input" id="pw-new" type="password" placeholder="Minimum 8 characters" autocomplete="new-password">
      </div>
      <div class="form-group">
        <label class="form-label">Confirm New Password</label>
        <input class="form-input" id="pw-cfm" type="password" placeholder="Repeat new password" autocomplete="new-password">
      </div>
      <div id="pw-strength" style="margin-bottom:12px;font-size:12px;color:var(--muted);min-height:18px"></div>
      <button class="btn btn-primary" onclick="profileChangePw()">
        <i class="fa-solid fa-lock"></i> Update Password
      </button>
    </div>
  </div>

  <!-- ── Tab: Sessions ─────────────────────────────────────────────────── -->
  <div id="tab-sessions" class="tab-pane" style="padding:20px 24px">
    <div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:14px;flex-wrap:wrap;gap:8px">
      <div style="font-size:13px;color:var(--text-dim)">
        Active login sessions for your account
      </div>
      <button class="btn btn-warn btn-sm" onclick="profileRevokeAll()">
        <i class="fa-solid fa-shield-xmark"></i> Revoke All Other Sessions
      </button>
    </div>
    <div id="sessions-list"><div class="empty"><div class="spinner"></div></div></div>
  </div>

  <?php if (!empty($user['can_admin'])): ?>
  <!-- ── Tab: Admin (session TTL management) ───────────────────────────── -->
  <div id="tab-admin" class="tab-pane" style="padding:20px 24px">
    <div class="alert alert-info" style="margin-bottom:16px">
      <i class="fa-solid fa-circle-info"></i>
      As an administrator, you can override the session duration for any user.
      Setting to <strong>Never Expire</strong> means the user stays logged in until manually revoked.
    </div>
    <div id="admin-users-list"><div class="empty"><div class="spinner"></div></div></div>
  </div>
  <?php endif; ?>

</div><!-- .card -->

<?php else: ?>
<div class="card">
  <div class="empty">
    <i class="fa-solid fa-circle-user" style="font-size:32px;margin-bottom:12px;display:block;color:var(--border)"></i>
    <p>Loading profile… <span class="spinner" style="vertical-align:middle"></span></p>
    <p style="font-size:12px;color:var(--muted);margin-top:8px">If this persists, try refreshing the page.</p>
  </div>
</div>
<?php endif; ?>

<script>
(function(){

/* ── Tab switching ────────────────────────────────────────────────────── */
window.profileTab = function(name, btn) {
    document.querySelectorAll('.tab-pane').forEach(function(p){ p.classList.remove('active'); });
    document.querySelectorAll('.tab-btn').forEach(function(b){ b.classList.remove('active'); });
    var pane = document.getElementById('tab-' + name);
    if (pane) pane.classList.add('active');
    if (btn)  btn.classList.add('active');
    if (name === 'sessions') loadSessions();
    if (name === 'admin')    loadAdminUsers();
};

/* ── Save profile info ────────────────────────────────────────────────── */
window.profileSave = function() {
    var name  = document.getElementById('pf-name').value.trim();
    var email = document.getElementById('pf-email').value.trim();
    var bio   = document.getElementById('pf-bio').value.trim();

    if (!name) { mc1Toast('Display name is required', 'warn'); return; }

    mc1Api('POST', '/app/api/profile.php', {
        action: 'update',
        display_name: name,
        email: email,
        bio: bio
    }).then(function(d) {
        if (d.ok) {
            mc1Toast('Profile saved', 'ok');
            /* We update the topbar display name */
            var uname = document.querySelector('.topbar-uname');
            if (uname) uname.textContent = name;
        } else {
            mc1Toast(d.error || 'Save failed', 'err');
        }
    }).catch(function() { mc1Toast('Request failed', 'err'); });
};

/* ── Change password ──────────────────────────────────────────────────── */
window.profileChangePw = function() {
    var old_pw = document.getElementById('pw-old').value;
    var new_pw = document.getElementById('pw-new').value;
    var cfm_pw = document.getElementById('pw-cfm').value;

    if (!old_pw) { mc1Toast('Current password required', 'warn'); return; }
    if (new_pw.length < 8) { mc1Toast('New password must be at least 8 characters', 'warn'); return; }
    if (new_pw !== cfm_pw) { mc1Toast('New passwords do not match', 'warn'); return; }

    mc1Api('POST', '/app/api/profile.php', {
        action: 'change_password',
        old_password: old_pw,
        new_password: new_pw,
        confirm_password: cfm_pw
    }).then(function(d) {
        if (d.ok) {
            mc1Toast('Password updated — you may need to log in again on other devices', 'ok');
            document.getElementById('pw-old').value = '';
            document.getElementById('pw-new').value = '';
            document.getElementById('pw-cfm').value = '';
            document.getElementById('pw-strength').textContent = '';
        } else {
            mc1Toast(d.error || 'Password change failed', 'err');
        }
    }).catch(function() { mc1Toast('Request failed', 'err'); });
};

/* We show password strength hint while typing */
document.getElementById('pw-new') && document.getElementById('pw-new').addEventListener('input', function() {
    var pw   = this.value;
    var el   = document.getElementById('pw-strength');
    if (!el) return;
    if (!pw) { el.textContent = ''; return; }
    var score = 0;
    if (pw.length >= 8)  score++;
    if (pw.length >= 12) score++;
    if (/[A-Z]/.test(pw)) score++;
    if (/[0-9]/.test(pw)) score++;
    if (/[^A-Za-z0-9]/.test(pw)) score++;
    var labels = ['Too short', 'Weak', 'Fair', 'Good', 'Strong', 'Very strong'];
    var colors = ['var(--red)','var(--red)','var(--orange)','var(--yellow)','var(--green)','var(--teal)'];
    el.textContent = 'Strength: ' + (labels[score] || 'Strong');
    el.style.color = colors[score] || 'var(--teal)';
});

/* ── Load sessions list ───────────────────────────────────────────────── */
function loadSessions() {
    var el = document.getElementById('sessions-list');
    if (!el) return;
    mc1Api('POST', '/app/api/profile.php', {action: 'get_sessions'}).then(function(d) {
        if (!d.ok || !d.sessions) {
            el.innerHTML = '<div class="alert alert-error">Failed to load sessions</div>';
            return;
        }
        if (!d.sessions.length) {
            el.innerHTML = '<div class="empty">No active sessions found.</div>';
            return;
        }
        var rows = d.sessions.map(function(s) {
            /* We parse the user agent for a friendly browser label */
            var ua = s.user_agent || '';
            var browser = 'Unknown';
            if (/Chrome\//.test(ua) && !/Edg\//.test(ua))       browser = 'Chrome';
            else if (/Edg\//.test(ua))                            browser = 'Edge';
            else if (/Firefox\//.test(ua))                        browser = 'Firefox';
            else if (/Safari\//.test(ua) && !/Chrome/.test(ua))  browser = 'Safari';
            else if (/curl\//.test(ua))                           browser = 'cURL';

            var expires = s.expires_at ? s.expires_at.substring(0,10) : '—';
            var isCurrent = s.is_current;

            return '<tr>'
                + '<td>' + (isCurrent ? '<span class="badge badge-teal">This session</span>' : '') + '</td>'
                + '<td class="td-mono" style="font-size:11px">' + esc(s.ip_address || '—') + '</td>'
                + '<td><i class="fa-solid fa-browser" style="color:var(--muted);margin-right:4px"></i>' + esc(browser) + '</td>'
                + '<td style="font-size:11px;color:var(--muted)">' + esc(s.created_at ? s.created_at.substring(0,16).replace('T',' ') : '—') + '</td>'
                + '<td><span class="badge ' + (expires > new Date().toISOString().substring(0,10) ? 'badge-green' : 'badge-red') + '">' + esc(expires) + '</span></td>'
                + '<td class="td-acts">'
                + (isCurrent ? '<span style="font-size:11px;color:var(--muted)">Current</span>'
                    : '<button class="btn btn-danger btn-xs" onclick="profileRevokeOne(' + (int(s.id)) + ')"><i class="fa-solid fa-xmark"></i> Revoke</button>')
                + '</td>'
                + '</tr>';
        }).join('');
        el.innerHTML = '<div class="tbl-wrap"><table>'
            + '<thead><tr><th></th><th>IP Address</th><th>Browser</th><th>Created</th><th>Expires</th><th></th></tr></thead>'
            + '<tbody>' + rows + '</tbody></table></div>';
    }).catch(function() {
        el.innerHTML = '<div class="alert alert-error">Failed to load sessions</div>';
    });
}

function int(v) { return parseInt(v, 10) || 0; }

window.profileRevokeOne = function(session_id) {
    if (!session_id) return;
    if (!confirm('Revoke this session? That device will be logged out.')) return;
    mc1Api('POST', '/app/api/profile.php', {action: 'revoke_session', session_id: session_id}).then(function(d) {
        if (d.ok) { mc1Toast('Session revoked', 'ok'); loadSessions(); }
        else       mc1Toast(d.error || 'Failed', 'err');
    });
};

window.profileRevokeAll = function() {
    if (!confirm('Revoke all other sessions? All other devices will be logged out.\nYour current session will remain active.')) return;
    mc1Api('POST', '/app/api/profile.php', {action: 'revoke_all_others'}).then(function(d) {
        if (d.ok) {
            mc1Toast('Revoked ' + (d.revoked || 0) + ' other session(s)', 'ok');
            loadSessions();
        } else {
            mc1Toast(d.error || 'Failed', 'err');
        }
    });
};

<?php if (!empty($user['can_admin'])): ?>
/* ── Admin: load user session TTL controls ────────────────────────────── */
function loadAdminUsers() {
    var el = document.getElementById('admin-users-list');
    if (!el || el.dataset.loaded) return;
    el.dataset.loaded = '1';
    /* We use the server-rendered ADMIN_USERS data injected by PHP */
    el.innerHTML = renderAdminUsersHtml(ADMIN_USERS);
}

var ADMIN_USERS = <?= !empty($user['can_admin'])
    ? json_encode(mc1_db('mcaster1_encoder')
        ->query("SELECT u.id, u.username, u.display_name, u.session_ttl_override, r.label as role_label FROM users u JOIN roles r ON r.id = u.role_id ORDER BY u.id")
        ->fetchAll(\PDO::FETCH_ASSOC))
    : '[]' ?>;

function renderAdminUsersHtml(users) {
    if (!users || !users.length) return '<div class="empty">No users found.</div>';
    var rows = users.map(function(u) {
        var ttl = u.session_ttl_override;
        var ttlLabel = ttl === null ? '30 days (default)' : (ttl == 0 ? 'Never expires' : Math.round(ttl/86400) + ' days');
        var ttlSelect = '<select class="form-select" style="font-size:12px;padding:4px 8px;width:auto" onchange="adminSetTtl(' + u.id + ',this.value)">'
            + '<option value="default"' + (ttl === null ? ' selected' : '') + '>30 days (default)</option>'
            + '<option value="0"'       + (ttl == 0    ? ' selected' : '') + '>Never expires</option>'
            + '<option value="604800"'  + (ttl == 604800 ? ' selected' : '') + '>7 days</option>'
            + '<option value="86400"'   + (ttl == 86400  ? ' selected' : '') + '>1 day</option>'
            + '<option value="2592000"' + (ttl == 2592000 ? ' selected' : '') + '>30 days</option>'
            + '<option value="31536000"'+ (ttl == 31536000 ? ' selected' : '') + '>1 year</option>'
            + '</select>';
        return '<tr>'
            + '<td style="font-weight:600;color:var(--text)">' + esc(u.username) + '</td>'
            + '<td>' + esc(u.display_name || '') + '</td>'
            + '<td><span class="badge badge-teal">' + esc(u.role_label) + '</span></td>'
            + '<td>' + ttlSelect + '</td>'
            + '</tr>';
    }).join('');
    return '<div class="tbl-wrap"><table>'
        + '<thead><tr><th>Username</th><th>Display Name</th><th>Role</th><th>Session Duration</th></tr></thead>'
        + '<tbody>' + rows + '</tbody></table></div>';
}

function renderAdminUsers(users) {
    var el = document.getElementById('admin-users-list');
    if (el) el.innerHTML = renderAdminUsersHtml(users.length ? users : ADMIN_USERS);
}

document.addEventListener('DOMContentLoaded', function() {
    var el = document.getElementById('admin-users-list');
    if (el && !el.dataset.loaded) {
        el.innerHTML = renderAdminUsersHtml(ADMIN_USERS);
        el.dataset.loaded = '1';
    }
});

window.adminSetTtl = function(user_id, val) {
    var ttl_override = (val === 'default') ? null : parseInt(val);
    mc1Api('POST', '/app/api/profile.php', {
        action: 'set_session_ttl',
        user_id: user_id,
        ttl_override: ttl_override
    }).then(function(d) {
        if (d.ok) mc1Toast('Session duration updated', 'ok');
        else      mc1Toast(d.error || 'Failed', 'err');
    });
};
<?php endif; ?>

function esc(s){ return String(s||'').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;'); }

document.addEventListener('DOMContentLoaded', function() {
    /* We load sessions if the sessions tab was pre-selected via URL hash */
    if (window.location.hash === '#sessions') {
        var btn = document.querySelector('[data-tab="sessions"]');
        if (btn) profileTab('sessions', btn);
    }

    /* We self-heal if PHP rendered without a session (shows spinner):
     * trigger auto_login and reload so the full form renders on second load */
    if (!document.getElementById('pf-name')) {
        mc1Api('POST', '/app/api/auth.php', {action: 'auto_login'}).then(function(d) {
            if (d && d.ok) window.location.reload();
        }).catch(function(){});
    }
});

})();
</script>

<?php require_once __DIR__ . '/app/inc/footer.php'; ?>
