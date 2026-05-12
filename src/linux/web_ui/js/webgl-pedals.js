/**
 * webgl-pedals.js -- WebGL rendering for pedalboard: metallic knobs, cable glow,
 * @version 2.0.1
 *                    EQ frequency response curve
 *
 * File:    src/linux/web_ui/js/webgl-pedals.js
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Purpose: GPU-accelerated pedalboard visuals: 3D metallic rotary knobs with
 *          specular highlights, glowing signal-flow cables, and a smooth EQ
 *          frequency response curve. Falls back to existing CSS/SVG when WebGL
 *          is unavailable or the user preference is off.
 *
 * Depends: webgl-viz.js (WebGLViz.isWebGLAvailable, WebGLViz.getWebGLPref)
 *          pedal-configs.js (createKnob fallback)
 *          pedalboard.js (Pedalboard._redrawCables, _cablePath, _getConnectorPos)
 *
 * Exports:
 *   WebGLKnob(container, opts)   -- single metallic rotary knob on its own canvas
 *   WebGLCableOverlay(board)     -- full-board cable glow canvas overlay
 *   WebGLEQCurve(canvas, bands)  -- EQ frequency response curve renderer
 *   webglPedalsAvailable()       -- true if WebGL + user pref both allow GPU rendering
 */

(function() {
'use strict';

/* ======================================================================
 * Availability check
 * ====================================================================== */

function webglPedalsAvailable() {
    if (!window.WebGLViz) return false;
    if (!WebGLViz.isWebGLAvailable()) return false;
    if (!WebGLViz.getWebGLPref()) return false;
    return true;
}

/* ======================================================================
 * Shared WebGL helpers (thin wrappers around WebGLViz)
 * ====================================================================== */

function _initGL(canvas, opts) {
    var o = opts || {};
    var gl = canvas.getContext('webgl2', { antialias: true, alpha: true, premultipliedAlpha: false });
    if (gl) return { gl: gl, version: 2 };
    gl = canvas.getContext('webgl', { antialias: true, alpha: true, premultipliedAlpha: false });
    if (gl) return { gl: gl, version: 1 };
    return null;
}

function _compileShader(gl, type, src) {
    var sh = gl.createShader(type);
    gl.shaderSource(sh, src);
    gl.compileShader(sh);
    if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) {
        console.error('webgl-pedals shader error:', gl.getShaderInfoLog(sh));
        gl.deleteShader(sh);
        return null;
    }
    return sh;
}

function _linkProgram(gl, vsSrc, fsSrc) {
    var vs = _compileShader(gl, gl.VERTEX_SHADER, vsSrc);
    var fs = _compileShader(gl, gl.FRAGMENT_SHADER, fsSrc);
    if (!vs || !fs) return null;
    var prog = gl.createProgram();
    gl.attachShader(prog, vs);
    gl.attachShader(prog, fs);
    gl.linkProgram(prog);
    if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
        console.error('webgl-pedals link error:', gl.getProgramInfoLog(prog));
        gl.deleteProgram(prog);
        return null;
    }
    return prog;
}

/* ======================================================================
 * WebGLKnob -- Metallic 3D rotary knob rendered on a small canvas
 * ======================================================================
 *
 * Each knob gets its own 48x48 (or configurable) canvas overlaid on
 * the config panel. Mouse drag rotates the knob and fires onChange.
 */

var KNOB_VS =
    'attribute vec2 a_pos;\n' +
    'varying vec2 v_uv;\n' +
    'void main() {\n' +
    '  v_uv = a_pos * 0.5 + 0.5;\n' +
    '  gl_Position = vec4(a_pos, 0.0, 1.0);\n' +
    '}\n';

