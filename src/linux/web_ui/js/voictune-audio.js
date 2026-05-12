/**
 * Mcaster1 VoicTune — Browser Mic Audio Capture + WebSocket
 * @version 2.0.1
 * js/voictune-audio.js
 *
 * getUserMedia capture with AudioWorklet -> WebSocket streaming to
 * VoicTune daemon on port 8355. Provides "Browser Mic" mode as
 * alternative to "Server Mic" (PortAudio capture on the daemon).
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

(function() {
'use strict';

/* ── State ───────────────────────────────────────────────────────── */
var audioCtx    = null;
var mediaStream = null;
var sourceNode  = null;
var workletNode = null;
var ws          = null;
var wsUrl       = null;
var reconnectTimer = null;
var reconnectDelay = 500;
var MAX_RECONNECT_DELAY = 10000;
var isRunning   = false;

/* WebSocket port for VoicTune daemon */
var WS_PORT = 8355;

/* ══════════════════════════════════════════════════════════════════
 * vtAudioStart(browserDeviceId)
 *
 * Starts browser mic capture via getUserMedia and streams PCM
 * float32 frames over WebSocket to the VoicTune daemon.
 * ══════════════════════════════════════════════════════════════════ */
window.vtAudioStart = function(browserDeviceId) {
    if (isRunning) {
        console.log('[VT-Audio] Already running');
        return;
    }

    var deviceId = browserDeviceId >= 0 ? String(browserDeviceId) : undefined;

    var constraints = {
        audio: {
            deviceId: deviceId ? {exact: deviceId} : undefined,
            sampleRate: 48000,
            channelCount: 1,
            echoCancellation: false,
            noiseSuppression: false,
            autoGainControl: false
        }
    };

    navigator.mediaDevices.getUserMedia(constraints).then(function(stream) {
        mediaStream = stream;
        isRunning = true;

        audioCtx = new (window.AudioContext || window.webkitAudioContext)({
            sampleRate: 48000
        });

        sourceNode = audioCtx.createMediaStreamSource(stream);

        /* Use ScriptProcessorNode for wide browser support.
         * AudioWorklet is preferred but requires HTTPS + module loading.
         * ScriptProcessorNode works everywhere and is adequate for
         * 48kHz mono PCM forwarding to WebSocket. */
        var bufferSize = 2048;
        var processorNode = audioCtx.createScriptProcessor(bufferSize, 1, 1);

        processorNode.onaudioprocess = function(e) {
            var input = e.inputBuffer.getChannelData(0);
            if (ws && ws.readyState === WebSocket.OPEN) {
                /* Send raw float32 PCM as binary */
                var buf = new Float32Array(input.length);
                buf.set(input);
                ws.send(buf.buffer);
            }
            /* Pass through silence (don't play back in browser) */
            var output = e.outputBuffer.getChannelData(0);
            for (var i = 0; i < output.length; i++) output[i] = 0;
        };

        sourceNode.connect(processorNode);
        processorNode.connect(audioCtx.destination);

        /* Store for cleanup */
        workletNode = processorNode;

        /* Connect WebSocket */
        connectWebSocket();

        console.log('[VT-Audio] Browser mic started, sampleRate=' + audioCtx.sampleRate);
        if (window.mc1Toast) mc1Toast('Browser mic active', 'ok');

    }).catch(function(err) {
        console.error('[VT-Audio] getUserMedia failed:', err);
        if (window.mc1Toast) mc1Toast('Mic access denied: ' + err.message, 'err');
        isRunning = false;
    });
};

/* ══════════════════════════════════════════════════════════════════
 * vtAudioStop()
 *
 * Stops browser mic capture and closes WebSocket.
 * ══════════════════════════════════════════════════════════════════ */
