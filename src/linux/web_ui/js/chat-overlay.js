/*
 * Mcaster1 Chat Overlay Engine
 * js/chat-overlay.js
 *
 * Renders live chat messages from Twitch, YouTube, built-in song requests,
 * or custom WebSocket sources as a 2D canvas overlay for the video producer.
 * The rendered canvas is composited as a WebGL texture overlay in the
 * program output (same pattern as lower-third / text crawl).
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

(function() {
'use strict';

/* ======================================================================
 * ChatMessage — single chat message data
 * ====================================================================== */

function ChatMessage(username, text, color) {
    this.username = username || 'Anonymous';
    this.text = text || '';
    this.color = color || '#14b8a6';
    this.addedAt = Date.now();
    this.opacity = 1.0;
}

/* ======================================================================
 * ChatOverlay — main overlay engine
 * ====================================================================== */

function ChatOverlay(config) {
    config = config || {};

    // Display config
    this.maxVisible = config.maxVisible || 5;
    this.duration = config.duration || 10000;        // ms before fade out
    this.fadeDuration = config.fadeDuration || 1000;  // fade out duration ms
    this.fontSize = config.fontSize || 16;
    this.position = config.position || 'br';          // br, bl, tr, tl
    this.globalOpacity = config.opacity || 0.85;
    this.bubblePadding = 8;
    this.bubbleSpacing = 4;
    this.bubbleRadius = 6;
    this.bubbleBg = 'rgba(15, 23, 42, 0.75)';

    // Canvas for rendering
    this.canvas = document.createElement('canvas');
    this.canvas.width = config.width || 1280;
    this.canvas.height = config.height || 720;
    this.ctx = this.canvas.getContext('2d');

    // Message queue
    this._messages = [];
    this._visible = true;

    // Source state
    this._source = 'none';    // 'none' | 'builtin' | 'twitch' | 'youtube' | 'custom'
    this._pollTimer = null;
    this._twitchWs = null;
    this._twitchChannel = '';
    this._customWs = null;
    this._customWsUrl = '';
    this._ytPollTimer = null;
    this._ytVideoId = '';
    this._ytApiKey = '';
    this._ytPageToken = '';
    this._ytChatId = '';
    this._lastBuiltinId = 0;
    this._destroyed = false;
}

/* -- Public API ------------------------------------------------------- */

/**
 * Add a chat message to the queue.
 */
ChatOverlay.prototype.addMessage = function(username, text, color) {
    var msg = new ChatMessage(username, text, color);
    this._messages.push(msg);

    // Trim to max queue size (keep 3x visible for scroll history)
    var maxQueue = this.maxVisible * 3;
    if (this._messages.length > maxQueue) {
        this._messages = this._messages.slice(this._messages.length - maxQueue);
    }
};

/**
 * Render visible messages onto the internal canvas.
 * Returns the canvas for WebGL texture upload, or null if hidden/empty.
 */
