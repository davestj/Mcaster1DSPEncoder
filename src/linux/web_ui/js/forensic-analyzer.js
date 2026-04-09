/**
 * forensic-analyzer.js — Forensic Audio Analysis Engine
 *
 * File:    src/linux/web_ui/js/forensic-analyzer.js
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Phase:   FA-3
 * Purpose: We provide a complete forensic audio analysis engine with offline FFT
 *          computation, configurable window functions, multiple frequency scales,
 *          region selection, variable-speed playback with band-pass filtering,
 *          annotations, AI analysis integration via Ollama, spectral noise
 *          reduction, band isolation, amplitude envelope, WSOLA pitch-preserved
 *          speed change, spectrum peak detection, side-by-side compare mode,
 *          professional HTML report generation, AI spectrum analysis with
 *          frequency distribution context, automatic event detection (silence,
 *          transients, tonal, clicks/pops), and stereo phase correlation
 *          goniometer display.
 *
 * Standards:
 *  - We use Web Audio API for decoding, playback, and filtering
 *  - We use OfflineAudioContext for pre-computing full spectrogram FFT data
 *  - We implement all window functions in JS (multiply before FFT)
 *  - We render via HQSpectrogram (WebGL 2.0) from webgl-spectrogram-hq.js
 *  - We never call exit()/die() in associated PHP
 *  - We use mc1Api() for server communication
 *  - All processing is non-destructive: original AudioBuffer always preserved
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
    self.compareBuffer = null;      /* AudioBuffer for file B */
    self.compareSpecData = null;    /* Float32Array spectrogram for file B */
    self.compareSpecWidth = 0;
    self.compareSpecHeight = 0;
    self.compareHqSpec = null;      /* HQSpectrogram for file B panel */
    self.diffHqSpec = null;         /* HQSpectrogram for difference panel */

    /* ── Enhancement: Noise Reduction ── */
    self.noisePrint = null;         /* Float32Array: average magnitude per bin */
    self.cleanedBuffer = null;      /* AudioBuffer after noise reduction */
    self.noiseStrength = 1.0;

    /* ── Enhancement: Band Isolation ── */
    self.isolatedBuffer = null;     /* AudioBuffer with isolated band */

    /* ── Enhancement: Amplitude Envelope ── */
    self.envelopeData = null;       /* Float32Array: RMS envelope */
    self.envelopeWindowMs = 50;     /* Sliding window size in ms */
    self.showEnvelope = false;

    /* ── Enhancement: WSOLA ── */
    self.preservePitch = false;
    self.wsolaBuffer = null;        /* AudioBuffer from WSOLA processing */

    /* ── Enhancement: Peak Detection ── */
    self.detectedPeaks = [];        /* Array of {freq, dB, time} */
    self.peakThreshold = -40;       /* dB threshold */
    self.peakMinDistance = 100;     /* Hz minimum distance between peaks */

    /* ── Active playback buffer selection ── */
    self.activeBuffer = null;       /* Which buffer is currently active for playback */

    /* ── Enhancement history (for reports) ── */
    self.enhancementHistory = [];   /* Array of {action, timestamp, params} */

    /* ── Event detection ── */
    self.detectedEvents = [];       /* Array of {type, startTime, endTime, freq, magnitude, label} */

    /* ── Goniometer (stereo phase correlation) ── */
    self.goniometerCanvas = null;
    self.goniometerGL = null;
    self.goniometerActive = false;
    self.goniometerAnimFrame = null;

    /* ── SHA256 hash (computed from file bytes) ── */
    self.fileArrayBuffer = null;    /* Original file bytes for hash computation */
    self.fileSHA256 = '';

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
        self.fileArrayBuffer = ev.target.result;

        /* Compute SHA256 hash from raw file bytes */
        self._computeSHA256(ev.target.result).then(function(hash) {
            self.fileSHA256 = hash;
        });

        self.audioCtx.decodeAudioData(ev.target.result.slice(0), function(buffer) {
            self.audioBuffer = buffer;
            self.sampleRate = buffer.sampleRate;
            self.channels = buffer.numberOfChannels;
            self.duration = buffer.duration;
            self.bitDepth = 32; /* Web Audio always decodes to float32 */
            self.enhancementHistory = [];

            /* Update file info display */
            document.getElementById('file-info').innerHTML =
                '<strong>FILE:</strong> ' + self._esc(file.name)
                + ' | ' + buffer.sampleRate + 'Hz ' + self.channels + 'ch | '
                + self._fmtTime(buffer.duration);

            self._computeSpectrogram();

            /* Initialize goniometer if stereo */
            if (buffer.numberOfChannels >= 2) {
                self._initGoniometer();
            } else {
                self._hideGoniometer();
            }
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

    /* Select active buffer: cleaned/isolated/wsola or original */
    var buffer = self.activeBuffer || self.audioBuffer;

    /* WSOLA pitch preservation: pre-process buffer at target speed */
    if (self.preservePitch && self.playbackRate !== 1.0) {
        self._showLoading('WSOLA time-stretch...');
        try {
            buffer = self._wsola(buffer, self.playbackRate);
        } catch (e) {
            mc1Toast('WSOLA failed: ' + e.message, 'err');
        }
        self._hideLoading();
    }

    if (self.isReverse) {
        buffer = self._reverseBuffer(buffer);
    }

    self.sourceNode = self.audioCtx.createBufferSource();
    self.sourceNode.buffer = buffer;
    /* When using WSOLA, play at 1.0x since speed is already baked in */
    self.sourceNode.playbackRate.value = (self.preservePitch && self.playbackRate !== 1.0) ? 1.0 : self.playbackRate;

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

    /* Draw detected peaks */
    for (var i = 0; i < self.detectedPeaks.length; i++) {
        var pk = self.detectedPeaks[i];
        var pkY = self.hqSpec.freqToCanvas(pk.freq);

        /* Horizontal line across visible area */
        ctx.strokeStyle = 'rgba(239, 68, 68, 0.5)';
        ctx.lineWidth = 0.5;
        ctx.setLineDash([4, 4]);
        ctx.beginPath();
        ctx.moveTo(0, pkY);
        ctx.lineTo(canvas.width, pkY);
        ctx.stroke();
        ctx.setLineDash([]);

        /* Label */
        ctx.font = '9px "SF Mono", "Fira Code", monospace';
        ctx.fillStyle = '#ef4444';
        var peakLabel = Math.round(pk.freq) + ' Hz (' + pk.dB.toFixed(1) + ' dB)';
        ctx.fillText(peakLabel, 4, pkY - 3);
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

    /* Envelope overlay */
    if (self.showEnvelope && self.envelopeData) {
        var envData = self.envelopeData;
        var envStep = Math.floor(envData.length / w);
        ctx.strokeStyle = 'rgba(234, 179, 8, 0.8)';
        ctx.lineWidth = 1.5;

        /* Upper envelope */
        ctx.beginPath();
        for (var i = 0; i < w; i++) {
            var s = i * envStep;
            var envVal = 0;
            for (var j = 0; j < envStep && s + j < envData.length; j++) {
                if (envData[s + j] > envVal) envVal = envData[s + j];
            }
            var y = mid - envVal * mid;
            if (i === 0) ctx.moveTo(i, y); else ctx.lineTo(i, y);
        }
        ctx.stroke();

        /* Lower envelope (mirror) */
        ctx.beginPath();
        for (var i = 0; i < w; i++) {
            var s = i * envStep;
            var envVal = 0;
            for (var j = 0; j < envStep && s + j < envData.length; j++) {
                if (envData[s + j] > envVal) envVal = envData[s + j];
            }
            var y = mid + envVal * mid;
            if (i === 0) ctx.moveTo(i, y); else ctx.lineTo(i, y);
        }
        ctx.stroke();
    }
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

/**
 * Export Report — opens the report config dialog. The actual generation
 * happens in generateReport() after the user fills in analyst info.
 */
ForensicAnalyzer.prototype.exportReport = function() {
    var modal = document.getElementById('report-modal');
    if (modal) modal.classList.add('show');
};

/**
 * Generate a professional self-contained HTML forensic report.
 * Includes file metadata, SHA256 hash, spectrogram screenshot, waveform,
 * annotations, enhancement history, detected events, and signature block.
 */
ForensicAnalyzer.prototype.generateReport = function(opts) {
    var self = this;
    opts = opts || {};
    var analystName = opts.analystName || 'Unknown';
    var caseNumber = opts.caseNumber || 'N/A';
    var includeSpec = opts.includeSpectrogram !== false;
    var includeWave = opts.includeWaveform !== false;
    var includeAnnotations = opts.includeAnnotations !== false;
    var includeMeta = opts.includeMetadata !== false;
    var includeEnhanceLog = opts.includeEnhanceLog !== false;
    var includeEvents = opts.includeEvents !== false;

    /* Build self-contained HTML */
    var css = 'body{font-family:"Segoe UI",Helvetica,Arial,sans-serif;max-width:960px;margin:40px auto;color:#1e293b;line-height:1.6;padding:0 20px}'
        + 'h1{color:#0d9488;border-bottom:3px solid #0d9488;padding-bottom:12px;font-size:24px}'
        + 'h2{color:#334155;margin-top:32px;font-size:18px;border-bottom:1px solid #e2e8f0;padding-bottom:6px}'
        + 'table{border-collapse:collapse;width:100%;margin:16px 0;font-size:13px}'
        + 'th,td{border:1px solid #cbd5e1;padding:8px 12px;text-align:left}'
        + 'th{background:#f1f5f9;font-weight:600;color:#334155}'
        + 'tr:nth-child(even){background:#f8fafc}'
        + '.meta{color:#64748b;margin-bottom:24px;font-size:14px}'
        + '.header-grid{display:grid;grid-template-columns:1fr 1fr;gap:20px;margin-bottom:20px}'
        + '.sig-block{margin-top:40px;padding:20px;border:2px solid #0d9488;border-radius:8px;background:#f0fdfa}'
        + '.sig-block h3{color:#0d9488;margin:0 0 12px 0}'
        + '.sig-row{display:flex;gap:40px;margin-top:12px}'
        + '.sig-row .sig-field{flex:1}'
        + '.sig-field label{font-size:11px;color:#64748b;text-transform:uppercase;letter-spacing:0.05em}'
        + '.sig-field .sig-val{font-size:14px;font-weight:600;margin-top:2px;border-bottom:1px solid #334155;padding-bottom:4px;min-height:20px}'
        + '.severity-info{color:#0284c7}.severity-warn{color:#d97706}.severity-critical{color:#dc2626}'
        + '.event-icon{display:inline-block;width:18px;text-align:center}'
        + 'img.screenshot{max-width:100%;border:1px solid #cbd5e1;border-radius:4px;margin:8px 0}'
        + '.chain-hash{font-family:"SF Mono","Fira Code",monospace;font-size:12px;word-break:break-all;background:#f1f5f9;padding:8px;border-radius:4px}'
        + '.enhance-item{padding:4px 0;border-bottom:1px solid #f1f5f9}'
        + '@media print{body{margin:20px}h1{font-size:20px}.sig-block{break-inside:avoid}}';

    var html = '<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8">'
        + '<title>Forensic Audio Report - ' + self._esc(self.fileName) + '</title>'
        + '<style>' + css + '</style></head><body>';

    /* Header */
    html += '<h1>Forensic Audio Analysis Report</h1>';
    html += '<div class="header-grid">';
    html += '<div><strong>Case Number:</strong> ' + self._esc(caseNumber) + '</div>';
    html += '<div><strong>Analyst:</strong> ' + self._esc(analystName) + '</div>';
    html += '<div><strong>Generated:</strong> ' + new Date().toLocaleString() + '</div>';
    html += '<div><strong>Tool:</strong> Mcaster1 Forensic Audio Analyzer v1.8</div>';
    html += '</div>';

    /* File Metadata */
    if (includeMeta) {
        html += '<h2>File Metadata</h2>';
        html += '<table>';
        html += '<tr><th>File Name</th><td>' + self._esc(self.fileName) + '</td></tr>';
        html += '<tr><th>SHA-256 Hash</th><td><span class="chain-hash">' + (self.fileSHA256 || 'Not computed') + '</span></td></tr>';
        html += '<tr><th>Sample Rate</th><td>' + self.sampleRate + ' Hz</td></tr>';
        html += '<tr><th>Bit Depth</th><td>' + self.bitDepth + '-bit (float32 decoded)</td></tr>';
        html += '<tr><th>Channels</th><td>' + self.channels + '</td></tr>';
        html += '<tr><th>Duration</th><td>' + self._fmtTime(self.duration) + ' (' + self.duration.toFixed(6) + ' seconds)</td></tr>';
        html += '</table>';

        html += '<h2>Analysis Settings</h2>';
        html += '<table>';
        html += '<tr><th>FFT Size</th><td>' + self.fftSize + '</td></tr>';
        html += '<tr><th>Window Function</th><td>' + self._esc(self.windowType) + '</td></tr>';
        html += '<tr><th>Color Map</th><td>' + (document.getElementById('ctl-colormap') ? document.getElementById('ctl-colormap').value : 'heat') + '</td></tr>';
        html += '<tr><th>Frequency Scale</th><td>' + self.freqScale + '</td></tr>';
        html += '<tr><th>Hop Ratio</th><td>' + self.hopRatio + '</td></tr>';
        html += '<tr><th>Gain</th><td>' + (document.getElementById('ctl-gain') ? document.getElementById('ctl-gain').value : '0') + ' dB</td></tr>';
        html += '<tr><th>Floor</th><td>' + (document.getElementById('ctl-floor') ? document.getElementById('ctl-floor').value : '-96') + ' dB</td></tr>';
        html += '</table>';
    }

    /* Spectrogram screenshot */
    if (includeSpec && self.specCanvas) {
        html += '<h2>Spectrogram</h2>';
        try {
            var specUrl = self.specCanvas.toDataURL('image/png');
            html += '<img class="screenshot" src="' + specUrl + '" alt="Spectrogram">';
        } catch (e) {
            html += '<p><em>Spectrogram screenshot unavailable (WebGL context lost)</em></p>';
        }
    }

    /* Waveform screenshot */
    if (includeWave && self.waveCanvas) {
        html += '<h2>Waveform Overview</h2>';
        try {
            var waveUrl = self.waveCanvas.toDataURL('image/png');
            html += '<img class="screenshot" src="' + waveUrl + '" alt="Waveform">';
        } catch (e) {
            html += '<p><em>Waveform screenshot unavailable</em></p>';
        }
    }

    /* Annotations */
    if (includeAnnotations && self.annotations.length > 0) {
        html += '<h2>Annotations (' + self.annotations.length + ')</h2>';
        html += '<table><tr><th>#</th><th>Timestamp</th><th>Frequency</th><th>Note</th><th>Severity</th></tr>';
        for (var i = 0; i < self.annotations.length; i++) {
            var a = self.annotations[i];
            var severity = a.severity || 'info';
            var sevClass = 'severity-' + severity;
            html += '<tr><td>' + (i + 1) + '</td>'
                + '<td>' + self._fmtTime(a.time) + '</td>'
                + '<td>' + Math.round(a.freq) + ' Hz</td>'
                + '<td>' + self._esc(a.note) + '</td>'
                + '<td class="' + sevClass + '">' + severity + '</td></tr>';
        }
        html += '</table>';
    }

    /* Detected Events */
    if (includeEvents && self.detectedEvents.length > 0) {
        html += '<h2>Detected Events (' + self.detectedEvents.length + ')</h2>';
        html += '<table><tr><th>#</th><th>Type</th><th>Start</th><th>End</th><th>Frequency</th><th>Magnitude</th></tr>';
        var eventIcons = { silence: 'Silence', transient: 'Transient', tonal: 'Tonal', click: 'Click/Pop' };
        for (var i = 0; i < self.detectedEvents.length; i++) {
            var ev = self.detectedEvents[i];
            html += '<tr><td>' + (i + 1) + '</td>'
                + '<td>' + (eventIcons[ev.type] || ev.type) + '</td>'
                + '<td>' + self._fmtTime(ev.startTime) + '</td>'
                + '<td>' + self._fmtTime(ev.endTime) + '</td>'
                + '<td>' + (ev.freq > 0 ? Math.round(ev.freq) + ' Hz' : '-') + '</td>'
                + '<td>' + (ev.magnitude !== undefined ? ev.magnitude.toFixed(1) + ' dB' : '-') + '</td></tr>';
        }
        html += '</table>';
    }

    /* Enhancement History */
    if (includeEnhanceLog && self.enhancementHistory.length > 0) {
        html += '<h2>Enhancement History</h2>';
        html += '<table><tr><th>#</th><th>Action</th><th>Timestamp</th><th>Parameters</th></tr>';
        for (var i = 0; i < self.enhancementHistory.length; i++) {
            var eh = self.enhancementHistory[i];
            var paramStr = '';
            if (eh.params) {
                var keys = Object.keys(eh.params);
                for (var k = 0; k < keys.length; k++) {
                    if (k > 0) paramStr += ', ';
                    paramStr += keys[k] + ': ' + eh.params[keys[k]];
                }
            }
            html += '<tr><td>' + (i + 1) + '</td>'
                + '<td>' + self._esc(eh.action) + '</td>'
                + '<td>' + eh.timestamp + '</td>'
                + '<td>' + self._esc(paramStr) + '</td></tr>';
        }
        html += '</table>';
    }

    /* Signature block */
    html += '<div class="sig-block">';
    html += '<h3>Chain of Custody / Certification</h3>';
    html += '<p>I certify that this analysis was performed using the above-described methods and tools, '
        + 'and the findings presented in this report accurately reflect my observations.</p>';
    html += '<div class="sig-row">';
    html += '<div class="sig-field"><label>Analyst Name</label><div class="sig-val">' + self._esc(analystName) + '</div></div>';
    html += '<div class="sig-field"><label>Date</label><div class="sig-val">' + new Date().toLocaleDateString() + '</div></div>';
    html += '<div class="sig-field"><label>Case Number</label><div class="sig-val">' + self._esc(caseNumber) + '</div></div>';
    html += '</div>';
    html += '<div style="margin-top:16px"><label style="font-size:11px;color:#64748b;text-transform:uppercase;letter-spacing:0.05em">File Hash (SHA-256)</label>';
    html += '<div class="chain-hash">' + (self.fileSHA256 || 'Not computed') + '</div></div>';
    html += '<div style="margin-top:20px;border-top:1px solid #0d9488;padding-top:12px">';
    html += '<label style="font-size:11px;color:#64748b;text-transform:uppercase;letter-spacing:0.05em">Signature</label>';
    html += '<div style="height:60px;border-bottom:1px solid #334155"></div></div>';
    html += '</div>';

    html += '</body></html>';

    /* Open in new window for printing + download as file */
    var blob = new Blob([html], { type: 'text/html' });
    var url = URL.createObjectURL(blob);

    /* Also offer download */
    var dlLink = document.createElement('a');
    dlLink.href = url;
    dlLink.download = (self.fileName || 'forensic') + '_report.html';
    dlLink.click();

    /* Open in new window for Ctrl+P */
    var win = window.open('', '_blank', 'width=1000,height=800');
    if (win) {
        win.document.write(html);
        win.document.close();
    }

    URL.revokeObjectURL(url);
    mc1Toast('Report generated and downloaded', 'ok');
};

/**
 * Export the current spectrogram view as a high-resolution PNG.
 */
ForensicAnalyzer.prototype.exportSpecPNG = function() {
    var self = this;
    if (!self.specCanvas) {
        mc1Toast('No spectrogram to export', 'warn');
        return;
    }
    try {
        var url = self.specCanvas.toDataURL('image/png');
        var a = document.createElement('a');
        a.href = url;
        a.download = (self.fileName || 'spectrogram') + '_spectrogram.png';
        a.click();
        mc1Toast('Spectrogram PNG exported', 'ok');
    } catch (e) {
        mc1Toast('PNG export failed: ' + e.message, 'err');
    }
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
 *  AI ANALYSIS (Ollama)
 * ══════════════════════════════════════════════════════════════ */

/**
 * AI Analyze Selection — sends selected region's averaged spectrum data
 * as frequency distribution context to Ollama for detailed analysis.
 */
ForensicAnalyzer.prototype.aiAnalyze = function() {
    var self = this;
    if (!self.spectrogramData) {
        mc1Toast('No spectrogram data to analyze', 'warn');
        return;
    }
    if (self.selStartTime < 0 || self.selEndTime < 0) {
        mc1Toast('Select a region on the spectrogram first', 'warn');
        return;
    }

    /* Compute average magnitude per frequency bin in selected region */
    var t0 = Math.min(self.selStartTime, self.selEndTime);
    var t1 = Math.max(self.selStartTime, self.selEndTime);
    var f0 = Math.min(self.selStartFreq, self.selEndFreq);
    var f1 = Math.max(self.selStartFreq, self.selEndFreq);
    var nyquist = self.sampleRate / 2;

    var colStart = Math.max(0, Math.floor((t0 / self.duration) * self.specWidth));
    var colEnd = Math.min(self.specWidth - 1, Math.ceil((t1 / self.duration) * self.specWidth));
    var rowStart = Math.max(0, Math.floor((f0 / nyquist) * self.specHeight));
    var rowEnd = Math.min(self.specHeight - 1, Math.ceil((f1 / nyquist) * self.specHeight));
    var numCols = colEnd - colStart + 1;

    /* Build frequency distribution string (sample up to 40 bins for brevity) */
    var totalRows = rowEnd - rowStart + 1;
    var step = Math.max(1, Math.floor(totalRows / 40));
    var freqDist = '';
    for (var r = rowStart; r <= rowEnd; r += step) {
        var sum = 0;
        for (var c = colStart; c <= colEnd; c++) {
            sum += self.spectrogramData[r * self.specWidth + c];
        }
        var avgDB = sum / numCols;
        var freq = Math.round((r / self.specHeight) * nyquist);
        if (freqDist) freqDist += ', ';
        freqDist += freq + 'Hz=' + avgDB.toFixed(1) + 'dB';
    }

    var prompt = 'You are a forensic audio analyst. Analyze this audio spectrum selection.\n\n'
        + 'File: ' + self.fileName + '\n'
        + 'Sample rate: ' + self.sampleRate + ' Hz, Channels: ' + self.channels + '\n'
        + 'Time range: ' + self._fmtTime(t0) + ' to ' + self._fmtTime(t1) + '\n'
        + 'Frequency range: ' + Math.round(f0) + ' Hz to ' + Math.round(f1) + ' Hz\n\n'
        + 'Frequency distribution (average dB per bin in selected region):\n'
        + freqDist + '\n\n'
        + 'Describe what you observe: identify dominant frequencies, harmonics, anomalies, '
        + 'possible sound sources, and any artifacts or unusual patterns.';

    self._callAI(prompt);
};

/**
 * AI Describe Full Audio — sends overall statistics for a general description.
 */
ForensicAnalyzer.prototype.aiDescribe = function() {
    var self = this;
    if (!self.spectrogramData) {
        mc1Toast('No spectrogram data', 'warn');
        return;
    }

    /* Compute overall statistics */
    var totalBins = self.specWidth * self.specHeight;
    var peak = -120, sum = 0, silenceCount = 0;
    var noiseFloor = -96;
    for (var i = 0; i < totalBins; i++) {
        var val = self.spectrogramData[i];
        if (val > peak) peak = val;
        sum += val;
        if (val < noiseFloor) silenceCount++;
    }
    var avg = totalBins > 0 ? sum / totalBins : -120;
    var silenceRatio = totalBins > 0 ? (silenceCount / totalBins * 100).toFixed(1) : '0';
    var dynamicRange = (peak - avg).toFixed(1);

    /* Find dominant frequencies (top 5 from average spectrum) */
    var nyquist = self.sampleRate / 2;
    var avgSpec = new Float32Array(self.specHeight);
    for (var r = 0; r < self.specHeight; r++) {
        var s = 0;
        for (var c = 0; c < self.specWidth; c++) {
            s += self.spectrogramData[r * self.specWidth + c];
        }
        avgSpec[r] = s / self.specWidth;
    }
    var domFreqs = [];
    for (var r = 1; r < self.specHeight - 1; r++) {
        if (avgSpec[r] > avgSpec[r - 1] && avgSpec[r] > avgSpec[r + 1] && avgSpec[r] > -60) {
            domFreqs.push({ freq: Math.round((r / self.specHeight) * nyquist), dB: avgSpec[r].toFixed(1) });
        }
    }
    domFreqs.sort(function(a, b) { return parseFloat(b.dB) - parseFloat(a.dB); });
    domFreqs = domFreqs.slice(0, 5);
    var domStr = domFreqs.map(function(d) { return d.freq + 'Hz (' + d.dB + 'dB)'; }).join(', ');

    var prompt = 'You are a forensic audio analyst. Provide a general description of this audio file.\n\n'
        + 'File: ' + self.fileName + '\n'
        + 'Duration: ' + self._fmtTime(self.duration) + ' (' + self.duration.toFixed(3) + 's)\n'
        + 'Sample rate: ' + self.sampleRate + ' Hz\n'
        + 'Channels: ' + self.channels + '\n'
        + 'Dynamic range: ' + dynamicRange + ' dB\n'
        + 'Noise floor ratio: ' + silenceRatio + '% of bins below ' + noiseFloor + 'dB\n'
        + 'Peak magnitude: ' + peak.toFixed(1) + ' dB\n'
        + 'Average magnitude: ' + avg.toFixed(1) + ' dB\n'
        + 'Dominant frequencies: ' + (domStr || 'none detected') + '\n\n'
        + 'What type of audio is this likely to be? Describe the spectral characteristics, '
        + 'probable content (speech, music, environmental, mechanical), and any notable features.';

    self._callAI(prompt);
};

/**
 * Call AI with typing animation in the result area.
 */
ForensicAnalyzer.prototype._callAI = function(prompt) {
    var self = this;
    var resultEl = document.getElementById('ai-result');
    resultEl.style.display = 'block';
    resultEl.innerHTML = '<span style="color:var(--teal)">Analyzing...</span>';

    mc1Api('POST', '/app/api/forensic.php', {
        action: 'ai_analyze',
        prompt: prompt
    }).then(function(d) {
        if (d && d.ok && d.response) {
            /* Typing animation */
            self._typeText(resultEl, d.response);
        } else {
            resultEl.textContent = 'AI error: ' + (d.error || 'unknown');
        }
    }).catch(function(e) {
        resultEl.textContent = 'AI unavailable: ' + e.message;
    });
};

/**
 * Typing animation for AI responses.
 */
ForensicAnalyzer.prototype._typeText = function(el, text) {
    var self = this;
    el.textContent = '';
    var idx = 0;
    var speed = 12; /* ms per character */
    var timer = setInterval(function() {
        if (idx < text.length) {
            var chunk = text.substring(idx, Math.min(idx + 3, text.length));
            el.textContent += chunk;
            idx += 3;
            el.scrollTop = el.scrollHeight;
        } else {
            clearInterval(timer);
            /* Add copy button */
            var copyBtn = document.createElement('button');
            copyBtn.className = 'btn btn-secondary btn-xs';
            copyBtn.style.cssText = 'position:absolute;top:4px;right:4px;font-size:10px';
            copyBtn.innerHTML = '<i class="fa-solid fa-copy"></i> Copy';
            copyBtn.onclick = function() {
                navigator.clipboard.writeText(text).then(function() {
                    mc1Toast('AI response copied', 'ok');
                });
            };
            el.style.position = 'relative';
            el.appendChild(copyBtn);
        }
    }, speed);
};

/* ══════════════════════════════════════════════════════════════
 *  SPECTRAL NOISE REDUCTION
 * ══════════════════════════════════════════════════════════════ */

/**
 * Capture noise print from the current selection region.
 * Computes average magnitude per frequency bin across selected time frames.
 */
ForensicAnalyzer.prototype.captureNoisePrint = function() {
    var self = this;
    if (!self.audioBuffer) {
        mc1Toast('No audio loaded', 'warn');
        return;
    }
    if (self.selStartTime < 0 || self.selEndTime < 0) {
        mc1Toast('Select a silence region on the spectrogram first', 'warn');
        return;
    }

    var t0 = Math.min(self.selStartTime, self.selEndTime);
    var t1 = Math.max(self.selStartTime, self.selEndTime);
    var sr = self.sampleRate;
    var fftSize = self.fftSize;
    var hopSize = Math.floor(fftSize * self.hopRatio);
    var freqBins = fftSize / 2;
    var windowFn = self._computeWindow(fftSize, self.windowType);

    /* Get mono data */
    var rawData = self._getMonoData(self.audioBuffer);

    var startSample = Math.floor(t0 * sr);
    var endSample = Math.floor(t1 * sr);
    startSample = Math.max(0, startSample);
    endSample = Math.min(rawData.length, endSample);

    var noiseMag = new Float64Array(freqBins);
    var frameCount = 0;

    for (var offset = startSample; offset + fftSize <= endSample; offset += hopSize) {
        var windowed = new Float32Array(fftSize);
        for (var i = 0; i < fftSize; i++) {
            windowed[i] = rawData[offset + i] * windowFn[i];
        }
        var spectrum = self._fft(windowed);
        for (var bin = 0; bin < freqBins; bin++) {
            var re = spectrum[bin * 2];
            var im = spectrum[bin * 2 + 1];
            noiseMag[bin] += Math.sqrt(re * re + im * im) / fftSize;
        }
        frameCount++;
    }

    if (frameCount === 0) {
        mc1Toast('Selection too small for noise print', 'warn');
        return;
    }

    self.noisePrint = new Float32Array(freqBins);
    for (var b = 0; b < freqBins; b++) {
        self.noisePrint[b] = noiseMag[b] / frameCount;
    }

    self.enhancementHistory.push({
        action: 'Noise print captured',
        timestamp: new Date().toISOString(),
        params: { frames: frameCount, duration: self._fmtTime(t1 - t0), timeRange: t0.toFixed(3) + '-' + t1.toFixed(3) + 's' }
    });
    mc1Toast('Noise print captured from ' + frameCount + ' frames (' + self._fmtTime(t1 - t0) + ')', 'ok');
};

/**
 * Apply spectral subtraction noise reduction.
 * For each FFT frame: cleaned_magnitude[bin] = max(0, original[bin] - noise[bin] * strength)
 * Resynthesizes via inverse FFT with overlap-add.
 */
ForensicAnalyzer.prototype.applyNoiseReduction = function() {
    var self = this;
    if (!self.audioBuffer) {
        mc1Toast('No audio loaded', 'warn');
        return;
    }
    if (!self.noisePrint) {
        mc1Toast('Capture a noise print first', 'warn');
        return;
    }

    self._showLoading('Applying noise reduction...');
    var strength = self.noiseStrength;
    var fftSize = self.fftSize;
    var hopSize = Math.floor(fftSize * self.hopRatio);
    var freqBins = fftSize / 2;
    var windowFn = self._computeWindow(fftSize, self.windowType);
    var rawData = self._getMonoData(self.audioBuffer);
    var outLength = rawData.length;
    var output = new Float32Array(outLength);
    var normBuf = new Float32Array(outLength);

    var numFrames = Math.floor((rawData.length - fftSize) / hopSize) + 1;
    var frame = 0;
    var chunkSize = 64;

    var processChunk = function() {
        var end = Math.min(frame + chunkSize, numFrames);
        for (; frame < end; frame++) {
            var offset = frame * hopSize;

            /* Forward FFT */
            var windowed = new Float32Array(fftSize);
            for (var i = 0; i < fftSize; i++) {
                var idx = offset + i;
                windowed[i] = (idx < rawData.length ? rawData[idx] : 0) * windowFn[i];
            }
            var spectrum = self._fft(windowed);

            /* Spectral subtraction */
            for (var bin = 0; bin < freqBins; bin++) {
                var re = spectrum[bin * 2];
                var im = spectrum[bin * 2 + 1];
                var mag = Math.sqrt(re * re + im * im) / fftSize;
                var phase = Math.atan2(im, re);
                var cleanMag = Math.max(0, mag - self.noisePrint[bin] * strength);
                spectrum[bin * 2] = cleanMag * fftSize * Math.cos(phase);
                spectrum[bin * 2 + 1] = cleanMag * fftSize * Math.sin(phase);
                /* Mirror for conjugate symmetry */
                if (bin > 0 && bin < freqBins) {
                    var mirrorBin = fftSize - bin;
                    spectrum[mirrorBin * 2] = spectrum[bin * 2];
                    spectrum[mirrorBin * 2 + 1] = -spectrum[bin * 2 + 1];
                }
            }

            /* Inverse FFT */
            var timeDomain = self._ifft(spectrum, fftSize);

            /* Overlap-add with window */
            for (var i = 0; i < fftSize; i++) {
                var idx = offset + i;
                if (idx < outLength) {
                    output[idx] += timeDomain[i] * windowFn[i];
                    normBuf[idx] += windowFn[i] * windowFn[i];
                }
            }
        }

        var pct = Math.round((frame / numFrames) * 100);
        document.getElementById('loading-text').textContent = 'Noise reduction... ' + pct + '%';

        if (frame < numFrames) {
            setTimeout(processChunk, 0);
        } else {
            /* Normalize by overlap-add window sum */
            for (var i = 0; i < outLength; i++) {
                if (normBuf[i] > 1e-8) output[i] /= normBuf[i];
            }

            /* Create cleaned AudioBuffer */
            self.cleanedBuffer = self.audioCtx.createBuffer(1, outLength, self.sampleRate);
            self.cleanedBuffer.getChannelData(0).set(output);
            self.activeBuffer = self.cleanedBuffer;

            self.enhancementHistory.push({
                action: 'Noise reduction applied',
                timestamp: new Date().toISOString(),
                params: { strength: strength.toFixed(1) }
            });

            self._hideLoading();
            mc1Toast('Noise reduction applied (strength: ' + strength.toFixed(1) + '). Playing cleaned audio.', 'ok');

            /* Recompute spectrogram from cleaned buffer */
            self._recomputeFromBuffer(self.cleanedBuffer);
        }
    };

    setTimeout(processChunk, 10);
};

/**
 * Inverse FFT. Takes interleaved [re0, im0, re1, im1, ...] and returns time-domain Float32Array.
 */
ForensicAnalyzer.prototype._ifft = function(spectrum, n) {
    /* IFFT = conjugate, FFT, conjugate, divide by N */
    var m = 1;
    while (m < n) m <<= 1;

    var re = new Float32Array(m);
    var im = new Float32Array(m);
    for (var i = 0; i < m; i++) {
        re[i] = spectrum[i * 2] || 0;
        im[i] = -(spectrum[i * 2 + 1] || 0); /* conjugate */
    }

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

    /* Conjugate and divide by N */
    var result = new Float32Array(m);
    for (var p = 0; p < m; p++) {
        result[p] = re[p] / m; /* im is conjugated back, we only need real part */
    }
    return result;
};

/** Get mono Float32Array from an AudioBuffer */
ForensicAnalyzer.prototype._getMonoData = function(buffer) {
    if (buffer.numberOfChannels > 1) {
        var ch0 = buffer.getChannelData(0);
        var ch1 = buffer.getChannelData(1);
        var mono = new Float32Array(ch0.length);
        for (var i = 0; i < ch0.length; i++) {
            mono[i] = (ch0[i] + ch1[i]) * 0.5;
        }
        return mono;
    }
    return buffer.getChannelData(0);
};

/** Recompute spectrogram from a different buffer (for cleaned/isolated audio) */
ForensicAnalyzer.prototype._recomputeFromBuffer = function(buffer) {
    var self = this;
    var rawData = self._getMonoData(buffer);
    var fftSize = self.fftSize;
    var hopSize = Math.floor(fftSize * self.hopRatio);
    var freqBins = fftSize / 2;
    var numFrames = Math.floor((rawData.length - fftSize) / hopSize) + 1;
    if (numFrames < 1) return;

    var windowFn = self._computeWindow(fftSize, self.windowType);
    self._computeSpectrogramManual(rawData, fftSize, hopSize, freqBins, numFrames, windowFn);
};

ForensicAnalyzer.prototype.setNoiseStrength = function(val) {
    this.noiseStrength = val;
};

/** Restore original audio (undo noise reduction / isolation) */
ForensicAnalyzer.prototype.restoreOriginal = function() {
    var self = this;
    if (!self.audioBuffer) {
        mc1Toast('No audio loaded', 'warn');
        return;
    }
    self.activeBuffer = null;
    self.cleanedBuffer = null;
    self.isolatedBuffer = null;
    self.wsolaBuffer = null;
    self._recomputeFromBuffer(self.audioBuffer);
    mc1Toast('Original audio restored', 'ok');
};

/* ══════════════════════════════════════════════════════════════
 *  BAND-PASS ISOLATION
 * ══════════════════════════════════════════════════════════════ */

/**
 * Isolate the selected frequency band by zeroing all FFT bins outside the range.
 * Creates a new AudioBuffer with only the selected frequency content.
 */
ForensicAnalyzer.prototype.isolateBand = function() {
    var self = this;
    if (!self.audioBuffer) {
        mc1Toast('No audio loaded', 'warn');
        return;
    }
    if (self.selStartFreq < 0 || self.selEndFreq < 0) {
        mc1Toast('Select a frequency range on the spectrogram first', 'warn');
        return;
    }

    self._showLoading('Isolating frequency band...');
    var loFreq = Math.min(self.selStartFreq, self.selEndFreq);
    var hiFreq = Math.max(self.selStartFreq, self.selEndFreq);
    var fftSize = self.fftSize;
    var hopSize = Math.floor(fftSize * self.hopRatio);
    var freqBins = fftSize / 2;
    var windowFn = self._computeWindow(fftSize, self.windowType);
    var rawData = self._getMonoData(self.audioBuffer);
    var outLength = rawData.length;
    var output = new Float32Array(outLength);
    var normBuf = new Float32Array(outLength);
    var nyquist = self.sampleRate / 2;
    var loBin = Math.floor((loFreq / nyquist) * freqBins);
    var hiBin = Math.ceil((hiFreq / nyquist) * freqBins);
    loBin = Math.max(0, loBin);
    hiBin = Math.min(freqBins - 1, hiBin);

    var numFrames = Math.floor((rawData.length - fftSize) / hopSize) + 1;
    var frame = 0;
    var chunkSize = 64;

    var processChunk = function() {
        var end = Math.min(frame + chunkSize, numFrames);
        for (; frame < end; frame++) {
            var offset = frame * hopSize;
            var windowed = new Float32Array(fftSize);
            for (var i = 0; i < fftSize; i++) {
                var idx = offset + i;
                windowed[i] = (idx < rawData.length ? rawData[idx] : 0) * windowFn[i];
            }
            var spectrum = self._fft(windowed);

            /* Zero out bins outside the selected range */
            for (var bin = 0; bin < freqBins; bin++) {
                if (bin < loBin || bin > hiBin) {
                    spectrum[bin * 2] = 0;
                    spectrum[bin * 2 + 1] = 0;
                    if (bin > 0) {
                        var mirror = fftSize - bin;
                        spectrum[mirror * 2] = 0;
                        spectrum[mirror * 2 + 1] = 0;
                    }
                } else {
                    /* Keep and set conjugate symmetry */
                    if (bin > 0 && bin < freqBins) {
                        var mirror = fftSize - bin;
                        spectrum[mirror * 2] = spectrum[bin * 2];
                        spectrum[mirror * 2 + 1] = -spectrum[bin * 2 + 1];
                    }
                }
            }

            var timeDomain = self._ifft(spectrum, fftSize);
            for (var i = 0; i < fftSize; i++) {
                var idx = offset + i;
                if (idx < outLength) {
                    output[idx] += timeDomain[i] * windowFn[i];
                    normBuf[idx] += windowFn[i] * windowFn[i];
                }
            }
        }

        var pct = Math.round((frame / numFrames) * 100);
        document.getElementById('loading-text').textContent = 'Band isolation... ' + pct + '%';

        if (frame < numFrames) {
            setTimeout(processChunk, 0);
        } else {
            for (var i = 0; i < outLength; i++) {
                if (normBuf[i] > 1e-8) output[i] /= normBuf[i];
            }

            self.isolatedBuffer = self.audioCtx.createBuffer(1, outLength, self.sampleRate);
            self.isolatedBuffer.getChannelData(0).set(output);
            self.activeBuffer = self.isolatedBuffer;

            self.enhancementHistory.push({
                action: 'Band isolation',
                timestamp: new Date().toISOString(),
                params: { lowFreq: Math.round(loFreq) + ' Hz', highFreq: Math.round(hiFreq) + ' Hz' }
            });

            self._hideLoading();
            mc1Toast('Band isolated: ' + Math.round(loFreq) + ' - ' + Math.round(hiFreq) + ' Hz', 'ok');
            self._recomputeFromBuffer(self.isolatedBuffer);
        }
    };

    setTimeout(processChunk, 10);
};

/* ══════════════════════════════════════════════════════════════
 *  AMPLITUDE ENVELOPE
 * ══════════════════════════════════════════════════════════════ */

/**
 * Compute RMS amplitude envelope with a sliding window.
 * Stores result in self.envelopeData and redraws the waveform with overlay.
 */
ForensicAnalyzer.prototype.computeEnvelope = function() {
    var self = this;
    if (!self.audioBuffer) {
        mc1Toast('No audio loaded', 'warn');
        return;
    }

    var rawData = self._getMonoData(self.audioBuffer);
    var windowSamples = Math.floor(self.envelopeWindowMs * self.sampleRate / 1000);
    if (windowSamples < 1) windowSamples = 1;
    var halfWin = Math.floor(windowSamples / 2);
    var len = rawData.length;
    var env = new Float32Array(len);

    /* Running sum of squares for efficiency */
    var sumSq = 0;
    for (var i = 0; i < Math.min(windowSamples, len); i++) {
        sumSq += rawData[i] * rawData[i];
    }

    for (var i = 0; i < len; i++) {
        var winStart = i - halfWin;
        var winEnd = i + halfWin;

        /* Slide window: add new sample, remove old */
        if (i > 0) {
            var addIdx = winEnd;
            var remIdx = winStart - 1;
            if (addIdx >= 0 && addIdx < len) sumSq += rawData[addIdx] * rawData[addIdx];
            if (remIdx >= 0 && remIdx < len) sumSq -= rawData[remIdx] * rawData[remIdx];
        }

        var count = Math.min(winEnd, len - 1) - Math.max(winStart, 0) + 1;
        env[i] = count > 0 ? Math.sqrt(Math.max(0, sumSq) / count) : 0;
    }

    self.envelopeData = env;
    self.showEnvelope = true;
    self._drawWaveform();
    mc1Toast('Envelope computed (window: ' + self.envelopeWindowMs + ' ms)', 'ok');
};

ForensicAnalyzer.prototype.toggleEnvelope = function() {
    var self = this;
    if (!self.envelopeData) {
        self.computeEnvelope();
        return;
    }
    self.showEnvelope = !self.showEnvelope;
    self._drawWaveform();
};

ForensicAnalyzer.prototype.setEnvelopeWindow = function(ms) {
    this.envelopeWindowMs = ms;
    if (this.showEnvelope && this.audioBuffer) {
        this.computeEnvelope();
    }
};

/* ══════════════════════════════════════════════════════════════
 *  WSOLA — Pitch-Preserved Speed Change
 * ══════════════════════════════════════════════════════════════ */

/**
 * WSOLA (Waveform Similarity Overlap-Add) time-stretching.
 * Changes playback speed without changing pitch.
 *
 * @param {AudioBuffer} inputBuffer - Source audio
 * @param {number} speed - Playback speed factor (0.25 to 4.0)
 * @returns {AudioBuffer} Time-stretched audio at original sample rate
 */
ForensicAnalyzer.prototype._wsola = function(inputBuffer, speed) {
    var sr = inputBuffer.sampleRate;
    var input = this._getMonoData(inputBuffer);
    var inputLen = input.length;
    var windowSize = 2048;
    var hopAnalysis = 512;
    var hopSynthesis = Math.round(hopAnalysis / speed);
    var searchRegion = 256;

    /* Hann window */
    var win = new Float32Array(windowSize);
    for (var i = 0; i < windowSize; i++) {
        win[i] = 0.5 * (1 - Math.cos(2 * Math.PI * i / (windowSize - 1)));
    }

    var outputLen = Math.round(inputLen / speed);
    var output = new Float32Array(outputLen);
    var normBuf = new Float32Array(outputLen);

    var analysisPos = 0;
    var synthesisPos = 0;

    while (analysisPos + windowSize < inputLen && synthesisPos + windowSize < outputLen) {
        /* Find best overlap point via cross-correlation in search region */
        var bestOffset = 0;
        var bestCorr = -Infinity;
        var searchStart = Math.max(0, analysisPos - searchRegion);
        var searchEnd = Math.min(inputLen - windowSize, analysisPos + searchRegion);

        for (var s = searchStart; s <= searchEnd; s++) {
            var corr = 0;
            /* Sample-based correlation for speed (check every 4th sample) */
            for (var i = 0; i < windowSize; i += 4) {
                corr += input[s + i] * input[analysisPos + i];
            }
            if (corr > bestCorr) {
                bestCorr = corr;
                bestOffset = s;
            }
        }

        /* Overlap-add with window at best position */
        for (var i = 0; i < windowSize; i++) {
            var outIdx = synthesisPos + i;
            if (outIdx < outputLen && bestOffset + i < inputLen) {
                output[outIdx] += input[bestOffset + i] * win[i];
                normBuf[outIdx] += win[i] * win[i];
            }
        }

        analysisPos += hopAnalysis;
        synthesisPos += hopSynthesis;
    }

    /* Normalize */
    for (var i = 0; i < outputLen; i++) {
        if (normBuf[i] > 1e-8) output[i] /= normBuf[i];
    }

    var outBuffer = this.audioCtx.createBuffer(1, outputLen, sr);
    outBuffer.getChannelData(0).set(output);
    return outBuffer;
};

ForensicAnalyzer.prototype.setPreservePitch = function(enabled) {
    this.preservePitch = enabled;
};

/* ══════════════════════════════════════════════════════════════
 *  SPECTRUM PEAK DETECTION
 * ══════════════════════════════════════════════════════════════ */

/**
 * Find the N strongest frequency peaks in the selected region (or full spectrum).
 * Uses a simple local maximum detection with threshold and minimum distance.
 */
ForensicAnalyzer.prototype.findPeaks = function() {
    var self = this;
    if (!self.spectrogramData || !self.specWidth || !self.specHeight) {
        mc1Toast('No spectrogram data', 'warn');
        return;
    }

    var threshold = self.peakThreshold;
    var minDistHz = self.peakMinDistance;
    var nyquist = self.sampleRate / 2;
    var minDistBins = Math.max(1, Math.floor((minDistHz / nyquist) * self.specHeight));

    /* Determine column range from time selection */
    var colStart = 0;
    var colEnd = self.specWidth - 1;
    if (self.selStartTime >= 0 && self.selEndTime >= 0) {
        var t0 = Math.min(self.selStartTime, self.selEndTime);
        var t1 = Math.max(self.selStartTime, self.selEndTime);
        colStart = Math.max(0, Math.floor((t0 / self.duration) * self.specWidth));
        colEnd = Math.min(self.specWidth - 1, Math.ceil((t1 / self.duration) * self.specWidth));
    }

    /* Average spectrum across selected time range */
    var avgSpectrum = new Float32Array(self.specHeight);
    var numCols = colEnd - colStart + 1;
    for (var r = 0; r < self.specHeight; r++) {
        var sum = 0;
        for (var c = colStart; c <= colEnd; c++) {
            sum += self.spectrogramData[r * self.specWidth + c];
        }
        avgSpectrum[r] = sum / numCols;
    }

    /* Find local maxima above threshold */
    var rawPeaks = [];
    for (var r = 1; r < self.specHeight - 1; r++) {
        if (avgSpectrum[r] > threshold
            && avgSpectrum[r] > avgSpectrum[r - 1]
            && avgSpectrum[r] > avgSpectrum[r + 1]) {
            rawPeaks.push({ bin: r, dB: avgSpectrum[r] });
        }
    }

    /* Sort by magnitude descending */
    rawPeaks.sort(function(a, b) { return b.dB - a.dB; });

    /* Apply minimum distance constraint (greedy) */
    var peaks = [];
    for (var i = 0; i < rawPeaks.length; i++) {
        var p = rawPeaks[i];
        var tooClose = false;
        for (var j = 0; j < peaks.length; j++) {
            if (Math.abs(p.bin - peaks[j].bin) < minDistBins) {
                tooClose = true;
                break;
            }
        }
        if (!tooClose) {
            peaks.push(p);
            if (peaks.length >= 20) break; /* Max 20 peaks */
        }
    }

    /* Convert to frequency */
    self.detectedPeaks = [];
    var midCol = Math.floor((colStart + colEnd) / 2);
    var midTime = (midCol / self.specWidth) * self.duration;
    for (var i = 0; i < peaks.length; i++) {
        var freq = (peaks[i].bin / self.specHeight) * nyquist;
        self.detectedPeaks.push({
            freq: freq,
            dB: peaks[i].dB,
            time: midTime,
            bin: peaks[i].bin
        });
    }

    self._drawOverlay();
    mc1Toast('Found ' + peaks.length + ' peaks above ' + threshold + ' dB', 'ok');
};

ForensicAnalyzer.prototype.setPeakThreshold = function(dB) {
    this.peakThreshold = dB;
};

ForensicAnalyzer.prototype.setPeakMinDistance = function(hz) {
    this.peakMinDistance = hz;
};

ForensicAnalyzer.prototype.clearPeaks = function() {
    this.detectedPeaks = [];
    this._drawOverlay();
};

/* ══════════════════════════════════════════════════════════════
 *  SIDE-BY-SIDE COMPARE MODE
 * ══════════════════════════════════════════════════════════════ */

/**
 * Toggle full compare mode with two spectrograms and a difference view.
 */
ForensicAnalyzer.prototype.toggleCompare = function() {
    var self = this;
    self.compareMode = !self.compareMode;
    var btn = document.getElementById('btn-compare');
    btn.classList.toggle('btn-primary', self.compareMode);
    btn.classList.toggle('btn-secondary', !self.compareMode);

    var container = document.getElementById('compare-container');
    if (self.compareMode) {
        if (container) container.classList.add('active');
        mc1Toast('Compare mode: load a second file using the File B loader below', 'ok');
    } else {
        if (container) container.classList.remove('active');
        self.compareBuffer = null;
        self.compareSpecData = null;
        mc1Toast('Compare mode off', 'ok');
    }
};

/**
 * Load the second file for comparison.
 */
ForensicAnalyzer.prototype.loadCompareFile = function(file) {
    var self = this;
    if (!self.audioCtx) {
        self.audioCtx = new (window.AudioContext || window.webkitAudioContext)();
    }

    self._showLoading('Decoding compare file...');
    var reader = new FileReader();
    reader.onload = function(ev) {
        self.audioCtx.decodeAudioData(ev.target.result, function(buffer) {
            self.compareBuffer = buffer;

            document.getElementById('compare-file-info').textContent =
                self._esc(file.name) + ' | ' + buffer.sampleRate + 'Hz ' + buffer.numberOfChannels + 'ch | ' + self._fmtTime(buffer.duration);

            /* Compute spectrogram for file B */
            self._computeCompareSpectrogram(buffer);
        }, function(err) {
            self._hideLoading();
            mc1Toast('Failed to decode compare file: ' + (err.message || err), 'err');
        });
    };
    reader.readAsArrayBuffer(file);
};

ForensicAnalyzer.prototype._computeCompareSpectrogram = function(buffer) {
    var self = this;
    var rawData = self._getMonoData(buffer);
    var fftSize = self.fftSize;
    var hopSize = Math.floor(fftSize * self.hopRatio);
    var freqBins = fftSize / 2;
    var numFrames = Math.floor((rawData.length - fftSize) / hopSize) + 1;
    if (numFrames < 1) {
        self._hideLoading();
        return;
    }

    var maxFrames = 8192;
    if (numFrames > maxFrames) {
        hopSize = Math.floor((rawData.length - fftSize) / maxFrames);
        numFrames = maxFrames;
    }

    var windowFn = self._computeWindow(fftSize, self.windowType);
    var specData = new Float32Array(freqBins * numFrames);
    var frame = 0;
    var chunkSize = 64;

    var processChunk = function() {
        var end = Math.min(frame + chunkSize, numFrames);
        for (; frame < end; frame++) {
            var offset = frame * hopSize;
            var windowed = new Float32Array(fftSize);
            for (var i = 0; i < fftSize; i++) {
                var idx = offset + i;
                windowed[i] = (idx < rawData.length ? rawData[idx] : 0) * windowFn[i];
            }
            var spectrum = self._fft(windowed);
            for (var bin = 0; bin < freqBins; bin++) {
                var re = spectrum[bin * 2];
                var im = spectrum[bin * 2 + 1];
                var mag = Math.sqrt(re * re + im * im) / fftSize;
                var dB = mag > 0 ? 20 * Math.log10(mag) : -120;
                specData[bin * numFrames + frame] = dB;
            }
        }

        document.getElementById('loading-text').textContent =
            'Computing compare spectrogram... ' + Math.round((frame / numFrames) * 100) + '%';

        if (frame < numFrames) {
            setTimeout(processChunk, 0);
        } else {
            self.compareSpecData = specData;
            self.compareSpecWidth = numFrames;
            self.compareSpecHeight = freqBins;

            /* Render file B spectrogram */
            var canvasB = document.getElementById('compare-canvas-b');
            if (canvasB) {
                if (!self.compareHqSpec) {
                    self.compareHqSpec = new HQSpectrogram(canvasB);
                }
                self.compareHqSpec.setSpectrogramData(specData);
                self.compareHqSpec.uploadSpectrogram(specData, numFrames, freqBins, buffer.duration, buffer.sampleRate, fftSize);
                self.compareHqSpec.draw();
            }

            /* Compute and render difference spectrogram */
            self._computeDiffSpectrogram(numFrames, freqBins, buffer.duration, buffer.sampleRate);

            self._hideLoading();
            mc1Toast('Compare spectrogram ready', 'ok');
        }
    };

    setTimeout(processChunk, 10);
};

ForensicAnalyzer.prototype._computeDiffSpectrogram = function(bFrames, freqBins, bDuration, bSampleRate) {
    var self = this;
    if (!self.spectrogramData || !self.compareSpecData) return;

    /* Use the smaller dimensions */
    var frames = Math.min(self.specWidth, bFrames);
    var bins = Math.min(self.specHeight, freqBins);
    var diffData = new Float32Array(bins * frames);

    for (var r = 0; r < bins; r++) {
        for (var c = 0; c < frames; c++) {
            var valA = self.spectrogramData[r * self.specWidth + c];
            var valB = self.compareSpecData[r * bFrames + c];
            diffData[r * frames + c] = Math.abs(valA - valB);
        }
    }

    var canvasDiff = document.getElementById('compare-canvas-diff');
    if (canvasDiff) {
        if (!self.diffHqSpec) {
            self.diffHqSpec = new HQSpectrogram(canvasDiff);
        }
        self.diffHqSpec.setSpectrogramData(diffData);
        var dur = Math.min(self.duration || 1, bDuration || 1);
        var sr = bSampleRate || self.sampleRate;
        self.diffHqSpec.uploadSpectrogram(diffData, frames, bins, dur, sr, self.fftSize);
        self.diffHqSpec.setColormap('inferno');
        self.diffHqSpec.draw();
    }
};

ForensicAnalyzer.prototype.toggleDiffView = function() {
    var diffPanel = document.getElementById('compare-diff-panel');
    if (diffPanel) {
        var visible = diffPanel.style.display !== 'none';
        diffPanel.style.display = visible ? 'none' : 'block';
    }
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

/* ══════════════════════════════════════════════════════════════
 *  SHA-256 HASH COMPUTATION (Web Crypto API)
 * ══════════════════════════════════════════════════════════════ */

/**
 * Compute SHA-256 hash of an ArrayBuffer using the Web Crypto API.
 * Returns a Promise resolving to a hex string.
 */
ForensicAnalyzer.prototype._computeSHA256 = function(arrayBuffer) {
    if (!window.crypto || !window.crypto.subtle) {
        return Promise.resolve('SHA-256 unavailable (no Web Crypto API)');
    }
    return window.crypto.subtle.digest('SHA-256', arrayBuffer).then(function(hashBuffer) {
        var hashArray = Array.from(new Uint8Array(hashBuffer));
        return hashArray.map(function(b) { return b.toString(16).padStart(2, '0'); }).join('');
    }).catch(function() {
        return 'SHA-256 computation failed';
    });
};

/* ══════════════════════════════════════════════════════════════
 *  EVENT DETECTION
 * ══════════════════════════════════════════════════════════════ */

/**
 * Detect notable audio events: silence gaps, transients, tonal events, clicks/pops.
 * Client-side implementation using the decoded audio buffer.
 */
ForensicAnalyzer.prototype.detectEvents = function() {
    var self = this;
    if (!self.audioBuffer) {
        mc1Toast('No audio loaded', 'warn');
        return;
    }

    self._showLoading('Detecting events...');
    self.detectedEvents = [];

    var rawData = self._getMonoData(self.audioBuffer);
    var sr = self.sampleRate;
    var len = rawData.length;

    /* Parameters */
    var silenceThresholdDB = -50;
    var silenceMinDurationMs = 500;
    var transientThresholdDB = 12;
    var tonalMinDurationMs = 2000;
    var clickMaxDurationMs = 10;

    var silenceThreshold = Math.pow(10, silenceThresholdDB / 20);
    var blockSize = Math.floor(sr * 0.01); /* 10ms blocks */
    var numBlocks = Math.floor(len / blockSize);

    /* Compute block RMS values */
    var blockRMS = new Float32Array(numBlocks);
    for (var b = 0; b < numBlocks; b++) {
        var sumSq = 0;
        var start = b * blockSize;
        for (var i = 0; i < blockSize; i++) {
            var s = rawData[start + i];
            sumSq += s * s;
        }
        blockRMS[b] = Math.sqrt(sumSq / blockSize);
    }

    /* Detect silence gaps */
    var silenceMinBlocks = Math.ceil(silenceMinDurationMs / 10);
    var inSilence = false;
    var silenceStart = 0;
    for (var b = 0; b < numBlocks; b++) {
        if (blockRMS[b] < silenceThreshold) {
            if (!inSilence) {
                inSilence = true;
                silenceStart = b;
            }
        } else {
            if (inSilence) {
                var durBlocks = b - silenceStart;
                if (durBlocks >= silenceMinBlocks) {
                    self.detectedEvents.push({
                        type: 'silence',
                        startTime: (silenceStart * blockSize) / sr,
                        endTime: (b * blockSize) / sr,
                        freq: 0,
                        magnitude: -96,
                        label: 'Silence (' + (durBlocks * 10) + 'ms)'
                    });
                }
                inSilence = false;
            }
        }
    }
    if (inSilence) {
        var durBlocks = numBlocks - silenceStart;
        if (durBlocks >= silenceMinBlocks) {
            self.detectedEvents.push({
                type: 'silence',
                startTime: (silenceStart * blockSize) / sr,
                endTime: (numBlocks * blockSize) / sr,
                freq: 0,
                magnitude: -96,
                label: 'Silence (' + (durBlocks * 10) + 'ms)'
            });
        }
    }

    /* Detect transients (sudden amplitude spikes >12dB) */
    for (var b = 1; b < numBlocks; b++) {
        var prevDB = blockRMS[b - 1] > 0 ? 20 * Math.log10(blockRMS[b - 1]) : -120;
        var currDB = blockRMS[b] > 0 ? 20 * Math.log10(blockRMS[b]) : -120;
        var jump = currDB - prevDB;
        if (jump > transientThresholdDB) {
            self.detectedEvents.push({
                type: 'transient',
                startTime: (b * blockSize) / sr,
                endTime: ((b + 1) * blockSize) / sr,
                freq: 0,
                magnitude: currDB,
                label: 'Transient (+' + jump.toFixed(1) + 'dB)'
            });
        }
    }

    /* Detect clicks/pops (very short impulses <10ms) */
    var clickMinBlocks = 1;
    var clickMaxBlocks = Math.ceil(clickMaxDurationMs / 10);
    var medianRMS = self._computeMedianRMS(blockRMS);
    var clickThreshold = medianRMS * 8; /* 8x the median RMS */
    var inClick = false;
    var clickStart = 0;
    for (var b = 0; b < numBlocks; b++) {
        if (blockRMS[b] > clickThreshold) {
            if (!inClick) {
                inClick = true;
                clickStart = b;
            }
        } else {
            if (inClick) {
                var dur = b - clickStart;
                if (dur >= clickMinBlocks && dur <= clickMaxBlocks) {
                    var peakDB = 0;
                    for (var bb = clickStart; bb < b; bb++) {
                        var db = blockRMS[bb] > 0 ? 20 * Math.log10(blockRMS[bb]) : -120;
                        if (db > peakDB) peakDB = db;
                    }
                    self.detectedEvents.push({
                        type: 'click',
                        startTime: (clickStart * blockSize) / sr,
                        endTime: (b * blockSize) / sr,
                        freq: 0,
                        magnitude: peakDB,
                        label: 'Click/Pop (' + (dur * 10) + 'ms)'
                    });
                }
                inClick = false;
            }
        }
    }

    /* Detect tonal events (sustained frequency components >2s) using spectrogram data */
    if (self.spectrogramData && self.specWidth > 0 && self.specHeight > 0) {
        var tonalMinFrames = Math.ceil((tonalMinDurationMs / 1000) / (self.duration / self.specWidth));
        var nyquist = sr / 2;

        /* For each frequency bin, find runs where magnitude > -40dB */
        for (var r = 1; r < self.specHeight; r += 4) { /* Step by 4 for speed */
            var inTone = false;
            var toneStart = 0;
            for (var c = 0; c < self.specWidth; c++) {
                var val = self.spectrogramData[r * self.specWidth + c];
                if (val > -40) {
                    if (!inTone) {
                        inTone = true;
                        toneStart = c;
                    }
                } else {
                    if (inTone) {
                        var dur = c - toneStart;
                        if (dur >= tonalMinFrames) {
                            var freq = (r / self.specHeight) * nyquist;
                            var startT = (toneStart / self.specWidth) * self.duration;
                            var endT = (c / self.specWidth) * self.duration;
                            /* Avoid duplicates near same frequency */
                            var hasDup = false;
                            for (var d = 0; d < self.detectedEvents.length; d++) {
                                var de = self.detectedEvents[d];
                                if (de.type === 'tonal' && Math.abs(de.freq - freq) < 50 &&
                                    Math.abs(de.startTime - startT) < 0.5) {
                                    hasDup = true;
                                    break;
                                }
                            }
                            if (!hasDup) {
                                self.detectedEvents.push({
                                    type: 'tonal',
                                    startTime: startT,
                                    endTime: endT,
                                    freq: freq,
                                    magnitude: -40,
                                    label: 'Tonal ' + Math.round(freq) + 'Hz (' + (endT - startT).toFixed(1) + 's)'
                                });
                            }
                        }
                        inTone = false;
                    }
                }
            }
        }
    }

    /* Sort by start time */
    self.detectedEvents.sort(function(a, b) { return a.startTime - b.startTime; });

    self._hideLoading();
    self._renderEventList();
    self._drawOverlay();
    mc1Toast('Detected ' + self.detectedEvents.length + ' events', 'ok');
};

/**
 * Compute median of a Float32Array (for click detection baseline).
 */
ForensicAnalyzer.prototype._computeMedianRMS = function(arr) {
    var sorted = Array.from(arr).filter(function(v) { return v > 0; }).sort(function(a, b) { return a - b; });
    if (sorted.length === 0) return 0.001;
    return sorted[Math.floor(sorted.length / 2)];
};

/**
 * Render the event list panel in the UI.
 */
ForensicAnalyzer.prototype._renderEventList = function() {
    var self = this;
    var list = document.getElementById('event-list');
    if (!list) return;

    if (self.detectedEvents.length === 0) {
        list.innerHTML = '<div class="empty" style="padding:12px;font-size:12px;color:var(--muted)">'
            + '<i class="fa-solid fa-magnifying-glass fa-fw"></i> No events detected</div>';
        return;
    }

    var eventIcons = {
        silence: { icon: '&#x1f507;', cls: 'evt-silence' },
        transient: { icon: '&#x26a1;', cls: 'evt-transient' },
        tonal: { icon: '&#x1f3b5;', cls: 'evt-tonal' },
        click: { icon: '&#x1f4a5;', cls: 'evt-click' }
    };

    var html = '';
    for (var i = 0; i < self.detectedEvents.length; i++) {
        var ev = self.detectedEvents[i];
        var meta = eventIcons[ev.type] || { icon: '?', cls: '' };
        html += '<div class="event-item ' + meta.cls + '" onclick="forensic.jumpToEvent(' + i + ')">'
            + '<span class="event-icon">' + meta.icon + '</span>'
            + '<span class="event-time">' + self._fmtTime(ev.startTime) + '</span>'
            + '<span class="event-label">' + self._esc(ev.label) + '</span>'
            + '</div>';
    }
    list.innerHTML = html;
};

/**
 * Jump to a detected event's position on the spectrogram.
 */
ForensicAnalyzer.prototype.jumpToEvent = function(idx) {
    var ev = this.detectedEvents[idx];
    if (!ev || !this.hqSpec) return;
    var viewW = this.hqSpec._viewW;
    this.hqSpec._viewX = Math.max(0, ev.startTime - viewW / 4);
    this.hqSpec._clampView();
    this.hqSpec.draw();
    this._drawOverlay();
    this._updateAxes();
    this._drawMinimap();
};

/**
 * Filter events by type in the list.
 */
ForensicAnalyzer.prototype.filterEvents = function(type) {
    var list = document.getElementById('event-list');
    if (!list) return;
    var items = list.querySelectorAll('.event-item');
    for (var i = 0; i < items.length; i++) {
        if (type === 'all') {
            items[i].style.display = '';
        } else {
            items[i].style.display = items[i].classList.contains('evt-' + type) ? '' : 'none';
        }
    }
};

/* ══════════════════════════════════════════════════════════════
 *  GONIOMETER (Stereo Phase Correlation)
 * ══════════════════════════════════════════════════════════════ */

/**
 * Initialize the Lissajous goniometer display for stereo files.
 * Uses WebGL point cloud rendering (GL_POINTS).
 */
ForensicAnalyzer.prototype._initGoniometer = function() {
    var self = this;
    var canvas = document.getElementById('goniometer-canvas');
    if (!canvas) return;

    canvas.parentElement.style.display = 'block';
    self.goniometerCanvas = canvas;
    canvas.width = 200;
    canvas.height = 200;

    var gl = canvas.getContext('webgl', { antialias: true, alpha: true });
    if (!gl) {
        /* Fallback to 2D canvas */
        self.goniometerGL = null;
        self._drawGoniometer2D();
        return;
    }
    self.goniometerGL = gl;

    /* Compile shaders */
    var vsSource = 'attribute vec2 aPos;void main(){gl_PointSize=1.5;gl_Position=vec4(aPos,0.0,1.0);}';
    var fsSource = 'precision mediump float;void main(){gl_FragColor=vec4(0.08,0.72,0.65,0.4);}';

    function compileShader(src, type) {
        var s = gl.createShader(type);
        gl.shaderSource(s, src);
        gl.compileShader(s);
        return s;
    }

    var vs = compileShader(vsSource, gl.VERTEX_SHADER);
    var fs = compileShader(fsSource, gl.FRAGMENT_SHADER);
    var prog = gl.createProgram();
    gl.attachShader(prog, vs);
    gl.attachShader(prog, fs);
    gl.linkProgram(prog);
    gl.useProgram(prog);

    self._gonioProgram = prog;
    self._gonioPosLoc = gl.getAttribLocation(prog, 'aPos');
    self._gonioVBO = gl.createBuffer();

    gl.viewport(0, 0, 200, 200);
    gl.clearColor(0.03, 0.05, 0.09, 1.0);
    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

    self._drawGoniometerStatic();
};

/**
 * Draw static goniometer overview from the full audio buffer.
 */
ForensicAnalyzer.prototype._drawGoniometerStatic = function() {
    var self = this;
    if (!self.audioBuffer || self.audioBuffer.numberOfChannels < 2) return;

    var left = self.audioBuffer.getChannelData(0);
    var right = self.audioBuffer.getChannelData(1);
    var len = left.length;

    /* Downsample to max 50000 points */
    var maxPts = 50000;
    var step = Math.max(1, Math.floor(len / maxPts));
    var pts = [];

    for (var i = 0; i < len; i += step) {
        var l = left[i];
        var r = right[i];
        /* Lissajous: X = (L+R)/2, Y = (L-R)/2 */
        var x = (l + r) * 0.5;
        var y = (l - r) * 0.5;
        /* Clamp to [-1, 1] */
        pts.push(Math.max(-1, Math.min(1, x)));
        pts.push(Math.max(-1, Math.min(1, y)));
    }

    var gl = self.goniometerGL;
    if (gl) {
        var data = new Float32Array(pts);
        gl.clear(gl.COLOR_BUFFER_BIT);

        /* Draw crosshair guides */
        self._drawGonioCrosshair(gl);

        gl.bindBuffer(gl.ARRAY_BUFFER, self._gonioVBO);
        gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
        gl.enableVertexAttribArray(self._gonioPosLoc);
        gl.vertexAttribPointer(self._gonioPosLoc, 2, gl.FLOAT, false, 0, 0);
        gl.useProgram(self._gonioProgram);
        gl.drawArrays(gl.POINTS, 0, data.length / 2);
    } else {
        self._drawGoniometer2D();
    }
};

/**
 * Draw crosshair on goniometer via WebGL lines overlay.
 */
ForensicAnalyzer.prototype._drawGonioCrosshair = function(gl) {
    /* We skip WebGL crosshair; the 2D canvas fallback and CSS border handles visual cues */
};

/**
 * 2D canvas fallback for goniometer when WebGL is unavailable.
 */
ForensicAnalyzer.prototype._drawGoniometer2D = function() {
    var self = this;
    var canvas = document.getElementById('goniometer-canvas');
    if (!canvas || !self.audioBuffer || self.audioBuffer.numberOfChannels < 2) return;

    var ctx = canvas.getContext('2d');
    var w = canvas.width;
    var h = canvas.height;
    var cx = w / 2;
    var cy = h / 2;

    ctx.clearRect(0, 0, w, h);
    ctx.fillStyle = 'rgba(8, 12, 24, 1)';
    ctx.fillRect(0, 0, w, h);

    /* Crosshair */
    ctx.strokeStyle = 'rgba(100, 116, 139, 0.3)';
    ctx.lineWidth = 0.5;
    ctx.beginPath();
    ctx.moveTo(cx, 0); ctx.lineTo(cx, h);
    ctx.moveTo(0, cy); ctx.lineTo(w, cy);
    /* Diagonal */
    ctx.moveTo(0, 0); ctx.lineTo(w, h);
    ctx.moveTo(w, 0); ctx.lineTo(0, h);
    ctx.stroke();

    /* Labels */
    ctx.font = '9px sans-serif';
    ctx.fillStyle = 'rgba(100, 116, 139, 0.6)';
    ctx.fillText('L', 2, cy - 4);
    ctx.fillText('R', w - 10, cy - 4);
    ctx.fillText('+', cx + 4, 12);
    ctx.fillText('-', cx + 4, h - 4);

    /* Plot points */
    var left = self.audioBuffer.getChannelData(0);
    var right = self.audioBuffer.getChannelData(1);
    var len = left.length;
    var maxPts = 40000;
    var step = Math.max(1, Math.floor(len / maxPts));

    ctx.fillStyle = 'rgba(20, 184, 166, 0.15)';
    for (var i = 0; i < len; i += step) {
        var x = (left[i] + right[i]) * 0.5;
        var y = (left[i] - right[i]) * 0.5;
        var px = cx + x * cx;
        var py = cy - y * cy;
        ctx.fillRect(px, py, 1.5, 1.5);
    }
};

/**
 * Update goniometer during playback (real-time mode).
 */
ForensicAnalyzer.prototype._updateGoniometerRealtime = function(leftSamples, rightSamples) {
    var self = this;
    var canvas = document.getElementById('goniometer-canvas');
    if (!canvas) return;

    var ctx = canvas.getContext('2d');
    if (!ctx) return;
    var w = canvas.width;
    var h = canvas.height;
    var cx = w / 2;
    var cy = h / 2;

    /* Fade previous frame */
    ctx.fillStyle = 'rgba(8, 12, 24, 0.3)';
    ctx.fillRect(0, 0, w, h);

    /* Plot new samples */
    ctx.fillStyle = 'rgba(20, 184, 166, 0.5)';
    var len = Math.min(leftSamples.length, rightSamples.length);
    var step = Math.max(1, Math.floor(len / 2000));
    for (var i = 0; i < len; i += step) {
        var x = (leftSamples[i] + rightSamples[i]) * 0.5;
        var y = (leftSamples[i] - rightSamples[i]) * 0.5;
        ctx.fillRect(cx + x * cx, cy - y * cy, 2, 2);
    }
};

/**
 * Hide the goniometer panel (mono files).
 */
ForensicAnalyzer.prototype._hideGoniometer = function() {
    var panel = document.getElementById('goniometer-panel');
    if (panel) panel.style.display = 'none';
};

/* ══════════════════════════════════════════════════════════════
 *  EVENT MARKERS ON OVERLAY
 * ══════════════════════════════════════════════════════════════ */

/**
 * We extend _drawOverlay to also render event markers on the timeline.
 * Store reference to original and wrap it.
 */
(function() {
    var origDrawOverlay = ForensicAnalyzer.prototype._drawOverlay;
    ForensicAnalyzer.prototype._drawOverlay = function(playheadPos) {
        /* Call original overlay drawing */
        origDrawOverlay.call(this, playheadPos);

        /* Draw event markers */
        var self = this;
        if (!self.detectedEvents.length || !self.hqSpec || !self.duration) return;

        var canvas = self.overlayCanvas;
        var ctx = canvas.getContext('2d');

        var eventColors = {
            silence: 'rgba(148, 163, 184, 0.6)',
            transient: 'rgba(234, 179, 8, 0.7)',
            tonal: 'rgba(59, 130, 246, 0.6)',
            click: 'rgba(239, 68, 68, 0.7)'
        };

        for (var i = 0; i < self.detectedEvents.length; i++) {
            var ev = self.detectedEvents[i];
            var x0 = self.hqSpec.timeToCanvas(ev.startTime);
            var x1 = self.hqSpec.timeToCanvas(ev.endTime);
            var color = eventColors[ev.type] || 'rgba(255,255,255,0.4)';

            /* Draw a thin colored strip at the bottom */
            ctx.fillStyle = color;
            var stripH = 4;
            var stripY = canvas.height - stripH - 22; /* above cursor readout */
            ctx.fillRect(x0, stripY, Math.max(2, x1 - x0), stripH);

            /* Small triangle marker */
            ctx.beginPath();
            ctx.moveTo(x0, stripY);
            ctx.lineTo(x0 + 4, stripY - 5);
            ctx.lineTo(x0 - 4, stripY - 5);
            ctx.closePath();
            ctx.fillStyle = color;
            ctx.fill();
        }
    };
})();

/* ══════════════════════════════════════════════════════════════
 *  REPORT MODAL CLOSE
 * ══════════════════════════════════════════════════════════════ */

ForensicAnalyzer.prototype.closeReportModal = function() {
    var modal = document.getElementById('report-modal');
    if (modal) modal.classList.remove('show');
};

ForensicAnalyzer.prototype.confirmReport = function() {
    var self = this;
    var analystName = document.getElementById('report-analyst').value.trim() || 'Analyst';
    var caseNumber = document.getElementById('report-case').value.trim() || 'N/A';

    self.generateReport({
        analystName: analystName,
        caseNumber: caseNumber,
        includeSpectrogram: document.getElementById('rpt-inc-spec').checked,
        includeWaveform: document.getElementById('rpt-inc-wave').checked,
        includeAnnotations: document.getElementById('rpt-inc-anno').checked,
        includeMetadata: document.getElementById('rpt-inc-meta').checked,
        includeEnhanceLog: document.getElementById('rpt-inc-enhance').checked,
        includeEvents: document.getElementById('rpt-inc-events').checked
    });

    self.closeReportModal();
};
