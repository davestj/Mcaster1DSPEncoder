/**
 * pedal-svgs.js -- SVG generator for broadcast rack-mount pedal faceplates
 * @version 2.0.1
 *
 * File:    src/linux/web_ui/js/pedal-svgs.js
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Purpose: We generate unique SVG markup for each DSP effect type styled as
 *          1U or 2U broadcast rack-mount units with dark metallic faceplates,
 *          teal LED indicators, and white silk-screen labels.
 *
 * Exports:
 *   generatePedalSVG(type, versionInfo) -> SVG markup string
 *   PEDAL_DIMENSIONS[type] -> {w, h}
 */

/* eslint-disable max-len */

(function() {
'use strict';

/* ── Shared dimensions ─────────────────────────────────────────────────── */
var DIMS = {
    eq:               { w: 280, h: 180 },   // 2U
    compressor:       { w: 280, h: 180 },   // 2U
    limiter:          { w: 280, h: 100 },   // 1U
    noise_gate:       { w: 280, h: 100 },   // 1U
    ducker:           { w: 280, h: 100 },   // 1U
    dead_air:         { w: 280, h: 100 },   // 1U
    crossfader:       { w: 280, h: 100 },   // 1U
    track_crossfader: { w: 280, h: 100 },   // 1U
    reverb:           { w: 280, h: 100 },   // 1U
    delay:            { w: 280, h: 100 },   // 1U
    loudness:         { w: 280, h: 180 },   // 2U
    __input:          { w: 120, h: 80 },    // Fixed pseudo-pedal
    __output:         { w: 120, h: 80 },    // Fixed pseudo-pedal
    __headend:        { w: 140, h: 80 }     // Fixed pseudo-pedal (optional)
};

/* ── Shared SVG defs (metallic gradient, rack screw filter, etc.) ─────── */
function sharedDefs(uid) {
    return '<defs>' +
        '<linearGradient id="fp-' + uid + '" x1="0" y1="0" x2="0" y2="1">' +
            '<stop offset="0%" stop-color="#2a3040"/>' +
            '<stop offset="35%" stop-color="#1e2636"/>' +
            '<stop offset="100%" stop-color="#141a26"/>' +
        '</linearGradient>' +
        '<linearGradient id="msheen-' + uid + '" x1="0" y1="0" x2="1" y2="1">' +
            '<stop offset="0%" stop-color="rgba(255,255,255,.08)"/>' +
            '<stop offset="50%" stop-color="rgba(255,255,255,0)"/>' +
            '<stop offset="100%" stop-color="rgba(255,255,255,.04)"/>' +
        '</linearGradient>' +
        '<radialGradient id="led-g-' + uid + '">' +
            '<stop offset="0%" stop-color="#14b8a6"/>' +
            '<stop offset="80%" stop-color="#0d9488"/>' +
            '<stop offset="100%" stop-color="#0a7a6f"/>' +
        '</radialGradient>' +
        '<radialGradient id="led-r-' + uid + '">' +
            '<stop offset="0%" stop-color="#ef4444"/>' +
            '<stop offset="80%" stop-color="#dc2626"/>' +
            '<stop offset="100%" stop-color="#b91c1c"/>' +
        '</radialGradient>' +
        '<filter id="glow-' + uid + '"><feGaussianBlur stdDeviation="2" result="g"/>' +
            '<feMerge><feMergeNode in="g"/><feMergeNode in="SourceGraphic"/></feMerge></filter>' +
        '<filter id="screw-' + uid + '"><feDropShadow dx="0" dy="1" stdDeviation=".5" flood-color="#000" flood-opacity=".4"/></filter>' +
    '</defs>';
}

/* ── Faceplate base (rounded rect with metallic look) ────────────────── */
function faceplate(uid, w, h) {
    return '<rect x="0" y="0" width="' + w + '" height="' + h +
        '" rx="4" fill="url(#fp-' + uid + ')" stroke="#0f1520" stroke-width="1.5"/>' +
        '<rect x="0" y="0" width="' + w + '" height="' + h +
        '" rx="4" fill="url(#msheen-' + uid + ')"/>';
}

/* ── Rack screws (4 corners) ─────────────────────────────────────────── */
function screws(uid, w, h) {
    var r = 4, m = 10, s = '';
    var pts = [[m, m], [w - m, m], [m, h - m], [w - m, h - m]];
    for (var i = 0; i < pts.length; i++) {
        var x = pts[i][0], y = pts[i][1];
        s += '<circle cx="' + x + '" cy="' + y + '" r="' + r +
            '" fill="#1a2030" stroke="#2a3444" stroke-width=".8" filter="url(#screw-' + uid + ')"/>' +
            '<line x1="' + (x - 2) + '" y1="' + y + '" x2="' + (x + 2) + '" y2="' + y +
            '" stroke="#3a4454" stroke-width=".6"/>';
    }
    return s;
}

/* ── Connector dots (left and right edges) ───────────────────────────── */
function connectors(uid, h) {
    var cy = h / 2;
    return '<circle cx="3" cy="' + cy + '" r="4" fill="#1a2030" stroke="#14b8a6" stroke-width="1.2"/>' +
        '<circle cx="3" cy="' + cy + '" r="1.5" fill="#14b8a6" filter="url(#glow-' + uid + ')"/>' +
        '<circle cx="277" cy="' + cy + '" r="4" fill="#1a2030" stroke="#14b8a6" stroke-width="1.2"/>' +
        '<circle cx="277" cy="' + cy + '" r="1.5" fill="#14b8a6" filter="url(#glow-' + uid + ')"/>';
}

/* ── Brand mark and version text ─────────────────────────────────────── */
function brandMark(uid, w, h, ver) {
    return '<text x="' + (w - 14) + '" y="' + (h - 6) +
        '" text-anchor="end" fill="#3a4a5a" font-family="\'SF Mono\',monospace" font-size="7" font-weight="700">MC1</text>' +
        '<text x="14" y="' + (h - 6) +
        '" fill="#2a3a4a" font-family="\'SF Mono\',monospace" font-size="6">v' + (ver || '1.0.0') + '</text>';
}

/* ── Silk-screen label ───────────────────────────────────────────────── */
function silk(x, y, text, size, anchor) {
    return '<text x="' + x + '" y="' + y +
        '" text-anchor="' + (anchor || 'middle') + '" fill="#8a9ab0" font-family="\'SF Mono\',sans-serif" font-size="' +
        (size || 8) + '" font-weight="600" letter-spacing=".5">' + text + '</text>';
}

/* ── LED indicator ───────────────────────────────────────────────────── */
function led(uid, x, y, color) {
    var grad = color === 'red' ? 'led-r-' : 'led-g-';
    return '<circle cx="' + x + '" cy="' + y + '" r="3.5" fill="#1a2030" stroke="#2a3444" stroke-width=".6"/>' +
        '<circle cx="' + x + '" cy="' + y + '" r="2.5" fill="url(#' + grad + uid + ')" filter="url(#glow-' + uid + ')"/>';
}

/* ── Knob (rotary) ───────────────────────────────────────────────────── */
function knob(x, y, label, size) {
    var r = size || 12;
    return '<circle cx="' + x + '" cy="' + y + '" r="' + (r + 2) +
        '" fill="#1a2030" stroke="#2a3444" stroke-width=".8"/>' +
        '<circle cx="' + x + '" cy="' + y + '" r="' + r +
        '" fill="#252e3e" stroke="#3a4a5a" stroke-width="1"/>' +
        '<line x1="' + x + '" y1="' + (y - r + 3) + '" x2="' + x + '" y2="' + (y - 2) +
        '" stroke="#14b8a6" stroke-width="1.5" stroke-linecap="round"/>' +
        silk(x, y + r + 10, label, 7);
}

/* ── Horizontal slider slot ──────────────────────────────────────────── */
function hSlider(x, y, w, label, leftLabel, rightLabel) {
    var s = '<rect x="' + x + '" y="' + (y - 3) + '" width="' + w + '" height="6" rx="3" fill="#1a2030" stroke="#2a3444" stroke-width=".6"/>';
    s += '<rect x="' + (x + w / 2 - 8) + '" y="' + (y - 6) + '" width="16" height="12" rx="2" fill="#3a4a5a" stroke="#4a5a6a" stroke-width=".5"/>';
    if (label) s += silk(x + w / 2, y + 16, label, 7);
    if (leftLabel) s += silk(x - 2, y + 3, leftLabel, 6, 'end');
    if (rightLabel) s += silk(x + w + 2, y + 3, rightLabel, 6, 'start');
    return s;
}

/* ── Vertical slider slot ────────────────────────────────────────────── */
function vSlider(x, y, h) {
    return '<rect x="' + (x - 2) + '" y="' + y + '" width="4" height="' + h + '" rx="2" fill="#1a2030" stroke="#2a3444" stroke-width=".4"/>' +
        '<rect x="' + (x - 4) + '" y="' + (y + h / 2 - 4) + '" width="8" height="8" rx="1.5" fill="#3a4a5a" stroke="#4a5a6a" stroke-width=".4"/>';
}

/* ── Level bar ───────────────────────────────────────────────────────── */
function levelBar(x, y, w, h, fillPct, color) {
    var fill = color || '#14b8a6';
    var fw = w * (fillPct || 0.6);
    return '<rect x="' + x + '" y="' + y + '" width="' + w + '" height="' + h + '" rx="2" fill="#1a2030" stroke="#2a3444" stroke-width=".5"/>' +
        '<rect x="' + (x + 1) + '" y="' + (y + 1) + '" width="' + fw + '" height="' + (h - 2) + '" rx="1" fill="' + fill + '" opacity=".8"/>';
}

/* ── VU Meter arc (for compressor) ───────────────────────────────────── */
function vuMeter(uid, cx, cy, r) {
    var s = '<path d="M ' + (cx - r) + ' ' + cy + ' A ' + r + ' ' + r + ' 0 0 1 ' + (cx + r) + ' ' + cy +
        '" fill="none" stroke="#2a3444" stroke-width="6" stroke-linecap="round"/>';
    // Scale markings
    for (var i = 0; i <= 10; i++) {
        var angle = Math.PI + (Math.PI * i / 10);
        var x1 = cx + (r - 4) * Math.cos(angle);
        var y1 = cy + (r - 4) * Math.sin(angle);
        var x2 = cx + (r + 2) * Math.cos(angle);
        var y2 = cy + (r + 2) * Math.sin(angle);
        s += '<line x1="' + x1.toFixed(1) + '" y1="' + y1.toFixed(1) +
            '" x2="' + x2.toFixed(1) + '" y2="' + y2.toFixed(1) +
            '" stroke="#4a5a6a" stroke-width=".6"/>';
    }
    // Needle
    var needleAngle = Math.PI + Math.PI * 0.35;
    var nx = cx + (r - 8) * Math.cos(needleAngle);
    var ny = cy + (r - 8) * Math.sin(needleAngle);
    s += '<line x1="' + cx + '" y1="' + cy + '" x2="' + nx.toFixed(1) + '" y2="' + ny.toFixed(1) +
        '" stroke="#14b8a6" stroke-width="1.5" stroke-linecap="round"/>';
    s += '<circle cx="' + cx + '" cy="' + cy + '" r="3" fill="#14b8a6"/>';
    return s;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Per-type SVG generators
 * ═══════════════════════════════════════════════════════════════════════════ */

var generators = {};

/* ── EQ: 10 vertical slider slots, frequency labels, band LEDs ────────── */
generators.eq = function(uid, vi) {
    var w = 280, h = 180;
    var freqs = ['31', '63', '125', '250', '500', '1K', '2K', '4K', '8K', '16K'];
    var s = sharedDefs(uid) + faceplate(uid, w, h) + screws(uid, w, h) + connectors(uid, h);

    // Title
    s += silk(140, 22, 'PARAMETRIC EQ', 9);

    // 10 band sliders
    var startX = 32, spacing = 22;
    for (var i = 0; i < 10; i++) {
        var x = startX + i * spacing;
        s += vSlider(x, 32, 90);
        s += led(uid, x, 130, 'green');
        s += silk(x, 148, freqs[i], 5.5);
    }

    s += brandMark(uid, w, h, vi ? vi.version : '1.1.0');
    return s;
};

/* ── Compressor: VU meter arc, 4 knobs, gain reduction meter ─────────── */
generators.compressor = function(uid, vi) {
    var w = 280, h = 180;
    var s = sharedDefs(uid) + faceplate(uid, w, h) + screws(uid, w, h) + connectors(uid, h);

    s += silk(140, 20, 'COMPRESSOR', 9);

    // VU meter
    s += vuMeter(uid, 140, 85, 45);
    s += silk(140, 58, 'GR', 6);

    // 4 knobs: Threshold, Ratio, Attack, Release
    s += knob(42, 150, 'THRESH', 10);
    s += knob(102, 150, 'RATIO', 10);
    s += knob(178, 150, 'ATK', 10);
    s += knob(238, 150, 'REL', 10);

    // Gain reduction bar
    s += levelBar(42, 110, 196, 6, 0.35, '#ef4444');
    s += silk(140, 125, 'GAIN REDUCTION', 5.5);

    s += brandMark(uid, w, h, vi ? vi.version : '1.1.0');
    return s;
};

/* ── Limiter: ceiling knob, red clip LED, output level bar ───────────── */
generators.limiter = function(uid, vi) {
    var w = 280, h = 100;
    var s = sharedDefs(uid) + faceplate(uid, w, h) + screws(uid, w, h) + connectors(uid, h);

    s += silk(140, 20, 'PEAK LIMITER', 9);
    s += knob(60, 55, 'CEILING', 14);
    s += led(uid, 120, 50, 'red');
    s += silk(120, 68, 'CLIP', 5.5);
    s += levelBar(150, 42, 100, 10, 0.7, '#14b8a6');
    s += silk(200, 65, 'OUTPUT', 6);

    s += brandMark(uid, w, h, vi ? vi.version : '1.1.0');
    return s;
};

/* ── Noise Gate: threshold knob, open/close LEDs, activity bar ────────── */
generators.noise_gate = function(uid, vi) {
    var w = 280, h = 100;
    var s = sharedDefs(uid) + faceplate(uid, w, h) + screws(uid, w, h) + connectors(uid, h);

    s += silk(140, 20, 'NOISE GATE', 9);
    s += knob(55, 55, 'THRESH', 12);

    // Open/close LEDs
    s += led(uid, 120, 42, 'green');
    s += silk(120, 58, 'OPEN', 5);
    s += led(uid, 145, 42, 'red');
    s += silk(145, 58, 'CLOSE', 5);

    s += levelBar(170, 42, 80, 8, 0.5, '#22c55e');
    s += silk(210, 62, 'ACTIVITY', 5.5);

    s += brandMark(uid, w, h, vi ? vi.version : '1.1.0');
    return s;
};

/* ── Sidechain Ducker: mic icon, depth knob, PTT indicator, meter ────── */
generators.ducker = function(uid, vi) {
    var w = 280, h = 100;
    var s = sharedDefs(uid) + faceplate(uid, w, h) + screws(uid, w, h) + connectors(uid, h);

    s += silk(140, 20, 'SIDECHAIN DUCKER', 9);

    // Mic icon (simplified)
    s += '<rect x="35" y="35" width="12" height="20" rx="6" fill="none" stroke="#8a9ab0" stroke-width="1.2"/>';
    s += '<path d="M30 52 C30 60 47 60 47 52" fill="none" stroke="#8a9ab0" stroke-width="1"/>';
    s += '<line x1="38.5" y1="60" x2="38.5" y2="67" stroke="#8a9ab0" stroke-width="1"/>';
    s += silk(40, 80, 'MIC', 5.5);

    s += knob(100, 55, 'DEPTH', 12);

    // PTT indicator
    s += led(uid, 160, 50, 'red');
    s += silk(160, 68, 'PTT', 6);

    // Ducking meter
    s += levelBar(185, 42, 70, 8, 0.4, '#f97316');
    s += silk(220, 62, 'DUCK', 5.5);

    s += brandMark(uid, w, h, vi ? vi.version : '1.1.0');
    return s;
};

/* ── Dead Air Detector: timer display, threshold knob, alert LED ─────── */
generators.dead_air = function(uid, vi) {
    var w = 280, h = 100;
    var s = sharedDefs(uid) + faceplate(uid, w, h) + screws(uid, w, h) + connectors(uid, h);

    s += silk(140, 20, 'DEAD AIR DETECTOR', 9);

    // Timer display (digital look)
    s += '<rect x="30" y="35" width="70" height="30" rx="3" fill="#0a0f18" stroke="#2a3444" stroke-width=".8"/>';
    s += '<text x="65" y="56" text-anchor="middle" fill="#14b8a6" font-family="\'SF Mono\',monospace" font-size="14" font-weight="700">00:00</text>';
    s += silk(65, 78, 'TIMER', 5.5);

    s += knob(145, 55, 'THRESH', 12);

    // Alert LED
    s += led(uid, 210, 45, 'red');
    s += silk(210, 63, 'ALERT', 5.5);

    // Level bar
    s += levelBar(230, 38, 8, 30, 0.2, '#ef4444');

    s += brandMark(uid, w, h, vi ? vi.version : '1.1.0');
    return s;
};

/* ── DJ Crossfader: horizontal slider, A/B labels, curve name ────────── */
generators.crossfader = function(uid, vi) {
    var w = 280, h = 100;
    var s = sharedDefs(uid) + faceplate(uid, w, h) + screws(uid, w, h) + connectors(uid, h);

    s += silk(140, 20, 'DJ CROSSFADER', 9);
    s += hSlider(50, 52, 180, '', 'A', 'B');

    // Curve label
    s += silk(140, 80, 'EQUAL POWER', 6);

    // Deck indicator LEDs
    s += led(uid, 30, 52, 'green');
    s += led(uid, 250, 52, 'green');

    s += brandMark(uid, w, h, vi ? vi.version : '1.1.0');
    return s;
};

/* ── Track Crossfader: horizontal slider, IN/OUT labels ──────────────── */
generators.track_crossfader = function(uid, vi) {
    var w = 280, h = 100;
    var s = sharedDefs(uid) + faceplate(uid, w, h) + screws(uid, w, h) + connectors(uid, h);

    s += silk(140, 20, 'TRACK CROSSFADER', 9);
    s += hSlider(50, 52, 180, '', 'IN', 'OUT');

    // Duration label
    s += silk(140, 80, '3.0s', 7);

    s += brandMark(uid, w, h, vi ? vi.version : '1.1.0');
    return s;
};

/* ── Reverb: room size, decay, mix knobs, damping bar, LED ────────────── */
generators.reverb = function(uid, vi) {
    var w = 280, h = 100;
    var s = sharedDefs(uid) + faceplate(uid, w, h) + screws(uid, w, h) + connectors(uid, h);

    s += silk(140, 20, 'REVERB', 9);

    // 3 knobs: Room Size, Decay, Mix
    s += knob(50, 55, 'ROOM', 10);
    s += knob(110, 55, 'DECAY', 10);
    s += knob(170, 55, 'MIX', 10);

    // Damping indicator bar
    s += levelBar(210, 42, 40, 6, 0.5, '#14b8a6');
    s += silk(230, 58, 'DAMP', 5.5);

    // Active LED
    s += led(uid, 245, 75, 'green');
    s += silk(245, 88, 'ACTIVE', 5);

    s += brandMark(uid, w, h, vi ? vi.version : '1.0.0');
    return s;
};

/* ── Delay: time, feedback, mix knobs, filter bar, LED ────────────────── */
generators.delay = function(uid, vi) {
    var w = 280, h = 100;
    var s = sharedDefs(uid) + faceplate(uid, w, h) + screws(uid, w, h) + connectors(uid, h);

    s += silk(140, 20, 'DELAY', 9);

    // 3 knobs: Time, Feedback, Mix
    s += knob(50, 55, 'TIME', 10);
    s += knob(110, 55, 'FDBK', 10);
    s += knob(170, 55, 'MIX', 10);

    // Filter indicator bar
    s += levelBar(210, 42, 40, 6, 0.4, '#3b82f6');
    s += silk(230, 58, 'FILTER', 5.5);

    // Active LED
    s += led(uid, 245, 75, 'green');
    s += silk(245, 88, 'ACTIVE', 5);

    s += brandMark(uid, w, h, vi ? vi.version : '1.0.0');
    return s;
};

/* ── Loudness Compliance: LUFS display, compliance LED, gain meter ────── */
generators.loudness = function(uid, vi) {
    var w = 280, h = 180;
    var s = sharedDefs(uid) + faceplate(uid, w, h) + screws(uid, w, h) + connectors(uid, h);

    s += silk(140, 20, 'LOUDNESS', 9);

    // Large LUFS display (digital readout style)
    s += '<rect x="30" y="30" width="100" height="45" rx="4" fill="#0a0f18" stroke="#2a3444" stroke-width="1"/>';
    s += '<text x="80" y="53" text-anchor="middle" fill="#14b8a6" font-family="\'SF Mono\',monospace" font-size="16" font-weight="700">-16.0</text>';
    s += '<text x="80" y="68" text-anchor="middle" fill="#5a8abf" font-family="\'SF Mono\',monospace" font-size="7" font-weight="600">LUFS</text>';

    // Standard label
    s += '<rect x="150" y="30" width="100" height="20" rx="3" fill="#0a0f18" stroke="#2a3444" stroke-width=".8"/>';
    s += '<text x="200" y="44" text-anchor="middle" fill="#8a9ab0" font-family="\'SF Mono\',monospace" font-size="8" font-weight="600">PODCAST</text>';

    // True Peak display
    s += '<rect x="150" y="55" width="100" height="20" rx="3" fill="#0a0f18" stroke="#2a3444" stroke-width=".8"/>';
    s += '<text x="200" y="69" text-anchor="middle" fill="#a78bfa" font-family="\'SF Mono\',monospace" font-size="8">-1.0 dBTP</text>';

    // Compliance indicator LED
    s += led(uid, 42, 95, 'green');
    s += silk(42, 110, 'COMPLIANT', 5.5);

    // Gain correction meter
    s += levelBar(70, 88, 110, 8, 0.5, '#14b8a6');
    s += silk(125, 108, 'GAIN CORRECTION', 5.5);

    // Target LUFS knob
    s += knob(42, 145, 'TARGET', 10);

    // True Peak ceiling knob
    s += knob(102, 145, 'TP CEIL', 10);

    // LRA max knob
    s += knob(162, 145, 'LRA MAX', 10);

    // LRA display
    s += '<rect x="200" y="130" width="52" height="24" rx="3" fill="#0a0f18" stroke="#2a3444" stroke-width=".8"/>';
    s += '<text x="226" y="146" text-anchor="middle" fill="#f59e0b" font-family="\'SF Mono\',monospace" font-size="9" font-weight="600">8.3 LU</text>';
    s += silk(226, 165, 'LRA', 5.5);

    s += brandMark(uid, w, h, vi ? vi.version : '1.0.0');
    return s;
};

/* ── Encoder Input (fixed pseudo-pedal, dark blue faceplate) ──────────── */
generators.__input = function(uid, vi) {
    var w = 120, h = 80;
    var s = '<defs>' +
        '<linearGradient id="fp-in-' + uid + '" x1="0" y1="0" x2="0" y2="1">' +
            '<stop offset="0%" stop-color="#1a2a4a"/>' +
            '<stop offset="50%" stop-color="#152040"/>' +
            '<stop offset="100%" stop-color="#0f1830"/>' +
        '</linearGradient>' +
        '<radialGradient id="led-g-' + uid + '">' +
            '<stop offset="0%" stop-color="#14b8a6"/>' +
            '<stop offset="100%" stop-color="#0a7a6f"/>' +
        '</radialGradient>' +
        '<filter id="glow-' + uid + '"><feGaussianBlur stdDeviation="2" result="g"/>' +
            '<feMerge><feMergeNode in="g"/><feMergeNode in="SourceGraphic"/></feMerge></filter>' +
    '</defs>';
    s += '<rect x="0" y="0" width="' + w + '" height="' + h + '" rx="6" fill="url(#fp-in-' + uid + ')" stroke="#1e3a6a" stroke-width="2"/>';
    s += '<text x="' + (w/2) + '" y="22" text-anchor="middle" fill="#5a8abf" font-family="\'SF Mono\',monospace" font-size="9" font-weight="700" letter-spacing="1">INPUT</text>';
    // Arrow pointing right
    s += '<path d="M 40 40 L 80 40 L 72 34 M 80 40 L 72 46" fill="none" stroke="#14b8a6" stroke-width="2" stroke-linecap="round"/>';
    // Output connector dot on right edge
    s += '<circle cx="' + (w - 3) + '" cy="' + (h / 2) + '" r="4" fill="#1a2030" stroke="#14b8a6" stroke-width="1.2"/>';
    s += '<circle cx="' + (w - 3) + '" cy="' + (h / 2) + '" r="1.5" fill="#14b8a6" filter="url(#glow-' + uid + ')"/>';
    s += '<text x="' + (w/2) + '" y="' + (h - 8) + '" text-anchor="middle" fill="#3a5a8a" font-family="\'SF Mono\',monospace" font-size="6">ENCODER</text>';
    return s;
};

/* ── Encoder Output (fixed pseudo-pedal, dark blue faceplate) ─────────── */
generators.__output = function(uid, vi) {
    var w = 120, h = 80;
    var s = '<defs>' +
        '<linearGradient id="fp-out-' + uid + '" x1="0" y1="0" x2="0" y2="1">' +
            '<stop offset="0%" stop-color="#1a2a4a"/>' +
            '<stop offset="50%" stop-color="#152040"/>' +
            '<stop offset="100%" stop-color="#0f1830"/>' +
        '</linearGradient>' +
        '<radialGradient id="led-r-' + uid + '">' +
            '<stop offset="0%" stop-color="#ef4444"/>' +
            '<stop offset="100%" stop-color="#b91c1c"/>' +
        '</radialGradient>' +
        '<filter id="glow-' + uid + '"><feGaussianBlur stdDeviation="2" result="g"/>' +
            '<feMerge><feMergeNode in="g"/><feMergeNode in="SourceGraphic"/></feMerge></filter>' +
    '</defs>';
    s += '<rect x="0" y="0" width="' + w + '" height="' + h + '" rx="6" fill="url(#fp-out-' + uid + ')" stroke="#1e3a6a" stroke-width="2"/>';
    s += '<text x="' + (w/2) + '" y="22" text-anchor="middle" fill="#5a8abf" font-family="\'SF Mono\',monospace" font-size="9" font-weight="700" letter-spacing="1">OUTPUT</text>';
    // Arrow pointing right (into output)
    s += '<path d="M 40 40 L 80 40 L 72 34 M 80 40 L 72 46" fill="none" stroke="#ef4444" stroke-width="2" stroke-linecap="round"/>';
    // Input connector dot on left edge
    s += '<circle cx="3" cy="' + (h / 2) + '" r="4" fill="#1a2030" stroke="#14b8a6" stroke-width="1.2"/>';
    s += '<circle cx="3" cy="' + (h / 2) + '" r="1.5" fill="#14b8a6" filter="url(#glow-' + uid + ')"/>';
    s += '<text x="' + (w/2) + '" y="' + (h - 8) + '" text-anchor="middle" fill="#3a5a8a" font-family="\'SF Mono\',monospace" font-size="6">STREAM</text>';
    return s;
};

/* ── Head-End Output (optional pre/post tap pseudo-pedal) ─────────────── */
generators.__headend = function(uid, vi) {
    var w = 140, h = 80;
    var s = '<defs>' +
        '<linearGradient id="fp-he-' + uid + '" x1="0" y1="0" x2="0" y2="1">' +
            '<stop offset="0%" stop-color="#2a2030"/>' +
            '<stop offset="50%" stop-color="#1e1828"/>' +
            '<stop offset="100%" stop-color="#151020"/>' +
        '</linearGradient>' +
        '<radialGradient id="led-g-' + uid + '">' +
            '<stop offset="0%" stop-color="#eab308"/>' +
            '<stop offset="100%" stop-color="#a16207"/>' +
        '</radialGradient>' +
        '<filter id="glow-' + uid + '"><feGaussianBlur stdDeviation="2" result="g"/>' +
            '<feMerge><feMergeNode in="g"/><feMergeNode in="SourceGraphic"/></feMerge></filter>' +
    '</defs>';
    s += '<rect x="0" y="0" width="' + w + '" height="' + h + '" rx="6" fill="url(#fp-he-' + uid + ')" stroke="#3a2a5a" stroke-width="2"/>';
    s += '<text x="' + (w/2) + '" y="22" text-anchor="middle" fill="#8a6ab0" font-family="\'SF Mono\',monospace" font-size="8" font-weight="700" letter-spacing="1">HEAD-END</text>';
    // Input connector dot on left edge
    s += '<circle cx="3" cy="' + (h / 2) + '" r="4" fill="#1a2030" stroke="#eab308" stroke-width="1.2"/>';
    s += '<circle cx="3" cy="' + (h / 2) + '" r="1.5" fill="#eab308" filter="url(#glow-' + uid + ')"/>';
    s += '<text x="' + (w/2) + '" y="50" text-anchor="middle" fill="#6a5a8a" font-family="\'SF Mono\',monospace" font-size="7">TAP OUTPUT</text>';
    s += '<text x="' + (w/2) + '" y="' + (h - 8) + '" text-anchor="middle" fill="#3a2a5a" font-family="\'SF Mono\',monospace" font-size="6">PRE/POST</text>';
    return s;
};

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * generatePedalSVG(type, versionInfo)
 * @param {string} type - effect type_id (eq, compressor, limiter, etc.)
 * @param {object} versionInfo - {version, brand_name, is_stub, ...} or null
 * @returns {string} SVG markup
 */
window.generatePedalSVG = function(type, versionInfo) {
    var gen = generators[type];
    if (!gen) {
        // Fallback: generic 1U unit
        var uid = 'gen-' + type;
        var w = 280, h = 100;
        var s = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ' + w + ' ' + h + '">';
        s += sharedDefs(uid) + faceplate(uid, w, h) + screws(uid, w, h) + connectors(uid, h);
        s += silk(140, 50, (type || 'UNKNOWN').toUpperCase(), 10);
        s += brandMark(uid, w, h, versionInfo ? versionInfo.version : '0.0.0');
        s += '</svg>';
        return s;
    }

    var uid = 'pu-' + type + '-' + Math.random().toString(36).substr(2, 6);
    var dim = DIMS[type] || { w: 280, h: 100 };
    var svg = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ' + dim.w + ' ' + dim.h + '">';
    svg += gen(uid, versionInfo);
    svg += '</svg>';
    return svg;
};

window.PEDAL_DIMENSIONS = DIMS;

})();
