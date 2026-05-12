/**
 * Mcaster1 WebGL Visualization Engine
 * @version 2.0.1
 * js/webgl-viz.js
 *
 * GPU-accelerated visualizations: spectrogram/waterfall, 3D spectrum bars,
 * LED VU meters with glow, and high-performance waveform rendering.
 * WebGL 2.0 preferred, fallback to WebGL 1.0, then Canvas 2D.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

(function() {
'use strict';

/* ======================================================================
 * Utility: WebGL helpers
 * ====================================================================== */

function isWebGLAvailable() {
    try {
        var c = document.createElement('canvas');
        return !!(c.getContext('webgl2') || c.getContext('webgl'));
    } catch (e) { return false; }
}

function initWebGL(canvas) {
    var gl = canvas.getContext('webgl2', { antialias: true, alpha: false });
    if (gl) return { gl: gl, version: 2 };
    gl = canvas.getContext('webgl', { antialias: true, alpha: false });
    if (gl) return { gl: gl, version: 1 };
    return null;
}

function createShader(gl, type, source) {
    var shader = gl.createShader(type);
    gl.shaderSource(shader, source);
    gl.compileShader(shader);
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
        var info = gl.getShaderInfoLog(shader);
        gl.deleteShader(shader);
        console.error('WebGL shader compile error:', info);
        return null;
    }
    return shader;
}

function createProgram(gl, vsSource, fsSource) {
    var vs = createShader(gl, gl.VERTEX_SHADER, vsSource);
    var fs = createShader(gl, gl.FRAGMENT_SHADER, fsSource);
    if (!vs || !fs) return null;
    var prog = gl.createProgram();
    gl.attachShader(prog, vs);
    gl.attachShader(prog, fs);
    gl.linkProgram(prog);
    if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
        console.error('WebGL program link error:', gl.getProgramInfoLog(prog));
        gl.deleteProgram(prog);
        return null;
    }
    return prog;
}

/* ======================================================================
 * WebGLSpectrogram — 2D frequency-vs-time waterfall display
 * ====================================================================== */

function WebGLSpectrogram(canvas) {
    this.canvas = canvas;
    this._history = 512;          /* columns of time history */
    this._texData = null;
    this._texWidth = 0;
    this._texHeight = 0;
    this._writeCol = 0;
    this._gl = null;
    this._prog = null;
    this._tex = null;
    this._vbo = null;
    this._ok = false;
    this._init();
}

WebGLSpectrogram.prototype._init = function() {
    var result = initWebGL(this.canvas);
    if (!result) return;
    var gl = this._gl = result.gl;

    /* Fullscreen quad */
    var quadVS = 'attribute vec2 a_pos;\n'
        + 'varying vec2 v_uv;\n'
        + 'void main() {\n'
        + '  v_uv = a_pos * 0.5 + 0.5;\n'
        + '  gl_Position = vec4(a_pos, 0.0, 1.0);\n'
        + '}\n';

    var quadFS = 'precision highp float;\n'
        + 'uniform sampler2D u_spectrum;\n'
        + 'uniform float u_offset;\n'
        + 'varying vec2 v_uv;\n'
        + 'vec3 heatmap(float t) {\n'
        + '  vec3 c = vec3(0.0);\n'
        + '  if (t < 0.25) c = mix(vec3(0.0,0.0,0.3), vec3(0.0,0.5,1.0), t*4.0);\n'
        + '  else if (t < 0.5) c = mix(vec3(0.0,0.5,1.0), vec3(0.0,1.0,0.0), (t-0.25)*4.0);\n'
        + '  else if (t < 0.75) c = mix(vec3(0.0,1.0,0.0), vec3(1.0,1.0,0.0), (t-0.5)*4.0);\n'
        + '  else c = mix(vec3(1.0,1.0,0.0), vec3(1.0,0.0,0.0), (t-0.75)*4.0);\n'
        + '  return c;\n'
        + '}\n'
        + 'void main() {\n'
        + '  float x = fract(v_uv.x + u_offset);\n'
        + '  float mag = texture2D(u_spectrum, vec2(x, v_uv.y)).r;\n'
        + '  gl_FragColor = vec4(heatmap(mag), 1.0);\n'
        + '}\n';

    this._prog = createProgram(gl, quadVS, quadFS);
    if (!this._prog) return;

    /* Quad buffer: two triangles covering [-1,1] */
    var verts = new Float32Array([-1,-1, 1,-1, -1,1, 1,1]);
    this._vbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this._vbo);
    gl.bufferData(gl.ARRAY_BUFFER, verts, gl.STATIC_DRAW);

    /* Texture for spectrogram data (history x bins) */
    this._texWidth = this._history;
    this._texHeight = 128;
    this._texData = new Uint8Array(this._texWidth * this._texHeight);
    this._tex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, this._tex);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.LUMINANCE, this._texWidth, this._texHeight, 0, gl.LUMINANCE, gl.UNSIGNED_BYTE, this._texData);

    this._ok = true;
};

