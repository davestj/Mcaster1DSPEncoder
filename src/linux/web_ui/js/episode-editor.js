/**
 * episode-editor.js — Waveform Editor Engine for Podcast Episodes
 *
 * File:    src/linux/web_ui/js/episode-editor.js
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Phase:   PC-2
 * Purpose: We provide a non-destructive browser-based audio editor with waveform display,
 *          selection, playback, chapter management, and EDL-based editing operations.
 *
 * Standards:
 *  - We use HTML5 Audio for playback, Web Audio API for waveform analysis
 *  - We draw waveforms on Canvas 2D with pre-computed peaks
 *  - We maintain an Edit Decision List (EDL) for non-destructive edits
 *  - We support undo/redo with a 50-operation stack
 *  - We never modify the original audio file
 */

/* global mc1Api, mc1Toast */

function EpisodeEditor(opts) {
    var self = this;

    /* ── Config ── */
    self.ep         = opts.episodeData;
    self.chapters   = JSON.parse(JSON.stringify(opts.markers || []));
    self.canvasId   = opts.canvasId;
    self.overlayId  = opts.overlayId;
    self.rulerId    = opts.rulerId;

    /* ── State ── */
    self.audioCtx   = null;
    self.audioBuffer= null;
    self.audioEl    = null;
    self.peaks      = [];        // pre-computed peaks array [{min, max}]
    self.peakSR     = 0;         // peaks per second
    self.duration   = 0;         // total duration in seconds
    self.isPlaying  = false;
    self.isLooping  = false;

    /* ── View state ── */
    self.zoom       = 1;         // 1 = fit entire track, higher = more zoom
    self.scrollX    = 0;         // scroll offset in seconds
    self.playPos    = 0;         // current playback position in seconds

    /* ── Selection ── */
    self.selStart   = -1;        // selection start in seconds (-1 = none)
    self.selEnd     = -1;
    self.isDragging = false;
    self.dragStartX = 0;
    self.isPanning  = false;
    self.panStartX  = 0;
    self.panStartScroll = 0;

    /* ── EDL (Edit Decision List) ── */
    self.edl = {
        source_file: self.ep.file_path,
        operations: [],
        chapters: self.chapters.map(function(c) {
            return { timestamp_ms: c.timestamp_ms, title: c.title, url: c.url || '', image_url: c.image_url || '' };
        })
    };

    /* ── Undo/Redo stacks ── */
    self.undoStack = [];
    self.redoStack = [];
    self.MAX_UNDO  = 50;

    /* ── Export state ── */
    self.exportFmt  = 'mp3';
    self.exportOpts = '128k';

    /* ── Canvas refs ── */
    self.canvas     = document.getElementById(self.canvasId);
    self.overlay    = document.getElementById(self.overlayId);
    self.ruler      = document.getElementById(self.rulerId);
    self.ctx        = self.canvas.getContext('2d');
    self.octx       = self.overlay.getContext('2d');
    self.rctx       = self.ruler.getContext('2d');

    /* ── WebGL waveform renderer (optional, for large files) ── */
    self.useWebGL   = false;
    self.glWaveform = null;
    if (window.WebGLViz && WebGLViz.isWebGLAvailable() && WebGLViz.getWebGLPref()) {
        self.useWebGL = true;
    }

    /* ── Initialize ── */
    self._init();
}

/* ══════════════════════════════════════════════════════════════
 *  INITIALIZATION
 * ══════════════════════════════════════════════════════════════ */

EpisodeEditor.prototype._init = function() {
    var self = this;

    self._resizeCanvases();
    self._bindTransport();
    self._bindKeyboard();
    self._bindCanvasEvents();
    self._bindZoom();
    self._loadAudio();

    /* We start the render loop */
    self._animFrame = null;
    self._startRenderLoop();
};

EpisodeEditor.prototype._resizeCanvases = function() {
    var self = this;
    var wrap = document.getElementById('ee-wave-wrap');
    var w = wrap.clientWidth;

    self.canvas.width = w;
    self.canvas.height = 200;
    self.overlay.width = w;
    self.overlay.height = 200;
    self.ruler.width = w;
    self.ruler.height = 24;

    /* We handle window resize */
    window.addEventListener('resize', function() {
        var nw = wrap.clientWidth;
        self.canvas.width = nw;
        self.canvas.height = 200;
        self.overlay.width = nw;
        self.overlay.height = 200;
        self.ruler.width = nw;
        self.ruler.height = 24;
        self._drawWaveform();
        self._drawOverlay();
        self._drawRuler();
    });
};

/* ══════════════════════════════════════════════════════════════
 *  AUDIO LOADING
 * ══════════════════════════════════════════════════════════════ */

