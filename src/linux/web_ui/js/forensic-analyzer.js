/**
 * forensic-analyzer.js — Forensic Audio Analysis Engine
 *
 * File:    src/linux/web_ui/js/forensic-analyzer.js
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Phase:   FA-1
 * Purpose: We provide a complete forensic audio analysis engine with offline FFT
 *          computation, configurable window functions, multiple frequency scales,
 *          region selection, variable-speed playback with band-pass filtering,
 *          annotations, and AI analysis integration via Ollama.
 *
 * Standards:
 *  - We use Web Audio API for decoding, playback, and filtering
 *  - We use OfflineAudioContext for pre-computing full spectrogram FFT data
 *  - We implement all window functions in JS (multiply before FFT)
 *  - We render via HQSpectrogram (WebGL 2.0) from webgl-spectrogram-hq.js
 *  - We never call exit()/die() in associated PHP
 *  - We use mc1Api() for server communication
 */

/* global mc1Api, mc1Toast, HQSpectrogram */

function ForensicAnalyzer(opts) {
    var self = this;

    /* ── Canvas refs ── */
    self.specCanvas  = document.getElementById(opts.spectrogramCanvasId);
    self.overlayCanvas = document.getElementById(opts.overlayCanvasId);
    self.waveCanvas   = document.getElementById(opts.waveformCanvasId);
    self.minimapCanvas = document.getElementById(opts.minimapCanvasId);

    /* ── Audio state ── */
    self.audioCtx    = null;
    self.audioBuffer = null;
    self.sourceNode  = null;
    self.gainNode    = null;
    self.filterNode  = null;
    self.isPlaying   = false;
    self.isLooping   = false;
    self.isReverse   = false;
    self.playbackRate = 1.0;
    self.startTime   = 0;
    self.startOffset = 0;

    /* ── File info ── */
    self.fileName    = '';
    self.filePath    = '';
    self.fileHash    = '';
    self.sampleRate  = 44100;
    self.channels    = 2;
    self.bitDepth    = 16;
    self.duration    = 0;

    /* ── FFT settings ── */
    self.fftSize     = 4096;
    self.windowType  = 'hann';
    self.freqScale   = 'log';
    self.hopRatio    = 0.5;

    /* ── Spectrogram data ── */
    self.spectrogramData = null;  /* Float32Array: rows(freq) x cols(time) in dB */
    self.specWidth   = 0;
    self.specHeight  = 0;

    /* ── HQ renderer ── */
    self.hqSpec      = null;
    self.minimapSpec  = null;

    /* ── Selection ── */
    self.selStartTime = -1;
    self.selEndTime   = -1;
    self.selStartFreq = -1;
    self.selEndFreq   = -1;
    self.isDragging   = false;
    self.isPanning    = false;
    self.dragStartX   = 0;
    self.dragStartY   = 0;

    /* ── Annotations ── */
    self.annotations = [];

    /* ── Recording ── */
    self.isRecording = false;
    self.mediaRecorder = null;
    self.recordedChunks = [];

    /* ── Compare mode ── */
    self.compareMode = false;

    /* ── Initialize ── */
    self._initCanvases();
    self._bindEvents();
}

/* ══════════════════════════════════════════════════════════════
 *  INITIALIZATION
 * ══════════════════════════════════════════════════════════════ */

ForensicAnalyzer.prototype._initCanvases = function() {
    var self = this;
    var wrap = document.getElementById('spectrogram-wrap');
    if (!wrap) return;

    var resize = function() {
        var w = wrap.clientWidth;
        var h = wrap.clientHeight;
        self.specCanvas.width = w;
        self.specCanvas.height = h;
        self.overlayCanvas.width = w;
        self.overlayCanvas.height = h;

        var waveWrap = document.getElementById('waveform-panel');
        if (waveWrap) {
            self.waveCanvas.width = waveWrap.clientWidth;
            self.waveCanvas.height = waveWrap.clientHeight;
        }

        var mmEl = document.getElementById('minimap');
        if (mmEl) {
            self.minimapCanvas.width = mmEl.clientWidth;
            self.minimapCanvas.height = mmEl.clientHeight;
        }

        if (self.hqSpec) self.hqSpec.draw();
        self._drawOverlay();
        self._drawWaveform();
        self._drawMinimap();
    };

    resize();
    window.addEventListener('resize', resize);
};

