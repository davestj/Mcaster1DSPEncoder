/*
 * Mcaster1 Video Producer Engine
 * js/video-producer.js
 *
 * Manages 3 video source slots (webcam, file, media library),
 * PGM/PVW bus switcher, T-bar crossfader, auto-transitions,
 * chroma key, PIP compositing, color correction, lower-third overlay,
 * recording and streaming state.
 * Depends on: js/webgl-video.js (Mc1WebGLVideo)
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

(function() {
'use strict';

var WGL = window.Mc1WebGLVideo;

/* ======================================================================
 * VideoSource -- per-slot source manager
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

    // Per-source color correction
    this.colorCorrection = {
        brightness: 0.0,    // -1.0 to 1.0
        contrast: 1.0,      // 0.0 to 2.0
        saturation: 1.0,    // 0.0 to 2.0
        hue: 0.0            // -PI to PI
    };

    // Per-source chroma key
    this.chromaKey = {
        enabled: false,
        color: [0, 1, 0],   // green default (RGB 0-1)
        tolerance: 0.3,
        softness: 0.1
    };
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

/* -- Webcam ---------------------------------------------------------- */

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

/* -- Video File ------------------------------------------------------ */

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

/* -- Media Library --------------------------------------------------- */

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

/* -- Render Loop ----------------------------------------------------- */

