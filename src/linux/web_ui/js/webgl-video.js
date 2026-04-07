/*
 * Mcaster1 WebGL Video Rendering Utilities
 * js/webgl-video.js
 *
 * GPU-accelerated video frame rendering: texture upload, fullscreen quad,
 * and transition effects between two video sources.
 * Supports: cut, fade, dissolve, wipe L/R, wipe circle, zoom, slide,
 * chroma key, PIP compositing, color correction, lower-third overlay.
 * WebGL 2.0 preferred, fallback to WebGL 1.0.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

(function() {
'use strict';

/* ======================================================================
 * Shader sources
 * ====================================================================== */

var VIDEO_VS = [
    'attribute vec2 a_pos;',
    'attribute vec2 a_uv;',
    'varying vec2 v_uv;',
    'void main() {',
    '    gl_Position = vec4(a_pos, 0.0, 1.0);',
    '    v_uv = a_uv;',
    '}'
].join('\n');

var VIDEO_FS = [
    'precision mediump float;',
    'uniform sampler2D u_video;',
    'varying vec2 v_uv;',
    'void main() {',
    '    gl_FragColor = texture2D(u_video, v_uv);',
    '}'
].join('\n');

/* ── Color correction fragment shader ─────────────────────────────── */

var COLOR_CORRECT_FS = [
    'precision mediump float;',
    'uniform sampler2D u_video;',
    'uniform float u_brightness;',  // -1.0 to 1.0
    'uniform float u_contrast;',    // 0.0 to 2.0
    'uniform float u_saturation;',  // 0.0 to 2.0
    'uniform float u_hue;',         // -PI to PI radians
    'varying vec2 v_uv;',
    '',
    'vec3 rgb2hsv(vec3 c) {',
    '    vec4 K = vec4(0.0, -1.0/3.0, 2.0/3.0, -1.0);',
    '    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));',
    '    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));',
    '    float d = q.x - min(q.w, q.y);',
    '    float e = 1.0e-10;',
    '    return vec3(abs(q.z + (q.w - q.y) / (6.0*d+e)), d/(q.x+e), q.x);',
    '}',
    '',
    'vec3 hsv2rgb(vec3 c) {',
    '    vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);',
    '    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);',
    '    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);',
    '}',
    '',
    'void main() {',
    '    vec4 color = texture2D(u_video, v_uv);',
    '    // Brightness',
    '    color.rgb += u_brightness;',
    '    // Contrast',
    '    color.rgb = (color.rgb - 0.5) * u_contrast + 0.5;',
    '    // Saturation + Hue',
    '    vec3 hsv = rgb2hsv(color.rgb);',
    '    hsv.x = fract(hsv.x + u_hue / 6.28318);',
    '    hsv.y *= u_saturation;',
    '    color.rgb = hsv2rgb(hsv);',
    '    color.rgb = clamp(color.rgb, 0.0, 1.0);',
    '    gl_FragColor = color;',
    '}'
].join('\n');

/* ── Advanced transition fragment shader ──────────────────────────── */
/* Types: 0=cut, 1=fade, 2=dissolve, 3=wipe_left, 4=wipe_right,
          5=wipe_circle, 6=zoom, 7=slide */

