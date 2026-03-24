/**
 * mixer-console.js — Mcaster1 Virtual Mixer Console
 * Phase MX-1: Channel strip management, fader/knob interaction, VU meters
 */
(function() {
'use strict';

var POLL_MS    = 3000;
var VU_FPS     = 20;
var FADER_MIN  = 0.0;
var FADER_MAX  = 2.0;
var FADER_DEF  = 1.0;
var PAN_MIN    = -1.0;
var PAN_MAX    = 1.0;

// VU meter segment colors (bottom to top): 8 green, 4 yellow, 2 red
var VU_COLORS = [
  '#166534','#16a34a','#22c55e','#22c55e','#22c55e','#4ade80','#4ade80','#86efac',
  '#a16207','#ca8a04','#eab308','#facc15',
  '#dc2626','#ef4444'
];
var VU_SEGMENTS = VU_COLORS.length;

function MixerConsole() {
  this.container = null;
  this.channels  = {};      // slot_id -> channel state
  this.master    = { volume: 1.0, vu_peak: 0 };
  this.soloSet   = {};      // slot_id -> true if soloed
  this.soloActive = false;
  this.pollTimer = null;
  this.vuTimer   = null;
  this.dragging  = null;    // { slot_id, startY, startVol }
  this.panDrag   = null;    // { slot_id, startX, startPan }
  this.configName = 'Default';
  this.configId   = null;
  this.selectedFader = null;
  this.skin       = 'broadcast_dark';
}

MixerConsole.prototype.init = function(containerId) {
  this.container = document.getElementById(containerId);
  if (!this.container) return;

  // Load skin from localStorage immediately (before server config arrives)
  var savedSkin = localStorage.getItem('mc1_mixer_skin');
  if (savedSkin) this.setSkin(savedSkin, true);

  // Load saved config from server
  this._loadConfig();

  // Initial poll to get slots
  this._poll();
  this.pollTimer = setInterval(this._poll.bind(this), POLL_MS);
  this.vuTimer   = setInterval(this._tickVU.bind(this), 1000 / VU_FPS);

  // Global mouse/touch handlers for fader dragging
  var self = this;
  document.addEventListener('mousemove', function(e) { self._onDrag(e); });
  document.addEventListener('mouseup',   function(e) { self._onDragEnd(e); });
  document.addEventListener('touchmove', function(e) { self._onDrag(e); }, { passive: false });
  document.addEventListener('touchend',  function(e) { self._onDragEnd(e); });

  // Keyboard: arrow keys adjust selected fader
  document.addEventListener('keydown', function(e) {
    if (!self.selectedFader) return;
    if (document.activeElement && (document.activeElement.tagName === 'INPUT' || document.activeElement.tagName === 'SELECT' || document.activeElement.tagName === 'TEXTAREA')) return;
    var ch = self.channels[self.selectedFader] || self.master;
    var slotId = self.selectedFader;
    if (e.key === 'ArrowUp') {
      e.preventDefault();
      var nv = Math.min(FADER_MAX, (ch.volume || 0) + 0.05);
      self._setVolume(slotId, nv);
    } else if (e.key === 'ArrowDown') {
      e.preventDefault();
      var nv2 = Math.max(FADER_MIN, (ch.volume || 0) - 0.05);
      self._setVolume(slotId, nv2);
    }
  });
};

/* ── Polling ───────────────────────────────────────────── */
MixerConsole.prototype._poll = function() {
  var self = this;
  mc1Api('GET', '/api/v1/encoders').then(function(encoders) {
    if (!Array.isArray(encoders)) return;

    var knownSlots = {};
    encoders.forEach(function(enc) {
      var id = enc.slot_id;
      knownSlots[id] = true;

      if (!self.channels[id]) {
        self.channels[id] = {
          slot_id: id,
          name: enc.name || ('Slot ' + id),
          mount: enc.mount || '',
          state: 'idle',
          volume: enc.volume !== undefined ? enc.volume : FADER_DEF,
          pan: 0,
          muted: false,
          solo: false,
          vu_peak: 0,
          vu_target: 0,
          bytes_sent: enc.bytes_sent || 0
        };
      }

      var ch = self.channels[id];
      ch.name  = enc.name || ('Slot ' + id);
      ch.mount = enc.mount || '';
      ch.state = (enc.state || 'idle').toLowerCase();
      ch.track = [enc.track_title, enc.track_artist].filter(Boolean).join(' - ') || '';

      // Derive VU from bytes_sent delta (crude but effective without real meters)
      var prevBytes = ch.bytes_sent || 0;
      var nowBytes  = enc.bytes_sent || 0;
      var delta     = (nowBytes > prevBytes) ? nowBytes - prevBytes : 0;
      ch.bytes_sent = nowBytes;
      // Normalize: ~16000 bytes/sec = ~128kbps = full scale
      var level = Math.min(1.0, delta / (16000 * (POLL_MS / 1000)));
      ch.vu_target = (ch.state === 'live') ? Math.max(0.15, level) : 0;
    });

    // Calculate master VU (average of live channels)
    var liveCount = 0, vuSum = 0;
    for (var sid in self.channels) {
      if (self.channels[sid].state === 'live') {
        liveCount++;
        vuSum += self.channels[sid].vu_target;
      }
    }
    self.master.vu_target = liveCount > 0 ? vuSum / liveCount : 0;

    // Remove channels for slots that no longer exist
    for (var sid2 in self.channels) {
      if (!knownSlots[sid2]) delete self.channels[sid2];
    }

    self._render();
  }).catch(function() { /* API offline — keep existing state */ });

  // Poll master volume
  mc1Api('GET', '/api/v1/status').then(function(d) {
    if (d && d.master_volume !== undefined) {
      self.master.volume = d.master_volume;
      self._updateFaderVisual('master', self.master.volume);
    }
  }).catch(function() {});
};

/* ── VU Meter Tick (smooth animation) ──────────────────── */
MixerConsole.prototype._tickVU = function() {
  var decay = 0.85;
  var attack = 0.5;
  for (var sid in this.channels) {
    var ch = this.channels[sid];
    var target = ch.vu_target || 0;
    var cur = ch.vu_peak || 0;
    if (target > cur) {
      ch.vu_peak = cur + (target - cur) * attack;
    } else {
      ch.vu_peak = cur * decay;
    }
    if (ch.vu_peak < 0.01) ch.vu_peak = 0;
    this._drawVU('vu-' + sid, ch.vu_peak);
  }
  // Master VU
  var mt = this.master.vu_target || 0;
  var mc = this.master.vu_peak || 0;
  if (mt > mc) this.master.vu_peak = mc + (mt - mc) * attack;
  else this.master.vu_peak = mc * decay;
  if (this.master.vu_peak < 0.01) this.master.vu_peak = 0;
  this._drawVU('vu-master', this.master.vu_peak);
};

/* ── Draw VU Meter ─────────────────────────────────────── */
MixerConsole.prototype._drawVU = function(canvasId, level) {
  var canvas = document.getElementById(canvasId);
  if (!canvas) return;
  var ctx = canvas.getContext('2d');
  var w = canvas.width;
  var h = canvas.height;
  ctx.clearRect(0, 0, w, h);

  var segH = Math.floor(h / VU_SEGMENTS) - 1;
  var litCount = Math.round(level * VU_SEGMENTS);

  for (var i = 0; i < VU_SEGMENTS; i++) {
    var y = h - (i + 1) * (segH + 1);
    if (i < litCount) {
      ctx.fillStyle = VU_COLORS[i];
      ctx.shadowBlur = 3;
      ctx.shadowColor = VU_COLORS[i];
    } else {
      ctx.fillStyle = 'rgba(255,255,255,.04)';
      ctx.shadowBlur = 0;
    }
    ctx.fillRect(2, y, w - 4, segH);
  }
  ctx.shadowBlur = 0;
};

/* ── Render Channel Strips ─────────────────────────────── */
MixerConsole.prototype._render = function() {
  var channelWrap = document.getElementById('mixer-ch-wrap');
  if (!channelWrap) return;

  // Get sorted slot IDs
  var ids = Object.keys(this.channels).map(Number).sort(function(a, b) { return a - b; });

  // Only rebuild DOM if channel count changed
  var currentCount = channelWrap.querySelectorAll('.ch-strip:not(.ch-master)').length;
  if (currentCount !== ids.length) {
    this._buildStrips(channelWrap, ids);
  }

  // Update existing strips
  var self = this;
  ids.forEach(function(id) {
    var ch = self.channels[id];
    self._updateStrip(id, ch);
  });

  // Update master
  this._updateFaderVisual('master', this.master.volume);
};

MixerConsole.prototype._buildStrips = function(wrap, ids) {
  var html = '';
  var self = this;

  ids.forEach(function(id) {
    var ch = self.channels[id];
    html += self._stripHTML(id, ch);
  });

  // Master strip
  html += self._masterStripHTML();

  wrap.innerHTML = html;

  // Attach event listeners after DOM insert
  ids.forEach(function(id) {
    self._attachStripEvents(id);
  });
  self._attachStripEvents('master');
};

MixerConsole.prototype._stripHTML = function(id, ch) {
  var vol = ch.volume !== undefined ? ch.volume : FADER_DEF;
  var volPct = Math.round(vol * 100);
  return '<div class="ch-strip" id="ch-' + id + '" data-slot="' + id + '">'
    + '<div class="ch-label">'
    + '  <div class="ch-label-name">' + esc(ch.name) + '</div>'
    + '  <div class="ch-label-mount">' + esc(ch.mount) + '</div>'
    + '</div>'
    + '<span class="ch-status idle" id="ch-status-' + id + '">IDLE</span>'
    + '<div class="ch-vu"><canvas id="vu-' + id + '" width="24" height="100"></canvas></div>'
    + '<div class="ch-fader-wrap">'
    + '  <div class="ch-fader-track" id="fader-track-' + id + '">'
    + '    <div class="ch-fader-unity" style="top:50%"></div>'
    + '    <div class="ch-fader-fill" id="fader-fill-' + id + '" style="height:' + (vol / FADER_MAX * 100) + '%"></div>'
    + '    <div class="ch-fader-cap" id="fader-cap-' + id + '" style="top:' + ((1 - vol / FADER_MAX) * 100) + '%"></div>'
    + '  </div>'
    + '  <div class="ch-fader-val" id="fader-val-' + id + '">' + volPct + '%</div>'
    + '</div>'
    + '<div class="ch-pan-wrap">'
    + '  <span class="ch-pan-label">L</span>'
    + '  <div class="ch-pan-knob" id="pan-knob-' + id + '" title="Pan: C">'
    + '    <div class="ch-pan-indicator" id="pan-ind-' + id + '"></div>'
    + '  </div>'
    + '  <span class="ch-pan-label">R</span>'
    + '</div>'
    + '<div class="ch-btns">'
    + '  <button class="ch-btn-mute" id="btn-mute-' + id + '" title="Mute">M</button>'
    + '  <button class="ch-btn-solo" id="btn-solo-' + id + '" title="Solo">S</button>'
    + '</div>'
    + '</div>';
};

MixerConsole.prototype._masterStripHTML = function() {
  var vol = this.master.volume !== undefined ? this.master.volume : FADER_DEF;
  var volPct = Math.round(vol * 100);
  return '<div class="ch-strip ch-master" id="ch-master" data-slot="master">'
    + '<div class="ch-label">'
    + '  <div class="ch-label-name" style="color:var(--teal)">MASTER</div>'
    + '  <div class="ch-label-mount">Main Bus</div>'
    + '</div>'
    + '<span class="ch-status live" id="ch-status-master" style="visibility:hidden">&nbsp;</span>'
    + '<div class="ch-vu"><canvas id="vu-master" width="24" height="100"></canvas></div>'
    + '<div class="ch-fader-wrap">'
    + '  <div class="ch-fader-track" id="fader-track-master">'
    + '    <div class="ch-fader-unity" style="top:50%"></div>'
    + '    <div class="ch-fader-fill" id="fader-fill-master" style="height:' + (vol / FADER_MAX * 100) + '%"></div>'
    + '    <div class="ch-fader-cap" id="fader-cap-master" style="top:' + ((1 - vol / FADER_MAX) * 100) + '%"></div>'
    + '  </div>'
    + '  <div class="ch-fader-val" id="fader-val-master">' + volPct + '%</div>'
    + '</div>'
    + '<div class="ch-pan-wrap" style="visibility:hidden">'
    + '  <span class="ch-pan-label">L</span>'
    + '  <div class="ch-pan-knob"><div class="ch-pan-indicator"></div></div>'
    + '  <span class="ch-pan-label">R</span>'
    + '</div>'
    + '<div class="ch-btns" style="visibility:hidden">'
    + '  <button class="ch-btn-mute">M</button>'
    + '  <button class="ch-btn-solo">S</button>'
    + '</div>'
    + '</div>';
};

/* ── Attach Strip Events ───────────────────────────────── */
MixerConsole.prototype._attachStripEvents = function(id) {
  var self = this;

  // Fader cap drag
  var cap = document.getElementById('fader-cap-' + id);
  if (cap) {
    cap.addEventListener('mousedown', function(e) { self._onDragStart(e, id); });
    cap.addEventListener('touchstart', function(e) { self._onDragStart(e, id); }, { passive: false });
  }

  // Click on fader track to jump
  var track = document.getElementById('fader-track-' + id);
  if (track) {
    track.addEventListener('click', function(e) {
      if (e.target.classList.contains('ch-fader-cap')) return;
      var rect = track.getBoundingClientRect();
      var pct = 1 - (e.clientY - rect.top) / rect.height;
      var vol = Math.max(FADER_MIN, Math.min(FADER_MAX, pct * FADER_MAX));
      self._setVolume(id, vol);
    });
  }

  // Select fader for keyboard
  var strip = document.getElementById('ch-' + id);
  if (strip) {
    strip.addEventListener('click', function() {
      // Deselect previous
      var prev = document.querySelector('.ch-strip.selected');
      if (prev) prev.classList.remove('selected');
      strip.classList.add('selected');
      self.selectedFader = id;
    });
  }

  if (id === 'master') return;

  // Pan knob
  var knob = document.getElementById('pan-knob-' + id);
  if (knob) {
    knob.addEventListener('mousedown', function(e) { self._onPanStart(e, id); });
    knob.addEventListener('dblclick', function() {
      if (self.channels[id]) self.channels[id].pan = 0;
      self._updatePanVisual(id, 0);
    });
  }

  // Mute button
  var muteBtn = document.getElementById('btn-mute-' + id);
  if (muteBtn) {
    muteBtn.addEventListener('click', function() { self._toggleMute(id); });
  }

  // Solo button
  var soloBtn = document.getElementById('btn-solo-' + id);
  if (soloBtn) {
    soloBtn.addEventListener('click', function() { self._toggleSolo(id); });
  }
};

/* ── Update Strip Visual State ─────────────────────────── */
MixerConsole.prototype._updateStrip = function(id, ch) {
  // State badge
  var badge = document.getElementById('ch-status-' + id);
  if (badge) {
    var cls = ({'live':'live','idle':'idle','connecting':'connecting','reconnecting':'connecting','error':'error','sleep':'sleep'})[(ch.state || 'idle')] || 'idle';
    badge.className = 'ch-status ' + cls;
    badge.textContent = (ch.state || 'idle').toUpperCase();
  }

  // Strip class
  var strip = document.getElementById('ch-' + id);
  if (strip) {
    strip.classList.remove('live-strip', 'dimmed', 'muted-strip');
    if (ch.state === 'live') strip.classList.add('live-strip');
    if (ch.muted) strip.classList.add('muted-strip');
    if (this.soloActive && !ch.solo) strip.classList.add('dimmed');
  }

  // Name / mount
  var nameEl = strip ? strip.querySelector('.ch-label-name') : null;
  if (nameEl) nameEl.textContent = ch.name;
  var mountEl = strip ? strip.querySelector('.ch-label-mount') : null;
  if (mountEl) mountEl.textContent = ch.mount;

  // Fader
  this._updateFaderVisual(id, ch.volume);

  // Pan
  this._updatePanVisual(id, ch.pan || 0);

  // Mute/solo button state
  var muteBtn = document.getElementById('btn-mute-' + id);
  if (muteBtn) muteBtn.classList.toggle('active', !!ch.muted);
  var soloBtn = document.getElementById('btn-solo-' + id);
  if (soloBtn) soloBtn.classList.toggle('active', !!ch.solo);
};

/* ── Fader Visual Update ───────────────────────────────── */
MixerConsole.prototype._updateFaderVisual = function(id, vol) {
  var fill = document.getElementById('fader-fill-' + id);
  var cap  = document.getElementById('fader-cap-' + id);
  var valEl = document.getElementById('fader-val-' + id);

  var pct = (vol / FADER_MAX) * 100;
  if (fill) fill.style.height = pct + '%';
  if (cap)  cap.style.top = (100 - pct) + '%';
  if (valEl) valEl.textContent = Math.round(vol * 100) + '%';
};

/* ── Pan Visual Update ─────────────────────────────────── */
MixerConsole.prototype._updatePanVisual = function(id, pan) {
  var ind = document.getElementById('pan-ind-' + id);
  if (!ind) return;
  // -1.0 (left) = -135 deg, 0 (center) = 0 deg, 1.0 (right) = 135 deg
  var angle = pan * 135;
  ind.style.transform = 'translateX(-50%) rotate(' + angle + 'deg)';
  var knob = document.getElementById('pan-knob-' + id);
  if (knob) {
    var label = pan < -0.05 ? 'L' + Math.round(Math.abs(pan) * 100) : pan > 0.05 ? 'R' + Math.round(pan * 100) : 'C';
    knob.title = 'Pan: ' + label;
  }
};

/* ── Fader Drag ────────────────────────────────────────── */
MixerConsole.prototype._onDragStart = function(e, id) {
  e.preventDefault();
  var y = e.type === 'touchstart' ? e.touches[0].clientY : e.clientY;
  var vol = id === 'master' ? this.master.volume : (this.channels[id] ? this.channels[id].volume : FADER_DEF);
  this.dragging = { slot_id: id, startY: y, startVol: vol };
  var cap = document.getElementById('fader-cap-' + id);
  if (cap) cap.classList.add('dragging');
  this.selectedFader = id;
};

MixerConsole.prototype._onDrag = function(e) {
  if (!this.dragging && !this.panDrag) return;

  if (this.dragging) {
    e.preventDefault();
    var y = e.type === 'touchmove' ? e.touches[0].clientY : e.clientY;
    var track = document.getElementById('fader-track-' + this.dragging.slot_id);
    if (!track) return;
    var trackH = track.getBoundingClientRect().height;
    var dy = this.dragging.startY - y;
    var dVol = (dy / trackH) * FADER_MAX;
    var nv = Math.max(FADER_MIN, Math.min(FADER_MAX, this.dragging.startVol + dVol));
    this._setVolumeVisual(this.dragging.slot_id, nv);
  }

  if (this.panDrag) {
    e.preventDefault();
    var x = e.type === 'touchmove' ? e.touches[0].clientX : e.clientX;
    var dx = x - this.panDrag.startX;
    var dp = dx / 60; // 60px = full range
    var np = Math.max(PAN_MIN, Math.min(PAN_MAX, this.panDrag.startPan + dp));
    if (this.channels[this.panDrag.slot_id]) {
      this.channels[this.panDrag.slot_id].pan = np;
    }
    this._updatePanVisual(this.panDrag.slot_id, np);
  }
};

MixerConsole.prototype._onDragEnd = function(e) {
  if (this.dragging) {
    var cap = document.getElementById('fader-cap-' + this.dragging.slot_id);
    if (cap) cap.classList.remove('dragging');
    // Send final volume to API
    var id = this.dragging.slot_id;
    var vol = id === 'master' ? this.master.volume : (this.channels[id] ? this.channels[id].volume : FADER_DEF);
    this._sendVolume(id, vol);
    this.dragging = null;
  }
  if (this.panDrag) {
    this.panDrag = null;
  }
};

/* ── Pan Drag ──────────────────────────────────────────── */
MixerConsole.prototype._onPanStart = function(e, id) {
  e.preventDefault();
  var pan = this.channels[id] ? this.channels[id].pan || 0 : 0;
  this.panDrag = { slot_id: id, startX: e.clientX, startPan: pan };
};

/* ── Volume Helpers ────────────────────────────────────── */
MixerConsole.prototype._setVolumeVisual = function(id, vol) {
  vol = Math.round(vol * 20) / 20; // snap to 0.05 increments
  if (id === 'master') {
    this.master.volume = vol;
  } else if (this.channels[id]) {
    this.channels[id].volume = vol;
  }
  this._updateFaderVisual(id, vol);
};

MixerConsole.prototype._setVolume = function(id, vol) {
  vol = Math.round(vol * 20) / 20;
  this._setVolumeVisual(id, vol);
  this._sendVolume(id, vol);
};

MixerConsole.prototype._sendVolume = function(id, vol) {
  var slot = (id === 'master') ? -1 : parseInt(id);
  mc1Api('PUT', '/api/v1/volume', { slot: slot, volume: vol }).catch(function() {});
};

/* ── Mute Toggle ───────────────────────────────────────── */
MixerConsole.prototype._toggleMute = function(id) {
  var ch = this.channels[id];
  if (!ch) return;
  ch.muted = !ch.muted;

  var muteBtn = document.getElementById('btn-mute-' + id);
  if (muteBtn) muteBtn.classList.toggle('active', ch.muted);

  var strip = document.getElementById('ch-' + id);
  if (strip) strip.classList.toggle('muted-strip', ch.muted);

  // Mute = set volume to 0, unmute = restore
  if (ch.muted) {
    ch._savedVol = ch.volume;
    this._setVolume(id, 0);
  } else {
    this._setVolume(id, ch._savedVol !== undefined ? ch._savedVol : FADER_DEF);
  }
};

/* ── Solo Toggle ───────────────────────────────────────── */
MixerConsole.prototype._toggleSolo = function(id) {
  var ch = this.channels[id];
  if (!ch) return;
  ch.solo = !ch.solo;

  // Recalculate solo state
  this.soloActive = false;
  for (var sid in this.channels) {
    if (this.channels[sid].solo) {
      this.soloActive = true;
      break;
    }
  }

  // Update visual dim state on all strips
  for (var sid2 in this.channels) {
    var strip = document.getElementById('ch-' + sid2);
    if (strip) {
      strip.classList.toggle('dimmed', this.soloActive && !this.channels[sid2].solo);
    }
    var soloBtn = document.getElementById('btn-solo-' + sid2);
    if (soloBtn) soloBtn.classList.toggle('active', !!this.channels[sid2].solo);
  }
};

/* ── Skin Management ──────────────────────────────────── */
MixerConsole.prototype.setSkin = function(skinName, skipSave) {
  var validSkins = ['broadcast_dark','studio_warm','live_neon','vintage_analog','military_tactical','arctic_clean'];
  if (validSkins.indexOf(skinName) === -1) skinName = 'broadcast_dark';
  this.skin = skinName;

  // Update the data attribute on the mixer surface
  var surface = document.getElementById('mixer-surface');
  if (surface) surface.setAttribute('data-mixer-skin', skinName);

  // Update the dropdown to match
  var sel = document.getElementById('mixer-skin-select');
  if (sel) sel.value = skinName;

  // Persist to localStorage
  localStorage.setItem('mc1_mixer_skin', skinName);

  // If not a silent load, save to server config too
  if (!skipSave) this.saveConfig();
};

/* ── Config Load / Save ────────────────────────────────── */
MixerConsole.prototype._loadConfig = function() {
  var self = this;
  mc1Api('GET', '/api/v1/mixer/config').then(function(d) {
    if (!d || !d.ok) return;
    if (d.config_name) self.configName = d.config_name;
    if (d.config_id) self.configId = d.config_id;
    if (d.master_volume !== undefined) self.master.volume = d.master_volume;
    if (d.skin) self.setSkin(d.skin, true);

    var nameInput = document.getElementById('mixer-config-name');
    if (nameInput) nameInput.value = self.configName;

    // Apply channel overrides
    if (d.channel_json && Array.isArray(d.channel_json)) {
      d.channel_json.forEach(function(saved) {
        if (self.channels[saved.slot_id]) {
          if (saved.volume !== undefined) self.channels[saved.slot_id].volume = saved.volume;
          if (saved.pan !== undefined) self.channels[saved.slot_id].pan = saved.pan;
          if (saved.muted !== undefined) self.channels[saved.slot_id].muted = saved.muted;
          if (saved.solo !== undefined) self.channels[saved.slot_id].solo = saved.solo;
        }
      });
    }
  }).catch(function() {});
};

MixerConsole.prototype.saveConfig = function() {
  var channelArr = [];
  for (var sid in this.channels) {
    var ch = this.channels[sid];
    channelArr.push({
      slot_id: parseInt(sid),
      volume: ch.volume,
      pan: ch.pan || 0,
      muted: !!ch.muted,
      solo: !!ch.solo
    });
  }

  var nameInput = document.getElementById('mixer-config-name');
  var name = nameInput ? nameInput.value.trim() : 'Default';
  if (!name) name = 'Default';

  var body = {
    config_name: name,
    master_volume: this.master.volume,
    skin: this.skin,
    channel_json: channelArr
  };

  mc1Api('PUT', '/api/v1/mixer/config', body).then(function(d) {
    if (d && d.ok) {
      mc1Toast('Mixer config saved', 'ok');
    } else {
      mc1Toast('Failed to save config: ' + (d.error || 'unknown'), 'err');
    }
  }).catch(function() {
    mc1Toast('API unreachable', 'err');
  });
};

MixerConsole.prototype.resetAll = function() {
  for (var sid in this.channels) {
    this.channels[sid].volume = FADER_DEF;
    this.channels[sid].pan = 0;
    this.channels[sid].muted = false;
    this.channels[sid].solo = false;
    this._updateFaderVisual(sid, FADER_DEF);
    this._updatePanVisual(sid, 0);
    this._sendVolume(sid, FADER_DEF);
  }
  this.master.volume = FADER_DEF;
  this._updateFaderVisual('master', FADER_DEF);
  this._sendVolume('master', FADER_DEF);
  this.soloActive = false;
  this._render();
  mc1Toast('Mixer reset to defaults', 'ok');
};

/* ── Utility ───────────────────────────────────────────── */
function esc(s) {
  return String(s || '').replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

// Export
window.MixerConsole = MixerConsole;

})();