ForensicAnalyzer.prototype._bindEvents = function() {
    var self = this;
    var overlay = self.overlayCanvas;

    /* Mouse move — cursor readout */
    overlay.addEventListener('mousemove', function(e) {
        if (!self.hqSpec || !self.duration) return;
        var rect = overlay.getBoundingClientRect();
        var px = e.clientX - rect.left;
        var py = e.clientY - rect.top;

        var time = self.hqSpec.canvasToTime(px);
        var freq = self.hqSpec.canvasToFreq(py);
        var mag = self.hqSpec.getMagnitudeAt(time, freq);

        document.getElementById('cursor-time').textContent = self._fmtTime(time);
        document.getElementById('cursor-freq').textContent = Math.round(freq) + ' Hz';
        document.getElementById('cursor-mag').textContent = mag.toFixed(1) + ' dB';
        document.getElementById('cursor-readout').style.display = 'flex';

        if (self.isDragging) {
            self._updateSelection(px, py);
            self._drawOverlay();
        }
        if (self.isPanning) {
            var dx = (px - self.dragStartX) / overlay.width;
            var dy = (py - self.dragStartY) / overlay.height;
            self.hqSpec.pan(dx, -dy);
            self.dragStartX = px;
            self.dragStartY = py;
            self.hqSpec.draw();
            self._drawOverlay();
            self._drawMinimap();
        }
    });

    overlay.addEventListener('mouseleave', function() {
        document.getElementById('cursor-readout').style.display = 'none';
    });

    /* Mouse down — start selection or pan */
    overlay.addEventListener('mousedown', function(e) {
        if (!self.hqSpec || !self.duration) return;
        var rect = overlay.getBoundingClientRect();
        var px = e.clientX - rect.left;
        var py = e.clientY - rect.top;

        if (e.button === 1 || e.altKey) {
            /* Middle-click or alt-click: pan */
            self.isPanning = true;
            self.dragStartX = px;
            self.dragStartY = py;
            overlay.style.cursor = 'grabbing';
            e.preventDefault();
        } else if (e.button === 0 && !e.ctrlKey && !e.metaKey) {
            /* Left click: start selection */
            self.isDragging = true;
            self.dragStartX = px;
            self.dragStartY = py;
            self.selStartTime = self.hqSpec.canvasToTime(px);
            self.selStartFreq = self.hqSpec.canvasToFreq(py);
            self.selEndTime = self.selStartTime;
            self.selEndFreq = self.selStartFreq;
        } else if (e.button === 0 && (e.ctrlKey || e.metaKey)) {
            /* Ctrl-click: add annotation */
            var time = self.hqSpec.canvasToTime(px);
            var freq = self.hqSpec.canvasToFreq(py);
            self._showAnnotationModal(time, freq);
        }
    });

    overlay.addEventListener('mouseup', function(e) {
        if (self.isDragging) {
            self.isDragging = false;
            var rect = overlay.getBoundingClientRect();
            var px = e.clientX - rect.left;
            var py = e.clientY - rect.top;

            /* If it was a tiny drag (click), treat as annotation */
            var dist = Math.sqrt(Math.pow(px - self.dragStartX, 2) + Math.pow(py - self.dragStartY, 2));
            if (dist < 5) {
                self.selStartTime = -1;
                self.selEndTime = -1;
                document.getElementById('sel-info').style.display = 'none';
                self._drawOverlay();
            } else {
                self._updateSelectionInfo();
            }
        }
        if (self.isPanning) {
            self.isPanning = false;
            overlay.style.cursor = 'crosshair';
        }
    });

    /* Mouse wheel — zoom */
    overlay.addEventListener('wheel', function(e) {
        if (!self.hqSpec || !self.duration) return;
        e.preventDefault();
        var rect = overlay.getBoundingClientRect();
        var cx = (e.clientX - rect.left) / overlay.width;
        var cy = (e.clientY - rect.top) / overlay.height;
        var delta = e.deltaY > 0 ? 0.8 : 1.25;

        if (e.ctrlKey) {
            /* Ctrl+wheel: zoom frequency only */
            self.hqSpec.zoomAt(cx, cy, 1, delta);
        } else if (e.shiftKey) {
            /* Shift+wheel: zoom time only */
            self.hqSpec.zoomAt(cx, cy, delta, 1);
        } else {
            /* Normal: zoom both */
            self.hqSpec.zoomAt(cx, cy, delta, delta);
        }

        self.hqSpec.draw();
        self._drawOverlay();
        self._updateAxes();
        self._drawMinimap();
    }, { passive: false });

    /* Double-click: reset zoom */
    overlay.addEventListener('dblclick', function() {
        if (!self.hqSpec) return;
        self.hqSpec.resetView();
        self.hqSpec.draw();
        self._drawOverlay();
        self._updateAxes();
        self._drawMinimap();
    });
};

/* ══════════════════════════════════════════════════════════════
 *  FILE LOADING
 * ══════════════════════════════════════════════════════════════ */

ForensicAnalyzer.prototype.loadFile = function(file) {
    var self = this;
    self.fileName = file.name;
    self._showLoading('Decoding audio...');

    if (!self.audioCtx) {
        self.audioCtx = new (window.AudioContext || window.webkitAudioContext)();
    }

    var reader = new FileReader();
    reader.onload = function(ev) {
        self.audioCtx.decodeAudioData(ev.target.result, function(buffer) {
            self.audioBuffer = buffer;
            self.sampleRate = buffer.sampleRate;
            self.channels = buffer.numberOfChannels;
            self.duration = buffer.duration;
            self.bitDepth = 32; /* Web Audio always decodes to float32 */

            /* Update file info display */
            document.getElementById('file-info').innerHTML =
                '<strong>FILE:</strong> ' + self._esc(file.name)
                + ' | ' + buffer.sampleRate + 'Hz ' + self.channels + 'ch | '
                + self._fmtTime(buffer.duration);

            self._computeSpectrogram();
        }, function(err) {
            self._hideLoading();
            mc1Toast('Failed to decode audio: ' + (err.message || err), 'err');
        });
    };
    reader.readAsArrayBuffer(file);
};

/* ══════════════════════════════════════════════════════════════
 *  FFT COMPUTATION
 * ══════════════════════════════════════════════════════════════ */

ForensicAnalyzer.prototype._computeSpectrogram = function() {
    var self = this;
    if (!self.audioBuffer) return;
    self._showLoading('Computing spectrogram (FFT ' + self.fftSize + ')...');

    /* Get mono channel data */
    var rawData;
    if (self.audioBuffer.numberOfChannels > 1) {
        var ch0 = self.audioBuffer.getChannelData(0);
        var ch1 = self.audioBuffer.getChannelData(1);
        rawData = new Float32Array(ch0.length);
        for (var i = 0; i < ch0.length; i++) {
            rawData[i] = (ch0[i] + ch1[i]) * 0.5;
        }
    } else {
        rawData = self.audioBuffer.getChannelData(0);
    }

    var fftSize = self.fftSize;
    var hopSize = Math.floor(fftSize * self.hopRatio);
    var freqBins = fftSize / 2;
    var numFrames = Math.floor((rawData.length - fftSize) / hopSize) + 1;

    if (numFrames < 1) {
        self._hideLoading();
        mc1Toast('Audio too short for FFT size ' + fftSize, 'warn');
        return;
    }

    /* Pre-compute window function */
    var windowFn = self._computeWindow(fftSize, self.windowType);

    /* We use OfflineAudioContext + AnalyserNode for FFT */
    var offCtx = new OfflineAudioContext(1, rawData.length, self.sampleRate);
    var analyser = offCtx.createAnalyser();
    analyser.fftSize = Math.min(fftSize, 32768); /* AnalyserNode max is 32768 */

    /* For sizes > 32768, we do manual FFT using a chunked approach */
    if (fftSize > 32768) {
        self._computeSpectrogramManual(rawData, fftSize, hopSize, freqBins, numFrames, windowFn);
        return;
    }

    /* Use ScriptProcessorNode approach for frame-by-frame extraction */
    self._computeSpectrogramManual(rawData, fftSize, hopSize, freqBins, numFrames, windowFn);
};

/**
 * Manual FFT computation using a simple DFT for arbitrary sizes.
 * For performance on large FFTs we use the Cooley-Tukey radix-2 algorithm.
 */
