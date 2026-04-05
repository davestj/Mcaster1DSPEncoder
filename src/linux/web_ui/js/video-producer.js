/*
 * Mcaster1 Video Producer Engine
 * js/video-producer.js
 *
 * Manages 3 video source slots (webcam, file, media library),
 * program output compositing, transitions, and recording/streaming state.
 * Depends on: js/webgl-video.js (Mc1WebGLVideo)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

(function() {
'use strict';

var WGL = window.Mc1WebGLVideo;

/* ======================================================================
 * VideoSource — per-slot source manager
 * ====================================================================== */

function VideoSource(slotId, canvas) {
    this.slotId = slotId;
    this.canvas = canvas;
    this.renderer = WGL.createVideoRenderer(canvas);
    this.type = 'none';     // 'webcam' | 'file' | 'library' | 'none'
    this.stream = null;     // MediaStream (webcam)
    this.videoEl = null;    // HTMLVideoElement (file/library playback)
    this.tracks = [];       // media library track list
    this.trackIdx = 0;
    this.isLive = false;
    this.isCue = false;
    this._rafId = null;
    this._destroyed = false;
}

VideoSource.prototype.destroy = function() {
    this._destroyed = true;
    this.stop();
    WGL.destroyRenderer(this.renderer);
    this.renderer = null;
};

VideoSource.prototype.stop = function() {
    if (this._rafId) {
        cancelAnimationFrame(this._rafId);
        this._rafId = null;
    }
    if (this.stream) {
        this.stream.getTracks().forEach(function(t) { t.stop(); });
        this.stream = null;
    }
    if (this.videoEl) {
        this.videoEl.pause();
        this.videoEl.removeAttribute('src');
        this.videoEl.load();
    }
    this.type = 'none';
};

/* ── Webcam ──────────────────────────────────────────────────────────── */

VideoSource.prototype.setWebcam = function(deviceId, constraints) {
    var self = this;
    this.stop();
    this.type = 'webcam';

    var c = constraints || {};
    var mediaConstraints = {
        video: {
            width:  { ideal: c.width  || 1280 },
            height: { ideal: c.height || 720 },
            frameRate: { ideal: c.fps || 30 }
        },
        audio: false
    };
    if (deviceId) {
        mediaConstraints.video.deviceId = { exact: deviceId };
    }

    return navigator.mediaDevices.getUserMedia(mediaConstraints).then(function(stream) {
        if (self._destroyed) {
            stream.getTracks().forEach(function(t) { t.stop(); });
            return;
        }
        self.stream = stream;
        if (!self.videoEl) {
            self.videoEl = document.createElement('video');
            self.videoEl.setAttribute('playsinline', '');
            self.videoEl.muted = true;
        }
        self.videoEl.srcObject = stream;
        self.videoEl.play();
        self._startRender();
        return stream;
    });
};

/* ── Video File ──────────────────────────────────────────────────────── */

VideoSource.prototype.setVideoFile = function(url) {
    this.stop();
    this.type = 'file';
    if (!this.videoEl) {
        this.videoEl = document.createElement('video');
        this.videoEl.setAttribute('playsinline', '');
    }
    this.videoEl.srcObject = null;
    this.videoEl.muted = true;
    this.videoEl.src = url;
    this.videoEl.load();
    this.videoEl.play();
    this._startRender();
};

/* ── Media Library ───────────────────────────────────────────────────── */

VideoSource.prototype.setMediaLibrary = function(tracks) {
    this.stop();
    this.type = 'library';
    this.tracks = tracks || [];
    this.trackIdx = 0;
    if (!this.videoEl) {
        this.videoEl = document.createElement('video');
        this.videoEl.setAttribute('playsinline', '');
    }
    this.videoEl.muted = true;
    var self = this;
    this.videoEl.onended = function() {
        self._nextLibraryTrack();
    };
    this._playLibraryTrack();
    this._startRender();
};

VideoSource.prototype._playLibraryTrack = function() {
    if (!this.tracks.length) return;
    var track = this.tracks[this.trackIdx];
    if (!track) return;
    var url = '/app/api/audio.php?id=' + track.id;
    // For video files we use a direct path if available
    if (track.file_path && /\.(mp4|webm|mkv|avi|mov)$/i.test(track.file_path)) {
        url = '/app/api/audio.php?path=' + encodeURIComponent(track.file_path);
    }
    this.videoEl.srcObject = null;
    this.videoEl.src = url;
    this.videoEl.load();
    this.videoEl.play();
};

VideoSource.prototype._nextLibraryTrack = function() {
    if (!this.tracks.length) return;
    this.trackIdx = (this.trackIdx + 1) % this.tracks.length;
    this._playLibraryTrack();
};

/* ── Render Loop ─────────────────────────────────────────────────────── */

