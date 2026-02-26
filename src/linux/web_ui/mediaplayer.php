<?php
/**
 * mediaplayer.php — Browser Media Player
 *
 * File:    src/linux/web_ui/mediaplayer.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-02-24
 * Purpose: We provide a full-featured HTML5 media player with:
 *          - Queue management (add, remove, reorder, clear)
 *          - Transport controls (play/pause, prev, next, seek, volume)
 *          - Shuffle and repeat modes (off/one/all)
 *          - Cover art display with MusicBrainz art URL
 *          - Auto-add and play via ?track_id=N URL parameter
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We wrap all startup JS in DOMContentLoaded
 *  - We use mc1Api() for all AJAX and mc1Toast() for notifications
 *  - We use h() on all PHP-rendered user data
 *
 * CHANGELOG:
 *  2026-02-24  v1.0.0  Initial implementation — full HTML5 player with queue
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/app/inc/auth.php';
require_once __DIR__ . '/app/inc/mc1_config.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/user_auth.php';

$page_title = 'Media Player';
$active_nav = 'mediaplayer';
$use_charts = false;

/* We pre-load track info if track_id is given in the URL (auto-play on open) */
$preload_track = null;
$preload_tid   = (int)($_GET['track_id'] ?? 0);
if ($preload_tid > 0) {
    try {
        $stmt = mc1_db('mcaster1_media')->prepare(
            'SELECT id, title, artist, album, cover_art_hash, duration_ms
             FROM tracks WHERE id = ? AND is_missing = 0 LIMIT 1'
        );
        $stmt->execute([$preload_tid]);
        $preload_track = $stmt->fetch(\PDO::FETCH_ASSOC) ?: null;
    } catch (Exception $e) {}
}

require_once __DIR__ . '/app/inc/header.php';
?>

<style>
/* ── Media Player Layout ───────────────────────────────────────────────── */
.mp-outer      { display:flex; flex-direction:column; height:calc(100vh - var(--topbar-h));
                 overflow:hidden; }
.mp-layout     { display:grid; grid-template-columns:1fr 320px; flex:1;
                 min-height:0; overflow:hidden; }

/* ── Track Info Panel (left) ───────────────────────────────────────────── */
.mp-info       { display:flex; flex-direction:column; align-items:center;
                 justify-content:center; padding:40px 32px; gap:20px;
                 overflow:hidden; }
.mp-cover      { width:220px; height:220px; border-radius:12px; object-fit:cover;
                 background:var(--bg3); flex-shrink:0;
                 box-shadow:0 12px 40px rgba(0,0,0,.5); }
.mp-cover-ph   { width:220px; height:220px; border-radius:12px; background:var(--bg3);
                 display:flex; align-items:center; justify-content:center;
                 font-size:64px; color:var(--border); flex-shrink:0; }
.mp-track-title  { font-size:22px; font-weight:700; color:var(--text);
                   text-align:center; max-width:380px; line-height:1.3; }
.mp-track-artist { font-size:15px; color:var(--teal); text-align:center; font-weight:500; }
.mp-track-album  { font-size:12px; color:var(--muted); text-align:center; }
.mp-track-dur    { font-size:11px; color:var(--muted); font-variant-numeric:tabular-nums; }

/* ── Queue Panel (right) ───────────────────────────────────────────────── */
.mp-queue      { border-left:1px solid var(--border); display:flex;
                 flex-direction:column; overflow:hidden; min-height:0; }
.mp-queue-hdr  { padding:12px 14px; border-bottom:1px solid var(--border);
                 display:flex; align-items:center; justify-content:space-between;
                 flex-shrink:0; gap:8px; }
.mp-queue-list { flex:1; overflow-y:auto; }
.mp-q-item     { display:flex; align-items:center; gap:9px;
                 padding:8px 12px; cursor:pointer;
                 border-bottom:1px solid rgba(51,65,85,.3); transition:background .1s; }
.mp-q-item:hover { background:var(--bg3); }
.mp-q-item.current { background:rgba(20,184,166,.1); border-left:3px solid var(--teal); }
.mp-q-pos      { width:22px; text-align:center; font-size:11px; color:var(--muted);
                 flex-shrink:0; font-variant-numeric:tabular-nums; }