ForensicAnalyzer.prototype._computeSpectrogramManual = function(rawData, fftSize, hopSize, freqBins, numFrames, windowFn) {
    var self = this;

    /* Limit frames for extremely long files */
    var maxFrames = 8192;
    if (numFrames > maxFrames) {
        hopSize = Math.floor((rawData.length - fftSize) / maxFrames);
        numFrames = maxFrames;
    }

    var specData = new Float32Array(freqBins * numFrames);
    var frame = 0;
    var chunkSize = 64; /* Frames per timeout chunk to keep UI responsive */

    var processChunk = function() {
        var end = Math.min(frame + chunkSize, numFrames);
        for (; frame < end; frame++) {
            var offset = frame * hopSize;
            /* Apply window and extract frame */
            var windowed = new Float32Array(fftSize);
            for (var i = 0; i < fftSize; i++) {
                var idx = offset + i;
                windowed[i] = (idx < rawData.length ? rawData[idx] : 0) * windowFn[i];
            }

            /* FFT via radix-2 Cooley-Tukey */
            var spectrum = self._fft(windowed);

            /* Convert to dB magnitude and store as row */
            for (var bin = 0; bin < freqBins; bin++) {
                var re = spectrum[bin * 2];
                var im = spectrum[bin * 2 + 1];
                var mag = Math.sqrt(re * re + im * im) / fftSize;
                var dB = mag > 0 ? 20 * Math.log10(mag) : -120;
                specData[bin * numFrames + frame] = dB;
            }
        }

        var pct = Math.round((frame / numFrames) * 100);
        document.getElementById('loading-text').textContent =
            'Computing spectrogram... ' + pct + '%';

        if (frame < numFrames) {
            setTimeout(processChunk, 0);
        } else {
            self._onSpectrogramReady(specData, numFrames, freqBins);
        }
    };

    setTimeout(processChunk, 10);
};

/** Radix-2 Cooley-Tukey FFT. Returns interleaved [re0, im0, re1, im1, ...] */
ForensicAnalyzer.prototype._fft = function(input) {
    var n = input.length;
    /* Pad to next power of 2 if needed */
    var m = 1;
    while (m < n) m <<= 1;

    var re = new Float32Array(m);
    var im = new Float32Array(m);
    for (var i = 0; i < n; i++) re[i] = input[i];

    /* Bit-reversal permutation */
    var bits = Math.log2(m);
    for (var j = 0; j < m; j++) {
        var rev = 0;
        for (var b = 0; b < bits; b++) {
            rev = (rev << 1) | ((j >> b) & 1);
        }
        if (rev > j) {
            var tmpR = re[j]; re[j] = re[rev]; re[rev] = tmpR;
            var tmpI = im[j]; im[j] = im[rev]; im[rev] = tmpI;
        }
    }

    /* FFT butterfly */
    for (var size = 2; size <= m; size *= 2) {
        var half = size / 2;
        var angle = -2 * Math.PI / size;
        var wRe = Math.cos(angle);
        var wIm = Math.sin(angle);

        for (var k = 0; k < m; k += size) {
            var twRe = 1, twIm = 0;
            for (var l = 0; l < half; l++) {
                var idx1 = k + l;
                var idx2 = k + l + half;
                var tRe = twRe * re[idx2] - twIm * im[idx2];
                var tIm = twRe * im[idx2] + twIm * re[idx2];
                re[idx2] = re[idx1] - tRe;
                im[idx2] = im[idx1] - tIm;
                re[idx1] = re[idx1] + tRe;
                im[idx1] = im[idx1] + tIm;
                var newTwRe = twRe * wRe - twIm * wIm;
                twIm = twRe * wIm + twIm * wRe;
                twRe = newTwRe;
            }
        }
    }

    /* Return interleaved */
    var result = new Float32Array(m * 2);
    for (var p = 0; p < m; p++) {
        result[p * 2] = re[p];
        result[p * 2 + 1] = im[p];
    }
    return result;
};

ForensicAnalyzer.prototype._onSpectrogramReady = function(specData, numFrames, freqBins) {
    var self = this;
    self.spectrogramData = specData;
    self.specWidth = numFrames;
    self.specHeight = freqBins;

    /* Initialize HQ renderer */
    if (!self.hqSpec) {
        self.hqSpec = new HQSpectrogram(self.specCanvas);
    }
    self.hqSpec.setSpectrogramData(specData);
    self.hqSpec.uploadSpectrogram(specData, numFrames, freqBins, self.duration, self.sampleRate, self.fftSize);
    self.hqSpec.draw();

    /* Draw waveform overview */
    self._drawWaveform();

    /* Update axes */
    self._updateAxes();

    /* Show minimap */
    document.getElementById('minimap').style.display = 'block';
    self._drawMinimap();

    self._hideLoading();
    mc1Toast('Spectrogram ready: ' + numFrames + ' frames, ' + freqBins + ' bins', 'ok');
};

/* ══════════════════════════════════════════════════════════════
 *  WINDOW FUNCTIONS
 * ══════════════════════════════════════════════════════════════ */

ForensicAnalyzer.prototype._computeWindow = function(size, type) {
    var w = new Float32Array(size);
    var n = size - 1;
    var PI = Math.PI;
    var TWO_PI = 2 * PI;

    switch (type) {
        case 'hann':
            for (var i = 0; i < size; i++) w[i] = 0.5 * (1 - Math.cos(TWO_PI * i / n));
            break;
        case 'hamming':
            for (var i = 0; i < size; i++) w[i] = 0.54 - 0.46 * Math.cos(TWO_PI * i / n);
            break;
        case 'blackman':
            for (var i = 0; i < size; i++) {
                w[i] = 0.42 - 0.5 * Math.cos(TWO_PI * i / n) + 0.08 * Math.cos(4 * PI * i / n);
            }
            break;
        case 'blackman-harris':
            for (var i = 0; i < size; i++) {
                w[i] = 0.35875
                     - 0.48829 * Math.cos(TWO_PI * i / n)
                     + 0.14128 * Math.cos(4 * PI * i / n)
                     - 0.01168 * Math.cos(6 * PI * i / n);
            }
            break;
        case 'kaiser':
            /* Kaiser with beta=6.0 (good spectral leakage suppression) */
            var beta = 6.0;
            var i0Beta = this._besselI0(beta);
            for (var i = 0; i < size; i++) {
                var r = 2.0 * i / n - 1.0;
                w[i] = this._besselI0(beta * Math.sqrt(1 - r * r)) / i0Beta;
            }
            break;
        case 'rectangular':
        default:
            for (var i = 0; i < size; i++) w[i] = 1.0;
            break;
    }
    return w;
};

/* Modified Bessel function I0 — needed for Kaiser window */
ForensicAnalyzer.prototype._besselI0 = function(x) {
    var sum = 1.0, term = 1.0;
    for (var k = 1; k <= 20; k++) {
        term *= (x * x) / (4.0 * k * k);
        sum += term;
        if (term < 1e-12) break;
    }
    return sum;
};

/* ══════════════════════════════════════════════════════════════
 *  SETTINGS
 * ══════════════════════════════════════════════════════════════ */

ForensicAnalyzer.prototype.setFFTSize = function(size) {
    this.fftSize = size;
    if (this.audioBuffer) this._computeSpectrogram();
};

