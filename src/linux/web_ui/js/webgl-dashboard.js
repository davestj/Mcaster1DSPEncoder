/**
 * Mcaster1 WebGL Dashboard Components
 * @version 2.0.1
 * js/webgl-dashboard.js
 *
 * GPU-accelerated dashboard visualizations:
 * - WebGLGlobe: 3D Earth with listener pins (GeoIP country codes)
 * - WebGLBandwidthChart: 3D area chart for live bandwidth
 * - WebGLEncoderRack: 3D server rack visualization
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

(function() {
'use strict';

/* ======================================================================
 * Utility: WebGL helpers (shared)
 * ====================================================================== */

function isWebGLAvailable() {
    try {
        var c = document.createElement('canvas');
        return !!(c.getContext('webgl2') || c.getContext('webgl'));
    } catch (e) { return false; }
}

function getWebGLPref() {
    try { return localStorage.getItem('mc1_webgl_viz') === 'true'; }
    catch (e) { return false; }
}

function initGL(canvas, opts) {
    opts = opts || {};
    var attr = { antialias: true, alpha: !!opts.alpha, premultipliedAlpha: false };
    var gl = canvas.getContext('webgl2', attr);
    if (gl) return { gl: gl, version: 2 };
    gl = canvas.getContext('webgl', attr);
    if (gl) return { gl: gl, version: 1 };
    return null;
}

function compileShader(gl, type, src) {
    var s = gl.createShader(type);
    gl.shaderSource(s, src);
    gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) {
        console.error('WebGL shader:', gl.getShaderInfoLog(s));
        gl.deleteShader(s);
        return null;
    }
    return s;
}

function linkProgram(gl, vsSrc, fsSrc) {
    var vs = compileShader(gl, gl.VERTEX_SHADER, vsSrc);
    var fs = compileShader(gl, gl.FRAGMENT_SHADER, fsSrc);
    if (!vs || !fs) return null;
    var p = gl.createProgram();
    gl.attachShader(p, vs);
    gl.attachShader(p, fs);
    gl.linkProgram(p);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS)) {
        console.error('WebGL link:', gl.getProgramInfoLog(p));
        gl.deleteProgram(p);
        return null;
    }
    return p;
}

/* ======================================================================
 * Math helpers
 * ====================================================================== */

function mat4Identity() {
    return [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1];
}

function mat4Perspective(fov, aspect, near, far) {
    var f = 1.0 / Math.tan(fov / 2);
    var nf = 1.0 / (near - far);
    return [
        f/aspect, 0, 0, 0,
        0, f, 0, 0,
        0, 0, (far+near)*nf, -1,
        0, 0, 2*far*near*nf, 0
    ];
}

function mat4LookAt(eye, center, up) {
    var zx=eye[0]-center[0], zy=eye[1]-center[1], zz=eye[2]-center[2];
    var zl=Math.sqrt(zx*zx+zy*zy+zz*zz); zx/=zl; zy/=zl; zz/=zl;
    var xx=up[1]*zz-up[2]*zy, xy=up[2]*zx-up[0]*zz, xz=up[0]*zy-up[1]*zx;
    var xl=Math.sqrt(xx*xx+xy*xy+xz*xz); xx/=xl; xy/=xl; xz/=xl;
    var yx=zy*xz-zz*xy, yy=zz*xx-zx*xz, yz=zx*xy-zy*xx;
    return [
        xx, yx, zx, 0,
        xy, yy, zy, 0,
        xz, yz, zz, 0,
        -(xx*eye[0]+xy*eye[1]+xz*eye[2]),
        -(yx*eye[0]+yy*eye[1]+yz*eye[2]),
        -(zx*eye[0]+zy*eye[1]+zz*eye[2]),
        1
    ];
}

function mat4Multiply(a, b) {
    var o = new Array(16);
    for (var i = 0; i < 4; i++)
        for (var j = 0; j < 4; j++) {
            o[i*4+j] = 0;
            for (var k = 0; k < 4; k++) o[i*4+j] += a[i*4+k] * b[k*4+j];
        }
    return o;
}

function mat4RotateY(m, rad) {
    var c = Math.cos(rad), s = Math.sin(rad);
    var r = [c,0,s,0, 0,1,0,0, -s,0,c,0, 0,0,0,1];
    return mat4Multiply(m, r);
}

function mat4RotateX(m, rad) {
    var c = Math.cos(rad), s = Math.sin(rad);
    var r = [1,0,0,0, 0,c,-s,0, 0,s,c,0, 0,0,0,1];
    return mat4Multiply(m, r);
}

function latLonToXYZ(lat, lon, radius) {
    var phi = (90 - lat) * Math.PI / 180;
    var theta = (lon + 180) * Math.PI / 180;
    return [
        radius * Math.sin(phi) * Math.cos(theta),
        radius * Math.cos(phi),
        radius * Math.sin(phi) * Math.sin(theta)
    ];
}

/* ======================================================================
 * Continent outline data (simplified lat/lon polylines, ~200 points)
 * ====================================================================== */