EpisodeEditor.prototype._loadAudio = function() {
    var self = this;
    var loadingEl = document.getElementById('ee-loading');

    /* We create the Audio element for playback */
    var audioUrl = '/app/api/audio.php?path=' + encodeURIComponent(self.ep.file_path);
    self.audioEl = new Audio(audioUrl);
    self.audioEl.preload = 'auto';
    self.audioEl.crossOrigin = 'anonymous';

    /* We use Web Audio API to decode the full buffer for peak analysis */
    self.audioCtx = new (window.AudioContext || window.webkitAudioContext)();

    /* We fetch the audio file as an ArrayBuffer for decoding */
    fetch(audioUrl, { credentials: 'same-origin' })
        .then(function(r) {
            if (!r.ok) throw new Error('HTTP ' + r.status);
            return r.arrayBuffer();
        })
        .then(function(buf) {
            return self.audioCtx.decodeAudioData(buf);
        })
        .then(function(decoded) {
            self.audioBuffer = decoded;
            self.duration = decoded.duration;
            self._computePeaks();
            self._updateDurationDisplay();
            self._drawWaveform();
            self._drawRuler();
            self._renderChapters();
            if (loadingEl) loadingEl.style.display = 'none';
        })
        .catch(function(err) {
            if (loadingEl) {
                loadingEl.innerHTML = '<i class="fa-solid fa-triangle-exclamation" style="color:var(--red);margin-right:8px"></i> '
                    + 'Failed to load audio: ' + self._esc(err.message);
            }
        });

    /* We listen for Audio element events */
    self.audioEl.addEventListener('ended', function() {
        self.isPlaying = false;
        self._updatePlayButton();
    });
    self.audioEl.addEventListener('timeupdate', function() {
        self.playPos = self.audioEl.currentTime;
    });
};

EpisodeEditor.prototype._computePeaks = function() {
    var self = this;
    /* We compute peaks at ~200 peaks per second for responsive zooming */
    var pps = 200;
    var ch = self.audioBuffer.numberOfChannels;
    var sr = self.audioBuffer.sampleRate;
    var samplesPerPeak = Math.floor(sr / pps);
    var totalPeaks = Math.ceil(self.audioBuffer.length / samplesPerPeak);

    self.peaks = [];
    self.peakSR = pps;

    /* We mix all channels to mono for display */
    var data = self.audioBuffer.getChannelData(0);
    var data2 = ch > 1 ? self.audioBuffer.getChannelData(1) : null;

    for (var i = 0; i < totalPeaks; i++) {
        var start = i * samplesPerPeak;
        var end = Math.min(start + samplesPerPeak, self.audioBuffer.length);
        var mn = 0, mx = 0;
        for (var j = start; j < end; j++) {
            var v = data[j];
            if (data2) v = (v + data2[j]) * 0.5;
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }
        self.peaks.push({ min: mn, max: mx });
    }

    /* Initialize WebGL waveform renderer with computed peaks */
    if (self.useWebGL && window.WebGLViz) {
        self.glWaveform = new WebGLViz.WaveformPeaks(self.canvas);
        self.glWaveform.setPeaks(self.peaks, self.peakSR, self.duration);
    }
};

/* ══════════════════════════════════════════════════════════════
 *  WAVEFORM DRAWING
 * ══════════════════════════════════════════════════════════════ */

EpisodeEditor.prototype._drawWaveform = function() {
    var self = this;

    /* WebGL fast path for large files */
    if (self.glWaveform && self.useWebGL) {
        var visibleDur = self.duration / self.zoom;
        self.glWaveform.draw(self.scrollX, visibleDur);
        return;
    }

    var ctx = self.ctx;
    var w = self.canvas.width;
    var h = self.canvas.height;

    ctx.clearRect(0, 0, w, h);

    if (!self.peaks.length) return;

    /* We calculate the visible time range based on zoom and scroll */
    var visibleDur = self.duration / self.zoom;
    var startTime = self.scrollX;
    var endTime = startTime + visibleDur;

    /* We draw the background */
    ctx.fillStyle = '#0f172a';
    ctx.fillRect(0, 0, w, h);

    /* We draw a center line */
    ctx.strokeStyle = 'rgba(51,65,85,.6)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(0, h / 2);
    ctx.lineTo(w, h / 2);
    ctx.stroke();

    /* We map peaks to pixel columns */
    var startPeak = Math.floor(startTime * self.peakSR);
    var endPeak = Math.ceil(endTime * self.peakSR);
    var peakRange = endPeak - startPeak;
    if (peakRange <= 0) return;

    var mid = h / 2;
    var amp = (h / 2) - 4;

    /* We draw waveform bars */
    ctx.fillStyle = 'rgba(20,184,166,.7)';

    for (var px = 0; px < w; px++) {
        var pi = startPeak + Math.floor((px / w) * peakRange);
        var piEnd = startPeak + Math.floor(((px + 1) / w) * peakRange);
        if (pi < 0) pi = 0;
        if (piEnd > self.peaks.length) piEnd = self.peaks.length;

        var mn = 0, mx = 0;
        for (var k = pi; k < piEnd; k++) {
            if (k >= 0 && k < self.peaks.length) {
                if (self.peaks[k].min < mn) mn = self.peaks[k].min;
                if (self.peaks[k].max > mx) mx = self.peaks[k].max;
            }
        }

        var y1 = mid - mx * amp;
        var y2 = mid - mn * amp;
        var barH = Math.max(1, y2 - y1);
        ctx.fillRect(px, y1, 1, barH);
    }

    /* We draw cut/silence regions from EDL as darkened areas */
    self.edl.operations.forEach(function(op) {
        if (op.type === 'cut' || op.type === 'silence') {
            var x1 = self._timeToPixel(op.start_ms / 1000);
            var x2 = self._timeToPixel(op.end_ms / 1000);
            if (x2 > 0 && x1 < w) {
                ctx.fillStyle = op.type === 'cut'
                    ? 'rgba(239,68,68,.25)'
                    : 'rgba(234,179,8,.2)';
                ctx.fillRect(Math.max(0, x1), 0, Math.min(w, x2) - Math.max(0, x1), h);
            }
        }
    });
};

