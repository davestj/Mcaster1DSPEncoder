/**
 * daw-engine.js — Multi-Track DAW Engine for Mcaster1 DSP Producer
 *
 * File:    src/linux/web_ui/js/daw-engine.js
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Phase:   DAW-1
 *
 * We provide a full multi-track DAW engine with:
 *   - Track and clip management (add, remove, move, split, duplicate)
 *   - Web Audio API playback with per-track gain + pan
 *   - WebGL waveform rendering via DawWaveformRenderer
 *   - Timeline interaction (zoom, scroll, drag clips, context menu)
 *   - Project save/load via server API
 *   - Undo/redo stack
 *   - Export mixdown via server-side ffmpeg
 *
 * Standards:
 *   - We use Web Audio API AudioBufferSourceNode for clip playback
 *   - We use requestAnimationFrame for render loop
 *   - We decode audio via AudioContext.decodeAudioData()
 *   - We never call exit()/die() or block the main thread
 */

/* global mc1Api, mc1Toast, DawWaveformRenderer */

function DawEngine(containerId) {
    var self = this;

    /* ── Constants ── */
    self.TRACK_HEIGHT  = 80;
    self.RULER_HEIGHT  = 24;
    self.TRACK_COLORS  = [
        '#14b8a6', '#0891b2', '#8b5cf6', '#ec4899', '#f97316',
        '#22c55e', '#eab308', '#ef4444', '#6366f1', '#06b6d4',
        '#a855f7', '#f43f5e', '#84cc16', '#d946ef', '#fb923c'
    ];

    /* ── State ── */
    self.containerId  = containerId;
    self.tracks       = [];
    self.nextTrackId  = 1;
    self.nextClipId   = 1;
    self.projectId    = null;
    self.projectName  = 'Untitled';
    self.bpm          = 120;
    self.timeSignature = '4/4';
    self.snapMode     = '1'; // '0' = none, 'beat', 'bar', or seconds string
    self.playing      = false;
    self.playPos      = 0;     // current position in seconds
    self.playStartTime = 0;    // audioCtx.currentTime when play was pressed
    self.playStartPos  = 0;    // playPos when play was pressed
    self.selectedClip = null;
    self.selectedTrack = null;
    self.ctxClip      = null;  // clip for context menu

    /* ── View state ── */
    self.pixelsPerSec = 100;
    self.scrollX      = 0;     // pixels
    self.totalDuration = 300;  // timeline total in seconds (grows as needed)

    /* ── Audio ── */
    self.audioCtx     = null;
    self.masterGain   = null;
    self.trackNodes   = {};    // trackId -> { gain: GainNode, pan: StereoPannerNode }
    self.activeSources = [];   // { source, clipId, trackId }

    /* ── Undo/Redo ── */
    self.undoStack    = [];
    self.redoStack    = [];
    self.MAX_UNDO     = 50;

    /* ── DOM refs ── */
    self.root         = document.getElementById(containerId);
    self.trackListEl  = document.getElementById('track-list');
    self.canvasWrap   = document.getElementById('canvas-wrap');
    self.waveCanvas   = document.getElementById('daw-waveform-canvas');
    self.overlayCanvas = document.getElementById('daw-overlay-canvas');
    self.rulerCanvas  = document.getElementById('ruler-canvas');
    self.playheadEl   = document.getElementById('playhead');
    self.tooltipEl    = document.getElementById('clip-tooltip');
    self.timeDisplay  = document.getElementById('time-display');
    self.hscroll      = document.getElementById('hscroll');
    self.hscrollInner = document.getElementById('hscroll-inner');
    self.dropZone     = document.getElementById('drop-zone');

    /* ── Renderer ── */
    self.waveRenderer = null;

    /* ── Init ── */
    self._initAudio();
    self._initRenderer();
    self._initInteraction();
    self._startRenderLoop();
    self._initDragDrop();

    // Start with one empty track
    self.addTrack('Track 1');
}

/* ══════════════════════════════════════════════════════════════
 *  AUDIO CONTEXT
 * ══════════════════════════════════════════════════════════════ */

DawEngine.prototype._initAudio = function() {
    var self = this;
    try {
        self.audioCtx = new (window.AudioContext || window.webkitAudioContext)();
        self.masterGain = self.audioCtx.createGain();
        self.masterGain.gain.value = 1.0;
        self.masterGain.connect(self.audioCtx.destination);
    } catch (e) {
        console.error('DAW: Failed to create AudioContext:', e);
    }
};

DawEngine.prototype._ensureAudioCtx = function() {
    if (this.audioCtx && this.audioCtx.state === 'suspended') {
        this.audioCtx.resume();
    }
};

DawEngine.prototype._createTrackNodes = function(trackId) {
    var self = this;
    if (self.trackNodes[trackId]) return;
    var gain = self.audioCtx.createGain();
    var pan = self.audioCtx.createStereoPanner();
    pan.connect(gain);
    gain.connect(self.masterGain);
    self.trackNodes[trackId] = { gain: gain, pan: pan };
};

DawEngine.prototype._removeTrackNodes = function(trackId) {
    var nodes = this.trackNodes[trackId];
    if (!nodes) return;
    try { nodes.gain.disconnect(); } catch (e) {}
    try { nodes.pan.disconnect(); } catch (e) {}
    delete this.trackNodes[trackId];
};

/* ══════════════════════════════════════════════════════════════
 *  RENDERER INIT
 * ══════════════════════════════════════════════════════════════ */

DawEngine.prototype._initRenderer = function() {
    var self = this;
    self._resizeCanvases();
    try {
        self.waveRenderer = new DawWaveformRenderer(self.waveCanvas);
    } catch (e) {
        console.warn('DAW: WebGL renderer failed, using fallback:', e);
        self.waveRenderer = null;
    }
    window.addEventListener('resize', function() { self._resizeCanvases(); });
};

DawEngine.prototype._resizeCanvases = function() {
    var self = this;
    var wrap = self.canvasWrap;
    if (!wrap) return;
    var w = wrap.clientWidth;
    var h = Math.max(wrap.clientHeight, self.tracks.length * self.TRACK_HEIGHT || 240);

    var dpr = window.devicePixelRatio || 1;

    // Waveform canvas
    self.waveCanvas.width = w * dpr;
    self.waveCanvas.height = h * dpr;
    self.waveCanvas.style.width = w + 'px';
    self.waveCanvas.style.height = h + 'px';

    // Overlay canvas
    self.overlayCanvas.width = w * dpr;
    self.overlayCanvas.height = h * dpr;
    self.overlayCanvas.style.width = w + 'px';
    self.overlayCanvas.style.height = h + 'px';

    // Ruler canvas
    var rulerW = self.canvasWrap.clientWidth;
    self.rulerCanvas.width = rulerW * dpr;
    self.rulerCanvas.height = self.RULER_HEIGHT * dpr;
    self.rulerCanvas.style.width = rulerW + 'px';
    self.rulerCanvas.style.height = self.RULER_HEIGHT + 'px';

    // Horizontal scrollbar
    self.hscrollInner.style.width = (self.totalDuration * self.pixelsPerSec) + 'px';

    if (self.waveRenderer) self.waveRenderer.resize(w, h);
};