var KNOB_FS =
    'precision mediump float;\n' +
    'varying vec2 v_uv;\n' +
    'uniform float u_angle;\n' +
    'uniform vec3 u_baseColor;\n' +
    'uniform float u_highlight;\n' +
    '\n' +
    'void main() {\n' +
    '  vec2 center = v_uv - 0.5;\n' +
    '  float dist = length(center);\n' +
    '  if (dist > 0.48) discard;\n' +
    '\n' +
    '  /* Metallic radial gradient */\n' +
    '  float light = 0.4 + 0.6 * pow(max(0.0, 1.0 - dist * 2.0), 1.5);\n' +
    '  vec3 metal = u_baseColor * light;\n' +
    '\n' +
    '  /* Specular highlight that shifts with u_highlight */\n' +
    '  vec2 lightDir = normalize(vec2(0.3 + u_highlight * 0.4, -0.5));\n' +
    '  float spec = pow(max(0.0, dot(normalize(center), lightDir)), 8.0);\n' +
    '  metal += vec3(spec * 0.6);\n' +
    '\n' +
    '  /* Edge ring (darker rim) */\n' +
    '  if (dist > 0.42) metal *= 0.5;\n' +
    '\n' +
    '  /* Knob cap raised center */\n' +
    '  if (dist < 0.12) metal += vec3(0.06);\n' +
    '\n' +
    '  /* Indicator line -- 270 degree sweep */\n' +
    '  float knobAngle = atan(center.y, center.x);\n' +
    '  float targetAngle = u_angle * 4.71239 - 2.35619;\n' +
    '  float angleDiff = knobAngle - targetAngle;\n' +
    '  /* Normalize to [-PI, PI] */\n' +
    '  angleDiff = angleDiff - 6.28318 * floor((angleDiff + 3.14159) / 6.28318);\n' +
    '  if (abs(angleDiff) < 0.07 && dist > 0.15 && dist < 0.40) {\n' +
    '    metal = vec3(0.08, 0.72, 0.66);\n' +
    '  }\n' +
    '\n' +
    '  /* Drop shadow ring at outer edge */\n' +
    '  float shadow = smoothstep(0.44, 0.48, dist);\n' +
    '  metal *= (1.0 - shadow * 0.4);\n' +
    '\n' +
    '  /* Antialiased circle edge */\n' +
    '  float alpha = 1.0 - smoothstep(0.46, 0.48, dist);\n' +
    '  gl_FragColor = vec4(metal, alpha);\n' +
    '}\n';

function WebGLKnob(container, opts) {
    this.container = container;
    this.size = opts.size || 48;
    this.min = opts.min !== undefined ? opts.min : 0;
    this.max = opts.max !== undefined ? opts.max : 1;
    this.step = opts.step || 0.01;
    this.value = opts.value !== undefined ? opts.value : 0.5;
    this.unit = opts.unit || '';
    this.label = opts.label || '';
    this.onChange = opts.onChange || null;
    this.baseColor = opts.baseColor || [0.28, 0.33, 0.42]; // dark gunmetal

    this._gl = null;
    this._prog = null;
    this._vbo = null;
    this._ok = false;
    this._dragging = false;
    this._startY = 0;
    this._startVal = 0;

    this.el = null;      // wrapper element
    this.canvas = null;
    this.valEl = null;

    this._build();
}