EpisodeEditor.prototype._drawOverlay = function() {
    var self = this;
    var ctx = self.octx;
    var w = self.overlay.width;
    var h = self.overlay.height;

    ctx.clearRect(0, 0, w, h);

    /* We draw the selection region */
    if (self.selStart >= 0 && self.selEnd >= 0 && self.selStart !== self.selEnd) {
        var x1 = self._timeToPixel(Math.min(self.selStart, self.selEnd));
        var x2 = self._timeToPixel(Math.max(self.selStart, self.selEnd));
        ctx.fillStyle = 'rgba(20,184,166,.18)';
        ctx.fillRect(x1, 0, x2 - x1, h);
        ctx.strokeStyle = 'rgba(20,184,166,.6)';
        ctx.lineWidth = 1;
        ctx.strokeRect(x1, 0, x2 - x1, h);
    }

    /* We draw chapter markers */
    self.chapters.forEach(function(ch) {
        var x = self._timeToPixel(ch.timestamp_ms / 1000);
        if (x < 0 || x > w) return;

        ctx.strokeStyle = 'rgba(249,115,22,.7)';
        ctx.lineWidth = 1;
        ctx.setLineDash([3, 3]);
        ctx.beginPath();
        ctx.moveTo(x, 0);
        ctx.lineTo(x, h);
        ctx.stroke();
        ctx.setLineDash([]);

        /* We draw the chapter label */
        if (ch.title) {
            ctx.font = '10px -apple-system, BlinkMacSystemFont, sans-serif';
            ctx.fillStyle = 'rgba(249,115,22,.9)';
            var tw = ctx.measureText(ch.title).width;
            var lx = Math.min(x + 3, w - tw - 4);
            ctx.fillRect(lx - 2, 2, tw + 4, 14);
            ctx.fillStyle = '#fff';
            ctx.fillText(ch.title, lx, 12);
        }
    });

    /* We draw the playback head */
    var px = self._timeToPixel(self.playPos);
    if (px >= 0 && px <= w) {
        ctx.strokeStyle = '#ef4444';
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.moveTo(px, 0);
        ctx.lineTo(px, h);
        ctx.stroke();

        /* We draw a small triangle at the top */
        ctx.fillStyle = '#ef4444';
        ctx.beginPath();
        ctx.moveTo(px - 5, 0);
        ctx.lineTo(px + 5, 0);
        ctx.lineTo(px, 7);
        ctx.closePath();
        ctx.fill();
    }
};

EpisodeEditor.prototype._drawRuler = function() {
    var self = this;
    var ctx = self.rctx;
    var w = self.ruler.width;
    var h = self.ruler.height;

    ctx.clearRect(0, 0, w, h);
    ctx.fillStyle = 'rgba(0,0,0,.3)';
    ctx.fillRect(0, 0, w, h);

    if (!self.duration) return;

    var visibleDur = self.duration / self.zoom;
    var startTime = self.scrollX;

    /* We determine tick spacing based on zoom level */
    var targetTicks = w / 80;
    var tickInterval = self._niceInterval(visibleDur / targetTicks);

    var firstTick = Math.ceil(startTime / tickInterval) * tickInterval;

    ctx.font = '10px SF Mono, Fira Code, monospace';
    ctx.fillStyle = '#64748b';
    ctx.strokeStyle = '#334155';
    ctx.lineWidth = 1;

    for (var t = firstTick; t <= startTime + visibleDur; t += tickInterval) {
        var x = self._timeToPixel(t);
        if (x < 0 || x > w) continue;

        ctx.beginPath();
        ctx.moveTo(x, 0);
        ctx.lineTo(x, 6);
        ctx.stroke();

        ctx.fillText(self._fmtTimeShort(t), x + 3, 18);
    }
};

/* ══════════════════════════════════════════════════════════════
 *  COORDINATE HELPERS
 * ══════════════════════════════════════════════════════════════ */

EpisodeEditor.prototype._timeToPixel = function(timeSec) {
    var self = this;
    var visibleDur = self.duration / self.zoom;
    return ((timeSec - self.scrollX) / visibleDur) * self.canvas.width;
};

EpisodeEditor.prototype._pixelToTime = function(px) {
    var self = this;
    var visibleDur = self.duration / self.zoom;
    return self.scrollX + (px / self.canvas.width) * visibleDur;
};

EpisodeEditor.prototype._niceInterval = function(rough) {
    /* We snap to nice time intervals: 0.1, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300... */
    var options = [0.1, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600];
    for (var i = 0; i < options.length; i++) {
        if (options[i] >= rough) return options[i];
    }
    return 3600;
};

/* ══════════════════════════════════════════════════════════════
 *  CANVAS EVENTS (selection, panning, scrolling)
 * ══════════════════════════════════════════════════════════════ */