var CONTINENTS = [
    // North America
    [[60,-140],[65,-168],[72,-157],[71,-138],[60,-140]],
    [[60,-140],[55,-130],[48,-125],[32,-117],[25,-110],[18,-105],[15,-87],[18,-80],[25,-80],[30,-82],
     [25,-90],[30,-88],[35,-75],[40,-74],[42,-70],[45,-67],[47,-60],[50,-56],[52,-56],[55,-60],
     [58,-64],[60,-65],[64,-78],[64,-92],[60,-95],[55,-85],[50,-90],[50,-95],[55,-100],[60,-110],
     [64,-120],[60,-140]],
    // South America
    [[12,-72],[10,-67],[7,-60],[5,-52],[0,-50],[-5,-35],[-10,-37],[-15,-39],[-23,-42],[-28,-49],
     [-33,-53],[-35,-57],[-40,-63],[-42,-65],[-46,-67],[-52,-69],[-55,-68],[-55,-65],[-52,-70],
     [-46,-75],[-40,-73],[-33,-72],[-20,-70],[-15,-76],[-5,-80],[0,-78],[5,-77],[8,-72],[12,-72]],
    // Europe
    [[36,-10],[38,-8],[40,-4],[43,0],[44,3],[43,7],[44,12],[40,14],[38,15],[38,24],[40,26],
     [41,29],[43,28],[45,30],[47,35],[50,40],[55,40],[60,30],[65,25],[70,20],[71,25],[70,30],
     [65,28],[60,25],[55,20],[50,15],[52,7],[54,10],[56,12],[58,12],[60,5],[63,5],[65,12],
     [70,20],[70,30],[68,38],[65,40],[60,42],[56,38],[55,28],[53,20],[52,5],[50,2],[48,-5],[44,-8],
     [40,-8],[36,-10]],
    // Africa
    [[35,-5],[37,10],[33,12],[32,25],[30,33],[22,37],[15,43],[12,44],[10,42],[5,42],[0,42],
     [-5,40],[-10,40],[-15,35],[-20,35],[-25,35],[-30,32],[-34,26],[-34,18],[-30,16],[-25,15],
     [-17,12],[-12,14],[-5,12],[0,10],[5,2],[5,-5],[7,-5],[5,0],[6,2],[4,10],[0,10],[5,0],
     [5,-5],[10,-15],[15,-17],[20,-17],[25,-15],[30,-10],[35,-5]],
    // Asia
    [[42,30],[45,40],[40,50],[38,58],[30,60],[25,56],[20,58],[15,55],[10,55],[5,52],[0,50],
     [-6,50],[-8,48],[-8,42],[-6,35],[1,30],[5,25],[8,20],[5,10],[10,0],[15,5],[20,10],[15,15],
     [22,35],[28,35],[30,48],[35,52],[35,60],[30,70],[25,68],[22,72],[23,80],[20,88],[22,90],
     [26,90],[28,97],[22,100],[18,106],[15,110],[10,107],[5,105],[0,100],[-5,105],[-8,110],
     [-8,115],[-5,120],[0,120],[5,118],[10,115],[20,110],[25,120],[30,122],[35,130],[38,135],
     [40,132],[35,129],[42,130],[45,135],[48,140],[50,140],[53,143],[55,137],[58,140],[60,150],
     [63,160],[65,170],[68,180],[70,175],[68,160],[65,150],[60,142],[58,130],[55,120],[58,110],
     [55,90],[53,85],[55,70],[50,55],[48,45],[42,43],[42,30]],
    // Australia
    [[-12,130],[-12,136],[-14,136],[-17,140],[-20,149],[-25,153],[-28,153],[-33,152],[-35,150],
     [-38,145],[-38,140],[-35,137],[-32,133],[-32,127],[-25,114],[-22,114],[-20,119],[-15,125],
     [-12,130]],
    // Antarctica (simple)
    [[-65,-60],[-70,-60],[-75,-30],[-72,0],[-75,30],[-72,60],[-70,80],[-68,110],[-70,140],
     [-72,170],[-75,-170],[-72,-140],[-70,-110],[-68,-90],[-65,-60]]
];

/* ======================================================================
 * Country code -> approximate center lat/lon (top 50 countries)
 * ====================================================================== */

var COUNTRY_CENTERS = {
    US:[39,-98],CA:[56,-106],MX:[23,-102],BR:[-14,-51],AR:[-34,-64],CL:[-30,-71],CO:[4,-72],
    PE:[-10,-76],VE:[8,-66],GB:[54,-2],DE:[51,10],FR:[47,2],ES:[40,-4],IT:[43,12],PT:[39,-8],
    NL:[52,5],BE:[51,4],SE:[62,15],NO:[62,10],FI:[64,26],DK:[56,10],CH:[47,8],AT:[48,14],
    PL:[52,20],CZ:[50,15],RO:[46,25],UA:[49,32],RU:[60,100],TR:[39,35],IL:[31,35],SA:[24,45],
    AE:[24,54],IN:[21,78],CN:[35,105],JP:[36,138],KR:[36,128],TW:[23,121],TH:[15,100],
    VN:[16,106],PH:[13,122],ID:[-2,118],MY:[4,110],SG:[1,104],AU:[-25,133],NZ:[-41,174],
    ZA:[-30,25],NG:[10,8],KE:[-1,38],EG:[27,30],GH:[8,-2],MA:[32,-5]
};

/* ======================================================================
 * WebGLGlobe — 3D Earth sphere with listener pins
 * ====================================================================== */

function WebGLGlobe(container, opts) {
    opts = opts || {};
    this._container = container;
    this._canvas = null;
    this._gl = null;
    this._ok = false;
    this._rotY = 0;
    this._rotX = 0.3;
    this._autoRotate = true;
    this._dragging = false;
    this._lastMouse = null;
    this._pins = [];         // { lat, lon, count, age, xyz }
    this._overlay = null;    // 2D overlay div for labels
    this._sphereVBO = null;
    this._sphereIBO = null;
    this._sphereCount = 0;
    this._lineVBO = null;
    this._lineCount = 0;
    this._pinVBO = null;
    this._progSphere = null;
    this._progLine = null;
    this._progPin = null;
    this._animFrame = null;
    this._lastTime = 0;
    this._radius = opts.radius || 1.0;
    this._init();
}

