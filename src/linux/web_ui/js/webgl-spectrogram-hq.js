/**
 * webgl-spectrogram-hq.js — High-Quality WebGL 2.0 Spectrogram Renderer
 *
 * File:    src/linux/web_ui/js/webgl-spectrogram-hq.js
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Phase:   FA-1
 * Purpose: We provide an offline-mode, high-resolution spectrogram renderer that
 *          pre-computes the full spectrogram as a 2D floating-point texture and
 *          renders it with configurable color maps, zoom, pan, and a minimap.
 *          We extend the WebGL infrastructure from webgl-viz.js.
 *
 * Standards:
 *  - We use WebGL 2.0 (R32F texture) with fallback to WebGL 1.0 (LUMINANCE)
 *  - We handle spectrogram data as Float32Array (time_frames x freq_bins)
 *  - We support 6 color maps via fragment shader uniform switching
 *  - We provide zoom (mouse wheel), pan (click-drag), and frequency/time axis independent zoom
 */

/* global WebGLViz */

(function() {
'use strict';

/* ======================================================================
 * HQSpectrogram — Offline high-resolution spectrogram display
 * ====================================================================== */

function HQSpectrogram(canvas) {
    this.canvas = canvas;
    this._gl = null;
    this._prog = null;
    this._tex = null;
    this._vbo = null;
    this._ok = false;
    this._isWebGL2 = false;

    /* Data dimensions */
    this._texWidth = 0;      /* time frames */
    this._texHeight = 0;     /* frequency bins (fftSize/2) */
    this._duration = 0;      /* total duration in seconds */
    this._sampleRate = 44100;
    this._fftSize = 4096;

    /* View state */
    this._viewX = 0;         /* left edge in seconds */
    this._viewW = 0;         /* visible width in seconds */
    this._viewFreqLo = 0;    /* bottom freq in normalized 0-1 */
    this._viewFreqHi = 1;    /* top freq in normalized 0-1 */
    this._zoomX = 1;
    this._zoomY = 1;

    /* Colormap and gain */
    this._colormap = 0;      /* 0=heat, 1=gray, 2=rainbow, 3=inferno, 4=ice */
    this._gain = 0;          /* dB gain offset */
    this._floor = -96;       /* noise floor dB */

    /* Interaction */
    this._isDragging = false;
    this._dragStartX = 0;
    this._dragStartY = 0;
    this._dragViewX = 0;
    this._dragViewFreqLo = 0;
    this._dragViewFreqHi = 0;

    this._init();
}

/* Color map names for external lookup */
HQSpectrogram.COLORMAPS = { heat: 0, gray: 1, rainbow: 2, inferno: 3, ice: 4 };

HQSpectrogram.prototype._init = function() {
    var result = WebGLViz.initWebGL(this.canvas);
    if (!result) return;
    var gl = this._gl = result.gl;
    this._isWebGL2 = result.version === 2;

    /* Vertex shader: fullscreen quad with UV mapping to viewport */
    var vs = (this._isWebGL2 ? '#version 300 es\n' : '')
        + (this._isWebGL2 ? 'in vec2 a_pos;\nout vec2 v_uv;\n' : 'attribute vec2 a_pos;\nvarying vec2 v_uv;\n')
        + 'uniform vec4 u_viewport;\n'  /* x=viewX/dur, y=viewW/dur, z=freqLo, w=freqHi */
        + 'void main() {\n'
        + '  float u = u_viewport.x + v_uv.x * u_viewport.y;\n'
        + '  float v = u_viewport.z + v_uv.y * (u_viewport.w - u_viewport.z);\n'
        + (this._isWebGL2
            ? '  v_uv = a_pos * 0.5 + 0.5;\n  gl_Position = vec4(a_pos, 0.0, 1.0);\n'
            : '  v_uv = a_pos * 0.5 + 0.5;\n  gl_Position = vec4(a_pos, 0.0, 1.0);\n')
        + '}\n';

    /* We need UV before viewport transform, so restructure */
    vs = this._isWebGL2
        ? '#version 300 es\nin vec2 a_pos;\nout vec2 v_uv;\nuniform vec4 u_viewport;\nvoid main() {\n  vec2 raw_uv = a_pos * 0.5 + 0.5;\n  v_uv = vec2(u_viewport.x + raw_uv.x * u_viewport.y, u_viewport.z + raw_uv.y * (u_viewport.w - u_viewport.z));\n  gl_Position = vec4(a_pos, 0.0, 1.0);\n}\n'
        : 'attribute vec2 a_pos;\nvarying vec2 v_uv;\nuniform vec4 u_viewport;\nvoid main() {\n  vec2 raw_uv = a_pos * 0.5 + 0.5;\n  v_uv = vec2(u_viewport.x + raw_uv.x * u_viewport.y, u_viewport.z + raw_uv.y * (u_viewport.w - u_viewport.z));\n  gl_Position = vec4(a_pos, 0.0, 1.0);\n}\n';

    /* Fragment shader with 5 color maps */
    var fsPrecision = 'precision highp float;\n';
    var fsVary = this._isWebGL2
        ? '#version 300 es\nprecision highp float;\nin vec2 v_uv;\nout vec4 fragColor;\n'
        : fsPrecision + 'varying vec2 v_uv;\n';
    var fsBody = ''
        + 'uniform sampler2D u_spectrum;\n'
        + 'uniform int u_colormap;\n'
        + 'uniform float u_gain;\n'
        + 'uniform float u_floor;\n'
        + '\n'
        + 'vec3 heatmap(float t) {\n'
        + '  if (t < 0.25) return mix(vec3(0.0,0.0,0.3), vec3(0.0,0.5,1.0), t*4.0);\n'
        + '  if (t < 0.5) return mix(vec3(0.0,0.5,1.0), vec3(0.0,1.0,0.0), (t-0.25)*4.0);\n'
        + '  if (t < 0.75) return mix(vec3(0.0,1.0,0.0), vec3(1.0,1.0,0.0), (t-0.5)*4.0);\n'
        + '  return mix(vec3(1.0,1.0,0.0), vec3(1.0,0.0,0.0), (t-0.75)*4.0);\n'
        + '}\n'
        + 'vec3 inferno(float t) {\n'
        + '  if (t < 0.33) return mix(vec3(0.0,0.0,0.04), vec3(0.55,0.1,0.55), t*3.0);\n'
        + '  if (t < 0.66) return mix(vec3(0.55,0.1,0.55), vec3(0.95,0.55,0.15), (t-0.33)*3.0);\n'
        + '  return mix(vec3(0.95,0.55,0.15), vec3(1.0,1.0,0.65), (t-0.66)*3.0);\n'
        + '}\n'
        + 'vec3 ice(float t) {\n'
        + '  if (t < 0.5) return mix(vec3(0.0,0.0,0.1), vec3(0.1,0.3,0.8), t*2.0);\n'
        + '  return mix(vec3(0.1,0.3,0.8), vec3(0.85,0.95,1.0), (t-0.5)*2.0);\n'
        + '}\n'
        + 'vec3 hsv2rgb(vec3 c) {\n'
        + '  vec3 p = abs(fract(c.xxx + vec3(1.0,2.0/3.0,1.0/3.0)) * 6.0 - 3.0);\n'
        + '  return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);\n'
        + '}\n'
        + 'vec3 applyColormap(float mag) {\n'
        + '  float range = -u_floor;\n'
        + '  float t = clamp((mag - u_floor + u_gain) / range, 0.0, 1.0);\n'
        + '  if (u_colormap == 0) return heatmap(t);\n'
        + '  if (u_colormap == 1) return vec3(t);\n'
        + '  if (u_colormap == 2) return hsv2rgb(vec3(t * 0.8, 1.0, t > 0.01 ? 1.0 : 0.0));\n'
        + '  if (u_colormap == 3) return inferno(t);\n'
        + '  return ice(t);\n'
        + '}\n'
        + 'void main() {\n'
        + (this._isWebGL2
            ? '  float mag = texture(u_spectrum, v_uv).r;\n'
            : '  float mag = texture2D(u_spectrum, v_uv).r;\n')
        + '  vec3 col = applyColormap(mag);\n'
        + (this._isWebGL2
            ? '  fragColor = vec4(col, 1.0);\n'
            : '  gl_FragColor = vec4(col, 1.0);\n')
        + '}\n';

    var fs = fsVary + fsBody;

    this._prog = WebGLViz.createProgram(gl, vs, fs);
    if (!this._prog) return;

    /* Fullscreen quad */
    var verts = new Float32Array([-1,-1, 1,-1, -1,1, 1,1]);
    this._vbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this._vbo);
    gl.bufferData(gl.ARRAY_BUFFER, verts, gl.STATIC_DRAW);

    /* Create empty texture placeholder */
    this._tex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, this._tex);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

    this._ok = true;
};