WebGLSpectrogram.prototype.pushSpectrum = function(spectrumData) {
    if (!this._ok || !spectrumData || spectrumData.length === 0) return;
    var bins = this._texHeight;
    var col = this._writeCol % this._texWidth;

    /* Map input spectrum to texture column (log-scaled magnitudes to 0-255) */
    var inputLen = spectrumData.length;
    for (var i = 0; i < bins; i++) {
        var srcIdx = Math.floor(i / bins * inputLen);
        if (srcIdx >= inputLen) srcIdx = inputLen - 1;
        var mag = spectrumData[srcIdx];
        var db = mag > 0 ? 20 * Math.log10(mag) : -96;
        var norm = Math.max(0, Math.min(1, (db + 96) / 96));
        this._texData[col + (bins - 1 - i) * this._texWidth] = Math.round(norm * 255);
    }
    this._writeCol++;
};

WebGLSpectrogram.prototype.draw = function() {
    if (!this._ok) return;
    var gl = this._gl;
    gl.viewport(0, 0, this.canvas.width, this.canvas.height);
    gl.clearColor(0.04, 0.06, 0.12, 1.0);
    gl.clear(gl.COLOR_BUFFER_BIT);

    /* Upload updated texture */
    gl.bindTexture(gl.TEXTURE_2D, this._tex);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.LUMINANCE, this._texWidth, this._texHeight, 0, gl.LUMINANCE, gl.UNSIGNED_BYTE, this._texData);

    gl.useProgram(this._prog);

    var aPos = gl.getAttribLocation(this._prog, 'a_pos');
    gl.bindBuffer(gl.ARRAY_BUFFER, this._vbo);
    gl.enableVertexAttribArray(aPos);
    gl.vertexAttribPointer(aPos, 2, gl.FLOAT, false, 0, 0);

    var uOffset = gl.getUniformLocation(this._prog, 'u_offset');
    gl.uniform1f(uOffset, (this._writeCol % this._texWidth) / this._texWidth);

    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
};

WebGLSpectrogram.prototype.destroy = function() {
    if (!this._gl) return;
    var gl = this._gl;
    if (this._tex) gl.deleteTexture(this._tex);
    if (this._vbo) gl.deleteBuffer(this._vbo);
    if (this._prog) gl.deleteProgram(this._prog);
    this._ok = false;
};

/* ======================================================================
 * WebGLSpectrum3D — 3D bar spectrum analyzer with perspective tilt
 * ====================================================================== */

function WebGLSpectrum3D(canvas) {
    this.canvas = canvas;
    this._gl = null;
    this._prog = null;
    this._vbo = null;
    this._ok = false;
    this._bars = 64;
    this._heights = new Float32Array(this._bars);
    this._smoothHeights = new Float32Array(this._bars);
    this._init();
}