/* ══════════════════════════════════════════════════════════════
 *  TRACK MANAGEMENT
 * ══════════════════════════════════════════════════════════════ */

DawEngine.prototype.addTrack = function(name) {
    var self = this;
    var id = 'track_' + self.nextTrackId++;
    var color = self.TRACK_COLORS[(self.tracks.length) % self.TRACK_COLORS.length];
    var track = {
        id: id,
        name: name || ('Track ' + self.tracks.length + 1),
        muted: false,
        solo: false,
        volume: 1.0,
        pan: 0.0,
        clips: [],
        color: color
    };
    self.tracks.push(track);
    self._createTrackNodes(id);
    self._renderTrackList();
    self._resizeCanvases();
    self._pushUndo('addTrack');
    return track;
};

DawEngine.prototype.removeTrack = function(trackId) {
    var self = this;
    var idx = self.tracks.findIndex(function(t) { return t.id === trackId; });
    if (idx < 0) return;
    self._pushUndo('removeTrack');
    // Stop any playing sources for this track
    self._stopTrackSources(trackId);
    self._removeTrackNodes(trackId);
    self.tracks.splice(idx, 1);
    self._renderTrackList();
    self._resizeCanvases();
};

DawEngine.prototype._getTrack = function(trackId) {
    return this.tracks.find(function(t) { return t.id === trackId; });
};

DawEngine.prototype._getClip = function(clipId) {
    for (var i = 0; i < this.tracks.length; i++) {
        for (var j = 0; j < this.tracks[i].clips.length; j++) {
            if (this.tracks[i].clips[j].id === clipId) {
                return { track: this.tracks[i], clip: this.tracks[i].clips[j], trackIdx: i, clipIdx: j };
            }
        }
    }
    return null;
};

/* ══════════════════════════════════════════════════════════════
 *  CLIP MANAGEMENT
 * ══════════════════════════════════════════════════════════════ */

DawEngine.prototype.addClip = function(trackId, audioBuffer, name, startTime, peaks) {
    var self = this;
    var track = self._getTrack(trackId);
    if (!track) return null;
    var clip = {
        id: 'clip_' + self.nextClipId++,
        name: name || 'Clip',
        audioBuffer: audioBuffer,
        peaks: peaks || self._computePeaks(audioBuffer),
        startTime: startTime || 0,
        duration: audioBuffer.duration,
        offset: 0,
        fadeIn: 0,
        fadeOut: 0,
        color: track.color
    };
    track.clips.push(clip);
    // Extend timeline if needed
    var end = clip.startTime + clip.duration;
    if (end + 30 > self.totalDuration) {
        self.totalDuration = end + 60;
        self.hscrollInner.style.width = (self.totalDuration * self.pixelsPerSec) + 'px';
    }
    self._pushUndo('addClip');
    return clip;
};

DawEngine.prototype.addClipFromLibrary = function(trackId, title) {
    var self = this;
    // If no track specified, use the first track or selected track
    var targetTrack = null;
    if (typeof trackId === 'number') {
        // trackId is actually a track DB id — load audio from the API
        var dbTrackId = trackId;
        var trackTarget = self.selectedTrack ? self._getTrack(self.selectedTrack) : self.tracks[0];
        if (!trackTarget) { trackTarget = self.addTrack(); }

        mc1Toast('Loading audio...', 'info');
        var url = '/app/api/audio.php?id=' + dbTrackId;
        self._loadAudio(url, function(buffer) {
            var clip = self.addClip(trackTarget.id, buffer, title, self.playPos);
            if (clip) mc1Toast('Added: ' + title, 'ok');
        }, function(err) {
            mc1Toast('Failed to load audio: ' + err, 'err');
        });
        document.getElementById('modal-library').classList.remove('open');
        return;
    }
};

DawEngine.prototype.removeClip = function(clipId) {
    var self = this;
    var found = self._getClip(clipId);
    if (!found) return;
    self._pushUndo('removeClip');
    found.track.clips.splice(found.clipIdx, 1);
    if (self.selectedClip === clipId) self.selectedClip = null;
};

DawEngine.prototype.moveClip = function(clipId, newTrackId, newStart) {
    var self = this;
    var found = self._getClip(clipId);
    if (!found) return;
    self._pushUndo('moveClip');
    // Remove from old track
    found.track.clips.splice(found.clipIdx, 1);
    // Add to new track
    var newTrack = self._getTrack(newTrackId) || found.track;
    found.clip.startTime = Math.max(0, newStart);
    found.clip.color = newTrack.color;
    newTrack.clips.push(found.clip);
};

DawEngine.prototype.splitClip = function(clipId, atTime) {
    var self = this;
    var found = self._getClip(clipId);
    if (!found) return;
    var clip = found.clip;
    if (atTime <= clip.startTime || atTime >= clip.startTime + clip.duration) return;

    self._pushUndo('splitClip');

    var splitOffset = atTime - clip.startTime;
    // First half
    var dur1 = splitOffset;
    // Second half
    var dur2 = clip.duration - splitOffset;

    // Modify existing clip (first half)
    clip.duration = dur1;
    clip.fadeOut = 0;

    // Create second clip
    var clip2 = {
        id: 'clip_' + self.nextClipId++,
        name: clip.name + ' (2)',
        audioBuffer: clip.audioBuffer,
        peaks: clip.peaks,
        startTime: atTime,
        duration: dur2,
        offset: clip.offset + splitOffset,
        fadeIn: 0,
        fadeOut: clip.fadeOut,
        color: clip.color
    };
    found.track.clips.push(clip2);
};

DawEngine.prototype.duplicateClip = function(clipId) {
    var self = this;
    var found = self._getClip(clipId);
    if (!found) return;
    self._pushUndo('duplicateClip');
    var orig = found.clip;
    var newClip = {
        id: 'clip_' + self.nextClipId++,
        name: orig.name + ' (copy)',
        audioBuffer: orig.audioBuffer,
        peaks: orig.peaks,
        startTime: orig.startTime + orig.duration + 0.5,
        duration: orig.duration,
        offset: orig.offset,
        fadeIn: orig.fadeIn,
        fadeOut: orig.fadeOut,
        color: orig.color
    };
    found.track.clips.push(newClip);
};

DawEngine.prototype.deleteSelectedClip = function() {
    if (this.selectedClip) this.removeClip(this.selectedClip);
};

/* ══════════════════════════════════════════════════════════════
 *  AUDIO LOADING
 * ══════════════════════════════════════════════════════════════ */

DawEngine.prototype._loadAudio = function(url, onSuccess, onError) {
    var self = this;
    self._ensureAudioCtx();
    fetch(url, { credentials: 'same-origin' })
        .then(function(r) {
            if (!r.ok) throw new Error('HTTP ' + r.status);
            return r.arrayBuffer();
        })
        .then(function(buf) {
            return self.audioCtx.decodeAudioData(buf);
        })
        .then(onSuccess)
        .catch(function(e) {
            console.error('DAW: Audio load error:', e);
            if (onError) onError(e.message || 'Decode error');
        });
};

