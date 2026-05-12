/**
 * Mcaster1 Captions Engine
 * @version 2.0.1
 * js/captions-engine.js
 *
 * Caption management: live transcription via Whisper/Ollama, SRT/VTT
 * import/export, cue editing, and 2D canvas rendering for WebGL overlay.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

(function() {
'use strict';

/* ======================================================================
 * Cue: a single caption entry with start/end time and text
 * ====================================================================== */

function Cue(start, end, text) {
    this.start = start;   // seconds (float)
    this.end   = end;     // seconds (float)
    this.text  = text;    // string (may contain line breaks)
}

/* ======================================================================
 * CaptionsEngine
 * ====================================================================== */

function CaptionsEngine(options) {
    options = options || {};

    this.cues = [];               // Array of Cue
    this.language = options.language || 'en';

    /* Live transcription state */
    this._liveActive   = false;
    this._mediaStream  = null;
    this._audioCtx     = null;
    this._sourceNode   = null;
    this._scriptNode   = null;
    this._chunkBuffer  = [];
    this._chunkStart   = 0;
    this._chunkIntervalMs = (options.chunkIntervalMs || 5000);
    this._chunkTimer   = null;
    this._timeOffset   = 0;       // cumulative time offset for live cues

    /* Caption rendering style */
    this.style = {
        fontFamily:   options.fontFamily   || 'Arial, Helvetica, sans-serif',
        fontSize:     options.fontSize     || 28,
        fontColor:    options.fontColor    || '#FFFFFF',
        bgColor:      options.bgColor      || 'rgba(0,0,0,0.70)',
        bgPadding:    options.bgPadding    || 8,
        outlineColor: options.outlineColor || '#000000',
        outlineWidth: options.outlineWidth || 2,
        position:     options.position     || 'bottom',  // 'bottom' or 'top'
        maxWidth:     options.maxWidth     || 0.9         // fraction of canvas width
    };

    /* Callback fired when new live cue arrives */
    this.onCueAdded = options.onCueAdded || null;
}

/* -- Cue management -------------------------------------------------- */

CaptionsEngine.prototype.addCue = function(start, end, text) {
    var cue = new Cue(start, end, text);
    this.cues.push(cue);
    this._sortCues();
    return cue;
};

CaptionsEngine.prototype.editCue = function(index, text) {
    if (index >= 0 && index < this.cues.length) {
        this.cues[index].text = text;
    }
};

CaptionsEngine.prototype.deleteCue = function(index) {
    if (index >= 0 && index < this.cues.length) {
        this.cues.splice(index, 1);
    }
};

CaptionsEngine.prototype.getCueAtTime = function(time) {
    for (var i = 0; i < this.cues.length; i++) {
        if (time >= this.cues[i].start && time <= this.cues[i].end) {
            return this.cues[i];
        }
    }
    return null;
};

CaptionsEngine.prototype.getCueIndexAtTime = function(time) {
    for (var i = 0; i < this.cues.length; i++) {
        if (time >= this.cues[i].start && time <= this.cues[i].end) {
            return i;
        }
    }
    return -1;
};

CaptionsEngine.prototype.clearCues = function() {
    this.cues = [];
};

CaptionsEngine.prototype._sortCues = function() {
    this.cues.sort(function(a, b) { return a.start - b.start; });
};

/* -- SRT parsing / export -------------------------------------------- */

CaptionsEngine.prototype.loadSRT = function(srtText) {
    this.cues = [];
    if (!srtText || typeof srtText !== 'string') return;

    var blocks = srtText.replace(/\r\n/g, '\n').split(/\n\n+/);
    for (var i = 0; i < blocks.length; i++) {
        var lines = blocks[i].trim().split('\n');
        if (lines.length < 2) continue;

        /* Find the timestamp line (may be line 1 or line 0 if index is missing) */
        var tsLine = -1;
        for (var j = 0; j < Math.min(lines.length, 2); j++) {
            if (lines[j].indexOf('-->') !== -1) { tsLine = j; break; }
        }
        if (tsLine < 0) continue;

        var times = lines[tsLine].split('-->');
        if (times.length < 2) continue;
        var start = this._parseSrtTime(times[0].trim());
        var end   = this._parseSrtTime(times[1].trim());
        if (start < 0 || end < 0) continue;

        var textLines = lines.slice(tsLine + 1);
        var text = textLines.join('\n').trim();
        if (text) {
            this.cues.push(new Cue(start, end, text));
        }
    }
    this._sortCues();
};

CaptionsEngine.prototype.exportSRT = function() {
    var out = '';
    for (var i = 0; i < this.cues.length; i++) {
        var c = this.cues[i];
        out += (i + 1) + '\n';
        out += this._fmtSrtTime(c.start) + ' --> ' + this._fmtSrtTime(c.end) + '\n';
        out += c.text + '\n\n';
    }
    return out;
};