.mp-q-art      { width:38px; height:38px; border-radius:4px; object-fit:cover;
                 flex-shrink:0; background:var(--bg3); }
.mp-q-artph    { width:38px; height:38px; border-radius:4px; background:var(--bg3);
                 display:flex; align-items:center; justify-content:center;
                 font-size:13px; color:var(--muted); flex-shrink:0; }
.mp-q-info     { flex:1; min-width:0; }
.mp-q-title    { font-size:13px; color:var(--text); white-space:nowrap;
                 overflow:hidden; text-overflow:ellipsis; }
.mp-q-artist   { font-size:11px; color:var(--muted); white-space:nowrap;
                 overflow:hidden; text-overflow:ellipsis; }
.mp-q-dur      { font-size:10px; color:var(--muted); flex-shrink:0;
                 font-variant-numeric:tabular-nums; }
.mp-q-rm       { flex-shrink:0; background:none; border:none; color:var(--muted);
                 font-size:12px; cursor:pointer; opacity:0; padding:3px 6px;
                 border-radius:3px; }
.mp-q-item:hover .mp-q-rm { opacity:1; }
.mp-q-rm:hover { color:var(--red); background:rgba(239,68,68,.12); }

/* ── Transport Bar ─────────────────────────────────────────────────────── */
.mp-transport  { flex-shrink:0; border-top:1px solid var(--border);
                 background:var(--bg2); padding:12px 28px 14px; }
.mp-seek-row   { display:flex; align-items:center; gap:10px; margin-bottom:10px; }
.mp-time       { font-size:11px; color:var(--muted); font-variant-numeric:tabular-nums;
                 min-width:36px; text-align:center; }
.mp-seekbar    { flex:1; -webkit-appearance:none; appearance:none;
                 height:5px; border-radius:3px; background:var(--border);
                 outline:none; cursor:pointer; }
.mp-seekbar::-webkit-slider-thumb { -webkit-appearance:none; width:16px; height:16px;
                 border-radius:50%; background:var(--teal); cursor:pointer;
                 box-shadow:0 0 4px rgba(20,184,166,.5); }
.mp-controls   { display:flex; align-items:center; justify-content:center; gap:16px; }
.mp-btn        { background:none; border:none; color:var(--text-dim); font-size:19px;
                 cursor:pointer; padding:6px; border-radius:6px;
                 transition:color .12s, background .12s; line-height:1; }
.mp-btn:hover  { color:var(--text); background:rgba(255,255,255,.06); }
.mp-btn.active { color:var(--teal); }
.mp-btn-play   { width:46px; height:46px; border-radius:50%; background:var(--teal);
                 color:#0f172a; font-size:18px; display:flex; align-items:center;
                 justify-content:center; border:none; cursor:pointer;
                 transition:background .15s, transform .1s; }
.mp-btn-play:hover { background:var(--teal2); transform:scale(1.06); }
.mp-side-ctrl  { flex:1; display:flex; align-items:center; gap:8px; }
.mp-side-ctrl.right { justify-content:flex-end; }
.mp-volbar     { width:80px; -webkit-appearance:none; appearance:none;
                 height:4px; border-radius:2px; background:var(--border); outline:none; cursor:pointer; }
.mp-volbar::-webkit-slider-thumb { -webkit-appearance:none; width:13px; height:13px;
                 border-radius:50%; background:var(--teal); cursor:pointer; }

/* ── Queue context menu ─────────────────────────────────────────────────── */
.mp-q-ctx-menu {
  position:fixed; z-index:9000; background:var(--bg2);
  border:1px solid var(--border); border-radius:6px; padding:4px 0;
  min-width:170px; box-shadow:0 6px 24px rgba(0,0,0,.5);
  display:none; user-select:none;
}
.mp-q-ctx-item {
  display:flex; align-items:center; gap:8px; padding:7px 14px;
  cursor:pointer; font-size:12px; color:var(--text-dim);
}
.mp-q-ctx-item:hover { background:var(--bg3); color:var(--text); }
.mp-q-ctx-item i { width:14px; text-align:center; color:var(--teal); font-size:12px; }
.mp-q-ctx-item.danger { color:var(--red); }
.mp-q-ctx-item.danger i { color:var(--red); }
.mp-q-ctx-item.danger:hover { background:rgba(239,68,68,.1); }
.mp-q-ctx-sep { border:none; border-top:1px solid var(--border); margin:3px 0; }
</style>

