/* app.js — Mcaster1 DSP Encoder admin dashboard
 * Vanilla JS, no frameworks.
 */
(function () {
  'use strict';

  /* ── Config ─────────────────────────────────────────────────────────────── */
  const POLL_INTERVAL_MS = 5000;

  /* ── State ──────────────────────────────────────────────────────────────── */
  let pollTimer    = null;
  let lastStatus   = null;
  let volumeTimer  = null;

  /* ── Toast ──────────────────────────────────────────────────────────────── */
  let toastTimer = null;

  function toast(msg, type /* 'ok' | 'err' | 'info' */) {
    const el   = document.getElementById('toast');
    const icon = document.getElementById('toast-icon');
    const txt  = document.getElementById('toast-msg');

    el.className = '';
    if (type === 'ok')  { el.classList.add('toast-ok');   icon.className = 'fa-solid fa-circle-check'; }
    if (type === 'err') { el.classList.add('toast-err');  icon.className = 'fa-solid fa-circle-xmark'; }
    if (type === 'info'){ el.classList.add('toast-info'); icon.className = 'fa-solid fa-circle-info';  }
    txt.textContent = msg;

    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => el.classList.add('hidden'), 3500);
  }

  /* ── Navigation ─────────────────────────────────────────────────────────── */
  function switchView(viewName) {
    document.querySelectorAll('.view').forEach(v => v.classList.remove('active'));
    document.querySelectorAll('.nav-item').forEach(n => n.classList.remove('active'));

    const target = document.getElementById('view-' + viewName);
    if (target) target.classList.add('active');

    const nav = document.querySelector('.nav-item[data-view="' + viewName + '"]');
    if (nav) nav.classList.add('active');

    const titleMap = {
      overview: 'Dashboard',
      encoders: 'Encoders',
      streams:  'Streams',
      metadata: 'Now Playing / Metadata',
      playlist: 'Playlist',
      volume:   'Volume Control',
      settings: 'Settings',
      ssl:      'SSL / Certificates',
    };
    const pageTitle = document.getElementById('topbar-page-title');
    if (pageTitle) pageTitle.textContent = titleMap[viewName] || viewName;
  }

  /* ── Uptime formatter ───────────────────────────────────────────────────── */
  function formatUptime(secs) {
    if (!secs && secs !== 0) return '—';
    const d = Math.floor(secs / 86400);
    const h = Math.floor((secs % 86400) / 3600);
    const m = Math.floor((secs % 3600) / 60);
    const s = secs % 60;
    if (d > 0) return d + 'd ' + h + 'h ' + m + 'm';
    if (h > 0) return h + 'h ' + m + 'm ' + s + 's';
    return m + 'm ' + s + 's';
  }

  /* ── Topbar connection indicator ────────────────────────────────────────── */
  function setConnected(ok) {
    const icon  = document.querySelector('#topbar-conn i');
    const label = document.getElementById('topbar-conn-label');
    if (ok) {
      icon.className  = 'fa-solid fa-circle text-green';
      icon.style.fontSize = '8px';
      label.textContent   = 'Connected';
    } else {
      icon.className  = 'fa-solid fa-circle text-red';
      icon.style.fontSize = '8px';
      label.textContent   = 'Offline';
    }
  }

  /* ── Status API poll ────────────────────────────────────────────────────── */
  async function refreshStatus() {
    try {
      const res  = await fetch('/api/v1/status', { headers: { 'Accept': 'application/json' } });
      if (res.status === 401) { window.location.href = '/login'; return; }
      if (!res.ok) throw new Error('HTTP ' + res.status);
      const data = await res.json();
      lastStatus = data;
      setConnected(true);
      updateDashboard(data);
    } catch (_) {
      setConnected(false);
    }
  }

  /* ── Update dashboard from status JSON ─────────────────────────────────── */
  function updateDashboard(data) {
    /* Topbar uptime */
    const uptimeEl = document.getElementById('topbar-uptime');
    if (uptimeEl) uptimeEl.textContent = formatUptime(data.uptime);

    /* Stat cards */
    setText('stat-status',     data.ok ? 'Online' : 'Error');
    setText('stat-uptime',     formatUptime(data.uptime));

    const encoders    = data.encoders || [];
    const activeCount = encoders.filter(e => e.active).length;
    const totalListen = encoders.reduce((s, e) => s + (e.listeners || 0), 0);

    setText('stat-active-enc', activeCount);
    setText('stat-listeners',  totalListen);

    /* Now playing bar */
    const anyActive = activeCount > 0;
    const npBadge   = document.getElementById('np-badge');
    if (npBadge) {
      npBadge.className  = anyActive ? 'badge badge-live' : 'badge badge-idle';
      npBadge.textContent = anyActive ? 'Live' : 'Idle';
    }

    /* Use first active encoder's metadata if available */
    const active = encoders.find(e => e.active);
    if (active) {
      setText('np-title',  active.song_title  || active.song || 'No track metadata');
      setText('np-artist', active.song_artist || active.mount || '—');
    }

    /* Encoder table (overview) */
    updateEncoderTable(encoders);

    /* Streams table */
    updateStreamsTable(encoders);

    /* Settings info */
    updateSettingsInfo(data);

    /* SSL info */
    updateSSLInfo(data);

    /* Current metadata panel */
    updateCurrentMeta(data);

    /* Per-encoder volume sliders */
    updatePerEncoderVolume(encoders);
  }

  function setText(id, val) {
    const el = document.getElementById(id);
    if (el) el.textContent = val;
  }

  /* ── Encoder table ──────────────────────────────────────────────────────── */
  function encoderStatusBadge(enc) {
    if (enc.active)       return '<span class="badge badge-live">Live</span>';
    if (enc.configured)   return '<span class="badge badge-idle">Idle</span>';
    return '<span class="badge badge-offline">Off</span>';
  }

  function updateEncoderTable(encoders) {
    const tbody = document.getElementById('encoder-table-body');
    if (!tbody) return;
    if (!encoders || encoders.length === 0) {
      tbody.innerHTML = '<tr><td colspan="7" style="text-align:center;color:var(--muted);padding:32px">No encoders configured.</td></tr>';
      return;
    }
    tbody.innerHTML = encoders.map(e => `
      <tr>
        <td class="mono">#${e.slot + 1}</td>
        <td>${encoderStatusBadge(e)}</td>
        <td><span class="badge" style="background:rgba(8,145,178,0.12);color:var(--cyan)">${esc(e.format || '—')}</span></td>
        <td class="mono">${e.bitrate ? e.bitrate + ' kbps' : '—'}</td>
        <td class="mono" style="font-size:11px">${esc(e.server || '—')}</td>
        <td class="mono" style="font-size:11px">${esc(e.mount || '—')}</td>
        <td class="mono">${e.listeners != null ? e.listeners : '—'}</td>
      </tr>`).join('');
  }

  /* ── Streams table ──────────────────────────────────────────────────────── */
  function updateStreamsTable(encoders) {
    const tbody = document.getElementById('streams-table-body');
    if (!tbody) return;
    const active = (encoders || []).filter(e => e.active);
    if (active.length === 0) {
      tbody.innerHTML = '<tr><td colspan="7" style="text-align:center;color:var(--muted);padding:32px">No active streams.</td></tr>';
      return;
    }
    tbody.innerHTML = active.map(e => `
      <tr>
        <td class="mono">#${e.slot + 1}</td>
        <td class="mono" style="font-size:11px">${esc(e.server || '—')}</td>
        <td class="mono">${esc(e.mount  || '—')}</td>
        <td><span class="badge" style="background:rgba(8,145,178,0.12);color:var(--cyan)">${esc(e.format || '—')}</span></td>
        <td class="mono">${e.bitrate ? e.bitrate + ' kbps' : '—'}</td>
        <td class="mono">${e.listeners != null ? e.listeners : '—'}</td>
        <td>${encoderStatusBadge(e)}</td>
      </tr>`).join('');
  }

  /* ── Current metadata display ───────────────────────────────────────────── */
  function updateCurrentMeta(data) {
    const el = document.getElementById('current-meta-display');
    if (!el) return;
    const m = data.metadata || {};
    const rows = [
      ['Title',    m.title  || '—'],
      ['Artist',   m.artist || '—'],
      ['Album',    m.album  || '—'],
      ['Genre',    m.genre  || '—'],
      ['Year',     m.year   || '—'],
      ['BPM',      m.bpm    || '—'],
    ];
    el.innerHTML = rows.map(([k, v]) => `
      <div style="display:flex;justify-content:space-between;gap:12px;padding:6px 0;border-bottom:1px solid var(--border)">
        <span style="color:var(--muted);font-size:12px;text-transform:uppercase;letter-spacing:.05em">${k}</span>
        <span style="font-size:13px;text-align:right">${esc(String(v))}</span>
      </div>`).join('');
  }

  /* ── Settings info ──────────────────────────────────────────────────────── */
  function updateSettingsInfo(data) {
    const el = document.getElementById('settings-info');
    if (!el) return;
    const rows = [
      ['Version',   data.version   || '—'],
      ['Uptime',    formatUptime(data.uptime)],
      ['Encoders',  (data.encoders || []).length + ' configured'],
      ['Listeners', (data.encoders || []).reduce((s, e) => s + (e.listeners || 0), 0)],
    ];
    el.innerHTML = rows.map(([k, v]) => `
      <div style="display:flex;gap:12px;padding:6px 0;border-bottom:1px solid var(--border)">
        <span style="color:var(--muted);font-size:12px;text-transform:uppercase;letter-spacing:.05em;width:100px;flex-shrink:0">${k}</span>
        <span class="mono" style="color:var(--teal)">${esc(String(v))}</span>
      </div>`).join('');
  }

  /* ── SSL info ───────────────────────────────────────────────────────────── */
  function updateSSLInfo(data) {
    const el = document.getElementById('ssl-info');
    if (!el) return;
    const sockets = data.sockets || [];
    if (sockets.length === 0) {
      el.innerHTML = '<div style="color:var(--muted)">No socket info available.</div>';
      return;
    }
    el.innerHTML = sockets.map((s, i) => `
      <div style="display:flex;gap:12px;padding:6px 0;border-bottom:1px solid var(--border)">
        <span style="color:var(--muted);font-size:12px;width:80px;flex-shrink:0">Socket ${i + 1}</span>
        <span class="mono" style="color:${s.ssl ? 'var(--green)' : 'var(--text-dim)'}">
          ${s.ssl ? '<i class="fa-solid fa-lock"></i>' : '<i class="fa-solid fa-lock-open"></i>'}
          ${esc(s.bind || '0.0.0.0')}:${s.port || '?'}
          &nbsp;<span style="color:var(--muted)">${s.ssl ? 'HTTPS' : 'HTTP'}</span>
        </span>
      </div>`).join('');
  }

  /* ── Per-encoder volume UI ──────────────────────────────────────────────── */
  function updatePerEncoderVolume(encoders) {
    const el = document.getElementById('per-encoder-volume');
    if (!el || el.dataset.populated) return;
    if (!encoders || encoders.length === 0) {
      el.innerHTML = '<p style="color:var(--muted);font-size:13px">No encoders configured.</p>';
      return;
    }
    el.innerHTML = encoders.map(e => `
      <div style="display:flex;align-items:center;gap:10px">
        <span style="font-size:12px;color:var(--muted);width:60px;flex-shrink:0">Slot #${e.slot + 1}</span>
        <div class="volume-wrap" style="flex:1">
          <i class="fa-solid fa-volume-low volume-icon"></i>
          <input type="range" class="volume-slider enc-vol-slider"
                 data-slot="${e.slot}" min="0" max="100" value="${e.volume != null ? e.volume : 100}">
          <span class="volume-value enc-vol-val" data-slot="${e.slot}">${e.volume != null ? e.volume : 100}%</span>
        </div>
      </div>`).join('');

    el.dataset.populated = '1';

    /* Wire up per-encoder sliders */
    el.querySelectorAll('.enc-vol-slider').forEach(slider => {
      slider.addEventListener('input', () => {
        const val    = slider.value;
        const valEl  = el.querySelector('.enc-vol-val[data-slot="' + slider.dataset.slot + '"]');
        if (valEl) valEl.textContent = val + '%';
      });
    });
  }

  /* ── HTML escaping ──────────────────────────────────────────────────────── */
  function esc(s) {
    return String(s)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }

  /* ── Logout ─────────────────────────────────────────────────────────────── */
  async function logout() {
    try {
      await fetch('/api/v1/auth/logout', { method: 'POST' });
    } catch (_) { /* ignore */ }
    window.location.href = '/login';
  }

  /* ── Metadata form submit ───────────────────────────────────────────────── */
  async function pushMetadata(e) {
    e.preventDefault();
    const payload = {
      title:   document.getElementById('meta-title').value.trim(),
      artist:  document.getElementById('meta-artist').value.trim(),
      album:   document.getElementById('meta-album').value.trim(),
      genre:   document.getElementById('meta-genre').value.trim(),
      year:    document.getElementById('meta-year').value.trim(),
      bpm:     document.getElementById('meta-bpm').value.trim(),
      artwork: document.getElementById('meta-artwork').value.trim(),
    };

    const btn = document.querySelector('#metadata-form button[type=submit]');
    btn.disabled = true;

    try {
      const res = await fetch('/api/v1/metadata', {
        method:  'PUT',
        headers: { 'Content-Type': 'application/json', 'Accept': 'application/json' },
        body:    JSON.stringify(payload),
      });
      const data = await res.json().catch(() => ({}));
      if (res.ok) {
        toast('Metadata pushed to all active streams.', 'ok');
      } else {
        toast(data.error || 'Failed to push metadata.', 'err');
      }
    } catch (_) {
      toast('Cannot reach the server.', 'err');
    } finally {
      btn.disabled = false;
    }
  }

  /* ── Volume apply ───────────────────────────────────────────────────────── */
  async function applyVolume() {
    const val = parseInt(document.getElementById('volume-slider-main').value, 10);
    try {
      const res = await fetch('/api/v1/volume', {
        method:  'PUT',
        headers: { 'Content-Type': 'application/json', 'Accept': 'application/json' },
        body:    JSON.stringify({ volume: val }),
      });
      const data = await res.json().catch(() => ({}));
      if (res.ok) {
        toast('Volume set to ' + val + '%.', 'ok');
      } else {
        toast(data.error || 'Failed to set volume.', 'err');
      }
    } catch (_) {
      toast('Cannot reach the server.', 'err');
    }
  }

  /* Exposed globally for inline onclick preset buttons */
  window.setPresetVolume = function (val) {
    const slider  = document.getElementById('volume-slider-main');
    const display = document.getElementById('volume-display-main');
    if (slider)  slider.value       = val;
    if (display) display.textContent = val + '%';
  };

  /* ── Wire up events ─────────────────────────────────────────────────────── */
  function init() {
    /* Nav items */
    document.querySelectorAll('.nav-item[data-view]').forEach(item => {
      item.addEventListener('click', () => switchView(item.dataset.view));
    });

    /* Logout */
    const btnLogout = document.getElementById('btn-logout');
    if (btnLogout) btnLogout.addEventListener('click', logout);

    /* Refresh button */
    const btnRefresh = document.getElementById('btn-refresh-now');
    if (btnRefresh) btnRefresh.addEventListener('click', () => {
      refreshStatus();
      toast('Refreshed.', 'info');
    });

    /* Metadata form */
    const metaForm = document.getElementById('metadata-form');
    if (metaForm) metaForm.addEventListener('submit', pushMetadata);

    /* Master volume slider display */
    const masterSlider  = document.getElementById('volume-slider-main');
    const masterDisplay = document.getElementById('volume-display-main');
    if (masterSlider) {
      masterSlider.addEventListener('input', () => {
        if (masterDisplay) masterDisplay.textContent = masterSlider.value + '%';
      });
    }

    /* Apply volume button */
    const btnVol = document.getElementById('btn-apply-volume');
    if (btnVol) btnVol.addEventListener('click', applyVolume);

    /* Playlist load (stub — will be wired to backend in Phase 5) */
    const btnPlaylist = document.getElementById('btn-load-playlist');
    if (btnPlaylist) {
      btnPlaylist.addEventListener('click', () => {
        const path = document.getElementById('playlist-path').value.trim();
        if (!path) { toast('Enter a playlist file path.', 'err'); return; }
        toast('Playlist loading is available in the CLI via --playlist flag.', 'info');
      });
    }

    /* Initial status fetch + start polling */
    refreshStatus();
    pollTimer = setInterval(refreshStatus, POLL_INTERVAL_MS);
  }

  /* ── Start ──────────────────────────────────────────────────────────────── */
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
