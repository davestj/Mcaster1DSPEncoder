/*
 * Mcaster1 VoicTune — Canvas 2D Visualization Engine
 * js/voictune-viz.js
 *
 * Oscilloscope, spectrum analyzer, RMS/Peak meters, LUFS gauge, pitch display.
 * All rendering uses requestAnimationFrame driven from voictune.php.
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

(function() {
'use strict';

/* ══════════════════════════════════════════════════════════════════════
 * VoicTuneViz — main visualization class
 * ══════════════════════════════════════════════════════════════════════ */
function VoicTuneViz() {
    /* Peak hold state for spectrum analyzer (slow decay) */
    this._spectrumPeaks = [];
    this._spectrumPeakDecay = 0.003;  /* dB per frame */

    /* Peak hold state for meters */
    this._meterPeakHold = {};   /* key -> {value, time} */
    this._meterPeakDecayMs = 1500;

    /* Smoothed meter values for visual interpolation */
    this._meterSmooth = {};     /* key -> smoothed dB */
    this._meterSmoothFactor = 0.25;

    /* Spectrum frequency labels for log scale */
    this._freqLabels = [
        {hz: 31,    label: '31'},
        {hz: 63,    label: '63'},
        {hz: 125,   label: '125'},
        {hz: 250,   label: '250'},
        {hz: 500,   label: '500'},
        {hz: 1000,  label: '1k'},
        {hz: 2000,  label: '2k'},
        {hz: 4000,  label: '4k'},
        {hz: 8000,  label: '8k'},
        {hz: 16000, label: '16k'}
    ];
}

/* ── Helper: dB to normalized 0-1 ──────────────────────────────────── */
VoicTuneViz.prototype._dbToNorm = function(db, floor, ceil) {
    floor = floor || -60;
    ceil  = ceil  || 0;
    return Math.max(0, Math.min(1, (db - floor) / (ceil - floor)));
};

/* ── Helper: meter color at position (green->yellow->red) ──────── */
VoicTuneViz.prototype._meterColor = function(norm) {
    if (norm < 0.6)  return '#22c55e'; /* green: -60 to -18 dB */
    if (norm < 0.85) return '#eab308'; /* yellow: -18 to -6 dB */
    return '#ef4444';                  /* red: -6 to 0 dB */
};

/* ══════════════════════════════════════════════════════════════════════
 * drawMeter(canvas, dbValue, peakHoldDb, label)
 *
 * Vertical bar meter: green (-60 to -18), yellow (-18 to -6), red (-6 to 0)
 * Peak hold: white horizontal line with slow decay
 * ══════════════════════════════════════════════════════════════════════ */