DawEngine.prototype._computePeaks = function(audioBuffer) {
    var ch = audioBuffer.getChannelData(0);
    var len = ch.length;
    var samplesPerPeak = Math.max(1, Math.floor(audioBuffer.sampleRate / 100)); // 100 peaks/sec
    var numPeaks = Math.ceil(len / samplesPerPeak);
    var peaks = new Float32Array(numPeaks * 2); // [min, max, min, max, ...]
    for (var i = 0; i < numPeaks; i++) {
        var start = i * samplesPerPeak;
        var end = Math.min(start + samplesPerPeak, len);
        var min = 1, max = -1;
        for (var j = start; j < end; j++) {
            var v = ch[j];
            if (v < min) min = v;
            if (v > max) max = v;
        }
        peaks[i * 2] = min;
        peaks[i * 2 + 1] = max;
    }
    return { data: peaks, peaksPerSec: 100 };
};

/* ══════════════════════════════════════════════════════════════
 *  TRANSPORT
 * ══════════════════════════════════════════════════════════════ */

DawEngine.prototype.play = function() {
    var self = this;
    if (self.playing) return;
    self._ensureAudioCtx();
    self.playing = true;
    self.playStartTime = self.audioCtx.currentTime;
    self.playStartPos = self.playPos;
    self._schedulePlayback();
    self._updatePlayButton(true);
};

DawEngine.prototype.pause = function() {
    var self = this;
    if (!self.playing) return;
    self.playPos = self.playStartPos + (self.audioCtx.currentTime - self.playStartTime);
    self.playing = false;
    self._stopAllSources();
    self._updatePlayButton(false);
};

DawEngine.prototype.stop = function() {
    var self = this;
    self.playing = false;
    self.playPos = 0;
    self._stopAllSources();
    self._updatePlayButton(false);
};

DawEngine.prototype.seek = function(time) {
    var self = this;
    var wasPlaying = self.playing;
    if (wasPlaying) self._stopAllSources();
    self.playPos = Math.max(0, time);
    self.playStartPos = self.playPos;
    self.playStartTime = self.audioCtx ? self.audioCtx.currentTime : 0;
    if (wasPlaying) self._schedulePlayback();
};

DawEngine.prototype._schedulePlayback = function() {
    var self = this;
    self._stopAllSources();
    var curTime = self.playPos;

    for (var ti = 0; ti < self.tracks.length; ti++) {
        var track = self.tracks[ti];
        if (track.muted) continue;
        // Solo logic: if any track is soloed, only play soloed tracks
        var anySolo = self.tracks.some(function(t) { return t.solo; });
        if (anySolo && !track.solo) continue;

        self._createTrackNodes(track.id);
        var nodes = self.trackNodes[track.id];
        nodes.gain.gain.value = track.volume;
        nodes.pan.pan.value = track.pan;

        for (var ci = 0; ci < track.clips.length; ci++) {
            var clip = track.clips[ci];
            var clipEnd = clip.startTime + clip.duration;
            if (clipEnd <= curTime) continue; // clip already passed

            var source = self.audioCtx.createBufferSource();
            source.buffer = clip.audioBuffer;

            // Apply fade in/out via gain envelope
            var clipGain = self.audioCtx.createGain();
            clipGain.connect(nodes.pan);

            // Schedule
            var when, offset, dur;
            if (curTime > clip.startTime) {
                // We're in the middle of this clip
                when = 0;
                offset = clip.offset + (curTime - clip.startTime);
                dur = clip.duration - (curTime - clip.startTime);
            } else {
                // Clip starts in the future
                when = clip.startTime - curTime;
                offset = clip.offset;
                dur = clip.duration;
            }

            // Fade in
            if (clip.fadeIn > 0 && curTime <= clip.startTime) {
                var fadeStart = self.audioCtx.currentTime + when;
                clipGain.gain.setValueAtTime(0, fadeStart);
                clipGain.gain.linearRampToValueAtTime(1, fadeStart + clip.fadeIn);
            }
            // Fade out
            if (clip.fadeOut > 0) {
                var fadeOutStart = self.audioCtx.currentTime + when + dur - clip.fadeOut;
                clipGain.gain.setValueAtTime(1, Math.max(fadeOutStart, self.audioCtx.currentTime));
                clipGain.gain.linearRampToValueAtTime(0, fadeOutStart + clip.fadeOut);
            }

            source.connect(clipGain);
            source.start(self.audioCtx.currentTime + when, offset, dur);
            self.activeSources.push({ source: source, clipId: clip.id, trackId: track.id });
        }
    }
};

DawEngine.prototype._stopAllSources = function() {
    for (var i = 0; i < this.activeSources.length; i++) {
        try { this.activeSources[i].source.stop(); } catch (e) {}
    }
    this.activeSources = [];
};

DawEngine.prototype._stopTrackSources = function(trackId) {
    var keep = [];
    for (var i = 0; i < this.activeSources.length; i++) {
        if (this.activeSources[i].trackId === trackId) {
            try { this.activeSources[i].source.stop(); } catch (e) {}
        } else {
            keep.push(this.activeSources[i]);
        }
    }
    this.activeSources = keep;
};

DawEngine.prototype._updatePlayButton = function(isPlaying) {
    var btn = document.getElementById('btn-play');
    if (!btn) return;
    btn.innerHTML = isPlaying
        ? '<i class="fa-solid fa-pause"></i>'
        : '<i class="fa-solid fa-play"></i>';
    btn.title = isPlaying ? 'Pause' : 'Play';
};

/* ══════════════════════════════════════════════════════════════
 *  ZOOM / SCROLL
 * ══════════════════════════════════════════════════════════════ */

DawEngine.prototype.setZoom = function(pxPerSec) {
    this.pixelsPerSec = pxPerSec;
    this.hscrollInner.style.width = (this.totalDuration * pxPerSec) + 'px';
};

DawEngine.prototype.snapTime = function(t) {
    var self = this;
    if (self.snapMode === '0') return t;
    if (self.snapMode === 'beat') {
        var beatLen = 60 / self.bpm;
        return Math.round(t / beatLen) * beatLen;
    }
    if (self.snapMode === 'bar') {
        var beatLen2 = 60 / self.bpm;
        var beatsPerBar = parseInt(self.timeSignature) || 4;
        var barLen = beatLen2 * beatsPerBar;
        return Math.round(t / barLen) * barLen;
    }
    var snap = parseFloat(self.snapMode);
    if (snap > 0) return Math.round(t / snap) * snap;
    return t;
};

/* ══════════════════════════════════════════════════════════════
 *  RENDER LOOP
 * ══════════════════════════════════════════════════════════════ */

DawEngine.prototype._startRenderLoop = function() {
    var self = this;
    function frame() {
        self._updatePlayPos();
        self._drawRuler();
        self._drawWaveforms();
        self._drawOverlay();
        self._updateTimeDisplay();
        self._updatePlayhead();
        requestAnimationFrame(frame);
    }
    requestAnimationFrame(frame);
};