WebGLSpectrum3D.prototype._init = function() {
    var result = initWebGL(this.canvas);
    if (!result) return;
    var gl = this._gl = result.gl;

    /* Each bar is a quad (2 triangles, 6 verts). Attribute: a_pos(x,y), a_barIdx(float), a_vertY(0=base, 1=top) */
    var barVS = 'attribute vec2 a_pos;\n'
        + 'attribute float a_barIdx;\n'
        + 'attribute float a_vertY;\n'
        + 'uniform float u_heights[64];\n'
        + 'uniform float u_numBars;\n'
        + 'varying float v_normH;\n'
        + 'varying float v_vertY;\n'
        + 'void main() {\n'
        + '  int idx = int(a_barIdx);\n'
        + '  float h = u_heights[idx];\n'
        + '  v_normH = h;\n'
        + '  v_vertY = a_vertY;\n'
        + '  float x = a_pos.x;\n'
        + '  float y = -0.9 + a_vertY * h * 1.7;\n'
        + '  /* Subtle 3D perspective tilt */\n'
        + '  float depth = 0.06 * (1.0 - a_vertY);\n'
        + '  y -= depth;\n'
        + '  x *= (1.0 + depth * 0.3);\n'
        + '  gl_Position = vec4(x, y, 0.0, 1.0);\n'
        + '}\n';

    var barFS = 'precision mediump float;\n'
        + 'varying float v_normH;\n'
        + 'varying float v_vertY;\n'
        + 'void main() {\n'
        + '  /* Green at base -> yellow -> red at top */\n'
        + '  vec3 green = vec3(0.13, 0.77, 0.37);\n'
        + '  vec3 yellow = vec3(0.92, 0.72, 0.03);\n'
        + '  vec3 red = vec3(0.94, 0.27, 0.27);\n'
        + '  vec3 col;\n'
        + '  float t = v_vertY;\n'
        + '  if (t < 0.6) col = mix(green, yellow, t / 0.6);\n'
        + '  else col = mix(yellow, red, (t - 0.6) / 0.4);\n'
        + '  /* Brighten based on bar height */\n'
        + '  col *= 0.7 + 0.3 * v_normH;\n'
        + '  /* Reflection below base line */\n'
        + '  float alpha = 1.0;\n'
        + '  gl_FragColor = vec4(col, alpha);\n'
        + '}\n';

    this._prog = createProgram(gl, barVS, barFS);
    if (!this._prog) return;

    /* Build bar geometry */
    var numBars = this._bars;
    var verts = [];
    var totalW = 1.85;
    var barW = totalW / numBars * 0.8;
    var gap = totalW / numBars * 0.2;

    for (var i = 0; i < numBars; i++) {
        var x0 = -totalW / 2 + i * (barW + gap);
        var x1 = x0 + barW;
        /* Two triangles per bar: (x0,0,base), (x1,0,base), (x0,1,top), (x1,1,top) */
        /* Tri 1: base-left, base-right, top-left */
        verts.push(x0, 0, i, 0, x1, 0, i, 0, x0, 0, i, 1);
        /* Tri 2: base-right, top-right, top-left */
        verts.push(x1, 0, i, 0, x1, 0, i, 1, x0, 0, i, 1);

        /* Reflection triangles (mirrored, drawn below base) */
        verts.push(x0, 0, i, 0, x1, 0, i, 0, x0, 0, i, -0.3);
        verts.push(x1, 0, i, 0, x1, 0, i, -0.3, x0, 0, i, -0.3);
    }

    this._vertCount = verts.length / 4;
    var vertArr = new Float32Array(verts);
    this._vbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this._vbo);
    gl.bufferData(gl.ARRAY_BUFFER, vertArr, gl.STATIC_DRAW);

    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

    this._ok = true;
};