WebGLGlobe.prototype._init = function() {
    // Create canvas
    this._canvas = document.createElement('canvas');
    this._canvas.style.width = '100%';
    this._canvas.style.height = '100%';
    this._canvas.style.display = 'block';
    this._container.appendChild(this._canvas);

    // Overlay for labels
    this._overlay = document.createElement('div');
    this._overlay.style.cssText = 'position:absolute;top:0;left:0;width:100%;height:100%;pointer-events:none;z-index:1';
    if (this._container.style.position !== 'absolute' && this._container.style.position !== 'relative')
        this._container.style.position = 'relative';
    this._container.appendChild(this._overlay);

    var res = initGL(this._canvas, { alpha: true });
    if (!res) return;
    this._gl = res.gl;

    this._buildShaders();
    this._buildSphere(32, 24);
    this._buildContinentLines();
    this._setupInteraction();
    this._ok = true;
    this._resize();
    this._startLoop();
};

WebGLGlobe.prototype._buildShaders = function() {
    var gl = this._gl;

    // Sphere shader (wireframe grid)
    this._progSphere = linkProgram(gl,
        'attribute vec3 a_pos;\n'
        + 'uniform mat4 u_mvp;\n'
        + 'void main() { gl_Position = u_mvp * vec4(a_pos, 1.0); }\n',
        'precision mediump float;\n'
        + 'uniform vec4 u_color;\n'
        + 'void main() { gl_FragColor = u_color; }\n'
    );

    // Line shader (continent outlines)
    this._progLine = linkProgram(gl,
        'attribute vec3 a_pos;\n'
        + 'uniform mat4 u_mvp;\n'
        + 'void main() { gl_Position = u_mvp * vec4(a_pos, 1.0); }\n',
        'precision mediump float;\n'
        + 'uniform vec4 u_color;\n'
        + 'void main() { gl_FragColor = u_color; }\n'
    );

    // Pin shader (points with glow)
    this._progPin = linkProgram(gl,
        'attribute vec3 a_pos;\n'
        + 'attribute float a_size;\n'
        + 'attribute float a_alpha;\n'
        + 'uniform mat4 u_mvp;\n'
        + 'varying float v_alpha;\n'
        + 'void main() {\n'
        + '  gl_Position = u_mvp * vec4(a_pos, 1.0);\n'
        + '  gl_PointSize = a_size;\n'
        + '  v_alpha = a_alpha;\n'
        + '}\n',
        'precision mediump float;\n'
        + 'uniform vec4 u_color;\n'
        + 'varying float v_alpha;\n'
        + 'void main() {\n'
        + '  float d = length(gl_PointCoord - vec2(0.5));\n'
        + '  if (d > 0.5) discard;\n'
        + '  float glow = 1.0 - smoothstep(0.0, 0.5, d);\n'
        + '  gl_FragColor = vec4(u_color.rgb, u_color.a * v_alpha * glow);\n'
        + '}\n'
    );
};

WebGLGlobe.prototype._buildSphere = function(slices, stacks) {
    var gl = this._gl;
    var R = this._radius;
    var verts = [];
    var indices = [];

    for (var i = 0; i <= stacks; i++) {
        var phi = Math.PI * i / stacks;
        for (var j = 0; j <= slices; j++) {
            var theta = 2 * Math.PI * j / slices;
            verts.push(
                R * Math.sin(phi) * Math.cos(theta),
                R * Math.cos(phi),
                R * Math.sin(phi) * Math.sin(theta)
            );
        }
    }

    // Wireframe lines: horizontal + vertical
    for (var i = 0; i <= stacks; i++) {
        for (var j = 0; j < slices; j++) {
            var a = i * (slices + 1) + j;
            var b = a + 1;
            indices.push(a, b);
        }
    }
    for (var j = 0; j <= slices; j++) {
        for (var i = 0; i < stacks; i++) {
            var a = i * (slices + 1) + j;
            var b = a + (slices + 1);
            indices.push(a, b);
        }
    }

    this._sphereVBO = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this._sphereVBO);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(verts), gl.STATIC_DRAW);

    this._sphereIBO = gl.createBuffer();
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this._sphereIBO);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint16Array(indices), gl.STATIC_DRAW);
    this._sphereCount = indices.length;
};

WebGLGlobe.prototype._buildContinentLines = function() {
    var gl = this._gl;
    var verts = [];
    var R = this._radius * 1.005; // slightly above sphere

    for (var c = 0; c < CONTINENTS.length; c++) {
        var pts = CONTINENTS[c];
        for (var i = 0; i < pts.length - 1; i++) {
            var a = latLonToXYZ(pts[i][0], pts[i][1], R);
            var b = latLonToXYZ(pts[i+1][0], pts[i+1][1], R);
            verts.push(a[0], a[1], a[2], b[0], b[1], b[2]);
        }
    }

    this._lineVBO = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this._lineVBO);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(verts), gl.STATIC_DRAW);
    this._lineCount = verts.length / 3;
};

WebGLGlobe.prototype._setupInteraction = function() {
    var self = this;
    var cv = this._canvas;

    cv.addEventListener('mousedown', function(e) {
        self._dragging = true;
        self._autoRotate = false;
        self._lastMouse = { x: e.clientX, y: e.clientY };
        e.preventDefault();
    });

    window.addEventListener('mousemove', function(e) {
        if (!self._dragging || !self._lastMouse) return;
        var dx = e.clientX - self._lastMouse.x;
        var dy = e.clientY - self._lastMouse.y;
        self._rotY += dx * 0.005;
        self._rotX += dy * 0.005;
        self._rotX = Math.max(-1.2, Math.min(1.2, self._rotX));
        self._lastMouse = { x: e.clientX, y: e.clientY };
    });

    window.addEventListener('mouseup', function() {
        if (self._dragging) {
            self._dragging = false;
            // Resume auto-rotate after 5s of inactivity
            setTimeout(function() { if (!self._dragging) self._autoRotate = true; }, 5000);
        }
    });

    // Touch support
    cv.addEventListener('touchstart', function(e) {
        if (e.touches.length === 1) {
            self._dragging = true;
            self._autoRotate = false;
            self._lastMouse = { x: e.touches[0].clientX, y: e.touches[0].clientY };
            e.preventDefault();
        }
    }, { passive: false });

    cv.addEventListener('touchmove', function(e) {
        if (!self._dragging || !self._lastMouse || e.touches.length !== 1) return;
        var dx = e.touches[0].clientX - self._lastMouse.x;
        var dy = e.touches[0].clientY - self._lastMouse.y;
        self._rotY += dx * 0.005;
        self._rotX += dy * 0.005;
        self._rotX = Math.max(-1.2, Math.min(1.2, self._rotX));
        self._lastMouse = { x: e.touches[0].clientX, y: e.touches[0].clientY };
        e.preventDefault();
    }, { passive: false });

    cv.addEventListener('touchend', function() {
        self._dragging = false;
        setTimeout(function() { if (!self._dragging) self._autoRotate = true; }, 5000);
    });
};

