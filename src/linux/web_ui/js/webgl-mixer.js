/**
 * webgl-mixer.js — WebGL Enhancements for Mcaster1 Mixer Console
 * Phase MX-3: 3D Fader Caps, Master Spectrum Bridge, Skin Shaders
 *
 * Provides GPU-rendered metallic fader caps with per-skin materials,
 * a subtle spectrum analyzer behind the master fader, and special
 * shader effects for live_neon (bloom glow) and vintage_analog (grain).
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
(function() {
'use strict';

/* ======================================================================
 * Skin material definitions
 * capColor: [r,g,b] 0-1, ridgeIntensity: 0-1, borderGlow: [r,g,b,a]
 * ====================================================================== */
var SKIN_MATERIALS = {
    broadcast_dark:    { capColor: [0.42, 0.50, 0.58], ridgeIntensity: 0.6, borderGlow: [0,0,0,0] },
    studio_warm:       { capColor: [0.80, 0.50, 0.20], ridgeIntensity: 0.7, borderGlow: [0,0,0,0] },
    live_neon:         { capColor: [0.0,  0.90, 1.0 ], ridgeIntensity: 0.3, borderGlow: [0, 0.9, 1.0, 0.6] },
    vintage_analog:    { capColor: [0.30, 0.22, 0.12], ridgeIntensity: 0.9, borderGlow: [0,0,0,0] },
    military_tactical: { capColor: [0.14, 0.14, 0.14], ridgeIntensity: 0.5, borderGlow: [0,0,0,0] },
    arctic_clean:      { capColor: [0.94, 0.96, 1.0 ], ridgeIntensity: 0.2, borderGlow: [0,0,0,0] }
};

/* ======================================================================
 * Shader sources
 * ====================================================================== */

var CAP_VS = [
    'attribute vec2 a_pos;',
    'varying vec2 v_uv;',
    'void main() {',
    '  v_uv = a_pos * 0.5 + 0.5;',
    '  gl_Position = vec4(a_pos, 0.0, 1.0);',
    '}'
].join('\n');

var CAP_FS = [
    'precision mediump float;',
    'varying vec2 v_uv;',
    'uniform vec3 u_capColor;',
    'uniform float u_ridgeIntensity;',
    'uniform float u_specularY;',
    'uniform vec4 u_borderGlow;',
    '',
    'void main() {',
    '  // Rounded rectangle SDF',
    '  vec2 p = abs(v_uv - 0.5) * 2.0;',
    '  float r = 0.15;',
    '  float d = length(max(p - (1.0 - r), 0.0)) - r;',
    '  if (d > 0.0) discard;',
    '',
    '  // Metallic base with vertical gradient',
    '  float light = 0.5 + 0.5 * (1.0 - v_uv.y);',
    '  vec3 col = u_capColor * light;',
    '',
    '  // Grip ridges (horizontal lines)',
    '  float ridge = sin(v_uv.y * 40.0) * 0.5 + 0.5;',
    '  col *= 1.0 - u_ridgeIntensity * ridge * 0.15;',
    '',
    '  // Specular highlight (shifts with fader position)',
    '  float specY = mix(0.25, 0.45, u_specularY);',
    '  float spec = pow(max(0.0, 1.0 - length(v_uv - vec2(0.5, specY)) * 3.0), 4.0);',
    '  col += vec3(spec * 0.4);',
    '',
    '  // Soft edge shadow',
    '  float edgeDist = min(min(v_uv.x, 1.0 - v_uv.x), min(v_uv.y, 1.0 - v_uv.y));',
    '  float shadow = smoothstep(0.0, 0.12, edgeDist);',
    '  col *= 0.7 + 0.3 * shadow;',
    '',
    '  // Border glow (live_neon)',
    '  float glowEdge = smoothstep(0.08, 0.0, edgeDist);',
    '  col += u_borderGlow.rgb * u_borderGlow.a * glowEdge;',
    '',
    '  gl_FragColor = vec4(col, 1.0);',
    '}'
].join('\n');

/* Vintage grain overlay fragment shader */
var GRAIN_FS = [
    'precision mediump float;',
    'varying vec2 v_uv;',
    'uniform float u_time;',
    'uniform float u_intensity;',
    '',
    'float rand(vec2 co) {',
    '  return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);',
    '}',
    '',
    'void main() {',
    '  float grain = rand(v_uv * 200.0 + vec2(u_time)) * 0.5 + 0.5;',
    '  float a = u_intensity * (grain - 0.5) * 0.15;',
    '  gl_FragColor = vec4(vec3(a > 0.0 ? a : 0.0), abs(a));',
    '}'
].join('\n');