DawEngine.prototype._updatePlayPos = function() {
    if (this.playing && this.audioCtx) {
        this.playPos = this.playStartPos + (this.audioCtx.currentTime - this.playStartTime);
    }
};

DawEngine.prototype._updateTimeDisplay = function() {
    var self = this;
    var cur = self.playPos;
    var total = self._getProjectDuration();
    self.timeDisplay.textContent = self._fmtTimeFull(cur) + ' / ' + self._fmtTimeFull(total);
};

DawEngine.prototype._fmtTimeFull = function(s) {
    var h = Math.floor(s / 3600);
    var m = Math.floor((s % 3600) / 60);
    var sec = Math.floor(s % 60);
    var ms = Math.floor((s % 1) * 1000);
    return (h < 10 ? '0' : '') + h + ':' +
           (m < 10 ? '0' : '') + m + ':' +
           (sec < 10 ? '0' : '') + sec + '.' +
           (ms < 100 ? (ms < 10 ? '00' : '0') : '') + ms;
};

DawEngine.prototype._getProjectDuration = function() {
    var maxEnd = 0;
    for (var i = 0; i < this.tracks.length; i++) {
        for (var j = 0; j < this.tracks[i].clips.length; j++) {
            var c = this.tracks[i].clips[j];
            var end = c.startTime + c.duration;
            if (end > maxEnd) maxEnd = end;
        }
    }
    return maxEnd;
};

DawEngine.prototype._updatePlayhead = function() {
    var self = this;
    var x = (self.playPos * self.pixelsPerSec) - self.scrollX;
    self.playheadEl.style.left = x + 'px';
    self.playheadEl.style.height = (self.tracks.length * self.TRACK_HEIGHT) + 'px';
    self.playheadEl.style.display = (x >= -2 && x <= self.canvasWrap.clientWidth + 2) ? '' : 'none';
};

/* ══════════════════════════════════════════════════════════════
 *  RULER DRAWING (Canvas 2D)
 * ══════════════════════════════════════════════════════════════ */

DawEngine.prototype._drawRuler = function() {
    var self = this;
    var canvas = self.rulerCanvas;
    var ctx = canvas.getContext('2d');
    var dpr = window.devicePixelRatio || 1;
    var w = canvas.width / dpr;
    var h = canvas.height / dpr;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, w, h);

    ctx.fillStyle = 'rgba(0,0,0,0.3)';
    ctx.fillRect(0, 0, w, h);

    // Determine tick interval based on zoom
    var pps = self.pixelsPerSec;
    var interval;
    if (pps >= 200) interval = 1;
    else if (pps >= 50) interval = 5;
    else if (pps >= 20) interval = 10;
    else interval = 30;

    var startSec = Math.floor(self.scrollX / pps / interval) * interval;
    var endSec = startSec + (w / pps) + interval;

    ctx.font = '10px -apple-system, sans-serif';
    ctx.textAlign = 'center';

    for (var s = startSec; s <= endSec; s += interval) {
        var x = (s * pps) - self.scrollX;
        if (x < -20 || x > w + 20) continue;

        // Major tick
        ctx.strokeStyle = 'rgba(148,163,184,0.4)';
        ctx.beginPath();
        ctx.moveTo(x, h - 8);
        ctx.lineTo(x, h);
        ctx.stroke();

        // Label
        ctx.fillStyle = '#94a3b8';
        var label;
        if (s >= 3600) label = Math.floor(s / 3600) + ':' + ('0' + Math.floor((s % 3600) / 60)).slice(-2) + ':' + ('0' + (s % 60)).slice(-2);
        else label = Math.floor(s / 60) + ':' + ('0' + (s % 60)).slice(-2);
        ctx.fillText(label, x, h - 10);

        // Minor ticks
        if (interval >= 5) {
            for (var m = 1; m < interval; m++) {
                var mx = ((s + m) * pps) - self.scrollX;
                if (mx >= 0 && mx <= w) {
                    ctx.strokeStyle = 'rgba(148,163,184,0.15)';
                    ctx.beginPath();
                    ctx.moveTo(mx, h - 4);
                    ctx.lineTo(mx, h);
                    ctx.stroke();
                }
            }
        }
    }

    // Playhead marker on ruler
    var phx = (self.playPos * pps) - self.scrollX;
    if (phx >= -5 && phx <= w + 5) {
        ctx.fillStyle = '#ef4444';
        ctx.beginPath();
        ctx.moveTo(phx - 4, 0);
        ctx.lineTo(phx + 4, 0);
        ctx.lineTo(phx, 6);
        ctx.closePath();
        ctx.fill();
    }
};

/* ══════════════════════════════════════════════════════════════
 *  WAVEFORM DRAWING (WebGL or Canvas 2D fallback)
 * ══════════════════════════════════════════════════════════════ */

DawEngine.prototype._drawWaveforms = function() {
    var self = this;
    if (self.waveRenderer && self.waveRenderer.ok) {
        self.waveRenderer.draw(self.tracks, self.pixelsPerSec, self.scrollX, self.TRACK_HEIGHT, self.selectedClip);
    } else {
        self._drawWaveformsFallback();
    }
};

DawEngine.prototype._drawWaveformsFallback = function() {
    var self = this;
    var canvas = self.waveCanvas;
    var ctx = canvas.getContext('2d');
    var dpr = window.devicePixelRatio || 1;
    var w = canvas.width / dpr;
    var h = canvas.height / dpr;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, w, h);

    var pps = self.pixelsPerSec;
    var scrollSec = self.scrollX / pps;
    var visSec = w / pps;

    for (var ti = 0; ti < self.tracks.length; ti++) {
        var track = self.tracks[ti];
        var yBase = ti * self.TRACK_HEIGHT;

        // Track background
        ctx.fillStyle = ti % 2 === 0 ? 'rgba(255,255,255,0.015)' : 'rgba(0,0,0,0.05)';
        ctx.fillRect(0, yBase, w, self.TRACK_HEIGHT);

        // Track separator
        ctx.strokeStyle = 'rgba(51,65,85,0.5)';
        ctx.beginPath();
        ctx.moveTo(0, yBase + self.TRACK_HEIGHT);
        ctx.lineTo(w, yBase + self.TRACK_HEIGHT);
        ctx.stroke();

        for (var ci = 0; ci < track.clips.length; ci++) {
            var clip = track.clips[ci];
            var clipEndSec = clip.startTime + clip.duration;

            // Skip clips outside view
            if (clipEndSec < scrollSec || clip.startTime > scrollSec + visSec) continue;

            var x1 = (clip.startTime * pps) - self.scrollX;
            var x2 = (clipEndSec * pps) - self.scrollX;
            var clipW = x2 - x1;
            var yMid = yBase + self.TRACK_HEIGHT / 2;
            var halfH = (self.TRACK_HEIGHT - 10) / 2;

            // Clip background
            var alpha = (self.selectedClip === clip.id) ? 0.3 : 0.15;
            ctx.fillStyle = clip.color + (alpha < 0.2 ? '26' : '4d');
            ctx.fillRect(x1, yBase + 2, clipW, self.TRACK_HEIGHT - 4);

            // Clip border
            ctx.strokeStyle = (self.selectedClip === clip.id) ? clip.color : (clip.color + '80');
            ctx.lineWidth = (self.selectedClip === clip.id) ? 2 : 1;
            ctx.strokeRect(x1, yBase + 2, clipW, self.TRACK_HEIGHT - 4);
            ctx.lineWidth = 1;

            // Draw waveform peaks
            if (clip.peaks && clip.peaks.data) {
                var peaksPerSec = clip.peaks.peaksPerSec;
                ctx.fillStyle = clip.color + 'cc';
                ctx.beginPath();

                var startPeak = Math.floor(clip.offset * peaksPerSec);
                var pxStart = Math.max(0, x1);
                var pxEnd = Math.min(w, x2);

                for (var px = pxStart; px < pxEnd; px++) {
                    var timeSec = ((px + self.scrollX) / pps) - clip.startTime + clip.offset;
                    var peakIdx = Math.floor(timeSec * peaksPerSec);
                    if (peakIdx < 0 || peakIdx * 2 + 1 >= clip.peaks.data.length) continue;
                    var min = clip.peaks.data[peakIdx * 2];
                    var max = clip.peaks.data[peakIdx * 2 + 1];
                    var y1p = yMid - max * halfH;
                    var y2p = yMid - min * halfH;
                    ctx.fillRect(px, y1p, 1, Math.max(1, y2p - y1p));
                }
            }

            // Clip name
            ctx.fillStyle = '#e2e8f0';
            ctx.font = '10px -apple-system, sans-serif';
            ctx.textAlign = 'left';
            var nameX = Math.max(x1 + 4, 4);
            if (clipW > 40) {
                ctx.fillText(clip.name, nameX, yBase + 14, clipW - 8);
            }
        }
    }
};