WebGLKnob.prototype._build = function() {
    var self = this;
    var range = this.max - this.min;

    // Wrapper
    this.el = document.createElement('div');
    this.el.className = 'pc-knob-wrap';

    // Label
    var labelEl = document.createElement('div');
    labelEl.className = 'pc-knob-label';
    labelEl.textContent = this.label;
    this.el.appendChild(labelEl);

    // Canvas container (same visual footprint as CSS knob)
    var knobBox = document.createElement('div');
    knobBox.className = 'pc-knob pc-knob-webgl';
    knobBox.style.width = this.size + 'px';
    knobBox.style.height = this.size + 'px';
    knobBox.style.position = 'relative';
    knobBox.style.cursor = 'grab';

    this.canvas = document.createElement('canvas');
    this.canvas.width = this.size * 2;   // 2x for retina
    this.canvas.height = this.size * 2;
    this.canvas.style.width = this.size + 'px';
    this.canvas.style.height = this.size + 'px';
    this.canvas.style.display = 'block';
    this.canvas.style.borderRadius = '50%';
    knobBox.appendChild(this.canvas);
    this.el.appendChild(knobBox);

    // Value readout
    this.valEl = document.createElement('div');
    this.valEl.className = 'pc-knob-value';
    this._updateValText();
    this.el.appendChild(this.valEl);

    // Init WebGL
    this._initGL();

    // Drag handlers
    var onMove = function(e) {
        if (!self._dragging) return;
        e.preventDefault();
        var clientY = e.touches ? e.touches[0].clientY : e.clientY;
        var dy = self._startY - clientY;
        var delta = (dy / 150) * range;
        var newVal = Math.min(self.max, Math.max(self.min, self._startVal + delta));
        newVal = Math.round(newVal / self.step) * self.step;
        self.value = newVal;
        self._updateValText();
        self.draw();
        if (self.onChange) self.onChange(newVal);
    };

    var onUp = function() {
        self._dragging = false;
        knobBox.style.cursor = 'grab';
        document.removeEventListener('mousemove', onMove);
        document.removeEventListener('mouseup', onUp);
        document.removeEventListener('touchmove', onMove);
        document.removeEventListener('touchend', onUp);
    };

    var onDown = function(e) {
        self._dragging = true;
        knobBox.style.cursor = 'grabbing';
        self._startY = e.touches ? e.touches[0].clientY : e.clientY;
        self._startVal = self.value;
        document.addEventListener('mousemove', onMove);
        document.addEventListener('mouseup', onUp);
        document.addEventListener('touchmove', onMove, { passive: false });
        document.addEventListener('touchend', onUp);
        e.preventDefault();
    };

    knobBox.addEventListener('mousedown', onDown);
    knobBox.addEventListener('touchstart', onDown, { passive: false });

    this.container.appendChild(this.el);

    // Initial draw
    this.draw();
};

WebGLKnob.prototype._updateValText = function() {
    if (this.valEl) {
        this.valEl.textContent = parseFloat(this.value).toFixed(1) +
            (this.unit ? ' ' + this.unit : '');
    }
};

WebGLKnob.prototype._initGL = function() {
    var result = _initGL(this.canvas);
    if (!result) return;
    var gl = this._gl = result.gl;

    this._prog = _linkProgram(gl, KNOB_VS, KNOB_FS);
    if (!this._prog) return;

    // Fullscreen quad
    var verts = new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]);
    this._vbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this._vbo);
    gl.bufferData(gl.ARRAY_BUFFER, verts, gl.STATIC_DRAW);

    this._ok = true;
};

WebGLKnob.prototype.draw = function() {
    if (!this._ok) return;
    var gl = this._gl;
    var w = this.canvas.width, h = this.canvas.height;

    gl.viewport(0, 0, w, h);
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT);
    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

    gl.useProgram(this._prog);

    var aPos = gl.getAttribLocation(this._prog, 'a_pos');
    gl.bindBuffer(gl.ARRAY_BUFFER, this._vbo);
    gl.enableVertexAttribArray(aPos);
    gl.vertexAttribPointer(aPos, 2, gl.FLOAT, false, 0, 0);

    // Normalized angle 0-1
    var range = this.max - this.min;
    var pct = range > 0 ? (this.value - this.min) / range : 0;

    gl.uniform1f(gl.getUniformLocation(this._prog, 'u_angle'), pct);
    gl.uniform3fv(gl.getUniformLocation(this._prog, 'u_baseColor'),
        new Float32Array(this.baseColor));
    gl.uniform1f(gl.getUniformLocation(this._prog, 'u_highlight'), pct * 0.5);

    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
};

WebGLKnob.prototype.setValue = function(v) {
    this.value = Math.min(this.max, Math.max(this.min, v));
    this._updateValText();
    this.draw();
};

WebGLKnob.prototype.destroy = function() {
    if (!this._gl) return;
    var gl = this._gl;
    if (this._vbo) gl.deleteBuffer(this._vbo);
    if (this._prog) gl.deleteProgram(this._prog);
    this._ok = false;
    if (this.el && this.el.parentNode) this.el.parentNode.removeChild(this.el);
};