WebGLSpectrum3D.prototype.update = function(spectrumData, sampleRate) {
    if (!spectrumData || spectrumData.length === 0) return;
    var numBars = this._bars;
    var binCount = spectrumData.length;
    sampleRate = sampleRate || 48000;
    var nyquist = sampleRate / 2;
    var logMin = Math.log10(20);
    var logMax = Math.log10(20000);
    var logRange = logMax - logMin;

    for (var i = 0; i < numBars; i++) {
        var logF0 = logMin + (i / numBars) * logRange;
        var logF1 = logMin + ((i + 1) / numBars) * logRange;
        var f0 = Math.pow(10, logF0);
        var f1 = Math.pow(10, logF1);
        var b0 = Math.max(0, Math.floor(f0 / nyquist * binCount));
        var b1 = Math.min(binCount - 1, Math.ceil(f1 / nyquist * binCount));
        var maxMag = 0;
        for (var k = b0; k <= b1; k++) {
            if (spectrumData[k] > maxMag) maxMag = spectrumData[k];
        }
        var db = maxMag > 0 ? 20 * Math.log10(maxMag) : -96;
        this._heights[i] = Math.max(0, Math.min(1, (db + 96) / 96));
    }

    /* Smooth animation */
    for (var j = 0; j < numBars; j++) {
        var target = this._heights[j];
        var cur = this._smoothHeights[j];
        if (target > cur) {
            this._smoothHeights[j] = cur + (target - cur) * 0.5;
        } else {
            this._smoothHeights[j] = cur * 0.88;
        }
        if (this._smoothHeights[j] < 0.005) this._smoothHeights[j] = 0;
    }
};

WebGLSpectrum3D.prototype.draw = function() {
    if (!this._ok) return;
    var gl = this._gl;
    gl.viewport(0, 0, this.canvas.width, this.canvas.height);
    gl.clearColor(0.04, 0.06, 0.12, 1.0);
    gl.clear(gl.COLOR_BUFFER_BIT);

    gl.useProgram(this._prog);

    /* Upload bar heights */
    var uHeights = gl.getUniformLocation(this._prog, 'u_heights[0]');
    gl.uniform1fv(uHeights, this._smoothHeights);

    var stride = 4 * 4; /* 4 floats * 4 bytes */
    gl.bindBuffer(gl.ARRAY_BUFFER, this._vbo);

    var aPos = gl.getAttribLocation(this._prog, 'a_pos');
    gl.enableVertexAttribArray(aPos);
    gl.vertexAttribPointer(aPos, 2, gl.FLOAT, false, stride, 0);

    var aBarIdx = gl.getAttribLocation(this._prog, 'a_barIdx');
    gl.enableVertexAttribArray(aBarIdx);
    gl.vertexAttribPointer(aBarIdx, 1, gl.FLOAT, false, stride, 8);

    var aVertY = gl.getAttribLocation(this._prog, 'a_vertY');
    gl.enableVertexAttribArray(aVertY);
    gl.vertexAttribPointer(aVertY, 1, gl.FLOAT, false, stride, 12);

    gl.drawArrays(gl.TRIANGLES, 0, this._vertCount);
};

WebGLSpectrum3D.prototype.destroy = function() {
    if (!this._gl) return;
    var gl = this._gl;
    if (this._vbo) gl.deleteBuffer(this._vbo);
    if (this._prog) gl.deleteProgram(this._prog);
    this._ok = false;
};

/* ======================================================================
 * WebGLVUMeter — LED-style meter segments with additive glow
 * ====================================================================== */

function WebGLVUMeter(canvas, opts) {
    this.canvas = canvas;
    this._gl = null;
    this._prog = null;
    this._vbo = null;
    this._ok = false;
    this._segments = 14;
    this._level = 0;
    this._peakLevel = 0;
    this._peakDecay = 0;
    this._vertical = (opts && opts.orientation === 'horizontal') ? false : true;
    this._init();
}