/* Neon bloom fader fill fragment shader */
var BLOOM_FS = [
    'precision mediump float;',
    'varying vec2 v_uv;',
    'uniform vec3 u_color;',
    'uniform float u_fillPct;',
    'uniform float u_time;',
    '',
    'void main() {',
    '  float fillEdge = 1.0 - u_fillPct;',
    '  if (v_uv.y < fillEdge) discard;',
    '',
    '  // Core glow',
    '  float cx = abs(v_uv.x - 0.5) * 2.0;',
    '  float core = 1.0 - cx;',
    '  float glow = pow(core, 2.0) * 0.8;',
    '',
    '  // Pulse animation',
    '  float pulse = 0.85 + 0.15 * sin(u_time * 3.0);',
    '  glow *= pulse;',
    '',
    '  // Soften near top edge',
    '  float topDist = (v_uv.y - fillEdge) / u_fillPct;',
    '  float topFade = smoothstep(0.0, 0.05, topDist);',
    '  glow *= topFade;',
    '',
    '  gl_FragColor = vec4(u_color * glow, glow * 0.7);',
    '}'
].join('\n');

var FULLSCREEN_VS = [
    'attribute vec2 a_pos;',
    'varying vec2 v_uv;',
    'void main() {',
    '  v_uv = a_pos * 0.5 + 0.5;',
    '  gl_Position = vec4(a_pos, 0.0, 1.0);',
    '}'
].join('\n');

/* ======================================================================
 * WebGLFaderCap — individual fader cap renderer
 * ====================================================================== */

function WebGLFaderCap(canvas, skinName) {
    this.canvas = canvas;
    this._gl = null;
    this._prog = null;
    this._vbo = null;
    this._ok = false;
    this._specularY = 0.5;
    this._material = SKIN_MATERIALS[skinName] || SKIN_MATERIALS.broadcast_dark;
    this._init();
}

WebGLFaderCap.prototype._init = function() {
    var gl = this.canvas.getContext('webgl', { antialias: true, alpha: true, premultipliedAlpha: false });
    if (!gl) return;
    this._gl = gl;

    this._prog = WebGLViz.createProgram(gl, CAP_VS, CAP_FS);
    if (!this._prog) return;

    var verts = new Float32Array([-1,-1, 1,-1, -1,1, 1,1]);
    this._vbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this._vbo);
    gl.bufferData(gl.ARRAY_BUFFER, verts, gl.STATIC_DRAW);

    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

    this._ok = true;
    this.draw();
};

WebGLFaderCap.prototype.setSpecularY = function(normalizedPos) {
    this._specularY = Math.max(0, Math.min(1, normalizedPos));
};

WebGLFaderCap.prototype.setMaterial = function(skinName) {
    this._material = SKIN_MATERIALS[skinName] || SKIN_MATERIALS.broadcast_dark;
};

WebGLFaderCap.prototype.draw = function() {
    if (!this._ok) return;
    var gl = this._gl;
    var m = this._material;

    gl.viewport(0, 0, this.canvas.width, this.canvas.height);
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT);

    gl.useProgram(this._prog);

    var aPos = gl.getAttribLocation(this._prog, 'a_pos');
    gl.bindBuffer(gl.ARRAY_BUFFER, this._vbo);
    gl.enableVertexAttribArray(aPos);
    gl.vertexAttribPointer(aPos, 2, gl.FLOAT, false, 0, 0);

    gl.uniform3fv(gl.getUniformLocation(this._prog, 'u_capColor'), m.capColor);
    gl.uniform1f(gl.getUniformLocation(this._prog, 'u_ridgeIntensity'), m.ridgeIntensity);
    gl.uniform1f(gl.getUniformLocation(this._prog, 'u_specularY'), this._specularY);
    gl.uniform4fv(gl.getUniformLocation(this._prog, 'u_borderGlow'), m.borderGlow);

    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
};

WebGLFaderCap.prototype.destroy = function() {
    if (!this._gl) return;
    var gl = this._gl;
    if (this._vbo) gl.deleteBuffer(this._vbo);
    if (this._prog) gl.deleteProgram(this._prog);
    this._ok = false;
    this._gl = null;
};

/* ======================================================================
 * WebGLGrainOverlay — vintage_analog film grain on channel strips
 * ====================================================================== */

function WebGLGrainOverlay(canvas) {
    this.canvas = canvas;
    this._gl = null;
    this._prog = null;
    this._vbo = null;
    this._ok = false;
    this._startTime = Date.now();
    this._rafId = null;
    this._init();
}