WebGLGlobe.prototype._resize = function() {
    var rect = this._container.getBoundingClientRect();
    var dpr = Math.min(window.devicePixelRatio || 1, 2);
    this._canvas.width = rect.width * dpr;
    this._canvas.height = rect.height * dpr;
    this._gl.viewport(0, 0, this._canvas.width, this._canvas.height);
};

WebGLGlobe.prototype._startLoop = function() {
    var self = this;
    var targetInterval = 1000 / 30; // 30fps
    var lastFrame = 0;

    function frame(now) {
        self._animFrame = requestAnimationFrame(frame);
        if (now - lastFrame < targetInterval) return;
        lastFrame = now;
        self._render(now);
    }
    self._animFrame = requestAnimationFrame(frame);
};

WebGLGlobe.prototype._render = function(now) {
    var gl = this._gl;
    var w = this._canvas.width;
    var h = this._canvas.height;

    // Detect container resize
    var rect = this._container.getBoundingClientRect();
    var dpr = Math.min(window.devicePixelRatio || 1, 2);
    if (Math.abs(rect.width * dpr - w) > 2 || Math.abs(rect.height * dpr - h) > 2) {
        this._resize();
        w = this._canvas.width;
        h = this._canvas.height;
    }

    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.enable(gl.DEPTH_TEST);
    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

    // Auto-rotate
    if (this._autoRotate) {
        this._rotY += 0.003;
    }

    var aspect = w / h;
    var proj = mat4Perspective(Math.PI / 4, aspect, 0.1, 100);
    var view = mat4LookAt([0, 0, 3], [0, 0, 0], [0, 1, 0]);
    var model = mat4Identity();
    model = mat4RotateX(model, this._rotX);
    model = mat4RotateY(model, this._rotY);
    var mvp = mat4Multiply(proj, mat4Multiply(view, model));

    // Draw sphere wireframe
    if (this._progSphere) {
        gl.useProgram(this._progSphere);
        gl.uniformMatrix4fv(gl.getUniformLocation(this._progSphere, 'u_mvp'), false, mvp);
        gl.uniform4f(gl.getUniformLocation(this._progSphere, 'u_color'), 0.2, 0.32, 0.42, 0.3);
        var aPos = gl.getAttribLocation(this._progSphere, 'a_pos');
        gl.bindBuffer(gl.ARRAY_BUFFER, this._sphereVBO);
        gl.enableVertexAttribArray(aPos);
        gl.vertexAttribPointer(aPos, 3, gl.FLOAT, false, 0, 0);
        gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this._sphereIBO);
        gl.drawElements(gl.LINES, this._sphereCount, gl.UNSIGNED_SHORT, 0);
    }

    // Draw continent outlines
    if (this._progLine && this._lineCount > 0) {
        gl.useProgram(this._progLine);
        gl.uniformMatrix4fv(gl.getUniformLocation(this._progLine, 'u_mvp'), false, mvp);
        gl.uniform4f(gl.getUniformLocation(this._progLine, 'u_color'), 0.08, 0.72, 0.65, 0.85);
        var aPos2 = gl.getAttribLocation(this._progLine, 'a_pos');
        gl.bindBuffer(gl.ARRAY_BUFFER, this._lineVBO);
        gl.enableVertexAttribArray(aPos2);
        gl.vertexAttribPointer(aPos2, 3, gl.FLOAT, false, 0, 0);
        gl.drawArrays(gl.LINES, 0, this._lineCount);
    }

    // Draw pins
    this._renderPins(gl, mvp, now);

    // Update overlay
    this._updateOverlay(mvp, w, h);
};

WebGLGlobe.prototype._renderPins = function(gl, mvp, now) {
    if (!this._progPin || this._pins.length === 0) return;

    var R = this._radius * 1.02;
    var posData = [];
    var sizeData = [];
    var alphaData = [];

    for (var i = 0; i < this._pins.length; i++) {
        var p = this._pins[i];
        var xyz = latLonToXYZ(p.lat, p.lon, R);
        posData.push(xyz[0], xyz[1], xyz[2]);

        // Size based on count (min 4, max 16)
        var sz = Math.min(16, Math.max(4, 4 + Math.log2(p.count + 1) * 3));
        sizeData.push(sz);

        // Pulse: new pins (age < 3s) pulse brighter
        var age = (now - p.time) / 1000;
        var pulse = age < 3 ? 0.5 + 0.5 * Math.sin(age * 4) : 1.0;
        alphaData.push(Math.min(1.0, pulse));
    }

    gl.useProgram(this._progPin);
    gl.uniformMatrix4fv(gl.getUniformLocation(this._progPin, 'u_mvp'), false, mvp);
    gl.uniform4f(gl.getUniformLocation(this._progPin, 'u_color'), 0.08, 0.72, 0.65, 1.0);

    // Position
    if (!this._pinVBO) this._pinVBO = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this._pinVBO);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(posData), gl.DYNAMIC_DRAW);
    var aPos = gl.getAttribLocation(this._progPin, 'a_pos');
    gl.enableVertexAttribArray(aPos);
    gl.vertexAttribPointer(aPos, 3, gl.FLOAT, false, 0, 0);

    // Size
    var sizeBuf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, sizeBuf);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(sizeData), gl.DYNAMIC_DRAW);
    var aSize = gl.getAttribLocation(this._progPin, 'a_size');
    if (aSize >= 0) {
        gl.enableVertexAttribArray(aSize);
        gl.vertexAttribPointer(aSize, 1, gl.FLOAT, false, 0, 0);
    }

    // Alpha
    var alphaBuf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, alphaBuf);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(alphaData), gl.DYNAMIC_DRAW);
    var aAlpha = gl.getAttribLocation(this._progPin, 'a_alpha');
    if (aAlpha >= 0) {
        gl.enableVertexAttribArray(aAlpha);
        gl.vertexAttribPointer(aAlpha, 1, gl.FLOAT, false, 0, 0);
    }

    gl.drawArrays(gl.POINTS, 0, this._pins.length);

    gl.deleteBuffer(sizeBuf);
    gl.deleteBuffer(alphaBuf);
};