var TRANSITION_FS = [
    'precision mediump float;',
    'uniform sampler2D u_srcA;',
    'uniform sampler2D u_srcB;',
    'uniform float u_progress;',
    'uniform int u_type;',
    'varying vec2 v_uv;',
    '',
    'void main() {',
    '    vec4 a = texture2D(u_srcA, v_uv);',
    '    vec4 b = texture2D(u_srcB, v_uv);',
    '    float p = u_progress;',
    '',
    '    // 0: Cut',
    '    if (u_type == 0) {',
    '        gl_FragColor = p < 0.5 ? a : b;',
    '    }',
    '    // 1: Fade (crossfade)',
    '    else if (u_type == 1) {',
    '        float alpha = smoothstep(0.0, 1.0, p);',
    '        gl_FragColor = mix(a, b, alpha);',
    '    }',
    '    // 2: Dissolve (staggered crossfade)',
    '    else if (u_type == 2) {',
    '        float aFade = 1.0 - smoothstep(0.0, 0.6, p);',
    '        float bFade = smoothstep(0.4, 1.0, p);',
    '        gl_FragColor = a * aFade + b * bFade;',
    '    }',
    '    // 3: Wipe Left (A slides out to the left, B revealed)',
    '    else if (u_type == 3) {',
    '        float edge = smoothstep(0.0, 1.0, p);',
    '        gl_FragColor = v_uv.x < edge ? b : a;',
    '    }',
    '    // 4: Wipe Right (A slides out to the right, B revealed)',
    '    else if (u_type == 4) {',
    '        float edge = 1.0 - smoothstep(0.0, 1.0, p);',
    '        gl_FragColor = v_uv.x > edge ? b : a;',
    '    }',
    '    // 5: Wipe Circle (iris from center)',
    '    else if (u_type == 5) {',
    '        vec2 center = vec2(0.5, 0.5);',
    '        float dist = distance(v_uv, center);',
    '        float radius = p * 0.7071;',  // sqrt(0.5) to cover corners
    '        float edge = smoothstep(radius - 0.02, radius + 0.02, dist);',
    '        gl_FragColor = mix(b, a, edge);',
    '    }',
    '    // 6: Zoom (A scales down, B scales up)',
    '    else if (u_type == 6) {',
    '        float scaleA = 1.0 + p * 0.5;',
    '        float scaleB = 2.0 - p;',
    '        vec2 uvA = (v_uv - 0.5) * scaleA + 0.5;',
    '        vec2 uvB = (v_uv - 0.5) * scaleB + 0.5;',
    '        vec4 colA = texture2D(u_srcA, clamp(uvA, 0.0, 1.0));',
    '        vec4 colB = texture2D(u_srcB, clamp(uvB, 0.0, 1.0));',
    '        float alpha = smoothstep(0.3, 0.7, p);',
    '        colA.a *= (1.0 - alpha);',
    '        colB.a *= alpha;',
    '        gl_FragColor = mix(colA, colB, alpha);',
    '    }',
    '    // 7: Slide (A slides out left, B slides in from right)',
    '    else if (u_type == 7) {',
    '        float offset = smoothstep(0.0, 1.0, p);',
    '        vec2 uvA = v_uv + vec2(offset, 0.0);',
    '        vec2 uvB = v_uv + vec2(offset - 1.0, 0.0);',
    '        if (uvA.x >= 0.0 && uvA.x <= 1.0) {',
    '            gl_FragColor = texture2D(u_srcA, uvA);',
    '        } else {',
    '            gl_FragColor = texture2D(u_srcB, clamp(uvB, 0.0, 1.0));',
    '        }',
    '    }',
    '    else {',
    '        gl_FragColor = mix(a, b, p);',
    '    }',
    '}'
].join('\n');

/* ── Chroma key fragment shader ───────────────────────────────────── */

var CHROMA_FS = [
    'precision mediump float;',
    'uniform sampler2D u_video;',
    'uniform vec3 u_keyColor;',      // target color in RGB (0-1)
    'uniform float u_tolerance;',    // 0.0 - 1.0
    'uniform float u_softness;',     // feather edge 0.0 - 0.5
    'varying vec2 v_uv;',
    '',
    'void main() {',
    '    vec4 color = texture2D(u_video, v_uv);',
    '    float diff = distance(color.rgb, u_keyColor);',
    '    float alpha = smoothstep(u_tolerance - u_softness, u_tolerance + u_softness, diff);',
    '    gl_FragColor = vec4(color.rgb, color.a * alpha);',
    '}'
].join('\n');

/* ── PIP compositing fragment shader ──────────────────────────────── */
/* Composites a PIP source (small) over the main program output.
   u_pipRect = vec4(x, y, width, height) in UV coords (0-1)        */

var PIP_FS = [
    'precision mediump float;',
    'uniform sampler2D u_main;',
    'uniform sampler2D u_pip;',
    'uniform vec4 u_pipRect;',      // x, y, w, h in UV space
    'uniform float u_pipAlpha;',    // PIP opacity
    'uniform float u_borderWidth;', // border in UV space
    'uniform vec3 u_borderColor;',
    'varying vec2 v_uv;',
    '',
    'void main() {',
    '    vec4 mainColor = texture2D(u_main, v_uv);',
    '    vec2 pipMin = u_pipRect.xy;',
    '    vec2 pipMax = u_pipRect.xy + u_pipRect.zw;',
    '',
    '    if (v_uv.x >= pipMin.x && v_uv.x <= pipMax.x &&',
    '        v_uv.y >= pipMin.y && v_uv.y <= pipMax.y) {',
    '        // Inside PIP region',
    '        vec2 pipUV = (v_uv - pipMin) / u_pipRect.zw;',
    '        // Border check',
    '        float bw = u_borderWidth;',
    '        if (pipUV.x < bw || pipUV.x > 1.0-bw || pipUV.y < bw || pipUV.y > 1.0-bw) {',
    '            gl_FragColor = vec4(u_borderColor, 1.0);',
    '        } else {',
    '            vec4 pipColor = texture2D(u_pip, pipUV);',
    '            gl_FragColor = mix(mainColor, pipColor, u_pipAlpha);',
    '        }',
    '    } else {',
    '        gl_FragColor = mainColor;',
    '    }',
    '}'
].join('\n');