/**
 * uploadSpectrogram(data, width, height, duration, sampleRate, fftSize)
 * data: Float32Array of dB magnitudes, row-major (height rows x width cols)
 *       row 0 = lowest frequency, row height-1 = highest frequency
 */
HQSpectrogram.prototype.uploadSpectrogram = function(data, width, height, duration, sampleRate, fftSize) {
    if (!this._ok) return;
    var gl = this._gl;

    this._texWidth = width;
    this._texHeight = height;
    this._duration = duration;
    this._sampleRate = sampleRate || 44100;
    this._fftSize = fftSize || 4096;

    /* Reset view */
    this._viewX = 0;
    this._viewW = duration;
    this._viewFreqLo = 0;
    this._viewFreqHi = 1;
    this._zoomX = 1;
    this._zoomY = 1;

    gl.bindTexture(gl.TEXTURE_2D, this._tex);

    if (this._isWebGL2) {
        /* WebGL 2: R32F texture for full float precision */
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.R32F, width, height, 0, gl.RED, gl.FLOAT, data);
    } else {
        /* WebGL 1 fallback: convert to LUMINANCE uint8 */
        var u8 = new Uint8Array(width * height);
        for (var i = 0; i < data.length; i++) {
            var norm = Math.max(0, Math.min(1, (data[i] + 96) / 96));
            u8[i] = Math.round(norm * 255);
        }
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.LUMINANCE, width, height, 0, gl.LUMINANCE, gl.UNSIGNED_BYTE, u8);
    }
};