WebGLGlobe.prototype._updateOverlay = function(mvp, w, h) {
    if (!this._overlay) return;
    var total = 0;
    for (var i = 0; i < this._pins.length; i++) total += this._pins[i].count;
    this._overlay.innerHTML = '<div style="position:absolute;bottom:8px;left:8px;font-size:11px;color:#94a3b8;'
        + 'background:rgba(15,23,42,.7);padding:2px 8px;border-radius:4px">'
        + total + ' listener' + (total === 1 ? '' : 's') + ' in '
        + this._pins.length + ' region' + (this._pins.length === 1 ? '' : 's')
        + '</div>';
};

/**
 * Update listener pins from country data.
 * @param {Array} data - [{country:'US', count:5}, ...]
 */
WebGLGlobe.prototype.updateListeners = function(data) {
    if (!Array.isArray(data)) return;
    var now = performance.now();
    var existing = {};
    for (var i = 0; i < this._pins.length; i++) {
        existing[this._pins[i].cc] = this._pins[i];
    }

    var newPins = [];
    for (var i = 0; i < data.length; i++) {
        var cc = (data[i].country || '').toUpperCase();
        var count = parseInt(data[i].count) || 1;
        var center = COUNTRY_CENTERS[cc];
        if (!center) continue;

        if (existing[cc]) {
            existing[cc].count = count;
            newPins.push(existing[cc]);
        } else {
            newPins.push({
                cc: cc, lat: center[0], lon: center[1],
                count: count, time: now
            });
        }
    }
    this._pins = newPins;
};

WebGLGlobe.prototype.destroy = function() {
    if (this._animFrame) cancelAnimationFrame(this._animFrame);
    if (this._canvas && this._canvas.parentNode) this._canvas.parentNode.removeChild(this._canvas);
    if (this._overlay && this._overlay.parentNode) this._overlay.parentNode.removeChild(this._overlay);
    this._gl = null;
    this._ok = false;
};

/* ======================================================================
 * WebGLBandwidthChart — 3D area chart for live bandwidth
 * ====================================================================== */

function WebGLBandwidthChart(canvas, opts) {
    opts = opts || {};
    this._canvas = canvas;
    this._gl = null;
    this._ok = false;
    this._prog = null;
    this._gridProg = null;
    this._vbo = null;
    this._gridVBO = null;
    this._animFrame = null;
    this._maxPoints = opts.maxPoints || 30;
    this._datasets = {};   // name -> { data: [], color: [r,g,b] }
    this._maxVal = 1024;
    this._targetMax = 1024;
    this._colors = [
        [0.08, 0.72, 0.65],   // teal
        [0.03, 0.57, 0.70],   // cyan
        [0.65, 0.55, 0.98]    // purple
    ];
    this._init();
}

WebGLBandwidthChart.prototype._init = function() {
    var res = initGL(this._canvas, { alpha: false });
    if (!res) return;
    this._gl = res.gl;

    // Area shader
    this._prog = linkProgram(this._gl,
        'attribute vec3 a_pos;\n'
        + 'attribute float a_alpha;\n'
        + 'uniform mat4 u_mvp;\n'
        + 'varying float v_alpha;\n'
        + 'varying float v_y;\n'
        + 'void main() {\n'
        + '  gl_Position = u_mvp * vec4(a_pos, 1.0);\n'
        + '  v_alpha = a_alpha;\n'
        + '  v_y = a_pos.y;\n'
        + '}\n',
        'precision mediump float;\n'
        + 'uniform vec3 u_color;\n'
        + 'varying float v_alpha;\n'
        + 'varying float v_y;\n'
        + 'void main() {\n'
        + '  float fade = mix(0.6, 0.05, v_y);\n'
        + '  gl_FragColor = vec4(u_color, fade * v_alpha);\n'
        + '}\n'
    );

    // Grid shader
    this._gridProg = linkProgram(this._gl,
        'attribute vec3 a_pos;\n'
        + 'uniform mat4 u_mvp;\n'
        + 'void main() { gl_Position = u_mvp * vec4(a_pos, 1.0); }\n',
        'precision mediump float;\n'
        + 'uniform vec4 u_color;\n'
        + 'void main() { gl_FragColor = u_color; }\n'
    );

    this._ok = true;
    this._resize();
    this._startLoop();
};

WebGLBandwidthChart.prototype._resize = function() {
    var rect = this._canvas.parentElement.getBoundingClientRect();
    var dpr = Math.min(window.devicePixelRatio || 1, 2);
    this._canvas.width = rect.width * dpr;
    this._canvas.height = rect.height * dpr;
    this._gl.viewport(0, 0, this._canvas.width, this._canvas.height);
};

WebGLBandwidthChart.prototype._startLoop = function() {
    var self = this;
    var targetInterval = 1000 / 30;
    var lastFrame = 0;

    function frame(now) {
        self._animFrame = requestAnimationFrame(frame);
        if (now - lastFrame < targetInterval) return;
        lastFrame = now;
        self._render();
    }
    self._animFrame = requestAnimationFrame(frame);
};