EpisodeEditor.prototype._bindCanvasEvents = function() {
    var self = this;
    var ov = self.overlay;

    /* We allow pointer events on the overlay canvas */
    ov.style.pointerEvents = 'auto';
    ov.style.cursor = 'crosshair';

    ov.addEventListener('mousedown', function(e) {
        if (!self.duration) return;
        var rect = ov.getBoundingClientRect();
        var mx = e.clientX - rect.left;

        if (e.button === 1 || e.shiftKey) {
            /* Middle button or shift = panning */
            self.isPanning = true;
            self.panStartX = mx;
            self.panStartScroll = self.scrollX;
            ov.style.cursor = 'grabbing';
            return;
        }

        /* Left click = start selection */
        self.isDragging = true;
        self.dragStartX = mx;
        var t = self._pixelToTime(mx);
        self.selStart = Math.max(0, Math.min(t, self.duration));
        self.selEnd = self.selStart;
        self._updateSelInfo();
    });

    ov.addEventListener('mousemove', function(e) {
        if (!self.duration) return;
        var rect = ov.getBoundingClientRect();
        var mx = e.clientX - rect.left;

        if (self.isPanning) {
            var dx = mx - self.panStartX;
            var visibleDur = self.duration / self.zoom;
            var dtSec = -(dx / self.canvas.width) * visibleDur;
            self.scrollX = Math.max(0, Math.min(self.panStartScroll + dtSec, self.duration - visibleDur));
            self._drawWaveform();
            self._drawRuler();
            return;
        }

        if (self.isDragging) {
            var t = self._pixelToTime(mx);
            self.selEnd = Math.max(0, Math.min(t, self.duration));
            self._updateSelInfo();
        }
    });

    var endDrag = function() {
        if (self.isPanning) {
            self.isPanning = false;
            ov.style.cursor = 'crosshair';
        }
        if (self.isDragging) {
            self.isDragging = false;
            /* If the selection is very small (click rather than drag), we seek instead */
            if (Math.abs(self.selEnd - self.selStart) < 0.05) {
                self.playPos = self.selStart;
                if (self.audioEl) self.audioEl.currentTime = self.playPos;
                self.selStart = -1;
                self.selEnd = -1;
                self._updateSelInfo();
            } else {
                /* We normalize so selStart < selEnd */
                if (self.selStart > self.selEnd) {
                    var tmp = self.selStart;
                    self.selStart = self.selEnd;
                    self.selEnd = tmp;
                }
                self._updateSelInfo();
            }
        }
    };

    ov.addEventListener('mouseup', endDrag);
    ov.addEventListener('mouseleave', function() {
        if (self.isDragging) endDrag();
    });

    /* We handle wheel for zoom */
    ov.addEventListener('wheel', function(e) {
        e.preventDefault();
        if (!self.duration) return;

        var rect = ov.getBoundingClientRect();
        var mx = e.clientX - rect.left;
        var timeAtMouse = self._pixelToTime(mx);

        /* We zoom in/out */
        var delta = e.deltaY > 0 ? -1 : 1;
        var newZoom = Math.max(1, Math.min(50, self.zoom + delta));

        if (newZoom !== self.zoom) {
            self.zoom = newZoom;
            /* We keep the time under the mouse in the same pixel position */
            var visibleDur = self.duration / self.zoom;
            self.scrollX = timeAtMouse - (mx / self.canvas.width) * visibleDur;
            self.scrollX = Math.max(0, Math.min(self.scrollX, self.duration - visibleDur));

            document.getElementById('ee-zoom').value = self.zoom;
            document.getElementById('ee-zoom-label').textContent = self.zoom + 'x';

            self._drawWaveform();
            self._drawRuler();
        }
    }, { passive: false });
};

/* ══════════════════════════════════════════════════════════════
 *  TRANSPORT CONTROLS
 * ══════════════════════════════════════════════════════════════ */

EpisodeEditor.prototype._bindTransport = function() {
    var self = this;

    document.getElementById('ee-btn-play').addEventListener('click', function() {
        self.togglePlay();
    });
    document.getElementById('ee-btn-start').addEventListener('click', function() {
        self.seekTo(0);
    });
    document.getElementById('ee-btn-end').addEventListener('click', function() {
        self.seekTo(self.duration);
    });
    document.getElementById('ee-btn-loop').addEventListener('click', function() {
        self.isLooping = !self.isLooping;
        this.classList.toggle('active', self.isLooping);
    });
};

EpisodeEditor.prototype.togglePlay = function() {
    var self = this;
    if (!self.audioEl) return;

    if (self.isPlaying) {
        self.audioEl.pause();
        self.isPlaying = false;
    } else {
        /* We resume the AudioContext if it was suspended (browser autoplay policy) */
        if (self.audioCtx && self.audioCtx.state === 'suspended') {
            self.audioCtx.resume();
        }

        /* If we have a selection and loop is on, we play the selection */
        if (self.isLooping && self.selStart >= 0 && self.selEnd > self.selStart) {
            self.audioEl.currentTime = self.selStart;
        }

        self.audioEl.play().catch(function(err) {
            if (typeof mc1Toast === 'function') mc1Toast('Cannot play: ' + err.message, 'warn');
        });
        self.isPlaying = true;
    }
    self._updatePlayButton();
};