ForensicAnalyzer.prototype.setWindow = function(type) {
    this.windowType = type;
    if (this.audioBuffer) this._computeSpectrogram();
};

ForensicAnalyzer.prototype.setColormap = function(name) {
    if (this.hqSpec) {
        this.hqSpec.setColormap(name);
        this.hqSpec.draw();
    }
};

ForensicAnalyzer.prototype.setFreqScale = function(scale) {
    this.freqScale = scale;
    this._updateAxes();
    if (this.hqSpec) this.hqSpec.draw();
};

ForensicAnalyzer.prototype.setGain = function(dB) {
    if (this.hqSpec) {
        this.hqSpec.setGain(dB);
        this.hqSpec.draw();
    }
};

ForensicAnalyzer.prototype.setFloor = function(dB) {
    if (this.hqSpec) {
        this.hqSpec.setFloor(dB);
        this.hqSpec.draw();
    }
};

ForensicAnalyzer.prototype.setHopRatio = function(ratio) {
    this.hopRatio = ratio;
    if (this.audioBuffer) this._computeSpectrogram();
};

/* ══════════════════════════════════════════════════════════════
 *  PLAYBACK
 * ══════════════════════════════════════════════════════════════ */

ForensicAnalyzer.prototype.togglePlay = function() {
    if (this.isPlaying) {
        this._stopPlayback();
    } else {
        this._startPlayback(false);
    }
};

ForensicAnalyzer.prototype._startPlayback = function(reverse) {
    var self = this;
    if (!self.audioBuffer || !self.audioCtx) return;

    if (self.audioCtx.state === 'suspended') {
        self.audioCtx.resume();
    }

    self.isReverse = reverse || false;
    self.isPlaying = true;
    document.getElementById('btn-play').innerHTML = '<i class="fa-solid fa-pause"></i>';

    var buffer = self.audioBuffer;
    if (self.isReverse) {
        buffer = self._reverseBuffer(self.audioBuffer);
    }

    self.sourceNode = self.audioCtx.createBufferSource();
    self.sourceNode.buffer = buffer;
    self.sourceNode.playbackRate.value = self.playbackRate;

    /* Build audio chain: source -> filter -> gain -> destination */
    self.gainNode = self.audioCtx.createGain();
    self.gainNode.gain.value = 1.0;

    var lastNode = self.sourceNode;

    /* Apply filter if set */
    var filterType = document.getElementById('ctl-filter').value;
    if (filterType !== 'none' && self.selStartFreq > 0 && self.selEndFreq > 0) {
        self.filterNode = self.audioCtx.createBiquadFilter();
        var loFreq = Math.min(self.selStartFreq, self.selEndFreq);
        var hiFreq = Math.max(self.selStartFreq, self.selEndFreq);
        var centerFreq = Math.sqrt(loFreq * hiFreq);
        var bandwidth = hiFreq - loFreq;

        if (filterType === 'bandpass') {
            self.filterNode.type = 'bandpass';
            self.filterNode.frequency.value = centerFreq;
            self.filterNode.Q.value = centerFreq / Math.max(1, bandwidth);
        } else if (filterType === 'lowpass') {
            self.filterNode.type = 'lowpass';
            self.filterNode.frequency.value = hiFreq;
            self.filterNode.Q.value = 1;
        } else if (filterType === 'highpass') {
            self.filterNode.type = 'highpass';
            self.filterNode.frequency.value = loFreq;
            self.filterNode.Q.value = 1;
        } else if (filterType === 'notch') {
            self.filterNode.type = 'notch';
            self.filterNode.frequency.value = centerFreq;
            self.filterNode.Q.value = centerFreq / Math.max(1, bandwidth);
        }
        lastNode.connect(self.filterNode);
        lastNode = self.filterNode;
    }

    lastNode.connect(self.gainNode);
    self.gainNode.connect(self.audioCtx.destination);

    /* Determine playback region */
    var startSec = 0;
    var durSec = buffer.duration;

    if (self.isLooping && self.selStartTime >= 0 && self.selEndTime >= 0) {
        startSec = Math.min(self.selStartTime, self.selEndTime);
        durSec = Math.abs(self.selEndTime - self.selStartTime);
        if (self.isReverse) {
            startSec = buffer.duration - Math.max(self.selStartTime, self.selEndTime);
        }
        self.sourceNode.loop = true;
        self.sourceNode.loopStart = startSec;
        self.sourceNode.loopEnd = startSec + durSec;
    }

    self.startTime = self.audioCtx.currentTime;
    self.startOffset = startSec;
    self.sourceNode.start(0, startSec, self.isLooping ? undefined : durSec);

    self.sourceNode.onended = function() {
        if (self.isPlaying) {
            self.isPlaying = false;
            document.getElementById('btn-play').innerHTML = '<i class="fa-solid fa-play"></i>';
        }
    };

    /* Playhead animation */
    self._animatePlayhead();
};

ForensicAnalyzer.prototype._stopPlayback = function() {
    if (this.sourceNode) {
        try { this.sourceNode.stop(); } catch (e) { /* already stopped */ }
        this.sourceNode.disconnect();
        this.sourceNode = null;
    }
    if (this.filterNode) {
        this.filterNode.disconnect();
        this.filterNode = null;
    }
    if (this.gainNode) {
        this.gainNode.disconnect();
        this.gainNode = null;
    }
    this.isPlaying = false;
    document.getElementById('btn-play').innerHTML = '<i class="fa-solid fa-play"></i>';
};

ForensicAnalyzer.prototype.stop = function() {
    this._stopPlayback();
    this.startOffset = 0;
    this._drawOverlay();
};

ForensicAnalyzer.prototype.seekStart = function() {
    var wasPlaying = this.isPlaying;
    this._stopPlayback();
    this.startOffset = 0;
    if (wasPlaying) this._startPlayback(this.isReverse);
};

ForensicAnalyzer.prototype.toggleLoop = function() {
    this.isLooping = !this.isLooping;
    var btn = document.getElementById('btn-loop');
    btn.classList.toggle('btn-primary', this.isLooping);
    btn.classList.toggle('btn-secondary', !this.isLooping);
};

ForensicAnalyzer.prototype.playReverse = function() {
    this._stopPlayback();
    this._startPlayback(true);
};

ForensicAnalyzer.prototype.setSpeed = function(rate) {
    this.playbackRate = rate;
    if (this.sourceNode) {
        this.sourceNode.playbackRate.value = rate;
    }
};

ForensicAnalyzer.prototype.setFilter = function(type) {
    /* Re-apply on next playback start */
    if (this.isPlaying) {
        this._stopPlayback();
        this._startPlayback(this.isReverse);
    }
};