VideoSource.prototype._startRender = function() {
    if (this._rafId) cancelAnimationFrame(this._rafId);
    var self = this;

    // Use requestVideoFrameCallback if available for better sync
    if (this.videoEl && typeof this.videoEl.requestVideoFrameCallback === 'function') {
        var onFrame = function() {
            if (self._destroyed || self.type === 'none') return;
            if (self.videoEl && self.renderer) {
                WGL.drawVideoFrame(self.renderer, self.videoEl);
            }
            if (!self._destroyed && self.type !== 'none') {
                self.videoEl.requestVideoFrameCallback(onFrame);
            }
        };
        this.videoEl.requestVideoFrameCallback(onFrame);
        // Also keep rAF as fallback for non-frame events
        var fallback = function() {
            if (self._destroyed || self.type === 'none') return;
            self._rafId = requestAnimationFrame(fallback);
        };
        this._rafId = requestAnimationFrame(fallback);
    } else {
        // Fallback: use requestAnimationFrame
        var render = function() {
            if (self._destroyed || self.type === 'none') return;
            if (self.videoEl && self.renderer && !self.videoEl.paused && self.videoEl.readyState >= 2) {
                WGL.drawVideoFrame(self.renderer, self.videoEl);
            }
            self._rafId = requestAnimationFrame(render);
        };
        this._rafId = requestAnimationFrame(render);
    }
};

VideoSource.prototype.getFrame = function() {
    return this.canvas;
};

VideoSource.prototype.getVideoElement = function() {
    return this.videoEl;
};

/* ======================================================================
 * VideoProducer — main orchestrator
 * ====================================================================== */

function VideoProducer(config) {
    this.sources = [];
    this.programCanvas = config.programCanvas;
    this.programRenderer = WGL.createTransitionRenderer(this.programCanvas);
    this.activeSourceIdx = -1;
    this.previousSourceIdx = -1;

    // Transition state
    this.transitionType = 'cut';
    this.transitionDuration = 500;
    this._transitioning = false;
    this._transProgress = 0;
    this._transStart = 0;
    this._transSrcA = -1;
    this._transSrcB = -1;

    // Recording / streaming
    this.isRecording = false;
    this.isStreaming = false;
    this.mediaRecorder = null;
    this.recordedChunks = [];

    this._rafId = null;
    this._destroyed = false;

    // Init source slots
    for (var i = 0; i < config.sourceCanvases.length; i++) {
        this.sources.push(new VideoSource(i, config.sourceCanvases[i]));
    }

    this._startProgramLoop();
}

/* ── Source management ─────────────────────────────────────────────── */

VideoProducer.prototype.getSource = function(idx) {
    return this.sources[idx] || null;
};

VideoProducer.prototype.setActiveSource = function(idx) {
    if (idx === this.activeSourceIdx) return;
    if (idx < 0 || idx >= this.sources.length) return;

    // Mark old source as CUE, new as LIVE
    for (var i = 0; i < this.sources.length; i++) {
        this.sources[i].isLive = (i === idx);
        this.sources[i].isCue = (i !== idx && this.sources[i].type !== 'none');
    }

    if (this.transitionType === 'cut' || this.activeSourceIdx < 0) {
        // Instant switch
        this.previousSourceIdx = this.activeSourceIdx;
        this.activeSourceIdx = idx;
    } else {
        // Animated transition
        this._startTransition(this.activeSourceIdx, idx);
    }
};

VideoProducer.prototype._startTransition = function(fromIdx, toIdx) {
    this._transitioning = true;
    this._transProgress = 0;
    this._transStart = performance.now();
    this._transSrcA = fromIdx;
    this._transSrcB = toIdx;
};

VideoProducer.prototype._updateTransition = function() {
    if (!this._transitioning) return;
    var elapsed = performance.now() - this._transStart;
    this._transProgress = Math.min(elapsed / this.transitionDuration, 1.0);
    if (this._transProgress >= 1.0) {
        this._transitioning = false;
        this.previousSourceIdx = this._transSrcA;
        this.activeSourceIdx = this._transSrcB;
        this._transProgress = 0;
    }
};

/* ── Program output render loop ──────────────────────────────────── */

VideoProducer.prototype._startProgramLoop = function() {
    var self = this;
    var render = function() {
        if (self._destroyed) return;
        self._renderProgram();
        self._rafId = requestAnimationFrame(render);
    };
    this._rafId = requestAnimationFrame(render);
};

VideoProducer.prototype._renderProgram = function() {
    this._updateTransition();

    if (this._transitioning) {
        var srcA = this.sources[this._transSrcA];
        var srcB = this.sources[this._transSrcB];
        var frameA = srcA ? srcA.getFrame() : null;
        var frameB = srcB ? srcB.getFrame() : null;
        WGL.drawTransition(this.programRenderer, frameA, frameB,
            this._transProgress, this.transitionType);
    } else if (this.activeSourceIdx >= 0) {
        var active = this.sources[this.activeSourceIdx];
        if (active && active.type !== 'none') {
            WGL.drawTransition(this.programRenderer, active.getFrame(), active.getFrame(),
                0, 'cut');
        }
    }
};