WebGLVUMeter.prototype._init = function() {
    var result = initWebGL(this.canvas);
    if (!result) return;
    var gl = this._gl = result.gl;

    var vuVS = 'attribute vec2 a_pos;\n'
        + 'attribute float a_segIdx;\n'
        + 'uniform float u_level;\n'
        + 'uniform float u_peakSeg;\n'
        + 'uniform float u_numSegs;\n'
        + 'varying float v_segIdx;\n'
        + 'varying float v_lit;\n'
        + 'varying float v_isPeak;\n'
        + 'void main() {\n'
        + '  v_segIdx = a_segIdx;\n'
        + '  float litCount = u_level * u_numSegs;\n'
        + '  v_lit = (a_segIdx < litCount) ? 1.0 : 0.0;\n'
        + '  v_isPeak = (abs(a_segIdx - u_peakSeg) < 0.5) ? 1.0 : 0.0;\n'
        + '  gl_Position = vec4(a_pos, 0.0, 1.0);\n'
        + '}\n';

    var vuFS = 'precision mediump float;\n'
        + 'varying float v_segIdx;\n'
        + 'varying float v_lit;\n'
        + 'varying float v_isPeak;\n'
        + 'uniform float u_numSegs;\n'
        + 'void main() {\n'
        + '  float t = v_segIdx / u_numSegs;\n'
        + '  /* Segment color: green -> yellow -> red */\n'
        + '  vec3 green = vec3(0.13, 0.77, 0.37);\n'
        + '  vec3 yellow = vec3(0.92, 0.72, 0.03);\n'
        + '  vec3 red = vec3(0.94, 0.27, 0.27);\n'
        + '  vec3 col;\n'
        + '  if (t < 0.57) col = mix(green, vec3(0.26,0.83,0.5), t/0.57);\n'
        + '  else if (t < 0.85) col = mix(yellow, vec3(0.98,0.85,0.1), (t-0.57)/0.28);\n'
        + '  else col = mix(red, vec3(1.0,0.3,0.3), (t-0.85)/0.15);\n'
        + '  if (v_lit > 0.5 || v_isPeak > 0.5) {\n'
        + '    float glow = v_lit > 0.5 ? 1.0 : 0.85;\n'
        + '    /* Peak indicator is white */\n'
        + '    if (v_isPeak > 0.5 && v_lit < 0.5) col = vec3(1.0);\n'
        + '    gl_FragColor = vec4(col * glow, 1.0);\n'
        + '  } else {\n'
        + '    gl_FragColor = vec4(col * 0.08, 1.0);\n'
        + '  }\n'
        + '}\n';

    this._prog = createProgram(gl, vuVS, vuFS);
    if (!this._prog) return;

    /* Build segment quads */
    var segs = this._segments;
    var verts = [];
    var margin = 0.08;
    var totalH = 2.0 - 2 * margin;
    var segH = totalH / segs * 0.85;
    var segGap = totalH / segs * 0.15;
    var segW = 0.6;

    for (var i = 0; i < segs; i++) {
        var y0, y1, x0, x1;
        if (this._vertical) {
            y0 = -1.0 + margin + i * (segH + segGap);
            y1 = y0 + segH;
            x0 = -segW;
            x1 = segW;
        } else {
            x0 = -1.0 + margin + i * (segH + segGap);
            x1 = x0 + segH;
            y0 = -segW;
            y1 = segW;
        }
        /* Two triangles */
        verts.push(x0, y0, i, x1, y0, i, x0, y1, i);
        verts.push(x1, y0, i, x1, y1, i, x0, y1, i);
    }

    this._vertCount = verts.length / 3;
    this._vbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this._vbo);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(verts), gl.STATIC_DRAW);

    this._ok = true;
};

WebGLVUMeter.prototype.setLevel = function(level, peakLevel) {
    this._level = Math.max(0, Math.min(1, level));
    if (peakLevel !== undefined) {
        if (peakLevel > this._peakLevel) {
            this._peakLevel = peakLevel;
            this._peakDecay = 0;
        } else {
            this._peakDecay++;
            if (this._peakDecay > 30) {
                this._peakLevel -= 0.015;
                if (this._peakLevel < 0) this._peakLevel = 0;
            }
        }
    }
};