EpisodeEditor.prototype.seekTo = function(timeSec) {
    var self = this;
    self.playPos = Math.max(0, Math.min(timeSec, self.duration));
    if (self.audioEl) self.audioEl.currentTime = self.playPos;
};

EpisodeEditor.prototype._updatePlayButton = function() {
    var self = this;
    var btn = document.getElementById('ee-btn-play');
    var icon = btn.querySelector('i');
    if (self.isPlaying) {
        icon.className = 'fa-solid fa-pause';
        btn.classList.add('active');
    } else {
        icon.className = 'fa-solid fa-play';
        btn.classList.remove('active');
    }
};

/* ══════════════════════════════════════════════════════════════
 *  KEYBOARD SHORTCUTS
 * ══════════════════════════════════════════════════════════════ */

EpisodeEditor.prototype._bindKeyboard = function() {
    var self = this;

    document.addEventListener('keydown', function(e) {
        /* We skip if user is typing in an input */
        var tag = (e.target.tagName || '').toLowerCase();
        if (tag === 'input' || tag === 'textarea' || tag === 'select') return;

        switch (e.key) {
            case ' ':
                e.preventDefault();
                self.togglePlay();
                break;
            case 'Home':
                e.preventDefault();
                self.seekTo(0);
                break;
            case 'End':
                e.preventDefault();
                self.seekTo(self.duration);
                break;
            case 'l':
            case 'L':
                self.isLooping = !self.isLooping;
                document.getElementById('ee-btn-loop').classList.toggle('active', self.isLooping);
                break;
            case 'z':
            case 'Z':
                if (e.ctrlKey || e.metaKey) {
                    e.preventDefault();
                    if (e.shiftKey) { self.redo(); } else { self.undo(); }
                }
                break;
            case 'y':
            case 'Y':
                if (e.ctrlKey || e.metaKey) { e.preventDefault(); self.redo(); }
                break;
            case 'x':
            case 'X':
                if (e.ctrlKey || e.metaKey) { e.preventDefault(); self.applyTool('cut'); }
                break;
            case 't':
            case 'T':
                if (e.ctrlKey || e.metaKey) { e.preventDefault(); self.applyTool('trim'); }
                break;
            case 'Delete':
                if (self.selStart >= 0 && self.selEnd > self.selStart) {
                    self.applyTool('cut');
                }
                break;
            case 'ArrowLeft':
                e.preventDefault();
                self.seekTo(self.playPos - (e.shiftKey ? 5 : 1));
                break;
            case 'ArrowRight':
                e.preventDefault();
                self.seekTo(self.playPos + (e.shiftKey ? 5 : 1));
                break;
        }
    });
};

/* ══════════════════════════════════════════════════════════════
 *  ZOOM SLIDER
 * ══════════════════════════════════════════════════════════════ */

EpisodeEditor.prototype._bindZoom = function() {
    var self = this;
    var slider = document.getElementById('ee-zoom');
    var label = document.getElementById('ee-zoom-label');

    slider.addEventListener('input', function() {
        var newZoom = parseInt(this.value);
        if (newZoom === self.zoom) return;

        /* We keep the playhead centered when using the slider */
        var centerTime = self.playPos;
        self.zoom = newZoom;
        var visibleDur = self.duration / self.zoom;
        self.scrollX = centerTime - visibleDur / 2;
        self.scrollX = Math.max(0, Math.min(self.scrollX, self.duration - visibleDur));

        label.textContent = newZoom + 'x';
        self._drawWaveform();
        self._drawRuler();
    });
};

/* ══════════════════════════════════════════════════════════════
 *  RENDER LOOP (rAF)
 * ══════════════════════════════════════════════════════════════ */

EpisodeEditor.prototype._startRenderLoop = function() {
    var self = this;

    function tick() {
        if (self.audioEl && self.isPlaying) {
            self.playPos = self.audioEl.currentTime;

            /* We handle loop mode */
            if (self.isLooping && self.selStart >= 0 && self.selEnd > self.selStart) {
                if (self.playPos >= self.selEnd) {
                    self.audioEl.currentTime = self.selStart;
                    self.playPos = self.selStart;
                }
            }

            /* We auto-scroll to keep the playhead visible */
            var visibleDur = self.duration / self.zoom;
            if (self.playPos < self.scrollX || self.playPos > self.scrollX + visibleDur) {
                self.scrollX = Math.max(0, self.playPos - visibleDur * 0.1);
                self._drawWaveform();
                self._drawRuler();
            }
        }

        self._updateTimeDisplay();
        self._drawOverlay();
        self._animFrame = requestAnimationFrame(tick);
    }

    self._animFrame = requestAnimationFrame(tick);
};

/* ══════════════════════════════════════════════════════════════
 *  EDIT TOOLS (non-destructive EDL operations)
 * ══════════════════════════════════════════════════════════════ */

