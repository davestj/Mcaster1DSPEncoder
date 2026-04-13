<?php
/**
 * media_picker.php -- Reusable Media Library Picker Modal
 *
 * File:    src/linux/web_ui/app/inc/media_picker.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Purpose: We provide a reusable modal that any page can include to let the
 *          user browse the media library, search tracks, filter by type
 *          (audio/video/all), and select a track. The calling page receives
 *          the selected track via a JS callback.
 *
 * Usage:
 *   <?php require_once __DIR__ . '/app/inc/media_picker.php'; ?>
 *
 *   Then in JS:
 *   mc1MediaPicker.open({
 *       type: 'audio',          // 'audio', 'video', or 'all'
 *       onSelect: function(track) {
 *           console.log(track.id, track.title, track.file_path);
 *       }
 *   });
 *
 * Standards:
 *  - We never call exit() or die() -- uopz extension is active
 *  - We use first-person plural in all comments
 */
?>
<!-- Media Library Picker Modal -->
<div class="mc1-modal-bg" id="mc1-media-picker-bg">
  <div class="mc1-modal" style="width:min(780px,96vw);max-height:85vh">
    <div class="mc1-modal-hdr">
      <span class="mc1-modal-title"><i class="fa-solid fa-folder-open fa-fw" style="color:var(--teal)"></i> Media Library</span>
      <div style="display:flex;align-items:center;gap:8px">
        <select class="form-select" id="mc1-mp-type-filter" style="font-size:11px;padding:3px 8px;width:auto" onchange="mc1MediaPicker._filter()">
          <option value="all">All Files</option>
          <option value="audio">Audio Only</option>
          <option value="video">Video Only</option>
        </select>
        <button class="btn btn-secondary btn-xs" onclick="mc1MediaPicker.close()"><i class="fa-solid fa-xmark"></i></button>
      </div>
    </div>
    <div class="mc1-modal-body" style="padding:12px 16px">
      <div style="display:flex;gap:8px;margin-bottom:10px">
        <input type="text" class="form-input" id="mc1-mp-search" placeholder="Search tracks by title, artist, album..."
               style="flex:1;font-size:12px;padding:6px 10px" onkeydown="if(event.key==='Enter')mc1MediaPicker._search()">
        <button class="btn btn-primary btn-sm" onclick="mc1MediaPicker._search()"><i class="fa-solid fa-magnifying-glass"></i> Search</button>
      </div>
      <div id="mc1-mp-results" style="max-height:420px;overflow-y:auto;border:1px solid var(--border);border-radius:6px">
        <div style="padding:40px 20px;text-align:center;color:var(--muted)">
          <i class="fa-solid fa-magnifying-glass" style="font-size:28px;display:block;margin-bottom:10px"></i>
          Search for tracks or browse the library
        </div>
      </div>
      <div style="margin-top:8px;font-size:10px;color:var(--muted)" id="mc1-mp-status"></div>
    </div>
  </div>
</div>