WebGLBandwidthChart.prototype._render = function() {
    var gl = this._gl;
    if (!gl) return;

    // Detect resize
    var rect = this._canvas.parentElement.getBoundingClientRect();
    var dpr = Math.min(window.devicePixelRatio || 1, 2);
    if (Math.abs(rect.width * dpr - this._canvas.width) > 2) {
        this._resize();
    }

    // Smooth max value transition
    this._maxVal += (this._targetMax - this._maxVal) * 0.08;

    gl.clearColor(0.059, 0.090, 0.165, 1.0); // #0f172a
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
    gl.enable(gl.DEPTH_TEST);

    var aspect = this._canvas.width / this._canvas.height;
    var proj = mat4Perspective(Math.PI / 6, aspect, 0.1, 100);
    var view = mat4LookAt([0, 0.5, 2.5], [0, 0.15, 0], [0, 1, 0]);
    var mvp = mat4Multiply(proj, view);

    // Draw grid
    this._renderGrid(gl, mvp);

    // Draw each dataset
    var names = Object.keys(this._datasets);
    for (var di = 0; di < names.length; di++) {
        var ds = this._datasets[names[di]];
        this._renderArea(gl, mvp, ds.data, ds.color, di * 0.15);
    }
};

WebGLBandwidthChart.prototype._renderGrid = function(gl, mvp) {
    if (!this._gridProg) return;
    var verts = [];
    var xMin = -1, xMax = 1, zMin = -0.3, zMax = 0.3;

    // Horizontal grid lines
    for (var i = 0; i <= 4; i++) {
        var y = i * 0.25;
        verts.push(xMin, y, zMax, xMax, y, zMax);
        // Depth lines on floor
        if (i === 0) {
            verts.push(xMin, 0, zMin, xMin, 0, zMax);
            verts.push(xMax, 0, zMin, xMax, 0, zMax);
        }
    }
    // Vertical grid lines
    for (var i = 0; i <= 6; i++) {
        var x = xMin + (xMax - xMin) * i / 6;
        verts.push(x, 0, zMax, x, 1, zMax);
    }

    if (!this._gridVBO) this._gridVBO = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this._gridVBO);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(verts), gl.DYNAMIC_DRAW);

    gl.useProgram(this._gridProg);
    gl.uniformMatrix4fv(gl.getUniformLocation(this._gridProg, 'u_mvp'), false, mvp);
    gl.uniform4f(gl.getUniformLocation(this._gridProg, 'u_color'), 0.2, 0.25, 0.33, 0.5);

    var aPos = gl.getAttribLocation(this._gridProg, 'a_pos');
    gl.enableVertexAttribArray(aPos);
    gl.vertexAttribPointer(aPos, 3, gl.FLOAT, false, 0, 0);
    gl.drawArrays(gl.LINES, 0, verts.length / 3);
};

WebGLBandwidthChart.prototype._renderArea = function(gl, mvp, data, color, zOffset) {
    if (!this._prog || data.length < 2) return;

    var maxPts = this._maxPoints;
    var maxV = Math.max(this._maxVal, 1);
    var xMin = -1, xMax = 1;
    var z = 0.2 - zOffset;

    // Build triangle strip: bottom-top pairs
    var verts = [];
    var alphas = [];

    for (var i = 0; i < data.length; i++) {
        var x = xMin + (xMax - xMin) * i / (maxPts - 1);
        var y = Math.min(1.0, (data[i] || 0) / maxV);

        // Bottom vertex
        verts.push(x, 0, z);
        alphas.push(0.3);
        // Top vertex
        verts.push(x, y, z);
        alphas.push(1.0);
    }

    if (!this._vbo) this._vbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this._vbo);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(verts), gl.DYNAMIC_DRAW);

    var alphaBuf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, alphaBuf);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(alphas), gl.DYNAMIC_DRAW);

    gl.useProgram(this._prog);
    gl.uniformMatrix4fv(gl.getUniformLocation(this._prog, 'u_mvp'), false, mvp);
    gl.uniform3f(gl.getUniformLocation(this._prog, 'u_color'), color[0], color[1], color[2]);

    var aPos = gl.getAttribLocation(this._prog, 'a_pos');
    gl.bindBuffer(gl.ARRAY_BUFFER, this._vbo);
    gl.enableVertexAttribArray(aPos);
    gl.vertexAttribPointer(aPos, 3, gl.FLOAT, false, 0, 0);

    var aAlpha = gl.getAttribLocation(this._prog, 'a_alpha');
    gl.bindBuffer(gl.ARRAY_BUFFER, alphaBuf);
    gl.enableVertexAttribArray(aAlpha);
    gl.vertexAttribPointer(aAlpha, 1, gl.FLOAT, false, 0, 0);

    gl.drawArrays(gl.TRIANGLE_STRIP, 0, verts.length / 3);

    // Also draw the top edge as a line for clarity
    var lineVerts = [];
    for (var i = 0; i < data.length; i++) {
        var x = xMin + (xMax - xMin) * i / (maxPts - 1);
        var y = Math.min(1.0, (data[i] || 0) / maxV);
        lineVerts.push(x, y, z - 0.001);
    }
    var lineBuf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, lineBuf);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(lineVerts), gl.DYNAMIC_DRAW);
    gl.bindBuffer(gl.ARRAY_BUFFER, lineBuf);
    gl.vertexAttribPointer(aPos, 3, gl.FLOAT, false, 0, 0);

    // Fake alpha = 1.0 for line
    var oneAlphas = new Float32Array(data.length);
    for (var i = 0; i < data.length; i++) oneAlphas[i] = 1.0;
    var oneAlphaBuf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, oneAlphaBuf);
    gl.bufferData(gl.ARRAY_BUFFER, oneAlphas, gl.DYNAMIC_DRAW);
    gl.vertexAttribPointer(aAlpha, 1, gl.FLOAT, false, 0, 0);

    gl.drawArrays(gl.LINE_STRIP, 0, data.length);

    gl.deleteBuffer(alphaBuf);
    gl.deleteBuffer(lineBuf);
    gl.deleteBuffer(oneAlphaBuf);
};