/* ======================================================================
 * WebGLCableOverlay -- Glowing animated cables for the entire pedalboard
 * ======================================================================
 *
 * A single WebGL canvas overlays the pedalboard surface, replacing the
 * SVG cable overlay. Cables are rendered as thick GL_TRIANGLE_STRIP
 * segments with bloom glow and animated dash pattern.
 */

var CABLE_VS =
    'attribute vec2 a_pos;\n' +
    'attribute float a_side;\n' +       // -1 or +1 (perpendicular offset)
    'attribute float a_along;\n' +      // 0..1 parameter along the cable
    'uniform vec2 u_resolution;\n' +
    'uniform float u_width;\n' +
    'varying float v_side;\n' +
    'varying float v_along;\n' +
    'void main() {\n' +
    '  v_side = a_side;\n' +
    '  v_along = a_along;\n' +
    '  vec2 ndc = (a_pos / u_resolution) * 2.0 - 1.0;\n' +
    '  ndc.y = -ndc.y;\n' +
    '  gl_Position = vec4(ndc, 0.0, 1.0);\n' +
    '}\n';

var CABLE_FS =
    'precision mediump float;\n' +
    'varying float v_side;\n' +
    'varying float v_along;\n' +
    'uniform vec3 u_color;\n' +
    'uniform float u_time;\n' +
    'uniform float u_brightness;\n' +
    'uniform float u_glow;\n' +     // 0=core, 1=glow pass
    '\n' +
    'void main() {\n' +
    '  float edge = abs(v_side);\n' +
    '\n' +
    '  if (u_glow > 0.5) {\n' +
    '    /* Glow pass: wide, soft, low alpha */\n' +
    '    float g = 1.0 - smoothstep(0.0, 1.0, edge);\n' +
    '    gl_FragColor = vec4(u_color * u_brightness, g * 0.25 * u_brightness);\n' +
    '  } else {\n' +
    '    /* Core pass: solid cable with dash animation */\n' +
    '    float core = 1.0 - smoothstep(0.6, 1.0, edge);\n' +
    '    /* Animated dash pattern */\n' +
    '    float dash = smoothstep(0.3, 0.5,\n' +
    '        fract(v_along * 8.0 - u_time * 1.5));\n' +
    '    float alpha = core * mix(0.6, 1.0, dash) * u_brightness;\n' +
    '    gl_FragColor = vec4(u_color * u_brightness, alpha);\n' +
    '  }\n' +
    '}\n';

function WebGLCableOverlay(board) {
    this.board = board;
    this.canvas = null;
    this._gl = null;
    this._prog = null;
    this._vbo = null;
    this._ok = false;
    this._animId = null;
    this._time = 0;
    this._meterBrightness = {}; // pedalId -> 0..1
    this._lastFrameTime = 0;
    this._cables = [];   // snapshot of board cables for rendering

    this._init();
}

WebGLCableOverlay.prototype._init = function() {
    if (!this.board || !this.board.container) return;

    // Create overlay canvas
    this.canvas = document.createElement('canvas');
    this.canvas.style.cssText = 'position:absolute;top:0;left:0;width:100%;height:100%;z-index:2;pointer-events:none;';

    // Insert after the SVG overlay (z-index 1 for SVG, 2 for WebGL)
    // Hide the SVG overlay since we replace it
    if (this.board.svgOverlay) {
        this.board.svgOverlay.style.display = 'none';
    }
    this.board.container.appendChild(this.canvas);

    this._resize();

    var result = _initGL(this.canvas);
    if (!result) {
        // Fallback: show SVG overlay again
        if (this.board.svgOverlay) this.board.svgOverlay.style.display = '';
        return;
    }
    var gl = this._gl = result.gl;

    this._prog = _linkProgram(gl, CABLE_VS, CABLE_FS);
    if (!this._prog) {
        if (this.board.svgOverlay) this.board.svgOverlay.style.display = '';
        return;
    }

    this._vbo = gl.createBuffer();
    this._ok = true;

    // Start animation loop
    var self = this;
    this._lastFrameTime = performance.now();
    var tick = function(now) {
        self._time += (now - self._lastFrameTime) / 1000.0;
        self._lastFrameTime = now;
        self._draw();
        self._animId = requestAnimationFrame(tick);
    };
    this._animId = requestAnimationFrame(tick);

    // Resize observer
    if (typeof ResizeObserver !== 'undefined') {
        this._ro = new ResizeObserver(function() { self._resize(); });
        this._ro.observe(this.board.container);
    }
};