EpisodeEditor.prototype.applyTool = function(tool) {
    var self = this;

    if (tool === 'normalize') {
        document.getElementById('ee-normalize-modal').classList.add('open');
        return;
    }

    if (self.selStart < 0 || self.selEnd <= self.selStart) {
        if (typeof mc1Toast === 'function') mc1Toast('Select a region first', 'warn');
        return;
    }

    var startMs = Math.round(Math.min(self.selStart, self.selEnd) * 1000);
    var endMs   = Math.round(Math.max(self.selStart, self.selEnd) * 1000);
    var op = null;

    switch (tool) {
        case 'cut':
            op = { type: 'cut', start_ms: startMs, end_ms: endMs };
            break;
        case 'trim':
            op = { type: 'trim', start_ms: startMs, end_ms: endMs };
            break;
        case 'silence':
            op = { type: 'silence', start_ms: startMs, end_ms: endMs };
            break;
        default:
            return;
    }

    self._pushOp(op);
    self._drawWaveform();
    if (typeof mc1Toast === 'function') mc1Toast(tool.charAt(0).toUpperCase() + tool.slice(1) + ' applied');
};

EpisodeEditor.prototype.showFadeDialog = function(direction) {
    var self = this;

    if (self.selStart < 0 || self.selEnd <= self.selStart) {
        if (typeof mc1Toast === 'function') mc1Toast('Select a region first', 'warn');
        return;
    }

    document.getElementById('ee-fade-direction').value = direction;
    document.getElementById('ee-fade-modal-title').textContent = direction === 'in' ? 'Fade In' : 'Fade Out';
    document.getElementById('ee-fade-modal').classList.add('open');
};

EpisodeEditor.prototype.closeFadeDialog = function() {
    document.getElementById('ee-fade-modal').classList.remove('open');
};

EpisodeEditor.prototype.applyFade = function() {
    var self = this;
    var direction = document.getElementById('ee-fade-direction').value;
    var durationMs = parseInt(document.getElementById('ee-fade-duration').value) || 2000;
    var curve = document.getElementById('ee-fade-curve').value;

    var startMs = Math.round(Math.min(self.selStart, self.selEnd) * 1000);
    var endMs = Math.round(Math.max(self.selStart, self.selEnd) * 1000);

    var op;
    if (direction === 'in') {
        op = { type: 'fade_in', start_ms: startMs, duration_ms: Math.min(durationMs, endMs - startMs), curve: curve };
    } else {
        var fadeStart = Math.max(startMs, endMs - durationMs);
        op = { type: 'fade_out', start_ms: fadeStart, duration_ms: endMs - fadeStart, curve: curve };
    }

    self._pushOp(op);
    self.closeFadeDialog();
    self._drawWaveform();
    if (typeof mc1Toast === 'function') mc1Toast('Fade ' + direction + ' applied');
};

EpisodeEditor.prototype.applyNormalize = function() {
    var self = this;
    var targetDb = parseFloat(document.getElementById('ee-norm-target').value) || -1;

    var op = { type: 'normalize', target_db: targetDb };
    self._pushOp(op);
    document.getElementById('ee-normalize-modal').classList.remove('open');
    if (typeof mc1Toast === 'function') mc1Toast('Normalize to ' + targetDb + ' dBFS applied');
};

/* ══════════════════════════════════════════════════════════════
 *  UNDO / REDO
 * ══════════════════════════════════════════════════════════════ */

EpisodeEditor.prototype._pushOp = function(op) {
    var self = this;
    self.edl.operations.push(op);
    self.undoStack.push(op);
    if (self.undoStack.length > self.MAX_UNDO) {
        self.undoStack.shift();
    }
    self.redoStack = [];
    self._updateUndoCounts();
};

EpisodeEditor.prototype.undo = function() {
    var self = this;
    if (self.undoStack.length === 0) return;

    var op = self.undoStack.pop();
    self.redoStack.push(op);

    /* We remove the operation from the EDL */
    var idx = self.edl.operations.lastIndexOf(op);
    if (idx >= 0) self.edl.operations.splice(idx, 1);

    self._updateUndoCounts();
    self._drawWaveform();
    if (typeof mc1Toast === 'function') mc1Toast('Undo: ' + op.type);
};

EpisodeEditor.prototype.redo = function() {
    var self = this;
    if (self.redoStack.length === 0) return;

    var op = self.redoStack.pop();
    self.undoStack.push(op);
    self.edl.operations.push(op);

    self._updateUndoCounts();
    self._drawWaveform();
    if (typeof mc1Toast === 'function') mc1Toast('Redo: ' + op.type);
};

EpisodeEditor.prototype._updateUndoCounts = function() {
    var self = this;
    var undoEl = document.getElementById('ee-undo-count');
    var redoEl = document.getElementById('ee-redo-count');

    if (self.undoStack.length > 0) {
        undoEl.textContent = self.undoStack.length;
        undoEl.style.display = '';
    } else {
        undoEl.style.display = 'none';
    }

    if (self.redoStack.length > 0) {
        redoEl.textContent = self.redoStack.length;
        redoEl.style.display = '';
    } else {
        redoEl.style.display = 'none';
    }
};

/* ══════════════════════════════════════════════════════════════
 *  SELECTION INFO
 * ══════════════════════════════════════════════════════════════ */

EpisodeEditor.prototype._updateSelInfo = function() {
    var self = this;
    var el = document.getElementById('ee-sel-info');
    var rangeEl = document.getElementById('ee-sel-range');
    var durEl = document.getElementById('ee-sel-dur');

    if (self.selStart >= 0 && self.selEnd >= 0 && Math.abs(self.selEnd - self.selStart) > 0.01) {
        var s = Math.min(self.selStart, self.selEnd);
        var e = Math.max(self.selStart, self.selEnd);
        rangeEl.textContent = self._fmtTimeFull(s) + ' - ' + self._fmtTimeFull(e);
        durEl.textContent = '(' + (e - s).toFixed(2) + 's)';
        el.classList.add('visible');
    } else {
        el.classList.remove('visible');
    }
};