/* ══════════════════════════════════════════════════════════════
 *  OVERLAY DRAWING (selection, grid, etc.)
 * ══════════════════════════════════════════════════════════════ */

DawEngine.prototype._drawOverlay = function() {
    var self = this;
    var canvas = self.overlayCanvas;
    var ctx = canvas.getContext('2d');
    var dpr = window.devicePixelRatio || 1;
    var w = canvas.width / dpr;
    var h = canvas.height / dpr;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, w, h);

    // Beat grid lines
    if (self.snapMode === 'beat' || self.snapMode === 'bar') {
        var beatLen = 60 / self.bpm;
        var step = self.snapMode === 'bar' ? beatLen * (parseInt(self.timeSignature) || 4) : beatLen;
        var startSec = Math.floor(self.scrollX / self.pixelsPerSec / step) * step;
        var endSec = startSec + (w / self.pixelsPerSec) + step;
        ctx.strokeStyle = 'rgba(148,163,184,0.07)';
        for (var s = startSec; s <= endSec; s += step) {
            var x = (s * self.pixelsPerSec) - self.scrollX;
            if (x < 0 || x > w) continue;
            ctx.beginPath();
            ctx.moveTo(x, 0);
            ctx.lineTo(x, h);
            ctx.stroke();
        }
    }
};

/* ══════════════════════════════════════════════════════════════
 *  TRACK LIST UI
 * ══════════════════════════════════════════════════════════════ */

DawEngine.prototype._renderTrackList = function() {
    var self = this;
    var container = self.trackListEl;
    // Keep the header
    var header = container.querySelector('.track-header');
    container.innerHTML = '';
    container.appendChild(header);

    for (var i = 0; i < self.tracks.length; i++) {
        var track = self.tracks[i];
        var panel = document.createElement('div');
        panel.className = 'track-panel' + (self.selectedTrack === track.id ? ' selected' : '');
        panel.dataset.trackId = track.id;
        panel.style.height = self.TRACK_HEIGHT + 'px';

        panel.innerHTML =
            '<div class="track-name-row">' +
            '  <div class="track-color" style="background:' + track.color + '" data-track="' + track.id + '"></div>' +
            '  <span class="track-name" contenteditable="true" data-track="' + track.id + '">' + self._esc(track.name) + '</span>' +
            '  <div class="track-btns">' +
            '    <button class="track-btn' + (track.muted ? ' muted' : '') + '" data-action="mute" data-track="' + track.id + '" title="Mute">M</button>' +
            '    <button class="track-btn' + (track.solo ? ' soloed' : '') + '" data-action="solo" data-track="' + track.id + '" title="Solo">S</button>' +
            '  </div>' +
            '  <span class="track-del" data-track="' + track.id + '" title="Delete track"><i class="fa-solid fa-trash"></i></span>' +
            '</div>' +
            '<div class="track-vol-row">' +
            '  <span>Vol</span>' +
            '  <input type="range" min="0" max="200" value="' + Math.round(track.volume * 100) + '" data-action="volume" data-track="' + track.id + '">' +
            '  <span class="vol-val">' + Math.round(track.volume * 100) + '%</span>' +
            '</div>' +
            '<div class="track-pan-row">' +
            '  <span>L</span>' +
            '  <input type="range" min="-100" max="100" value="' + Math.round(track.pan * 100) + '" data-action="pan" data-track="' + track.id + '">' +
            '  <span>R</span>' +
            '</div>';

        container.appendChild(panel);
    }

    // Bind events
    container.querySelectorAll('.track-btn').forEach(function(btn) {
        btn.addEventListener('click', function(e) {
            e.stopPropagation();
            var act = btn.dataset.action;
            var tid = btn.dataset.track;
            var tr = self._getTrack(tid);
            if (!tr) return;
            if (act === 'mute') {
                tr.muted = !tr.muted;
                btn.classList.toggle('muted', tr.muted);
                if (self.trackNodes[tid]) self.trackNodes[tid].gain.gain.value = tr.muted ? 0 : tr.volume;
            } else if (act === 'solo') {
                tr.solo = !tr.solo;
                btn.classList.toggle('soloed', tr.solo);
                // Reschedule if playing
                if (self.playing) { self._schedulePlayback(); }
            }
        });
    });

    container.querySelectorAll('.track-del').forEach(function(el) {
        el.addEventListener('click', function(e) {
            e.stopPropagation();
            if (confirm('Delete track "' + self._getTrack(el.dataset.track).name + '"?')) {
                self.removeTrack(el.dataset.track);
            }
        });
    });

    container.querySelectorAll('[data-action="volume"]').forEach(function(inp) {
        inp.addEventListener('input', function() {
            var tid = inp.dataset.track;
            var tr = self._getTrack(tid);
            if (!tr) return;
            tr.volume = parseFloat(inp.value) / 100;
            inp.parentElement.querySelector('.vol-val').textContent = inp.value + '%';
            if (self.trackNodes[tid] && !tr.muted) {
                self.trackNodes[tid].gain.gain.value = tr.volume;
            }
        });
    });

    container.querySelectorAll('[data-action="pan"]').forEach(function(inp) {
        inp.addEventListener('input', function() {
            var tid = inp.dataset.track;
            var tr = self._getTrack(tid);
            if (!tr) return;
            tr.pan = parseFloat(inp.value) / 100;
            if (self.trackNodes[tid]) {
                self.trackNodes[tid].pan.pan.value = tr.pan;
            }
        });
    });

    container.querySelectorAll('.track-name').forEach(function(el) {
        el.addEventListener('blur', function() {
            var tid = el.dataset.track;
            var tr = self._getTrack(tid);
            if (tr) tr.name = el.textContent.trim() || 'Track';
        });
        el.addEventListener('keydown', function(e) {
            if (e.key === 'Enter') { e.preventDefault(); el.blur(); }
        });
    });

    container.querySelectorAll('.track-panel').forEach(function(panel) {
        panel.addEventListener('click', function() {
            self.selectedTrack = panel.dataset.trackId;
            container.querySelectorAll('.track-panel').forEach(function(p) { p.classList.remove('selected'); });
            panel.classList.add('selected');
        });
    });
};