WebGLCableOverlay.prototype._resize = function() {
    if (!this.canvas || !this.board.container) return;
    var w = this.board.container.clientWidth || 1200;
    var h = this.board.container.clientHeight || 800;
    var dpr = window.devicePixelRatio || 1;
    this.canvas.width = w * dpr;
    this.canvas.height = h * dpr;
    // CSS size is handled by style 100%
};

/* Build GL vertex data for a single bezier cable */
WebGLCableOverlay.prototype._buildCableVerts = function(x1, y1, x2, y2, halfWidth) {
    var segments = 32;
    var verts = []; // each vertex: x, y, side(-1/+1), along(0..1)

    // Evaluate bezier (same as Pedalboard._cablePath logic)
    var dx = Math.abs(x2 - x1);
    var cpOffset = Math.max(60, dx * 0.4);
    var sag = Math.max(30, Math.min(80, dx * 0.15));
    var cp1x = x1 + cpOffset;
    var cp1y = y1 + sag;
    var cp2x = x2 - cpOffset;
    var cp2y = y2 + sag;

    var points = [];
    for (var i = 0; i <= segments; i++) {
        var t = i / segments;
        var mt = 1 - t;
        var px = mt*mt*mt*x1 + 3*mt*mt*t*cp1x + 3*mt*t*t*cp2x + t*t*t*x2;
        var py = mt*mt*mt*y1 + 3*mt*mt*t*cp1y + 3*mt*t*t*cp2y + t*t*t*y2;
        points.push({ x: px, y: py });
    }

    // Generate triangle strip with perpendicular offsets
    for (var j = 0; j <= segments; j++) {
        var cur = points[j];
        var prev = points[Math.max(0, j - 1)];
        var next = points[Math.min(segments, j + 1)];
        // Tangent direction
        var tx = next.x - prev.x;
        var ty = next.y - prev.y;
        var len = Math.sqrt(tx * tx + ty * ty) || 1;
        // Perpendicular (normal)
        var nx = -ty / len;
        var ny = tx / len;
        var along = j / segments;

        // Two vertices per segment point: left (-1) and right (+1)
        verts.push(cur.x + nx * halfWidth, cur.y + ny * halfWidth, -1.0, along);
        verts.push(cur.x - nx * halfWidth, cur.y - ny * halfWidth, 1.0, along);
    }

    return new Float32Array(verts);
};

/* Parse CSS hex color to [r,g,b] 0-1 */
WebGLCableOverlay.prototype._parseColor = function(hex) {
    if (!hex || hex.charAt(0) !== '#') return [0.08, 0.72, 0.66]; // teal default
    var r = parseInt(hex.substring(1, 3), 16) / 255;
    var g = parseInt(hex.substring(3, 5), 16) / 255;
    var b = parseInt(hex.substring(5, 7), 16) / 255;
    return [r, g, b];
};