WebGLGrainOverlay.prototype._init = function() {
    var gl = this.canvas.getContext('webgl', { antialias: false, alpha: true, premultipliedAlpha: false });
    if (!gl) return;
    this._gl = gl;

    this._prog = WebGLViz.createProgram(gl, FULLSCREEN_VS, GRAIN_FS);
    if (!this._prog) return;

    var verts = new Float32Array([-1,-1, 1,-1, -1,1, 1,1]);
    this._vbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this._vbo);
    gl.bufferData(gl.ARRAY_BUFFER, verts, gl.STATIC_DRAW);

    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

    this._ok = true;
    this._animate();
};

WebGLGrainOverlay.prototype._animate = function() {
    if (!this._ok) return;
    var self = this;
    this._rafId = requestAnimationFrame(function() { self._animate(); });
    this._draw();
};

WebGLGrainOverlay.prototype._draw = function() {
    if (!this._ok) return;
    var gl = this._gl;
    var elapsed = (Date.now() - this._startTime) / 1000.0;

    gl.viewport(0, 0, this.canvas.width, this.canvas.height);
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT);

    gl.useProgram(this._prog);

    var aPos = gl.getAttribLocation(this._prog, 'a_pos');
    gl.bindBuffer(gl.ARRAY_BUFFER, this._vbo);
    gl.enableVertexAttribArray(aPos);
    gl.vertexAttribPointer(aPos, 2, gl.FLOAT, false, 0, 0);

    gl.uniform1f(gl.getUniformLocation(this._prog, 'u_time'), elapsed);
    gl.uniform1f(gl.getUniformLocation(this._prog, 'u_intensity'), 1.0);

    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
};

WebGLGrainOverlay.prototype.destroy = function() {
    if (this._rafId) cancelAnimationFrame(this._rafId);
    if (!this._gl) return;
    var gl = this._gl;
    if (this._vbo) gl.deleteBuffer(this._vbo);
    if (this._prog) gl.deleteProgram(this._prog);
    this._ok = false;
    this._gl = null;
};

/* ======================================================================
 * WebGLNeonFaderFill — bloom glow on fader fill for live_neon skin
 * ====================================================================== */

function WebGLNeonFaderFill(canvas) {
    this.canvas = canvas;
    this._gl = null;
    this._prog = null;
    this._vbo = null;
    this._ok = false;
    this._fillPct = 0.5;
    this._startTime = Date.now();
    this._rafId = null;
    this._color = [0.0, 0.9, 1.0];
    this._init();
}

WebGLNeonFaderFill.prototype._init = function() {
    var gl = this.canvas.getContext('webgl', { antialias: false, alpha: true, premultipliedAlpha: false });
    if (!gl) return;
    this._gl = gl;

    this._prog = WebGLViz.createProgram(gl, FULLSCREEN_VS, BLOOM_FS);
    if (!this._prog) return;

    var verts = new Float32Array([-1,-1, 1,-1, -1,1, 1,1]);
    this._vbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this._vbo);
    gl.bufferData(gl.ARRAY_BUFFER, verts, gl.STATIC_DRAW);

    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

    this._ok = true;
    this._animate();
};

WebGLNeonFaderFill.prototype.setFill = function(pct) {
    this._fillPct = Math.max(0, Math.min(1, pct));
};

WebGLNeonFaderFill.prototype._animate = function() {
    if (!this._ok) return;
    var self = this;
    this._rafId = requestAnimationFrame(function() { self._animate(); });
    this._draw();
};

WebGLNeonFaderFill.prototype._draw = function() {
    if (!this._ok) return;
    var gl = this._gl;
    var elapsed = (Date.now() - this._startTime) / 1000.0;

    gl.viewport(0, 0, this.canvas.width, this.canvas.height);
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT);

    gl.useProgram(this._prog);

    var aPos = gl.getAttribLocation(this._prog, 'a_pos');
    gl.bindBuffer(gl.ARRAY_BUFFER, this._vbo);
    gl.enableVertexAttribArray(aPos);
    gl.vertexAttribPointer(aPos, 2, gl.FLOAT, false, 0, 0);

    gl.uniform3fv(gl.getUniformLocation(this._prog, 'u_color'), this._color);
    gl.uniform1f(gl.getUniformLocation(this._prog, 'u_fillPct'), this._fillPct);
    gl.uniform1f(gl.getUniformLocation(this._prog, 'u_time'), elapsed);

    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
};