EpisodeEditor.prototype.clearSelection = function() {
    var self = this;
    self.selStart = -1;
    self.selEnd = -1;
    self._updateSelInfo();
};

/* ══════════════════════════════════════════════════════════════
 *  CHAPTER MANAGEMENT
 * ══════════════════════════════════════════════════════════════ */

EpisodeEditor.prototype.addChapterAtCurrent = function() {
    var self = this;
    var tsMs = Math.round(self.playPos * 1000);

    var title = prompt('Chapter title:', 'Chapter ' + (self.chapters.length + 1));
    if (title === null) return;

    var ch = {
        id: 0,
        episode_id: self.ep.id,
        timestamp_ms: tsMs,
        title: title,
        marker_type: 'chapter',
        url: '',
        image_url: ''
    };

    /* We save to the database */
    mc1Api('POST', '/app/api/podcast.php', {
        action: 'add_marker',
        episode_id: self.ep.id,
        timestamp_ms: tsMs,
        title: title,
        marker_type: 'chapter',
        url: '',
        image_url: ''
    }).then(function(d) {
        if (d.ok) {
            ch.id = d.id || 0;
            self.chapters.push(ch);
            self.chapters.sort(function(a, b) { return a.timestamp_ms - b.timestamp_ms; });
            self._syncEdlChapters();
            self._renderChapters();
            if (typeof mc1Toast === 'function') mc1Toast('Chapter added');
        } else {
            if (typeof mc1Toast === 'function') mc1Toast(d.error || 'Failed to add chapter', 'err');
        }
    });
};

EpisodeEditor.prototype._renderChapters = function() {
    var self = this;
    var el = document.getElementById('ee-ch-list');

    if (!self.chapters.length) {
        el.innerHTML = '<div class="ee-ch-empty">No chapters. Click "Add at Playhead" to create one.</div>';
        return;
    }

    var html = '';
    self.chapters.forEach(function(ch, idx) {
        html += '<div class="ee-ch-item" data-idx="' + idx + '">'
              + '<span class="ee-ch-ts" onclick="window.eeEditor.seekToChapter(' + idx + ')">'
              + self._fmtTimeFull(ch.timestamp_ms / 1000) + '</span>'
              + '<span class="ee-ch-title-text" id="ee-ch-title-' + idx + '">' + self._esc(ch.title) + '</span>'
              + '<div class="ee-ch-acts">'
              + '<button class="btn btn-icon btn-xs" onclick="window.eeEditor.editChapter(' + idx + ')" title="Edit">'
              + '<i class="fa-solid fa-pen"></i></button>'
              + '<button class="btn btn-icon btn-xs" onclick="window.eeEditor.deleteChapter(' + idx + ')" title="Delete">'
              + '<i class="fa-solid fa-trash"></i></button>'
              + '</div></div>';
    });
    el.innerHTML = html;
};

EpisodeEditor.prototype.seekToChapter = function(idx) {
    var self = this;
    if (idx >= 0 && idx < self.chapters.length) {
        self.seekTo(self.chapters[idx].timestamp_ms / 1000);
    }
};

EpisodeEditor.prototype.editChapter = function(idx) {
    var self = this;
    var ch = self.chapters[idx];
    if (!ch) return;

    var newTitle = prompt('Chapter title:', ch.title);
    if (newTitle === null) return;

    ch.title = newTitle;

    /* We save to the database if the chapter has a DB id */
    if (ch.id > 0) {
        mc1Api('POST', '/app/api/podcast.php', {
            action: 'update_marker',
            id: ch.id,
            title: newTitle,
            marker_type: ch.marker_type || 'chapter',
            url: ch.url || '',
            image_url: ch.image_url || ''
        }).then(function(d) {
            if (!d.ok && typeof mc1Toast === 'function') mc1Toast(d.error || 'Failed to update', 'err');
        });
    }

    self._syncEdlChapters();
    self._renderChapters();
};

EpisodeEditor.prototype.deleteChapter = function(idx) {
    var self = this;
    var ch = self.chapters[idx];
    if (!ch) return;

    if (!confirm('Delete chapter "' + ch.title + '"?')) return;

    if (ch.id > 0) {
        mc1Api('POST', '/app/api/podcast.php', {
            action: 'delete_marker',
            id: ch.id
        }).then(function(d) {
            if (!d.ok && typeof mc1Toast === 'function') mc1Toast(d.error || 'Failed to delete', 'err');
        });
    }

    self.chapters.splice(idx, 1);
    self._syncEdlChapters();
    self._renderChapters();
    if (typeof mc1Toast === 'function') mc1Toast('Chapter deleted');
};

EpisodeEditor.prototype._syncEdlChapters = function() {
    var self = this;
    self.edl.chapters = self.chapters.map(function(c) {
        return { timestamp_ms: c.timestamp_ms, title: c.title, url: c.url || '', image_url: c.image_url || '' };
    });
};

/* ══════════════════════════════════════════════════════════════
 *  METADATA SAVE
 * ══════════════════════════════════════════════════════════════ */