/* ══════════════════════════════════════════════════════════════
 *  TIMELINE INTERACTION
 * ══════════════════════════════════════════════════════════════ */

DawEngine.prototype._initInteraction = function() {
    var self = this;
    var wrap = self.canvasWrap;
    var dragging = false;
    var dragClip = null;
    var dragOffsetX = 0;
    var dragOffsetTrack = 0;
    var dragOrigStart = 0;
    var dragOrigTrack = null;

    // Click on timeline — select clip or seek
    wrap.addEventListener('mousedown', function(e) {
        if (e.button !== 0) return;
        var rect = wrap.getBoundingClientRect();
        var mx = e.clientX - rect.left;
        var my = e.clientY - rect.top;

        var timeSec = (mx + self.scrollX) / self.pixelsPerSec;
        var trackIdx = Math.floor(my / self.TRACK_HEIGHT);

        // Check if clicking on a clip
        var hit = self._hitTestClip(mx, my);
        if (hit) {
            self.selectedClip = hit.clip.id;
            self.selectedTrack = hit.track.id;
            self._renderTrackList();
            // Start drag
            dragging = true;
            dragClip = hit.clip;
            dragOrigStart = hit.clip.startTime;
            dragOrigTrack = hit.track;
            dragOffsetX = timeSec - hit.clip.startTime;
            dragOffsetTrack = trackIdx;
            e.preventDefault();
        } else {
            self.selectedClip = null;
            // Seek to position
            self.seek(self.snapTime(timeSec));
        }
    });

    wrap.addEventListener('mousemove', function(e) {
        var rect = wrap.getBoundingClientRect();
        var mx = e.clientX - rect.left;
        var my = e.clientY - rect.top;

        if (dragging && dragClip) {
            var timeSec = (mx + self.scrollX) / self.pixelsPerSec;
            var newStart = self.snapTime(timeSec - dragOffsetX);
            var newTrackIdx = Math.floor(my / self.TRACK_HEIGHT);
            newTrackIdx = Math.max(0, Math.min(self.tracks.length - 1, newTrackIdx));
            var newTrack = self.tracks[newTrackIdx];

            if (newTrack && (newTrack.id !== dragOrigTrack.id || newStart !== dragClip.startTime)) {
                self.moveClip(dragClip.id, newTrack.id, Math.max(0, newStart));
                dragOrigTrack = newTrack;
            }
        } else {
            // Tooltip on hover
            var hit = self._hitTestClip(mx, my);
            if (hit) {
                self.tooltipEl.textContent = hit.clip.name + ' (' + self._fmtTimeFull(hit.clip.duration).substring(3) + ')';
                self.tooltipEl.style.left = (mx + 10) + 'px';
                self.tooltipEl.style.top = (my - 20) + 'px';
                self.tooltipEl.style.display = 'block';
                wrap.style.cursor = 'grab';
            } else {
                self.tooltipEl.style.display = 'none';
                wrap.style.cursor = 'default';
            }
        }
    });

    wrap.addEventListener('mouseup', function() {
        if (dragging) {
            dragging = false;
            dragClip = null;
        }
    });

    wrap.addEventListener('mouseleave', function() {
        self.tooltipEl.style.display = 'none';
        if (dragging) { dragging = false; dragClip = null; }
    });

    // Right-click context menu on clip
    wrap.addEventListener('contextmenu', function(e) {
        e.preventDefault();
        var rect = wrap.getBoundingClientRect();
        var mx = e.clientX - rect.left;
        var my = e.clientY - rect.top;
        var hit = self._hitTestClip(mx, my);
        if (hit) {
            self.ctxClip = hit.clip;
            self.selectedClip = hit.clip.id;
            var ctx = document.getElementById('ctx-menu');
            ctx.style.left = e.clientX + 'px';
            ctx.style.top = e.clientY + 'px';
            ctx.style.display = 'block';
        }
    });

    // Horizontal scroll sync
    self.hscroll.addEventListener('scroll', function() {
        self.scrollX = self.hscroll.scrollLeft;
    });

    // Mouse wheel zoom
    wrap.addEventListener('wheel', function(e) {
        if (e.shiftKey) {
            // Horizontal scroll
            self.hscroll.scrollLeft += e.deltaY;
            self.scrollX = self.hscroll.scrollLeft;
            e.preventDefault();
        } else if (e.ctrlKey) {
            // Zoom
            e.preventDefault();
            var delta = e.deltaY > 0 ? -10 : 10;
            var newPps = Math.max(10, Math.min(500, self.pixelsPerSec + delta));
            self.setZoom(newPps);
            document.getElementById('zoom-slider').value = newPps;
            document.getElementById('zoom-label').textContent = newPps + ' px/s';
        }
    }, { passive: false });

    // Double-click on empty track area to add clip
    wrap.addEventListener('dblclick', function(e) {
        var rect = wrap.getBoundingClientRect();
        var mx = e.clientX - rect.left;
        var my = e.clientY - rect.top;
        var hit = self._hitTestClip(mx, my);
        if (!hit) {
            var trackIdx = Math.floor(my / self.TRACK_HEIGHT);
            if (trackIdx >= 0 && trackIdx < self.tracks.length) {
                self.selectedTrack = self.tracks[trackIdx].id;
                document.getElementById('modal-library').classList.add('open');
                document.getElementById('lib-search').focus();
            }
        }
    });

    // Ruler click to seek
    document.getElementById('ruler-area').addEventListener('mousedown', function(e) {
        var rect = self.canvasWrap.getBoundingClientRect();
        var mx = e.clientX - rect.left;
        var timeSec = (mx + self.scrollX) / self.pixelsPerSec;
        self.seek(self.snapTime(Math.max(0, timeSec)));
    });
};

DawEngine.prototype._hitTestClip = function(mx, my) {
    var self = this;
    var trackIdx = Math.floor(my / self.TRACK_HEIGHT);
    if (trackIdx < 0 || trackIdx >= self.tracks.length) return null;
    var track = self.tracks[trackIdx];
    var timeSec = (mx + self.scrollX) / self.pixelsPerSec;

    for (var i = track.clips.length - 1; i >= 0; i--) {
        var clip = track.clips[i];
        if (timeSec >= clip.startTime && timeSec <= clip.startTime + clip.duration) {
            return { track: track, clip: clip, trackIdx: trackIdx };
        }
    }
    return null;
};