VideoSource.prototype._startRender = function() {
    if (this._rafId) cancelAnimationFrame(this._rafId);
    var self = this;

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
        var fallback = function() {
            if (self._destroyed || self.type === 'none') return;
            self._rafId = requestAnimationFrame(fallback);
        };
        this._rafId = requestAnimationFrame(fallback);
    } else {
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

/* -- Color Correction ------------------------------------------------ */

VideoSource.prototype.setColorCorrection = function(key, value) {
    if (this.colorCorrection.hasOwnProperty(key)) {
        this.colorCorrection[key] = value;
    }
};

/* -- Chroma Key ------------------------------------------------------ */

VideoSource.prototype.setChromaKey = function(enabled, color, tolerance, softness) {
    this.chromaKey.enabled = !!enabled;
    if (color) this.chromaKey.color = color;
    if (tolerance !== undefined) this.chromaKey.tolerance = tolerance;
    if (softness !== undefined) this.chromaKey.softness = softness;
};

/* ======================================================================
 * VideoProducer -- main orchestrator with PGM/PVW bus switcher
 * ====================================================================== */

function VideoProducer(config) {
    this.sources = [];
    this.programCanvas = config.programCanvas;
    this.programRenderer = WGL.createTransitionRenderer(this.programCanvas);

    // PGM/PVW bus (switcher model)
    this.pgmSourceIdx = -1;   // Program (live) source index
    this.pvwSourceIdx = -1;   // Preview (next) source index

    // Legacy compat
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

    // T-bar manual mode
    this.tbarMode = false;       // true = manual T-bar, false = auto-transition
    this.tbarValue = 0.0;       // 0.0 = full PGM, 1.0 = full PVW

    // PIP state
    this.pip = {
        enabled: false,
        sourceIdx: -1,
        position: 'br',          // tl, tr, bl, br
        size: '33',              // 25, 33, 50
        alpha: 1.0
    };

    // Lower-third overlay
    this.lowerThird = new WGL.LowerThirdRenderer(1280, 160);

    // Recording / streaming
    this.isRecording = false;
    this.isStreaming = false;
    this.mediaRecorder = null;
    this.recordedChunks = [];

    // Scene presets
    this._scenePresets = [];

    this._rafId = null;
    this._destroyed = false;

    // Init source slots
    for (var i = 0; i < config.sourceCanvases.length; i++) {
        this.sources.push(new VideoSource(i, config.sourceCanvases[i]));
    }

    this._startProgramLoop();
}

/* -- Source management ------------------------------------------------ */

VideoProducer.prototype.getSource = function(idx) {
    return this.sources[idx] || null;
};

/* -- PGM/PVW bus control --------------------------------------------- */

VideoProducer.prototype.setPVW = function(idx) {
    if (idx < 0 || idx >= this.sources.length) return;
    if (idx === this.pgmSourceIdx) return; // cannot PVW what is already PGM
    this.pvwSourceIdx = idx;
    this._updateSourceFlags();
};

VideoProducer.prototype.setPGM = function(idx) {
    if (idx < 0 || idx >= this.sources.length) return;
    this.pgmSourceIdx = idx;
    this.activeSourceIdx = idx;
    if (this.pvwSourceIdx === idx) {
        this.pvwSourceIdx = -1;
    }
    this._updateSourceFlags();
};

/* -- Cut: instant switch PVW -> PGM ---------------------------------- */

VideoProducer.prototype.cut = function() {
    if (this.pvwSourceIdx < 0) return;
    this.previousSourceIdx = this.pgmSourceIdx;
    this.pgmSourceIdx = this.pvwSourceIdx;
    this.activeSourceIdx = this.pgmSourceIdx;
    this.pvwSourceIdx = this.previousSourceIdx >= 0 ? this.previousSourceIdx : -1;
    this._transitioning = false;
    this._transProgress = 0;
    this.tbarValue = 0;
    this._updateSourceFlags();
};

/* -- Auto transition: timed PVW -> PGM ------------------------------- */

VideoProducer.prototype.autoTransition = function(type) {
    if (this.pvwSourceIdx < 0) return;
    if (this._transitioning) return;

    var transType = type || this.transitionType;
    if (transType === 'cut') {
        this.cut();
        return;
    }

    this._transitioning = true;
    this._transProgress = 0;
    this._transStart = performance.now();
    this._transSrcA = this.pgmSourceIdx;
    this._transSrcB = this.pvwSourceIdx;
    this.transitionType = transType;
};

/* -- Legacy setActiveSource (calls into PGM/PVW model) --------------- */

VideoProducer.prototype.setActiveSource = function(idx) {
    if (idx === this.pgmSourceIdx) return;
    if (idx < 0 || idx >= this.sources.length) return;

    this.pvwSourceIdx = idx;

    if (this.transitionType === 'cut' || this.pgmSourceIdx < 0) {
        this.cut();
    } else {
        this.autoTransition(this.transitionType);
    }
};

/* -- T-bar ----------------------------------------------------------- */

VideoProducer.prototype.setTbarValue = function(value) {
    if (!this.tbarMode) return;
    if (this.pvwSourceIdx < 0) return;
    this.tbarValue = Math.max(0, Math.min(1, value));

    // When T-bar reaches the end, commit the switch
    if (this.tbarValue >= 0.999) {
        var oldPgm = this.pgmSourceIdx;
        this.pgmSourceIdx = this.pvwSourceIdx;
        this.activeSourceIdx = this.pgmSourceIdx;
        this.pvwSourceIdx = oldPgm >= 0 ? oldPgm : -1;
        this.tbarValue = 0;
        this._transitioning = false;
        this._updateSourceFlags();
    }
};

VideoProducer.prototype.setTbarMode = function(manual) {
    this.tbarMode = !!manual;
    if (!this.tbarMode) {
        this.tbarValue = 0;
    }
};

/* -- Transition update ----------------------------------------------- */

VideoProducer.prototype._updateTransition = function() {
    if (!this._transitioning) return;
    var elapsed = performance.now() - this._transStart;
    this._transProgress = Math.min(elapsed / this.transitionDuration, 1.0);
    if (this._transProgress >= 1.0) {
        this._transitioning = false;
        this.previousSourceIdx = this._transSrcA;
        this.pgmSourceIdx = this._transSrcB;
        this.activeSourceIdx = this.pgmSourceIdx;
        this.pvwSourceIdx = this.previousSourceIdx >= 0 ? this.previousSourceIdx : -1;
        this._transProgress = 0;
        this.tbarValue = 0;
        this._updateSourceFlags();
    }
};

VideoProducer.prototype._updateSourceFlags = function() {
    for (var i = 0; i < this.sources.length; i++) {
        this.sources[i].isLive = (i === this.pgmSourceIdx);
        this.sources[i].isCue = (i === this.pvwSourceIdx);
    }
};

/* -- Program output render loop -------------------------------------- */

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

    // Update lower-third animation
    if (this.lowerThird.isVisible()) {
        this.lowerThird.update();
    }

    var programFrame = null;

    // Determine if we are in a transition
    if (this._transitioning && !this.tbarMode) {
        var srcA = this.sources[this._transSrcA];
        var srcB = this.sources[this._transSrcB];
        var frameA = srcA ? srcA.getFrame() : null;
        var frameB = srcB ? srcB.getFrame() : null;
        WGL.drawTransition(this.programRenderer, frameA, frameB,
            this._transProgress, this.transitionType);
        programFrame = this.programCanvas;
    } else if (this.tbarMode && this.pvwSourceIdx >= 0 && this.tbarValue > 0.001) {
        // T-bar manual crossfade
        var srcA2 = this.sources[this.pgmSourceIdx];
        var srcB2 = this.sources[this.pvwSourceIdx];
        var fA = srcA2 ? srcA2.getFrame() : null;
        var fB = srcB2 ? srcB2.getFrame() : null;
        WGL.drawTransition(this.programRenderer, fA, fB,
            this.tbarValue, this.transitionType === 'cut' ? 'fade' : this.transitionType);
        programFrame = this.programCanvas;
    } else if (this.pgmSourceIdx >= 0) {
        var active = this.sources[this.pgmSourceIdx];
        if (active && active.type !== 'none') {
            WGL.drawTransition(this.programRenderer, active.getFrame(), active.getFrame(),
                0, 'cut');
            programFrame = this.programCanvas;
        }
    }

    // PIP compositing pass
    if (this.pip.enabled && this.pip.sourceIdx >= 0 && programFrame) {
        var pipSrc = this.sources[this.pip.sourceIdx];
        if (pipSrc && pipSrc.type !== 'none') {
            var sizeFrac = WGL.PIP_SIZES[this.pip.size] || 0.33;
            var rectFn = WGL.PIP_POSITIONS[this.pip.position];
            var pipRect = rectFn ? rectFn(sizeFrac) : { x: 0.65, y: 0.65, w: 0.33, h: 0.33 };
            WGL.drawPIP(this.programRenderer, programFrame, pipSrc.getFrame(),
                pipRect, this.pip.alpha, 0.008, [1, 1, 1]);
        }
    }

    // Lower-third overlay pass
    if (this.lowerThird.isVisible() && programFrame) {
        WGL.drawOverlay(this.programRenderer, this.programCanvas,
            this.lowerThird.getCanvas(),
            { x: 0.03, y: 0.72, w: 0.94, h: 0.14 },
            1.0);
    }
};