VoicTuneViz.prototype.drawMeter = function(canvas, dbValue, peakHoldDb, label) {
    if (!canvas) return;
    var ctx = canvas.getContext('2d');
    var w = canvas.width;
    var h = canvas.height;
    var dpr = window.devicePixelRatio || 1;

    var DB_FLOOR = -60;
    var DB_CEIL  = 0;

    /* Smooth the value */
    var key = label || 'meter';
    var prev = this._meterSmooth[key];
    if (prev === undefined) prev = DB_FLOOR;
    var smoothed = prev + (dbValue - prev) * this._meterSmoothFactor;
    this._meterSmooth[key] = smoothed;

    var norm = this._dbToNorm(smoothed, DB_FLOOR, DB_CEIL);

    /* Peak hold with decay */
    var peakKey = key + '_peak';
    var ph = this._meterPeakHold[peakKey];
    var now = Date.now();
    if (!ph || peakHoldDb > ph.value) {
        ph = {value: peakHoldDb, time: now};
    } else if (now - ph.time > this._meterPeakDecayMs) {
        ph.value -= 0.3; /* slow decay */
        if (ph.value < DB_FLOOR) ph.value = DB_FLOOR;
    }
    this._meterPeakHold[peakKey] = ph;

    ctx.clearRect(0, 0, w, h);

    /* Bar dimensions */
    var barX = Math.round(w * 0.25);
    var barW = Math.round(w * 0.5);
    var barH = h;
    var fillH = Math.round(norm * barH);

    /* Background track */
    ctx.fillStyle = 'rgba(255,255,255,0.04)';
    ctx.fillRect(barX, 0, barW, barH);

    /* Gradient fill */
    if (fillH > 0) {
        var grd = ctx.createLinearGradient(0, barH, 0, barH - fillH);
        grd.addColorStop(0, '#22c55e');
        grd.addColorStop(0.6, '#22c55e');
        grd.addColorStop(0.8, '#eab308');
        grd.addColorStop(0.95, '#ef4444');
        grd.addColorStop(1, '#ef4444');
        ctx.fillStyle = grd;
        ctx.fillRect(barX, barH - fillH, barW, fillH);
    }

    /* Peak hold line */
    var peakNorm = this._dbToNorm(ph.value, DB_FLOOR, DB_CEIL);
    if (peakNorm > 0.01) {
        var peakY = Math.round(barH - peakNorm * barH);
        ctx.strokeStyle = '#ffffff';
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.moveTo(barX - 2, peakY);
        ctx.lineTo(barX + barW + 2, peakY);
        ctx.stroke();
    }

    /* dB scale marks */
    ctx.fillStyle = 'rgba(255,255,255,0.25)';
    ctx.font = (8 * dpr) + 'px sans-serif';
    ctx.textAlign = 'right';
    var marks = [0, -6, -12, -18, -24, -36, -48, -60];
    for (var i = 0; i < marks.length; i++) {
        var mn = this._dbToNorm(marks[i], DB_FLOOR, DB_CEIL);
        var my = Math.round(barH - mn * barH);
        ctx.fillRect(barX - 4, my, 3, 1);
    }
};

/* ══════════════════════════════════════════════════════════════════════
 * drawLufsGauge(canvas, lufsValue, target)
 *
 * Semicircular arc gauge with color zones.
 * Red (too quiet) -> green (target zone) -> red (too loud)
 * ══════════════════════════════════════════════════════════════════════ */
