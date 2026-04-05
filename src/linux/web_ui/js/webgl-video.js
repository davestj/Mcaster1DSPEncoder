/*
 * Mcaster1 WebGL Video Rendering Utilities
 * js/webgl-video.js
 *
 * GPU-accelerated video frame rendering: texture upload, fullscreen quad,
 * and transition effects (cut, fade, dissolve) between two video sources.
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

var TRANSITION_FS = [
    'precision mediump float;',
    'uniform sampler2D u_srcA;',
    'uniform sampler2D u_srcB;',
    'uniform float u_progress;',
    'uniform int u_type;',  // 0=cut, 1=fade, 2=dissolve
    'varying vec2 v_uv;',
    'void main() {',
    '    vec4 a = texture2D(u_srcA, v_uv);',
    '    vec4 b = texture2D(u_srcB, v_uv);',
    '    if (u_type == 0) {',
    '        gl_FragColor = u_progress < 0.5 ? a : b;',
    '    } else if (u_type == 1) {',
    '        float alpha = smoothstep(0.0, 1.0, u_progress);',
    '        gl_FragColor = mix(a, b, alpha);',
    '    } else {',
    '        float p = u_progress;',
    '        float aFade = 1.0 - smoothstep(0.0, 0.6, p);',
    '        float bFade = smoothstep(0.4, 1.0, p);',
    '        gl_FragColor = a * aFade + b * bFade;',
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

/* ======================================================================
 * createVideoRenderer(canvas) — single-source video renderer
 * ====================================================================== */

function createVideoRenderer(canvas) {
    var gl = canvas.getContext('webgl2', { antialias: false, alpha: false, premultipliedAlpha: false });
    if (!gl) gl = canvas.getContext('webgl', { antialias: false, alpha: false, premultipliedAlpha: false });
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
 * drawVideoFrame(renderer, source) — upload frame as texture + draw
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
 * createTransitionRenderer(canvas) — dual-source with transition
 * ====================================================================== */

function createTransitionRenderer(canvas) {
    var gl = canvas.getContext('webgl2', { antialias: false, alpha: false, premultipliedAlpha: false });
    if (!gl) gl = canvas.getContext('webgl', { antialias: false, alpha: false, premultipliedAlpha: false });
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
        destroyed: false
    };
}

/* ======================================================================
 * drawTransition(renderer, srcA, srcB, progress, type)
 *   type: 'cut' | 'fade' | 'dissolve'
 *   progress: 0.0 (full A) to 1.0 (full B)
 * ====================================================================== */

var TRANSITION_MAP = { 'cut': 0, 'fade': 1, 'dissolve': 2 };

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
    gl.uniform1i(renderer.uType, TRANSITION_MAP[type] || 0);

    bindQuad(gl, renderer.quad);
    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
}

/* ======================================================================
 * destroyRenderer(renderer) — clean up GL resources
 * ====================================================================== */

function destroyRenderer(renderer) {
    if (!renderer || renderer.destroyed) return;
    renderer.destroyed = true;
    var gl = renderer.gl;
    if (renderer.tex) gl.deleteTexture(renderer.tex);
    if (renderer.texA) gl.deleteTexture(renderer.texA);
    if (renderer.texB) gl.deleteTexture(renderer.texB);
    if (renderer.quad && renderer.quad.vbo) gl.deleteBuffer(renderer.quad.vbo);
    if (renderer.prog) gl.deleteProgram(renderer.prog);
    var ext = gl.getExtension('WEBGL_lose_context');
    if (ext) ext.loseContext();
}

/* ======================================================================
 * Public API
 * ====================================================================== */

window.Mc1WebGLVideo = {
    createVideoRenderer: createVideoRenderer,
    drawVideoFrame: drawVideoFrame,
    createTransitionRenderer: createTransitionRenderer,
    drawTransition: drawTransition,
    destroyRenderer: destroyRenderer,
    TRANSITION_TYPES: ['cut', 'fade', 'dissolve']
};

})();