<!-- Hidden audio element — JS drives this entirely -->
<audio id="mp-audio" preload="auto" style="display:none"></audio>

<!-- Queue item context menu -->
<div class="mp-q-ctx-menu" id="mp-q-ctx-menu">
  <div class="mp-q-ctx-item" onclick="qCtxPlayNow()">
    <i class="fa-solid fa-play"></i> Play Now
  </div>
  <div class="mp-q-ctx-item" onclick="qCtxMoveToTop()">
    <i class="fa-solid fa-arrow-up-to-line"></i> Move to Top
  </div>
  <hr class="mp-q-ctx-sep">
  <div class="mp-q-ctx-item danger" onclick="qCtxRemove()">
    <i class="fa-solid fa-xmark"></i> Remove from Queue
  </div>
</div>

<div class="mp-outer">
  <!-- Two-column content: track info | queue -->
  <div class="mp-layout">

    <!-- Left: Now Playing Info -->
    <div class="mp-info" id="mp-info">
      <div id="mp-cover-wrap">
        <div class="mp-cover-ph"><i class="fa-solid fa-music"></i></div>
      </div>
      <div style="text-align:center">
        <div class="mp-track-title" id="mp-track-title">No track loaded</div>
        <div class="mp-track-artist" id="mp-track-artist" style="margin-top:6px"></div>
        <div class="mp-track-album"  id="mp-track-album"  style="margin-top:3px"></div>
      </div>
    </div>

    <!-- Right: Queue -->
    <div class="mp-queue">
      <div class="mp-queue-hdr">
        <span style="font-size:13px;font-weight:600;color:var(--text)">
          <i class="fa-solid fa-list-ul fa-fw" style="color:var(--teal)"></i>
          Queue <span id="mp-q-count" class="badge badge-gray ms-1">0</span>
        </span>
        <div style="display:flex;gap:5px">
          <button class="btn btn-secondary btn-xs" onclick="openPopoutPlayer()"
                  title="Launch standalone player — keeps playing while you navigate">
            <i class="fa-solid fa-arrow-up-right-from-square"></i>
          </button>
          <button class="btn btn-secondary btn-xs" onclick="playerClearQueue()" title="Clear all">
            <i class="fa-solid fa-trash-can"></i>
          </button>
        </div>
      </div>
      <div class="mp-queue-list" id="mp-queue-list">
        <div class="empty" style="padding:48px 20px">
          <i class="fa-solid fa-headphones fa-fw"></i>
          <p style="margin-top:12px;font-size:13px">Queue is empty</p>
          <p style="font-size:11px;margin-top:6px;color:var(--muted)">
            In Media Library: right-click a track → <strong>Play Track</strong>
          </p>
        </div>
      </div>
    </div>
  </div>

  <!-- Transport Bar (pinned at bottom of mp-outer) -->
  <div class="mp-transport">
    <div class="mp-seek-row">
      <span class="mp-time" id="mp-pos">0:00</span>
      <input type="range" class="mp-seekbar" id="mp-seekbar" min="0" max="1000" value="0" step="1">
      <span class="mp-time" id="mp-dur">0:00</span>
    </div>
    <div style="display:flex;align-items:center;gap:8px">
      <div class="mp-side-ctrl">
        <button class="mp-btn" id="mp-btn-shuffle" onclick="playerToggleShuffle()" title="Shuffle">
          <i class="fa-solid fa-shuffle"></i>
        </button>
      </div>
      <div class="mp-controls">
        <button class="mp-btn" onclick="playerPrev()" title="Previous">
          <i class="fa-solid fa-backward-step"></i>
        </button>
        <button class="mp-btn-play" id="mp-btn-play" onclick="playerPlayPause()">
          <i class="fa-solid fa-play" id="mp-play-icon"></i>
        </button>
        <button class="mp-btn" onclick="playerNext()" title="Next">
          <i class="fa-solid fa-forward-step"></i>
        </button>
      </div>
      <div class="mp-side-ctrl right">
        <button class="mp-btn" id="mp-btn-repeat" onclick="playerCycleRepeat()" title="Repeat: off">
          <i class="fa-solid fa-repeat"></i>
        </button>
        <i class="fa-solid fa-volume-low" style="color:var(--muted);font-size:13px;flex-shrink:0"></i>
        <input type="range" class="mp-volbar" id="mp-volbar" min="0" max="100" value="80">
        <span id="mp-vol-pct" style="font-size:11px;color:var(--muted);min-width:30px;text-align:right">80%</span>
      </div>
    </div>
  </div>