VoicTuneViz.prototype.drawLufsGauge = function(canvas, lufsValue, target) {
    if (!canvas) return;
    var ctx = canvas.getContext('2d');
    var w = canvas.width;
    var h = canvas.height;
    var dpr = window.devicePixelRatio || 1;

    ctx.clearRect(0, 0, w, h);

    var cx = w / 2;
    var cy = h * 0.85;
    var radius = Math.min(cx - 8, cy - 8);
    var arcStart = Math.PI;      /* 180 deg (left) */
    var arcEnd   = 2 * Math.PI;  /* 360 deg (right) */
    var lineW    = Math.max(8, radius * 0.15);

    /* LUFS range: -36 to -6 */
    var LUFS_MIN = -36;
    var LUFS_MAX = -6;
    target = target || -16;

    /* Background arc */
    ctx.strokeStyle = 'rgba(255,255,255,0.06)';
    ctx.lineWidth = lineW;
    ctx.lineCap = 'butt';
    ctx.beginPath();
    ctx.arc(cx, cy, radius, arcStart, arcEnd);
    ctx.stroke();

    /* Color zones arc — paint in 3 segments */
    var targetNorm = (target - LUFS_MIN) / (LUFS_MAX - LUFS_MIN);
    var zoneW = 0.08; /* target zone half-width in normalized units */

    /* Red (too quiet) */
    ctx.strokeStyle = 'rgba(239,68,68,0.5)';
    ctx.lineWidth = lineW;
    ctx.beginPath();
    ctx.arc(cx, cy, radius, arcStart, arcStart + Math.PI * Math.max(0, targetNorm - zoneW));
    ctx.stroke();

    /* Green (target zone) */
    ctx.strokeStyle = 'rgba(34,197,94,0.5)';
    ctx.beginPath();
    ctx.arc(cx, cy, radius,
        arcStart + Math.PI * Math.max(0, targetNorm - zoneW),
        arcStart + Math.PI * Math.min(1, targetNorm + zoneW));
    ctx.stroke();

    /* Red (too loud) */
    ctx.strokeStyle = 'rgba(239,68,68,0.5)';
    ctx.beginPath();
    ctx.arc(cx, cy, radius,
        arcStart + Math.PI * Math.min(1, targetNorm + zoneW),
        arcEnd);
    ctx.stroke();

    /* Value needle */
    var valNorm = Math.max(0, Math.min(1, (lufsValue - LUFS_MIN) / (LUFS_MAX - LUFS_MIN)));
    var needleAngle = arcStart + Math.PI * valNorm;
    var needleLen = radius * 0.85;
    var nx = cx + Math.cos(needleAngle) * needleLen;
    var ny = cy + Math.sin(needleAngle) * needleLen;

    ctx.strokeStyle = '#e2e8f0';
    ctx.lineWidth = 2 * dpr;
    ctx.lineCap = 'round';
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.lineTo(nx, ny);
    ctx.stroke();

    /* Center dot */
    ctx.fillStyle = '#e2e8f0';
    ctx.beginPath();
    ctx.arc(cx, cy, 3 * dpr, 0, Math.PI * 2);
    ctx.fill();

    /* Target line marker */
    var tAngle = arcStart + Math.PI * targetNorm;
    var t1x = cx + Math.cos(tAngle) * (radius - lineW / 2 - 2);
    var t1y = cy + Math.sin(tAngle) * (radius - lineW / 2 - 2);
    var t2x = cx + Math.cos(tAngle) * (radius + lineW / 2 + 2);
    var t2y = cy + Math.sin(tAngle) * (radius + lineW / 2 + 2);
    ctx.strokeStyle = '#14b8a6';
    ctx.lineWidth = 2 * dpr;
    ctx.beginPath();
    ctx.moveTo(t1x, t1y);
    ctx.lineTo(t2x, t2y);
    ctx.stroke();

    /* Labels at min and max */
    ctx.fillStyle = 'rgba(255,255,255,0.35)';
    ctx.font = (9 * dpr) + 'px sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText(LUFS_MIN.toString(), cx - radius + lineW, cy + 14 * dpr);
    ctx.fillText(LUFS_MAX.toString(), cx + radius - lineW, cy + 14 * dpr);

    /* Target label */
    ctx.fillStyle = 'rgba(20,184,166,0.7)';
    ctx.font = (8 * dpr) + 'px sans-serif';
    var tlx = cx + Math.cos(tAngle) * (radius + lineW / 2 + 12 * dpr);
    var tly = cy + Math.sin(tAngle) * (radius + lineW / 2 + 12 * dpr);
    ctx.fillText(target.toString(), tlx, tly);
};

/* ══════════════════════════════════════════════════════════════════════
 * drawOscilloscope(canvas, waveformData)
 *
 * Green line on dark background with zero-crossing trigger alignment.
 * ══════════════════════════════════════════════════════════════════════ */