HQSpectrogram.prototype.setColormap = function(id) {
    this._colormap = typeof id === 'string' ? (HQSpectrogram.COLORMAPS[id] || 0) : id;
};

HQSpectrogram.prototype.setGain = function(dB) { this._gain = dB; };
HQSpectrogram.prototype.setFloor = function(dB) { this._floor = dB; };

/* ── Zoom and pan ── */

HQSpectrogram.prototype.zoomAt = function(cx, cy, factorX, factorY) {
    /* cx, cy in normalized 0-1 canvas coordinates */
    if (!this._duration) return;

    /* Time zoom */
    if (factorX !== 1) {
        var timeAtCursor = this._viewX + cx * this._viewW;
        var newW = Math.max(0.01, Math.min(this._duration, this._viewW / factorX));
        this._viewX = timeAtCursor - cx * newW;
        this._viewW = newW;
        this._zoomX = this._duration / newW;
    }

    /* Frequency zoom */
    if (factorY !== 1) {
        var freqRange = this._viewFreqHi - this._viewFreqLo;
        var freqAtCursor = this._viewFreqLo + (1 - cy) * freqRange;
        var newRange = Math.max(0.01, Math.min(1, freqRange / factorY));
        this._viewFreqLo = freqAtCursor - (1 - cy) * newRange;
        this._viewFreqHi = this._viewFreqLo + newRange;
        this._zoomY = 1 / newRange;
    }

    this._clampView();
};

HQSpectrogram.prototype.pan = function(dx, dy) {
    /* dx, dy in normalized 0-1 canvas coordinates */
    this._viewX -= dx * this._viewW;
    var freqRange = this._viewFreqHi - this._viewFreqLo;
    this._viewFreqLo += dy * freqRange;
    this._viewFreqHi += dy * freqRange;
    this._clampView();
};

HQSpectrogram.prototype.resetView = function() {
    this._viewX = 0;
    this._viewW = this._duration || 1;
    this._viewFreqLo = 0;
    this._viewFreqHi = 1;
    this._zoomX = 1;
    this._zoomY = 1;
};

HQSpectrogram.prototype._clampView = function() {
    if (this._viewX < 0) this._viewX = 0;
    if (this._viewX + this._viewW > this._duration) {
        this._viewX = Math.max(0, this._duration - this._viewW);
    }
    if (this._viewFreqLo < 0) {
        this._viewFreqHi -= this._viewFreqLo;
        this._viewFreqLo = 0;
    }
    if (this._viewFreqHi > 1) {
        this._viewFreqLo -= (this._viewFreqHi - 1);
        this._viewFreqHi = 1;
        if (this._viewFreqLo < 0) this._viewFreqLo = 0;
    }
};