WebGLNeonFaderFill.prototype.destroy = function() {
    if (this._rafId) cancelAnimationFrame(this._rafId);
    if (!this._gl) return;
    var gl = this._gl;
    if (this._vbo) gl.deleteBuffer(this._vbo);
    if (this._prog) gl.deleteProgram(this._prog);
    this._ok = false;
    this._gl = null;
};

/* ======================================================================
 * MixerWebGL — integration bridge called from mixer-console.js
 * ====================================================================== */

function MixerWebGL() {
    this._caps = {};         // fader-id -> WebGLFaderCap
    this._grainOverlay = null;
    this._neonFills = {};    // fader-id -> WebGLNeonFaderFill
    this._masterSpectrum = null;
    this._skin = 'broadcast_dark';
    this._active = false;
}

/**
 * Check if WebGL mixer enhancements should be active.
 */
MixerWebGL.prototype.isAvailable = function() {
    return !!(window.WebGLViz && WebGLViz.isWebGLAvailable() && WebGLViz.getWebGLPref());
};

/**
 * Initialize a fader cap WebGL canvas on a channel strip.
 * Call after strip DOM is inserted.
 * @param {string} faderId - e.g. '1', '2', 'master'
 */
MixerWebGL.prototype.initCap = function(faderId) {
    if (!this._active) return;
    var capEl = document.getElementById('fader-cap-' + faderId);
    if (!capEl) return;

    // Check if canvas already exists
    var existing = document.getElementById('glcap-' + faderId);
    if (existing) return;

    // Create a small WebGL canvas inside the fader cap
    var cvs = document.createElement('canvas');
    cvs.id = 'glcap-' + faderId;
    cvs.width = 60;    // 2x for retina
    cvs.height = 32;
    cvs.className = 'gl-fader-cap-canvas';
    capEl.appendChild(cvs);

    var cap = new WebGLFaderCap(cvs, this._skin);
    if (cap._ok) {
        this._caps[faderId] = cap;
        // Hide CSS gradient background
        capEl.classList.add('gl-cap-active');
    }
};

/**
 * Initialize the master bus spectrum analyzer canvas.
 */
MixerWebGL.prototype.initMasterSpectrum = function() {
    if (!this._active) return;
    var vuWrap = document.querySelector('#ch-master .ch-vu');
    if (!vuWrap) return;

    var existing = document.getElementById('gl-master-spectrum');
    if (existing) return;

    var cvs = document.createElement('canvas');
    cvs.id = 'gl-master-spectrum';
    cvs.width = 48;
    cvs.height = 200;
    cvs.className = 'gl-master-spectrum-canvas';
    vuWrap.appendChild(cvs);

    this._masterSpectrum = new WebGLViz.Spectrum3D(cvs);
};

/**
 * Feed spectrum data to master analyzer.
 * @param {Float32Array|Array} data - frequency magnitudes
 */
MixerWebGL.prototype.updateMasterSpectrum = function(data) {
    if (this._masterSpectrum && data) {
        this._masterSpectrum.update(data);
        this._masterSpectrum.draw();
    }
};

/**
 * Generate fake spectrum data from VU level for master bus visualization.
 * When real FFT data is not available, this creates a plausible spectrum.
 * @param {number} level - 0.0 to 1.0 VU level
 */
MixerWebGL.prototype.updateMasterSpectrumFromVU = function(level) {
    if (!this._masterSpectrum) return;
    var bins = 64;
    var data = new Float32Array(bins);
    for (var i = 0; i < bins; i++) {
        // Simulate a natural spectrum rolloff with some randomness
        var freq = i / bins;
        var rolloff = Math.exp(-freq * 3.0) * level;
        var noise = (Math.random() - 0.5) * 0.15 * level;
        data[i] = Math.max(0, rolloff + noise);
    }
    this._masterSpectrum.update(data);
    this._masterSpectrum.draw();
};

/**
 * Update fader cap specular position when fader moves.
 * @param {string} faderId
 * @param {number} normalizedPos - 0 (bottom) to 1 (top)
 */
MixerWebGL.prototype.updateCapPosition = function(faderId, normalizedPos) {
    var cap = this._caps[faderId];
    if (cap) {
        cap.setSpecularY(normalizedPos);
        cap.draw();
    }
};

/**
 * Update neon fader fill for live_neon skin.
 * @param {string} faderId
 * @param {number} fillPct - 0.0 to 1.0
 */
MixerWebGL.prototype.updateNeonFill = function(faderId, fillPct) {
    var neon = this._neonFills[faderId];
    if (neon) {
        neon.setFill(fillPct);
    }
};