/* -- Transition config ----------------------------------------------- */

VideoProducer.prototype.setTransition = function(type, durationMs) {
    if (WGL.TRANSITION_TYPES.indexOf(type) >= 0) {
        this.transitionType = type;
    }
    if (typeof durationMs === 'number' && durationMs > 0) {
        this.transitionDuration = durationMs;
    }
};

/* -- PIP control ----------------------------------------------------- */

VideoProducer.prototype.setPIP = function(enabled, sourceIdx, position, size) {
    this.pip.enabled = !!enabled;
    if (sourceIdx !== undefined) this.pip.sourceIdx = sourceIdx;
    if (position) this.pip.position = position;
    if (size) this.pip.size = size;
};

/* -- Lower-third control --------------------------------------------- */

VideoProducer.prototype.showLowerThird = function(text, options) {
    this.lowerThird.show(text, options);
};

VideoProducer.prototype.hideLowerThird = function() {
    this.lowerThird.hide();
};

/* -- Camera enumeration ---------------------------------------------- */

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

/* -- Recording ------------------------------------------------------- */

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
    options.videoBitsPerSecond = 5000000;

    this.recordedChunks = [];
    var self = this;
    this.mediaRecorder = new MediaRecorder(stream, options);
    this.mediaRecorder.ondataavailable = function(e) {
        if (e.data && e.data.size > 0) self.recordedChunks.push(e.data);
    };
    this.mediaRecorder.start(1000);
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

/* -- Scene save/load ------------------------------------------------- */

VideoProducer.prototype.getSceneConfig = function() {
    var sources = [];
    for (var i = 0; i < this.sources.length; i++) {
        var s = this.sources[i];
        sources.push({
            slot: i,
            type: s.type,
            config: {
                trackIdx: s.trackIdx,
                tracksCount: s.tracks.length,
                colorCorrection: {
                    brightness: s.colorCorrection.brightness,
                    contrast: s.colorCorrection.contrast,
                    saturation: s.colorCorrection.saturation,
                    hue: s.colorCorrection.hue
                },
                chromaKey: {
                    enabled: s.chromaKey.enabled,
                    color: s.chromaKey.color.slice(),
                    tolerance: s.chromaKey.tolerance,
                    softness: s.chromaKey.softness
                }
            }
        });
    }
    return {
        sources_json: JSON.stringify(sources),
        active_source: this.pgmSourceIdx,
        preview_source: this.pvwSourceIdx,
        transition_type: this.transitionType,
        transition_duration_ms: this.transitionDuration,
        pip_enabled: this.pip.enabled ? 1 : 0,
        pip_source: this.pip.sourceIdx,
        pip_position: this.pip.position,
        pip_size: this.pip.size,
        tbar_mode: this.tbarMode ? 1 : 0
    };
};

VideoProducer.prototype.loadSceneConfig = function(scene) {
    if (scene.transition_type) this.transitionType = scene.transition_type;
    if (scene.transition_duration_ms) this.transitionDuration = scene.transition_duration_ms;
    if (scene.pip_enabled !== undefined) this.pip.enabled = !!scene.pip_enabled;
    if (scene.pip_source !== undefined) this.pip.sourceIdx = scene.pip_source;
    if (scene.pip_position) this.pip.position = scene.pip_position;
    if (scene.pip_size) this.pip.size = scene.pip_size;
    if (scene.tbar_mode !== undefined) this.tbarMode = !!scene.tbar_mode;
};

/* -- Cleanup --------------------------------------------------------- */

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