EpisodeEditor.prototype.saveMetadata = function() {
    var self = this;

    var payload = {
        action: 'update_episode',
        id: self.ep.id,
        title: document.getElementById('ee-meta-title').value,
        description: document.getElementById('ee-meta-desc').value,
        season: document.getElementById('ee-meta-season').value || null,
        episode_number: document.getElementById('ee-meta-epnum').value || null,
        tags: document.getElementById('ee-meta-tags').value,
        format: self.ep.format,
        bitrate_kbps: self.ep.bitrate_kbps,
    };

    mc1Api('POST', '/app/api/podcast.php', payload).then(function(d) {
        if (d.ok) {
            self.ep.title = payload.title;
            self.ep.description = payload.description;
            self.ep.season = payload.season;
            self.ep.episode_number = payload.episode_number;
            self.ep.tags = payload.tags;

            document.getElementById('ee-title').textContent = payload.title || 'Untitled Episode';
            if (typeof mc1Toast === 'function') mc1Toast('Metadata saved');
        } else {
            if (typeof mc1Toast === 'function') mc1Toast(d.error || 'Save failed', 'err');
        }
    });
};

/* ══════════════════════════════════════════════════════════════
 *  EXPORT
 * ══════════════════════════════════════════════════════════════ */

EpisodeEditor.prototype.selectExportFmt = function(el) {
    var self = this;
    document.querySelectorAll('.ee-export-fmt').forEach(function(f) { f.classList.remove('selected'); });
    el.classList.add('selected');
    self.exportFmt = el.dataset.fmt;
    self.exportOpts = el.dataset.opts || '';
};

EpisodeEditor.prototype.saveEdl = function() {
    var self = this;
    /* We save the EDL as a downloadable JSON file */
    var json = JSON.stringify(self.edl, null, 2);
    var blob = new Blob([json], { type: 'application/json' });
    var url = URL.createObjectURL(blob);
    var a = document.createElement('a');
    a.href = url;
    a.download = 'episode-' + self.ep.id + '-edl.json';
    a.click();
    URL.revokeObjectURL(url);
    if (typeof mc1Toast === 'function') mc1Toast('EDL downloaded');
};

EpisodeEditor.prototype.exportEpisode = function() {
    var self = this;

    var progressEl = document.getElementById('ee-export-progress');
    var statusEl = document.getElementById('ee-export-status');
    var barEl = document.getElementById('ee-export-bar');

    progressEl.style.display = 'block';
    statusEl.textContent = 'Exporting as ' + self.exportFmt.toUpperCase() + '...';
    barEl.style.width = '10%';

    mc1Api('POST', '/app/api/podcast.php', {
        action: 'export_episode',
        episode_id: self.ep.id,
        format: self.exportFmt,
        bitrate: self.exportOpts,
        edl: self.edl,
    }).then(function(d) {
        barEl.style.width = '100%';
        if (d.ok) {
            statusEl.textContent = 'Export complete: ' + (d.output_file || 'done');
            if (typeof mc1Toast === 'function') mc1Toast('Episode exported successfully');
        } else {
            statusEl.textContent = 'Export failed: ' + (d.error || 'unknown error');
            if (typeof mc1Toast === 'function') mc1Toast(d.error || 'Export failed', 'err');
        }
        setTimeout(function() { progressEl.style.display = 'none'; barEl.style.width = '0%'; }, 5000);
    }).catch(function(err) {
        barEl.style.width = '0%';
        statusEl.textContent = 'Export error: ' + err.message;
        if (typeof mc1Toast === 'function') mc1Toast('Export failed: ' + err.message, 'err');
        setTimeout(function() { progressEl.style.display = 'none'; }, 5000);
    });
};

/* ══════════════════════════════════════════════════════════════
 *  DISPLAY HELPERS
 * ══════════════════════════════════════════════════════════════ */

EpisodeEditor.prototype._updateTimeDisplay = function() {
    var self = this;
    document.getElementById('ee-pos-time').textContent = self._fmtTimeFull(self.playPos);
};

EpisodeEditor.prototype._updateDurationDisplay = function() {
    var self = this;
    document.getElementById('ee-dur-time').textContent = self._fmtTimeFull(self.duration);
};

EpisodeEditor.prototype._fmtTimeFull = function(sec) {
    sec = sec || 0;
    var h = Math.floor(sec / 3600);
    var m = Math.floor((sec % 3600) / 60);
    var s = Math.floor(sec % 60);
    var ms = Math.floor((sec % 1) * 10);
    return String(h).padStart(2, '0') + ':' + String(m).padStart(2, '0') + ':'
         + String(s).padStart(2, '0') + '.' + ms;
};

EpisodeEditor.prototype._fmtTimeShort = function(sec) {
    sec = sec || 0;
    var h = Math.floor(sec / 3600);
    var m = Math.floor((sec % 3600) / 60);
    var s = Math.floor(sec % 60);
    if (h > 0) return h + ':' + String(m).padStart(2, '0') + ':' + String(s).padStart(2, '0');
    return m + ':' + String(s).padStart(2, '0');
};

EpisodeEditor.prototype._esc = function(s) {
    if (!s) return '';
    return String(s).replace(/&/g, '&amp;').replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
};