DawEngine.prototype.handleContextAction = function(action) {
    var self = this;
    if (!self.ctxClip) return;
    var clipId = self.ctxClip.id;

    if (action === 'split') {
        self.splitClip(clipId, self.playPos);
    } else if (action === 'duplicate') {
        self.duplicateClip(clipId);
    } else if (action === 'fadein') {
        var found = self._getClip(clipId);
        if (found) { self._pushUndo('fadeIn'); found.clip.fadeIn = 0.5; }
    } else if (action === 'fadeout') {
        var found2 = self._getClip(clipId);
        if (found2) { self._pushUndo('fadeOut'); found2.clip.fadeOut = 0.5; }
    } else if (action === 'delete') {
        self.removeClip(clipId);
    }
    self.ctxClip = null;
};

/* ══════════════════════════════════════════════════════════════
 *  DRAG AND DROP
 * ══════════════════════════════════════════════════════════════ */

DawEngine.prototype._initDragDrop = function() {
    var self = this;
    var wrap = self.canvasWrap;

    wrap.addEventListener('dragenter', function(e) { e.preventDefault(); self.dropZone.classList.add('active'); });
    wrap.addEventListener('dragover', function(e) { e.preventDefault(); });
    wrap.addEventListener('dragleave', function(e) {
        if (!wrap.contains(e.relatedTarget)) self.dropZone.classList.remove('active');
    });
    wrap.addEventListener('drop', function(e) {
        e.preventDefault();
        self.dropZone.classList.remove('active');
        var files = e.dataTransfer.files;
        if (!files || files.length === 0) return;

        var rect = wrap.getBoundingClientRect();
        var mx = e.clientX - rect.left;
        var my = e.clientY - rect.top;
        var timeSec = self.snapTime((mx + self.scrollX) / self.pixelsPerSec);
        var trackIdx = Math.floor(my / self.TRACK_HEIGHT);
        if (trackIdx < 0 || trackIdx >= self.tracks.length) trackIdx = 0;
        var track = self.tracks[trackIdx] || self.addTrack();

        for (var i = 0; i < files.length; i++) {
            (function(file, idx) {
                if (!file.type.startsWith('audio/')) {
                    mc1Toast('Skipped non-audio file: ' + file.name, 'warn');
                    return;
                }
                var reader = new FileReader();
                reader.onload = function(ev) {
                    self._ensureAudioCtx();
                    self.audioCtx.decodeAudioData(ev.target.result, function(buffer) {
                        var clip = self.addClip(track.id, buffer, file.name, timeSec + idx * 0.5);
                        if (clip) mc1Toast('Added: ' + file.name, 'ok');
                    }, function() {
                        mc1Toast('Failed to decode: ' + file.name, 'err');
                    });
                };
                reader.readAsArrayBuffer(file);
            })(files[i], i);
        }
    });
};

/* ══════════════════════════════════════════════════════════════
 *  UNDO / REDO
 * ══════════════════════════════════════════════════════════════ */

DawEngine.prototype._pushUndo = function(label) {
    var self = this;
    var state = self._serializeState();
    self.undoStack.push({ label: label, state: state });
    if (self.undoStack.length > self.MAX_UNDO) self.undoStack.shift();
    self.redoStack = [];
};

DawEngine.prototype.undo = function() {
    var self = this;
    if (self.undoStack.length === 0) { mc1Toast('Nothing to undo', 'warn'); return; }
    var entry = self.undoStack.pop();
    self.redoStack.push({ label: 'redo', state: self._serializeState() });
    self._restoreState(entry.state);
    mc1Toast('Undo: ' + entry.label, 'ok');
};

DawEngine.prototype.redo = function() {
    var self = this;
    if (self.redoStack.length === 0) { mc1Toast('Nothing to redo', 'warn'); return; }
    var entry = self.redoStack.pop();
    self.undoStack.push({ label: 'undo', state: self._serializeState() });
    self._restoreState(entry.state);
    mc1Toast('Redo', 'ok');
};

DawEngine.prototype._serializeState = function() {
    var self = this;
    // We serialize everything except audioBuffer and peaks (kept by reference)
    var trackData = self.tracks.map(function(t) {
        return {
            id: t.id, name: t.name, muted: t.muted, solo: t.solo,
            volume: t.volume, pan: t.pan, color: t.color,
            clips: t.clips.map(function(c) {
                return {
                    id: c.id, name: c.name, startTime: c.startTime, duration: c.duration,
                    offset: c.offset, fadeIn: c.fadeIn, fadeOut: c.fadeOut, color: c.color,
                    _bufRef: c.audioBuffer, _peaksRef: c.peaks
                };
            })
        };
    });
    return JSON.stringify(trackData);
};

DawEngine.prototype._restoreState = function(stateStr) {
    var self = this;
    var trackData = JSON.parse(stateStr);
    // Rebuild _bufRef and _peaksRef from the stringified marker
    // Since JSON.stringify drops functions and typed arrays, we need
    // to find them from existing clips by id
    var clipMap = {};
    for (var i = 0; i < self.tracks.length; i++) {
        for (var j = 0; j < self.tracks[i].clips.length; j++) {
            var c = self.tracks[i].clips[j];
            clipMap[c.id] = { audioBuffer: c.audioBuffer, peaks: c.peaks };
        }
    }

    self.tracks = trackData.map(function(td) {
        return {
            id: td.id, name: td.name, muted: td.muted, solo: td.solo,
            volume: td.volume, pan: td.pan, color: td.color,
            clips: td.clips.map(function(cd) {
                var ref = clipMap[cd.id] || {};
                return {
                    id: cd.id, name: cd.name, startTime: cd.startTime, duration: cd.duration,
                    offset: cd.offset, fadeIn: cd.fadeIn, fadeOut: cd.fadeOut, color: cd.color,
                    audioBuffer: cd._bufRef || ref.audioBuffer || null,
                    peaks: cd._peaksRef || ref.peaks || null
                };
            })
        };
    });
    self._renderTrackList();
    self._resizeCanvases();
};

/* ══════════════════════════════════════════════════════════════
 *  PROJECT SAVE / LOAD
 * ══════════════════════════════════════════════════════════════ */