/* ── Lower-third overlay fragment shader ──────────────────────────── */
/* Composites a 2D text texture over the main output in the lower third */

var OVERLAY_FS = [
    'precision mediump float;',
    'uniform sampler2D u_main;',
    'uniform sampler2D u_overlay;',
    'uniform vec4 u_overlayRect;',  // x, y, w, h in UV space
    'uniform float u_overlayAlpha;',
    'varying vec2 v_uv;',
    '',
    'void main() {',
    '    vec4 mainColor = texture2D(u_main, v_uv);',
    '    vec2 oMin = u_overlayRect.xy;',
    '    vec2 oMax = u_overlayRect.xy + u_overlayRect.zw;',
    '',
    '    if (v_uv.x >= oMin.x && v_uv.x <= oMax.x &&',
    '        v_uv.y >= oMin.y && v_uv.y <= oMax.y) {',
    '        vec2 oUV = (v_uv - oMin) / u_overlayRect.zw;',
    '        vec4 ovColor = texture2D(u_overlay, oUV);',
    '        float a = ovColor.a * u_overlayAlpha;',
    '        gl_FragColor = mix(mainColor, ovColor, a);',
    '    } else {',
    '        gl_FragColor = mainColor;',
    '    }',
    '}'
].join('\n');

/* ======================================================================
 * WebGL helpers
 * ====================================================================== */

function compileShader(gl, type, source) {
    var shader = gl.createShader(type);
    gl.shaderSource(shader, source);
    gl.compileShader(shader);
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
        console.error('WebGL video shader error:', gl.getShaderInfoLog(shader));
        gl.deleteShader(shader);
        return null;
    }
    return shader;
}

function linkProgram(gl, vsSource, fsSource) {
    var vs = compileShader(gl, gl.VERTEX_SHADER, vsSource);
    var fs = compileShader(gl, gl.FRAGMENT_SHADER, fsSource);
    if (!vs || !fs) return null;
    var prog = gl.createProgram();
    gl.attachShader(prog, vs);
    gl.attachShader(prog, fs);
    gl.linkProgram(prog);
    if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
        console.error('WebGL video program link error:', gl.getProgramInfoLog(prog));
        gl.deleteProgram(prog);
        return null;
    }
    return prog;
}

function createTexture(gl) {
    var tex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    // Initialize with 1x1 black pixel
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE,
        new Uint8Array([0, 0, 0, 255]));
    return tex;
}

function setupQuad(gl, prog) {
    // Fullscreen quad: positions + UVs interleaved
    // UV is flipped vertically (1-v) because video textures are top-down
    var data = new Float32Array([
        // pos       uv
        -1, -1,     0, 1,
         1, -1,     1, 1,
        -1,  1,     0, 0,
         1,  1,     1, 0
    ]);
    var vbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
    gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);

    var aPos = gl.getAttribLocation(prog, 'a_pos');
    var aUv  = gl.getAttribLocation(prog, 'a_uv');

    return { vbo: vbo, aPos: aPos, aUv: aUv, stride: 16 };
}

function bindQuad(gl, quad) {
    gl.bindBuffer(gl.ARRAY_BUFFER, quad.vbo);
    gl.enableVertexAttribArray(quad.aPos);
    gl.vertexAttribPointer(quad.aPos, 2, gl.FLOAT, false, quad.stride, 0);
    if (quad.aUv >= 0) {
        gl.enableVertexAttribArray(quad.aUv);
        gl.vertexAttribPointer(quad.aUv, 2, gl.FLOAT, false, quad.stride, 8);
    }
}

function getGl(canvas) {
    var gl = canvas.getContext('webgl2', { antialias: false, alpha: false, premultipliedAlpha: false });
    if (!gl) gl = canvas.getContext('webgl', { antialias: false, alpha: false, premultipliedAlpha: false });
    return gl;
}