/**
 * Push data for a named series.
 * @param {string} name - Series name
 * @param {number[]} data - Array of values
 * @param {number} colorIdx - Color index (0-2)
 */
WebGLBandwidthChart.prototype.updateData = function(name, data, colorIdx) {
    this._datasets[name] = {
        data: data.slice(),
        color: this._colors[colorIdx % this._colors.length]
    };
    // Recalculate max
    var maxV = 1024;
    var names = Object.keys(this._datasets);
    for (var i = 0; i < names.length; i++) {
        var d = this._datasets[names[i]].data;
        for (var j = 0; j < d.length; j++) {
            if (d[j] > maxV) maxV = d[j];
        }
    }
    this._targetMax = maxV * 1.2;
};

WebGLBandwidthChart.prototype.destroy = function() {
    if (this._animFrame) cancelAnimationFrame(this._animFrame);
    this._gl = null;
    this._ok = false;
};

/* ======================================================================
 * WebGLEncoderRack — 3D isometric server rack
 * ====================================================================== */

function WebGLEncoderRack(canvas, opts) {
    opts = opts || {};
    this._canvas = canvas;
    this._gl = null;
    this._ok = false;
    this._prog = null;
    this._vbo = null;
    this._animFrame = null;
    this._slots = [];   // { id, name, state, color }
    this._hover = -1;
    this._onClick = opts.onClick || null;
    this._init();
}

WebGLEncoderRack.prototype._init = function() {
    var res = initGL(this._canvas, { alpha: false });
    if (!res) return;
    this._gl = res.gl;

    this._prog = linkProgram(this._gl,
        'attribute vec3 a_pos;\n'
        + 'attribute vec4 a_color;\n'
        + 'uniform mat4 u_mvp;\n'
        + 'varying vec4 v_color;\n'
        + 'void main() {\n'
        + '  gl_Position = u_mvp * vec4(a_pos, 1.0);\n'
        + '  v_color = a_color;\n'
        + '}\n',
        'precision mediump float;\n'
        + 'varying vec4 v_color;\n'
        + 'void main() { gl_FragColor = v_color; }\n'
    );

    this._setupInteraction();
    this._ok = true;
    this._resize();
    this._startLoop();
};

WebGLEncoderRack.prototype._setupInteraction = function() {
    var self = this;
    this._canvas.addEventListener('click', function(e) {
        if (!self._onClick) return;
        var idx = self._hitTest(e);
        if (idx >= 0 && idx < self._slots.length) {
            self._onClick(self._slots[idx]);
        }
    });
    this._canvas.addEventListener('mousemove', function(e) {
        self._hover = self._hitTest(e);
        self._canvas.style.cursor = self._hover >= 0 ? 'pointer' : 'default';
    });
};

WebGLEncoderRack.prototype._hitTest = function(e) {
    var rect = this._canvas.getBoundingClientRect();
    var y = e.clientY - rect.top;
    var slotH = rect.height / Math.max(this._slots.length, 1);
    var idx = Math.floor(y / slotH);
    if (idx >= 0 && idx < this._slots.length) return idx;
    return -1;
};

WebGLEncoderRack.prototype._resize = function() {
    var rect = this._canvas.parentElement.getBoundingClientRect();
    var dpr = Math.min(window.devicePixelRatio || 1, 2);
    this._canvas.width = rect.width * dpr;
    this._canvas.height = rect.height * dpr;
    this._gl.viewport(0, 0, this._canvas.width, this._canvas.height);
};

WebGLEncoderRack.prototype._startLoop = function() {
    var self = this;
    var targetInterval = 1000 / 30;
    var lastFrame = 0;

    function frame(now) {
        self._animFrame = requestAnimationFrame(frame);
        if (now - lastFrame < targetInterval) return;
        lastFrame = now;
        self._render(now);
    }
    self._animFrame = requestAnimationFrame(frame);
};