DawEngine.prototype.saveProject = function() {
    var self = this;
    var name = self.projectName;
    if (!name || name === 'Untitled') {
        name = prompt('Project name:', self.projectName || 'Untitled');
        if (!name) return;
        self.projectName = name;
    }

    var projectJson = {
        version: 1,
        bpm: self.bpm,
        timeSignature: self.timeSignature,
        tracks: self.tracks.map(function(t) {
            return {
                id: t.id, name: t.name, muted: t.muted, solo: t.solo,
                volume: t.volume, pan: t.pan, color: t.color,
                clips: t.clips.map(function(c) {
                    return {
                        id: c.id, name: c.name, startTime: c.startTime,
                        duration: c.duration, offset: c.offset,
                        fadeIn: c.fadeIn, fadeOut: c.fadeOut
                        // audioBuffer not serializable — user must re-add audio
                    };
                })
            };
        })
    };

    mc1Api('POST', '/app/api/daw.php', {
        action: 'save_project',
        id: self.projectId,
        project_name: name,
        bpm: self.bpm,
        time_signature: self.timeSignature,
        project_json: projectJson,
        duration_sec: self._getProjectDuration()
    }).then(function(d) {
        if (d.ok) {
            self.projectId = d.id || self.projectId;
            mc1Toast('Project saved: ' + name, 'ok');
        } else {
            mc1Toast('Save failed: ' + (d.error || 'Unknown error'), 'err');
        }
    }).catch(function(e) {
        mc1Toast('Save error: ' + e.message, 'err');
    });
};

DawEngine.prototype.loadProject = function(id) {
    var self = this;
    mc1Api('POST', '/app/api/daw.php', { action: 'get_project', id: id }).then(function(d) {
        if (!d.ok || !d.project) { mc1Toast('Failed to load project', 'err'); return; }
        var p = d.project;
        self.projectId = p.id;
        self.projectName = p.project_name;
        self.bpm = p.bpm || 120;
        self.timeSignature = p.time_signature || '4/4';
        document.getElementById('bpm-input').value = self.bpm;

        var json = typeof p.project_json === 'string' ? JSON.parse(p.project_json) : p.project_json;
        if (json && json.tracks) {
            self.tracks = json.tracks.map(function(t) {
                self._createTrackNodes(t.id);
                return {
                    id: t.id, name: t.name, muted: t.muted || false, solo: t.solo || false,
                    volume: t.volume || 1.0, pan: t.pan || 0.0, color: t.color || '#14b8a6',
                    clips: (t.clips || []).map(function(c) {
                        return {
                            id: c.id, name: c.name, startTime: c.startTime || 0,
                            duration: c.duration || 0, offset: c.offset || 0,
                            fadeIn: c.fadeIn || 0, fadeOut: c.fadeOut || 0,
                            audioBuffer: null, peaks: null, color: t.color || '#14b8a6'
                        };
                    })
                };
            });
            // Update nextTrackId/nextClipId
            self.tracks.forEach(function(t) {
                var num = parseInt(t.id.replace('track_', ''));
                if (num >= self.nextTrackId) self.nextTrackId = num + 1;
                t.clips.forEach(function(c) {
                    var cnum = parseInt(c.id.replace('clip_', ''));
                    if (cnum >= self.nextClipId) self.nextClipId = cnum + 1;
                });
            });
        }
        self._renderTrackList();
        self._resizeCanvases();
        mc1Toast('Loaded: ' + self.projectName, 'ok');
        document.getElementById('modal-projects').classList.remove('open');
    }).catch(function(e) {
        mc1Toast('Load error: ' + e.message, 'err');
    });
};

DawEngine.prototype.loadProjectList = function() {
    var self = this;
    var container = document.getElementById('project-list');
    container.innerHTML = '<div style="text-align:center;padding:20px;color:var(--muted)"><i class="fa-solid fa-spinner fa-spin"></i> Loading...</div>';

    mc1Api('POST', '/app/api/daw.php', { action: 'list_projects' }).then(function(d) {
        if (!d.ok || !d.projects || d.projects.length === 0) {
            container.innerHTML = '<div style="text-align:center;padding:20px;color:var(--muted)">No saved projects</div>';
            return;
        }
        container.innerHTML = '';
        d.projects.forEach(function(p) {
            var row = document.createElement('div');
            row.className = 'project-row';
            row.innerHTML =
                '<i class="fa-solid fa-folder-open fa-fw" style="color:var(--teal)"></i>' +
                '<span class="pname">' + self._esc(p.project_name) + '</span>' +
                '<span class="pdate">' + (p.updated_at || p.created_at || '') + '</span>' +
                '<span class="pdel" data-id="' + p.id + '" title="Delete"><i class="fa-solid fa-trash"></i></span>';
            row.addEventListener('click', function(e) {
                if (e.target.closest('.pdel')) return;
                self.loadProject(p.id);
            });
            row.querySelector('.pdel').addEventListener('click', function(e) {
                e.stopPropagation();
                if (confirm('Delete project "' + p.project_name + '"?')) {
                    mc1Api('POST', '/app/api/daw.php', { action: 'delete_project', id: p.id }).then(function(r) {
                        if (r.ok) { row.remove(); mc1Toast('Deleted', 'ok'); }
                        else mc1Toast('Delete failed', 'err');
                    });
                }
            });
            container.appendChild(row);
        });
    }).catch(function() {
        container.innerHTML = '<div class="alert alert-error">Failed to load projects</div>';
    });
};

DawEngine.prototype.newProject = function() {
    var self = this;
    self.projectId = null;
    self.projectName = 'Untitled';
    self.bpm = 120;
    self.timeSignature = '4/4';
    self.tracks = [];
    self.undoStack = [];
    self.redoStack = [];
    self.selectedClip = null;
    self.selectedTrack = null;
    self._stopAllSources();
    self.playing = false;
    self.playPos = 0;
    self._updatePlayButton(false);
    self.addTrack('Track 1');
    document.getElementById('bpm-input').value = 120;
    mc1Toast('New project created', 'ok');
};

/* ══════════════════════════════════════════════════════════════
 *  EXPORT MIXDOWN
 * ══════════════════════════════════════════════════════════════ */

DawEngine.prototype.exportMixdown = function() {
    var self = this;
    var format = document.getElementById('export-format').value;
    var bitrate = document.getElementById('export-bitrate').value;
    var name = document.getElementById('export-name').value.trim() || 'mixdown';
    var progDiv = document.getElementById('export-progress');
    var statusEl = document.getElementById('export-status');

    // We need to save the project first so the server has the data
    if (!self.projectId) {
        mc1Toast('Save the project first', 'warn');
        return;
    }

    progDiv.style.display = '';
    statusEl.textContent = 'Exporting...';

    mc1Api('POST', '/app/api/daw.php', {
        action: 'export_mixdown',
        project_id: self.projectId,
        format: format,
        bitrate: bitrate,
        output_name: name
    }).then(function(d) {
        if (d.ok && d.download_url) {
            statusEl.textContent = 'Export complete!';
            setTimeout(function() {
                var a = document.createElement('a');
                a.href = d.download_url;
                a.download = name + '.' + format;
                a.click();
                progDiv.style.display = 'none';
            }, 500);
        } else {
            statusEl.textContent = 'Export failed: ' + (d.error || 'Unknown error');
        }
    }).catch(function(e) {
        statusEl.textContent = 'Export error: ' + e.message;
    });
};

/* ══════════════════════════════════════════════════════════════
 *  HELPERS
 * ══════════════════════════════════════════════════════════════ */

DawEngine.prototype._esc = function(s) {
    var d = document.createElement('div');
    d.appendChild(document.createTextNode(s || ''));
    return d.innerHTML;
};