function getGlAlpha(canvas) {
    var gl = canvas.getContext('webgl2', { antialias: false, alpha: true, premultipliedAlpha: true });
    if (!gl) gl = canvas.getContext('webgl', { antialias: false, alpha: true, premultipliedAlpha: true });
    return gl;
}

/* ======================================================================
 * createVideoRenderer(canvas) -- single-source video renderer
 * ====================================================================== */

function createVideoRenderer(canvas) {
    var gl = getGl(canvas);
    if (!gl) {
        console.warn('WebGL not available for video renderer');
        return null;
    }

    var prog = linkProgram(gl, VIDEO_VS, VIDEO_FS);
    if (!prog) return null;

    var quad = setupQuad(gl, prog);
    var tex  = createTexture(gl);
    var uVideo = gl.getUniformLocation(prog, 'u_video');

    return {
        gl: gl,
        canvas: canvas,
        prog: prog,
        quad: quad,
        tex: tex,
        uVideo: uVideo,
        destroyed: false
    };
}

/* ======================================================================
 * drawVideoFrame(renderer, source) -- upload frame as texture + draw
 *   source can be: HTMLVideoElement, HTMLCanvasElement, ImageBitmap
 * ====================================================================== */

function drawVideoFrame(renderer, source) {
    if (!renderer || renderer.destroyed) return;
    var gl = renderer.gl;
    var canvas = renderer.canvas;

    // Match canvas internal resolution to display size
    var dw = canvas.clientWidth;
    var dh = canvas.clientHeight;
    if (canvas.width !== dw || canvas.height !== dh) {
        canvas.width = dw;
        canvas.height = dh;
    }

    gl.viewport(0, 0, canvas.width, canvas.height);
    gl.clearColor(0, 0, 0, 1);
    gl.clear(gl.COLOR_BUFFER_BIT);

    gl.useProgram(renderer.prog);

    // Upload video frame to texture
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, renderer.tex);
    try {
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, source);
    } catch (e) {
        // Source not ready (video not playing yet, etc.)
        return;
    }
    gl.uniform1i(renderer.uVideo, 0);

    bindQuad(gl, renderer.quad);
    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
}

/* ======================================================================
 * createColorCorrectionRenderer(canvas) -- per-source color correction
 * ====================================================================== */

function createColorCorrectionRenderer(canvas) {
    var gl = getGl(canvas);
    if (!gl) return null;

    var prog = linkProgram(gl, VIDEO_VS, COLOR_CORRECT_FS);
    if (!prog) return null;

    var quad = setupQuad(gl, prog);
    var tex = createTexture(gl);

    return {
        gl: gl,
        canvas: canvas,
        prog: prog,
        quad: quad,
        tex: tex,
        uVideo: gl.getUniformLocation(prog, 'u_video'),
        uBrightness: gl.getUniformLocation(prog, 'u_brightness'),
        uContrast: gl.getUniformLocation(prog, 'u_contrast'),
        uSaturation: gl.getUniformLocation(prog, 'u_saturation'),
        uHue: gl.getUniformLocation(prog, 'u_hue'),
        destroyed: false
    };
}

function drawColorCorrected(renderer, source, brightness, contrast, saturation, hue) {
    if (!renderer || renderer.destroyed) return;
    var gl = renderer.gl;
    var canvas = renderer.canvas;

    var dw = canvas.clientWidth;
    var dh = canvas.clientHeight;
    if (canvas.width !== dw || canvas.height !== dh) {
        canvas.width = dw;
        canvas.height = dh;
    }

    gl.viewport(0, 0, canvas.width, canvas.height);
    gl.clearColor(0, 0, 0, 1);
    gl.clear(gl.COLOR_BUFFER_BIT);
    gl.useProgram(renderer.prog);

    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, renderer.tex);
    try {
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, source);
    } catch (e) { return; }
    gl.uniform1i(renderer.uVideo, 0);
    gl.uniform1f(renderer.uBrightness, brightness || 0.0);
    gl.uniform1f(renderer.uContrast, contrast !== undefined ? contrast : 1.0);
    gl.uniform1f(renderer.uSaturation, saturation !== undefined ? saturation : 1.0);
    gl.uniform1f(renderer.uHue, hue || 0.0);

    bindQuad(gl, renderer.quad);
    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
}

/* ======================================================================
 * createTransitionRenderer(canvas) -- dual-source with transition
 * ====================================================================== */