/**
 * Apply skin change: destroy and recreate all WebGL renderers.
 * @param {string} skinName
 */
MixerWebGL.prototype.setSkin = function(skinName) {
    this._skin = skinName;
    if (!this._active) return;

    // Update all fader cap materials
    for (var id in this._caps) {
        this._caps[id].setMaterial(skinName);
        this._caps[id].draw();
    }

    // Grain overlay: only for vintage_analog
    this._destroyGrain();
    if (skinName === 'vintage_analog') {
        this._initGrain();
    }

    // Neon fills: only for live_neon
    this._destroyNeonFills();
    if (skinName === 'live_neon') {
        this._initNeonFills();
    }
};

/**
 * Activate WebGL mixer enhancements.
 */
MixerWebGL.prototype.activate = function() {
    if (!this.isAvailable()) return;
    this._active = true;
};

/**
 * Initialize all caps for currently rendered strips.
 */
MixerWebGL.prototype.initAllCaps = function() {
    if (!this._active) return;
    var strips = document.querySelectorAll('.ch-strip[data-slot]');
    for (var i = 0; i < strips.length; i++) {
        var slot = strips[i].getAttribute('data-slot');
        if (slot) this.initCap(slot);
    }

    // Skin-specific effects
    if (this._skin === 'vintage_analog') {
        this._initGrain();
    }
    if (this._skin === 'live_neon') {
        this._initNeonFills();
    }

    this.initMasterSpectrum();
};

/**
 * Destroy all WebGL resources.
 */
MixerWebGL.prototype.destroyAll = function() {
    for (var id in this._caps) {
        this._caps[id].destroy();
        var el = document.getElementById('glcap-' + id);
        if (el) el.remove();
        var capEl = document.getElementById('fader-cap-' + id);
        if (capEl) capEl.classList.remove('gl-cap-active');
    }
    this._caps = {};

    this._destroyGrain();
    this._destroyNeonFills();

    if (this._masterSpectrum) {
        this._masterSpectrum.destroy();
        this._masterSpectrum = null;
        var specEl = document.getElementById('gl-master-spectrum');
        if (specEl) specEl.remove();
    }
};

/* ── Private: Grain overlay ──────────────────────────── */

MixerWebGL.prototype._initGrain = function() {
    if (this._grainOverlay) return;
    var surface = document.getElementById('mixer-surface');
    if (!surface) return;

    var cvs = document.createElement('canvas');
    cvs.id = 'gl-grain-overlay';
    cvs.className = 'gl-grain-overlay-canvas';
    cvs.width = surface.offsetWidth || 800;
    cvs.height = surface.offsetHeight || 520;
    surface.appendChild(cvs);

    this._grainOverlay = new WebGLGrainOverlay(cvs);
};

MixerWebGL.prototype._destroyGrain = function() {
    if (this._grainOverlay) {
        this._grainOverlay.destroy();
        this._grainOverlay = null;
    }
    var el = document.getElementById('gl-grain-overlay');
    if (el) el.remove();
};

/* ── Private: Neon fader fills ───────────────────────── */

MixerWebGL.prototype._initNeonFills = function() {
    var tracks = document.querySelectorAll('.ch-fader-track');
    for (var i = 0; i < tracks.length; i++) {
        var trackEl = tracks[i];
        var idMatch = trackEl.id.match(/fader-track-(.+)/);
        if (!idMatch) continue;
        var faderId = idMatch[1];

        var existing = document.getElementById('gl-neon-' + faderId);
        if (existing) continue;

        var cvs = document.createElement('canvas');
        cvs.id = 'gl-neon-' + faderId;
        cvs.className = 'gl-neon-fill-canvas';
        cvs.width = 12;
        cvs.height = 280;
        trackEl.appendChild(cvs);

        var neon = new WebGLNeonFaderFill(cvs);
        if (neon._ok) {
            this._neonFills[faderId] = neon;
        }
    }
};

MixerWebGL.prototype._destroyNeonFills = function() {
    for (var id in this._neonFills) {
        this._neonFills[id].destroy();
        var el = document.getElementById('gl-neon-' + id);
        if (el) el.remove();
    }
    this._neonFills = {};
};

/* ======================================================================
 * Exports
 * ====================================================================== */

window.WebGLMixer = {
    FaderCap:       WebGLFaderCap,
    GrainOverlay:   WebGLGrainOverlay,
    NeonFaderFill:  WebGLNeonFaderFill,
    MixerWebGL:     MixerWebGL,
    SKIN_MATERIALS: SKIN_MATERIALS
};

})();