WebGLCableOverlay.prototype._draw = function() {
    if (!this._ok) return;
    var gl = this._gl;
    var dpr = window.devicePixelRatio || 1;
    var w = this.canvas.width;
    var h = this.canvas.height;

    gl.viewport(0, 0, w, h);
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT);
    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

    gl.useProgram(this._prog);

    var uRes = gl.getUniformLocation(this._prog, 'u_resolution');
    gl.uniform2f(uRes, w / dpr, h / dpr);
    var uTime = gl.getUniformLocation(this._prog, 'u_time');
    gl.uniform1f(uTime, this._time);

    var uColor = gl.getUniformLocation(this._prog, 'u_color');
    var uBright = gl.getUniformLocation(this._prog, 'u_brightness');
    var uGlow = gl.getUniformLocation(this._prog, 'u_glow');
    var uWidth = gl.getUniformLocation(this._prog, 'u_width');

    var aPos = gl.getAttribLocation(this._prog, 'a_pos');
    var aSide = gl.getAttribLocation(this._prog, 'a_side');
    var aAlong = gl.getAttribLocation(this._prog, 'a_along');

    var cables = this.board.cables;
    if (!cables || cables.length === 0) return;

    for (var i = 0; i < cables.length; i++) {
        var cable = cables[i];
        var pos1 = this.board._getConnectorPos(cable.from, 'out');
        var pos2 = this.board._getConnectorPos(cable.to, 'in');

        var color = this._parseColor(cable.color);
        var brightness = 0.8 + (this._meterBrightness[cable.from] || 0) * 0.5;
        brightness = Math.min(brightness, 1.5);

        // -- Glow pass (wider, softer) --
        var glowVerts = this._buildCableVerts(pos1.x, pos1.y, pos2.x, pos2.y, 9);
        gl.bindBuffer(gl.ARRAY_BUFFER, this._vbo);
        gl.bufferData(gl.ARRAY_BUFFER, glowVerts, gl.DYNAMIC_DRAW);

        gl.enableVertexAttribArray(aPos);
        gl.enableVertexAttribArray(aSide);
        gl.enableVertexAttribArray(aAlong);
        gl.vertexAttribPointer(aPos, 2, gl.FLOAT, false, 16, 0);
        gl.vertexAttribPointer(aSide, 1, gl.FLOAT, false, 16, 8);
        gl.vertexAttribPointer(aAlong, 1, gl.FLOAT, false, 16, 12);

        gl.uniform3fv(uColor, new Float32Array(color));
        gl.uniform1f(uBright, brightness);
        gl.uniform1f(uGlow, 1.0);

        gl.drawArrays(gl.TRIANGLE_STRIP, 0, glowVerts.length / 4);

        // -- Core pass (narrower, solid) --
        var coreVerts = this._buildCableVerts(pos1.x, pos1.y, pos2.x, pos2.y, 2.5);
        gl.bufferData(gl.ARRAY_BUFFER, coreVerts, gl.DYNAMIC_DRAW);
        gl.vertexAttribPointer(aPos, 2, gl.FLOAT, false, 16, 0);
        gl.vertexAttribPointer(aSide, 1, gl.FLOAT, false, 16, 8);
        gl.vertexAttribPointer(aAlong, 1, gl.FLOAT, false, 16, 12);

        gl.uniform1f(uGlow, 0.0);

        gl.drawArrays(gl.TRIANGLE_STRIP, 0, coreVerts.length / 4);
    }
};

/* Update signal brightness for a pedal (drives cable glow pulse) */
WebGLCableOverlay.prototype.setMeterLevel = function(pedalId, level) {
    this._meterBrightness[pedalId] = Math.max(0, Math.min(1, level));
};

/* Trigger full cable redraw (called after pedal move, cable add/remove) */
WebGLCableOverlay.prototype.refresh = function() {
    // Next animation frame will pick up the new cable positions automatically
};

WebGLCableOverlay.prototype.destroy = function() {
    if (this._animId) cancelAnimationFrame(this._animId);
    if (this._ro) this._ro.disconnect();
    if (!this._gl) return;
    var gl = this._gl;
    if (this._vbo) gl.deleteBuffer(this._vbo);
    if (this._prog) gl.deleteProgram(this._prog);
    this._ok = false;
    if (this.canvas && this.canvas.parentNode) {
        this.canvas.parentNode.removeChild(this.canvas);
    }
    // Restore SVG overlay
    if (this.board && this.board.svgOverlay) {
        this.board.svgOverlay.style.display = '';
    }
};