function createTransitionRenderer(canvas) {
    var gl = getGl(canvas);
    if (!gl) return null;

    var prog = linkProgram(gl, VIDEO_VS, TRANSITION_FS);
    if (!prog) return null;

    var quad = setupQuad(gl, prog);
    var texA = createTexture(gl);
    var texB = createTexture(gl);
    var uSrcA = gl.getUniformLocation(prog, 'u_srcA');
    var uSrcB = gl.getUniformLocation(prog, 'u_srcB');
    var uProgress = gl.getUniformLocation(prog, 'u_progress');
    var uType = gl.getUniformLocation(prog, 'u_type');

    // Also build chroma key program for keyed sources
    var chromaProg = linkProgram(gl, VIDEO_VS, CHROMA_FS);
    var chromaQuad = chromaProg ? setupQuad(gl, chromaProg) : null;

    // PIP compositing program
    var pipProg = linkProgram(gl, VIDEO_VS, PIP_FS);
    var pipQuad = pipProg ? setupQuad(gl, pipProg) : null;

    // Overlay compositing program
    var overlayProg = linkProgram(gl, VIDEO_VS, OVERLAY_FS);
    var overlayQuad = overlayProg ? setupQuad(gl, overlayProg) : null;

    return {
        gl: gl,
        canvas: canvas,
        prog: prog,
        quad: quad,
        texA: texA,
        texB: texB,
        uSrcA: uSrcA,
        uSrcB: uSrcB,
        uProgress: uProgress,
        uType: uType,
        // Chroma key
        chromaProg: chromaProg,
        chromaQuad: chromaQuad,
        chromaTex: createTexture(gl),
        // PIP
        pipProg: pipProg,
        pipQuad: pipQuad,
        pipTexMain: createTexture(gl),
        pipTexPip: createTexture(gl),
        // Overlay
        overlayProg: overlayProg,
        overlayQuad: overlayQuad,
        overlayTexMain: createTexture(gl),
        overlayTex: createTexture(gl),
        destroyed: false
    };
}

/* ======================================================================
 * drawTransition(renderer, srcA, srcB, progress, type)
 *   type: 'cut' | 'fade' | 'dissolve' | 'wipe_left' | 'wipe_right'
 *         | 'wipe_circle' | 'zoom' | 'slide'
 *   progress: 0.0 (full A) to 1.0 (full B)
 * ====================================================================== */

var TRANSITION_MAP = {
    'cut': 0, 'fade': 1, 'dissolve': 2,
    'wipe_left': 3, 'wipe_right': 4, 'wipe_circle': 5,
    'zoom': 6, 'slide': 7
};

function drawTransition(renderer, srcA, srcB, progress, type) {
    if (!renderer || renderer.destroyed) return;
    var gl = renderer.gl;
    var canvas = renderer.canvas;

    var dw = canvas.clientWidth;
    var dh = canvas.clientHeight;
    if (canvas.width !== dw || canvas.height !== dh) {
        canvas.width = dw;
        canvas.height = dh;
    }

    gl.viewport(0, 0, canvas.width, canvas.height);
    gl.clearColor(0, 0, 0, 1);
    gl.clear(gl.COLOR_BUFFER_BIT);

    gl.useProgram(renderer.prog);

    // Upload source A
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, renderer.texA);
    if (srcA) {
        try { gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, srcA); } catch (e) {}
    }
    gl.uniform1i(renderer.uSrcA, 0);

    // Upload source B
    gl.activeTexture(gl.TEXTURE1);
    gl.bindTexture(gl.TEXTURE_2D, renderer.texB);
    if (srcB) {
        try { gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, srcB); } catch (e) {}
    }
    gl.uniform1i(renderer.uSrcB, 1);

    gl.uniform1f(renderer.uProgress, progress);
    gl.uniform1i(renderer.uType, TRANSITION_MAP[type] !== undefined ? TRANSITION_MAP[type] : 0);

    bindQuad(gl, renderer.quad);
    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
}

/* ======================================================================
 * drawChromaKey(renderer, source, keyColor, tolerance, softness)
 *   Renders source with chroma key removal to an offscreen canvas.
 *   keyColor: [r, g, b] 0-1 range
 *   tolerance: 0.0-1.0
 *   softness: 0.0-0.5
 *   Returns the canvas (can be used as a texture source)
 * ====================================================================== */