ChatOverlay.prototype.render = function() {
    if (!this._visible || this._destroyed) return null;

    var now = Date.now();
    var ctx = this.ctx;
    var w = this.canvas.width;
    var h = this.canvas.height;

    ctx.clearRect(0, 0, w, h);

    // Filter to visible messages (not fully faded)
    var visible = [];
    for (var i = this._messages.length - 1; i >= 0 && visible.length < this.maxVisible; i--) {
        var msg = this._messages[i];
        var age = now - msg.addedAt;
        if (age > this.duration + this.fadeDuration) continue;

        if (age > this.duration) {
            msg.opacity = 1.0 - ((age - this.duration) / this.fadeDuration);
        } else {
            msg.opacity = 1.0;
        }
        visible.unshift(msg); // oldest first at top
    }

    if (visible.length === 0) return null;

    // Measure and render bubbles
    ctx.font = 'bold ' + this.fontSize + 'px -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif';
    var lineHeight = this.fontSize + this.bubblePadding * 2;
    var totalHeight = visible.length * (lineHeight + this.bubbleSpacing) - this.bubbleSpacing;
    var maxBubbleWidth = w * 0.4;

    // Calculate start position based on position setting
    var startX, startY;
    var alignRight = (this.position === 'br' || this.position === 'tr');
    var alignBottom = (this.position === 'br' || this.position === 'bl');

    var margin = 20;

    if (alignBottom) {
        startY = h - margin - totalHeight;
    } else {
        startY = margin;
    }
    if (alignRight) {
        startX = w - margin;
    } else {
        startX = margin;
    }

    for (var v = 0; v < visible.length; v++) {
        var m = visible[v];
        var y = startY + v * (lineHeight + this.bubbleSpacing);

        // Measure text
        ctx.font = 'bold ' + this.fontSize + 'px -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif';
        var nameWidth = ctx.measureText(m.username + ': ').width;
        ctx.font = this.fontSize + 'px -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif';
        var textWidth = ctx.measureText(m.text).width;
        var bubbleWidth = Math.min(nameWidth + textWidth + this.bubblePadding * 2, maxBubbleWidth);

        var bx = alignRight ? startX - bubbleWidth : startX;
        var by = y;

        // Draw bubble background
        ctx.globalAlpha = m.opacity * this.globalOpacity;
        ctx.fillStyle = this.bubbleBg;
        this._roundRect(ctx, bx, by, bubbleWidth, lineHeight, this.bubbleRadius);
        ctx.fill();

        // Draw username
        ctx.font = 'bold ' + this.fontSize + 'px -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif';
        ctx.fillStyle = m.color;
        var textX = bx + this.bubblePadding;
        var textY = by + this.bubblePadding + this.fontSize * 0.85;
        ctx.fillText(m.username + ': ', textX, textY);

        // Draw message text
        var nameW = ctx.measureText(m.username + ': ').width;
        ctx.font = this.fontSize + 'px -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif';
        ctx.fillStyle = '#ffffff';
        var availWidth = bubbleWidth - this.bubblePadding * 2 - nameW;
        var clippedText = this._clipText(ctx, m.text, availWidth);
        ctx.fillText(clippedText, textX + nameW, textY);
    }

    ctx.globalAlpha = 1.0;
    return this.canvas;
};

/**
 * Get the overlay rect for WebGL compositing (normalized 0-1).
 */
ChatOverlay.prototype.getRect = function() {
    return { x: 0, y: 0, w: 1.0, h: 1.0 };
};

/**
 * Show/hide the overlay.
 */
ChatOverlay.prototype.show = function() { this._visible = true; };
ChatOverlay.prototype.hide = function() { this._visible = false; };
ChatOverlay.prototype.isVisible = function() { return this._visible; };

/**
 * Get the canvas for WebGL texture upload.
 */
ChatOverlay.prototype.getCanvas = function() { return this.canvas; };

/* -- Source: Built-in (song requests) --------------------------------- */

ChatOverlay.prototype.connectBuiltin = function() {
    this.disconnect();
    this._source = 'builtin';
    this._lastBuiltinId = 0;
    this._pollBuiltin();
};

ChatOverlay.prototype._pollBuiltin = function() {
    if (this._source !== 'builtin' || this._destroyed) return;
    var self = this;

    var poll = function() {
        if (self._source !== 'builtin' || self._destroyed) return;
        if (typeof mc1Api === 'function') {
            mc1Api('POST', '/app/api/requests.php', { action: 'list' }).then(function(d) {
                if (d && d.ok && Array.isArray(d.requests)) {
                    for (var i = 0; i < d.requests.length; i++) {
                        var r = d.requests[i];
                        var rid = parseInt(r.id || 0);
                        if (rid > self._lastBuiltinId) {
                            self._lastBuiltinId = rid;
                            var name = r.requester_name || 'Listener';
                            var text = 'Requests: ' + (r.title || r.track_title || 'a song');
                            if (r.message) text += ' - "' + r.message + '"';
                            self.addMessage(name, text, '#22c55e');
                        }
                    }
                }
            }).catch(function() {});
        }
        self._pollTimer = setTimeout(poll, 5000);
    };
    poll();
};

/* -- Source: Twitch IRC ------------------------------------------------ */