CaptionsEngine.prototype._parseSrtTime = function(s) {
    /* 00:01:23,456 or 00:01:23.456 */
    s = s.replace(',', '.');
    var m = s.match(/(\d+):(\d+):(\d+)\.?(\d*)/);
    if (!m) return -1;
    return parseInt(m[1]) * 3600 + parseInt(m[2]) * 60 + parseInt(m[3])
         + (m[4] ? parseInt(m[4].substring(0,3).padEnd(3,'0')) / 1000 : 0);
};

CaptionsEngine.prototype._fmtSrtTime = function(sec) {
    var h  = Math.floor(sec / 3600);
    var mi = Math.floor((sec % 3600) / 60);
    var s  = Math.floor(sec % 60);
    var ms = Math.round((sec - Math.floor(sec)) * 1000);
    return pad2(h) + ':' + pad2(mi) + ':' + pad2(s) + ',' + pad3(ms);
};

/* -- VTT parsing / export -------------------------------------------- */

CaptionsEngine.prototype.loadVTT = function(vttText) {
    this.cues = [];
    if (!vttText || typeof vttText !== 'string') return;

    /* Strip the WEBVTT header */
    var body = vttText.replace(/\r\n/g, '\n');
    var headerEnd = body.indexOf('\n\n');
    if (headerEnd >= 0) body = body.substring(headerEnd + 2);

    var blocks = body.split(/\n\n+/);
    for (var i = 0; i < blocks.length; i++) {
        var lines = blocks[i].trim().split('\n');
        if (lines.length < 1) continue;

        var tsLine = -1;
        for (var j = 0; j < Math.min(lines.length, 2); j++) {
            if (lines[j].indexOf('-->') !== -1) { tsLine = j; break; }
        }
        if (tsLine < 0) continue;

        var parts = lines[tsLine].split('-->');
        if (parts.length < 2) continue;
        var start = this._parseVttTime(parts[0].trim());
        var end   = this._parseVttTime(parts[1].trim().split(/\s/)[0]);
        if (start < 0 || end < 0) continue;

        var text = lines.slice(tsLine + 1).join('\n').trim();
        /* Strip VTT formatting tags for plain text */
        text = text.replace(/<[^>]+>/g, '');
        if (text) {
            this.cues.push(new Cue(start, end, text));
        }
    }
    this._sortCues();
};

CaptionsEngine.prototype.exportVTT = function() {
    var out = 'WEBVTT\n\n';
    for (var i = 0; i < this.cues.length; i++) {
        var c = this.cues[i];
        out += this._fmtVttTime(c.start) + ' --> ' + this._fmtVttTime(c.end) + '\n';
        out += c.text + '\n\n';
    }
    return out;
};

CaptionsEngine.prototype._parseVttTime = function(s) {
    /* 00:01:23.456 or 01:23.456 */
    var m = s.match(/(?:(\d+):)?(\d+):(\d+)\.?(\d*)/);
    if (!m) return -1;
    var h = m[1] ? parseInt(m[1]) : 0;
    return h * 3600 + parseInt(m[2]) * 60 + parseInt(m[3])
         + (m[4] ? parseInt(m[4].substring(0,3).padEnd(3,'0')) / 1000 : 0);
};

CaptionsEngine.prototype._fmtVttTime = function(sec) {
    var h  = Math.floor(sec / 3600);
    var mi = Math.floor((sec % 3600) / 60);
    var s  = Math.floor(sec % 60);
    var ms = Math.round((sec - Math.floor(sec)) * 1000);
    return pad2(h) + ':' + pad2(mi) + ':' + pad2(s) + '.' + pad3(ms);
};

/* -- Live transcription ---------------------------------------------- */

CaptionsEngine.prototype.startLiveTranscription = function(audioStream) {
    if (this._liveActive) return;
    var self = this;
    this._liveActive = true;
    this._mediaStream = audioStream;
    this._chunkBuffer = [];
    this._timeOffset = 0;
    this._chunkStart = Date.now();

    try {
        this._audioCtx = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: 16000 });
        this._sourceNode = this._audioCtx.createMediaStreamSource(audioStream);

        /* We use ScriptProcessorNode (deprecated but universally supported)
           for capturing raw PCM data in chunks */
        var bufSize = 4096;
        this._scriptNode = this._audioCtx.createScriptProcessor(bufSize, 1, 1);
        this._scriptNode.onaudioprocess = function(e) {
            if (!self._liveActive) return;
            var input = e.inputBuffer.getChannelData(0);
            self._chunkBuffer.push(new Float32Array(input));
        };

        this._sourceNode.connect(this._scriptNode);
        this._scriptNode.connect(this._audioCtx.destination);

        /* Send chunks on interval */
        this._chunkTimer = setInterval(function() {
            self._sendChunk();
        }, this._chunkIntervalMs);

    } catch (e) {
        console.error('CaptionsEngine: failed to start live transcription', e);
        this._liveActive = false;
    }
};

