/**
 * pedalboard.js -- Core pedalboard engine with drag/drop, snap-to-grid,
 *                  collision detection, interactive bezier cable routing,
 *                  and layout persistence.
 *
 * File:    src/linux/web_ui/js/pedalboard.js
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Purpose: We implement the visual pedalboard surface where broadcast DSP
 *          effect pedals can be positioned, connected via interactive cables,
 *          and configured. PB-2 adds click-to-connect cable routing with
 *          gravity-sag bezier curves, midpoint disconnect, and signal flow
 *          animation.
 *
 * Exports:
 *   Pedalboard class with init(containerId, effects), saveLayout(), loadLayout()
 */

(function() {
'use strict';

var GRID = 20;         // snap grid size in pixels
var CABLE_COLORS = ['#14b8a6', '#0891b2', '#22c55e', '#f97316', '#eab308', '#a855f7', '#ec4899'];
var CABLE_ACTIVE  = '#14b8a6';
var CABLE_GREY    = '#64748b';
var CABLE_ERROR   = '#ef4444';

/* ═══════════════════════════════════════════════════════════════════════════
 * SVG Defs for cable glow and flow animation
 * ═══════════════════════════════════════════════════════════════════════════ */
var SVG_DEFS_MARKUP =
    '<defs>' +
        '<filter id="pb-cable-glow" x="-20%" y="-20%" width="140%" height="140%">' +
            '<feGaussianBlur in="SourceGraphic" stdDeviation="2" result="blur"/>' +
            '<feMerge><feMergeNode in="blur"/><feMergeNode in="SourceGraphic"/></feMerge>' +
        '</filter>' +
    '</defs>';

/* ═══════════════════════════════════════════════════════════════════════════
 * Pedalboard Class
 * ═══════════════════════════════════════════════════════════════════════════ */

function Pedalboard() {
    this.container = null;
    this.svgOverlay = null;
    this.pedals = {};       // id -> {el, x, y, w, h, type, unitId, enabled, params}
    this.cables = [];       // [{from: id, to: id, pathEl, midEl, hitEl, color}]
    this.versions = {};     // type -> versionInfo
    this.slotId = null;
    this._dragging = null;
    this._dragOffset = { x: 0, y: 0 };
    /* Interactive cable drawing state */
    this._cabling = null;   // {fromId, fromConn:'out', tempPath}
    this._cableMode = true; // interactive cable mode on by default
    this._savedCableJson = null; // loaded cable topology from server
}

/* ── Initialize the pedalboard surface ───────────────────────────────── */
Pedalboard.prototype.init = function(containerId, effects, versions, slotId) {
    var self = this;
    this.slotId = slotId || null;
    this.versions = versions || {};

    this.container = document.getElementById(containerId);
    if (!this.container) return;

    this.container.style.position = 'relative';
    this.container.style.overflow = 'hidden';

    // SVG overlay for cables
    this.svgOverlay = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    this.svgOverlay.setAttribute('class', 'pb-cables');
    this.svgOverlay.style.cssText = 'position:absolute;top:0;left:0;width:100%;height:100%;z-index:1;';
    this.svgOverlay.innerHTML = SVG_DEFS_MARKUP;
    this.container.appendChild(this.svgOverlay);

    // Grid background
    this.container.style.backgroundImage =
        'radial-gradient(circle, rgba(20,184,166,.08) 1px, transparent 1px)';
    this.container.style.backgroundSize = GRID + 'px ' + GRID + 'px';

    // Create pedals
    if (effects && effects.length) {
        for (var i = 0; i < effects.length; i++) {
            this.addPedal(effects[i], i);
        }
    }

    // Mouse/touch handlers on container
    this.container.addEventListener('mousedown', function(e) { self._onPointerDown(e); });
    this.container.addEventListener('mousemove', function(e) { self._onPointerMove(e); });
    this.container.addEventListener('mouseup', function(e) { self._onPointerUp(e); });
    this.container.addEventListener('touchstart', function(e) { self._onPointerDown(e); }, { passive: false });
    this.container.addEventListener('touchmove', function(e) { self._onPointerMove(e); }, { passive: false });
    this.container.addEventListener('touchend', function(e) { self._onPointerUp(e); });

    // ESC cancels cable drawing
    document.addEventListener('keydown', function(e) {
        if (e.key === 'Escape' && self._cabling) {
            self._cancelCabling();
        }
    });

    // Load saved layout
    this.loadLayout();
};

/* ── Add a pedal to the board ────────────────────────────────────────── */
Pedalboard.prototype.addPedal = function(effect, index) {
    var self = this;
    var type = effect.type || 'unknown';
    var unitId = effect.id;
    var vi = this.versions[type] || null;
    var dims = (window.PEDAL_DIMENSIONS && window.PEDAL_DIMENSIONS[type]) || { w: 280, h: 100 };
    var isFixed = (type === '__input' || type === '__output' || type === '__headend');

    // Default position: arranged in a grid (fixed nodes get special positions)
    var defaultX, defaultY;
    if (type === '__input') {
        defaultX = 0; defaultY = 200;
    } else if (type === '__output') {
        defaultX = (this.container ? this.container.clientWidth || 1200 : 1200) - dims.w;
        defaultY = 200;
    } else if (type === '__headend') {
        defaultX = (this.container ? this.container.clientWidth || 1200 : 1200) - dims.w - 20;
        defaultY = 360;
    } else {
        var col = index % 3;
        var row = Math.floor(index / 3);
        defaultX = 160 + col * (dims.w + 40);
        defaultY = 20 + row * (dims.h + 40);
    }

    // Snap to grid
    var x = Math.round(defaultX / GRID) * GRID;
    var y = Math.round(defaultY / GRID) * GRID;

    // Create pedal element
    var el = document.createElement('div');
    var classes = 'pb-pedal';
    if (effect.enabled === false) classes += ' disabled';
    if (isFixed) classes += ' pb-pedal-fixed';
    el.className = classes;
    el.setAttribute('data-pedal-id', unitId);
    el.style.cssText = 'position:absolute;width:' + dims.w + 'px;height:' + dims.h +
        'px;transform:translate(' + x + 'px,' + y + 'px);z-index:10;' +
        (isFixed ? 'cursor:default;' : 'cursor:grab;');

    // SVG faceplate
    var svgMarkup = window.generatePedalSVG ? window.generatePedalSVG(type, vi) : '';
    var controlsHtml = '';
    if (!isFixed) {
        controlsHtml =
            '<div class="pb-pedal-overlay">' +
                '<div class="pb-pedal-controls">' +
                    '<button class="pb-btn-info" data-action="info" title="Version info">' +
                        '<i class="fa-solid fa-circle-info"></i>' +
                    '</button>' +
                    '<button class="pb-btn-config" data-action="config" title="Configure">' +
                        '<i class="fa-solid fa-gear"></i>' +
                    '</button>' +
                    '<button class="pb-btn-toggle" data-action="toggle" title="Enable/Disable">' +
                        '<i class="fa-solid fa-power-off"></i>' +
                    '</button>' +
                    '<button class="pb-btn-remove" data-action="remove" title="Remove">' +
                        '<i class="fa-solid fa-trash-can"></i>' +
                    '</button>' +
                '</div>' +
                '<div class="pb-pedal-name">' + (vi ? vi.short_name : type) + '</div>' +
            '</div>';
    }

    // Connectors: input has only output, output has only input, others have both
    var connInHtml = '';
    var connOutHtml = '';
    if (type !== '__input') {
        connInHtml = '<div class="pb-connector pb-conn-in" data-conn="in" data-pedal-conn="' + unitId + '"></div>';
    }
    if (type !== '__output') {
        connOutHtml = '<div class="pb-connector pb-conn-out" data-conn="out" data-pedal-conn="' + unitId + '"></div>';
    }

    el.innerHTML =
        '<div class="pb-pedal-svg">' + svgMarkup + '</div>' +
        controlsHtml +
        connInHtml + connOutHtml;

    this.container.appendChild(el);

    this.pedals[unitId] = {
        el: el,
        x: x,
        y: y,
        w: dims.w,
        h: dims.h,
        type: type,
        unitId: unitId,
        enabled: effect.enabled !== false,
        params: effect.params || {},
        isFixed: isFixed
    };

    // Button event handling (only for non-fixed pedals)
    if (!isFixed) {
        el.addEventListener('click', function(e) {
            var btn = e.target.closest('[data-action]');
            if (!btn) return;
            e.stopPropagation();

            var action = btn.getAttribute('data-action');
            if (action === 'info') {
                self._showVersionInfo(type, vi);
            } else if (action === 'config') {
                var p = self.pedals[unitId];
                if (window.openPedalConfig) {
                    window.openPedalConfig(type, unitId, p.params, self.slotId);
                }
            } else if (action === 'toggle') {
                self._togglePedal(unitId);
            } else if (action === 'remove') {
                self._removePedal(unitId);
            }
        });
    }

    // Connector click handlers for interactive cable routing
    var connectors = el.querySelectorAll('.pb-connector');
    for (var ci = 0; ci < connectors.length; ci++) {
        (function(conn) {
            conn.addEventListener('mousedown', function(e) {
                e.stopPropagation();
                e.preventDefault();
                self._onConnectorClick(unitId, conn.getAttribute('data-conn'), e);
            });
            conn.addEventListener('touchstart', function(e) {
                e.stopPropagation();
                e.preventDefault();
                self._onConnectorClick(unitId, conn.getAttribute('data-conn'), e);
            }, { passive: false });
        })(connectors[ci]);
    }
};

/* ═══════════════════════════════════════════════════════════════════════════
 * Interactive Cable System (PB-2)
 * ═══════════════════════════════════════════════════════════════════════════ */

/* ── Start cable drawing from a connector ─────────────────────────────── */
Pedalboard.prototype._onConnectorClick = function(pedalId, connType, e) {
    var self = this;

    if (this._cabling) {
        // We are completing a cable connection
        this._completeCabling(pedalId, connType);
        return;
    }

    // Start a new cable from an output connector
    if (connType === 'out') {
        // Check if this output already has a cable (v1: 1:1 restriction)
        for (var i = 0; i < this.cables.length; i++) {
            if (this.cables[i].from === pedalId) {
                mc1Toast('Output already connected. Disconnect first.', 'warn');
                return;
            }
        }
        this._startCabling(pedalId, 'out', e);
    } else if (connType === 'in') {
        // Allow starting from input as well (draw backwards)
        for (var j = 0; j < this.cables.length; j++) {
            if (this.cables[j].to === pedalId) {
                mc1Toast('Input already connected. Disconnect first.', 'warn');
                return;
            }
        }
        this._startCabling(pedalId, 'in', e);
    }
};

/* ── Begin drawing a temporary cable ──────────────────────────────────── */
Pedalboard.prototype._startCabling = function(pedalId, connType, e) {
    var pedal = this.pedals[pedalId];
    if (!pedal) return;

    var pos = this._getConnectorPos(pedalId, connType);

    // Create temporary cable path
    var tempPath = document.createElementNS('http://www.w3.org/2000/svg', 'path');
    tempPath.setAttribute('fill', 'none');
    tempPath.setAttribute('stroke', CABLE_ACTIVE);
    tempPath.setAttribute('stroke-width', '3');
    tempPath.setAttribute('stroke-linecap', 'round');
    tempPath.setAttribute('stroke-dasharray', '8 4');
    tempPath.setAttribute('opacity', '0.8');
    tempPath.style.filter = 'url(#pb-cable-glow)';
    this.svgOverlay.appendChild(tempPath);

    this._cabling = {
        fromId: pedalId,
        fromConn: connType,
        startX: pos.x,
        startY: pos.y,
        tempPath: tempPath
    };

    // Add visual feedback
    pedal.el.classList.add('pb-cabling-source');
    this.container.classList.add('pb-cabling-active');
};

/* ── Update temp cable on mouse move ──────────────────────────────────── */
Pedalboard.prototype._updateTempCable = function(e) {
    if (!this._cabling) return;

    var clientX = e.touches ? e.touches[0].clientX : e.clientX;
    var clientY = e.touches ? e.touches[0].clientY : e.clientY;
    var rect = this.container.getBoundingClientRect();
    var mx = clientX - rect.left;
    var my = clientY - rect.top;

    var sx = this._cabling.startX;
    var sy = this._cabling.startY;

    // Draw cable from connector to mouse with gravity sag
    var d;
    if (this._cabling.fromConn === 'out') {
        d = this._cablePath(sx, sy, mx, my);
    } else {
        d = this._cablePath(mx, my, sx, sy);
    }
    this._cabling.tempPath.setAttribute('d', d);
};

/* ── Complete cable connection ─────────────────────────────────────────── */
Pedalboard.prototype._completeCabling = function(targetId, targetConn) {
    if (!this._cabling) return;

    var fromId = this._cabling.fromId;
    var fromConn = this._cabling.fromConn;

    // Determine actual from/to based on connection direction
    var sourceId, destId;
    if (fromConn === 'out' && targetConn === 'in') {
        sourceId = fromId;
        destId = targetId;
    } else if (fromConn === 'in' && targetConn === 'out') {
        sourceId = targetId;
        destId = fromId;
    } else {
        mc1Toast('Connect output to input (right to left dot)', 'warn');
        this._cancelCabling();
        return;
    }

    // Validation: no self-connection
    if (sourceId === destId) {
        mc1Toast('Cannot connect a pedal to itself', 'warn');
        this._cancelCabling();
        return;
    }

    // Validation: no duplicate connection
    for (var i = 0; i < this.cables.length; i++) {
        if (this.cables[i].from === sourceId && this.cables[i].to === destId) {
            mc1Toast('Already connected', 'warn');
            this._cancelCabling();
            return;
        }
    }

    // Validation: no multiple inputs to same pedal (v1: 1:1)
    for (var j = 0; j < this.cables.length; j++) {
        if (this.cables[j].to === destId) {
            mc1Toast('Input already connected. Disconnect existing cable first.', 'warn');
            this._cancelCabling();
            return;
        }
    }

    // Validation: no multiple outputs from same pedal (v1: 1:1)
    for (var k = 0; k < this.cables.length; k++) {
        if (this.cables[k].from === sourceId) {
            mc1Toast('Output already connected. Disconnect existing cable first.', 'warn');
            this._cancelCabling();
            return;
        }
    }

    // Validation: no circular routing
    if (this._wouldCreateCycle(sourceId, destId)) {
        mc1Toast('Circular routing detected. Connection rejected.', 'err');
        this._cancelCabling();
        return;
    }

    // Clean up temp state
    this._cancelCabling();

    // Create the actual cable
    this._createCable(sourceId, destId);

    // Sync routing to API
    this._syncRouting();
    this.saveLayout();
};

/* ── Cancel cable drawing ─────────────────────────────────────────────── */
Pedalboard.prototype._cancelCabling = function() {
    if (this._cabling) {
        if (this._cabling.tempPath && this._cabling.tempPath.parentNode) {
            this._cabling.tempPath.parentNode.removeChild(this._cabling.tempPath);
        }
        var pedal = this.pedals[this._cabling.fromId];
        if (pedal) pedal.el.classList.remove('pb-cabling-source');
    }
    this._cabling = null;
    this.container.classList.remove('pb-cabling-active');
};

/* ── Detect circular routing ──────────────────────────────────────────── */
Pedalboard.prototype._wouldCreateCycle = function(sourceId, destId) {
    // Build adjacency map from existing cables + the proposed new cable
    var adj = {};
    for (var i = 0; i < this.cables.length; i++) {
        adj[this.cables[i].from] = this.cables[i].to;
    }
    adj[sourceId] = destId;

    // Walk from destId and see if we reach sourceId (cycle)
    var visited = {};
    var current = destId;
    while (adj[current] !== undefined) {
        if (visited[current]) return true;
        if (adj[current] === sourceId) return true;
        visited[current] = true;
        current = adj[current];
    }
    return false;
};

/* ── Create a visual cable between two pedals ─────────────────────────── */
Pedalboard.prototype._createCable = function(fromId, toId) {
    var self = this;
    var colorIdx = this.cables.length % CABLE_COLORS.length;
    var color = CABLE_COLORS[colorIdx];

    var pos1 = this._getConnectorPos(fromId, 'out');
    var pos2 = this._getConnectorPos(toId, 'in');

    // Create cable group
    var d = this._cablePath(pos1.x, pos1.y, pos2.x, pos2.y);

    // Hit area path (wider, invisible, for click detection)
    var hitPath = document.createElementNS('http://www.w3.org/2000/svg', 'path');
    hitPath.setAttribute('d', d);
    hitPath.setAttribute('fill', 'none');
    hitPath.setAttribute('stroke', 'transparent');
    hitPath.setAttribute('stroke-width', '16');
    hitPath.setAttribute('stroke-linecap', 'round');
    hitPath.style.cursor = 'pointer';
    hitPath.style.pointerEvents = 'stroke';

    // Visual cable path
    var path = document.createElementNS('http://www.w3.org/2000/svg', 'path');
    path.setAttribute('d', d);
    path.setAttribute('fill', 'none');
    path.setAttribute('stroke', color);
    path.setAttribute('stroke-width', '3');
    path.setAttribute('stroke-linecap', 'round');
    path.setAttribute('opacity', '0.8');
    path.style.filter = 'url(#pb-cable-glow)';
    path.style.pointerEvents = 'none';
    path.setAttribute('class', 'pb-cable-path');

    // Flow animation dash
    path.setAttribute('stroke-dasharray', '12 6');

    // Midpoint disconnect button
    var mid = this._cableMidpoint(pos1.x, pos1.y, pos2.x, pos2.y);
    var midCircle = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
    midCircle.setAttribute('cx', mid.x);
    midCircle.setAttribute('cy', mid.y);
    midCircle.setAttribute('r', '6');
    midCircle.setAttribute('fill', '#1e293b');
    midCircle.setAttribute('stroke', color);
    midCircle.setAttribute('stroke-width', '1.5');
    midCircle.style.cursor = 'pointer';
    midCircle.style.pointerEvents = 'auto';
    midCircle.setAttribute('class', 'pb-cable-midpoint');

    this.svgOverlay.appendChild(hitPath);
    this.svgOverlay.appendChild(path);
    this.svgOverlay.appendChild(midCircle);

    var cableEntry = {
        from: fromId,
        to: toId,
        pathEl: path,
        midEl: midCircle,
        hitEl: hitPath,
        color: color
    };
    this.cables.push(cableEntry);

    // Hover effects on hit area
    hitPath.addEventListener('mouseenter', function() {
        path.setAttribute('stroke-width', '5');
        path.setAttribute('opacity', '1');
        midCircle.setAttribute('r', '8');
        midCircle.setAttribute('stroke', '#ef4444');
        midCircle.setAttribute('fill', '#1e293b');
    });
    hitPath.addEventListener('mouseleave', function() {
        path.setAttribute('stroke-width', '3');
        path.setAttribute('opacity', '0.8');
        midCircle.setAttribute('r', '6');
        midCircle.setAttribute('stroke', color);
    });

    // Click hit area or midpoint to disconnect
    var disconnectHandler = function(e) {
        e.stopPropagation();
        e.preventDefault();
        self._disconnectCable(cableEntry);
    };
    hitPath.addEventListener('click', disconnectHandler);
    midCircle.addEventListener('click', disconnectHandler);
};

/* ── Disconnect a cable ───────────────────────────────────────────────── */
Pedalboard.prototype._disconnectCable = function(cable) {
    // Pulse animation before removing
    if (cable.pathEl) {
        cable.pathEl.setAttribute('stroke', CABLE_ERROR);
        cable.pathEl.setAttribute('stroke-width', '5');
        cable.pathEl.setAttribute('opacity', '1');
    }

    var self = this;
    setTimeout(function() {
        if (cable.pathEl && cable.pathEl.parentNode) cable.pathEl.parentNode.removeChild(cable.pathEl);
        if (cable.midEl && cable.midEl.parentNode) cable.midEl.parentNode.removeChild(cable.midEl);
        if (cable.hitEl && cable.hitEl.parentNode) cable.hitEl.parentNode.removeChild(cable.hitEl);

        var idx = self.cables.indexOf(cable);
        if (idx >= 0) self.cables.splice(idx, 1);

        self._syncRouting();
        self.saveLayout();
        mc1Toast('Cable disconnected', 'ok');
    }, 200);
};

/* ── Get connector world position ─────────────────────────────────────── */
Pedalboard.prototype._getConnectorPos = function(pedalId, connType) {
    var p = this.pedals[pedalId];
    if (!p) return { x: 0, y: 0 };
    if (connType === 'out') {
        return { x: p.x + p.w, y: p.y + p.h / 2 };
    } else {
        return { x: p.x, y: p.y + p.h / 2 };
    }
};

/* ── Generate bezier path with gravity sag ────────────────────────────── */
Pedalboard.prototype._cablePath = function(x1, y1, x2, y2) {
    var dx = Math.abs(x2 - x1);
    var cpOffset = Math.max(60, dx * 0.4);
    var sag = Math.max(30, Math.min(80, dx * 0.15));

    return 'M ' + x1 + ' ' + y1 +
        ' C ' + (x1 + cpOffset) + ' ' + (y1 + sag) + ', ' +
        (x2 - cpOffset) + ' ' + (y2 + sag) + ', ' +
        x2 + ' ' + y2;
};

/* ── Get midpoint of bezier cable ─────────────────────────────────────── */
Pedalboard.prototype._cableMidpoint = function(x1, y1, x2, y2) {
    // Approximate midpoint of the cubic bezier at t=0.5
    var dx = Math.abs(x2 - x1);
    var cpOffset = Math.max(60, dx * 0.4);
    var sag = Math.max(30, Math.min(80, dx * 0.15));

    var cp1x = x1 + cpOffset, cp1y = y1 + sag;
    var cp2x = x2 - cpOffset, cp2y = y2 + sag;
    var t = 0.5;
    var mt = 1 - t;

    var mx = mt*mt*mt*x1 + 3*mt*mt*t*cp1x + 3*mt*t*t*cp2x + t*t*t*x2;
    var my = mt*mt*mt*y1 + 3*mt*mt*t*cp1y + 3*mt*t*t*cp2y + t*t*t*y2;
    return { x: mx, y: my };
};

/* ── Redraw all cables at current positions ───────────────────────────── */
Pedalboard.prototype._redrawCables = function() {
    for (var i = 0; i < this.cables.length; i++) {
        var cable = this.cables[i];
        var pos1 = this._getConnectorPos(cable.from, 'out');
        var pos2 = this._getConnectorPos(cable.to, 'in');
        var d = this._cablePath(pos1.x, pos1.y, pos2.x, pos2.y);

        if (cable.pathEl) cable.pathEl.setAttribute('d', d);
        if (cable.hitEl) cable.hitEl.setAttribute('d', d);

        var mid = this._cableMidpoint(pos1.x, pos1.y, pos2.x, pos2.y);
        if (cable.midEl) {
            cable.midEl.setAttribute('cx', mid.x);
            cable.midEl.setAttribute('cy', mid.y);
        }
    }
};

/* ── Sync cable topology to C++ routing API ───────────────────────────── */
Pedalboard.prototype._syncRouting = function() {
    // Derive routing from cable topology
    var routing = [];
    for (var i = 0; i < this.cables.length; i++) {
        var cable = this.cables[i];
        var fromP = this.pedals[cable.from];
        var toP = this.pedals[cable.to];
        if (!fromP || !toP) continue;

        var fromName = fromP.type;
        var toName = toP.type;
        // Map pseudo-pedal types to routing names
        if (fromName === '__input') fromName = 'input';
        if (toName === '__output') toName = 'output';
        if (fromName === '__headend') fromName = 'headend';

        routing.push({ from: fromName, to: toName });
    }

    mc1Api('PUT', '/api/v1/effects/global/routing', { routing: routing }).catch(function() {});
};

/* ── Rebuild all cables (legacy auto-chain fallback) ──────────────────── */
Pedalboard.prototype.renderCables = function() {
    // Clear existing cable SVG elements
    this._clearCableSVG();

    // If we have saved cable data, restore those connections
    if (this._savedCableJson && this._savedCableJson.length > 0) {
        for (var i = 0; i < this._savedCableJson.length; i++) {
            var c = this._savedCableJson[i];
            var fromId = c.from;
            var toId = c.to;
            // Validate both pedals exist
            if (this.pedals[fromId] && this.pedals[toId]) {
                this._createCable(fromId, toId);
            }
        }
        this._savedCableJson = null; // Only apply once
        return;
    }

    // If no saved cables and no existing cables, auto-chain by x position
    if (this.cables.length === 0) {
        var ids = Object.keys(this.pedals);
        // Filter out fixed nodes for auto-chain
        var normalIds = [];
        var inputId = null, outputId = null;
        for (var j = 0; j < ids.length; j++) {
            var p = this.pedals[ids[j]];
            if (p.type === '__input') inputId = ids[j];
            else if (p.type === '__output') outputId = ids[j];
            else if (p.type !== '__headend') normalIds.push(ids[j]);
        }

        // Sort normal pedals by x position
        var self = this;
        normalIds.sort(function(a, b) {
            return self.pedals[a].x - self.pedals[b].x || self.pedals[a].y - self.pedals[b].y;
        });

        // Build chain: input -> pedals in order -> output
        var chain = [];
        if (inputId) chain.push(inputId);
        for (var k = 0; k < normalIds.length; k++) chain.push(normalIds[k]);
        if (outputId) chain.push(outputId);

        for (var m = 0; m < chain.length - 1; m++) {
            this._createCable(chain[m], chain[m + 1]);
        }
    } else {
        // Just redraw existing cables at their current positions
        this._redrawCables();
    }
};

/* ── Clear cable SVG elements without clearing cable data ─────────────── */
Pedalboard.prototype._clearCableSVG = function() {
    for (var i = 0; i < this.cables.length; i++) {
        var c = this.cables[i];
        if (c.pathEl && c.pathEl.parentNode) c.pathEl.parentNode.removeChild(c.pathEl);
        if (c.midEl && c.midEl.parentNode) c.midEl.parentNode.removeChild(c.midEl);
        if (c.hitEl && c.hitEl.parentNode) c.hitEl.parentNode.removeChild(c.hitEl);
    }
    this.cables = [];
};

/* ── Pointer handling (drag pedals + cable drawing) ────────────────────── */
Pedalboard.prototype._onPointerDown = function(e) {
    // If cabling is active, clicks on empty space cancel it
    if (this._cabling) {
        var connEl = e.target.closest('.pb-connector');
        if (!connEl) {
            this._cancelCabling();
        }
        return;
    }

    var pedalEl = e.target.closest('.pb-pedal');
    if (!pedalEl) return;

    // Ignore if clicking a button or connector
    if (e.target.closest('[data-action]')) return;
    if (e.target.closest('.pb-connector')) return;

    var id = pedalEl.getAttribute('data-pedal-id');
    // Try numeric first, fallback to string for pseudo-pedals
    var numId = parseInt(id);
    var pedalKey = isNaN(numId) ? id : numId;
    var pedal = this.pedals[pedalKey];
    if (!pedal || pedal.isFixed) return;

    e.preventDefault();
    this._dragging = pedalKey;

    var clientX = e.touches ? e.touches[0].clientX : e.clientX;
    var clientY = e.touches ? e.touches[0].clientY : e.clientY;

    var rect = this.container.getBoundingClientRect();
    this._dragOffset.x = clientX - rect.left - pedal.x;
    this._dragOffset.y = clientY - rect.top - pedal.y;

    pedalEl.style.cursor = 'grabbing';
    pedalEl.style.zIndex = '100';
    pedalEl.classList.add('dragging');
};

Pedalboard.prototype._onPointerMove = function(e) {
    // Update temp cable if drawing
    if (this._cabling) {
        this._updateTempCable(e);
        return;
    }

    if (this._dragging === null) return;

    e.preventDefault();
    var clientX = e.touches ? e.touches[0].clientX : e.clientX;
    var clientY = e.touches ? e.touches[0].clientY : e.clientY;

    var rect = this.container.getBoundingClientRect();
    var rawX = clientX - rect.left - this._dragOffset.x;
    var rawY = clientY - rect.top - this._dragOffset.y;

    // Snap to grid
    var x = Math.round(rawX / GRID) * GRID;
    var y = Math.round(rawY / GRID) * GRID;

    // Clamp to container bounds
    var pedal = this.pedals[this._dragging];
    x = Math.max(0, Math.min(x, this.container.clientWidth - pedal.w));
    y = Math.max(0, Math.min(y, this.container.clientHeight - pedal.h));

    // Collision detection
    if (!this._checkCollision(this._dragging, x, y)) {
        pedal.x = x;
        pedal.y = y;
        pedal.el.style.transform = 'translate(' + x + 'px,' + y + 'px)';
        this._redrawCables();
    }
};

Pedalboard.prototype._onPointerUp = function(e) {
    if (this._dragging === null) return;

    var pedal = this.pedals[this._dragging];
    if (pedal) {
        pedal.el.style.cursor = 'grab';
        pedal.el.style.zIndex = '10';
        pedal.el.classList.remove('dragging');
    }

    this._dragging = null;
    this.saveLayout();
};

/* ── Collision detection ─────────────────────────────────────────────── */
Pedalboard.prototype._checkCollision = function(dragId, newX, newY) {
    var dragP = this.pedals[dragId];
    var margin = 4;

    for (var id in this.pedals) {
        var idKey = isNaN(parseInt(id)) ? id : parseInt(id);
        if (idKey === dragId) continue;
        var p = this.pedals[id];

        if (newX < p.x + p.w + margin &&
            newX + dragP.w + margin > p.x &&
            newY < p.y + p.h + margin &&
            newY + dragP.h + margin > p.y) {
            return true;
        }
    }
    return false;
};

/* ── Version info modal ──────────────────────────────────────────────── */
Pedalboard.prototype._showVersionInfo = function(type, vi) {
    if (!vi) {
        mc1Toast('No version info available for ' + type, 'warn');
        return;
    }

    // Create modal
    var overlay = document.createElement('div');
    overlay.className = 'pb-modal-overlay';
    overlay.addEventListener('click', function(e) {
        if (e.target === overlay) document.body.removeChild(overlay);
    });

    var modal = document.createElement('div');
    modal.className = 'pb-modal';
    modal.innerHTML =
        '<div class="pb-modal-header">' +
            '<h3>' + (vi.brand_name || type) + '</h3>' +
            '<button onclick="this.closest(\'.pb-modal-overlay\').remove()">' +
                '<i class="fa-solid fa-xmark"></i>' +
            '</button>' +
        '</div>' +
        '<div class="pb-modal-body">' +
            '<div class="pb-modal-row"><span class="pb-modal-label">Type</span><span>' + (vi.type_id || type) + '</span></div>' +
            '<div class="pb-modal-row"><span class="pb-modal-label">Version</span><span class="badge-teal badge">' + (vi.version || '?') + '</span></div>' +
            '<div class="pb-modal-row"><span class="pb-modal-label">Released</span><span>' + (vi.release_date || 'Unknown') + '</span></div>' +
            '<div class="pb-modal-row"><span class="pb-modal-label">Status</span><span class="badge ' + (vi.is_stub ? 'badge-orange' : 'badge-green') + '">' + (vi.is_stub ? 'Stub' : 'Active') + '</span></div>' +
            '<div class="pb-modal-desc">' +
                '<div class="pb-modal-label">Description</div>' +
                '<p>' + (vi.description || 'No description.') + '</p>' +
            '</div>' +
            '<div class="pb-modal-desc">' +
                '<div class="pb-modal-label">Changelog</div>' +
                '<p>' + (vi.changelog || 'No changelog.') + '</p>' +
            '</div>' +
        '</div>';

    overlay.appendChild(modal);
    document.body.appendChild(overlay);
};

/* ── Toggle pedal enabled ────────────────────────────────────────────── */
Pedalboard.prototype._togglePedal = function(unitId) {
    var pedal = this.pedals[unitId];
    if (!pedal) return;

    var newState = !pedal.enabled;
    mc1Api('PUT', '/api/v1/effects/global', { unit_id: unitId, enabled: newState }).then(function(d) {
        if (d && d.ok) {
            pedal.enabled = newState;
            pedal.el.classList.toggle('disabled', !newState);
            mc1Toast(newState ? 'Enabled' : 'Disabled', 'ok');
        }
    });
};

/* ── Remove pedal from rack ──────────────────────────────────────────── */
Pedalboard.prototype._removePedal = function(unitId) {
    var self = this;
    if (!confirm('Remove this effect from the rack?')) return;

    mc1Api('DELETE', '/api/v1/effects/global/units/' + unitId).then(function(d) {
        if (d && d.ok) {
            // Remove all cables connected to this pedal
            var toRemove = [];
            for (var i = 0; i < self.cables.length; i++) {
                if (self.cables[i].from === unitId || self.cables[i].to === unitId) {
                    toRemove.push(self.cables[i]);
                }
            }
            for (var j = 0; j < toRemove.length; j++) {
                var c = toRemove[j];
                if (c.pathEl && c.pathEl.parentNode) c.pathEl.parentNode.removeChild(c.pathEl);
                if (c.midEl && c.midEl.parentNode) c.midEl.parentNode.removeChild(c.midEl);
                if (c.hitEl && c.hitEl.parentNode) c.hitEl.parentNode.removeChild(c.hitEl);
                var idx = self.cables.indexOf(c);
                if (idx >= 0) self.cables.splice(idx, 1);
            }

            var pedal = self.pedals[unitId];
            if (pedal && pedal.el && pedal.el.parentNode) {
                pedal.el.parentNode.removeChild(pedal.el);
            }
            delete self.pedals[unitId];
            self._syncRouting();
            mc1Toast('Effect removed', 'ok');
        }
    });
};

/* ── Save layout to server ───────────────────────────────────────────── */
Pedalboard.prototype.saveLayout = function() {
    var layoutJson = {};
    for (var id in this.pedals) {
        var p = this.pedals[id];
        layoutJson[id] = { x: p.x, y: p.y };
    }

    var cableJson = [];
    for (var i = 0; i < this.cables.length; i++) {
        cableJson.push({ from: this.cables[i].from, to: this.cables[i].to });
    }

    mc1Api('PUT', '/api/v1/pedalboard/layout', {
        slot_id: this.slotId,
        layout_json: layoutJson,
        cable_json: cableJson
    }).catch(function() {});

    // Also save to localStorage as fallback
    try {
        var key = 'pb_layout_' + (this.slotId || 'global');
        localStorage.setItem(key, JSON.stringify(layoutJson));
        localStorage.setItem(key + '_cables', JSON.stringify(cableJson));
    } catch (e) {}
};

/* ── Load layout from server (or localStorage fallback) ──────────────── */
Pedalboard.prototype.loadLayout = function() {
    var self = this;

    mc1Api('GET', '/api/v1/pedalboard/layout?slot_id=' + (this.slotId || '')).then(function(d) {
        if (d && d.ok && d.layout_json) {
            self._applyLayout(d.layout_json);
            if (d.cable_json && d.cable_json.length > 0) {
                self._savedCableJson = d.cable_json;
            }
        } else {
            self._loadLocalLayout();
        }
        self.renderCables();
    }).catch(function() {
        self._loadLocalLayout();
        self.renderCables();
    });
};

Pedalboard.prototype._loadLocalLayout = function() {
    try {
        var key = 'pb_layout_' + (this.slotId || 'global');
        var data = localStorage.getItem(key);
        if (data) {
            this._applyLayout(JSON.parse(data));
        }
        var cableData = localStorage.getItem(key + '_cables');
        if (cableData) {
            this._savedCableJson = JSON.parse(cableData);
        }
    } catch (e) {}
    this.renderCables();
};

Pedalboard.prototype._applyLayout = function(layout) {
    for (var id in layout) {
        var pedal = this.pedals[id];
        if (!pedal) continue;
        var pos = layout[id];
        var x = Math.round((pos.x || 0) / GRID) * GRID;
        var y = Math.round((pos.y || 0) / GRID) * GRID;

        // Clamp
        x = Math.max(0, Math.min(x, (this.container.clientWidth || 1200) - pedal.w));
        y = Math.max(0, Math.min(y, (this.container.clientHeight || 800) - pedal.h));

        pedal.x = x;
        pedal.y = y;
        pedal.el.style.transform = 'translate(' + x + 'px,' + y + 'px)';
    }
};

/* ── Reset layout to default positions ───────────────────────────────── */
Pedalboard.prototype.resetLayout = function() {
    var ids = Object.keys(this.pedals);
    for (var i = 0; i < ids.length; i++) {
        var pedal = this.pedals[ids[i]];
        if (pedal.isFixed) continue;
        var normalIdx = 0;
        for (var j = 0; j < i; j++) {
            if (!this.pedals[ids[j]].isFixed) normalIdx++;
        }
        var col = normalIdx % 3;
        var row = Math.floor(normalIdx / 3);
        var x = 160 + col * (pedal.w + 40);
        var y = 20 + row * (pedal.h + 40);
        x = Math.round(x / GRID) * GRID;
        y = Math.round(y / GRID) * GRID;

        pedal.x = x;
        pedal.y = y;
        pedal.el.style.transform = 'translate(' + x + 'px,' + y + 'px)';
    }
    // Clear existing cables and auto-chain
    this._clearCableSVG();
    this.renderCables();
    this.saveLayout();
};

/* ── Refresh pedals with new data (after API reload) ─────────────────── */
Pedalboard.prototype.refresh = function(effects) {
    // Remove pedals that no longer exist (skip fixed pseudo-pedals)
    for (var id in this.pedals) {
        if (this.pedals[id].isFixed) continue;
        var found = false;
        for (var i = 0; i < effects.length; i++) {
            if (effects[i].id === parseInt(id)) { found = true; break; }
        }
        if (!found) {
            if (this.pedals[id].el && this.pedals[id].el.parentNode) {
                this.pedals[id].el.parentNode.removeChild(this.pedals[id].el);
            }
            delete this.pedals[id];
        }
    }

    // Add new pedals
    for (var j = 0; j < effects.length; j++) {
        if (!this.pedals[effects[j].id]) {
            this.addPedal(effects[j], Object.keys(this.pedals).length);
        } else {
            // Update params
            this.pedals[effects[j].id].params = effects[j].params || {};
            this.pedals[effects[j].id].enabled = effects[j].enabled !== false;
            this.pedals[effects[j].id].el.classList.toggle('disabled', !this.pedals[effects[j].id].enabled);
        }
    }
    this._redrawCables();
};

/* ── Export ───────────────────────────────────────────────────────────── */
window.Pedalboard = Pedalboard;

})();