</div>

<script>
(function() {
/* ── Player State ──────────────────────────────────────────────────────── */
var queue      = [];      /* Array of queue item objects from the API */
var currentIdx = -1;      /* Index in queue[] currently playing */
var shuffle    = false;
var repeatMode = 0;       /* 0=off, 1=one, 2=all */
var seeking    = false;

var audio   = document.getElementById('mp-audio');
var seekbar = document.getElementById('mp-seekbar');
var volbar  = document.getElementById('mp-volbar');

/* ── Audio element event handlers ─────────────────────────────────────── */
audio.addEventListener('timeupdate', function() {
    if (seeking || !audio.duration) { return; }
    var pct = (audio.currentTime / audio.duration) * 1000;
    seekbar.value = Math.floor(pct);
    document.getElementById('mp-pos').textContent = fmtTime(audio.currentTime);
});

audio.addEventListener('loadedmetadata', function() {
    document.getElementById('mp-dur').textContent = fmtTime(audio.duration);
});

audio.addEventListener('ended', function() {
    playerNext();
});

audio.addEventListener('play', function() {
    document.getElementById('mp-play-icon').className = 'fa-solid fa-pause';
});

audio.addEventListener('pause', function() {
    document.getElementById('mp-play-icon').className = 'fa-solid fa-play';
});

audio.addEventListener('error', function() {
    mc1Toast('Playback error — track may be missing or corrupt', 'err');
});

/* ── Seek bar interaction ──────────────────────────────────────────────── */
seekbar.addEventListener('mousedown', function() { seeking = true; });
seekbar.addEventListener('touchstart', function() { seeking = true; });

seekbar.addEventListener('mouseup', function() {
    seeking = false;
    if (audio.duration) {
        audio.currentTime = (seekbar.value / 1000) * audio.duration;
    }
});
seekbar.addEventListener('touchend', function() {
    seeking = false;
    if (audio.duration) {
        audio.currentTime = (seekbar.value / 1000) * audio.duration;
    }
});

seekbar.addEventListener('input', function() {
    /* We show the time during drag without actually seeking yet */
    if (audio.duration) {
        var t = (seekbar.value / 1000) * audio.duration;
        document.getElementById('mp-pos').textContent = fmtTime(t);
    }
});

/* ── Volume bar ────────────────────────────────────────────────────────── */
volbar.addEventListener('input', function() {
    audio.volume = volbar.value / 100;
    document.getElementById('mp-vol-pct').textContent = volbar.value + '%';
    mc1State.set('player', 'volume', parseInt(volbar.value));
});

/* ── Transport controls ────────────────────────────────────────────────── */
window.playerPlayPause = function() {
    if (!queue.length) { mc1Toast('Queue is empty', 'warn'); return; }
    if (currentIdx < 0) { playerJumpTo(0); return; }
    if (audio.paused) {
        audio.play().catch(function(e) { mc1Toast('Playback blocked: ' + e.message, 'err'); });
    } else {
        audio.pause();
    }
};

window.playerNext = function() {
    if (!queue.length) { return; }
    var next;
    if (repeatMode === 1) {
        /* We repeat the current track */
        next = currentIdx < 0 ? 0 : currentIdx;
    } else if (shuffle) {
        /* We pick a random track different from current */
        var pool = [];
        for (var i = 0; i < queue.length; i++) { if (i !== currentIdx) { pool.push(i); } }
        if (!pool.length) {
            if (repeatMode === 2) { pool = [0]; } else { audio.pause(); return; }
        }
        next = pool[Math.floor(Math.random() * pool.length)];
    } else {
        next = currentIdx + 1;
        if (next >= queue.length) {
            if (repeatMode === 2) { next = 0; }
            else { audio.pause(); currentIdx = -1; updateNowPlaying(null); renderQueue(); return; }
        }
    }
    playerJumpTo(next);
};

window.playerPrev = function() {
    if (!queue.length) { return; }
    /* We restart current track if more than 3 seconds in */
    if (audio.currentTime > 3) { audio.currentTime = 0; return; }
    var prev = currentIdx - 1;
    if (prev < 0) { prev = (repeatMode === 2) ? queue.length - 1 : 0; }
    playerJumpTo(prev);
};

window.playerJumpTo = function(idx) {
    if (idx < 0 || idx >= queue.length) { return; }
    currentIdx  = idx;
    var item    = queue[idx];
    /* We save the current queue_id so we can restore position after navigation */
    mc1State.set('player', 'queueId', item.queue_id || null);
    audio.src   = '/app/api/audio.php?id=' + item.track_id;
    audio.load();
    audio.play().catch(function(e) {
        mc1Toast('Cannot play track — ' + (e.message || 'unknown error'), 'err');
    });
    updateNowPlaying(item);
    renderQueue();
    /* We auto-fetch album art from MusicBrainz if the track has none */
    if (!item.art_url && item.track_id && (item.artist || item.title)) {
        mc1Api('POST', '/app/api/tracks.php', {
            action: 'fetch_art', track_id: item.track_id,
            artist: item.artist || '', title: item.title || '', album: item.album || '',
            embed_in_file: false,
        }).then(function(d) {
            if (d.ok && d.art_url) {
                item.art_url = d.art_url;
                if (queue[currentIdx] && queue[currentIdx].track_id === item.track_id) {
                    updateNowPlaying(item);
                    renderQueue();
                }
            }
        }).catch(function() {});
    }
};

window.playerToggleShuffle = function() {
    shuffle = !shuffle;
    mc1State.set('player', 'shuffle', shuffle);
    document.getElementById('mp-btn-shuffle').classList.toggle('active', shuffle);
    mc1Toast(shuffle ? 'Shuffle on' : 'Shuffle off', 'ok');
};

window.playerCycleRepeat = function() {
    repeatMode = (repeatMode + 1) % 3;
    mc1State.set('player', 'repeat', repeatMode);
    var btn  = document.getElementById('mp-btn-repeat');
    var icon = btn.querySelector('i');
    if (repeatMode === 0) {
        btn.classList.remove('active');
        btn.title = 'Repeat: off';
        icon.className = 'fa-solid fa-repeat';
        mc1Toast('Repeat off', 'ok');
    } else if (repeatMode === 1) {
        btn.classList.add('active');
        btn.title = 'Repeat: one';
        icon.className = 'fa-solid fa-repeat-1';
        mc1Toast('Repeat one', 'ok');
    } else {
        btn.classList.add('active');
        btn.title = 'Repeat: all';
        icon.className = 'fa-solid fa-repeat';
        mc1Toast('Repeat all', 'ok');
    }
};

/* ── Queue management ──────────────────────────────────────────────────── */
function loadQueue() {
    mc1Api('POST', '/app/api/player.php', {action: 'queue_list'}).then(function(d) {
        if (d.ok) {
            queue = d.queue || [];
            /* We restore the last-playing position by matching saved queue_id */
            if (currentIdx < 0) {
                var savedQueueId = mc1State.get('player', 'queueId', null);
                if (savedQueueId !== null) {
                    var idx = queue.findIndex(function(i){ return i.queue_id === savedQueueId; });
                    if (idx >= 0) { currentIdx = idx; updateNowPlaying(queue[idx]); }
                }
            }
            renderQueue();
        }
    }).catch(function() {});
}

window.playerClearQueue = function() {
    if (!queue.length) { return; }
    if (!confirm('Clear the entire playback queue?')) { return; }
    mc1Api('POST', '/app/api/player.php', {action: 'queue_clear'}).then(function(d) {
        if (d.ok) {
            queue = [];
            currentIdx = -1;
            mc1State.del('player', 'queueId');
            audio.pause();
            audio.src = '';
            seekbar.value = 0;
            document.getElementById('mp-pos').textContent = '0:00';
            document.getElementById('mp-dur').textContent = '0:00';
            updateNowPlaying(null);
            renderQueue();
            mc1Toast('Queue cleared', 'ok');
        }
    }).catch(function() { mc1Toast('Clear failed', 'err'); });
};

window.playerRemoveFromQueue = function(queue_id) {
    mc1Api('POST', '/app/api/player.php', {action: 'queue_remove', queue_id: queue_id})
    .then(function(d) {
        if (d.ok) {
            /* We adjust currentIdx if a track before the current was removed */
            var removedIdx = queue.findIndex(function(i) { return i.queue_id === queue_id; });
            queue = d.queue || [];
            if (removedIdx >= 0 && removedIdx < currentIdx) { currentIdx--; }
            else if (removedIdx === currentIdx) { currentIdx = -1; }
            renderQueue();
        }
    }).catch(function() { mc1Toast('Remove failed', 'err'); });
};

/* ── Queue rendering ───────────────────────────────────────────────────── */
function renderQueue() {
    var list = document.getElementById('mp-queue-list');
    var cnt  = document.getElementById('mp-q-count');
    cnt.textContent = queue.length;

    if (!queue.length) {
        list.innerHTML = '<div class="empty" style="padding:48px 20px">'
            + '<i class="fa-solid fa-headphones fa-fw"></i>'
            + '<p style="margin-top:12px;font-size:13px">Queue is empty</p>'
            + '<p style="font-size:11px;margin-top:6px;color:var(--muted)">Right-click a track → <strong>Play Track</strong></p>'
            + '</div>';
        return;
    }

    var html = '';
    for (var i = 0; i < queue.length; i++) {
        var item      = queue[i];
        var isCurrent = (i === currentIdx);
        var artHtml   = item.art_url
            ? '<img class="mp-q-art" src="' + _e(item.art_url) + '" onerror="this.outerHTML=\'<div class=mp-q-artph><i class=\\\'fa-solid fa-music\\\'></i></div>\'">'
            : '<div class="mp-q-artph"><i class="fa-solid fa-music"></i></div>';
        var dur = item.duration_ms > 0 ? fmtTime(item.duration_ms / 1000) : '';
        html += '<div class="mp-q-item' + (isCurrent ? ' current' : '') + '" onclick="playerJumpTo(' + i + ')" oncontextmenu="qCtxShow(event,' + i + ')" data-qid="' + item.queue_id + '">'
              + '<span class="mp-q-pos">' + (isCurrent ? '<i class="fa-solid fa-volume-high" style="color:var(--teal);font-size:10px"></i>' : (i + 1)) + '</span>'
              + artHtml
              + '<div class="mp-q-info">'
              + '<div class="mp-q-title">' + _e(item.title || '—') + '</div>'
              + '<div class="mp-q-artist">' + _e(item.artist || '') + '</div>'
              + '</div>'
              + (dur ? '<span class="mp-q-dur">' + dur + '</span>' : '')
              + '<button class="mp-q-rm" onclick="event.stopPropagation();playerRemoveFromQueue(' + item.queue_id + ')" title="Remove"><i class="fa-solid fa-xmark"></i></button>'
              + '</div>';
    }
    list.innerHTML = html;

    /* We scroll the current track into view smoothly */
    if (currentIdx >= 0) {
        var items = list.querySelectorAll('.mp-q-item');
        if (items[currentIdx]) {
            items[currentIdx].scrollIntoView({block: 'nearest', behavior: 'smooth'});
        }
    }
}

/* ── Now Playing panel ─────────────────────────────────────────────────── */
function updateNowPlaying(item) {
    var titleEl   = document.getElementById('mp-track-title');
    var artistEl  = document.getElementById('mp-track-artist');
    var albumEl   = document.getElementById('mp-track-album');
    var coverWrap = document.getElementById('mp-cover-wrap');

    if (!item) {
        titleEl.textContent  = 'No track loaded';
        artistEl.textContent = '';
        albumEl.textContent  = '';
        coverWrap.innerHTML  = '<div class="mp-cover-ph"><i class="fa-solid fa-music"></i></div>';
        document.title       = 'Media Player — Mcaster1';
        return;
    }

    titleEl.textContent  = item.title  || '—';
    artistEl.textContent = item.artist || '';
    albumEl.textContent  = item.album  || '';
    document.title       = (item.artist ? item.artist + ' — ' : '') + (item.title || '?') + ' — Mcaster1';

    if (item.art_url) {
        coverWrap.innerHTML = '<img class="mp-cover" src="' + _e(item.art_url) + '"'
            + ' onerror="this.outerHTML=\'<div class=mp-cover-ph><i class=\\\'fa-solid fa-music\\\'></i></div>\'">';
    } else {
        coverWrap.innerHTML = '<div class="mp-cover-ph"><i class="fa-solid fa-music"></i></div>';
    }
}

/* ── Keyboard shortcuts ─────────────────────────────────────────────────── */
document.addEventListener('keydown', function(e) {
    /* We skip if user is typing in an input */
    if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') { return; }
    if (e.code === 'Space') { e.preventDefault(); playerPlayPause(); }
    if (e.code === 'ArrowRight') { if (audio.duration) audio.currentTime = Math.min(audio.duration, audio.currentTime + 5); }
    if (e.code === 'ArrowLeft')  { audio.currentTime = Math.max(0, audio.currentTime - 5); }
    if (e.code === 'ArrowUp')    { e.preventDefault(); audio.volume = Math.min(1, audio.volume + 0.05); volbar.value = Math.round(audio.volume * 100); document.getElementById('mp-vol-pct').textContent = volbar.value + '%'; }
    if (e.code === 'ArrowDown')  { e.preventDefault(); audio.volume = Math.max(0, audio.volume - 0.05); volbar.value = Math.round(audio.volume * 100); document.getElementById('mp-vol-pct').textContent = volbar.value + '%'; }
    if (e.code === 'KeyN') { playerNext(); }
    if (e.code === 'KeyP') { playerPrev(); }
});

/* ════════════════════════════════════════════════════════════════════════
 * QUEUE CONTEXT MENU
 * ════════════════════════════════════════════════════════════════════════ */
var qCtxIdx     = -1;
var qCtxQueueId = null;

window.qCtxShow = function(e, idx) {
    e.preventDefault();
    e.stopPropagation();
    qCtxIdx     = idx;
    qCtxQueueId = (queue[idx] && queue[idx].queue_id != null) ? queue[idx].queue_id : null;
    var menu = document.getElementById('mp-q-ctx-menu');
    menu.style.display = 'block';
    var vw = window.innerWidth, vh = window.innerHeight;
    var mw = menu.offsetWidth || 170, mh = menu.offsetHeight || 80;
    menu.style.left = Math.min(e.clientX, vw - mw - 6) + 'px';
    menu.style.top  = Math.min(e.clientY, vh - mh - 6) + 'px';
};

document.addEventListener('click', function(e) {
    var m = document.getElementById('mp-q-ctx-menu');
    if (m && !m.contains(e.target)) { m.style.display = 'none'; }
});

window.qCtxPlayNow = function() {
    var m = document.getElementById('mp-q-ctx-menu');
    if (m) m.style.display = 'none';
    if (qCtxIdx >= 0) playerJumpTo(qCtxIdx);
};

window.qCtxMoveToTop = function() {
    var m = document.getElementById('mp-q-ctx-menu');
    if (m) m.style.display = 'none';
    if (qCtxQueueId === null || qCtxIdx <= 0) return;
    var item = queue[qCtxIdx];
    if (!item) return;
    /* We remove and re-add at position 0 */
    mc1Api('POST', '/app/api/player.php', {action: 'queue_remove', queue_id: qCtxQueueId}).then(function(d) {
        if (!d.ok) { mc1Toast(d.error || 'Move failed', 'err'); return; }
        return mc1Api('POST', '/app/api/player.php', {action: 'queue_add', track_id: item.track_id, position: 0});
    }).then(function(d) {
        if (d && d.ok) {
            queue = d.queue || [];
            if (currentIdx > 0) currentIdx++;
            renderQueue();
            mc1Toast('Moved to top', 'ok');
        }
    }).catch(function() { mc1Toast('Request failed', 'err'); });
};

window.qCtxRemove = function() {
    var m = document.getElementById('mp-q-ctx-menu');
    if (m) m.style.display = 'none';
    if (qCtxQueueId !== null) playerRemoveFromQueue(qCtxQueueId);
};

/* ── Utility ────────────────────────────────────────────────────────────── */
function fmtTime(sec) {
    sec = Math.floor(sec || 0);
    var m = Math.floor(sec / 60), s = sec % 60;
    return m + ':' + (s < 10 ? '0' : '') + s;
}

function _e(s) {
    return String(s || '').replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

/* ── Popout player window ───────────────────────────────────────────────── */
var mc1PlayerWin = null;

window.openPopoutPlayer = function() {
    /* We focus the existing window if it is still open */
    if (mc1PlayerWin && !mc1PlayerWin.closed) {
        mc1PlayerWin.focus();
        return;
    }
    mc1PlayerWin = window.open(
        '/mediaplayerpro.php',
        'mc1player',
        'width=1200,height=700,toolbar=no,location=no,menubar=no,status=no,scrollbars=no,resizable=yes'
    );
    if (!mc1PlayerWin) {
        mc1Toast('Popup blocked — allow popups for this site and try again', 'err');
    }
};

/* ── PHP preload: auto-add and play a track from URL param ──────────────── */
var PRELOAD_TRACK_ID = <?= $preload_track ? (int)$preload_track['id'] : 0 ?>;

/* ── DOMContentLoaded — We wire everything up after footer.php has loaded ── */
document.addEventListener('DOMContentLoaded', function() {
    /* We restore persisted player settings from localStorage */
    var savedVol = mc1State.get('player', 'volume', 80);
    audio.volume = savedVol / 100;
    volbar.value = savedVol;
    document.getElementById('mp-vol-pct').textContent = savedVol + '%';

    shuffle = mc1State.get('player', 'shuffle', false);
    document.getElementById('mp-btn-shuffle').classList.toggle('active', shuffle);

    repeatMode = mc1State.get('player', 'repeat', 0);
    var rBtn = document.getElementById('mp-btn-repeat');
    if (rBtn && repeatMode > 0) {
        rBtn.classList.add('active');
        var rIcon = rBtn.querySelector('i');
        if (rIcon && repeatMode === 1) { rBtn.title = 'Repeat: one'; rIcon.className = 'fa-solid fa-repeat-1'; }
        else if (rIcon) { rBtn.title = 'Repeat: all'; }
    }

    loadQueue();

    /* We auto-add and play the preload track if given in the URL */
    if (PRELOAD_TRACK_ID > 0) {
        mc1Api('POST', '/app/api/player.php', {action: 'queue_add', track_id: PRELOAD_TRACK_ID})
        .then(function(d) {
            if (d.ok) {
                queue = d.queue || [];
                renderQueue();
                /* We play the track we just added (last in queue) */
                playerJumpTo(queue.length - 1);
            } else {
                mc1Toast(d.error || 'Could not add track to queue', 'err');
            }
        }).catch(function() { mc1Toast('Could not load track', 'err'); });
    }
});

})(); /* end IIFE */
</script>

<?php require_once __DIR__ . '/app/inc/footer.php'; ?>