<style>
.mc1-mp-row {
  display:grid; grid-template-columns:32px 1fr 120px 80px 60px;
  align-items:center; gap:8px; padding:6px 10px;
  cursor:pointer; border-bottom:1px solid rgba(51,65,85,.2);
  transition:background .1s; font-size:12px;
}
.mc1-mp-row:hover { background:rgba(255,255,255,.04); }
.mc1-mp-row.selected { background:rgba(20,184,166,.12); }
.mc1-mp-icon { text-align:center; font-size:14px; color:var(--muted); }
.mc1-mp-icon.video { color:var(--orange, #f97316); }
.mc1-mp-icon.audio { color:var(--teal); }
.mc1-mp-title { white-space:nowrap; overflow:hidden; text-overflow:ellipsis; color:var(--text); font-weight:500; }
.mc1-mp-artist { white-space:nowrap; overflow:hidden; text-overflow:ellipsis; color:var(--muted); font-size:11px; }
.mc1-mp-dur { text-align:right; color:var(--muted); font-size:11px; font-variant-numeric:tabular-nums; }
.mc1-mp-select-btn {
  padding:3px 10px; border-radius:4px; border:1px solid var(--teal);
  background:transparent; color:var(--teal); font-size:11px; cursor:pointer;
  transition:background .12s, color .12s;
}
.mc1-mp-select-btn:hover { background:var(--teal); color:#0f172a; }
</style>

<script>
/* ── mc1MediaPicker — reusable media library picker ─────────────────────── */
var mc1MediaPicker = (function() {
    var _opts = {};
    var _tracks = [];
    var VIDEO_EXT = /\.(mp4|webm|mkv|avi|mov|ogv)$/i;
    var AUDIO_EXT = /\.(mp3|ogg|flac|opus|aac|m4a|wav|wma|aiff|aif|ape)$/i;

    function _isVideo(fp) { return fp && VIDEO_EXT.test(fp); }

    function _fmtDur(ms) {
        if (!ms) return '--:--';
        var s = Math.floor(ms / 1000);
        var m = Math.floor(s / 60);
        s = s % 60;
        return m + ':' + (s < 10 ? '0' : '') + s;
    }

    function _esc(s) {
        var d = document.createElement('div');
        d.appendChild(document.createTextNode(s || ''));
        return d.innerHTML;
    }

    function _open(opts) {
        _opts = opts || {};
        var bg = document.getElementById('mc1-media-picker-bg');
        if (bg) bg.classList.add('open');
        /* We pre-set the type filter based on the caller's preference */
        var typeFilter = document.getElementById('mc1-mp-type-filter');
        if (typeFilter && _opts.type && _opts.type !== 'all') {
            typeFilter.value = _opts.type;
        } else if (typeFilter) {
            typeFilter.value = 'all';
        }
        document.getElementById('mc1-mp-search').value = '';
        document.getElementById('mc1-mp-search').focus();
        /* We auto-load recent tracks on open */
        _doSearch('');
    }

    function _close() {
        var bg = document.getElementById('mc1-media-picker-bg');
        if (bg) bg.classList.remove('open');
        _opts = {};
    }

    function _doSearch(q) {
        var resultsEl = document.getElementById('mc1-mp-results');
        resultsEl.innerHTML = '<div style="padding:30px;text-align:center;color:var(--muted)"><i class="fa-solid fa-spinner fa-spin"></i> Searching...</div>';
        var payload = q ? {action:'list', q:q, limit:100, page:1} : {action:'list', limit:100, page:1};
        mc1Api('POST', '/app/api/tracks.php', payload).then(function(d) {
            var tracks = d.data || d.tracks || [];
            _tracks = tracks;
            _render(tracks);
        }).catch(function() {
            resultsEl.innerHTML = '<div style="padding:30px;text-align:center;color:var(--red,#ef4444)">Search failed</div>';
        });
    }

    function _search() {
        var q = (document.getElementById('mc1-mp-search').value || '').trim();
        _doSearch(q);
    }

    function _filter() {
        _render(_tracks);
    }

    function _render(tracks) {
        var resultsEl = document.getElementById('mc1-mp-results');
        var statusEl  = document.getElementById('mc1-mp-status');
        var typeVal   = (document.getElementById('mc1-mp-type-filter') || {}).value || 'all';

        var filtered = tracks.filter(function(t) {
            var fp = t.file_path || '';
            if (typeVal === 'video') return _isVideo(fp);
            if (typeVal === 'audio') return !_isVideo(fp);
            return true;
        });

        if (filtered.length === 0) {
            resultsEl.innerHTML = '<div style="padding:40px 20px;text-align:center;color:var(--muted)"><i class="fa-solid fa-inbox" style="font-size:24px;display:block;margin-bottom:8px"></i>No tracks found</div>';
            statusEl.textContent = '0 results';
            return;
        }

        var html = '';
        filtered.forEach(function(t, i) {
            var isVid = _isVideo(t.file_path || '');
            var iconCls = isVid ? 'video' : 'audio';
            var icon = isVid ? 'fa-solid fa-film' : 'fa-solid fa-music';
            html += '<div class="mc1-mp-row" data-idx="' + i + '" ondblclick="mc1MediaPicker._select(' + i + ')">'
                + '<div class="mc1-mp-icon ' + iconCls + '"><i class="' + icon + '"></i></div>'
                + '<div><div class="mc1-mp-title">' + _esc(t.title || t.file_path || '') + '</div>'
                + '<div class="mc1-mp-artist">' + _esc(t.artist || '') + (t.album ? ' - ' + _esc(t.album) : '') + '</div></div>'
                + '<div class="mc1-mp-artist">' + _esc(t.genre || '') + '</div>'
                + '<div class="mc1-mp-dur">' + _fmtDur(t.duration_ms) + '</div>'
                + '<div><button class="mc1-mp-select-btn" onclick="mc1MediaPicker._select(' + i + ')">Select</button></div>'
                + '</div>';
        });
        resultsEl.innerHTML = html;
        statusEl.textContent = filtered.length + ' result' + (filtered.length !== 1 ? 's' : '');

        /* We store the filtered list for selection */
        resultsEl._filtered = filtered;
    }

    function _select(idx) {
        var resultsEl = document.getElementById('mc1-mp-results');
        var filtered = resultsEl._filtered || _tracks;
        var track = filtered[idx];
        if (!track) return;
        if (typeof _opts.onSelect === 'function') {
            _opts.onSelect(track);
        }
        _close();
    }

    return {
        open: _open,
        close: _close,
        _search: _search,
        _filter: _filter,
        _select: _select
    };
})();
</script>