CaptionsEngine.prototype.stopLiveTranscription = function() {
    this._liveActive = false;
    if (this._chunkTimer) {
        clearInterval(this._chunkTimer);
        this._chunkTimer = null;
    }
    /* Send any remaining buffer */
    if (this._chunkBuffer.length > 0) {
        this._sendChunk();
    }
    if (this._scriptNode) {
        try { this._scriptNode.disconnect(); } catch (e) {}
        this._scriptNode = null;
    }
    if (this._sourceNode) {
        try { this._sourceNode.disconnect(); } catch (e) {}
        this._sourceNode = null;
    }
    if (this._audioCtx) {
        try { this._audioCtx.close(); } catch (e) {}
        this._audioCtx = null;
    }
    this._mediaStream = null;
};

CaptionsEngine.prototype._sendChunk = function() {
    if (this._chunkBuffer.length === 0) return;

    var chunkDuration = (Date.now() - this._chunkStart) / 1000;
    var chunkStartTime = this._timeOffset;
    this._timeOffset += chunkDuration;
    this._chunkStart = Date.now();

    /* Merge all recorded Float32 arrays into one */
    var totalLen = 0;
    for (var i = 0; i < this._chunkBuffer.length; i++) {
        totalLen += this._chunkBuffer[i].length;
    }
    var merged = new Float32Array(totalLen);
    var offset = 0;
    for (var j = 0; j < this._chunkBuffer.length; j++) {
        merged.set(this._chunkBuffer[j], offset);
        offset += this._chunkBuffer[j].length;
    }
    this._chunkBuffer = [];

    /* Convert Float32 PCM to 16-bit WAV */
    var wavBlob = this._float32ToWav(merged, 16000);

    /* Read as base64 and POST to server */
    var self = this;
    var reader = new FileReader();
    reader.onload = function() {
        var base64 = reader.result.split(',')[1];
        if (!base64) return;

        var apiCall = (typeof mc1Api === 'function') ? mc1Api : function(method, url, body) {
            return fetch(url, {
                method: method,
                headers: {'Content-Type': 'application/json'},
                credentials: 'same-origin',
                body: body ? JSON.stringify(body) : undefined
            }).then(function(r) { return r.json(); });
        };

        apiCall('POST', '/app/api/captions.php', {
            action: 'transcribe_chunk',
            wav_base64: base64,
            language: self.language,
            offset_sec: chunkStartTime
        }).then(function(d) {
            if (d && d.segments && d.segments.length > 0) {
                for (var s = 0; s < d.segments.length; s++) {
                    var seg = d.segments[s];
                    var cue = self.addCue(
                        seg.start + chunkStartTime,
                        seg.end + chunkStartTime,
                        seg.text
                    );
                    if (self.onCueAdded) {
                        self.onCueAdded(cue, self.cues.length - 1);
                    }
                }
            }
        }).catch(function(e) {
            console.warn('CaptionsEngine: transcription request failed', e);
        });
    };
    reader.readAsDataURL(wavBlob);
};

CaptionsEngine.prototype._float32ToWav = function(samples, sampleRate) {
    var numChannels = 1;
    var bitsPerSample = 16;
    var bytesPerSample = bitsPerSample / 8;
    var blockAlign = numChannels * bytesPerSample;
    var dataLen = samples.length * bytesPerSample;
    var buffer = new ArrayBuffer(44 + dataLen);
    var view = new DataView(buffer);

    /* RIFF header */
    writeStr(view, 0, 'RIFF');
    view.setUint32(4, 36 + dataLen, true);
    writeStr(view, 8, 'WAVE');

    /* fmt chunk */
    writeStr(view, 12, 'fmt ');
    view.setUint32(16, 16, true);
    view.setUint16(20, 1, true);     // PCM
    view.setUint16(22, numChannels, true);
    view.setUint32(24, sampleRate, true);
    view.setUint32(28, sampleRate * blockAlign, true);
    view.setUint16(32, blockAlign, true);
    view.setUint16(34, bitsPerSample, true);

    /* data chunk */
    writeStr(view, 36, 'data');
    view.setUint32(40, dataLen, true);

    var off = 44;
    for (var i = 0; i < samples.length; i++) {
        var s = Math.max(-1, Math.min(1, samples[i]));
        view.setInt16(off, s < 0 ? s * 0x8000 : s * 0x7FFF, true);
        off += 2;
    }

    return new Blob([buffer], { type: 'audio/wav' });
};