ForensicAnalyzer.prototype._reverseBuffer = function(buffer) {
    var ctx = this.audioCtx;
    var reversed = ctx.createBuffer(buffer.numberOfChannels, buffer.length, buffer.sampleRate);
    for (var ch = 0; ch < buffer.numberOfChannels; ch++) {
        var src = buffer.getChannelData(ch);
        var dst = reversed.getChannelData(ch);
        for (var i = 0; i < src.length; i++) {
            dst[i] = src[src.length - 1 - i];
        }
    }
    return reversed;
};

ForensicAnalyzer.prototype._animatePlayhead = function() {
    var self = this;
    if (!self.isPlaying || !self.hqSpec) return;

    var elapsed = (self.audioCtx.currentTime - self.startTime) * self.playbackRate;
    var pos = self.startOffset + elapsed;

    /* Draw playhead on overlay */
    self._drawOverlay(pos);

    requestAnimationFrame(function() { self._animatePlayhead(); });
};

/* ══════════════════════════════════════════════════════════════
 *  SELECTION
 * ══════════════════════════════════════════════════════════════ */

ForensicAnalyzer.prototype._updateSelection = function(px, py) {
    if (!this.hqSpec) return;
    this.selEndTime = this.hqSpec.canvasToTime(px);
    this.selEndFreq = this.hqSpec.canvasToFreq(py);
};

ForensicAnalyzer.prototype._updateSelectionInfo = function() {
    if (this.selStartTime < 0 || this.selEndTime < 0) return;

    var t0 = Math.min(this.selStartTime, this.selEndTime);
    var t1 = Math.max(this.selStartTime, this.selEndTime);
    var f0 = Math.min(this.selStartFreq, this.selEndFreq);
    var f1 = Math.max(this.selStartFreq, this.selEndFreq);

    document.getElementById('sel-time-range').textContent = this._fmtTime(t0) + ' - ' + this._fmtTime(t1);
    document.getElementById('sel-freq-range').textContent = Math.round(f0) + ' - ' + Math.round(f1) + ' Hz';

    /* Compute peak and average in selection */
    if (this.hqSpec && this.spectrogramData) {
        var peak = -120, sum = 0, count = 0;
        var colStart = Math.floor((t0 / this.duration) * this.specWidth);
        var colEnd = Math.ceil((t1 / this.duration) * this.specWidth);
        var nyquist = this.sampleRate / 2;
        var rowStart = Math.floor((f0 / nyquist) * this.specHeight);
        var rowEnd = Math.ceil((f1 / nyquist) * this.specHeight);
        colStart = Math.max(0, Math.min(this.specWidth - 1, colStart));
        colEnd = Math.max(0, Math.min(this.specWidth - 1, colEnd));
        rowStart = Math.max(0, Math.min(this.specHeight - 1, rowStart));
        rowEnd = Math.max(0, Math.min(this.specHeight - 1, rowEnd));

        for (var r = rowStart; r <= rowEnd; r++) {
            for (var c = colStart; c <= colEnd; c++) {
                var val = this.spectrogramData[r * this.specWidth + c];
                if (val > peak) peak = val;
                sum += val;
                count++;
            }
        }
        var avg = count > 0 ? sum / count : -120;
        document.getElementById('sel-peak').textContent = peak.toFixed(1) + ' dB';
        document.getElementById('sel-avg').textContent = avg.toFixed(1) + ' dB';
    }

    document.getElementById('sel-info').style.display = 'block';
};

ForensicAnalyzer.prototype.clearSelection = function() {
    this.selStartTime = -1;
    this.selEndTime = -1;
    this.selStartFreq = -1;
    this.selEndFreq = -1;
    document.getElementById('sel-info').style.display = 'none';
    this._drawOverlay();
};

/* ══════════════════════════════════════════════════════════════
 *  DRAWING
 * ══════════════════════════════════════════════════════════════ */

ForensicAnalyzer.prototype._drawOverlay = function(playheadPos) {
    var self = this;
    var canvas = self.overlayCanvas;
    var ctx = canvas.getContext('2d');
    ctx.clearRect(0, 0, canvas.width, canvas.height);

    if (!self.hqSpec || !self.duration) return;

    /* Draw selection rectangle */
    if (self.selStartTime >= 0 && self.selEndTime >= 0) {
        var x0 = self.hqSpec.timeToCanvas(Math.min(self.selStartTime, self.selEndTime));
        var x1 = self.hqSpec.timeToCanvas(Math.max(self.selStartTime, self.selEndTime));
        var y0 = self.hqSpec.freqToCanvas(Math.max(self.selStartFreq, self.selEndFreq));
        var y1 = self.hqSpec.freqToCanvas(Math.min(self.selStartFreq, self.selEndFreq));

        ctx.fillStyle = 'rgba(20, 184, 166, 0.12)';
        ctx.fillRect(x0, y0, x1 - x0, y1 - y0);
        ctx.strokeStyle = 'rgba(20, 184, 166, 0.6)';
        ctx.lineWidth = 1;
        ctx.strokeRect(x0, y0, x1 - x0, y1 - y0);
    }

    /* Draw playhead */
    if (playheadPos !== undefined && playheadPos >= 0) {
        var px = self.hqSpec.timeToCanvas(playheadPos);
        if (px >= 0 && px <= canvas.width) {
            ctx.strokeStyle = '#ef4444';
            ctx.lineWidth = 1.5;
            ctx.beginPath();
            ctx.moveTo(px, 0);
            ctx.lineTo(px, canvas.height);
            ctx.stroke();
        }
    }

    /* Draw annotation markers */
    for (var i = 0; i < self.annotations.length; i++) {
        var a = self.annotations[i];
        var ax = self.hqSpec.timeToCanvas(a.time);
        var ay = self.hqSpec.freqToCanvas(a.freq);

        /* Marker dot */
        ctx.beginPath();
        ctx.arc(ax, ay, 5, 0, Math.PI * 2);
        ctx.fillStyle = a.color || '#14b8a6';
        ctx.fill();
        ctx.strokeStyle = '#fff';
        ctx.lineWidth = 1;
        ctx.stroke();

        /* Label */
        if (a.note) {
            ctx.font = '10px -apple-system, sans-serif';
            ctx.fillStyle = '#fff';
            ctx.fillText(a.note.substring(0, 30), ax + 8, ay + 3);
        }
    }
};