/* ======================================================================
 * WebGLEQCurve -- Smooth EQ frequency response curve
 * ======================================================================
 *
 * Renders inside a small canvas (e.g. 280x60) within the EQ pedal
 * faceplate. 10 control points connected by smooth interpolation,
 * with a gradient fill below the curve.
 */

var EQCURVE_VS =
    'attribute vec2 a_pos;\n' +
    'varying vec2 v_uv;\n' +
    'void main() {\n' +
    '  v_uv = a_pos * 0.5 + 0.5;\n' +
    '  gl_Position = vec4(a_pos, 0.0, 1.0);\n' +
    '}\n';

var EQCURVE_FS =
    'precision mediump float;\n' +
    'varying vec2 v_uv;\n' +
    'uniform float u_gains[10];\n' +
    '\n' +
    '/* Hermite spline interpolation across 10 bands */\n' +
    'float eqCurve(float x) {\n' +
    '  float fx = x * 9.0;\n' +
    '  int idx = int(floor(fx));\n' +
    '  float t = fract(fx);\n' +
    '  if (idx >= 9) return u_gains[9];\n' +
    '  if (idx < 0) return u_gains[0];\n' +
    '  /* Catmull-Rom style: use neighbors for smooth curve */\n' +
    '  float p0 = idx > 0 ? u_gains[idx - 1] : u_gains[0];\n' +
    '  float p1 = u_gains[idx];\n' +
    '  float p2 = u_gains[idx + 1];\n' +
    '  float p3 = idx < 8 ? u_gains[idx + 2] : u_gains[9];\n' +
    '  /* Catmull-Rom */\n' +
    '  float t2 = t * t;\n' +
    '  float t3 = t2 * t;\n' +
    '  return 0.5 * ((2.0 * p1) +\n' +
    '    (-p0 + p2) * t +\n' +
    '    (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +\n' +
    '    (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);\n' +
    '}\n' +
    '\n' +
    'void main() {\n' +
    '  float curveY = eqCurve(v_uv.x);\n' +
    '  /* Map gain (-12..+12 dB) to 0..1 screen space */\n' +
    '  float curveNorm = (curveY + 12.0) / 24.0;\n' +
    '  float pixelY = v_uv.y;\n' +
    '\n' +
    '  /* Curve line (anti-aliased) */\n' +
    '  float lineDist = abs(pixelY - curveNorm);\n' +
    '  float line = 1.0 - smoothstep(0.0, 0.025, lineDist);\n' +
    '\n' +
    '  /* Fill below curve */\n' +
    '  float fill = 0.0;\n' +
    '  if (pixelY < curveNorm) {\n' +
    '    fill = 0.15 * (1.0 - (curveNorm - pixelY) * 2.0);\n' +
    '    fill = max(fill, 0.0);\n' +
    '  }\n' +
    '\n' +
    '  vec3 teal = vec3(0.08, 0.72, 0.66);\n' +
    '  float alpha = line * 0.95 + fill;\n' +
    '  gl_FragColor = vec4(teal, alpha);\n' +
    '}\n';

function WebGLEQCurve(canvas) {
    this.canvas = canvas;
    this._gl = null;
    this._prog = null;
    this._vbo = null;
    this._ok = false;
    this._gains = new Float32Array(10); // -12..+12 dB per band

    this._init();
}

WebGLEQCurve.prototype._init = function() {
    var result = _initGL(this.canvas);
    if (!result) return;
    var gl = this._gl = result.gl;

    this._prog = _linkProgram(gl, EQCURVE_VS, EQCURVE_FS);
    if (!this._prog) return;

    var verts = new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]);
    this._vbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this._vbo);
    gl.bufferData(gl.ARRAY_BUFFER, verts, gl.STATIC_DRAW);

    this._ok = true;
};

WebGLEQCurve.prototype.setGains = function(gains) {
    if (!gains) return;
    for (var i = 0; i < 10; i++) {
        this._gains[i] = (gains[i] !== undefined) ? gains[i] : 0;
    }
    this.draw();
};