/* -- Caption rendering (2D canvas for WebGL overlay) ----------------- */

/**
 * renderCaptionFrame(canvas2d, text, styleOverrides)
 *
 * Draws caption text onto a 2D canvas element. The canvas is then
 * uploaded as a texture by the WebGL overlay compositor.
 *
 * @param {HTMLCanvasElement} canvas2d  - offscreen canvas for text rendering
 * @param {string} text                - caption text to display
 * @param {Object} styleOverrides      - optional style overrides
 */
CaptionsEngine.prototype.renderCaptionFrame = function(canvas2d, text, styleOverrides) {
    if (!canvas2d || !text) {
        if (canvas2d) {
            var cx = canvas2d.getContext('2d');
            cx.clearRect(0, 0, canvas2d.width, canvas2d.height);
        }
        return;
    }

    var s = {};
    for (var k in this.style) { s[k] = this.style[k]; }
    if (styleOverrides) {
        for (var sk in styleOverrides) { s[sk] = styleOverrides[sk]; }
    }

    var ctx = canvas2d.getContext('2d');
    ctx.clearRect(0, 0, canvas2d.width, canvas2d.height);

    ctx.font = 'bold ' + s.fontSize + 'px ' + s.fontFamily;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';

    var maxW = canvas2d.width * s.maxWidth;
    var lines = this._wrapText(ctx, text, maxW);
    var lineH = s.fontSize * 1.3;
    var blockH = lines.length * lineH;
    var pad = s.bgPadding;

    /* Background rectangle */
    var bgW = 0;
    for (var i = 0; i < lines.length; i++) {
        var lw = ctx.measureText(lines[i]).width;
        if (lw > bgW) bgW = lw;
    }
    bgW += pad * 2;
    var bgH = blockH + pad * 2;
    var bgX = (canvas2d.width - bgW) / 2;
    var bgY;
    if (s.position === 'top') {
        bgY = pad;
    } else {
        bgY = canvas2d.height - bgH - pad;
    }

    /* Draw semi-transparent background */
    ctx.fillStyle = s.bgColor;
    roundRect(ctx, bgX, bgY, bgW, bgH, 6);
    ctx.fill();

    /* Draw text lines */
    var textX = canvas2d.width / 2;
    var textY = bgY + pad + lineH / 2;

    for (var li = 0; li < lines.length; li++) {
        /* Outline */
        if (s.outlineWidth > 0) {
            ctx.strokeStyle = s.outlineColor;
            ctx.lineWidth = s.outlineWidth;
            ctx.lineJoin = 'round';
            ctx.strokeText(lines[li], textX, textY);
        }
        /* Fill */
        ctx.fillStyle = s.fontColor;
        ctx.fillText(lines[li], textX, textY);
        textY += lineH;
    }
};

CaptionsEngine.prototype._wrapText = function(ctx, text, maxW) {
    /* Split on explicit newlines first */
    var paragraphs = text.split('\n');
    var result = [];
    for (var p = 0; p < paragraphs.length; p++) {
        var words = paragraphs[p].split(' ');
        var line = '';
        for (var w = 0; w < words.length; w++) {
            var test = line ? line + ' ' + words[w] : words[w];
            if (ctx.measureText(test).width > maxW && line) {
                result.push(line);
                line = words[w];
            } else {
                line = test;
            }
        }
        if (line) result.push(line);
    }
    return result.length > 0 ? result : [''];
};

/* -- Utility helpers ------------------------------------------------- */

function pad2(n) { return n < 10 ? '0' + n : '' + n; }
function pad3(n) {
    if (n < 10)  return '00' + n;
    if (n < 100) return '0' + n;
    return '' + n;
}

function writeStr(view, offset, str) {
    for (var i = 0; i < str.length; i++) {
        view.setUint8(offset + i, str.charCodeAt(i));
    }
}

function roundRect(ctx, x, y, w, h, r) {
    ctx.beginPath();
    ctx.moveTo(x + r, y);
    ctx.lineTo(x + w - r, y);
    ctx.quadraticCurveTo(x + w, y, x + w, y + r);
    ctx.lineTo(x + w, y + h - r);
    ctx.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
    ctx.lineTo(x + r, y + h);
    ctx.quadraticCurveTo(x, y + h, x, y + h - r);
    ctx.lineTo(x, y + r);
    ctx.quadraticCurveTo(x, y, x + r, y);
    ctx.closePath();
}

/* -- Expose ---------------------------------------------------------- */

window.Mc1CaptionsEngine = {
    CaptionsEngine: CaptionsEngine,
    Cue: Cue
};

})();