ForensicAnalyzer.prototype._drawWaveform = function() {
    var self = this;
    if (!self.audioBuffer) return;
    var canvas = self.waveCanvas;
    var ctx = canvas.getContext('2d');
    var w = canvas.width;
    var h = canvas.height;
    ctx.clearRect(0, 0, w, h);

    ctx.fillStyle = 'rgba(15, 23, 42, 1)';
    ctx.fillRect(0, 0, w, h);

    var data = self.audioBuffer.getChannelData(0);
    var step = Math.floor(data.length / w);
    var mid = h / 2;

    ctx.strokeStyle = 'rgba(20, 184, 166, 0.6)';
    ctx.lineWidth = 1;
    ctx.beginPath();

    for (var i = 0; i < w; i++) {
        var start = i * step;
        var min = 0, max = 0;
        for (var j = 0; j < step && start + j < data.length; j++) {
            var val = data[start + j];
            if (val < min) min = val;
            if (val > max) max = val;
        }
        ctx.moveTo(i, mid + min * mid);
        ctx.lineTo(i, mid + max * mid);
    }
    ctx.stroke();

    /* Center line */
    ctx.strokeStyle = 'rgba(100, 116, 139, 0.3)';
    ctx.beginPath();
    ctx.moveTo(0, mid);
    ctx.lineTo(w, mid);
    ctx.stroke();
};

ForensicAnalyzer.prototype._drawMinimap = function() {
    var self = this;
    if (!self.hqSpec || !self.duration) return;
    var canvas = self.minimapCanvas;
    var ctx = canvas.getContext('2d');
    ctx.clearRect(0, 0, canvas.width, canvas.height);

    /* Simple minimap: draw reduced waveform */
    if (self.audioBuffer) {
        var data = self.audioBuffer.getChannelData(0);
        var step = Math.floor(data.length / canvas.width);
        var mid = canvas.height / 2;
        ctx.strokeStyle = 'rgba(20, 184, 166, 0.4)';
        ctx.lineWidth = 1;
        ctx.beginPath();
        for (var i = 0; i < canvas.width; i++) {
            var s = i * step;
            var mn = 0, mx = 0;
            for (var j = 0; j < step && s + j < data.length; j++) {
                if (data[s + j] < mn) mn = data[s + j];
                if (data[s + j] > mx) mx = data[s + j];
            }
            ctx.moveTo(i, mid + mn * mid);
            ctx.lineTo(i, mid + mx * mid);
        }
        ctx.stroke();
    }

    /* Viewport rectangle */
    var vp = self.hqSpec.getViewport();
    var vpEl = document.getElementById('minimap-viewport');
    if (vpEl) {
        vpEl.style.left = (vp.xFrac * 100) + '%';
        vpEl.style.width = (vp.wFrac * 100) + '%';
    }
};

ForensicAnalyzer.prototype._updateAxes = function() {
    var self = this;
    if (!self.hqSpec || !self.duration) return;

    /* Frequency axis */
    var freqAxis = document.getElementById('freq-axis');
    var nyquist = self.sampleRate / 2;
    var freqs;

    if (self.freqScale === 'log') {
        freqs = [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000];
    } else if (self.freqScale === 'mel') {
        freqs = [100, 300, 600, 1000, 2000, 4000, 8000, 16000];
    } else if (self.freqScale === 'bark') {
        freqs = [100, 300, 630, 1080, 1720, 2700, 4400, 7700, 15500];
    } else {
        var step = nyquist / 8;
        freqs = [];
        for (var f = 0; f <= nyquist; f += step) freqs.push(Math.round(f));
    }

    var html = '';
    for (var i = freqs.length - 1; i >= 0; i--) {
        if (freqs[i] <= nyquist) {
            var label = freqs[i] >= 1000 ? (freqs[i] / 1000) + 'k' : freqs[i] + '';
            html += '<span>' + label + '</span>';
        }
    }
    freqAxis.innerHTML = html;

    /* Time axis */
    var timeAxis = document.getElementById('time-axis');
    var viewStart = self.hqSpec._viewX;
    var viewEnd = viewStart + self.hqSpec._viewW;
    var numTicks = 8;
    var tStep = (viewEnd - viewStart) / numTicks;
    html = '';
    for (var t = 0; t <= numTicks; t++) {
        html += '<span>' + self._fmtTime(viewStart + t * tStep) + '</span>';
    }
    timeAxis.innerHTML = html;
};

/* ══════════════════════════════════════════════════════════════
 *  ANNOTATIONS
 * ══════════════════════════════════════════════════════════════ */

ForensicAnalyzer.prototype._showAnnotationModal = function(time, freq) {
    document.getElementById('anno-time').value = this._fmtTime(time);
    document.getElementById('anno-freq').value = Math.round(freq) + ' Hz';
    document.getElementById('anno-note').value = '';
    document.getElementById('anno-modal').classList.add('show');
    this._pendingAnnoTime = time;
    this._pendingAnnoFreq = freq;
    setTimeout(function() { document.getElementById('anno-note').focus(); }, 100);
};

ForensicAnalyzer.prototype.closeAnnotationModal = function() {
    document.getElementById('anno-modal').classList.remove('show');
};

ForensicAnalyzer.prototype.confirmAnnotation = function() {
    var note = document.getElementById('anno-note').value.trim();
    var color = document.getElementById('anno-color').value;

    this.annotations.push({
        time: this._pendingAnnoTime,
        freq: this._pendingAnnoFreq,
        note: note || 'Marker',
        color: color
    });

    this.closeAnnotationModal();
    this._renderAnnotationList();
    this._drawOverlay();
};

ForensicAnalyzer.prototype._renderAnnotationList = function() {
    var self = this;
    var list = document.getElementById('anno-list');
    if (self.annotations.length === 0) {
        list.innerHTML = '<div class="empty" style="padding:16px"><i class="fa-solid fa-map-pin fa-fw"></i> Click spectrogram to add markers</div>';
        return;
    }

    var html = '';
    for (var i = 0; i < self.annotations.length; i++) {
        var a = self.annotations[i];
        html += '<div class="anno-item" onclick="forensic.jumpToAnnotation(' + i + ')">'
            + '<span class="anno-time">' + self._fmtTime(a.time) + '</span>'
            + '<span class="anno-freq">' + Math.round(a.freq) + 'Hz</span>'
            + '<span class="anno-text" style="border-left:3px solid ' + self._esc(a.color) + ';padding-left:6px">' + self._esc(a.note) + '</span>'
            + '<span class="anno-del" onclick="event.stopPropagation();forensic.deleteAnnotation(' + i + ')"><i class="fa-solid fa-xmark"></i></span>'
            + '</div>';
    }
    list.innerHTML = html;
};