function drawChromaKey(renderer, source, keyColor, tolerance, softness) {
    if (!renderer || renderer.destroyed || !renderer.chromaProg) return;
    var gl = renderer.gl;
    var canvas = renderer.canvas;

    var dw = canvas.clientWidth;
    var dh = canvas.clientHeight;
    if (canvas.width !== dw || canvas.height !== dh) {
        canvas.width = dw;
        canvas.height = dh;
    }

    gl.viewport(0, 0, canvas.width, canvas.height);
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT);
    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

    gl.useProgram(renderer.chromaProg);

    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, renderer.chromaTex);
    try {
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, source);
    } catch (e) { return; }

    gl.uniform1i(gl.getUniformLocation(renderer.chromaProg, 'u_video'), 0);
    gl.uniform3fv(gl.getUniformLocation(renderer.chromaProg, 'u_keyColor'), keyColor || [0, 1, 0]);
    gl.uniform1f(gl.getUniformLocation(renderer.chromaProg, 'u_tolerance'), tolerance !== undefined ? tolerance : 0.3);
    gl.uniform1f(gl.getUniformLocation(renderer.chromaProg, 'u_softness'), softness !== undefined ? softness : 0.1);

    bindQuad(gl, renderer.chromaQuad);
    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
    gl.disable(gl.BLEND);
}

/* ======================================================================
 * drawPIP(renderer, mainSource, pipSource, pipRect, pipAlpha, borderWidth, borderColor)
 *   pipRect: { x, y, w, h } in 0-1 UV coords
 * ====================================================================== */

function drawPIP(renderer, mainSource, pipSource, pipRect, pipAlpha, borderWidth, borderColor) {
    if (!renderer || renderer.destroyed || !renderer.pipProg) return;
    var gl = renderer.gl;
    var canvas = renderer.canvas;

    var dw = canvas.clientWidth;
    var dh = canvas.clientHeight;
    if (canvas.width !== dw || canvas.height !== dh) {
        canvas.width = dw;
        canvas.height = dh;
    }

    gl.viewport(0, 0, canvas.width, canvas.height);
    gl.clearColor(0, 0, 0, 1);
    gl.clear(gl.COLOR_BUFFER_BIT);

    gl.useProgram(renderer.pipProg);

    // Main texture
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, renderer.pipTexMain);
    if (mainSource) {
        try { gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, mainSource); } catch (e) {}
    }
    gl.uniform1i(gl.getUniformLocation(renderer.pipProg, 'u_main'), 0);

    // PIP texture
    gl.activeTexture(gl.TEXTURE1);
    gl.bindTexture(gl.TEXTURE_2D, renderer.pipTexPip);
    if (pipSource) {
        try { gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, pipSource); } catch (e) {}
    }
    gl.uniform1i(gl.getUniformLocation(renderer.pipProg, 'u_pip'), 1);

    var r = pipRect || { x: 0.65, y: 0.65, w: 0.3, h: 0.3 };
    gl.uniform4f(gl.getUniformLocation(renderer.pipProg, 'u_pipRect'), r.x, r.y, r.w, r.h);
    gl.uniform1f(gl.getUniformLocation(renderer.pipProg, 'u_pipAlpha'), pipAlpha !== undefined ? pipAlpha : 1.0);
    gl.uniform1f(gl.getUniformLocation(renderer.pipProg, 'u_borderWidth'), borderWidth || 0.01);
    var bc = borderColor || [1.0, 1.0, 1.0];
    gl.uniform3fv(gl.getUniformLocation(renderer.pipProg, 'u_borderColor'), bc);

    bindQuad(gl, renderer.pipQuad);
    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
}

/* ======================================================================
 * drawOverlay(renderer, mainSource, overlayCanvas, overlayRect, overlayAlpha)
 *   overlayCanvas: 2D canvas with rendered text (lower third etc.)
 *   overlayRect: { x, y, w, h } in 0-1 UV coords
 * ====================================================================== */