WebGLVUMeter.prototype.draw = function() {
    if (!this._ok) return;
    var gl = this._gl;
    gl.viewport(0, 0, this.canvas.width, this.canvas.height);
    gl.clearColor(0.04, 0.06, 0.12, 1.0);
    gl.clear(gl.COLOR_BUFFER_BIT);

    gl.useProgram(this._prog);

    var uLevel = gl.getUniformLocation(this._prog, 'u_level');
    gl.uniform1f(uLevel, this._level);

    var uPeakSeg = gl.getUniformLocation(this._prog, 'u_peakSeg');
    gl.uniform1f(uPeakSeg, this._peakLevel * this._segments);

    var uNumSegs = gl.getUniformLocation(this._prog, 'u_numSegs');
    gl.uniform1f(uNumSegs, this._segments);

    var stride = 3 * 4;
    gl.bindBuffer(gl.ARRAY_BUFFER, this._vbo);

    var aPos = gl.getAttribLocation(this._prog, 'a_pos');
    gl.enableVertexAttribArray(aPos);
    gl.vertexAttribPointer(aPos, 2, gl.FLOAT, false, stride, 0);

    var aSegIdx = gl.getAttribLocation(this._prog, 'a_segIdx');
    gl.enableVertexAttribArray(aSegIdx);
    gl.vertexAttribPointer(aSegIdx, 1, gl.FLOAT, false, stride, 8);

    gl.drawArrays(gl.TRIANGLES, 0, this._vertCount);
};

WebGLVUMeter.prototype.destroy = function() {
    if (!this._gl) return;
    var gl = this._gl;
    if (this._vbo) gl.deleteBuffer(this._vbo);
    if (this._prog) gl.deleteProgram(this._prog);
    this._ok = false;
};

/* ======================================================================
 * WebGLWaveform — GPU-accelerated waveform line strip
 * ====================================================================== */

function WebGLWaveform(canvas) {
    this.canvas = canvas;
    this._gl = null;
    this._prog = null;
    this._vbo = null;
    this._ok = false;
    this._maxPoints = 8192;
    this._pointCount = 0;
    this._init();
}

WebGLWaveform.prototype._init = function() {
    var result = initWebGL(this.canvas);
    if (!result) return;
    var gl = this._gl = result.gl;

    var waveVS = 'attribute vec2 a_pos;\n'
        + 'uniform float u_scale;\n'
        + 'varying float v_amp;\n'
        + 'void main() {\n'
        + '  v_amp = abs(a_pos.y * u_scale);\n'
        + '  float y = a_pos.y * u_scale;\n'
        + '  gl_Position = vec4(a_pos.x, y, 0.0, 1.0);\n'
        + '}\n';

    var waveFS = 'precision mediump float;\n'
        + 'varying float v_amp;\n'
        + 'void main() {\n'
        + '  vec3 green = vec3(0.13, 0.77, 0.37);\n'
        + '  vec3 cyan = vec3(0.08, 0.72, 0.83);\n'
        + '  vec3 col = mix(green, cyan, v_amp);\n'
        + '  /* Glow effect: brighter at higher amplitude */\n'
        + '  float glow = 0.7 + 0.3 * v_amp;\n'
        + '  gl_FragColor = vec4(col * glow, 1.0);\n'
        + '}\n';

    this._prog = createProgram(gl, waveVS, waveFS);
    if (!this._prog) return;

    /* Pre-allocate VBO for max points */
    this._vbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this._vbo);
    gl.bufferData(gl.ARRAY_BUFFER, this._maxPoints * 2 * 4, gl.DYNAMIC_DRAW);

    this._ok = true;
};