VoicTuneViz.prototype.drawOscilloscope = function(canvas, waveformData) {
    if (!canvas || !waveformData || waveformData.length === 0) return;
    var ctx = canvas.getContext('2d');
    var w = canvas.width;
    var h = canvas.height;
    var dpr = window.devicePixelRatio || 1;

    ctx.clearRect(0, 0, w, h);

    /* Background */
    ctx.fillStyle = '#0a0f1e';
    ctx.fillRect(0, 0, w, h);

    /* Grid lines */
    ctx.strokeStyle = 'rgba(255,255,255,0.06)';
    ctx.lineWidth = 1;
    var gridDivX = 8;
    var gridDivY = 4;
    for (var gx = 1; gx < gridDivX; gx++) {
        var x = Math.round(w * gx / gridDivX) + 0.5;
        ctx.beginPath();
        ctx.moveTo(x, 0);
        ctx.lineTo(x, h);
        ctx.stroke();
    }
    for (var gy = 1; gy < gridDivY; gy++) {
        var y = Math.round(h * gy / gridDivY) + 0.5;
        ctx.beginPath();
        ctx.moveTo(0, y);
        ctx.lineTo(w, y);
        ctx.stroke();
    }

    /* Center line */
    ctx.strokeStyle = 'rgba(255,255,255,0.12)';
    ctx.beginPath();
    ctx.moveTo(0, h / 2);
    ctx.lineTo(w, h / 2);
    ctx.stroke();

    /* Find trigger point (positive zero-crossing) for stable display */
    var data = waveformData;
    var len = data.length;
    var triggerIdx = 0;
    for (var i = 1; i < len / 2; i++) {
        if (data[i - 1] <= 0 && data[i] > 0) {
            triggerIdx = i;
            break;
        }
    }

    /* Auto-scale amplitude */
    var maxAmp = 0;
    for (var j = triggerIdx; j < len; j++) {
        var a = Math.abs(data[j]);
        if (a > maxAmp) maxAmp = a;
    }
    var scale = maxAmp > 0.001 ? (0.45 / maxAmp) : 0.45;

    /* Draw waveform */
    ctx.strokeStyle = '#22c55e';
    ctx.lineWidth = 2 * dpr;
    ctx.lineJoin = 'round';
    ctx.lineCap = 'round';
    ctx.shadowColor = 'rgba(34,197,94,0.3)';
    ctx.shadowBlur = 4 * dpr;

    var displayLen = Math.min(len - triggerIdx, len);
    var step = displayLen / w;

    ctx.beginPath();
    for (var px = 0; px < w; px++) {
        var si = triggerIdx + Math.floor(px * step);
        if (si >= len) si = len - 1;
        var val = data[si] * scale;
        var py = h / 2 - val * h;
        if (px === 0) ctx.moveTo(px, py);
        else ctx.lineTo(px, py);
    }
    ctx.stroke();
    ctx.shadowBlur = 0;
};

/* ══════════════════════════════════════════════════════════════════════
 * drawSpectrum(canvas, spectrumData, sampleRate)
 *
 * Vertical gradient bars on logarithmic frequency scale (20Hz-20kHz).
 * Peak hold indicators with slow decay.
 * ══════════════════════════════════════════════════════════════════════ */