ForensicAnalyzer.prototype.jumpToAnnotation = function(idx) {
    var a = this.annotations[idx];
    if (!a || !this.hqSpec) return;
    /* Center view on annotation */
    var viewW = this.hqSpec._viewW;
    this.hqSpec._viewX = Math.max(0, a.time - viewW / 2);
    this.hqSpec._clampView();
    this.hqSpec.draw();
    this._drawOverlay();
    this._updateAxes();
    this._drawMinimap();
};

ForensicAnalyzer.prototype.deleteAnnotation = function(idx) {
    this.annotations.splice(idx, 1);
    this._renderAnnotationList();
    this._drawOverlay();
};

ForensicAnalyzer.prototype.exportAnnotations = function() {
    var json = JSON.stringify({
        file: this.fileName,
        sampleRate: this.sampleRate,
        duration: this.duration,
        annotations: this.annotations,
        exportedAt: new Date().toISOString()
    }, null, 2);

    var blob = new Blob([json], { type: 'application/json' });
    var a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = (this.fileName || 'forensic') + '_annotations.json';
    a.click();
    URL.revokeObjectURL(a.href);
    mc1Toast('Annotations exported', 'ok');
};

ForensicAnalyzer.prototype.exportReport = function() {
    var self = this;
    var html = '<!DOCTYPE html><html><head><meta charset="UTF-8">'
        + '<title>Forensic Report — ' + self._esc(self.fileName) + '</title>'
        + '<style>body{font-family:sans-serif;max-width:900px;margin:40px auto;color:#333}'
        + 'h1{color:#0d9488}table{border-collapse:collapse;width:100%;margin:16px 0}'
        + 'th,td{border:1px solid #ccc;padding:8px;text-align:left}th{background:#f0f0f0}'
        + '.meta{color:#666;margin-bottom:20px}</style></head><body>'
        + '<h1>Forensic Audio Analysis Report</h1>'
        + '<div class="meta">Generated: ' + new Date().toLocaleString() + '</div>'
        + '<h2>File Information</h2>'
        + '<table><tr><th>File</th><td>' + self._esc(self.fileName) + '</td></tr>'
        + '<tr><th>Sample Rate</th><td>' + self.sampleRate + ' Hz</td></tr>'
        + '<tr><th>Channels</th><td>' + self.channels + '</td></tr>'
        + '<tr><th>Duration</th><td>' + self._fmtTime(self.duration) + '</td></tr>'
        + '<tr><th>FFT Size</th><td>' + self.fftSize + '</td></tr>'
        + '<tr><th>Window</th><td>' + self._esc(self.windowType) + '</td></tr></table>';

    if (self.annotations.length > 0) {
        html += '<h2>Annotations (' + self.annotations.length + ')</h2><table>'
            + '<tr><th>#</th><th>Time</th><th>Frequency</th><th>Note</th></tr>';
        for (var i = 0; i < self.annotations.length; i++) {
            var a = self.annotations[i];
            html += '<tr><td>' + (i + 1) + '</td><td>' + self._fmtTime(a.time)
                + '</td><td>' + Math.round(a.freq) + ' Hz</td><td>'
                + self._esc(a.note) + '</td></tr>';
        }
        html += '</table>';
    }

    html += '</body></html>';

    var blob = new Blob([html], { type: 'text/html' });
    var a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = (self.fileName || 'forensic') + '_report.html';
    a.click();
    URL.revokeObjectURL(a.href);
    mc1Toast('Report exported', 'ok');
};

/* ══════════════════════════════════════════════════════════════
 *  SAVE/LOAD ANALYSIS
 * ══════════════════════════════════════════════════════════════ */

ForensicAnalyzer.prototype.saveAnalysis = function() {
    var self = this;
    if (!self.fileName) {
        mc1Toast('No file loaded', 'warn');
        return;
    }

    var name = prompt('Analysis name:', self.fileName + ' analysis');
    if (!name) return;

    mc1Api('POST', '/app/api/forensic.php', {
        action: 'save_annotations',
        file_path: self.filePath || self.fileName,
        file_hash: self.fileHash,
        analysis_name: name,
        annotations_json: self.annotations,
        settings_json: {
            fftSize: self.fftSize,
            windowType: self.windowType,
            freqScale: self.freqScale,
            colormap: document.getElementById('ctl-colormap').value,
            gain: parseFloat(document.getElementById('ctl-gain').value),
            floor: parseFloat(document.getElementById('ctl-floor').value)
        },
        notes: ''
    }).then(function(d) {
        if (d && d.ok) {
            mc1Toast('Analysis saved: ' + name, 'ok');
        } else {
            mc1Toast(d.error || 'Save failed', 'err');
        }
    }).catch(function(e) {
        mc1Toast('Save failed: ' + e.message, 'err');
    });
};

ForensicAnalyzer.prototype.loadAnalysis = function() {
    var self = this;
    mc1Api('POST', '/app/api/forensic.php', {
        action: 'load_annotations',
        file_path: self.filePath || self.fileName
    }).then(function(d) {
        if (d && d.ok && d.analyses && d.analyses.length > 0) {
            /* Take the most recent */
            var latest = d.analyses[0];
            if (latest.annotations_json) {
                self.annotations = typeof latest.annotations_json === 'string'
                    ? JSON.parse(latest.annotations_json) : latest.annotations_json;
                self._renderAnnotationList();
                self._drawOverlay();
            }
            if (latest.settings_json) {
                var s = typeof latest.settings_json === 'string'
                    ? JSON.parse(latest.settings_json) : latest.settings_json;
                if (s.fftSize) {
                    document.getElementById('ctl-fftsize').value = s.fftSize;
                    self.fftSize = s.fftSize;
                }
                if (s.windowType) {
                    document.getElementById('ctl-window').value = s.windowType;
                    self.windowType = s.windowType;
                }
                if (s.colormap) document.getElementById('ctl-colormap').value = s.colormap;
                if (s.gain !== undefined) document.getElementById('ctl-gain').value = s.gain;
                if (s.floor !== undefined) document.getElementById('ctl-floor').value = s.floor;
            }
            mc1Toast('Loaded analysis: ' + (latest.analysis_name || 'unnamed'), 'ok');
        } else {
            mc1Toast('No saved analyses found for this file', 'warn');
        }
    }).catch(function(e) {
        mc1Toast('Load failed: ' + e.message, 'err');
    });
};

/* ══════════════════════════════════════════════════════════════
 *  LIVE RECORDING
 * ══════════════════════════════════════════════════════════════ */

ForensicAnalyzer.prototype.toggleRecordLive = function() {
    var self = this;
    if (self.isRecording) {
        self._stopRecording();
    } else {
        self._startRecording();
    }
};