WebGLWaveform.prototype.setData = function(waveformData) {
    if (!this._ok || !waveformData || waveformData.length === 0) return;
    var gl = this._gl;
    var len = Math.min(waveformData.length, this._maxPoints);
    this._pointCount = len;

    /* Find trigger point for stable display */
    var triggerIdx = 0;
    for (var i = 1; i < len / 2; i++) {
        if (waveformData[i - 1] <= 0 && waveformData[i] > 0) {
            triggerIdx = i;
            break;
        }
    }

    var displayLen = Math.min(len - triggerIdx, len);
    this._pointCount = displayLen;

    var verts = new Float32Array(displayLen * 2);
    for (var j = 0; j < displayLen; j++) {
        verts[j * 2] = (j / displayLen) * 2.0 - 1.0;     /* x: -1 to 1 */
        verts[j * 2 + 1] = waveformData[triggerIdx + j];  /* y: raw amplitude */
    }

    gl.bindBuffer(gl.ARRAY_BUFFER, this._vbo);
    gl.bufferSubData(gl.ARRAY_BUFFER, 0, verts);
};

WebGLWaveform.prototype.draw = function() {
    if (!this._ok || this._pointCount < 2) return;
    var gl = this._gl;
    gl.viewport(0, 0, this.canvas.width, this.canvas.height);
    gl.clearColor(0.04, 0.06, 0.12, 1.0);
    gl.clear(gl.COLOR_BUFFER_BIT);

    gl.useProgram(this._prog);

    /* Auto-scale */
    var uScale = gl.getUniformLocation(this._prog, 'u_scale');
    gl.uniform1f(uScale, 0.9);

    gl.bindBuffer(gl.ARRAY_BUFFER, this._vbo);
    var aPos = gl.getAttribLocation(this._prog, 'a_pos');
    gl.enableVertexAttribArray(aPos);
    gl.vertexAttribPointer(aPos, 2, gl.FLOAT, false, 0, 0);

    gl.lineWidth(2.0);
    gl.drawArrays(gl.LINE_STRIP, 0, this._pointCount);
};

WebGLWaveform.prototype.destroy = function() {
    if (!this._gl) return;
    var gl = this._gl;
    if (this._vbo) gl.deleteBuffer(this._vbo);
    if (this._prog) gl.deleteProgram(this._prog);
    this._ok = false;
};

/* ======================================================================
 * WebGLWaveformPeaks — GPU-rendered waveform from pre-computed peaks
 * Used by episode-editor.js for large audio files.
 * ====================================================================== */

function WebGLWaveformPeaks(canvas) {
    this.canvas = canvas;
    this._gl = null;
    this._prog = null;
    this._vbo = null;
    this._ok = false;
    this._maxPeaks = 65536;
    this._peakCount = 0;
    this._init();
}

WebGLWaveformPeaks.prototype._init = function() {
    var result = initWebGL(this.canvas);
    if (!result) return;
    var gl = this._gl = result.gl;

    /* Each peak becomes a vertical line segment (min, max). We draw as GL_LINES. */
    var peakVS = 'attribute vec2 a_pos;\n'
        + 'uniform float u_scrollX;\n'
        + 'uniform float u_visibleW;\n'
        + 'uniform float u_totalW;\n'
        + 'varying float v_amp;\n'
        + 'void main() {\n'
        + '  float x = (a_pos.x - u_scrollX) / u_visibleW * 2.0 - 1.0;\n'
        + '  v_amp = abs(a_pos.y);\n'
        + '  gl_Position = vec4(x, a_pos.y * 0.9, 0.0, 1.0);\n'
        + '}\n';

    var peakFS = 'precision mediump float;\n'
        + 'varying float v_amp;\n'
        + 'void main() {\n'
        + '  vec3 teal = vec3(0.08, 0.72, 0.65);\n'
        + '  vec3 cyan = vec3(0.02, 0.60, 0.83);\n'
        + '  vec3 col = mix(teal, cyan, v_amp);\n'
        + '  gl_FragColor = vec4(col, 0.85);\n'
        + '}\n';

    this._prog = createProgram(gl, peakVS, peakFS);
    if (!this._prog) return;

    this._vbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this._vbo);
    gl.bufferData(gl.ARRAY_BUFFER, this._maxPeaks * 4 * 4, gl.DYNAMIC_DRAW);

    this._ok = true;
};