VoicTuneViz.prototype.drawSpectrum = function(canvas, spectrumData, sampleRate) {
    if (!canvas || !spectrumData || spectrumData.length === 0) return;
    var ctx = canvas.getContext('2d');
    var w = canvas.width;
    var h = canvas.height;
    var dpr = window.devicePixelRatio || 1;

    sampleRate = sampleRate || 48000;
    var bins = spectrumData;
    var binCount = bins.length;
    var nyquist = sampleRate / 2;

    var DB_FLOOR = -96;
    var DB_CEIL  = 0;
    var FREQ_MIN = 20;
    var FREQ_MAX = 20000;
    var logMin = Math.log10(FREQ_MIN);
    var logMax = Math.log10(FREQ_MAX);
    var logRange = logMax - logMin;

    /* Margins */
    var marginL = 32 * dpr;
    var marginB = 18 * dpr;
    var plotW = w - marginL;
    var plotH = h - marginB;

    ctx.clearRect(0, 0, w, h);

    /* Background */
    ctx.fillStyle = '#0a0f1e';
    ctx.fillRect(0, 0, w, h);

    /* dB scale on left */
    ctx.fillStyle = 'rgba(255,255,255,0.3)';
    ctx.font = (8 * dpr) + 'px sans-serif';
    ctx.textAlign = 'right';
    var dbMarks = [0, -12, -24, -36, -48, -60, -72, -84, -96];
    for (var di = 0; di < dbMarks.length; di++) {
        var dbNorm = (dbMarks[di] - DB_FLOOR) / (DB_CEIL - DB_FLOOR);
        var dy = plotH - dbNorm * plotH;
        ctx.fillText(dbMarks[di].toString(), marginL - 4, dy + 3 * dpr);
        ctx.fillStyle = 'rgba(255,255,255,0.04)';
        ctx.fillRect(marginL, dy, plotW, 1);
        ctx.fillStyle = 'rgba(255,255,255,0.3)';
    }

    /* Number of visual bars */
    var numBars = Math.min(128, Math.floor(plotW / (2 * dpr)));
    var barW = Math.max(1, Math.floor(plotW / numBars) - 1);
    var gap = 1;

    /* Ensure peak array is sized */
    while (this._spectrumPeaks.length < numBars) this._spectrumPeaks.push(DB_FLOOR);

    /* Gradient for bars */
    var barGrd = ctx.createLinearGradient(0, plotH, 0, 0);
    barGrd.addColorStop(0, '#0d9488');
    barGrd.addColorStop(0.5, '#14b8a6');
    barGrd.addColorStop(1, '#06b6d4');

    for (var bi = 0; bi < numBars; bi++) {
        /* Map bar index to frequency range on log scale */
        var logFreq0 = logMin + (bi / numBars) * logRange;
        var logFreq1 = logMin + ((bi + 1) / numBars) * logRange;
        var freq0 = Math.pow(10, logFreq0);
        var freq1 = Math.pow(10, logFreq1);

        /* Map frequency to FFT bin index */
        var binIdx0 = Math.max(0, Math.floor(freq0 / nyquist * binCount));
        var binIdx1 = Math.min(binCount - 1, Math.ceil(freq1 / nyquist * binCount));
        if (binIdx1 < binIdx0) binIdx1 = binIdx0;

        /* Find max magnitude in this frequency band */
        var maxMag = 0;
        for (var k = binIdx0; k <= binIdx1; k++) {
            if (bins[k] > maxMag) maxMag = bins[k];
        }

        /* Convert magnitude to dB */
        var db = maxMag > 0 ? 20 * Math.log10(maxMag) : DB_FLOOR;
        if (db < DB_FLOOR) db = DB_FLOOR;

        var norm = (db - DB_FLOOR) / (DB_CEIL - DB_FLOOR);
        var barH = Math.max(0, Math.round(norm * plotH));

        var bx = marginL + Math.round(bi * (plotW / numBars));

        /* Draw bar */
        if (barH > 0) {
            ctx.fillStyle = barGrd;
            ctx.fillRect(bx, plotH - barH, barW, barH);
        }

        /* Peak hold */
        if (db > this._spectrumPeaks[bi]) {
            this._spectrumPeaks[bi] = db;
        } else {
            this._spectrumPeaks[bi] -= this._spectrumPeakDecay;
            if (this._spectrumPeaks[bi] < DB_FLOOR) this._spectrumPeaks[bi] = DB_FLOOR;
        }
        var peakNorm = (this._spectrumPeaks[bi] - DB_FLOOR) / (DB_CEIL - DB_FLOOR);
        if (peakNorm > 0.01) {
            var peakY = Math.round(plotH - peakNorm * plotH);
            ctx.fillStyle = 'rgba(255,255,255,0.7)';
            ctx.fillRect(bx, peakY, barW, 2);
        }
    }

    /* Frequency labels on bottom */
    ctx.fillStyle = 'rgba(255,255,255,0.35)';
    ctx.font = (8 * dpr) + 'px sans-serif';
    ctx.textAlign = 'center';
    for (var fi = 0; fi < this._freqLabels.length; fi++) {
        var fl = this._freqLabels[fi];
        if (fl.hz < FREQ_MIN || fl.hz > FREQ_MAX) continue;
        var fx = marginL + ((Math.log10(fl.hz) - logMin) / logRange) * plotW;
        ctx.fillText(fl.label, fx, h - 4 * dpr);

        /* Vertical guide line */
        ctx.fillStyle = 'rgba(255,255,255,0.04)';
        ctx.fillRect(Math.round(fx), 0, 1, plotH);
        ctx.fillStyle = 'rgba(255,255,255,0.35)';
    }
};

/* Export */
window.VoicTuneViz = VoicTuneViz;

})();