window.vtAudioStop = function() {
    isRunning = false;

    if (reconnectTimer) {
        clearTimeout(reconnectTimer);
        reconnectTimer = null;
    }

    if (ws) {
        ws.onclose = null; /* prevent reconnect */
        ws.close();
        ws = null;
    }

    if (workletNode) {
        workletNode.disconnect();
        workletNode = null;
    }

    if (sourceNode) {
        sourceNode.disconnect();
        sourceNode = null;
    }

    if (audioCtx) {
        audioCtx.close().catch(function(){});
        audioCtx = null;
    }

    if (mediaStream) {
        mediaStream.getTracks().forEach(function(t) { t.stop(); });
        mediaStream = null;
    }

    console.log('[VT-Audio] Stopped');
};

/* ══════════════════════════════════════════════════════════════════
 * WebSocket connection to VoicTune daemon
 * ══════════════════════════════════════════════════════════════════ */
function connectWebSocket() {
    var protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    wsUrl = protocol + '//' + window.location.hostname + ':' + WS_PORT;

    console.log('[VT-Audio] Connecting WebSocket to ' + wsUrl);

    ws = new WebSocket(wsUrl);
    ws.binaryType = 'arraybuffer';

    ws.onopen = function() {
        console.log('[VT-Audio] WebSocket connected');
        reconnectDelay = 500; /* reset backoff */

        /* Send config message */
        var config = {
            type: 'config',
            sample_rate: audioCtx ? audioCtx.sampleRate : 48000,
            channels: 1,
            format: 'float32'
        };
        ws.send(JSON.stringify(config));
    };

    ws.onmessage = function(evt) {
        /* Server may send JSON messages back (e.g. status updates) */
        if (typeof evt.data === 'string') {
            try {
                var msg = JSON.parse(evt.data);
                console.log('[VT-Audio] WS message:', msg);
            } catch(e) {}
        }
    };

    ws.onerror = function(err) {
        console.error('[VT-Audio] WebSocket error:', err);
    };

    ws.onclose = function(evt) {
        console.log('[VT-Audio] WebSocket closed, code=' + evt.code);
        ws = null;

        /* Auto-reconnect if still running */
        if (isRunning) {
            reconnectTimer = setTimeout(function() {
                if (isRunning) {
                    reconnectDelay = Math.min(reconnectDelay * 2, MAX_RECONNECT_DELAY);
                    connectWebSocket();
                }
            }, reconnectDelay);
        }
    };
}

/* ══════════════════════════════════════════════════════════════════
 * vtAudioEnumerateBrowserDevices(selectElement)
 *
 * Populates a <select> with browser audio input devices.
 * ══════════════════════════════════════════════════════════════════ */
window.vtAudioEnumerateBrowserDevices = function(selectEl) {
    if (!selectEl) return;

    /* Need to request mic permission first to get device labels */
    navigator.mediaDevices.getUserMedia({audio: true}).then(function(stream) {
        /* Stop the temp stream immediately */
        stream.getTracks().forEach(function(t) { t.stop(); });

        return navigator.mediaDevices.enumerateDevices();
    }).then(function(devices) {
        selectEl.innerHTML = '';
        var inputDevices = devices.filter(function(d) { return d.kind === 'audioinput'; });

        if (inputDevices.length === 0) {
            selectEl.innerHTML = '<option value="-1">No input devices</option>';
            return;
        }

        inputDevices.forEach(function(dev, idx) {
            var opt = document.createElement('option');
            opt.value = dev.deviceId;
            opt.textContent = dev.label || ('Microphone ' + (idx + 1));
            if (dev.deviceId === 'default') opt.textContent += ' (default)';
            selectEl.appendChild(opt);
        });
    }).catch(function(err) {
        console.error('[VT-Audio] Device enumeration failed:', err);
        selectEl.innerHTML = '<option value="-1">Mic access denied</option>';
    });
};

/* ── Listen for device changes (hotplug) ────────────────────────── */
if (navigator.mediaDevices && navigator.mediaDevices.addEventListener) {
    navigator.mediaDevices.addEventListener('devicechange', function() {
        console.log('[VT-Audio] Device change detected');
        /* Re-enumerate if in browser mode */
        var srcBtn = document.getElementById('vt-src-browser');
        if (srcBtn && srcBtn.classList.contains('active')) {
            var sel = document.getElementById('vt-device-select');
            if (sel) vtAudioEnumerateBrowserDevices(sel);
        }
    });
}

})();