WebGLEncoderRack.prototype._render = function(now) {
    var gl = this._gl;
    if (!gl || !this._prog) return;

    var rect = this._canvas.parentElement.getBoundingClientRect();
    var dpr = Math.min(window.devicePixelRatio || 1, 2);
    if (Math.abs(rect.width * dpr - this._canvas.width) > 2) this._resize();

    gl.clearColor(0.059, 0.090, 0.165, 1.0);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.enable(gl.DEPTH_TEST);

    if (this._slots.length === 0) return;

    var aspect = this._canvas.width / this._canvas.height;
    var proj = mat4Perspective(Math.PI / 5, aspect, 0.1, 100);
    var view = mat4LookAt([1.5, 1.5, 3], [0, 0, 0], [0, 1, 0]);
    var mvp = mat4Multiply(proj, view);

    var verts = [];
    var n = this._slots.length;
    var unitH = 1.8 / Math.max(n, 3);
    var startY = (n - 1) * unitH / 2;

    for (var i = 0; i < n; i++) {
        var slot = this._slots[i];
        var y = startY - i * unitH;
        var w = 1.2, h = unitH * 0.85, d = 0.4;

        // State colors
        var sc;
        switch ((slot.state || '').toLowerCase()) {
            case 'live':        sc = [0.13, 0.72, 0.37, 1.0]; break;
            case 'connecting':
            case 'reconnecting':sc = [0.98, 0.45, 0.09, 1.0]; break;
            case 'error':       sc = [0.94, 0.27, 0.27, 1.0]; break;
            case 'sleep':       sc = [0.92, 0.72, 0.03, 1.0]; break;
            default:            sc = [0.39, 0.45, 0.53, 1.0]; break;
        }

        // Brighten on hover
        if (i === this._hover) {
            sc[0] = Math.min(1, sc[0] + 0.15);
            sc[1] = Math.min(1, sc[1] + 0.15);
            sc[2] = Math.min(1, sc[2] + 0.15);
        }

        // Front face darker (panel)
        var fc = [sc[0]*0.7, sc[1]*0.7, sc[2]*0.7, sc[3]];
        // Top face lighter
        var tc = [sc[0]*0.9, sc[1]*0.9, sc[2]*0.9, sc[3]];

        // Front face (z = d/2)
        var x0 = -w/2, x1 = w/2, y0 = y-h/2, y1 = y+h/2, z0 = -d/2, z1 = d/2;
        // Front
        this._addQuad(verts, [x0,y0,z1], [x1,y0,z1], [x1,y1,z1], [x0,y1,z1], fc);
        // Top
        this._addQuad(verts, [x0,y1,z0], [x1,y1,z0], [x1,y1,z1], [x0,y1,z1], tc);
        // Right
        this._addQuad(verts, [x1,y0,z0], [x1,y0,z1], [x1,y1,z1], [x1,y1,z0], sc);

        // LED indicator on front face
        var ledX = x0 + 0.08, ledY = y + h * 0.2;
        var ledS = Math.min(0.04, h * 0.2);
        var pulse = ((slot.state || '').toLowerCase() === 'live') ?
            0.7 + 0.3 * Math.sin(now * 0.004 + i) : 1.0;
        var lc = [sc[0]*pulse, sc[1]*pulse, sc[2]*pulse, 1.0];
        this._addQuad(verts,
            [ledX, ledY-ledS, z1+0.01], [ledX+ledS*2, ledY-ledS, z1+0.01],
            [ledX+ledS*2, ledY+ledS, z1+0.01], [ledX, ledY+ledS, z1+0.01], lc);
    }

    // Rack frame (border)
    var frameC = [0.15, 0.2, 0.27, 1.0];
    var fw = 1.35, fh = n * unitH + 0.1, fd = 0.45;
    var fy = 0;
    // Left rail
    this._addQuad(verts,
        [-fw/2, fy-fh/2, fd/2], [-fw/2+0.03, fy-fh/2, fd/2],
        [-fw/2+0.03, fy+fh/2, fd/2], [-fw/2, fy+fh/2, fd/2], frameC);
    // Right rail
    this._addQuad(verts,
        [fw/2-0.03, fy-fh/2, fd/2], [fw/2, fy-fh/2, fd/2],
        [fw/2, fy+fh/2, fd/2], [fw/2-0.03, fy+fh/2, fd/2], frameC);

    if (!this._vbo) this._vbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this._vbo);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(verts), gl.DYNAMIC_DRAW);

    gl.useProgram(this._prog);
    gl.uniformMatrix4fv(gl.getUniformLocation(this._prog, 'u_mvp'), false, mvp);

    var stride = 7 * 4; // 3 pos + 4 color = 7 floats
    var aPos = gl.getAttribLocation(this._prog, 'a_pos');
    var aColor = gl.getAttribLocation(this._prog, 'a_color');
    gl.enableVertexAttribArray(aPos);
    gl.vertexAttribPointer(aPos, 3, gl.FLOAT, false, stride, 0);
    if (aColor >= 0) {
        gl.enableVertexAttribArray(aColor);
        gl.vertexAttribPointer(aColor, 4, gl.FLOAT, false, stride, 12);
    }

    gl.drawArrays(gl.TRIANGLES, 0, verts.length / 7);
};

WebGLEncoderRack.prototype._addQuad = function(arr, a, b, c, d, col) {
    // Two triangles: a-b-c, a-c-d
    var pts = [a,b,c, a,c,d];
    for (var i = 0; i < pts.length; i++) {
        arr.push(pts[i][0], pts[i][1], pts[i][2], col[0], col[1], col[2], col[3]);
    }
};

/**
 * Update slot data.
 * @param {Array} slots - [{ slot_id, name, state }, ...]
 */
WebGLEncoderRack.prototype.updateSlots = function(slots) {
    if (!Array.isArray(slots)) return;
    this._slots = slots.map(function(s) {
        return { id: s.slot_id, name: s.name || 'Slot '+s.slot_id, state: s.state || 'idle' };
    });
};

WebGLEncoderRack.prototype.destroy = function() {
    if (this._animFrame) cancelAnimationFrame(this._animFrame);
    this._gl = null;
    this._ok = false;
};

/* ======================================================================
 * Fallback: simple country list (no WebGL)
 * ====================================================================== */

function renderCountryFallback(container, data) {
    if (!Array.isArray(data) || data.length === 0) {
        container.innerHTML = '<div style="color:var(--muted);font-size:12px;padding:12px">No geographic data available</div>';
        return;
    }
    var html = '<div style="max-height:200px;overflow-y:auto;padding:8px">';
    var sorted = data.slice().sort(function(a,b){ return (b.count||0) - (a.count||0); });
    for (var i = 0; i < sorted.length; i++) {
        var cc = (sorted[i].country || '??').toUpperCase();
        var cnt = parseInt(sorted[i].count) || 0;
        html += '<div style="display:flex;justify-content:space-between;padding:3px 0;font-size:12px;color:var(--text-dim)">'
            + '<span>' + cc + '</span><span style="color:var(--teal)">' + cnt + '</span></div>';
    }
    html += '</div>';
    container.innerHTML = html;
}

/* ======================================================================
 * Public API
 * ====================================================================== */

window.WebGLDashboard = {
    isWebGLAvailable: isWebGLAvailable,
    getWebGLPref: getWebGLPref,
    Globe: WebGLGlobe,
    BandwidthChart: WebGLBandwidthChart,
    EncoderRack: WebGLEncoderRack,
    renderCountryFallback: renderCountryFallback,
    COUNTRY_CENTERS: COUNTRY_CENTERS
};

})();