function drawOverlay(renderer, mainSource, overlayCanvas, overlayRect, overlayAlpha) {
    if (!renderer || renderer.destroyed || !renderer.overlayProg) return;
    var gl = renderer.gl;
    var canvas = renderer.canvas;

    var dw = canvas.clientWidth;
    var dh = canvas.clientHeight;
    if (canvas.width !== dw || canvas.height !== dh) {
        canvas.width = dw;
        canvas.height = dh;
    }

    gl.viewport(0, 0, canvas.width, canvas.height);
    gl.clearColor(0, 0, 0, 1);
    gl.clear(gl.COLOR_BUFFER_BIT);
    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

    gl.useProgram(renderer.overlayProg);

    // Main
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, renderer.overlayTexMain);
    if (mainSource) {
        try { gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, mainSource); } catch (e) {}
    }
    gl.uniform1i(gl.getUniformLocation(renderer.overlayProg, 'u_main'), 0);

    // Overlay texture
    gl.activeTexture(gl.TEXTURE1);
    gl.bindTexture(gl.TEXTURE_2D, renderer.overlayTex);
    if (overlayCanvas) {
        try { gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, overlayCanvas); } catch (e) {}
    }
    gl.uniform1i(gl.getUniformLocation(renderer.overlayProg, 'u_overlay'), 1);

    var r = overlayRect || { x: 0.05, y: 0.72, w: 0.9, h: 0.18 };
    gl.uniform4f(gl.getUniformLocation(renderer.overlayProg, 'u_overlayRect'), r.x, r.y, r.w, r.h);
    gl.uniform1f(gl.getUniformLocation(renderer.overlayProg, 'u_overlayAlpha'), overlayAlpha !== undefined ? overlayAlpha : 1.0);

    bindQuad(gl, renderer.overlayQuad);
    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
    gl.disable(gl.BLEND);
}

/* ======================================================================
 * LowerThirdRenderer -- 2D canvas text overlay generator
 * ====================================================================== */

function LowerThirdRenderer(width, height) {
    this.canvas = document.createElement('canvas');
    this.canvas.width = width || 1280;
    this.canvas.height = height || 160;
    this.ctx = this.canvas.getContext('2d');

    // State
    this.text = '';
    this.fontSize = 36;
    this.fontFamily = 'Arial, sans-serif';
    this.textColor = '#ffffff';
    this.bgColor = 'rgba(15, 23, 42, 0.85)';
    this.accentColor = '#14b8a6';
    this.visible = false;

    // Animation
    this.animState = 'hidden';   // 'hidden' | 'slide_in' | 'hold' | 'slide_out'
    this.animProgress = 0;
    this.animStart = 0;
    this.slideInDuration = 400;
    this.holdDuration = 5000;
    this.slideOutDuration = 400;
}

LowerThirdRenderer.prototype.show = function(text, options) {
    options = options || {};
    this.text = text || '';
    if (options.fontSize) this.fontSize = options.fontSize;
    if (options.textColor) this.textColor = options.textColor;
    if (options.bgColor) this.bgColor = options.bgColor;
    if (options.accentColor) this.accentColor = options.accentColor;
    if (options.holdDuration !== undefined) this.holdDuration = options.holdDuration;

    this.visible = true;
    this.animState = 'slide_in';
    this.animStart = performance.now();
    this.animProgress = 0;
};

LowerThirdRenderer.prototype.hide = function() {
    if (this.animState === 'hidden') return;
    this.animState = 'slide_out';
    this.animStart = performance.now();
    this.animProgress = 0;
};

LowerThirdRenderer.prototype.update = function() {
    var now = performance.now();
    var elapsed = now - this.animStart;

    if (this.animState === 'slide_in') {
        this.animProgress = Math.min(elapsed / this.slideInDuration, 1.0);
        if (this.animProgress >= 1.0) {
            this.animState = 'hold';
            this.animStart = now;
        }
    } else if (this.animState === 'hold') {
        if (this.holdDuration > 0 && elapsed >= this.holdDuration) {
            this.animState = 'slide_out';
            this.animStart = now;
            this.animProgress = 0;
        }
    } else if (this.animState === 'slide_out') {
        this.animProgress = Math.min(elapsed / this.slideOutDuration, 1.0);
        if (this.animProgress >= 1.0) {
            this.animState = 'hidden';
            this.visible = false;
        }
    }

    this._render();
};