WebGLEQCurve.prototype.draw = function() {
    if (!this._ok) return;
    var gl = this._gl;

    gl.viewport(0, 0, this.canvas.width, this.canvas.height);
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT);
    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

    gl.useProgram(this._prog);

    var aPos = gl.getAttribLocation(this._prog, 'a_pos');
    gl.bindBuffer(gl.ARRAY_BUFFER, this._vbo);
    gl.enableVertexAttribArray(aPos);
    gl.vertexAttribPointer(aPos, 2, gl.FLOAT, false, 0, 0);

    // Upload gains as uniform array
    var uGains = gl.getUniformLocation(this._prog, 'u_gains[0]');
    gl.uniform1fv(uGains, this._gains);

    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
};

WebGLEQCurve.prototype.destroy = function() {
    if (!this._gl) return;
    var gl = this._gl;
    if (this._vbo) gl.deleteBuffer(this._vbo);
    if (this._prog) gl.deleteProgram(this._prog);
    this._ok = false;
};


/* ======================================================================
 * Integration helpers
 * ====================================================================== */

/**
 * createWebGLKnob() -- drop-in replacement for createKnob() in pedal-configs.js
 * Returns a wrapper element with the same .pc-knob-wrap class.
 */
function createWebGLKnob(id, label, value, min, max, step, unit, onChange) {
    var container = document.createElement('div');
    var knob = new WebGLKnob(container, {
        size: 48,
        label: label,
        value: value,
        min: min,
        max: max,
        step: step,
        unit: unit,
        onChange: onChange
    });
    container._webglKnob = knob;
    return container;
}

/**
 * hookPedalboardCables(board) -- replace SVG cables with WebGL overlay
 * Call after pedalboard init, when the board object is ready.
 */
function hookPedalboardCables(board) {
    if (!webglPedalsAvailable()) return null;
    var overlay = new WebGLCableOverlay(board);
    if (!overlay._ok) return null;

    // Monkey-patch _redrawCables to refresh the WebGL overlay instead
    var origRedraw = board._redrawCables.bind(board);
    board._redrawCables = function() {
        // Still update SVG positions (for hit testing / midpoint circles)
        origRedraw();
        overlay.refresh();
    };

    // Start meter polling for cable brightness
    var meterInterval = setInterval(function() {
        mc1Api('GET', '/api/v1/effects/meters').then(function(d) {
            if (!d || !d.ok || !d.levels) return;
            for (var pid in d.levels) {
                overlay.setMeterLevel(pid, d.levels[pid]);
            }
        }).catch(function() {});
    }, 200);

    overlay._meterInterval = meterInterval;

    return overlay;
}

/**
 * createEQCurveOverlay(pedalEl, gains) -- add WebGL EQ curve canvas to an EQ pedal
 */
function createEQCurveOverlay(pedalEl, gains) {
    if (!webglPedalsAvailable()) return null;

    var canvas = document.createElement('canvas');
    canvas.width = 560;   // 2x retina for 280px
    canvas.height = 120;  // 2x retina for 60px
    canvas.style.cssText = 'position:absolute;bottom:4px;left:50%;transform:translateX(-50%);' +
        'width:260px;height:56px;pointer-events:none;z-index:15;border-radius:4px;';

    pedalEl.style.position = 'relative';
    pedalEl.appendChild(canvas);

    var curve = new WebGLEQCurve(canvas);
    if (!curve._ok) {
        pedalEl.removeChild(canvas);
        return null;
    }
    curve.setGains(gains || [0,0,0,0,0,0,0,0,0,0]);
    return curve;
}


/* ======================================================================
 * Exports
 * ====================================================================== */

window.WebGLPedals = {
    available: webglPedalsAvailable,
    Knob: WebGLKnob,
    CableOverlay: WebGLCableOverlay,
    EQCurve: WebGLEQCurve,
    createKnob: createWebGLKnob,
    hookCables: hookPedalboardCables,
    createEQCurve: createEQCurveOverlay
};

})();