/* ── Transition config ───────────────────────────────────────────── */

VideoProducer.prototype.setTransition = function(type, durationMs) {
    if (WGL.TRANSITION_TYPES.indexOf(type) >= 0) {
        this.transitionType = type;
    }
    if (typeof durationMs === 'number' && durationMs > 0) {
        this.transitionDuration = durationMs;
    }
};

/* ── Camera enumeration ──────────────────────────────────────────── */

VideoProducer.prototype.enumerateVideoDevices = function() {
    return navigator.mediaDevices.enumerateDevices().then(function(devices) {
        return devices.filter(function(d) { return d.kind === 'videoinput'; });
    });
};

VideoProducer.prototype.onDeviceChange = function(callback) {
    navigator.mediaDevices.addEventListener('devicechange', function() {
        navigator.mediaDevices.enumerateDevices().then(function(devices) {
            var video = devices.filter(function(d) { return d.kind === 'videoinput'; });
            callback(video);
        });
    });
};

/* ── Recording ───────────────────────────────────────────────────── */

VideoProducer.prototype.startRecording = function(mimeType) {
    if (this.isRecording) return;
    var stream = this.programCanvas.captureStream(30);
    if (!stream) {
        console.error('captureStream not supported');
        return;
    }
    var options = {};
    if (mimeType && MediaRecorder.isTypeSupported(mimeType)) {
        options.mimeType = mimeType;
    } else if (MediaRecorder.isTypeSupported('video/webm;codecs=vp9')) {
        options.mimeType = 'video/webm;codecs=vp9';
    } else if (MediaRecorder.isTypeSupported('video/webm')) {
        options.mimeType = 'video/webm';
    }
    options.videoBitsPerSecond = 5000000; // 5 Mbps

    this.recordedChunks = [];
    var self = this;
    this.mediaRecorder = new MediaRecorder(stream, options);
    this.mediaRecorder.ondataavailable = function(e) {
        if (e.data && e.data.size > 0) self.recordedChunks.push(e.data);
    };
    this.mediaRecorder.start(1000); // 1s chunks
    this.isRecording = true;
};

VideoProducer.prototype.stopRecording = function() {
    if (!this.isRecording || !this.mediaRecorder) return null;
    var self = this;
    return new Promise(function(resolve) {
        self.mediaRecorder.onstop = function() {
            var blob = new Blob(self.recordedChunks, { type: self.mediaRecorder.mimeType });
            self.recordedChunks = [];
            self.isRecording = false;
            resolve(blob);
        };
        self.mediaRecorder.stop();
    });
};

/* ── Scene save/load ─────────────────────────────────────────────── */

VideoProducer.prototype.getSceneConfig = function() {
    var sources = [];
    for (var i = 0; i < this.sources.length; i++) {
        var s = this.sources[i];
        sources.push({
            slot: i,
            type: s.type,
            config: {
                trackIdx: s.trackIdx,
                tracksCount: s.tracks.length
            }
        });
    }
    return {
        sources_json: JSON.stringify(sources),
        active_source: this.activeSourceIdx,
        transition_type: this.transitionType,
        transition_duration_ms: this.transitionDuration
    };
};

VideoProducer.prototype.loadSceneConfig = function(scene) {
    if (scene.transition_type) this.transitionType = scene.transition_type;
    if (scene.transition_duration_ms) this.transitionDuration = scene.transition_duration_ms;
};

/* ── Cleanup ─────────────────────────────────────────────────────── */

VideoProducer.prototype.destroy = function() {
    this._destroyed = true;
    if (this._rafId) cancelAnimationFrame(this._rafId);
    for (var i = 0; i < this.sources.length; i++) {
        this.sources[i].destroy();
    }
    WGL.destroyRenderer(this.programRenderer);
    if (this.isRecording && this.mediaRecorder) {
        this.mediaRecorder.stop();
    }
};

/* ======================================================================
 * Resolution presets
 * ====================================================================== */

var RESOLUTIONS = {
    '480p':  { width: 854,  height: 480,  fps: 30, label: '480p (SD)' },
    '720p':  { width: 1280, height: 720,  fps: 30, label: '720p (HD)' },
    '1080p': { width: 1920, height: 1080, fps: 30, label: '1080p (Full HD)' }
};

/* ======================================================================
 * Public API
 * ====================================================================== */

window.Mc1VideoProducer = {
    VideoSource: VideoSource,
    VideoProducer: VideoProducer,
    RESOLUTIONS: RESOLUTIONS
};

})();