ChatOverlay.prototype.connectTwitch = function(channel) {
    this.disconnect();
    if (!channel) return;
    this._source = 'twitch';
    this._twitchChannel = channel.toLowerCase().replace(/^#/, '');

    var self = this;
    try {
        this._twitchWs = new WebSocket('wss://irc-ws.chat.twitch.tv:443');
    } catch (e) {
        return;
    }

    this._twitchWs.onopen = function() {
        self._twitchWs.send('CAP REQ :twitch.tv/tags twitch.tv/commands');
        self._twitchWs.send('NICK justinfan' + Math.floor(Math.random() * 99999));
        self._twitchWs.send('JOIN #' + self._twitchChannel);
    };

    this._twitchWs.onmessage = function(evt) {
        var lines = evt.data.split('\r\n');
        for (var i = 0; i < lines.length; i++) {
            var line = lines[i];
            if (!line) continue;

            // Respond to PING
            if (line.startsWith('PING')) {
                self._twitchWs.send('PONG :tmi.twitch.tv');
                continue;
            }

            // Parse PRIVMSG
            if (line.indexOf('PRIVMSG') === -1) continue;

            var parsed = self._parseTwitchMsg(line);
            if (parsed) {
                self.addMessage(parsed.username, parsed.text, parsed.color);
            }
        }
    };

    this._twitchWs.onerror = function() {};
    this._twitchWs.onclose = function() {
        // Auto-reconnect after 5s if still twitch source
        if (self._source === 'twitch' && !self._destroyed) {
            self._pollTimer = setTimeout(function() {
                self.connectTwitch(self._twitchChannel);
            }, 5000);
        }
    };
};

ChatOverlay.prototype._parseTwitchMsg = function(raw) {
    // Format: @tags :user!user@user.tmi.twitch.tv PRIVMSG #channel :message
    var tags = {};
    var rest = raw;

    // Parse tags
    if (raw.charAt(0) === '@') {
        var spaceIdx = raw.indexOf(' ');
        var tagStr = raw.substring(1, spaceIdx);
        rest = raw.substring(spaceIdx + 1);
        var pairs = tagStr.split(';');
        for (var i = 0; i < pairs.length; i++) {
            var eq = pairs[i].indexOf('=');
            if (eq !== -1) {
                tags[pairs[i].substring(0, eq)] = pairs[i].substring(eq + 1);
            }
        }
    }

    // Parse PRIVMSG
    var privIdx = rest.indexOf('PRIVMSG');
    if (privIdx === -1) return null;

    var username = tags['display-name'] || '';
    if (!username) {
        var bangIdx = rest.indexOf('!');
        if (bangIdx > 1) {
            username = rest.substring(1, bangIdx);
        }
    }

    var msgIdx = rest.indexOf(' :', privIdx);
    if (msgIdx === -1) return null;
    var text = rest.substring(msgIdx + 2);

    // Color from tags
    var color = tags['color'] || '#' + ((Math.random() * 0xCCCCCC + 0x333333) | 0).toString(16);

    return { username: username, text: text, color: color };
};

/* -- Source: YouTube Live Chat ---------------------------------------- */

ChatOverlay.prototype.connectYouTube = function(videoId, apiKey) {
    this.disconnect();
    if (!videoId || !apiKey) return;
    this._source = 'youtube';
    this._ytVideoId = videoId;
    this._ytApiKey = apiKey;
    this._ytPageToken = '';
    this._ytChatId = '';

    // First, get the liveChatId from the video
    var self = this;
    this._ytFetchChatId(function() {
        if (self._ytChatId) {
            self._pollYouTube();
        }
    });
};

ChatOverlay.prototype._ytFetchChatId = function(cb) {
    var self = this;
    var url = 'https://www.googleapis.com/youtube/v3/videos?part=liveStreamingDetails&id='
        + encodeURIComponent(this._ytVideoId) + '&key=' + encodeURIComponent(this._ytApiKey);

    fetch(url).then(function(r) { return r.json(); }).then(function(d) {
        if (d.items && d.items.length > 0 && d.items[0].liveStreamingDetails) {
            self._ytChatId = d.items[0].liveStreamingDetails.activeLiveChatId || '';
        }
        if (cb) cb();
    }).catch(function() {
        if (cb) cb();
    });
};

ChatOverlay.prototype._pollYouTube = function() {
    if (this._source !== 'youtube' || !this._ytChatId || this._destroyed) return;
    var self = this;

    var poll = function() {
        if (self._source !== 'youtube' || self._destroyed) return;

        var url = 'https://www.googleapis.com/youtube/v3/liveChat/messages?liveChatId='
            + encodeURIComponent(self._ytChatId)
            + '&part=snippet,authorDetails&key=' + encodeURIComponent(self._ytApiKey);
        if (self._ytPageToken) {
            url += '&pageToken=' + encodeURIComponent(self._ytPageToken);
        }

        fetch(url).then(function(r) { return r.json(); }).then(function(d) {
            if (d.items && Array.isArray(d.items)) {
                for (var i = 0; i < d.items.length; i++) {
                    var item = d.items[i];
                    var author = item.authorDetails ? item.authorDetails.displayName : 'Viewer';
                    var text = item.snippet ? item.snippet.displayMessage : '';
                    if (text) {
                        var color = item.authorDetails && item.authorDetails.isChatOwner ? '#ef4444' :
                                    item.authorDetails && item.authorDetails.isChatModerator ? '#22c55e' : '#0ea5e9';
                        self.addMessage(author, text, color);
                    }
                }
            }
            if (d.nextPageToken) self._ytPageToken = d.nextPageToken;

            var delay = d.pollingIntervalMillis || 6000;
            self._ytPollTimer = setTimeout(poll, delay);
        }).catch(function() {
            self._ytPollTimer = setTimeout(poll, 10000);
        });
    };
    poll();
};

/* -- Source: Custom WebSocket ----------------------------------------- */

ChatOverlay.prototype.connectCustom = function(wsUrl) {
    this.disconnect();
    if (!wsUrl) return;
    this._source = 'custom';
    this._customWsUrl = wsUrl;

    var self = this;
    try {
        this._customWs = new WebSocket(wsUrl);
    } catch (e) {
        return;
    }

    this._customWs.onmessage = function(evt) {
        try {
            var d = JSON.parse(evt.data);
            var username = d.username || d.user || d.name || 'User';
            var text = d.text || d.message || d.msg || '';
            var color = d.color || '#14b8a6';
            if (text) {
                self.addMessage(username, text, color);
            }
        } catch (e) {
            // Non-JSON message, display as-is
            if (evt.data && typeof evt.data === 'string' && evt.data.length < 500) {
                self.addMessage('Chat', evt.data, '#14b8a6');
            }
        }
    };

    this._customWs.onerror = function() {};
    this._customWs.onclose = function() {
        if (self._source === 'custom' && !self._destroyed) {
            self._pollTimer = setTimeout(function() {
                self.connectCustom(self._customWsUrl);
            }, 5000);
        }
    };
};

/* -- Disconnect all sources ------------------------------------------- */

ChatOverlay.prototype.disconnect = function() {
    this._source = 'none';

    if (this._pollTimer) {
        clearTimeout(this._pollTimer);
        this._pollTimer = null;
    }

    if (this._ytPollTimer) {
        clearTimeout(this._ytPollTimer);
        this._ytPollTimer = null;
    }

    if (this._twitchWs) {
        try { this._twitchWs.close(); } catch (e) {}
        this._twitchWs = null;
    }

    if (this._customWs) {
        try { this._customWs.close(); } catch (e) {}
        this._customWs = null;
    }
};

/* -- Destroy ---------------------------------------------------------- */

ChatOverlay.prototype.destroy = function() {
    this._destroyed = true;
    this.disconnect();
    this._messages = [];
};

/* -- Helpers ---------------------------------------------------------- */

ChatOverlay.prototype._roundRect = function(ctx, x, y, w, h, r) {
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
};

ChatOverlay.prototype._clipText = function(ctx, text, maxWidth) {
    if (maxWidth <= 0) return '';
    var measured = ctx.measureText(text).width;
    if (measured <= maxWidth) return text;

    // Binary search for clip point
    var lo = 0, hi = text.length;
    while (lo < hi) {
        var mid = (lo + hi + 1) >> 1;
        if (ctx.measureText(text.substring(0, mid) + '...').width <= maxWidth) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    return text.substring(0, lo) + '...';
};

/* -- Export ----------------------------------------------------------- */

window.Mc1ChatOverlay = ChatOverlay;

})();