/* ── Coordinate conversions ── */

HQSpectrogram.prototype.canvasToTime = function(px) {
    return this._viewX + (px / this.canvas.width) * this._viewW;
};

HQSpectrogram.prototype.canvasToFreq = function(py) {
    var norm = 1 - (py / this.canvas.height);
    var freqNorm = this._viewFreqLo + norm * (this._viewFreqHi - this._viewFreqLo);
    return freqNorm * (this._sampleRate / 2);
};

HQSpectrogram.prototype.timeToCanvas = function(t) {
    return ((t - this._viewX) / this._viewW) * this.canvas.width;
};

HQSpectrogram.prototype.freqToCanvas = function(f) {
    var norm = f / (this._sampleRate / 2);
    var viewNorm = (norm - this._viewFreqLo) / (this._viewFreqHi - this._viewFreqLo);
    return (1 - viewNorm) * this.canvas.height;
};

/* ── Minimap viewport info ── */

HQSpectrogram.prototype.getViewport = function() {
    return {
        xFrac: this._duration > 0 ? this._viewX / this._duration : 0,
        wFrac: this._duration > 0 ? this._viewW / this._duration : 1,
        yFrac: this._viewFreqLo,
        hFrac: this._viewFreqHi - this._viewFreqLo
    };
};

/* ── Magnitude lookup ── */

HQSpectrogram.prototype.getMagnitudeAt = function(timeSec, freqHz) {
    if (!this._texWidth || !this._texHeight || !this._spectrogramData) return -96;
    var col = Math.floor((timeSec / this._duration) * this._texWidth);
    var row = Math.floor((freqHz / (this._sampleRate / 2)) * this._texHeight);
    col = Math.max(0, Math.min(this._texWidth - 1, col));
    row = Math.max(0, Math.min(this._texHeight - 1, row));
    return this._spectrogramData[row * this._texWidth + col];
};

/** Store reference to raw data for magnitude lookups */
HQSpectrogram.prototype.setSpectrogramData = function(data) {
    this._spectrogramData = data;
};

/* ── Draw ── */

HQSpectrogram.prototype.draw = function() {
    if (!this._ok || !this._texWidth) return;
    var gl = this._gl;
    gl.viewport(0, 0, this.canvas.width, this.canvas.height);
    gl.clearColor(0.03, 0.05, 0.1, 1.0);
    gl.clear(gl.COLOR_BUFFER_BIT);

    gl.useProgram(this._prog);

    /* Viewport uniform: maps UV to texture coordinates */
    var uViewport = gl.getUniformLocation(this._prog, 'u_viewport');
    var xNorm = this._duration > 0 ? this._viewX / this._duration : 0;
    var wNorm = this._duration > 0 ? this._viewW / this._duration : 1;
    gl.uniform4f(uViewport, xNorm, wNorm, this._viewFreqLo, this._viewFreqHi);

    /* Colormap uniforms */
    gl.uniform1i(gl.getUniformLocation(this._prog, 'u_colormap'), this._colormap);
    gl.uniform1f(gl.getUniformLocation(this._prog, 'u_gain'), this._gain);
    gl.uniform1f(gl.getUniformLocation(this._prog, 'u_floor'), this._floor);

    /* Texture */
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, this._tex);
    gl.uniform1i(gl.getUniformLocation(this._prog, 'u_spectrum'), 0);

    /* Draw quad */
    var aPos = gl.getAttribLocation(this._prog, 'a_pos');
    gl.bindBuffer(gl.ARRAY_BUFFER, this._vbo);
    gl.enableVertexAttribArray(aPos);
    gl.vertexAttribPointer(aPos, 2, gl.FLOAT, false, 0, 0);

    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
};

HQSpectrogram.prototype.destroy = function() {
    if (!this._gl) return;
    var gl = this._gl;
    if (this._tex) gl.deleteTexture(this._tex);
    if (this._vbo) gl.deleteBuffer(this._vbo);
    if (this._prog) gl.deleteProgram(this._prog);
    this._ok = false;
};

/* ── Export ── */
window.HQSpectrogram = HQSpectrogram;

})();
