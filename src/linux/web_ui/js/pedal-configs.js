/**
 * pedal-configs.js -- Config panel generators for pedalboard effect units
 *
 * File:    src/linux/web_ui/js/pedal-configs.js
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Purpose: We provide slide-out config panels for each DSP effect type with
 *          rotary knobs (CSS 3D), vertical sliders, toggles, and dropdowns.
 *          All parameter changes fire live updates via mc1Api.
 *
 * Exports:
 *   openPedalConfig(type, unitId, currentParams, slotId)
 *   closePedalConfig()
 */

(function() {
'use strict';

var _panel = null;
var _backdrop = null;
var _currentUnitId = null;
var _currentSlotId = null;
var _debounceTimers = {};

/* ── Debounced API update ────────────────────────────────────────────── */
function debouncedUpdate(key, params) {
    if (_debounceTimers[key]) clearTimeout(_debounceTimers[key]);
    _debounceTimers[key] = setTimeout(function() {
        if (_currentSlotId !== null && _currentSlotId !== undefined) {
            mc1Api('PUT', '/api/v1/encoders/' + _currentSlotId + '/dsp', params);
        } else {
            mc1Api('PUT', '/api/v1/effects/global', {
                unit_id: _currentUnitId,
                params: params
            });
        }
    }, 80);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * UI Component Generators
 * ═══════════════════════════════════════════════════════════════════════════ */

/* ── Rotary Knob (CSS 3D with mouse drag) ────────────────────────────── */
function createKnob(id, label, value, min, max, step, unit, onChange) {
    var range = max - min;
    var pct = (value - min) / range;
    var angle = -135 + pct * 270;

    var wrap = document.createElement('div');
    wrap.className = 'pc-knob-wrap';
    wrap.innerHTML =
        '<div class="pc-knob-label">' + label + '</div>' +
        '<div class="pc-knob" id="knob-' + id + '" data-min="' + min + '" data-max="' + max +
            '" data-step="' + step + '" data-value="' + value + '">' +
            '<div class="pc-knob-body" style="transform: rotateZ(' + angle + 'deg)">' +
                '<div class="pc-knob-indicator"></div>' +
            '</div>' +
        '</div>' +
        '<div class="pc-knob-value" id="knob-val-' + id + '">' + parseFloat(value).toFixed(1) + (unit ? ' ' + unit : '') + '</div>';

    // Attach drag handler after insertion
    setTimeout(function() {
        var knobEl = document.getElementById('knob-' + id);
        if (!knobEl) return;
        var body = knobEl.querySelector('.pc-knob-body');
        var valEl = document.getElementById('knob-val-' + id);
        var dragging = false;
        var startY = 0, startVal = 0;

        function onMove(e) {
            if (!dragging) return;
            e.preventDefault();
            var clientY = e.touches ? e.touches[0].clientY : e.clientY;
            var dy = startY - clientY;
            var delta = (dy / 150) * range;
            var newVal = Math.min(max, Math.max(min, startVal + delta));
            newVal = Math.round(newVal / step) * step;
            var newPct = (newVal - min) / range;
            var newAngle = -135 + newPct * 270;
            body.style.transform = 'rotateZ(' + newAngle + 'deg)';
            valEl.textContent = parseFloat(newVal).toFixed(1) + (unit ? ' ' + unit : '');
            knobEl.setAttribute('data-value', newVal);
            if (onChange) onChange(newVal);
        }

        function onUp() {
            dragging = false;
            document.removeEventListener('mousemove', onMove);
            document.removeEventListener('mouseup', onUp);
            document.removeEventListener('touchmove', onMove);
            document.removeEventListener('touchend', onUp);
        }

        function onDown(e) {
            dragging = true;
            startY = e.touches ? e.touches[0].clientY : e.clientY;
            startVal = parseFloat(knobEl.getAttribute('data-value'));
            document.addEventListener('mousemove', onMove);
            document.addEventListener('mouseup', onUp);
            document.addEventListener('touchmove', onMove, { passive: false });
            document.addEventListener('touchend', onUp);
            e.preventDefault();
        }

        knobEl.addEventListener('mousedown', onDown);
        knobEl.addEventListener('touchstart', onDown, { passive: false });
    }, 50);

    return wrap;
}

/* ── Vertical Slider ─────────────────────────────────────────────────── */
function createVSlider(id, label, value, min, max, step, unit, onChange) {
    var wrap = document.createElement('div');
    wrap.className = 'pc-vslider-wrap';
    wrap.innerHTML =
        '<div class="pc-vslider-label">' + label + '</div>' +
        '<input type="range" class="pc-vslider" id="vs-' + id + '" ' +
            'min="' + min + '" max="' + max + '" step="' + step + '" value="' + value +
            '" orient="vertical">' +
        '<div class="pc-vslider-value" id="vs-val-' + id + '">' +
            parseFloat(value).toFixed(1) + (unit ? ' ' + unit : '') + '</div>';

    setTimeout(function() {
        var slider = document.getElementById('vs-' + id);
        var valEl = document.getElementById('vs-val-' + id);
        if (!slider) return;
        slider.addEventListener('input', function() {
            var v = parseFloat(this.value);
            valEl.textContent = v.toFixed(1) + (unit ? ' ' + unit : '');
            if (onChange) onChange(v);
        });
    }, 50);

    return wrap;
}

/* ── Toggle Switch ───────────────────────────────────────────────────── */
function createToggle(id, label, value, onChange) {
    var wrap = document.createElement('div');
    wrap.className = 'pc-toggle-wrap';
    wrap.innerHTML =
        '<span class="pc-toggle-label">' + label + '</span>' +
        '<label class="pc-toggle">' +
            '<input type="checkbox" id="tog-' + id + '"' + (value ? ' checked' : '') + '>' +
            '<span class="pc-toggle-slider"></span>' +
        '</label>';

    setTimeout(function() {
        var cb = document.getElementById('tog-' + id);
        if (!cb) return;
        cb.addEventListener('change', function() {
            if (onChange) onChange(this.checked);
        });
    }, 50);

    return wrap;
}

/* ── Dropdown Select ─────────────────────────────────────────────────── */
function createSelect(id, label, options, value, onChange) {
    var wrap = document.createElement('div');
    wrap.className = 'pc-select-wrap';
    var optsHtml = '';
    for (var i = 0; i < options.length; i++) {
        var opt = options[i];
        var val = typeof opt === 'object' ? opt.value : opt;
        var lbl = typeof opt === 'object' ? opt.label : opt;
        optsHtml += '<option value="' + val + '"' + (val === value ? ' selected' : '') + '>' + lbl + '</option>';
    }
    wrap.innerHTML =
        '<div class="pc-select-label">' + label + '</div>' +
        '<select class="pc-select" id="sel-' + id + '">' + optsHtml + '</select>';

    setTimeout(function() {
        var sel = document.getElementById('sel-' + id);
        if (!sel) return;
        sel.addEventListener('change', function() {
            if (onChange) onChange(this.value);
        });
    }, 50);

    return wrap;
}

/* ── Horizontal Slider (for crossfaders, duration) ───────────────────── */
function createHSlider(id, label, value, min, max, step, unit, onChange) {
    var wrap = document.createElement('div');
    wrap.className = 'pc-hslider-wrap';
    wrap.innerHTML =
        '<div class="pc-hslider-label">' + label +
            ' <span class="pc-hslider-value" id="hs-val-' + id + '">' +
            parseFloat(value).toFixed(1) + (unit ? ' ' + unit : '') + '</span></div>' +
        '<input type="range" class="pc-hslider" id="hs-' + id + '" ' +
            'min="' + min + '" max="' + max + '" step="' + step + '" value="' + value + '">';

    setTimeout(function() {
        var slider = document.getElementById('hs-' + id);
        var valEl = document.getElementById('hs-val-' + id);
        if (!slider) return;
        slider.addEventListener('input', function() {
            var v = parseFloat(this.value);
            valEl.textContent = v.toFixed(1) + (unit ? ' ' + unit : '');
            if (onChange) onChange(v);
        });
    }, 50);

    return wrap;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Per-Type Config Panel Builders
 * ═══════════════════════════════════════════════════════════════════════════ */

var configBuilders = {};

/* ── EQ: 10 band sliders + Q selectors + preset dropdown ────────────── */
configBuilders.eq = function(container, params) {
    var presets = ['flat', 'classic_rock', 'country', 'modern_rock', 'broadcast', 'spoken_word',
                   'bass_boost', 'treble_boost', 'mid_scoop', 'loudness'];

    container.appendChild(createSelect('eq-preset', 'Preset', presets, params.preset || 'flat',
        function(v) { debouncedUpdate('eq-preset', { preset: v }); }));

    container.appendChild(createToggle('eq-enabled', 'Enabled', params.enabled !== false,
        function(v) {
            mc1Api('PUT', '/api/v1/effects/global', { unit_id: _currentUnitId, enabled: v });
        }));

    var bandWrap = document.createElement('div');
    bandWrap.className = 'pc-eq-bands';
    var freqs = ['31Hz', '63Hz', '125Hz', '250Hz', '500Hz', '1kHz', '2kHz', '4kHz', '8kHz', '16kHz'];
    var gains = params.band_gains || [0,0,0,0,0,0,0,0,0,0];

    for (var i = 0; i < 10; i++) {
        (function(idx) {
            var gain = gains[idx] || 0;
            bandWrap.appendChild(createVSlider('eq-b' + idx, freqs[idx], gain, -12, 12, 0.5, 'dB',
                function(v) {
                    var g = {}; g['band_' + idx + '_gain'] = v;
                    debouncedUpdate('eq-b' + idx, g);
                }));
        })(i);
    }
    container.appendChild(bandWrap);

    // Q per band section
    var qLabel = document.createElement('div');
    qLabel.className = 'pc-section-label';
    qLabel.textContent = 'Q / Bandwidth';
    container.appendChild(qLabel);

    var qWrap = document.createElement('div');
    qWrap.className = 'pc-eq-q-row';
    var qOpts = [
        { value: '0.5', label: '0.5 (Wide)' },
        { value: '1.0', label: '1.0' },
        { value: '1.4', label: '1.4 (Default)' },
        { value: '2.0', label: '2.0' },
        { value: '4.0', label: '4.0 (Narrow)' },
        { value: '8.0', label: '8.0 (Very Narrow)' }
    ];
    for (var j = 0; j < 10; j++) {
        (function(idx) {
            var qVal = (params.band_q && params.band_q[idx]) ? String(params.band_q[idx]) : '1.4';
            qWrap.appendChild(createSelect('eq-q' + idx, freqs[idx], qOpts, qVal,
                function(v) {
                    var p = {}; p['band_' + idx + '_q'] = parseFloat(v);
                    debouncedUpdate('eq-q' + idx, p);
                }));
        })(j);
    }
    container.appendChild(qWrap);
};

/* ── Compressor: threshold, ratio, attack, release, makeup ───────────── */
configBuilders.compressor = function(container, params) {
    container.appendChild(createToggle('comp-enabled', 'Enabled', params.enabled !== false,
        function(v) {
            mc1Api('PUT', '/api/v1/effects/global', { unit_id: _currentUnitId, enabled: v });
        }));

    var knobRow = document.createElement('div');
    knobRow.className = 'pc-knob-row';

    knobRow.appendChild(createKnob('comp-thresh', 'Threshold', params.threshold_db || -20, -60, 0, 1, 'dB',
        function(v) { debouncedUpdate('comp-thresh', { threshold_db: v }); }));

    knobRow.appendChild(createKnob('comp-ratio', 'Ratio', params.ratio || 4, 1, 20, 0.5, ':1',
        function(v) { debouncedUpdate('comp-ratio', { ratio: v }); }));

    knobRow.appendChild(createKnob('comp-atk', 'Attack', params.attack_ms || 10, 0.1, 100, 0.5, 'ms',
        function(v) { debouncedUpdate('comp-atk', { attack_ms: v }); }));

    knobRow.appendChild(createKnob('comp-rel', 'Release', params.release_ms || 100, 10, 1000, 10, 'ms',
        function(v) { debouncedUpdate('comp-rel', { release_ms: v }); }));

    container.appendChild(knobRow);

    container.appendChild(createHSlider('comp-makeup', 'Makeup Gain', params.makeup_gain_db || 0, 0, 24, 0.5, 'dB',
        function(v) { debouncedUpdate('comp-makeup', { makeup_gain_db: v }); }));

    container.appendChild(createHSlider('comp-limiter', 'Limiter Ceiling', params.limiter_db || -1, -12, 0, 0.5, 'dB',
        function(v) { debouncedUpdate('comp-limiter', { limiter_db: v }); }));
};

/* ── Limiter: ceiling knob + enable toggle ───────────────────────────── */
configBuilders.limiter = function(container, params) {
    container.appendChild(createToggle('lim-enabled', 'Enabled', params.enabled !== false,
        function(v) {
            mc1Api('PUT', '/api/v1/effects/global', { unit_id: _currentUnitId, enabled: v });
        }));

    var knobRow = document.createElement('div');
    knobRow.className = 'pc-knob-row';

    knobRow.appendChild(createKnob('lim-ceil', 'Ceiling', params.ceiling_db || -1, -12, 0, 0.5, 'dB',
        function(v) { debouncedUpdate('lim-ceil', { ceiling_db: v }); }));

    knobRow.appendChild(createKnob('lim-atk', 'Attack', params.attack_ms || 0.5, 0.01, 10, 0.01, 'ms',
        function(v) { debouncedUpdate('lim-atk', { attack_ms: v }); }));

    knobRow.appendChild(createKnob('lim-rel', 'Release', params.release_ms || 50, 1, 500, 1, 'ms',
        function(v) { debouncedUpdate('lim-rel', { release_ms: v }); }));

    container.appendChild(knobRow);
};

/* ── Noise Gate: threshold, attack, release, hold ────────────────────── */
configBuilders.noise_gate = function(container, params) {
    container.appendChild(createToggle('gate-enabled', 'Enabled', params.enabled !== false,
        function(v) {
            mc1Api('PUT', '/api/v1/effects/global', { unit_id: _currentUnitId, enabled: v });
        }));

    var knobRow = document.createElement('div');
    knobRow.className = 'pc-knob-row';

    knobRow.appendChild(createKnob('gate-thresh', 'Threshold', params.threshold_db || -50, -80, 0, 1, 'dB',
        function(v) { debouncedUpdate('gate-thresh', { threshold_db: v }); }));

    knobRow.appendChild(createKnob('gate-atk', 'Attack', params.attack_ms || 1, 0.1, 50, 0.1, 'ms',
        function(v) { debouncedUpdate('gate-atk', { attack_ms: v }); }));

    knobRow.appendChild(createKnob('gate-rel', 'Release', params.release_ms || 100, 1, 500, 5, 'ms',
        function(v) { debouncedUpdate('gate-rel', { release_ms: v }); }));

    knobRow.appendChild(createKnob('gate-hold', 'Hold', params.hold_ms || 50, 0, 500, 5, 'ms',
        function(v) { debouncedUpdate('gate-hold', { hold_ms: v }); }));

    container.appendChild(knobRow);
};

/* ── Sidechain Ducker: depth, attack, release, keybind ───────────────── */
configBuilders.ducker = function(container, params) {
    container.appendChild(createToggle('duck-enabled', 'Enabled', params.enabled !== false,
        function(v) {
            mc1Api('PUT', '/api/v1/effects/global', { unit_id: _currentUnitId, enabled: v });
        }));

    var knobRow = document.createElement('div');
    knobRow.className = 'pc-knob-row';

    knobRow.appendChild(createKnob('duck-depth', 'Depth', params.duck_amount_db || -15, -30, -3, 1, 'dB',
        function(v) { debouncedUpdate('duck-depth', { duck_amount_db: v }); }));

    knobRow.appendChild(createKnob('duck-atk', 'Attack', params.attack_ms || 50, 5, 200, 5, 'ms',
        function(v) { debouncedUpdate('duck-atk', { attack_ms: v }); }));

    knobRow.appendChild(createKnob('duck-rel', 'Release', params.release_ms || 500, 50, 2000, 50, 'ms',
        function(v) { debouncedUpdate('duck-rel', { release_ms: v }); }));

    container.appendChild(knobRow);

    var keyOpts = [
        { value: 'Space', label: 'Spacebar' },
        { value: 'KeyT', label: 'T key' },
        { value: 'KeyP', label: 'P key' },
        { value: 'mouse', label: 'Mouse button' }
    ];
    container.appendChild(createSelect('duck-key', 'PTT Keybind', keyOpts, params.keybind || 'Space',
        function(v) { debouncedUpdate('duck-key', { keybind: v }); }));
};

/* ── Dead Air Detector (no user-adjustable config in panel) ──────────── */
configBuilders.dead_air = function(container, params) {
    container.appendChild(createToggle('dair-enabled', 'Enabled', params.enabled !== false,
        function(v) {
            mc1Api('PUT', '/api/v1/effects/global', { unit_id: _currentUnitId, enabled: v });
        }));

    var knobRow = document.createElement('div');
    knobRow.className = 'pc-knob-row';

    knobRow.appendChild(createKnob('dair-thresh', 'Threshold', params.threshold_db || -60, -96, -20, 1, 'dB',
        function(v) { debouncedUpdate('dair-thresh', { threshold_db: v }); }));

    knobRow.appendChild(createKnob('dair-timeout', 'Timeout', params.timeout_sec || 10, 1, 120, 1, 's',
        function(v) { debouncedUpdate('dair-timeout', { timeout_sec: v }); }));

    container.appendChild(knobRow);
};

/* ── DJ Crossfader: curve type, position ─────────────────────────────── */
configBuilders.crossfader = function(container, params) {
    var curves = [
        { value: 'linear', label: 'Linear' },
        { value: 'equal_power', label: 'Equal Power (Constant Power)' },
        { value: 's_curve', label: 'S-Curve' },
        { value: 'exponential', label: 'Exponential' },
        { value: 'logarithmic', label: 'Log Taper' },
        { value: 'broadcast', label: 'Broadcast Blend (EBU)' },
        { value: 'transform', label: 'Transform Cut' },
        { value: 'hard_cut', label: 'Hard Cut' },
        { value: 'dual_open', label: 'Dual Open' }
    ];

    container.appendChild(createSelect('xf-curve', 'Curve Type', curves, params.curve || 'equal_power',
        function(v) { debouncedUpdate('xf-curve', { curve: v }); }));

    container.appendChild(createHSlider('xf-pos', 'Position', params.position || 0.5, 0, 1, 0.01, '',
        function(v) { debouncedUpdate('xf-pos', { position: v }); }));
};

/* ── Track Crossfader: curve type, duration ──────────────────────────── */
configBuilders.track_crossfader = function(container, params) {
    var curves = [
        { value: 'linear', label: 'Linear' },
        { value: 'equal_power', label: 'Equal Power' },
        { value: 's_curve', label: 'S-Curve' },
        { value: 'exponential', label: 'Exponential' }
    ];

    container.appendChild(createSelect('txf-curve', 'Curve Type', curves, params.curve || 'equal_power',
        function(v) { debouncedUpdate('txf-curve', { curve: v }); }));

    container.appendChild(createHSlider('txf-dur', 'Duration', params.duration_sec || 3, 0.5, 15, 0.5, 's',
        function(v) { debouncedUpdate('txf-dur', { duration_sec: v }); }));
};

/* ── Reverb: mix, decay, damping, room_size, pre_delay_ms ────────────── */
configBuilders.reverb = function(container, params) {
    container.appendChild(createToggle('rev-enabled', 'Enabled', params.enabled !== false,
        function(v) {
            mc1Api('PUT', '/api/v1/effects/global', { unit_id: _currentUnitId, enabled: v });
        }));

    var knobRow = document.createElement('div');
    knobRow.className = 'pc-knob-row';

    knobRow.appendChild(createKnob('rev-mix', 'Mix', params.mix || 0.3, 0, 1, 0.01, '',
        function(v) { debouncedUpdate('rev-mix', { mix: v }); }));

    knobRow.appendChild(createKnob('rev-decay', 'Decay', params.decay || 1.5, 0.1, 5, 0.1, 's',
        function(v) { debouncedUpdate('rev-decay', { decay: v }); }));

    knobRow.appendChild(createKnob('rev-damp', 'Damping', params.damping || 0.5, 0, 1, 0.01, '',
        function(v) { debouncedUpdate('rev-damp', { damping: v }); }));

    knobRow.appendChild(createKnob('rev-room', 'Room Size', params.room_size || 0.7, 0.1, 1, 0.01, '',
        function(v) { debouncedUpdate('rev-room', { room_size: v }); }));

    container.appendChild(knobRow);

    container.appendChild(createHSlider('rev-predly', 'Pre-Delay', params.pre_delay_ms || 20, 0, 100, 1, 'ms',
        function(v) { debouncedUpdate('rev-predly', { pre_delay_ms: v }); }));
};

/* ── Delay: delay_ms, feedback, mix, filter_hz, stereo_spread ─────────── */
configBuilders.delay = function(container, params) {
    container.appendChild(createToggle('del-enabled', 'Enabled', params.enabled !== false,
        function(v) {
            mc1Api('PUT', '/api/v1/effects/global', { unit_id: _currentUnitId, enabled: v });
        }));

    var knobRow = document.createElement('div');
    knobRow.className = 'pc-knob-row';

    knobRow.appendChild(createKnob('del-time', 'Delay', params.delay_ms || 250, 10, 2000, 1, 'ms',
        function(v) { debouncedUpdate('del-time', { delay_ms: v }); }));

    knobRow.appendChild(createKnob('del-fdbk', 'Feedback', params.feedback || 0.4, 0, 0.95, 0.01, '',
        function(v) { debouncedUpdate('del-fdbk', { feedback: v }); }));

    knobRow.appendChild(createKnob('del-mix', 'Mix', params.mix || 0.3, 0, 1, 0.01, '',
        function(v) { debouncedUpdate('del-mix', { mix: v }); }));

    container.appendChild(knobRow);

    container.appendChild(createHSlider('del-filter', 'Filter', params.filter_hz || 3000, 200, 8000, 50, 'Hz',
        function(v) { debouncedUpdate('del-filter', { filter_hz: v }); }));

    container.appendChild(createHSlider('del-spread', 'Stereo Spread', params.stereo_spread || 0, 0, 1, 0.01, '',
        function(v) { debouncedUpdate('del-spread', { stereo_spread: v }); }));
};

/* ── Loudness: standard preset, target_lufs, target_tp, lra_max ────────── */
configBuilders.loudness = function(container, params) {
    container.appendChild(createToggle('loud-enabled', 'Enabled', params.enabled !== false,
        function(v) {
            mc1Api('PUT', '/api/v1/effects/global', { unit_id: _currentUnitId, enabled: v });
        }));

    /* Standard preset dropdown */
    container.appendChild(createSelect('loud-standard', 'Standard', [
        { value: 'ebu_r128', label: 'EBU R128 (-23 LUFS)' },
        { value: 'atsc_a85', label: 'ATSC A/85 (-24 LUFS)' },
        { value: 'podcast',  label: 'Podcast (-16 LUFS)' },
        { value: 'spotify',  label: 'Spotify (-14 LUFS)' },
        { value: 'youtube',  label: 'YouTube (-14 LUFS)' },
        { value: 'custom',   label: 'Custom' }
    ], params.standard || 'podcast', function(v) {
        debouncedUpdate('loud-standard', { standard: v });
    }));

    var knobRow = document.createElement('div');
    knobRow.className = 'pc-knob-row';

    knobRow.appendChild(createKnob('loud-target', 'Target LUFS', params.target_lufs || -16, -36, -8, 0.5, 'LUFS',
        function(v) { debouncedUpdate('loud-target', { target_lufs: v, standard: 'custom' }); }));

    knobRow.appendChild(createKnob('loud-tp', 'True Peak', params.target_tp || -1, -6, 0, 0.1, 'dBTP',
        function(v) { debouncedUpdate('loud-tp', { target_tp: v, standard: 'custom' }); }));

    knobRow.appendChild(createKnob('loud-lra', 'LRA Max', params.lra_max || 0, 0, 20, 0.5, 'LU',
        function(v) { debouncedUpdate('loud-lra', { lra_max: v, standard: 'custom' }); }));

    container.appendChild(knobRow);

    /* Live loudness readout */
    var readout = document.createElement('div');
    readout.className = 'pc-readout';
    readout.style.cssText = 'margin-top:12px;padding:10px;background:rgba(10,15,24,.6);border:1px solid rgba(42,52,68,.6);border-radius:6px;font-family:monospace;font-size:11px;line-height:1.8;color:#8a9ab0';
    var integ = params.integrated_lufs !== undefined ? params.integrated_lufs.toFixed(1) : '--';
    var moment = params.momentary_lufs !== undefined ? params.momentary_lufs.toFixed(1) : '--';
    var st = params.short_term_lufs !== undefined ? params.short_term_lufs.toFixed(1) : '--';
    var tp = params.true_peak_dbtp !== undefined ? params.true_peak_dbtp.toFixed(1) : '--';
    var lra = params.loudness_range_lu !== undefined ? params.loudness_range_lu.toFixed(1) : '0.0';
    var gc = params.gain_correction_db !== undefined ? params.gain_correction_db.toFixed(1) : '0.0';
    var comp = params.compliant ? '<span style="color:#22c55e">COMPLIANT</span>' : '<span style="color:#ef4444">NON-COMPLIANT</span>';

    readout.innerHTML =
        '<div>Integrated: <span style="color:#14b8a6;font-weight:700">' + integ + ' LUFS</span></div>' +
        '<div>Momentary: <span style="color:#14b8a6">' + moment + ' LUFS</span></div>' +
        '<div>Short-term: <span style="color:#14b8a6">' + st + ' LUFS</span></div>' +
        '<div>True Peak: <span style="color:#a78bfa">' + tp + ' dBTP</span></div>' +
        '<div>LRA: <span style="color:#f59e0b">' + lra + ' LU</span></div>' +
        '<div>Gain: <span style="color:#3b82f6">' + (parseFloat(gc) >= 0 ? '+' : '') + gc + ' dB</span></div>' +
        '<div>Status: ' + comp + '</div>';
    container.appendChild(readout);
};

/* ═══════════════════════════════════════════════════════════════════════════
 * Panel Management
 * ═══════════════════════════════════════════════════════════════════════════ */

function createPanel() {
    if (_backdrop) return;

    _backdrop = document.createElement('div');
    _backdrop.className = 'pc-backdrop';
    _backdrop.addEventListener('click', function() { closePedalConfig(); });
    document.body.appendChild(_backdrop);

    _panel = document.createElement('div');
    _panel.className = 'pc-panel';
    _panel.innerHTML =
        '<div class="pc-panel-header">' +
            '<div class="pc-panel-title">' +
                '<span class="pc-panel-name"></span>' +
                '<span class="pc-panel-ver"></span>' +
            '</div>' +
            '<button class="pc-panel-close" onclick="closePedalConfig()">' +
                '<i class="fa-solid fa-xmark"></i>' +
            '</button>' +
        '</div>' +
        '<div class="pc-panel-body"></div>';
    document.body.appendChild(_panel);
}

/**
 * openPedalConfig(type, unitId, currentParams, slotId)
 * @param {string} type - effect type_id
 * @param {number} unitId - unit ID in the rack
 * @param {object} currentParams - current parameter values
 * @param {number|null} slotId - encoder slot ID (null for global)
 */
window.openPedalConfig = function(type, unitId, currentParams, slotId) {
    createPanel();
    _currentUnitId = unitId;
    _currentSlotId = slotId;

    var builder = configBuilders[type];
    var name = (type || 'unknown').replace(/_/g, ' ').toUpperCase();

    _panel.querySelector('.pc-panel-name').textContent = name;
    _panel.querySelector('.pc-panel-ver').textContent = currentParams._version || '';

    var body = _panel.querySelector('.pc-panel-body');
    body.innerHTML = '';

    if (builder) {
        builder(body, currentParams || {});
    } else {
        body.innerHTML = '<div class="pc-stub-note">No configuration available for this effect type.</div>';
    }

    // Animate in
    requestAnimationFrame(function() {
        _backdrop.classList.add('open');
        _panel.classList.add('open');
    });
};

window.closePedalConfig = function() {
    if (_panel) _panel.classList.remove('open');
    if (_backdrop) _backdrop.classList.remove('open');
    _currentUnitId = null;
    _currentSlotId = null;
};

/* ═══════════════════════════════════════════════════════════════════════════
 * Real-Time Pedal Meters (Phase PB-3)
 * Canvas 2D rendering at 10Hz, fed by GET /api/v1/effects/meters
 * ═══════════════════════════════════════════════════════════════════════════ */

var _meterInterval = null;
var _meterData = {};  // unit_id -> last meter snapshot

/* ── Ensure a canvas element exists inside a pedal ────────────────────── */
function ensureMeterCanvas(pedalEl, width, height) {
    var canvas = pedalEl.querySelector('.pb-meter-canvas');
    if (!canvas) {
        canvas = document.createElement('canvas');
        canvas.className = 'pb-meter-canvas';
        canvas.width = width;
        canvas.height = height;
        canvas.style.cssText = 'position:absolute;bottom:2px;left:50%;transform:translateX(-50%);' +
            'pointer-events:none;z-index:15;';
        pedalEl.appendChild(canvas);
    }
    if (canvas.width !== width) canvas.width = width;
    if (canvas.height !== height) canvas.height = height;
    return canvas;
}

/* ── dB to linear 0..1 for bar meters (range: -60 to 0 dB) ───────────── */
function dbToBar(db) {
    if (db <= -60) return 0;
    if (db >= 0) return 1;
    return (db + 60) / 60;
}

/* ── Compressor: VU needle arc gauge for gain reduction ──────────────── */
function drawCompressorMeter(canvas, md) {
    var ctx = canvas.getContext('2d');
    var w = canvas.width, h = canvas.height;
    ctx.clearRect(0, 0, w, h);

    var gr = md.gain_reduction_db || 0;
    var inDb = md.input_db || -96;
    var outDb = md.output_db || -96;

    /* GR arc gauge: center bottom, arc from -20 dB to 0 dB */
    var cx = w / 2, cy = h - 2;
    var radius = Math.min(w / 2 - 4, h - 6);
    var startAngle = Math.PI + 0.3;
    var endAngle = 2 * Math.PI - 0.3;
    var range = 20; // -20 to 0 dB range

    /* Background arc */
    ctx.beginPath();
    ctx.arc(cx, cy, radius, startAngle, endAngle, false);
    ctx.strokeStyle = 'rgba(100,116,139,0.3)';
    ctx.lineWidth = 3;
    ctx.stroke();

    /* GR arc (teal → red as GR increases) */
    var grClamped = Math.max(-range, Math.min(0, -Math.abs(gr)));
    var grPct = Math.abs(grClamped) / range;
    var grAngle = startAngle + (1 - grPct) * (endAngle - startAngle);
    ctx.beginPath();
    ctx.arc(cx, cy, radius, grAngle, endAngle, false);
    var grColor = grPct > 0.5 ? ('rgba(239,68,68,' + Math.min(1, grPct) + ')') :
                                ('rgba(20,184,166,' + Math.max(0.4, 1 - grPct) + ')');
    ctx.strokeStyle = grColor;
    ctx.lineWidth = 3;
    ctx.stroke();

    /* Needle */
    var needleAngle = grAngle;
    var nx = cx + (radius - 4) * Math.cos(needleAngle);
    var ny = cy + (radius - 4) * Math.sin(needleAngle);
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.lineTo(nx, ny);
    ctx.strokeStyle = '#e2e8f0';
    ctx.lineWidth = 1.5;
    ctx.stroke();

    /* GR text */
    ctx.fillStyle = grPct > 0.3 ? '#ef4444' : '#94a3b8';
    ctx.font = '9px monospace';
    ctx.textAlign = 'center';
    ctx.fillText(gr.toFixed(1) + ' dB', cx, cy - radius - 2);

    /* Input/Output mini bars */
    var barW = 4, barH = h - 8, barY = 2;
    drawVertBar(ctx, 2, barY, barW, barH, dbToBar(inDb), '#3b82f6');
    drawVertBar(ctx, w - barW - 2, barY, barW, barH, dbToBar(outDb), '#14b8a6');
}

/* ── Gate: LED indicator — green when open, red when closed ──────────── */
function drawGateMeter(canvas, md) {
    var ctx = canvas.getContext('2d');
    var w = canvas.width, h = canvas.height;
    ctx.clearRect(0, 0, w, h);

    var isOpen = md.gate_open !== false;
    var inDb = md.input_db || -96;
    var outDb = md.output_db || -96;

    /* Gate LED */
    var ledR = Math.min(8, h / 2 - 2);
    var ledX = w / 2, ledY = h / 2;
    ctx.beginPath();
    ctx.arc(ledX, ledY, ledR, 0, 2 * Math.PI);
    ctx.fillStyle = isOpen ? '#22c55e' : '#ef4444';
    ctx.fill();
    /* Glow */
    ctx.beginPath();
    ctx.arc(ledX, ledY, ledR + 3, 0, 2 * Math.PI);
    ctx.fillStyle = isOpen ? 'rgba(34,197,94,0.2)' : 'rgba(239,68,68,0.2)';
    ctx.fill();

    /* Label */
    ctx.fillStyle = isOpen ? '#22c55e' : '#ef4444';
    ctx.font = '8px monospace';
    ctx.textAlign = 'center';
    ctx.fillText(isOpen ? 'OPEN' : 'CLOSED', ledX, ledY + ledR + 10);

    /* Input/Output mini bars */
    var barW = 4, barH = h - 4, barY = 2;
    drawVertBar(ctx, 2, barY, barW, barH, dbToBar(inDb), '#3b82f6');
    drawVertBar(ctx, w - barW - 2, barY, barW, barH, dbToBar(outDb), '#14b8a6');
}

/* ── EQ: frequency response curve overlay ────────────────────────────── */
function drawEqMeter(canvas, md) {
    var ctx = canvas.getContext('2d');
    var w = canvas.width, h = canvas.height;
    ctx.clearRect(0, 0, w, h);

    var resp = md.eq_response || [];
    var inDb = md.input_db || -96;
    var outDb = md.output_db || -96;

    if (resp.length > 0) {
        /* Draw frequency response curve */
        var maxGain = 12; // +/-12 dB range
        var midY = h / 2;
        var scaleY = (h - 8) / (2 * maxGain);

        /* Zero line */
        ctx.beginPath();
        ctx.moveTo(8, midY);
        ctx.lineTo(w - 8, midY);
        ctx.strokeStyle = 'rgba(100,116,139,0.25)';
        ctx.lineWidth = 1;
        ctx.stroke();

        /* Curve path */
        ctx.beginPath();
        for (var i = 0; i < resp.length; i++) {
            var x = 8 + (i / (resp.length - 1)) * (w - 16);
            var gain = Math.max(-maxGain, Math.min(maxGain, resp[i]));
            var y = midY - gain * scaleY;
            if (i === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
        }
        ctx.strokeStyle = '#14b8a6';
        ctx.lineWidth = 2;
        ctx.stroke();

        /* Fill under curve */
        ctx.lineTo(w - 8, midY);
        ctx.lineTo(8, midY);
        ctx.closePath();
        ctx.fillStyle = 'rgba(20,184,166,0.08)';
        ctx.fill();

        /* Band dots */
        for (var j = 0; j < resp.length; j++) {
            var bx = 8 + (j / (resp.length - 1)) * (w - 16);
            var bg = Math.max(-maxGain, Math.min(maxGain, resp[j]));
            var by = midY - bg * scaleY;
            ctx.beginPath();
            ctx.arc(bx, by, 2.5, 0, 2 * Math.PI);
            ctx.fillStyle = Math.abs(resp[j]) > 0.5 ? '#14b8a6' : '#64748b';
            ctx.fill();
        }
    }

    /* Input/Output mini bars at edges */
    var barW = 3, barH = h - 4, barY = 2;
    drawVertBar(ctx, 1, barY, barW, barH, dbToBar(inDb), '#3b82f6');
    drawVertBar(ctx, w - barW - 1, barY, barW, barH, dbToBar(outDb), '#14b8a6');
}

/* ── Limiter: clip LED + GR bar ──────────────────────────────────────── */
function drawLimiterMeter(canvas, md) {
    var ctx = canvas.getContext('2d');
    var w = canvas.width, h = canvas.height;
    ctx.clearRect(0, 0, w, h);

    var gr = md.gain_reduction_db || 0;
    var inDb = md.input_db || -96;
    var outDb = md.output_db || -96;
    var isClipping = Math.abs(gr) > 0.1;

    /* Clip LED */
    var ledR = Math.min(6, h / 3);
    var ledX = w / 2, ledY = ledR + 2;
    ctx.beginPath();
    ctx.arc(ledX, ledY, ledR, 0, 2 * Math.PI);
    ctx.fillStyle = isClipping ? '#ef4444' : '#334155';
    ctx.fill();
    if (isClipping) {
        ctx.beginPath();
        ctx.arc(ledX, ledY, ledR + 4, 0, 2 * Math.PI);
        ctx.fillStyle = 'rgba(239,68,68,0.25)';
        ctx.fill();
    }

    /* CLIP label */
    ctx.fillStyle = isClipping ? '#ef4444' : '#475569';
    ctx.font = '8px monospace';
    ctx.textAlign = 'center';
    ctx.fillText('CLIP', ledX, ledY + ledR + 9);

    /* GR bar (horizontal, below LED) */
    var barX = 10, barY2 = h - 8, barW2 = w - 20, barH2 = 4;
    ctx.fillStyle = 'rgba(100,116,139,0.2)';
    ctx.fillRect(barX, barY2, barW2, barH2);
    var grPct = Math.min(1, Math.abs(gr) / 12);
    if (grPct > 0) {
        ctx.fillStyle = grPct > 0.5 ? '#ef4444' : '#f59e0b';
        ctx.fillRect(barX + barW2 * (1 - grPct), barY2, barW2 * grPct, barH2);
    }

    /* Input/Output mini bars */
    var vBarW = 4, vBarH = h - 8, vBarY = 2;
    drawVertBar(ctx, 2, vBarY, vBarW, vBarH, dbToBar(inDb), '#3b82f6');
    drawVertBar(ctx, w - vBarW - 2, vBarY, vBarW, vBarH, dbToBar(outDb), '#14b8a6');
}

/* ── Generic meter (reverb, delay, etc.) — just input/output bars ────── */
function drawGenericMeter(canvas, md) {
    var ctx = canvas.getContext('2d');
    var w = canvas.width, h = canvas.height;
    ctx.clearRect(0, 0, w, h);

    var inDb = md.input_db || -96;
    var outDb = md.output_db || -96;

    var barW = 6, gap = 4;
    var totalW = barW * 2 + gap;
    var startX = (w - totalW) / 2;
    var barH = h - 4;

    drawVertBar(ctx, startX, 2, barW, barH, dbToBar(inDb), '#3b82f6');
    drawVertBar(ctx, startX + barW + gap, 2, barW, barH, dbToBar(outDb), '#14b8a6');

    ctx.fillStyle = '#64748b';
    ctx.font = '7px monospace';
    ctx.textAlign = 'center';
    ctx.fillText('IN', startX + barW / 2, h);
    ctx.fillText('OUT', startX + barW + gap + barW / 2, h);
}

/* ── Vertical bar helper ─────────────────────────────────────────────── */
function drawVertBar(ctx, x, y, w, h, pct, color) {
    /* Background */
    ctx.fillStyle = 'rgba(100,116,139,0.15)';
    ctx.fillRect(x, y, w, h);
    /* Fill from bottom */
    var fillH = h * Math.max(0, Math.min(1, pct));
    if (fillH > 0) {
        ctx.fillStyle = color;
        ctx.fillRect(x, y + h - fillH, w, fillH);
    }
}

/* ── Meter renderer dispatch ─────────────────────────────────────────── */
/* ── Loudness: LUFS level bar + compliance LED + gain correction ──────── */
function drawLoudnessMeter(canvas, md) {
    var ctx = canvas.getContext('2d');
    var w = canvas.width, h = canvas.height;
    ctx.clearRect(0, 0, w, h);

    var inDb = md.input_db || -96;
    var outDb = md.output_db || -96;
    var gr = md.gain_reduction_db || 0;

    /* Input/Output vertical bars */
    var barW = 4, barH = h - 4;
    drawVertBar(ctx, 2, 2, barW, barH, dbToBar(inDb), '#3b82f6');
    drawVertBar(ctx, w - barW - 2, 2, barW, barH, dbToBar(outDb), '#14b8a6');

    /* Gain correction bar (center, horizontal) */
    var gcBarX = 12, gcBarY = h - 6, gcBarW = w - 24, gcBarH = 3;
    ctx.fillStyle = 'rgba(100,116,139,0.2)';
    ctx.fillRect(gcBarX, gcBarY, gcBarW, gcBarH);
    var grPct = Math.min(1, Math.abs(gr) / 12);
    if (grPct > 0) {
        ctx.fillStyle = gr > 0 ? '#3b82f6' : '#f59e0b';
        if (gr > 0) {
            ctx.fillRect(gcBarX + gcBarW / 2, gcBarY, gcBarW / 2 * grPct, gcBarH);
        } else {
            ctx.fillRect(gcBarX + gcBarW / 2 * (1 - grPct), gcBarY, gcBarW / 2 * grPct, gcBarH);
        }
    }
    /* Center mark */
    ctx.fillStyle = '#64748b';
    ctx.fillRect(gcBarX + gcBarW / 2 - 0.5, gcBarY - 1, 1, gcBarH + 2);

    /* GR label */
    ctx.fillStyle = '#64748b';
    ctx.font = '7px monospace';
    ctx.textAlign = 'center';
    ctx.fillText('GC', gcBarX + gcBarW / 2, gcBarY - 2);
}

var meterRenderers = {
    'compressor': drawCompressorMeter,
    'limiter':    drawLimiterMeter,
    'noise_gate': drawGateMeter,
    'eq':         drawEqMeter,
    'reverb':     drawGenericMeter,
    'delay':      drawGenericMeter,
    'loudness':   drawLoudnessMeter
};

/* ── Poll meters and render ──────────────────────────────────────────── */
function pollMeters() {
    if (typeof mc1Api !== 'function') return;
    mc1Api('GET', '/api/v1/effects/meters').then(function(data) {
        if (!Array.isArray(data)) return;
        for (var i = 0; i < data.length; i++) {
            var md = data[i];
            _meterData[md.id] = md;

            /* Find the pedal element for this unit ID */
            var pedalEl = document.querySelector('[data-pedal-id="' + md.id + '"]');
            if (!pedalEl) continue;

            /* Determine canvas size from pedal dimensions */
            var pw = pedalEl.offsetWidth || 280;
            var ph = 36;  /* fixed meter strip height */
            var canvas = ensureMeterCanvas(pedalEl, pw - 8, ph);

            /* Dispatch to type-specific renderer */
            var renderer = meterRenderers[md.type] || drawGenericMeter;
            renderer(canvas, md);
        }
    }).catch(function() { /* meter poll failed — ignore, retry next interval */ });
}

/* ── Start/Stop meter polling (10Hz) ─────────────────────────────────── */
window.startPedalMeters = function() {
    if (_meterInterval) return;
    _meterInterval = setInterval(pollMeters, 100);
};

window.stopPedalMeters = function() {
    if (_meterInterval) {
        clearInterval(_meterInterval);
        _meterInterval = null;
    }
};

/* We auto-start meters when the effects rack page is loaded */
document.addEventListener('DOMContentLoaded', function() {
    /* Only start if we are on the effects rack page (pedalboard container exists) */
    if (document.getElementById('pedalboard') || document.querySelector('.pb-container')) {
        setTimeout(function() { window.startPedalMeters(); }, 1000);
    }
});

})();