ForensicAnalyzer.prototype._startRecording = function() {
    var self = this;
    if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
        mc1Toast('Microphone access not supported', 'err');
        return;
    }

    navigator.mediaDevices.getUserMedia({ audio: true }).then(function(stream) {
        self.isRecording = true;
        self.recordedChunks = [];
        var btn = document.getElementById('btn-record');
        btn.classList.add('btn-danger');
        btn.classList.remove('btn-secondary');
        btn.innerHTML = '<i class="fa-solid fa-stop"></i> Stop';

        self.mediaRecorder = new MediaRecorder(stream, { mimeType: 'audio/webm' });
        self.mediaRecorder.ondataavailable = function(e) {
            if (e.data.size > 0) self.recordedChunks.push(e.data);
        };
        self.mediaRecorder.onstop = function() {
            stream.getTracks().forEach(function(t) { t.stop(); });
            var blob = new Blob(self.recordedChunks, { type: 'audio/webm' });
            self.fileName = 'live_recording_' + new Date().toISOString().replace(/[:.]/g, '-') + '.webm';
            /* Decode the recorded blob */
            var reader = new FileReader();
            reader.onload = function(ev) {
                if (!self.audioCtx) {
                    self.audioCtx = new (window.AudioContext || window.webkitAudioContext)();
                }
                self.audioCtx.decodeAudioData(ev.target.result, function(buffer) {
                    self.audioBuffer = buffer;
                    self.sampleRate = buffer.sampleRate;
                    self.channels = buffer.numberOfChannels;
                    self.duration = buffer.duration;
                    document.getElementById('file-info').innerHTML =
                        '<strong>FILE:</strong> ' + self._esc(self.fileName)
                        + ' | ' + buffer.sampleRate + 'Hz ' + self.channels + 'ch | '
                        + self._fmtTime(buffer.duration);
                    self._computeSpectrogram();
                });
            };
            reader.readAsArrayBuffer(blob);
        };
        self.mediaRecorder.start(250);
        mc1Toast('Recording started', 'ok');
    }).catch(function(err) {
        mc1Toast('Microphone error: ' + err.message, 'err');
    });
};

ForensicAnalyzer.prototype._stopRecording = function() {
    if (this.mediaRecorder && this.mediaRecorder.state !== 'inactive') {
        this.mediaRecorder.stop();
    }
    this.isRecording = false;
    var btn = document.getElementById('btn-record');
    btn.classList.remove('btn-danger');
    btn.classList.add('btn-secondary');
    btn.innerHTML = '<i class="fa-solid fa-circle" style="color:#ef4444"></i> Record Live';
    mc1Toast('Recording stopped, processing...', 'ok');
};

/* ══════════════════════════════════════════════════════════════
 *  COMPARE MODE
 * ══════════════════════════════════════════════════════════════ */

ForensicAnalyzer.prototype.toggleCompare = function() {
    this.compareMode = !this.compareMode;
    var btn = document.getElementById('btn-compare');
    btn.classList.toggle('btn-primary', this.compareMode);
    btn.classList.toggle('btn-secondary', !this.compareMode);
    mc1Toast(this.compareMode ? 'Compare mode: load a second file' : 'Compare mode off', 'ok');
};

/* ══════════════════════════════════════════════════════════════
 *  AI ANALYSIS (Ollama)
 * ══════════════════════════════════════════════════════════════ */

ForensicAnalyzer.prototype.aiAnalyze = function() {
    var self = this;
    if (!self.spectrogramData) {
        mc1Toast('No spectrogram data to analyze', 'warn');
        return;
    }

    var prompt = 'Analyze this audio spectrum region. ';
    if (self.selStartTime >= 0 && self.selEndTime >= 0) {
        prompt += 'Time range: ' + self._fmtTime(Math.min(self.selStartTime, self.selEndTime))
            + ' to ' + self._fmtTime(Math.max(self.selStartTime, self.selEndTime)) + '. ';
        prompt += 'Frequency range: ' + Math.round(Math.min(self.selStartFreq, self.selEndFreq))
            + ' Hz to ' + Math.round(Math.max(self.selStartFreq, self.selEndFreq)) + ' Hz. ';
    }
    prompt += 'File: ' + self.fileName + ', Sample rate: ' + self.sampleRate + ' Hz. ';
    prompt += 'Identify any anomalous frequencies, patterns, or artifacts. Is there anything unusual about this spectrum?';

    self._callAI(prompt);
};

ForensicAnalyzer.prototype.aiDescribe = function() {
    var self = this;
    if (!self.spectrogramData) {
        mc1Toast('No spectrogram data', 'warn');
        return;
    }

    var prompt = 'Describe the audio content of this file: ' + self.fileName + '. '
        + 'Duration: ' + self._fmtTime(self.duration) + ', '
        + 'Sample rate: ' + self.sampleRate + ' Hz, '
        + 'Channels: ' + self.channels + '. '
        + 'The spectrogram shows ' + self.specWidth + ' time frames and ' + self.specHeight + ' frequency bins. '
        + 'What type of audio is this likely to be? Describe the spectral characteristics.';

    self._callAI(prompt);
};

ForensicAnalyzer.prototype._callAI = function(prompt) {
    var self = this;
    var resultEl = document.getElementById('ai-result');
    resultEl.style.display = 'block';
    resultEl.textContent = 'Analyzing...';

    mc1Api('POST', '/app/api/forensic.php', {
        action: 'ai_analyze',
        prompt: prompt
    }).then(function(d) {
        if (d && d.ok) {
            resultEl.textContent = d.response || 'No response from AI';
        } else {
            resultEl.textContent = 'AI error: ' + (d.error || 'unknown');
        }
    }).catch(function(e) {
        resultEl.textContent = 'AI unavailable: ' + e.message;
    });
};

/* ══════════════════════════════════════════════════════════════
 *  UTILITIES
 * ══════════════════════════════════════════════════════════════ */

ForensicAnalyzer.prototype._fmtTime = function(sec) {
    if (sec < 0 || !isFinite(sec)) return '0:00.000';
    var m = Math.floor(sec / 60);
    var s = sec - m * 60;
    var sStr = s < 10 ? '0' + s.toFixed(3) : s.toFixed(3);
    return m + ':' + sStr;
};

ForensicAnalyzer.prototype._esc = function(str) {
    if (!str) return '';
    var div = document.createElement('div');
    div.textContent = str;
    return div.innerHTML;
};

ForensicAnalyzer.prototype._showLoading = function(text) {
    var el = document.getElementById('loading-overlay');
    document.getElementById('loading-text').textContent = text || 'Loading...';
    el.classList.add('show');
};

ForensicAnalyzer.prototype._hideLoading = function() {
    document.getElementById('loading-overlay').classList.remove('show');
};