/**
 * setPeaks(peaks, peakSR, duration)
 * peaks = [{min, max}, ...], peakSR = peaks/sec, duration = total seconds
 */
WebGLWaveformPeaks.prototype.setPeaks = function(peaks, peakSR, duration) {
    if (!this._ok || !peaks || peaks.length === 0) return;
    var gl = this._gl;
    var len = Math.min(peaks.length, this._maxPeaks);
    this._peakCount = len;
    this._peakSR = peakSR;
    this._duration = duration;

    /* Each peak = 2 vertices (min line, max line) */
    var verts = new Float32Array(len * 4);
    for (var i = 0; i < len; i++) {
        var t = i / peakSR; /* time in seconds */
        verts[i * 4]     = t;
        verts[i * 4 + 1] = peaks[i].max;
        verts[i * 4 + 2] = t;
        verts[i * 4 + 3] = peaks[i].min;
    }

    gl.bindBuffer(gl.ARRAY_BUFFER, this._vbo);
    gl.bufferSubData(gl.ARRAY_BUFFER, 0, verts);
};

WebGLWaveformPeaks.prototype.draw = function(scrollX, visibleDur) {
    if (!this._ok || this._peakCount < 2) return;
    var gl = this._gl;
    gl.viewport(0, 0, this.canvas.width, this.canvas.height);
    gl.clearColor(0.06, 0.09, 0.16, 1.0);
    gl.clear(gl.COLOR_BUFFER_BIT);

    gl.useProgram(this._prog);

    gl.uniform1f(gl.getUniformLocation(this._prog, 'u_scrollX'), scrollX || 0);
    gl.uniform1f(gl.getUniformLocation(this._prog, 'u_visibleW'), visibleDur || this._duration);
    gl.uniform1f(gl.getUniformLocation(this._prog, 'u_totalW'), this._duration);

    gl.bindBuffer(gl.ARRAY_BUFFER, this._vbo);
    var aPos = gl.getAttribLocation(this._prog, 'a_pos');
    gl.enableVertexAttribArray(aPos);
    gl.vertexAttribPointer(aPos, 2, gl.FLOAT, false, 0, 0);

    gl.drawArrays(gl.LINES, 0, this._peakCount * 2);
};

WebGLWaveformPeaks.prototype.destroy = function() {
    if (!this._gl) return;
    var gl = this._gl;
    if (this._vbo) gl.deleteBuffer(this._vbo);
    if (this._prog) gl.deleteProgram(this._prog);
    this._ok = false;
};

/* ======================================================================
 * Preference management
 * ====================================================================== */

var PREF_KEY = 'mc1_webgl_viz';

function getWebGLPref() {
    try {
        return localStorage.getItem(PREF_KEY) === 'true';
    } catch (e) { return false; }
}

function setWebGLPref(enabled) {
    try {
        localStorage.setItem(PREF_KEY, enabled ? 'true' : 'false');
    } catch (e) { /* localStorage unavailable */ }
}

/* ======================================================================
 * Exports
 * ====================================================================== */

window.WebGLViz = {
    isWebGLAvailable: isWebGLAvailable,
    initWebGL: initWebGL,
    createShader: createShader,
    createProgram: createProgram,
    getWebGLPref: getWebGLPref,
    setWebGLPref: setWebGLPref,

    Spectrogram: WebGLSpectrogram,
    Spectrum3D:  WebGLSpectrum3D,
    VUMeter:     WebGLVUMeter,
    Waveform:    WebGLWaveform,
    WaveformPeaks: WebGLWaveformPeaks
};

})();