LowerThirdRenderer.prototype._render = function() {
    var ctx = this.ctx;
    var w = this.canvas.width;
    var h = this.canvas.height;

    ctx.clearRect(0, 0, w, h);
    if (this.animState === 'hidden') return;

    // Calculate slide offset
    var offsetX = 0;
    if (this.animState === 'slide_in') {
        offsetX = -(1.0 - this._ease(this.animProgress)) * w;
    } else if (this.animState === 'slide_out') {
        offsetX = -this._ease(this.animProgress) * w;
    }

    ctx.save();
    ctx.translate(offsetX, 0);

    // Accent bar on the left
    var accentWidth = 6;
    ctx.fillStyle = this.accentColor;
    ctx.fillRect(0, 0, accentWidth, h);

    // Background
    ctx.fillStyle = this.bgColor;
    ctx.fillRect(accentWidth, 0, w - accentWidth, h);

    // Text
    ctx.fillStyle = this.textColor;
    ctx.font = 'bold ' + this.fontSize + 'px ' + this.fontFamily;
    ctx.textBaseline = 'middle';
    ctx.fillText(this.text, accentWidth + 20, h / 2);

    ctx.restore();
};

LowerThirdRenderer.prototype._ease = function(t) {
    // ease-out cubic
    return 1 - Math.pow(1 - t, 3);
};

LowerThirdRenderer.prototype.getCanvas = function() {
    return this.canvas;
};

LowerThirdRenderer.prototype.isVisible = function() {
    return this.visible || this.animState !== 'hidden';
};

/* ======================================================================
 * destroyRenderer(renderer) -- clean up GL resources
 * ====================================================================== */

function destroyRenderer(renderer) {
    if (!renderer || renderer.destroyed) return;
    renderer.destroyed = true;
    var gl = renderer.gl;
    if (renderer.tex) gl.deleteTexture(renderer.tex);
    if (renderer.texA) gl.deleteTexture(renderer.texA);
    if (renderer.texB) gl.deleteTexture(renderer.texB);
    if (renderer.chromaTex) gl.deleteTexture(renderer.chromaTex);
    if (renderer.pipTexMain) gl.deleteTexture(renderer.pipTexMain);
    if (renderer.pipTexPip) gl.deleteTexture(renderer.pipTexPip);
    if (renderer.overlayTexMain) gl.deleteTexture(renderer.overlayTexMain);
    if (renderer.overlayTex) gl.deleteTexture(renderer.overlayTex);
    if (renderer.quad && renderer.quad.vbo) gl.deleteBuffer(renderer.quad.vbo);
    if (renderer.chromaQuad && renderer.chromaQuad.vbo) gl.deleteBuffer(renderer.chromaQuad.vbo);
    if (renderer.pipQuad && renderer.pipQuad.vbo) gl.deleteBuffer(renderer.pipQuad.vbo);
    if (renderer.overlayQuad && renderer.overlayQuad.vbo) gl.deleteBuffer(renderer.overlayQuad.vbo);
    if (renderer.prog) gl.deleteProgram(renderer.prog);
    if (renderer.chromaProg) gl.deleteProgram(renderer.chromaProg);
    if (renderer.pipProg) gl.deleteProgram(renderer.pipProg);
    if (renderer.overlayProg) gl.deleteProgram(renderer.overlayProg);
    var ext = gl.getExtension('WEBGL_lose_context');
    if (ext) ext.loseContext();
}

/* ======================================================================
 * PIP position presets
 * ====================================================================== */

var PIP_POSITIONS = {
    'tl': function(size) { return { x: 0.02, y: 0.02, w: size, h: size }; },
    'tr': function(size) { return { x: 0.98 - size, y: 0.02, w: size, h: size }; },
    'bl': function(size) { return { x: 0.02, y: 0.98 - size, w: size, h: size }; },
    'br': function(size) { return { x: 0.98 - size, y: 0.98 - size, w: size, h: size }; }
};

var PIP_SIZES = {
    '25': 0.25,
    '33': 0.33,
    '50': 0.50
};

/* ======================================================================
 * Public API
 * ====================================================================== */

window.Mc1WebGLVideo = {
    createVideoRenderer: createVideoRenderer,
    drawVideoFrame: drawVideoFrame,
    createColorCorrectionRenderer: createColorCorrectionRenderer,
    drawColorCorrected: drawColorCorrected,
    createTransitionRenderer: createTransitionRenderer,
    drawTransition: drawTransition,
    drawChromaKey: drawChromaKey,
    drawPIP: drawPIP,
    drawOverlay: drawOverlay,
    LowerThirdRenderer: LowerThirdRenderer,
    destroyRenderer: destroyRenderer,
    PIP_POSITIONS: PIP_POSITIONS,
    PIP_SIZES: PIP_SIZES,
    TRANSITION_TYPES: ['cut', 'fade', 'dissolve', 'wipe_left', 'wipe_right', 'wipe_circle', 'zoom', 'slide']
};

})();
